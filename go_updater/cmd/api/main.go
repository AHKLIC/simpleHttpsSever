package main

import (
	"fmt"

	"context"
	"sync"

	"github.com/zsh/go_updater/cmd/api/work"
	"github.com/zsh/go_updater/internal/model"
)

func main() {
	ctxMain, cancel := context.WithCancel(context.Background())
	defer cancel()

	err := model.InitWriteRelUser()
	if err != nil {
		fmt.Println("InitWriteRelUser err=", err)
		return
	}
	err = model.InitWriteComvideo()
	if err != nil {
		fmt.Println("InitWriteComvideo err", err)
	}
	workreluserWg := &sync.WaitGroup{}
	workcomvideoWg := &sync.WaitGroup{}
	workreluserWg.Add(1)
	go work.RelUserWork(workreluserWg, ctxMain)
	workcomvideoWg.Add(1)
	go work.ComvideoWork(workcomvideoWg, ctxMain)
	workreluserWg.Wait()
	workcomvideoWg.Wait()
	fmt.Println("所有work协程已完成")
	fmt.Println("所有comvideo协程已完成")
}
