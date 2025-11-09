package model

import (
	"sync"
	"time"

	"gorm.io/gorm"
)

// User 映射到 usertable 表（只映射需要的字段）
type User struct {
	Userid uint32 `gorm:"column:userid;primaryKey"`
	// 其他字段不定义，GORM会忽略表中多余字段
}

func (User) TableName() string {
	return "usertable" // 映射到指定表名
}

// BatchGetUsers 可控制的批量查询函数
// userCh: 用于接收用户数据的通道（缓冲50）
// startCh: 开始/继续查询的信号通道（发送任意值触发查询）
// stopCh: 彻底停止查询的信号通道
func BatchGetUsers(db *gorm.DB, userCh chan<- *User, startCh <-chan struct{}, stopCh <-chan struct{}, closeOnce *sync.Once) error {
	offset := 0              // 偏移量
	batchSize := cap(userCh) // 每次查询数量
	isFirstRun := true       // 是否首次运行
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
		var users []*User
		result := db.Offset(offset).Limit(batchSize).Find(&users)
		if result.Error != nil && result.Error != gorm.ErrRecordNotFound {
			isStop = true      // 查询出错，停止查询
			err = result.Error // 记录错误
			break
		}

		// 写入通道
		for _, user := range users {
			select {
			case <-stopCh:
				isStop = true
				break outLoop0
			default:
			}

			select {
			case userCh <- user: // 写入数据
			case <-stopCh: // 中途收到停止信号
				isStop = true // 停止查询
				break outLoop0
			}
		}

		// 更新偏移量
		if len(users) < batchSize {
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
		close(userCh)
	})
	return err
}
