package db

import (
	"fmt"
	"sync"
	"time"

	"github.com/zsh/go_updater/pkg/logger"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
)

type DbManage struct {
	url  string
	db   *gorm.DB
	once sync.Once
	err  error // 用于记录初始化过程中的错误
}

// NewDbManage 是 DbManage 的构造函数
func NewDbManage(Url string) *DbManage {
	return &DbManage{url: Url}
}

// IntiDb 初始化数据库连接（改为返回错误而不是panic）
func (dbM *DbManage) IntiDb() error {
	customLogger := logger.NewCustomLogger(logger.Error)

	db, err := gorm.Open(mysql.Open(dbM.url), &gorm.Config{
		Logger: customLogger,
	})
	if err != nil {
		return fmt.Errorf("数据库连接失败: %w", err) // 返回错误而非panic
	}

	sqlDB, err := db.DB()
	if err != nil {
		return fmt.Errorf("获取底层DB失败: %w", err)
	}

	// 配置连接池
	sqlDB.SetMaxIdleConns(10)
	sqlDB.SetMaxOpenConns(100)
	sqlDB.SetConnMaxLifetime(time.Hour)

	dbM.db = db
	fmt.Println("数据库连接池初始化成功！")
	return nil
}

// GetDb 获取数据库连接，确保初始化只执行一次，并捕获错误
func (dbM *DbManage) GetDb() (*gorm.DB, error) {
	dbM.once.Do(func() {
		// 在匿名函数中执行初始化，并捕获可能的错误（包括panic）
		defer func() {
			if r := recover(); r != nil {
				// 捕获panic并转为错误
				dbM.err = fmt.Errorf("初始化过程发生panic: %v", r)
			}
		}()

		// 执行初始化，如果返回错误则记录
		if err := dbM.IntiDb(); err != nil {
			dbM.err = err
		}
	})

	// 返回初始化结果和错误
	return dbM.db, dbM.err
}
