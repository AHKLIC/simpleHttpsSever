package model

import (
	"context"
	"fmt"
	"runtime"
	"sort"
	"sync"

	"github.com/zsh/go_updater/pkg/db"
	"gorm.io/gorm"
)

// Comvideo 映射到 com_videos 表
type Comvideo struct {
	Id      uint32          `gorm:"column:Id;primaryKey"`    // 字段名映射：结构体Id → 表中com_id
	Userid  uint32          `gorm:"column:Userid"`           // 结构体Userid → 表中user_id
	Vidlist JSONUint32Slice `gorm:"column:Comvid;type:json"` // 存储JSON数组
}

var (
	WriteComvideoDb  *gorm.DB
	comvideoUidLocks sync.Map
)

func (Comvideo) TableName() string {
	return "comvideo"
}
func InitWriteComvideo() error {
	dbM := db.NewDbManage("root:123456@tcp(localhost:3306)/comdb?charset=utf8mb4&parseTime=True&loc=Local")
	db, err := dbM.GetDb()
	if err != nil {
		return err
	}
	WriteComvideoDb = db
	return nil

}

// reluser.Userid为目标用户reluser.Uidlist关联用户  batchData.userVideoMapGlobal为当前批次的所有用户观看的视频的映射
func CalculateComvideo(ctx context.Context, reluser *Reluser, batchData *BatchData) error {
	muResultComvideoList := &sync.Mutex{}
	err := error(nil)
	uid := reluser.Userid
	tuidList := reluser.Uidlist
	batchData.muUserVideoMapGlobal.RLock()
	_, exists := batchData.userVideoMapGlobal[uid]
	batchData.muUserVideoMapGlobal.RUnlock()
	if !exists {
		return fmt.Errorf("用户ID %d 无观看记录", uid)
	}
	closeOnce := &sync.Once{}
	resultComvideoList := make([]Videoinf, 0, len(tuidList)*6) // 预估容量
	var wg sync.WaitGroup

	cpuNum := runtime.NumCPU()
	workerNum := cpuNum / 4                //test
	taskCh := make(chan uint32, workerNum) // 任务通道（缓冲大小=协程数）

	// 2. 启动协程池
	for i := 0; i < workerNum; i++ {
		go func() {
			for {
				select {
				case <-ctx.Done():
					return
				case tUid, ok := <-taskCh:
					if !ok {
						return
					}

					// 处理任务前再次检查取消（避免已取消但任务仍执行）
					select {
					case <-ctx.Done():
						wg.Done() // 释放计数
						return
					default:
					}

					comvideoList := CalculateComvideoList(uid, tUid, batchData)

					// 写入结果前检查取消
					select {
					case <-ctx.Done():
						wg.Done()
						return
					default:
					}

					muResultComvideoList.Lock()
					resultComvideoList = append(resultComvideoList, comvideoList...)
					muResultComvideoList.Unlock()
					wg.Done()
				}
			}
		}()
	}

	// 3. 向任务通道发送目标用户ID（任务）
	for _, targetUid := range tuidList {
		if targetUid == uid {
			continue
		}
		wg.Add(1)
		select {
		case <-ctx.Done():
			closeOnce.Do(func() { // 仅第一次调用时执行关闭
				close(taskCh)
			})
			wg.Wait()
			return ctx.Err()
		case taskCh <- targetUid:
		}
	}

	// 4. 关闭任务通道 + 等待协程完成
	closeOnce.Do(func() { // 替换直接 close(taskCh)
		close(taskCh)
	})
	wg.Wait()
	err = WriteComvideo(uid, resultComvideoList)
	if err != nil {
		return fmt.Errorf("WriteComvideo err=%v", err)
	}
	return nil
}

func CalculateComvideoList(uid uint32, tuid uint32, batchData *BatchData) []Videoinf {

	batchData.muUserVideoMapGlobal.RLock()

	uidVideos := batchData.userVideoMapGlobal[uid]   // 当前用户uid的视频列表
	tuidVideos := batchData.userVideoMapGlobal[tuid] // 目标用户tuid的视频列表
	batchData.muUserVideoMapGlobal.RUnlock()
	uidWatched := make(map[uint32]struct{}, len(uidVideos))
	for _, video := range uidVideos {
		uidWatched[video.Videoid] = struct{}{}
	}

	//  筛选tuid中符合条件的视频：
	//    - uid未观看（不在uidWatched集合中）
	//    - 评分score > 60
	var result []Videoinf
	sort.Slice(tuidVideos, func(i, j int) bool {
		return tuidVideos[i].Score > tuidVideos[j].Score
	})
	for _, video := range tuidVideos {
		// 检查评分是否大于60
		if video.Score < 60 {
			break
		}
		if _, exists := uidWatched[video.Videoid]; !exists {
			result = append(result, video)
		}
	}

	//  转换为JSONUint32Slice类型返回（适配数据库JSON字段）
	return result
}
func deduplicateAndSort(comvideoList []Videoinf) []Videoinf {
	// 1. 去重：使用 map 记录已出现的 Videoid
	seen := make(map[uint32]struct{}, len(comvideoList)) // 预分配容量，提升性能
	uniqueList := make([]Videoinf, 0, len(comvideoList))

	for _, video := range comvideoList {
		// 只保留首次出现的 Videoid
		if _, exists := seen[video.Videoid]; !exists {
			seen[video.Videoid] = struct{}{}
			uniqueList = append(uniqueList, video)
		}
	}

	// 2. 按 Score 降序排序（若分数相同，可按 UpdateTime 或 Videoid 辅助排序）
	sort.Slice(uniqueList, func(i, j int) bool {
		// 主条件：Score 降序
		if uniqueList[i].Score != uniqueList[j].Score {
			return uniqueList[i].Score > uniqueList[j].Score
		}
		// 次要条件（可选）：若分数相同，按更新时间降序（保留最新的）
		return uniqueList[i].UpdateTime.After(uniqueList[j].UpdateTime)
	})

	return uniqueList
}
func WriteComvideo(uid uint32, comvideoList []Videoinf) error {
	lock, _ := comvideoUidLocks.LoadOrStore(uid, &sync.Mutex{})
	mu := lock.(*sync.Mutex)
	mu.Lock()         // 同一uid的协程排队
	defer mu.Unlock() // 确保释放锁
	if len(comvideoList) == 0 {
		return nil
	}

	uniqueList := deduplicateAndSort(comvideoList)
	topCount := min(len(uniqueList), 60)
	topComvideoIDs := make(JSONUint32Slice, 0, topCount)
	for _, item := range uniqueList[:topCount] {
		topComvideoIDs = append(topComvideoIDs, item.Videoid)
	}

	tx := WriteComvideoDb.Begin()
	if tx.Error != nil {
		return tx.Error
	}
	defer func() {
		if r := recover(); r != nil {
			tx.Rollback()
		}
	}()
	comvideo := &Comvideo{}
	query := WriteComvideoDb.Where("userid = ?", uid).First(comvideo)
	switch {
	case query.Error == gorm.ErrRecordNotFound:
		newComvideo := Comvideo{
			Userid:  uid,
			Vidlist: topComvideoIDs,
		}
		if err := tx.Create(&newComvideo).Error; err != nil {
			tx.Rollback()
			return err
		}
	case query.Error != nil:
		tx.Rollback()
		return query.Error
	default:
		comvideo.Vidlist = topComvideoIDs
		if err := tx.Save(comvideo).Error; err != nil {
			tx.Rollback()
			return err
		}
	}
	if err := tx.Commit().Error; err != nil {
		return err
	}
	return nil

}
