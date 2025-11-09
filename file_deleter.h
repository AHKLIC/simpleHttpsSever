#ifndef  FILE_DELETER_H
#define  FILE_DELETER_H

#include <chrono>
#include <deque>
#include <thread>
#include <mutex>
#include <functional>
#include <string>
#include <filesystem>
#include <atomic>
#include <iostream>

namespace fs = std::filesystem;

class FileDeleter {
private:
    // 删除请求结构体
    struct DeleteRequest {
        std::string file_path;
        std::string request_id;
        std::function<void(bool, std::string)> callback;
        std::chrono::steady_clock::time_point create_time;
        int retry_count=0;
        int max_retry_count = 10;  // 最大重试次数
    };

    // 成员变量
    std::deque<DeleteRequest> delete_queue_;  // 任务队列
    std::mutex queue_mutex_;                  // 队列互斥锁
    std::thread worker_thread_;               // 工作线程
    std::atomic<bool> is_running_;            // 线程运行标志（原子变量确保线程安全）

    // 生成唯一请求ID
    std::string generate_request_id() {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        return std::to_string(now) + "_" + std::to_string(rand() % 10000);
    }

    // 递归删除文件夹
    bool delete_directory(const std::string& dir_path) {
        try {
            if (!fs::exists(dir_path)) {
                std::cerr <<"path not found：" << dir_path << std::endl;
                return false;
            }

            // 如果是文件直接删除
            if (fs::is_regular_file(dir_path)) {
                return fs::remove(dir_path);
            }

            // 如果是文件夹，递归删除内容
            if (fs::is_directory(dir_path)) {
                for (const auto& entry : fs::directory_iterator(dir_path)) {
                    if (entry.is_regular_file() || entry.is_symlink()) {
                        if (!fs::remove(entry.path())) {
                            std::cerr << "file delete bad (maybe is reading):" << entry.path() << std::endl;
                            return false;
                        }
                    }
                    else if (entry.is_directory()) {
                        if (!delete_directory(entry.path().string())) {
                            return false;
                        }
                    }
                }
                // 删除空文件夹
                return fs::remove(dir_path);
            }

            return false;  // 既不是文件也不是文件夹
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "delete error：" << e.what() << std::endl;
            return false;
        }
    }

    // 工作线程函数
    void worker() {
        while (is_running_) {
            DeleteRequest req;
            bool has_request = false;

            // 从队列获取任务
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (!delete_queue_.empty()) {
                    req = std::move(delete_queue_.front());
                    delete_queue_.pop_front();
                    has_request = true;
                }
            }

            if (has_request) {
                // 检查是否超时
                if (req.retry_count >= req.max_retry_count) {
                    std::cerr << "delete overtime：" << req.file_path << std::endl;
                    req.callback(false, req.file_path);
                    continue;
                }

                // 尝试删除
                bool delete_success = delete_directory(req.file_path);
                if (delete_success) {
                    std::cout << "delete success：" << req.file_path << std::endl;
                    req.callback(true, req.file_path);
                }
                else {
                    req.retry_count++;
                    std::cerr << "delete bad retry... " << req.retry_count << " time: " << req.file_path << std::endl;
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    delete_queue_.push_back(std::move(req));
                }
            }

            // 等待重试间隔
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

public:
    // 构造函数：启动工作线程
    FileDeleter() : is_running_(true) {
        worker_thread_ = std::thread(&FileDeleter::worker, this);
    }

    // 析构函数：停止工作线程并等待结束
    ~FileDeleter() {
        is_running_ = false;
        if (worker_thread_.joinable()) {
            worker_thread_.join();  // 确保线程安全退出
        }
    }

    // 禁止拷贝构造和赋值（避免线程和锁的浅拷贝）
    FileDeleter(const FileDeleter&) = delete;
    FileDeleter& operator=(const FileDeleter&) = delete;

    // 允许移动构造和赋值
    FileDeleter(FileDeleter&&) = default;
    FileDeleter& operator=(FileDeleter&&) = default;

    // 发起删除请求（支持文件和文件夹）
    std::string request_delete(const std::string& path,
        std::function<void(bool, std::string)> callback) {
        DeleteRequest req;
        req.file_path = path;
        req.request_id = generate_request_id();
        req.callback = std::move(callback);
        req.create_time = std::chrono::steady_clock::now();
        req.retry_count = 0;
		std::string request_id = req.request_id;
        std::lock_guard<std::mutex> lock(queue_mutex_);
        delete_queue_.push_back(std::move(req));

        std::cout << "delete requst submit: " << path << "(ID:" << request_id << ")" << std::endl;
        return request_id;
    }
};
#endif // ! FILE_DELETER_H