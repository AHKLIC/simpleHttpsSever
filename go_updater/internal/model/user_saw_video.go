package model

import (
	"context"
	"fmt"
	"math"
	"runtime"
	"sync"
	"time"

	"gorm.io/gorm"
)

// SawVideo 映射到 saw_videos 表（注意结构体名去掉下划线，符合Go命名规范）
type SawVideo struct {
	Id         uint32    `gorm:"column:id;primaryKey"`
	Userid     uint32    `gorm:"column:userid"`
	Videoid    uint32    `gorm:"column:videoid"`
	Score      int       `gorm:"column:score"`
	UpdateTime time.Time `gorm:"column:update_time"`
}

type UserSawVideo struct {
	Userid   uint32 `gorm:"column:userid;primaryKey"`
	VideoInf []Videoinf
}

type Videoinf struct {
	Videoid    uint32    `gorm:"column:videoid"`
	Score      int       `gorm:"column:score"`
	UpdateTime time.Time `gorm:"column:update_time"`
}

func (SawVideo) TableName() string {
	return "user_saw_video"
}

type BatchData struct {
	userVideoMapGlobal   map[uint32][]Videoinf
	muUserVideoMapGlobal sync.RWMutex
	pearsonMapGlobal     map[[2]uint32]float32
	muPearsonMapGlobal   sync.RWMutex
	jaccardMapGlobal     map[[2]uint32]float32
	muJaccardMapGlobal   sync.RWMutex
	once                 sync.Once
}

func (b *BatchData) Init() {
	b.once.Do(func() {
		b.userVideoMapGlobal = make(map[uint32][]Videoinf)
		b.pearsonMapGlobal = make(map[[2]uint32]float32)
		b.jaccardMapGlobal = make(map[[2]uint32]float32)
	})

}
func (b *BatchData) normalizeKey(uid1, uid2 uint32) [2]uint32 {
	if uid1 <= uid2 {
		return [2]uint32{uid1, uid2}
	}
	return [2]uint32{uid2, uid1}
}
func (b *BatchData) GetPearson(uid1, uid2 uint32) (float32, bool) {
	key := b.normalizeKey(uid1, uid2)
	b.muPearsonMapGlobal.RLock()
	value, exists := b.pearsonMapGlobal[key]
	b.muPearsonMapGlobal.RUnlock()
	return value, exists
}
func (b *BatchData) SetPearson(uid1, uid2 uint32, value float32) {
	key := b.normalizeKey(uid1, uid2)
	b.muPearsonMapGlobal.Lock()
	b.pearsonMapGlobal[key] = value
	b.muPearsonMapGlobal.Unlock()
}
func (b *BatchData) GetJaccard(uid1, uid2 uint32) (float32, bool) {
	key := b.normalizeKey(uid1, uid2)
	b.muJaccardMapGlobal.RLock()
	value, exists := b.jaccardMapGlobal[key]
	b.muJaccardMapGlobal.RUnlock()
	return value, exists
}
func (b *BatchData) SetJaccard(uid1, uid2 uint32, value float32) {
	key := b.normalizeKey(uid1, uid2)
	b.muJaccardMapGlobal.Lock()
	b.jaccardMapGlobal[key] = value
	b.muJaccardMapGlobal.Unlock()
}

func UpdateBatchData(db *gorm.DB, batchData *BatchData) error {
	batchData.Init()
	err := error(nil)
	batchData.userVideoMapGlobal, err = BuildUserVideoMapOptimized(db)
	if err != nil {
		return fmt.Errorf("构建用户视频映射失败: %v", err)
	}
	batchData.pearsonMapGlobal = make(map[[2]uint32]float32)
	batchData.jaccardMapGlobal = make(map[[2]uint32]float32)
	return err
}

// CalculateRelUid 独立协程调用入口，每一批调用共用一个userVideoMap
// 参数：db-数据库连接，uid-目标用户ID
// 返回：相似用户映射（用户ID->相似度）和错误信息
func CalculateRelUid(ctx context.Context, db *gorm.DB, uid uint32, batchData *BatchData) error {
	// 1. 为当前调用创建独立的用户视频映射
	muRelUserMap := &sync.Mutex{}
	err := error(nil)
	batchData.muUserVideoMapGlobal.RLock()
	userVideoMap := batchData.userVideoMapGlobal
	batchData.muUserVideoMapGlobal.RUnlock()

	// 2. 检查目标用户是否存在
	if _, exists := userVideoMap[uid]; !exists {
		return fmt.Errorf("用户ID %d 无观看记录", uid)
	}

	relUserMap := make(map[uint32]float32, len(userVideoMap)-1)
	closeOnce := &sync.Once{}
	var wg sync.WaitGroup

	// 1. 确定协程池大小：默认 CPU 核心数 * 2（平衡并行度和资源消耗）
	cpuNum := runtime.NumCPU()
	workerNum := cpuNum                    //test
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
					var similarity float32
					Pearson, okP := batchData.GetPearson(uid, tUid)
					Jaccard, okJ := batchData.GetJaccard(uid, tUid)
					if okP && okJ {
						similarity = 0.6*Jaccard + 0.4*Pearson
					} else { // 执行计算
						similarity = calculateRelPercent(uid, tUid, userVideoMap, batchData)
					}
					// 写入结果前检查取消
					select {
					case <-ctx.Done():
						wg.Done()
						return
					default:
					}

					muRelUserMap.Lock()
					relUserMap[tUid] = similarity
					muRelUserMap.Unlock()
					wg.Done()
				}
			}
		}()
	}

	// 3. 向任务通道发送目标用户ID（任务）
	for targetUid := range userVideoMap {
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
	err = WriteRelUser(uid, relUserMap)
	if err != nil {
		return fmt.Errorf("WriteRelUser err=%v", err)
	}
	return nil
}

// buildUserVideoMap 为当前调用构建独立的用户视频映射（用户ID->视频列表）
func BuildUserVideoMapOptimized(db *gorm.DB) (map[uint32][]Videoinf, error) {
	// 1. 执行原生SQL查询，按userid分组获取所有视频信息
	rows, err := db.Raw(`
		SELECT userid, videoid, score, update_time
		FROM user_saw_video
		ORDER BY userid, update_time DESC
	`).Rows()
	if err != nil {
		return nil, fmt.Errorf("执行查询失败: %w", err)
	}
	defer rows.Close()

	userVideoMap := make(map[uint32][]Videoinf)
	var (
		uid        uint32
		videoid    uint32
		score      int
		updateTime time.Time
	)

	// 2. 遍历结果集，按userid组装videoinf切片
	for rows.Next() {
		// 扫描当前行数据
		if err := rows.Scan(&uid, &videoid, &score, &updateTime); err != nil {
			return nil, fmt.Errorf("扫描行失败: %w", err)
		}

		// 初始化用户的视频列表（若不存在）
		if _, exists := userVideoMap[uid]; !exists {
			userVideoMap[uid] = make([]Videoinf, 0)
		}

		// 添加视频信息到对应用户
		userVideoMap[uid] = append(userVideoMap[uid], Videoinf{
			Videoid:    videoid,
			Score:      score,
			UpdateTime: updateTime,
		})
	}

	// 检查遍历过程中是否发生错误
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("遍历结果集失败: %w", err)
	}

	return userVideoMap, nil
}

// calculateRelPercent 基于jaccard和皮尔逊相关系数计算相似度
func calculateRelPercent(uid1, uid2 uint32, userVideoMap map[uint32][]Videoinf, batchData *BatchData) float32 {
	// 1. 获取两个用户的视频列表
	videos1 := userVideoMap[uid1]
	videos2 := userVideoMap[uid2]

	// 处理无观看记录的情况
	if len(videos1) == 0 || len(videos2) == 0 {
		return 0.0
	}
	// 2. 构建视频ID到评分的映射，同时收集所有视频ID
	vidScoreMap1 := make(map[uint32]int) // 视频ID -> 评分
	vidScoreMap2 := make(map[uint32]int)
	allVids := make(map[uint32]struct{}) // 所有涉及的视频ID

	for _, v := range videos1 {
		vidScoreMap1[v.Videoid] = v.Score
		allVids[v.Videoid] = struct{}{}
	}
	for _, v := range videos2 {
		vidScoreMap2[v.Videoid] = v.Score
		allVids[v.Videoid] = struct{}{}
	}

	// 3. 计算视频ID相似度（基于共同观看）
	commonCount, totalCount := 0, 0
	for vid := range allVids {
		has1 := false
		has2 := false

		if _, ok := vidScoreMap1[vid]; ok {
			has1 = true
			totalCount++
		}
		if _, ok := vidScoreMap2[vid]; ok {
			has2 = true
			if !has1 { // 避免重复计算
				totalCount++
			}
		}

		if has1 && has2 {
			commonCount++
		}
	}

	// 无共同视频时相似度为0
	if commonCount == 0 {
		return 0.0
	}
	// 计算Jaccard相似度（共同视频比例）
	jaccard := float64(commonCount) / float64(totalCount)
	batchData.SetJaccard(uid1, uid2, float32(jaccard)) //缓存jaccard相似度
	// ------------------------------------------------------------------------------------------------------------------------------------------
	var pearson float64
	// 4. 计算共同视频的评分皮尔逊系数（增加评分阈值处理）
	var (
		sumX, sumY     int // 评分总和（可能经过阈值处理）
		sumXSq, sumYSq int // 评分平方和
		sumXY          int // 评分乘积和
		n              int // 共同视频数量
	)

	// 遍历共同视频，收集评分数据（增加阈值处理逻辑）
	for vid := range vidScoreMap1 {
		score2, hasCommon := vidScoreMap2[vid]
		if !hasCommon {
			continue
		}
		score1 := vidScoreMap1[vid]

		// 核心逻辑：当两者评分都大于300时，视为评分相同
		// 这里将其统一处理为300（也可根据业务改为其他值）
		if score1 > 300 && score2 > 300 {
			score1 = 300
			score2 = 300
		}

		// 累加统计量
		sumX += score1
		sumY += score2
		sumXSq += score1 * score1
		sumYSq += score2 * score2
		sumXY += score1 * score2
		n++
	}

	// 计算皮尔逊系数（处理评分相似度）
	if n > 0 {
		numerator := sumXY - (sumX*sumY)/n
		denominator := math.Sqrt(float64((sumXSq - sumX*sumX/n) * (sumYSq - sumY*sumY/n)))

		if denominator == 0 {
			// 评分方差为0时，使用默认中等相似度0.5
			pearson = 0.5
		} else {
			// 标准化到[0,1]范围（原始皮尔逊范围为[-1,1]）
			pearson = (float64(numerator)/denominator + 1) / 2
		}
	} else {
		// 理论上不会走到这里，因为commonCount>0
		pearson = 0.0
	}
	batchData.SetPearson(uid1, uid2, float32(pearson)) //缓存pearson相似度

	// 5. 综合相似度：视频ID相似度占60%，评分相似度占40%
	// 权重可根据业务需求调整
	videoWeight := 0.6
	scoreWeight := 0.4
	totalSimilarity := jaccard*videoWeight + pearson*scoreWeight

	// 确保结果在[0,1]范围内
	if totalSimilarity < 0 {
		totalSimilarity = 0
	} else if totalSimilarity > 1 {
		totalSimilarity = 1
	}

	return float32(totalSimilarity)
}
