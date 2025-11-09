package work

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/zsh/go_updater/internal/model"
	"github.com/zsh/go_updater/pkg/db"
)

// 入口 RelUserWork 协程的作用是把BatchGetUsers协程从usertable循环查到userid交给CalculateRelUid处理
func RelUserWork(wg *sync.WaitGroup, ctxMain context.Context) error {
	ctx, cancel := context.WithCancel(context.Background())
	defer func() {
		if r := recover(); r != nil {
			fmt.Println("Recovered in Work", r)
		}
		cancel() //停止所有协程
		wg.Done()

	}()

	url := "root:123456@tcp(localhost:3306)/mhwnet?charset=utf8mb4&parseTime=True&loc=Local"
	dbM := db.NewDbManage(url)
	db, err := dbM.GetDb()
	handleWg := &sync.WaitGroup{}
	if err == nil {
		cotrolClose := &sync.Once{}
		userCh := make(chan *model.User, 10)
		startCh := make(chan struct{})
		stopCh := make(chan struct{})
		defer func() {
			close(startCh)
		}()
		count := 0
		go model.BatchGetUsers(db, userCh, startCh, stopCh, cotrolClose)
		batchdata := &model.BatchData{}
		err = model.UpdateBatchData(db, batchdata)
		if err != nil {
			fmt.Println("UpdateBatchData in RelUserWork err=", err)
			cotrolClose.Do(func() { close(stopCh) })
			return err
		}
		for {
			// 先尝试读取所有现有数据（非阻塞）
			hasData := false
		loopInner:
			for {
				select {
				case <-ctxMain.Done():
					cotrolClose.Do(func() { close(stopCh) })
					return err
				case user, ok := <-userCh:
					if !ok {
						fmt.Println("userCh通道已关闭,退出循环")
						return err // 通道关闭，退出
					}
					hasData = true
					count++
					handleWg.Add(1)
					go func(u *model.User) {
						defer handleWg.Done()
						err := model.CalculateRelUid(ctx, db, u.Userid, batchdata)
						if err != nil {
							// fmt.Println("CalculateRelUid in RelUserWork err=", err)
						}
					}(user)

					// 检查阈值
					select {
					case <-ctxMain.Done():
						cotrolClose.Do(func() { close(stopCh) })
						return err
					default:
					}

				default:
					// 无数据，退出内层循环
					break loopInner
				}
			}

			// 若本轮没有处理数据，发送信号并等待处理完成
			if !hasData {
				startCh <- struct{}{}
				handleWg.Wait() // 等待上一轮处理完成
				err = model.UpdateBatchData(db, batchdata)
				if err != nil {
					fmt.Println("UpdateBatchData err=", err)
					time.Sleep(1 * time.Second)
				}
			}

			// 适当休眠，避免空轮询（根据业务调整间隔）
			time.Sleep(10 * time.Millisecond)
		}

	}
	return err

}
