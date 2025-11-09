package logger

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"time"

	"gorm.io/gorm/logger"
)

// 自定义日志级别常量（与GORM兼容）
const (
	Silent logger.LogLevel = iota // 静默模式，不输出任何日志
	Error                         // 仅输出错误日志
	Warn                          // 输出警告和错误日志
	Info                          // 输出信息、警告和错误日志
)

// 自定义日志器结构体
type CustomLogger struct {
	LogLevel logger.LogLevel // 日志级别
	Writer   io.Writer       // 日志输出目标（默认stdout）
	Prefix   string          // 日志前缀
	// 可以添加更多配置，如是否显示颜色、是否输出到文件等
	EnableColor bool // 是否启用彩色输出
}

// 新建自定义日志器
func NewCustomLogger(level logger.LogLevel) *CustomLogger {
	return &CustomLogger{
		LogLevel:    level,
		Writer:      os.Stdout,
		Prefix:      "[GORM] ",
		EnableColor: true, // 默认启用彩色输出
	}
}

// LogMode 设置日志级别（实现GORM的Logger接口）
func (l *CustomLogger) LogMode(level logger.LogLevel) logger.Interface {
	newLogger := *l
	newLogger.LogLevel = level
	return &newLogger
}

// Info 输出信息级别日志（实现GORM的Logger接口）
func (l *CustomLogger) Info(ctx context.Context, msg string, data ...interface{}) {
	if l.LogLevel < Info {
		return
	}
	l.printLog("INFO", msg, data...)
}

// Warn 输出警告级别日志（实现GORM的Logger接口）
func (l *CustomLogger) Warn(ctx context.Context, msg string, data ...interface{}) {
	if l.LogLevel < Warn {
		return
	}
	l.printLog("WARN", msg, data...)
}

// Error 输出错误级别日志（实现GORM的Logger接口）
func (l *CustomLogger) Error(ctx context.Context, msg string, data ...interface{}) {
	if l.LogLevel < Error {
		return
	}
	l.printLog("ERROR", msg, data...)
}

// Trace 输出SQL执行轨迹日志（实现GORM的Logger接口）
// 这是GORM日志中最常用的方法，用于记录SQL执行详情
func (l *CustomLogger) Trace(ctx context.Context, begin time.Time, fc func() (string, int64), err error) {
	// 如果日志级别低于Info，不输出Trace日志
	if l.LogLevel < Info {
		return
	}

	// 计算SQL执行时间
	elapsed := time.Since(begin)
	// 获取SQL语句和影响的行数
	sql, rows := fc()

	// 根据是否有错误，确定日志级别
	level := "INFO"
	if err != nil {
		level = "ERROR"
	}

	// 构建日志消息
	msg := fmt.Sprintf("SQL执行 - 耗时: %s, 影响行数: %d, SQL: %s", elapsed, rows, sql)
	if err != nil {
		msg += fmt.Sprintf(", 错误: %v", err)
	}

	l.printLog(level, msg)
}

// 打印日志的通用方法
func (l *CustomLogger) printLog(level, msg string, data ...interface{}) {
	// 格式化消息
	fullMsg := fmt.Sprintf(msg, data...)

	// 根据级别设置颜色
	var colorCode, resetCode string
	if l.EnableColor {
		switch level {
		case "INFO":
			colorCode = "\033[32m" // 绿色
		case "WARN":
			colorCode = "\033[33m" // 黄色
		case "ERROR":
			colorCode = "\033[31m" // 红色
		default:
			colorCode = "\033[0m" // 默认颜色
		}
		resetCode = "\033[0m" // 重置颜色
	}

	// 构建完整日志行
	logLine := fmt.Sprintf(
		"%s%s%s %s%s %s\n",
		colorCode,
		level,
		resetCode,
		time.Now().Format("2006-01-02 15:04:05"),
		l.Prefix,
		fullMsg,
	)

	// 输出到指定的Writer
	_, err := l.Writer.Write([]byte(logLine))
	if err != nil {
		log.Printf("日志输出失败: %v", err)
	}
}
