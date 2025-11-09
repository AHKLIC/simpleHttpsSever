package model

import (
	"database/sql/driver"
	"encoding/json"
	"fmt"
	"sort"
	"sync"
	"time"

	"github.com/zsh/go_updater/pkg/db"
	"gorm.io/gorm"
)

// 1. 定义自定义JSON切片类型，支持数据库读写
type JSONUint32Slice []uint32

// Scan 实现sql.Scanner接口，将数据库JSON字节流转换为JSONUint32Slice
func (j *JSONUint32Slice) Scan(value interface{}) error {
	// 数据库中存储的是JSON格式的字节流（[]uint8）
	bytes, ok := value.([]byte)
	if !ok {
		return fmt.Errorf("无法将 %T 转换为 []byte", value)
	}

	// 反序列化JSON字节流到[]uint32
	var slice []uint32
	if err := json.Unmarshal(bytes, &slice); err != nil {
		return fmt.Errorf("JSON反序列化失败: %w", err)
	}

	*j = JSONUint32Slice(slice)
	return nil
}
func (j JSONUint32Slice) Value() (driver.Value, error) {
	// 序列化切片为JSON字节流
	return json.Marshal(j)
}

// Reluser 映射到 rel_users 表
type Reluser struct {
	Id      uint32          `gorm:"column:Id;primaryKey"`
	Userid  uint32          `gorm:"column:Userid"`
	Uidlist JSONUint32Slice `gorm:"column:Reluserid;type:json"` // JSON数组存储关联用户ID
}

var (
	WriteRelUserDb  *gorm.DB
	relUserUidLocks sync.Map
)

func (Reluser) TableName() string {
	return "rel_user"
}
func InitWriteRelUser() error {
	dbM := db.NewDbManage("root:123456@tcp(localhost:3306)/comdb?charset=utf8mb4&parseTime=True&loc=Local")
	db, err := dbM.GetDb()
	if err != nil {
		return err
	}
	WriteRelUserDb = db
	return nil
}

// 定义切片结构存储用户ID和对应的相似度
type relUserItem struct {
	UserID     uint32
	Similarity float32
}

func BatchGetRelUsers(RelUserCh chan<- *Reluser, startCh <-chan struct{}, stopCh <-chan struct{}, closeOnce *sync.Once) error {
	db := WriteRelUserDb
	offset := 0                 // 偏移量
	batchSize := cap(RelUserCh) // 每次查询数量
	isFirstRun := true          // 是否首次运行
	isStop := false
	err := error(nil)

	defer func() {
		// 捕获panic
		if r := recover(); r != nil {
			// 判断是否为"向已关闭通道发送数据"的错误
			if errStr, ok := r.(string); ok && errStr == "send on closed channel" {
				// 只处理通道关闭错误，记录日志后忽略
				err = r.(error)
				return
			}
			// 其他类型的panic重新抛出
			panic(r)
		}
	}()

outLoop0:
	for !isStop {
		// 首次运行直接开始，非首次需要等待"开始"信号
		if !isFirstRun {
			select {
			case <-startCh:
				// 收到开始信号，继续查询
			case <-stopCh:
				// 收到停止信号，退出并关闭通道
				isStop = true
				break
			}
		}
		isFirstRun = false // 首次运行标志置为false

		// 执行批量查询
		var relusers []*Reluser
		result := db.Offset(offset).Limit(batchSize).Find(&relusers)
		if result.Error != nil && result.Error != gorm.ErrRecordNotFound {
			isStop = true      // 查询出错，停止查询
			err = result.Error // 记录错误
			break
		}

		// 写入通道
		for _, reluser := range relusers {
			select {
			case <-stopCh:
				isStop = true
				break outLoop0
			default:
			}

			select {
			case RelUserCh <- reluser: // 写入数据
			case <-stopCh: // 中途收到停止信号
				isStop = true // 停止查询
				break outLoop0
			}
		}

		// 更新偏移量
		if len(relusers) < batchSize {
			offset = 0 // 不足10条，重置偏移量
			time.Sleep(1000 * time.Millisecond)
		} else {
			offset += batchSize // 足够10条，偏移量递增
		}

		// 检查是否需要彻底停止（在写入完一批后）
		select {
		case <-stopCh:
			isStop = true // 收到停止信号，退出循环
		default:
			// 未收到停止信号，等待下一次开始信号
		}
	}

	closeOnce.Do(func() {
		close(RelUserCh)
	})
	return err
}

func WriteRelUser(uid uint32, relUserMap map[uint32]float32) error {
	// 获取当前uid的锁（不存在则创建）
	lock, _ := relUserUidLocks.LoadOrStore(uid, &sync.Mutex{})
	mu := lock.(*sync.Mutex)
	mu.Lock()         // 同一uid的协程排队
	defer mu.Unlock() // 确保释放锁

	if WriteRelUserDb == nil {
		return gorm.ErrInvalidDB
	}
	if len(relUserMap) == 0 {
		return nil
	}

	var relUserList []relUserItem
	for userID, similarity := range relUserMap {
		if similarity <= 0 {
			continue
		}
		relUserList = append(relUserList, relUserItem{
			UserID:     userID,
			Similarity: similarity,
		})
	}

	sort.Slice(relUserList, func(i, j int) bool {
		return relUserList[i].Similarity > relUserList[j].Similarity
	})
	if len(relUserList) == 0 || relUserList[0].Similarity <= 0 {
		return nil
	}

	topCount := 10
	if len(relUserList) < topCount {
		topCount = len(relUserList)
	}
	topRelUserIDs := make([]uint32, 0, topCount)
	for _, item := range relUserList[:topCount] {
		topRelUserIDs = append(topRelUserIDs, item.UserID)
	}
	tx := WriteRelUserDb.Begin()
	if tx.Error != nil {
		return tx.Error
	}
	defer func() {
		if r := recover(); r != nil {
			tx.Rollback()
		}
	}()
	var existingRelUser Reluser
	query := tx.Where("Userid = ?", uid).First(&existingRelUser)
	switch {
	case query.Error == gorm.ErrRecordNotFound:
		newRelUser := Reluser{
			Userid:  uid,
			Uidlist: JSONUint32Slice(topRelUserIDs),
		}
		if err := tx.Create(&newRelUser).Error; err != nil {
			tx.Rollback() // 错误时回滚
			return fmt.Errorf("插入失败: %w", err)
		}
	case query.Error != nil:
		tx.Rollback()
		return fmt.Errorf("查询失败: %w", query.Error)
	default:
		existingRelUser.Uidlist = JSONUint32Slice(topRelUserIDs)
		if err := tx.Save(&existingRelUser).Error; err != nil {
			tx.Rollback()
			return fmt.Errorf("更新失败: %w", err)
		}
	}

	// 6. 提交事务（在锁释放前完成）
	if err := tx.Commit().Error; err != nil {
		tx.Rollback()
		return err
	}
	return nil
}
