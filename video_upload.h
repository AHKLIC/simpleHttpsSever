#include"video_resource.h"
#include"file_deleter.h"
#include"MYSQL_USER.h"
#include <fstream>
#include<Windows.h>
#include<sstream>
#include <mutex>
#include<future>
#include <cctype>
typedef char ElementType;
namespace fs = std::filesystem;
const int BUFFER_SIZE_1 = 1024;
const int BUFFER_SIZE_2 = 2048;
const int BUFFER_SIZE_3 = 8192;
const size_t MAX_FILE_SIZE = 1024 * 1024 * 1024; // 1GB
const int MAX_FILE_SIZE_1 = 1024 * 1024;// 1MB
const int Sql_Capacity = 100;
const int select_size = 100;
std::mutex mtx;
 std::mutex user_locks_mutex;
std::unordered_map<int, std::mutex> user_locks;
std::deque<std::string> MYSQL_execute;//生产者消费者模型
std::condition_variable cv_producer;
std::condition_variable cv_consumer;
void execute_sql(int id);
void Produce_Sql(std::string&& , int id );
std::string build_http_response( int , const std::string& ,const std::string&, const std::string&);
void  create_id(std::string filetype, json_execute& );
int Handle_pv_Upload(SSL* );
int Handle_videoinf_Upload(SSL* );
int Handle_Select_request(SSL* );
void Handle_Select_vpconnect_callback(const std::vector<Video_Data>& ,SOCKET,std::string&);
std::string removeHtdocPrefix(const std::string& filename);
int Handle_Delete_request(SSL* );
int Handle_command_guest(SSL* ,const char*);
int Handle_command_login(SSL*, const char*);
int Handle_command_guest_related_video(SSL*);
int Handle_video_search(SSL*, const char*);
int Handle_postcomment_request(SSL* );
int Handle_getcomments(SSL* , const char* );
void Handle_comment_callback(const std::vector<VideoComment>&,SOCKET,std::string&);
std::string url_decode(std::string_view encoded);
json parse_query_string(const char* );
size_t findCaseInsensitive(const std::string&, const std::string&);
FileDeleter file_deleter;

class ThreadPool {
private:
    // 工作线程数组
    std::vector<std::thread> workers;
    // 任务队列（存储待执行的任务，返回值通过std::future传递）
    std::queue<std::function<void()>> tasks;

    // 同步机制
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop; // 线程池停止标志

public:
    // 构造函数：初始化线程池，创建n个工作线程
    ThreadPool(size_t n) : stop(false) {
        for (size_t i = 0; i < n; ++i) {
            workers.emplace_back(
                [this] {
                    // 工作线程循环：从任务队列取任务执行
                    for (;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this] { return this->stop || !this->tasks.empty(); });


                            if (this->stop && this->tasks.empty())
                                return;

                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
            );
        }
    }

    // 禁止拷贝构造和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 析构函数：停止线程池，回收资源
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true; // 标记线程池停止
        }
        condition.notify_all(); // 唤醒所有工作线程

        // 等待所有工作线程退出
        for (std::thread& worker : workers)
            worker.join();
    }

    // 提交任务到线程池，返回std::future用于获取结果
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {  

        // 任务返回值类型：获取F在Args...参数下的返回类型
        using return_type = typename std::invoke_result<F, Args...>::type;

        // 包装任务为std::packaged_task（可生成std::future）
        auto task = std::make_shared<std::packaged_task<return_type()>>
         (
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取与任务绑定的future
        std::future<return_type> res = task->get_future();

        // 将任务加入队列（加锁保护）
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // 若线程池已停止，不能提交新任务
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            // 任务队列存储的是无参函数，通过lambda捕获task并调用
            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one(); // 唤醒一个工作线程执行任务
        return res;
    }
};


class Buffer_Malloc
{

  public:
      Buffer_Malloc(int buffersize)
      {

          buffer = new ElementType[buffersize];
          
      }
      ElementType* get()
      {
          return buffer;
      }

      ~Buffer_Malloc()
      {

          delete[] buffer;
      }

private:
     ElementType* buffer ;

};

void execute_sql(int id) {
    MYSQL_MS VP;
   
        while (true) {
            std::unique_lock<std::mutex> lock(mtx); // 自动锁定mtx

            // 1. 等待队列有数据（条件变量自动管理锁的释放与重获）
            cv_consumer.wait(lock, []() { return !MYSQL_execute.empty(); });

            // 2. 原子化获取并移除队列元素（在锁内完成，确保一致性）
            json consumed_product;
            try {
                consumed_product = std::move(json::parse(std::move(MYSQL_execute.front())));
                std::cout << "consumer" << id << consumed_product << std::endl;
                MYSQL_execute.pop_front();
            }
            catch (const json::parse_error& e) {
                std::cerr << "JSON parse_error: " << e.what() << std::endl;
                MYSQL_execute.pop_front(); 
                continue; // 跳过当前循环，继续下一次消费
            }

            lock.unlock();

            // 4. 处理SQL操作（根据类型使用不同锁策略）
            if (consumed_product["index"] == "INSERT") {
                // INSERT无需用户级锁（假设VP内部线程安全或单线程操作）
                if (consumed_product["execute_type"] == "post_comment")
                {
					VP.add_video_comment(consumed_product, Handle_comment_callback);//批量处理线程安全函数
                }
                else {
                    consumed_product.erase("index");
                    VP.Add_videoInf(consumed_product);
                }
            
            }
            else if (consumed_product["index"] == "UPDATE") {
                
                std::cout << "updata ok" << std::endl;
                // 对同一用户加细粒度锁（避免全局阻塞，提升性能）
                try {
                    int user_id = consumed_product["uid"].get<int>();

                    std::unique_lock<std::mutex> map_lock(user_locks_mutex);//维护用户哈希锁表
                    std::lock_guard<std::mutex> user_lock(user_locks[user_id]); // 为当前用户ID获取锁（自动创建新锁如果不存在）
                    map_lock.unlock();
                    consumed_product.erase("index");
                    VP.Updata_videoInf(consumed_product);
                }
                // 4. 针对不同异常类型的处理
                catch (const nlohmann::json::exception& e) {
                    // 处理JSON相关错误（如"uid"字段不存在、类型不是int）
                    std::cerr << "JSON handle error: " << e.what()
                        << "data: " << consumed_product.dump() << std::endl;
                    // 可选择记录日志后继续处理下一条数据
                }
                catch (const sql::SQLException& e) {
                    // 处理数据库操作错误（如SQL语法错误、连接断开）
                    std::cerr << "database update error: " << "error code=" << e.getErrorCode()
                        << " inf:" << e.what() << std::endl;
                    // 若为连接错误，可尝试重连VP（视MYSQL_MS实现而定）
                    // VP.reconnect(); 
                }
                catch (const std::system_error& e) {
                    // 处理锁操作相关错误（如系统资源不足导致锁创建失败）
                    std::cerr << "system_error: " << e.what()
                        << "code: " << e.code() << std::endl;
                }
                catch (const std::exception& e) {
                    // 捕获其他标准异常（如内存分配失败）
                    std::cerr << "std unknown error:  " << e.what() << std::endl;
                }
                catch (...) {
                    // 捕获所有非标准异常（如自定义异常）
                    std::cerr << "unknown error: " << std::endl;
                }
            }
            else if (consumed_product["index"] == "DELETE") {
                // DELETE需用户级锁（同UPDATA，确保同一用户操作互斥）
               int user_id = consumed_product["uid"].get<int>();
               std::unique_lock<std::mutex> map_lock(user_locks_mutex);
                std::lock_guard<std::mutex> user_lock(user_locks[user_id]);
                map_lock.unlock();
                consumed_product.erase("index");
                VP.Delete_Inf(consumed_product);
            }
            else { // SELECT
                // SELECT通常无需锁（只读操作，除非有实时性要求）
             try{
                consumed_product.erase("index");
                if (consumed_product["data_type"] == "video_data")
                {
                    consumed_product.erase("data_type");
                    if (consumed_product["execute_type"] == "select_video_uid" || consumed_product["execute_type"] == "select_video_videoid")
                        VP.Select_video_Inf(consumed_product, select_size, Handle_Select_vpconnect_callback);
                    else if (consumed_product["execute_type"] == "command_login")
                        VP.command_login(consumed_product, Handle_Select_vpconnect_callback);
                    else if (consumed_product["execute_type"] == "command_guest")
                        VP.command_guest(consumed_product, Handle_Select_vpconnect_callback);
                    else if (consumed_product["execute_type"] == "command_guest_related_video")
                        VP.command_related_video(consumed_product, Handle_Select_vpconnect_callback);
                    else if (consumed_product["execute_type"] == "search_video")
                       VP.search_video(consumed_product, Handle_Select_vpconnect_callback);
                    else
                        ;
                }
                else if(consumed_product["data_type"] == "video_comment") {
					if (consumed_product["execute_type"] == "get_comments")
					{
						VP.get_video_comments(consumed_product, Handle_comment_callback);
					}

                    //其他数据类型..
                }
            }
            catch (const sql::SQLException& e) {
                    // 处理数据库操作错误（如SQL语法错误、连接断开）
                    std::cerr << "database update error: " << "error code=" << e.getErrorCode()
                        << " inf:" << e.what() << std::endl;
                    // 若为连接错误，可尝试重连VP（视MYSQL_MS实现而定）
                    // VP.reconnect(); 
                }
                catch (const std::system_error& e) {
                    // 处理锁操作相关错误（如系统资源不足导致锁创建失败）
                    std::cerr << "system_error: " << e.what()
                        << "code: " << e.code() << std::endl;
                }
                catch (const std::exception& e) {
                    // 捕获其他标准异常（如内存分配失败）
                    std::cerr << "std unknown error:  " << e.what() << std::endl;
                }
                catch (...) {
                    // 捕获所有非标准异常（如自定义异常）
                    std::cerr << "unknown error: " << std::endl;
                }
           

            }

            // 5. 通知生产者队列有空位（无需持有锁，通知是原子操作）
            cv_producer.notify_all();

            

            // （可选）模拟消费耗时，此时已释放所有锁，不阻塞其他线程
            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    
}

void Produce_Sql(std::string &&sql,int id)
{
    bool p = true;//after improve 
    while (p) {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待缓冲区有空位
        cv_producer.wait(lock, []() { return  MYSQL_execute.size() < Sql_Capacity; });

        // 生产数据并放入缓冲区
        std::cout << "Producer " << id << " produced: " << sql << std::endl;
        MYSQL_execute.push_back(std::move(sql));

        // 通知消费者有数据可取
        cv_consumer.notify_all();
       
        p = false;
        // 释放锁，生产间隔一段时间（模拟生产过程）
   
        
       
    }

}



// SSL 读取辅助函数（处理 SSL 特定错误，如 WANT_READ/WANT_WRITE）
int ssl_safe_read(SSL* ssl, char* buf, size_t len) {
    if (ssl == NULL || buf == NULL || len == 0) {
        throw std::invalid_argument("Invalid SSL object or buffer");
    }

    int ret = SSL_read(ssl, buf, static_cast<int>(len)); // SSL_read 第三个参数为 int
    if (ret <= 0) {
        int ssl_err = SSL_get_error(ssl, ret);
        // 处理 "暂时无数据" 或 "需要重试" 的非致命错误（类似原 WSAEWOULDBLOCK）
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            return 0; // 返回 0 表示需要继续读取，不抛出异常
        }
        // 致命错误（如连接关闭、解密失败），抛出异常
           // 关键：立即获取并保存 OpenSSL 错误码  避免null解引用错误
        unsigned long openssl_err = ERR_get_error();
        const char* err_str = ERR_reason_error_string(openssl_err);
        if (err_str == nullptr) {
            err_str = "Unknown OpenSSL error";
        }

        std::cerr << "SSL read failed: " << err_str
            << " (SSL error code: " << ssl_err
            << ", OpenSSL error code: " << openssl_err << ")" << std::endl;
        throw std::runtime_error(err_str);
    }
    return ret; // 返回实际读取的明文字节数
}

// SSL 写入辅助函数（确保响应通过 SSL 加密发送）
void ssl_safe_write(SSL* ssl, const std::string& data) {
    if (ssl == NULL || data.empty()) {
        return;
    }

    const char* buf = data.c_str();
    size_t total_len = data.size();
    size_t written_len = 0;
    int retry_count = 0; 

    while (written_len < total_len) {
        int ret = SSL_write(ssl, buf + written_len, static_cast<int>(total_len - written_len));
        if (ret <= 0) {
            int ssl_err = SSL_get_error(ssl, ret);

            if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                std::cerr << "SSL connection closed by peer (SSL_ERROR_ZERO_RETURN)\n";
                return;
            }

            // 非致命错误：重试（最多 10 次，避免 busy waiting）
            if ((ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) && retry_count < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                retry_count++;
                continue;
            }

            // 其他致命错误（如证书错误）
              // 关键：立即获取并保存 OpenSSL 错误码  避免null解引用错误
            unsigned long openssl_err = ERR_get_error();
            const char* err_str = ERR_reason_error_string(openssl_err);
            if (err_str == nullptr) {
                err_str = "Unknown OpenSSL error";
            }

            std::cerr << "SSL write failed: " << err_str
                        << " (SSL error code: " << ssl_err
                        << ", OpenSSL error code: " << openssl_err << ")" << std::endl;
            return;
        }

        written_len += ret;
        retry_count = 0; // 成功写入后重置重试计数
    }
}





std::string build_http_response(int status_code, const std::string& content_type, const std::string& body,const std::string& origin) {
    std::string response;
    // 1. 状态行（HTTP/1.1 标准，兼容大多数客户端）
    response += "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n";

    // 2. 响应头（关键：Content-Type指定UTF-8，确保中文正常解析）
    response += "Content-Type: " + content_type + "; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";

    if (!origin.empty()) {
        response += "Access-Control-Allow-Origin: " + origin + "\r\n";
    }
    else {
        response += "Access-Control-Allow-Origin: *\r\n";
    }
                                    
    response += "Access-Control-Allow-Credentials: true\r\n";
    // 4. 连接关闭（根据需求可改为 Keep-Alive，但此处处理完即关闭）
    response += "Connection: close\r\n";
    // 5. 空行（HTTP头与体的分隔符，必须存在）
    response += "\r\n";
    // 6. 响应体（JSON数据）
    response += body;
    return response;
}

std::string removeHtdocPrefix(const std::string& filename) {
    const std:: string prefix = "htdocs/";
    // 检查字符串是否以 "htdoc/" 开头
    if (filename.length() >= prefix.length() &&
        filename.substr(0, prefix.length()) == prefix) {
        // 截取前缀之后的部分（从第 6 个字符开始，因为 "htdoc/" 长度为 6）
        return filename.substr(prefix.length());
    }
    // 若不是以该前缀开头，则返回原字符串
    return filename;
}
void  create_id (std::string filetype,json_execute& josn_e)
{
  static std::mutex idmtx;
    unsigned vid = 0;
    if (filetype == "video")
    {
        idmtx.lock();
        vid = Videoid_Max++;
        josn_e.json_addvid(std::to_string(vid));
        idmtx.unlock();
    }
    if (filetype == "img"||filetype =="image")
    {
        idmtx.lock();
       vid = Pid_Max++;
       josn_e.json_addpid(std::to_string(vid));
        idmtx.unlock();
    }
	std::cout << "Generated ID: " << vid << std::endl;
}
std::string filetype_kind(std::string type)
{
    if (type == "video" || type == "img"||type=="image")
        return "video";
    else {
        return"unknown";
    }

}

std::string url_decode(std::string_view encoded) {
    std::string decoded;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            if (i + 2 < encoded.size() && std::isxdigit(encoded[i + 1]) && std::isxdigit(encoded[i + 2])) {
                std::string hex = { encoded[i + 1], encoded[i + 2] };
                decoded += static_cast<char>(std::stoi(hex, nullptr, 16));
                i += 2;
            }
            else {
                decoded += '%';
            }
        }
        else if (encoded[i] == '+') {
            decoded += ' ';
        }
        else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

// 解析query_string为JSON对象
json parse_query_string(const char* query_string) {
    json result;
    if (!query_string || *query_string == '\0') return result;

    std::string_view query(query_string);
    size_t start = 0;
    while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string_view::npos) end = query.size();
        std::string_view pair = query.substr(start, end - start);

        size_t eq_pos = pair.find('=');
        std::string key, value;
        if (eq_pos != std::string_view::npos) {
            key = url_decode(pair.substr(0, eq_pos));
            value = url_decode(pair.substr(eq_pos + 1));
        }
        else {
            key = url_decode(pair);
            value = "";
        }
        result[key] = value;

        start = end + 1;
    }
    return result;
}
size_t findCaseInsensitive(const std::string& str, const std::string& substr) {

    if (substr.empty()) return 0;
     if (str.size() < substr.size()) return std::string::npos;

    // 正常循环查找
     for (size_t i = 0; i <= str.size() - substr.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < substr.size(); ++j) {
            if (tolower(str[i + j]) != tolower(substr[j])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return std::string::npos;
}




void Handle_Select_vpconnect_callback(const std::vector<Video_Data>& selected_vpinf, SOCKET client,std::string& origin)
{
    SSL* ssl = {};
    json send_data = {};
    SOCKET socket = client;
    ssl = connectionPool.getSslBySocket(client);//线程安全
    if (ssl != nullptr)
    {
        try {
            send_data["status"] = "success";
            send_data["code"] = 200;
            send_data["count"] = selected_vpinf.size();//支持隐式转换

            // 1.2 核心数据数组（JS端可通过 Array 遍历，字段名与JS习惯对齐）
            json video_list = json::array();          // 数组类型，JS可直接 forEach 遍历
            for (const auto& record : selected_vpinf) {
                json video_item = {};
                video_item["userId"] = record.uid;
                video_item["videoId"] = record.vid;
                video_item["picId"] = record.pid;
                video_item["videoName"] = record.videoname;  //
                video_item["picAddress"] = record.picaddress;
                video_item["videoAddress"] = record.videoaddress;
                video_item["userName"] = record.username;
                video_item["userIcon"] = record.usericon;

                // 扩展JSON字段：直接合并（保留Video_inf中的所有键值对，如时长、大小等）
                // 若Video_inf有嵌套结构，JSON库会自动处理，JS端可直接访问（如 videoItem.videoInf.duration）
                video_item["videoInf"] = record.Video_inf;

                // 添加到数据数组
                video_list.push_back(video_item);
            }
            // 将数据数组放入响应JSON
            send_data["data"] = video_list;
            std::cout << "send_data:  " << send_data.dump() << std::endl;
            std::string  response = build_http_response(200, "application/json", send_data.dump(4),origin);
            ssl_safe_write(ssl, response);
            connectionPool.setConnectionState(socket, ConnectionState::Closed);
            connectionPool.closeConnection(socket);
        }
        catch (const std::exception& e) {
            std::cerr << "Error handling client response: " << e.what() << std::endl;
            // 异常时确保关闭Socket
            connectionPool.setConnectionState(socket, ConnectionState::Closed);
            connectionPool.closeConnection(socket);
        }
    }
    else
    {
        std::cout << "connection closed  " << socket << std::endl;
        //
    }
}

void Handle_comment_callback(const std::vector<VideoComment>& video_comments, SOCKET client, std::string&origin) {
    SSL* ssl = nullptr;
    json send_data = {};
    SOCKET socket = client;

    // 获取与当前socket关联的SSL连接（线程安全）
    ssl = connectionPool.getSslBySocket(client);
    if (ssl != nullptr) {
        try {
            // 1. 构建基础响应信息
            send_data["status"] = "success";
            send_data["code"] = 200;
            send_data["count"] = video_comments.size();  // 评论总数

            // 2. 构建评论数据数组（字段名适配JS命名习惯）
            json comment_list = json::array();
            for (const auto& comment : video_comments) {
                json comment_item = {};

                // 核心评论信息
                comment_item["id"] = comment.id;                  // 评论ID
                comment_item["videoId"] = comment.video_id;       // 关联视频ID
                comment_item["userId"] = comment.user_id;         // 评论用户ID
                comment_item["parentId"] = comment.parent_id;     // 父评论ID（0表示主评论）
                comment_item["content"] = comment.content;        // 评论内容
                comment_item["likeCount"] = comment.like_count;   // 点赞数
                comment_item["replyCount"] = comment.reply_count; // 回复数

                // 状态转换：将枚举转为字符串（前端更易理解）
                switch (comment.status) {
                case VideoCommentStatus::kPendingReview:
                    comment_item["status"] = "pending";
                    break;
                case VideoCommentStatus::kApproved:
                    comment_item["status"] = "approved";
                    break;
                case VideoCommentStatus::kDeleted:
                    comment_item["status"] = "deleted";
                    break;
                default:
                    comment_item["status"] = "unknown";
                }

                // 时间信息
                comment_item["createdAt"] = comment.created_at;   
                comment_item["updatedAt"] = comment.updated_at;   

                comment_item["username"] = comment.username;      
                comment_item["userIcon"] = comment.usericon;      

                comment_list.push_back(comment_item);
            }

            // 将评论数组放入响应数据
            send_data["comments"] = comment_list;

            // 3. 调试输出与发送响应
            std::cout << "Handle_comment_callback data: " << send_data.dump() << std::endl;
            std::string response = build_http_response(200, "application/json", send_data.dump(4),origin);
            ssl_safe_write(ssl, response);

            // 4. 关闭连接（按连接池规范处理）
            connectionPool.setConnectionState(socket, ConnectionState::Closed);
            connectionPool.closeConnection(socket);

        }
        catch (const std::exception& e) {
            // 异常处理：确保连接正确关闭
            std::cerr << "Handle_comment_callback error" << e.what() << std::endl;
            connectionPool.setConnectionState(socket, ConnectionState::Closed);
            connectionPool.closeConnection(socket);
        }
    }
    else {
        // SSL连接获取失败的处理
        std::cout << " closed Handle_comment_callback！！（socket: " << socket << "）" << std::endl;
    }
}











int Handle_command_guest(SSL* client,const char* query_string) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 缓冲区对象（延长生命周期，避免野指针）
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
    int pos = 0;
    std::string request;       // 存储完整的 HTTP 请求头
    bool headers_complete = false; // 标记请求头是否读取完成
    std::string origin = {};
    try {
        // 1. 读取 HTTP 请求头（通过 SSL 解密后读取明文）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数（替代原 recv）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理“暂时无数据”情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 是 HTTP 头与体的分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析请求头（访客逻辑无需额外字段，仅打印日志）
                std::istringstream iss(request.substr(0, header_end));
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除 Windows 换行符残留的 \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    else if (findCaseInsensitive(line,"Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }
         
                }

                headers_complete = true; // 标记头读取完成，退出循环
            }
        }


        json parm = parse_query_string(query_string);
        json sql;
		int socket = SSL_get_fd(client);
        sql["index"] = "SELECT";         
        sql["socket"] = socket; 
        sql["origin"] = origin;
        sql["data_type"] = "video_data";
        sql["execute_type"] = "command_guest";// 标记为访客命令类型
        sql.update(parm);
        // 访客用户 ID 固定为 0（原有逻辑）
        Produce_Sql(sql.dump(), 0);

        // 4. 设置连接状态为“等待”（原有逻辑不变）
        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1; // 处理成功

    }
    catch (const std::exception& e) {
        // 5. 异常处理：返回 500 内部错误响应（通过 SSL 加密发送）
        std::cerr << "Error (guest command): " << e.what() << std::endl;
		const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);

        // 用 SSL 安全写入替代原 send，确保错误响应加密发送
        ssl_safe_write(client, response);

        return 0; // 处理失败
    }

    return 1;
}
int Handle_command_login(SSL* client, const char* query_string) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);
    ElementType* buffer = bufferObj.get();
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
    int pos = 0;
    std::string request;       // 存储完整的 HTTP 请求头
    bool headers_complete = false; // 标记请求头是否读取完成
    std::string origin = {};
    try {
        // 1. 读取 HTTP 请求头（通过 SSL 解密后读取明文）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数（替代原 recv）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理“暂时无数据”情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 是 HTTP 头与体的分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析请求头（访客逻辑无需额外字段，仅打印日志）
                std::istringstream iss(request.substr(0, header_end));
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除 Windows 换行符残留的 \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }

                }

                headers_complete = true; // 标记头读取完成，退出循环
            }
        }


        json parm = parse_query_string(query_string);
        json sql;
        int socket = SSL_get_fd(client);
        sql["index"] = "SELECT";
        sql["socket"] = socket;
        sql["origin"] = origin;
        sql["data_type"] = "video_data";
        sql["execute_type"] = "command_login";
        sql.update(parm);
        Produce_Sql(sql.dump(), 0);

        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1; // 处理成功

    }
    catch (const std::exception& e) {
        // 5. 异常处理：返回 500 内部错误响应（通过 SSL 加密发送）
        std::cerr << "Error (guest command): " << e.what() << std::endl;
        const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);

        // 用 SSL 安全写入替代原 send，确保错误响应加密发送
        ssl_safe_write(client, response);

        return 0; // 处理失败
    }

    return 1;
}



int Handle_command_guest_related_video(SSL* client)
{

    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
	std::string origin = {};
    std::string request;
    bool headers_complete = false;
    std::string userid, videoid, data_type, handle_type, authorid,pagenum;
    size_t pos;

    try {
        // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取到的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除行尾可能残留的 \r（Windows 换行符）
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    if (findCaseInsensitive(line, "data_type:") != std::string::npos) {
                        pos = line.find(':');
                        data_type = line.substr(pos + 1);
                        // 去除字段值前后的空格/制表符
                        data_type.erase(0, data_type.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line,"handle_type:") != std::string::npos) {
                        pos = line.find(':');
                        handle_type = line.substr(pos + 1);
                        handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                        pos = line.find(':');
                        userid = line.substr(pos + 1);
                        userid.erase(0, userid.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line, "authorid:") != std::string::npos) {
                        pos = line.find(':');
                        authorid = line.substr(pos + 1);
                        authorid.erase(0, authorid.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line,"pagenum:") != std::string::npos) {
                        pos = line.find(':');
                        pagenum = line.substr(pos + 1);
                       pagenum.erase(0, pagenum.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }
                }

                headers_complete = true; // 标记头解析完成
            }
        }

        json sql;
        int socket = connectionPool.getSocketBySsl(client);
        sql["socket"] = socket;
        sql["index"] = "SELECT";
		sql["origin"] = origin;
        sql["uid"] = std::stoi(userid);
        sql["authorid"] = std::stoul(authorid); // authorid
        sql["execute_type"] = handle_type; // 处理类型（如查询类型）
        sql["pagenum"] = std::stoul(pagenum);
        sql["data_type"] = data_type;     // 数据类型

        // 生产 SQL 任务（原有逻辑不变）
        Produce_Sql(sql.dump(), sql["uid"].get<int>());
        std::cout << "select sql: " << sql.dump() << std::endl;
        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1;

    }
    catch (const std::exception& e) {
        // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
        std::cerr << "Error: " << e.what() << std::endl;
		const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);

        // 用 SSL 写入响应，替代原 send
        ssl_safe_write(client, response);

        return 0;
    }

    return 1;



}

int Handle_video_search(SSL* client, const char* query_string) {
     Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 缓冲区对象（延长生命周期，避免野指针）
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
    std::string origin = {};
    int pos = 0;
    std::string request;       // 存储完整的 HTTP 请求头
    bool headers_complete = false; // 标记请求头是否读取完成

    try {
        // 1. 读取 HTTP 请求头（通过 SSL 解密后读取明文）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数（替代原 recv）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理“暂时无数据”情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 是 HTTP 头与体的分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析请求头（访客逻辑无需额外字段，仅打印日志）
                std::istringstream iss(request.substr(0, header_end));
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除 Windows 换行符残留的 \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }
                }

                headers_complete = true; // 标记头读取完成，退出循环
            }
        }

        // 3. 构造访客 SQL 请求（原有业务逻辑不变）

        json parm = parse_query_string(query_string);
        json sql;
        int socket = SSL_get_fd(client);
        sql["index"] = "SELECT";
        sql["socket"] = socket;
        sql["origin"] = origin;
        sql["data_type"] = "video_data";
        sql["execute_type"] = "search_video";
        sql.update(parm);
        Produce_Sql(sql.dump(), std::stoi(sql["uid"].get<std::string>()));

        // 4. 设置连接状态为“等待”（原有逻辑不变）
        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1; // 处理成功

    }
    catch (const std::exception& e) {
        // 5. 异常处理：返回 500 内部错误响应（通过 SSL 加密发送）
        std::cerr << "Error (guest command): " << e.what() << std::endl;
		const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);
        // 用 SSL 安全写入替代原 send，确保错误响应加密发送
        ssl_safe_write(client, response);

        return 0; // 处理失败
    }

    return 1;
}


int Handle_Delete_request(SSL* client) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 10KB 缓冲区，延长生命周期
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }

    // 核心变量初始化
    std::string origin = {};
    std::string request;               // 存储接收的请求数据（头+体）
    size_t content_length = 0;         // HTTP 头中的 Content-Length
    bool headers_complete = false;     // 标记请求头是否解析完成
    size_t received_total = 0;         // 已接收的 JSON 数据总长度
    std::string json_str;              // 存储删除请求的 JSON 数据
    std::string userid, handle_type;   // 业务字段：用户ID、删除类型
    size_t pos;                        // 字符串查找辅助变量
    size_t body_start = 0;             // 请求体起始位置（仅计算一次，修复原重复累加问题）

    try {
        while (true) {
            // 2. 读取数据：recv() → ssl_safe_read()（SSL 解密后读取明文）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_2);
            if (bytes_read == 0) {
                continue; // 暂时无数据，继续循环等待
            }

            // 将读取的明文追加到请求缓冲区
            request.append(buffer, bytes_read);
            size_t current_request_size = request.size();

            // 3. 解析 HTTP 请求头（仅首次解析，避免重复计算）
            if (!headers_complete) {
                size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符（\r\n\r\n）
                if (header_end != std::string::npos) {
                    // 解析头字段：Content-Length、handle_type、userid
                    std::istringstream iss(request.substr(0, header_end));
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除 Windows 换行符残留的 \r（避免解析无效字符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        // 解析 Content-Length（判断请求体是否完整）
                        if (findCaseInsensitive(line,"Content-Length:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                std::string len_str = line.substr(pos + 1);
                                len_str.erase(0, len_str.find_first_not_of(" \t"));
                                content_length = std::stoul(len_str);
                            }
                        }
                        // 解析 handle_type（删除类型，如删除视频/图片）
                        else if (findCaseInsensitive(line, "handle_type:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                handle_type = line.substr(pos + 1);
                                handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                            }
                        }
                        // 解析 userid（关联用户，用于构建文件路径）
                        else if (findCaseInsensitive(line,"userid:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                userid = line.substr(pos + 1);
                                userid.erase(0, userid.find_first_not_of(" \t"));
                            }
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                    }

                    // 4. 头解析后验证：请求体大小是否超过 1MB 限制
                    if (content_length > MAX_FILE_SIZE_1) {
						const std::string response = build_http_response(413, "text/plain", "Payload Too Large", origin);
                        ssl_safe_write(client, response); // SSL 发送 413 响应
                        return 0;
                    }

                    body_start = header_end + 4; // 4 = "\r\n\r\n" 的字节数
                    // 提取当前请求中的首次 JSON 数据（头后可能已包含部分体数据）
                    if (current_request_size > body_start) {
                        size_t initial_body_size = current_request_size - body_start;
                        json_str.append(request.c_str() + body_start, initial_body_size);
                        received_total += initial_body_size;
                    }

                    // 标记头解析完成，后续仅接收 JSON 数据
                    headers_complete = true;
                    // 清空请求缓冲区（后续数据直接追加到 json_str，避免内存堆积）
                    request.clear();
                }
            }
            // 6. 头解析完成后，直接接收剩余 JSON 数据
            else {
                json_str.append(buffer, bytes_read);
                received_total += bytes_read;
            }

            // 7. 检查 JSON 数据是否接收完整（用 >= 避免多读导致阻塞）
            if (headers_complete && received_total >= content_length) {
                // 截断超出 Content-Length 的冗余数据（防止 JSON 解析错误）
                if (received_total > content_length) {
                    json_str.resize(content_length);
                    printf("Received del content_length : %zu bytes (total: %zu)\n", content_length, received_total);
                }

                // 8. 解析 JSON 并执行删除逻辑（核心业务不变）
                try {
                    json delete_content = json::parse(json_str); // 解析删除请求参数（如 videoid_list）
                    std::string file_path;
                    json sql;

                    // 构造 DELETE 类型 SQL
                    sql["index"] = "DELETE";
                    sql["execute_type"] = handle_type;       // 传入删除类型（如删除视频）
                    sql["uid"] = std::stoul(userid);         // 关联用户 ID
                    sql["origin"] = origin;
                    sql.update(delete_content);              // 合并删除参数（如 videoid_list）
                    std::cout << "Generated delete SQL: " << sql.dump(4) << std::endl;

                    // 构建待删除文件路径（如 htdocs/MUSE/vp/1/video
                    std::string videoid = std::to_string(delete_content["videoid_list"][0].get<int>());
                    file_path = "htdocs/MUSE/vp/" + userid + "/video/" + videoid;

                    // 9. 异步请求删除文件（回调函数适配 SSL）
                    file_deleter.request_delete(
                        file_path,
                        // 回调函数：文件删除结果处理（适配 SSL 发送响应）
                        [sql, client,origin](bool success, std::string path) {
                            if (success) {
                                // 删除成功：发送 200 响应 + 提交 SQL + 关闭 SSL 连接
                                const std::string response = build_http_response(200, "application/json", "success", origin);
                                ssl_safe_write(client, response); // SSL 发送成功响应

                                Produce_Sql(sql.dump(), sql["uid"].get<int>()); // 提交删除 SQL
                            }
                            else {
                                // 删除失败：发送 400 响应 + 关闭 SSL 连接
                                const std::string response = build_http_response(400, "application/json", "delete failed", origin);
                                ssl_safe_write(client, response); // SSL 发送失败响应
                            }
                            int sock = SSL_get_fd(client);
                         
                            connectionPool.setConnectionState(sock, ConnectionState::Closed);
                            // 从 SSL 对象获取底层 Socket，再关闭连接（适配原连接池逻辑）
                          
                          
                            connectionPool.closeConnection(sock); // 关闭底层 Socket
                        }
                    );

                    // 设置连接状态为等待（等待文件删除回调）
                    int sock = SSL_get_fd(client);
                    connectionPool.setConnectionState(sock, ConnectionState::Wait);
                    return 1;

                }
                catch (const json::parse_error& e) {
                    // 处理 JSON 解析错误（返回 400 错误）
                    throw std::runtime_error("Invalid delete JSON: " + std::string(e.what()));
                }
            }
        }
    }
    // 11. 异常处理（网络错误、JSON 解析错误等）
    catch (const std::exception& e) {
        std::cerr << "Delete request error: " << e.what() << std::endl;
        // 发送 500 错误响应（SSL 加密）
		const std::string response = build_http_response(500, "text/plain", "Internal Server Error",origin);
        ssl_safe_write(client, response);

        // 异常时关闭 SSL 连接
        if (client != NULL) {
            SOCKET sock = static_cast<SOCKET>(SSL_get_fd(client));
            connectionPool.setConnectionState(sock, ConnectionState::Closed);
            connectionPool.closeConnection(sock);
        }
        return 0;
    }

    return 1;
}

int Handle_getcomments(SSL* client, const char* query_string) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 缓冲区对象（延长生命周期，避免野指针）
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
    std::string origin = {};
    int pos = 0;
    std::string request;       // 存储完整的 HTTP 请求头
    bool headers_complete = false; // 标记请求头是否读取完成

    try {
        // 1. 读取 HTTP 请求头（通过 SSL 解密后读取明文）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数（替代原 recv）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理“暂时无数据”情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 是 HTTP 头与体的分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析请求头（访客逻辑无需额外字段，仅打印日志）
                std::istringstream iss(request.substr(0, header_end));
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除 Windows 换行符残留的 \r
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }
                }

                headers_complete = true; // 标记头读取完成，退出循环
            }
        }

        // 3. 构造访客 SQL 请求（原有业务逻辑不变）

        json parm = parse_query_string(query_string);
        json sql;
        int socket = SSL_get_fd(client);
        sql["index"] = "SELECT";
        sql["socket"] = socket;
        sql["origin"] = origin;
        sql["data_type"] = "video_comment";
        sql["execute_type"] = "get_comments";
        sql.update(parm);
        // 访客用户 ID 固定为 0（原有逻辑）
        Produce_Sql(sql.dump(), 0);

        // 4. 设置连接状态为“等待”（原有逻辑不变）
        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1; // 处理成功

    }
    catch (const std::exception& e) {
        // 5. 异常处理：返回 500 内部错误响应（通过 SSL 加密发送）
        std::cerr << "Error (guest command): " << e.what() << std::endl;
        const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);
        // 用 SSL 安全写入替代原 send，确保错误响应加密发送
        ssl_safe_write(client, response);

        return 0; // 处理失败
    }

    return 1;
}




int Handle_postcomment_request(SSL* client) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 1KB 缓冲区，延长生命周期
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
	std::string origin = {};
    std::string request;               
    size_t content_length = 0;         
    bool headers_complete = false;    
    size_t received_total = 0;         
    std::string json_str;             
    size_t pos;                        
    size_t body_start = 0;             

    try {
        while (true) {
            // 2. 读取数据：recv() → ssl_safe_read()（SSL 解密后读取明文）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_2);
            if (bytes_read == 0) {
                continue; // 暂时无数据，继续循环等待
            }

            // 将读取的明文追加到请求缓冲区
            request.append(buffer, bytes_read);

            size_t current_request_size = request.size();
            if (!headers_complete) {
                size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符（\r\n\r\n）
                if (header_end != std::string::npos) {
                    std::istringstream iss(request.substr(0, header_end));
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除 Windows 换行符残留的 \r（避免解析无效字符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        // 解析 Content-Length（判断请求体是否完整）
                        if (findCaseInsensitive(line, "Content-Length:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                std::string len_str = line.substr(pos + 1);
                                len_str.erase(0, len_str.find_first_not_of(" \t"));
                                content_length = std::stoul(len_str);
                            }
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                       
                    }

                    // 4. 头解析后验证：请求体大小是否超过 1MB 限制
                    if (content_length > MAX_FILE_SIZE_1) {
						const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                        ssl_safe_write(client, response); // SSL 发送 413 响应
                        return 0;
                    }

                    body_start = header_end + 4; // 4 = "\r\n\r\n" 的字节数
                    // 提取当前请求中的首次 JSON 数据（头后可能已包含部分体数据）
                    if (current_request_size > body_start) {
                        size_t initial_body_size = current_request_size - body_start;
                        json_str.append(request.c_str() + body_start, initial_body_size);
                        received_total += initial_body_size;
                    }

                    // 标记头解析完成，后续仅接收 JSON 数据
                    headers_complete = true;
                    // 清空请求缓冲区（后续数据直接追加到 json_str，避免内存堆积）
                    request.clear();
                }
            }
            // 6. 头解析完成后，直接接收剩余 JSON 数据
            else {
                json_str.append(buffer, bytes_read);
                received_total += bytes_read;
            }

            // 7. 检查 JSON 数据是否接收完整（用 >= 避免多读导致阻塞）
            if ( received_total >= content_length) {
                // 截断超出 Content-Length 的冗余数据（防止 JSON 解析错误）
                if (received_total > content_length) {
                    json_str.resize(content_length);
                    printf("Received postcomment content_length : %zu bytes (total: %zu)\n", content_length, received_total);
                }

                // 8. 解析 JSON 并执行删除逻辑（核心业务不变）
                try {
                    int sock = SSL_get_fd(client);
                    json comment_content = json::parse(json_str); // 解析请求参数
                    std::string file_path;
                    json sql;
                    sql["index"] = "INSERT";
					sql["execute_type"] = "post_comment";
					sql["socket"] = sock;
					sql["origin"] = origin;
                    sql.update(comment_content);             
                    std::cout << "Generated postcomment SQL: " << sql.dump(4) << std::endl;
                    Produce_Sql(sql.dump(), std::stoul((comment_content["video_id"].get<std::string>() )));
                    connectionPool.setConnectionState(sock, ConnectionState::Wait);
                    return 1;

                }
                catch (const json::parse_error& e) {
                    // 处理 JSON 解析错误（返回 400 错误）
                    throw std::runtime_error("Invalid postcomment JSON: " + std::string(e.what()));
                }
            }
        }
    }
    // 11. 异常处理（网络错误、JSON 解析错误等）
    catch (const std::exception& e) {
        std::cerr << "addcommment request error: " << e.what() << std::endl;
        // 发送 500 错误响应（SSL 加密）
		const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);
        ssl_safe_write(client, response);

        // 异常时关闭 SSL 连接
        if (client != NULL) {
            SOCKET sock = static_cast<SOCKET>(SSL_get_fd(client));
            connectionPool.setConnectionState(sock, ConnectionState::Closed);
            connectionPool.closeConnection(sock);
        }
        return 0;
    }

    return 1;
}





int Handle_Select_request(SSL* client) 
{
    Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }

    std::string request = {};
    std::string origin = {};
    bool headers_complete = false;
    std::string userid, videoid, data_type, handle_type;
    size_t pos;

    try {
        // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
        while (!headers_complete) {
            // 调用 SSL 安全读取函数
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

            // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
            if (bytes_read == 0) {
                continue;
            }

            // 将读取到的明文追加到请求字符串
            request.append(buffer, bytes_read);

            // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
            size_t header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                std::string line;
                while (std::getline(iss, line)) {
                    // 移除行尾可能残留的 \r（Windows 换行符）
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }

                    // 解析 data_type 字段
                    if (findCaseInsensitive(line,"data_type:") != std::string::npos) {
                        pos = line.find(':');
                        data_type = line.substr(pos + 1);
                        // 去除字段值前后的空格/制表符
                        data_type.erase(0, data_type.find_first_not_of(" \t"));
                    }
                    // 解析 handle_type 字段
                    else if (findCaseInsensitive(line, "handle_type:") != std::string::npos) {
                        pos = line.find(':');
                        handle_type = line.substr(pos + 1);
                        handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                    }
                    // 解析 userid 字段
                    else if (findCaseInsensitive(line,"userid:") != std::string::npos) {
                        pos = line.find(':');
                        userid = line.substr(pos + 1);
                        userid.erase(0, userid.find_first_not_of(" \t"));
                    }
                    // 解析 videoid 字段
                    else if (findCaseInsensitive(line, "videoid:") != std::string::npos) {
                        pos = line.find(':');
                        videoid = line.substr(pos + 1);
                        videoid.erase(0, videoid.find_first_not_of(" \t"));
                    }
                    else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                        pos = line.find(':');
                        if (pos != std::string::npos) {
                            origin = line.substr(pos + 1);
                            origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                        }
                    }

                }

                headers_complete = true; // 标记头解析完成
            }
        }

        // 3. 构造 SQL 请求（原有业务逻辑不变）
        json sql;
        int socket=SSL_get_fd(client);
        sql["index"] = "SELECT";       // 修正原拼写 "SElECT"
        sql["socket"] = socket;        // 保留 SSL 对象引用（若后续需使用）
		sql["origin"] = origin;      
        sql["uid"] = std::stoi(userid);  
        sql["vid"] = std::stoi(videoid); 
        sql["execute_type"] = handle_type; // 处理类型（如查询类型）
        sql["data_type"] = data_type;     // 数据类型

        // 生产 SQL 任务（原有逻辑不变）
        Produce_Sql(sql.dump(), sql["uid"].get<int>());

        // 设置连接状态为等待（原有逻辑不变）
        connectionPool.setConnectionState(socket, ConnectionState::Wait);

        return 1;

    }
    catch (const std::exception& e) {
        // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
        std::cerr << "Error: " << e.what() << std::endl;
		const std::string response = build_http_response(500, "application/json", "Internal Server Error", origin);

        // 用 SSL 写入响应，替代原 send
        ssl_safe_write(client, response);

        return 0;
    }

    return 1;
}


// ---------------------- 视频信息（JSON）上传处理函数（SSL 适配版）----------------------
int Handle_videoinf_Upload(SSL* client) {
	
    const size_t MAX_FILE_SIZE = 10 * 1024 * 1024; // 10MB，JSON 数据大小限制
    Buffer_Malloc bufferObj(BUFFER_SIZE_2);  // 4KB 缓冲区，延长生命周期
    ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }

    std::string request;               // 存储接收的请求数据（头+体）
    size_t content_length = 0;        
    bool headers_complete = false;     
    size_t received_total = 0;        
    std::string json_str;              
    json_execute json_e;             
    size_t body_start = 0;           
    int pos = 0;
    std::string origin = {};
    try {
        while (true) {
            // 1. 通过 SSL 读取数据（替代原 recv，自动解密）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_2);
            if (bytes_read == 0) {
                continue; // 暂时无数据，继续循环等待
            }

            // 将读取的明文追加到请求缓冲区
            request.append(buffer, bytes_read);
            size_t current_request_size = request.size();

            // 2. 解析 HTTP 请求头（仅首次解析，避免重复计算）
            if (!headers_complete) {
                size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符（\r\n\r\n）
                if (header_end != std::string::npos) {
                    // 解析头字段：Content-Length、userid、videoid
                    std::istringstream iss(request.substr(0, header_end));
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除 Windows 换行符残留的 \r（避免解析时包含无效字符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        // 解析 Content-Length（必须，用于判断 JSON 数据是否接收完整）
                        if (findCaseInsensitive(line, "Content-Length:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                // 提取冒号后的内容，去除空格后转换为无符号长整型
                                std::string len_str = line.substr(colon_pos + 1);
                                len_str.erase(0, len_str.find_first_not_of(" \t"));
                                content_length = std::stoul(len_str);
                            }
                        }
                        // 解析 userid（关联用户，存入 json_execute 对象）
                        else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                // 提取 userid（跳过冒号和后续空格，避免硬编码偏移量）
                                std::string uid_str = line.substr(colon_pos + 1);
                                uid_str.erase(0, uid_str.find_first_not_of(" \t"));
                                json_e.json_adduserid(uid_str);
                            }
                        }
                        // 解析 videoid（关联视频，存入 json_execute 对象）
                        else if (findCaseInsensitive(line, "videoid:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                // 提取 videoid（同样跳过空格，修复原硬编码偏移量问题）
                                std::string vid_str = line.substr(colon_pos + 1);
                                vid_str.erase(0, vid_str.find_first_not_of(" \t"));
                                json_e.json_addvid(vid_str);
                            }
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                        
                    }

                    // 3. 头解析后验证：检查 JSON 大小是否超过限制（10MB）
                    if (content_length > MAX_FILE_SIZE) {
						const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                        ssl_safe_write(client, response); // SSL 发送 413 响应
                        return 0;
                    }

                    // 4. 计算请求体起始位置（仅一次，修复原重复累加问题）
                    body_start = header_end + 4; // 4 = "\r\n\r\n" 的长度
                    // 提取当前请求中的首次 JSON 数据（头后可能已包含部分体数据）
                    if (current_request_size > body_start) {
                        size_t initial_body_size = current_request_size - body_start;
                        json_str.append(request.c_str() + body_start, initial_body_size);
                        received_total += initial_body_size;
                    }

                    // 标记头解析完成，后续仅接收 JSON 数据
                    headers_complete = true;
                    // 清空请求缓冲区（后续数据直接追加到 json_str，避免内存堆积）
                    request.clear();
                }
            }
            // 3. 头解析完成后，直接接收剩余 JSON 数据
            else {
                // 本次读取的所有数据都是 JSON 体，直接追加
                json_str.append(buffer, bytes_read);
                received_total += bytes_read;
            }

            // 4. 检查 JSON 数据是否接收完整（支持 >= 避免多读导致的阻塞）
            if (headers_complete && received_total >= content_length) {
                // 截断超出 Content-Length 的冗余数据（防止 JSON 解析错误）
                if (received_total > content_length) {
                    json_str.resize(content_length);
                    printf("Received vinf content_length : %zu bytes (total: %zu)\n", content_length, received_total);
                }

                // 5. 解析 JSON 并生成 SQL（业务逻辑不变）
                json content, sql;
                std::string response;
                try {
                    // 解析接收的 JSON 字符串（需处理 JSON 格式错误）
                    json video_info = json::parse(json_str);

                    // 构造更新视频信息的 SQL
                    sql["index"] = "UPDATE"; // 修正原拼写 "UPDATA"，避免 SQL 语法错误
                    sql["execute_type"] = "videoinf_json";
                    sql["vid"] = std::stoul(json_e.json_getvid()); // 关联视频 ID
                    sql["uid"] = std::stoul(json_e.json_getuid()); // 关联用户 ID
                    sql.update(video_info); // 合并视频信息到 SQL JSON 中

                    std::cout << "Generated SQL: " << sql.dump(4) << std::endl;
                    // 提交 SQL 任务到任务队列
                    Produce_Sql(sql.dump(), sql["uid"].get<int>());

                    // 构建成功响应（JSON 格式）
                    content["status"] = "success";
                    response = build_http_response(200, "application/json", content.dump(),origin);
                }
                catch (const json::parse_error& e) {
                    // 处理 JSON 解析错误（返回 400 错误）
                    throw std::runtime_error("Invalid JSON format: " + std::string(e.what()));
                }

                // 6. 通过 SSL 发送成功响应
                ssl_safe_write(client, response);
                return 1;
            }
        }
    }
    // 7. 异常处理（网络错误、JSON 解析错误、SQL 构建错误等）
    catch (const std::exception& e) {
        std::cerr << "Video info upload error: " << e.what() << std::endl;
        // 返回 500 内部错误响应（SSL 加密发送）
        const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);
        ssl_safe_write(client, response);
        return 0;
    }

    return 1;
}



int Handle_pv_Upload(SSL* client) {
    Buffer_Malloc bufferObj(BUFFER_SIZE_3);
    ElementType* buffer = bufferObj.get();
    if (buffer == NULL) {
        std::cerr << "Buffer allocation failed" << std::endl;
        return 0;
    }
    const std::string uploadDir = "htdocs/MUSE/vp";
    // 上传核心变量初始化
    std::string request = {};               // 存储接收的请求数据（头+体）
    size_t content_length = 0;         // 从HTTP头获取的总内容长度
    size_t current_request_size = {};
    bool headers_complete = false;     // 标记请求头是否解析完成
    size_t received_total = 0;         // 已接收的总字节数
    std::ofstream file = {};                // 上传文件的输出流（提前声明，避免作用域问题）
    int pos;
    std::string origin = {};
    // 业务变量
    std::string file_type, file_type_judge, file_extention;
    std::string userid, videoid;
    std::string userfileDir, videofileDir, videoidfileDir, filename;
    json_execute json_e;

    // 确保上传根目录存在
    if (!fs::exists(uploadDir)) {
        try {
            fs::create_directories(uploadDir);
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "Failed to create upload dir: " << e.what() << std::endl;
            return 0;
        }
    }

    try {
        // 循环接收请求数据（头+体）
        while (true) {
            // 1. 通过SSL读取数据（替代原 recv）
            int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_3);
            if (bytes_read == 0) {
                continue; // 暂时无数据，继续循环
            }

            // 将读取的明文追加到请求缓冲区
            request.append(buffer, bytes_read);
            current_request_size = request.size(); // 当前请求总长度

            // 2. 解析HTTP请求头（仅首次解析）
            if (!headers_complete) {
                size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符
                if (header_end != std::string::npos) {
                    // 解析头字段（Content-Length、Content-Type、userid、videoid）
                    std::istringstream iss(request.substr(0, header_end));
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除Windows换行符残留的 \r
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        // 解析 Content-Length（必选，用于判断上传是否完成）
                        if (findCaseInsensitive(line, "Content-Length:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                content_length = std::stoul(line.substr(colon_pos + 1));
                            }
                        }
                        // 解析 Content-Type（获取文件类型和扩展名）
                        else if (findCaseInsensitive(line,"Content-Type:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                std::string content_type = line.substr(colon_pos + 1);
                                content_type.erase(0, content_type.find_first_not_of(" \t"));
                                // 提取文件类型（如 "image/jpeg" → "image"）和扩展名（"jpeg"）
                                size_t slash_pos = content_type.find('/');
                                if (slash_pos != std::string::npos) {
                                    file_type_judge = content_type.substr(0, slash_pos);
                                    file_extention = content_type.substr(slash_pos + 1);
                                    file_type = filetype_kind(file_type_judge); // 转换为业务用类型
                                }
                            }
                        }
                        // 解析 userid（上传目录依赖）
                        else if (findCaseInsensitive(line,"userid:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                userid = line.substr(colon_pos + 1);
                                userid.erase(0, userid.find_first_not_of(" \t"));
                            }
                        }
                        // 解析 videoid（图片上传需关联视频ID）
                        else if (findCaseInsensitive(line, "videoid:") != std::string::npos) {
                            size_t colon_pos = line.find(':');
                            if (colon_pos != std::string::npos) {
                                videoid = line.substr(colon_pos + 1);
                                videoid.erase(0, videoid.find_first_not_of(" \t"));
                            }
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                    }

                    // 3. 头解析后验证与初始化
                    // 验证文件大小（超过1GB返回413）
                    if (content_length > MAX_FILE_SIZE) {
						printf("Upload file too large: %zu bytes\n", content_length);
						const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                        ssl_safe_write(client, response);
                        return 0;
                    }

                    // 验证 userid 非空（否则返回400）
                    if (userid.empty()) {
                        const std::string response =
                            "HTTP/1.1 400 Bad Request (empty userid)\r\n"
                            "Content-Length: 0\r\n"
                            "Connection: close\r\n\r\n";
                        ssl_safe_write(client, response);
                        return 0;
                    }

                    // 创建用户目录和文件类型目录（如 uploadDir/123/images）
                    userfileDir = uploadDir + '/' + userid;
                    videofileDir = userfileDir + '/' + file_type;
                    try {
                        if (!fs::exists(userfileDir)) fs::create_directories(userfileDir);
                        if (!fs::exists(videofileDir)) fs::create_directories(videofileDir);
                    }
                    catch (const fs::filesystem_error& e) {
                        throw std::runtime_error("Failed to create user/file dir: " + std::string(e.what()));
                    }

                    // 生成文件ID和最终路径（图片需关联videoid，视频自动生成）
                    json_e.json_adduserid(userid);
                    if (!videoid.empty()) { // 图片上传（关联已有视频ID）
                        json_e.json_addvid(videoid);
                        create_id(file_type_judge, json_e);
                      /*  std::string id = (file_type_judge == "video") ? json_e.json_getvid() : json_e.json_getpid();*/
                        videoidfileDir = videofileDir + '/' + json_e.json_getvid();
                    }
                    else { // 视频上传（自动生成视频ID）
                        create_id(file_type_judge, json_e);
                        std::string id = json_e.json_getvid();
                        videoidfileDir = videofileDir + '/' + id;
                    }

                    // 创建视频/图片的具体目录并生成文件名
                    if (!fs::exists(videoidfileDir)) fs::create_directories(videoidfileDir);
                    std::string id = (file_type_judge == "video") ? json_e.json_getvid() : json_e.json_getpid();
                    filename = videoidfileDir + "/upload_" + file_type_judge + id + '.' + file_extention;

                    // 打开文件流（二进制追加模式，准备写入数据）
                    file.open(filename, std::ios::binary | std::ios::app);
                    if (!file.is_open()) {
                        throw std::runtime_error("Failed to open file: " + filename);
                    }

                    // 计算请求体的起始位置（头结束位置 + 4字节分隔符 "\r\n\r\n"）
                    size_t body_start = header_end + 4;
                    // 写入当前请求中的请求体数据（头解析后可能已包含部分体数据）
                    if (current_request_size > body_start) {
                        size_t body_size = current_request_size - body_start;
                        file.write(request.c_str() + body_start, body_size);
                        received_total += body_size;
                    }

                    // 标记头解析完成，后续仅接收请求体
                    headers_complete = true;
                    // 清空请求缓冲区（后续数据直接写入文件，避免内存堆积）
                    request.clear();
                }
            }
            // 4. 头解析完成后，直接接收请求体并写入文件
            else {
                // 写入本次读取的所有数据（request已清空，buffer是纯请求体）
                file.write(buffer, bytes_read);
                received_total += bytes_read;
                // 5. 检查上传是否完成（已接收字节数 == Content-Length）
                if (received_total >= content_length) {
                    printf("Received vp content_length : %zu bytes (total: %zu)\n", content_length, received_total);
                    if (received_total > content_length) {
                        size_t excess = received_total - content_length;
                        fs::resize_file(filename, content_length); // 直接调整文件大小
                    }
                    file.close(); // 关闭文件流
                    std::string final_path = removeHtdocPrefix(filename); // 处理路径前缀

                    // 6. 区分视频/图片，生成SQL并返回响应
                    json sql, content;
                    std::string response;
                    if (file_type_judge == "video") {
                        // 视频：插入数据库
                        sql["index"] = "INSERT";
                        sql["execute_type"] = "insert_video";
                        sql["uid"] = std::stoul(json_e.json_getuid());
                        sql["vid"] = std::stoul(json_e.json_getvid());
                        sql["videoaddress"] = final_path;
                        Produce_Sql(sql.dump(), sql["uid"].get<int>());

                        // 构建成功响应（带视频ID）
                        content["status"] = "success";
                        content["videoid"] = json_e.json_getvid();
                        response = build_http_response(200, "application/json", content.dump(),origin);
                    }
                    else {
                        // 图片：更新数据库
                        sql["index"] = "UPDATE"; // 修正原拼写 "UPDATA"
                        sql["execute_type"] = "updata_pic";
                        sql["vid"] = std::stoul(json_e.json_getvid());
                        sql["uid"] = std::stoul(json_e.json_getuid());
                        sql["pid"] = std::stoul(json_e.json_getpid());
                        sql["picaddress"] = final_path;
                        Produce_Sql(sql.dump(), sql["uid"].get<int>());

                        // 构建成功响应
                        response = build_http_response(200, "application/json", R"({"status":"success"})",origin);
                    }

                    // 通过SSL发送成功响应
                    ssl_safe_write(client, response);
                    return 1;
                }
            }
        }
    }
    // 7. 异常处理（网络错误、文件错误等）
    catch (const std::exception& e) {
        std::cerr << "Upload error: " << e.what() << std::endl;
        // 关闭文件流（若已打开）
        if (file.is_open()) file.close();
        // 删除不完整的上传文件
        if (!filename.empty() && fs::exists(filename)) {
            fs::remove(filename);
            std::cerr << "Incomplete file deleted: " << filename << std::endl;
        }
        // 返回500错误响应
		const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);
        ssl_safe_write(client, response);
        return 0;
    }

    return 1;
}



namespace user_upload {
    const int Sql_Capacity = 100;
    const int select_size = 100;
    std::mutex mtx;
    std::mutex user_locks_mutex;
    std::unordered_map<int, std::mutex> user_locks;
    std::deque<std::string> MYSQL_execute;
    std::condition_variable cv_producer;
    std::condition_variable cv_consumer;
    void  Handle_userdata_vneed_callback(const std::vector<sql_user::User_Data>&, SOCKET,std::string&);
    void Handle_userlogin_callback(SOCKET,int,sql_user::User_Data, std::string&);
    void Handle_usersubscribe_callback(SOCKET, int&, std::string&);
    void Handle_userthumbsup_callback(SOCKET, int&, std::string&);
    void Handle_sqlstatus_callback(SOCKET, int&, std::string&);
    int  Handle_usericon_Upload(SSL*);
    int Handle_update_profile(SSL*);
    void execute_sql(int id) {
       sql_user:: MYSQL_MS VP;

        while (true) {
            std::unique_lock<std::mutex> lock(mtx); // 自动锁定mtx

            // 1. 等待队列有数据（条件变量自动管理锁的释放与重获）
            cv_consumer.wait(lock, []() { return !MYSQL_execute.empty(); });

            // 2. 原子化获取并移除队列元素（在锁内完成，确保一致性）
            json consumed_product;
            try {
                consumed_product = std::move(json::parse(std::move(MYSQL_execute.front())));
                std::cout << "consumer:---------------------" << id << consumed_product << std::endl;
                MYSQL_execute.pop_front();
            }
            catch (const json::parse_error& e) {
                std::cerr << "JSON parse_error: " << e.what() << std::endl;
                MYSQL_execute.pop_front();
                continue; // 跳过当前循环，继续下一次消费
            }

            lock.unlock();

            // 4. 处理SQL操作（根据类型使用不同锁策略）
            if (consumed_product["index"] == "INSERT") {
                // INSERT无需用户级锁（假设VP内部线程安全或单线程操作）
                consumed_product.erase("index");
                if (consumed_product["execute_type"]=="saw_video")//
                {
                   VP.AddorUpdate_sv(consumed_product);
                }
        
            }
            else if (consumed_product["index"] == "UPDATE") {

                // 对同一用户加细粒度锁（避免全局阻塞，提升性能）
                try {
                    int user_id=0;
                    if(consumed_product["uid"].is_number())
                     user_id = consumed_product["uid"].get<int>();
                    else
					user_id = std::stoi(consumed_product["uid"].get<std::string>());

                    std::unique_lock<std::mutex> map_lock(user_locks_mutex);//维护用户哈希锁表
                    std::lock_guard<std::mutex> user_lock(user_locks[user_id]); // 为当前用户ID获取锁（自动创建新锁如果不存在）
                    map_lock.unlock();
                    consumed_product.erase("index");
                    if (consumed_product["execute_type"] == "video_click")//
                    {
                        VP.Update_videoclick(consumed_product);
                    }
                    else if (consumed_product["execute_type"] == "thumbs_down" || consumed_product["execute_type"] == "thumbs_up")
                    {
                        VP.Update_userthumbs(consumed_product, Handle_userthumbsup_callback);
                    }
                    else if (consumed_product["execute_type"] == "sub_minus" || consumed_product["execute_type"] == "sub_add")
                    {
                        VP.Update_usersub(consumed_product,Handle_usersubscribe_callback);
                                  
                    }
                    else if (consumed_product["execute_type"] == "unlike_undo" || consumed_product["execute_type"] == "unlike_do")
                    {

                         VP.AddorUpdate_sv(consumed_product);
					}
                    else if (consumed_product["execute_type"] == "updata_usericon")
                    {
                        VP.Update_userIcon(consumed_product);
                    }
					else if (consumed_product["execute_type"] == "update_profile")
					{
						VP.Update_userProfile(consumed_product, Handle_sqlstatus_callback);
					}

              
                }
                // 4. 针对不同异常类型的处理
                catch (const nlohmann::json::exception& e) {
                    // 处理JSON相关错误（如"uid"字段不存在、类型不是int）
                    std::cerr << "JSON handle error: " << e.what()
                        << "data: " << consumed_product.dump() << std::endl;
                    // 可选择记录日志后继续处理下一条数据
                }
                catch (const sql::SQLException& e) {
                    // 处理数据库操作错误（如SQL语法错误、连接断开）
                    std::cerr << "database update error: " << "error code=" << e.getErrorCode()
                        << " inf:" << e.what() << std::endl;
                    // 若为连接错误，可尝试重连VP（视MYSQL_MS实现而定）
                    // VP.reconnect(); 
                }
                catch (const std::system_error& e) {
                    // 处理锁操作相关错误（如系统资源不足导致锁创建失败）
                    std::cerr << "system_error: " << e.what()
                        << "code: " << e.code() << std::endl;
                }
                catch (const std::exception& e) {
                    // 捕获其他标准异常（如内存分配失败）
                    std::cerr << "std unknown error:  " << e.what() << std::endl;
                }
                catch (...) {
                    // 捕获所有非标准异常（如自定义异常）
                    std::cerr << "unknown error: " << std::endl;
                }
            }
            else if (consumed_product["index"] == "DELETE") {
                // DELETE需用户级锁（同UPDATA，确保同一用户操作互斥）
                int user_id = consumed_product["uid"].get<int>();
                std::unique_lock<std::mutex> map_lock(user_locks_mutex);
                std::lock_guard<std::mutex> user_lock(user_locks[user_id]);
                map_lock.unlock();
                consumed_product.erase("index");
            }
            else { // SELECT
                // SELECT通常无需锁（只读操作，除非有实时性要求）
                consumed_product.erase("index");
                if (consumed_product["execute_type"] == "select_userdata_vneed")
                {
                    VP.Select_User_VneedInf(consumed_product, Handle_userdata_vneed_callback);
                }
                else if (consumed_product["execute_type"] == "login")
                {

                    VP.check_login(consumed_product,Handle_userlogin_callback);
                }

            }

            // 5. 通知生产者队列有空位（无需持有锁，通知是原子操作）
            cv_producer.notify_all();



            // （可选）模拟消费耗时，此时已释放所有锁，不阻塞其他线程
            // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    }

    void Produce_Sql(std::string &&sql, int id)
    {
        bool p = true;//after improve 
        while (p) {
            std::unique_lock<std::mutex> lock(mtx);

            // 等待缓冲区有空位
            cv_producer.wait(lock, []() { return  MYSQL_execute.size() < Sql_Capacity; });

            // 生产数据并放入缓冲区
            std::cout << "Producer " << id << " produced:-------------------------------- " << sql << std::endl;
            MYSQL_execute.push_back(std::move(sql));
          

            // 通知消费者有数据可取
            cv_consumer.notify_all();

            p = false;
            // 释放锁，生产间隔一段时间（模拟生产过程）



        }


    }
    void Handle_usersubscribe_callback(SOCKET client, int& status, std::string&origin)
    {
        SSL* ssl = connectionPool.getSslBySocket(client);
        json send_data = {};
        if (ssl != nullptr)
        {
            try {
                send_data["status"] = "success";
                send_data["code"] = 200;
                send_data["subscribeStatus"] = status;
               
                std::cout << "send_data:  " << send_data.dump() << std::endl;
                std::string  response = build_http_response(200, "application/json", send_data.dump(4),origin);
                ssl_safe_write(ssl, response);
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
            catch (const std::exception& e) {
                std::cerr << "Error handling client usersubscribe response: " << e.what() << std::endl;
                // 异常时确保关闭Socket
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
        }
        else
        {
            std::cout << "ssl is null  " << socket << std::endl;
            //
        }




    }

    void Handle_sqlstatus_callback(SOCKET client, int& status, std::string& origin)
    {
        SSL* ssl = connectionPool.getSslBySocket(client);
        json send_data = {};
        if (ssl != nullptr)
        {
            try {
                send_data["status"] = "success";
                send_data["code"] = 200;
                send_data["sqlstatus"] = status;

                std::cout << "send_data:  " << send_data.dump() << std::endl;
                std::string  response = build_http_response(200, "application/json", send_data.dump(4), origin);
                ssl_safe_write(ssl, response);
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
            catch (const std::exception& e) {
                std::cerr << "Error handling client sqlstatus response: " << e.what() << std::endl;
                // 异常时确保关闭Socket
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
        }
        else
        {
            std::cout << "ssl is null  " << socket << std::endl;
            //
        }




    }


    void Handle_userthumbsup_callback(SOCKET client, int& status, std::string&origin)
    {
        SSL* ssl = connectionPool.getSslBySocket(client);
        json send_data = {};
        if (ssl != nullptr)
        {
            try {
                send_data["status"] = "success";
                send_data["code"] = 200;
                send_data["userthumbsupStatus"] = status;

                std::cout << "send_data:  " << send_data.dump() << std::endl;
                std::string  response = build_http_response(200, "application/json", send_data.dump(4),origin);
                ssl_safe_write(ssl, response);
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
            catch (const std::exception& e) {
                std::cerr << "Error handling client userthumbsup response: " << e.what() << std::endl;
                // 异常时确保关闭Socket
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
        }
        else
        {
            std::cout << "ssl is null  " << socket << std::endl;
            //
        }




    }

    void  Handle_userlogin_callback(SOCKET client, int status ,sql_user::User_Data inf, std::string&origin)
    {
        SSL* ssl = connectionPool.getSslBySocket(client);
        json send_data = {};
        if (ssl != nullptr)
        {
            try {
                send_data["status"] = "success";
                send_data["code"] = 200;
                send_data["loginStatus"] = status;
                // 将数据数组放入响应JSON
                send_data["userId"] = inf.uid ;
                send_data["userName"] = inf.username;
                send_data["Token"] = inf.token;
				send_data["userIcon"] = inf.usericon;
                std::cout << "send_data:  " << send_data.dump() << std::endl;
                std::string  response = build_http_response(200, "application/json", send_data.dump(4),origin);
                ssl_safe_write(ssl, response);
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
            catch (const std::exception& e) {
                std::cerr << "Error handling client userlogin response: " << e.what() << std::endl;
                // 异常时确保关闭Socket
                connectionPool.setConnectionStateByssl(ssl, ConnectionState::Closed);
                connectionPool.closeConnectionByssl(ssl);
            }
        }
        else
        {
            std::cout << "ssl is null  " << socket << std::endl;
            //
        }





    }

    void Handle_userdata_vneed_callback(const std::vector<sql_user::User_Data>& userdata,SOCKET client, std::string& origin)
    {
        SSL* ssl = {};
        json send_data = {};
        SOCKET socket = client;
        ssl = connectionPool.getSslBySocket(client);//线程安全
        if (ssl != nullptr)
        {
            try {
                send_data["status"] = "success";
                send_data["code"] = 200;
                send_data["count"] = userdata.size();//支持隐式转换

                // 1.2 核心数据数组（JS端可通过 Array 遍历，字段名与JS习惯对齐）
                json video_list = json::array();          // 数组类型，JS可直接 forEach 遍历
                for (const auto& record : userdata) {
                    json video_item = {};
                    video_item["authorId"] = record.uid;
                    video_item["followNum"] = record.follownum;
                    video_item["userIcon"] = record.usericon;
                    video_item["userName"] = record.username;  //
                    video_item["introduce"] = record.introduce;
					video_item["videoNum"] = record.videonum;
                    video_item["userInf"] = record.User_inf;
					
                    // 添加到数据数组
                    video_list.push_back(video_item);
                }
                // 将数据数组放入响应JSON
                send_data["data"] = video_list;
                std::cout << "send_data:  " << send_data.dump() << std::endl;
                std::string  response = build_http_response(200, "application/json", send_data.dump(4),origin);
                ssl_safe_write(ssl, response);
                connectionPool.setConnectionState(socket, ConnectionState::Closed);
                connectionPool.closeConnection(socket);
            }
            catch (const std::exception& e) {
                std::cerr << "Error handling client userdata_vneed response: " << e.what() << std::endl;
                // 异常时确保关闭Socket
                connectionPool.setConnectionState(socket, ConnectionState::Closed);
                connectionPool.closeConnection(socket);
            }
        }
        else
        {
            std::cout << "ssl is null  " << socket << std::endl;
            //
        }
    }
    int Handle_userdata_vneed(SSL* client)
    {
        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {};
        std::string request;
        bool headers_complete = false;
        std::string userid, videoid, data_type, handle_type,authorid;
        size_t pos;

        try {
            // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
            while (!headers_complete) {
                // 调用 SSL 安全读取函数
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

                // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
                if (bytes_read == 0) {
                    continue;
                }

                // 将读取到的明文追加到请求字符串
                request.append(buffer, bytes_read);

                // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
                size_t header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                    std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除行尾可能残留的 \r（Windows 换行符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        // 解析 data_type 字段
                        if (findCaseInsensitive(line,"data_type:") != std::string::npos) {
                            pos = line.find(':');
                            data_type = line.substr(pos + 1);
                            // 去除字段值前后的空格/制表符
                            data_type.erase(0, data_type.find_first_not_of(" \t"));
                        }
                        // 解析 handle_type 字段
                        else if (findCaseInsensitive(line, "handle_type:") != std::string::npos) {
                            pos = line.find(':');
                            handle_type = line.substr(pos + 1);
                            handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                        }
                        // 解析 userid 字段
                        else if (findCaseInsensitive(line,"userid:") != std::string::npos) {
                            pos = line.find(':');
                            userid = line.substr(pos + 1);
                            userid.erase(0, userid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "authorid:") != std::string::npos) {
                            pos = line.find(':');
                            authorid = line.substr(pos + 1);
                            authorid.erase(0, authorid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                    }

                    headers_complete = true; // 标记头解析完成
                }
            }

            // 3. 构造 SQL 请求（原有业务逻辑不变）
            json sql;
            int socket = SSL_get_fd(client);
            sql["index"] = "SELECT";       // 修正原拼写 "SElECT"
            sql["socket"] = socket;  
            sql["origin"] = origin;
            sql["uid"] = std::stoi(userid); 
            sql["authorid"] = std::stoul(authorid); // authorid
            sql["execute_type"] = handle_type; // 处理类型（如查询类型）
            sql["data_type"] = data_type;     // 数据类型

            // 生产 SQL 任务（原有逻辑不变）
            Produce_Sql(sql.dump(), sql["uid"].get<int>());

            // 设置连接状态为等待（原有逻辑不变）
            connectionPool.setConnectionState(socket, ConnectionState::Wait);

            return 1;

        }
        catch (const std::exception& e) {
            // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
            std::cerr << "Error: " << e.what() << std::endl;
			const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);

            // 用 SSL 写入响应，替代原 send
            ssl_safe_write(client, response);

            return 0;
        }

        return 1;




    }

    int Handle_user_subscribe(SSL* client)
    {

        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {};
        std::string request;
        bool headers_complete = false;
        std::string userid, videoid, data_type, handle_type, authorid;
        size_t pos;

        try {
            // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
            while (!headers_complete) {
                // 调用 SSL 安全读取函数
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

                // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
                if (bytes_read == 0) {
                    continue;
                }

                // 将读取到的明文追加到请求字符串
                request.append(buffer, bytes_read);

                // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
                size_t header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                    std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除行尾可能残留的 \r（Windows 换行符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        if (findCaseInsensitive(line, "data_type:") != std::string::npos) {
                            pos = line.find(':');
                            data_type = line.substr(pos + 1);
                            // 去除字段值前后的空格/制表符
                            data_type.erase(0, data_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "handle_type:") != std::string::npos) {
                            pos = line.find(':');
                            handle_type = line.substr(pos + 1);
                            handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                            pos = line.find(':');
                            userid = line.substr(pos + 1);
                            userid.erase(0, userid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "authorid:") != std::string::npos) {
                            pos = line.find(':');
                            authorid = line.substr(pos + 1);
                            authorid.erase(0, authorid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                    }

                    headers_complete = true; // 标记头解析完成
                }
            }

            json sql;
            int socket = connectionPool.getSocketBySsl(client);
            sql["socket"] = socket;
            sql["index"] = "UPDATE";    
			sql["origin"] = origin;
            sql["uid"] = std::stoul(userid);
            sql["authorid"] = std::stoul(authorid); // authorid
            sql["execute_type"] = handle_type; // 处理类型（如查询类型）
            sql["data_type"] = data_type;     // 数据类型

            // 生产 SQL 任务（原有逻辑不变）
            Produce_Sql(sql.dump(), sql["uid"].get<int>());
            std::cout << "select sql: " << sql.dump() << std::endl;
            connectionPool.setConnectionState(socket, ConnectionState::Wait);

            return 1;

        }
        catch (const std::exception& e) {
            // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
            std::cerr << "Error: " << e.what() << std::endl;
			const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);

            // 用 SSL 写入响应，替代原 send
            ssl_safe_write(client, response);

            return 0;
        }

        return 1;



    }
    int Handle_userthumbsup(SSL* client)
    {
        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {};
        std::string request;
        bool headers_complete = false;
        std::string userid, videoid, data_type, handle_type;
        size_t pos;

        try {
            // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
            while (!headers_complete) {
                // 调用 SSL 安全读取函数
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

                // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
                if (bytes_read == 0) {
                    continue;
                }

                // 将读取到的明文追加到请求字符串
                request.append(buffer, bytes_read);

                // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
                size_t header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                    std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除行尾可能残留的 \r（Windows 换行符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        if (findCaseInsensitive(line, "data_type:") != std::string::npos) {
                            pos = line.find(':');
                            data_type = line.substr(pos + 1);
                            // 去除字段值前后的空格/制表符
                            data_type.erase(0, data_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "handle_type:") != std::string::npos) {
                            pos = line.find(':');
                            handle_type = line.substr(pos + 1);
                            handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                            pos = line.find(':');
                            userid = line.substr(pos + 1);
                            userid.erase(0, userid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "videoid:") != std::string::npos) {
                            pos = line.find(':');
                            videoid = line.substr(pos + 1);
                            videoid.erase(0, videoid.find_first_not_of(" \t"));
                        }

                    }

                    headers_complete = true; // 标记头解析完成
                }
            }

            json sql;
            int socket = connectionPool.getSocketBySsl(client);
            sql["socket"] = socket;
            sql["index"] = "UPDATE";
			sql["origin"] = origin;
            sql["uid"] = std::stoul(userid);
            sql["vid"] = std::stoul(videoid); 
            sql["execute_type"] = handle_type; // 处理类型（如查询类型）
            sql["data_type"] = data_type;     // 数据类型

            // 生产 SQL 任务（原有逻辑不变）
            Produce_Sql(sql.dump(), sql["uid"].get<int>());
            connectionPool.setConnectionState(socket, ConnectionState::Wait);

            return 1;

        }
        catch (const std::exception& e) {
            // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
            std::cerr << "Error: " << e.what() << std::endl;
			const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);

            // 用 SSL 写入响应，替代原 send
            ssl_safe_write(client, response);

            return 0;
        }

        return 1;

    }
    int Handle_user_unlike(SSL* client)
    {
        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 持有对象，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {};
        std::string request;
        bool headers_complete = false;
        std::string userid, videoid, data_type, handle_type;
        size_t pos;

        try {
            // 1. 读取 HTTP 请求头（通过 SSL 读取，替代原 recv）
            while (!headers_complete) {
                // 调用 SSL 安全读取函数
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_1);

                // 处理 "暂时无数据" 情况（类似原 WSAEWOULDBLOCK）
                if (bytes_read == 0) {
                    continue;
                }

                // 将读取到的明文追加到请求字符串
                request.append(buffer, bytes_read);

                // 检查 HTTP 头是否结束（\r\n\r\n 为头分隔符）
                size_t header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    // 2. 解析 HTTP 头中的自定义字段（userid/videoid 等）
                    std::istringstream iss(request.substr(0, header_end)); // 截取头部分
                    std::string line;
                    while (std::getline(iss, line)) {
                        // 移除行尾可能残留的 \r（Windows 换行符）
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        if (findCaseInsensitive(line, "data_type:") != std::string::npos) {
                            pos = line.find(':');
                            data_type = line.substr(pos + 1);
                            // 去除字段值前后的空格/制表符
                            data_type.erase(0, data_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line,"handle_type:") != std::string::npos) {
                            pos = line.find(':');
                            handle_type = line.substr(pos + 1);
                            handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                            pos = line.find(':');
                            userid = line.substr(pos + 1);
                            userid.erase(0, userid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line,"videoid:") != std::string::npos) {
                            pos = line.find(':');
                            videoid = line.substr(pos + 1);
                            videoid.erase(0, videoid.find_first_not_of(" \t"));
                        }
                        else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                            pos = line.find(':');
                            if (pos != std::string::npos) {
                                origin = line.substr(pos + 1);
                                origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                            }
                        }
                    }

                    headers_complete = true; // 标记头解析完成
                }
            }

            json sql;
            int socket = connectionPool.getSocketBySsl(client);
            sql["socket"] = socket;
            sql["index"] = "UPDATE";
			sql["origin"] = origin;
            sql["uid"] = std::stoul(userid);
            sql["vid"] = std::stoul(videoid);
            sql["execute_type"] = handle_type; // 处理类型（如查询类型）
            sql["data_type"] = data_type;     // 数据类型

            // 生产 SQL 任务（原有逻辑不变）
            Produce_Sql(sql.dump(), sql["uid"].get<int>());
            sql.clear();
            sql["status"] = "success";
            sql["code"] = 200;
            std::string  response = build_http_response(200, "application/json", sql.dump(4),origin);
            ssl_safe_write(client, response);
            return 1;

        }
        catch (const std::exception& e) {
            // 4. 异常处理：返回 500 内部错误响应（通过 SSL 发送）
            std::cerr << "Error: " << e.what() << std::endl;
			const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);

            // 用 SSL 写入响应，替代原 send
            ssl_safe_write(client, response);

            return 0;
        }

        return 1;

    }


    void Handle_videostart(SSL* ssl, int& userid, int& videoid)//处理视频click，sawvideo插入或更新，
    {
        json sql = {};
        sql["vid"] = videoid;
        sql["index"] = "INSERT";
        sql["uid"] = userid;
        sql["execute_type"] = "saw_video";
        Produce_Sql(sql.dump(), userid);
        sql["index"] = "UPDATE";
        sql["execute_type"] = "video_click";
        Produce_Sql(sql.dump(), userid);
      

    }


    int  Handle_usericon_Upload(SSL* client) {
        Buffer_Malloc bufferObj(BUFFER_SIZE_3);
        ElementType* buffer = bufferObj.get();
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        const std::string uploadDir = "htdocs/MUSE/vp";
        // 上传核心变量初始化
        std::string request = {};               // 存储接收的请求数据（头+体）
        size_t content_length = 0;         // 从HTTP头获取的总内容长度
        size_t current_request_size = {};
        bool headers_complete = false;     // 标记请求头是否解析完成
        size_t received_total = 0;         // 已接收的总字节数
        std::ofstream file = {};                // 上传文件的输出流（提前声明，避免作用域问题）
        int pos;
        std::string origin = {};
        // 业务变量
        std::string file_type, file_extention;
        std::string userid;
        std::string userfileDir, IconfilerDir, filename;
        json_execute json_e;

        // 确保上传根目录存在
        if (!fs::exists(uploadDir)) {
            try {
                fs::create_directories(uploadDir);
            }
            catch (const fs::filesystem_error& e) {
                std::cerr << "Failed to create upload dir: " << e.what() << std::endl;
                return 0;
            }
        }

        try {
            // 循环接收请求数据（头+体）
            while (true) {
                // 1. 通过SSL读取数据（替代原 recv）
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_3);
                if (bytes_read == 0) {
                    continue; // 暂时无数据，继续循环
                }

                // 将读取的明文追加到请求缓冲区
                request.append(buffer, bytes_read);
                current_request_size = request.size(); // 当前请求总长度

                // 2. 解析HTTP请求头（仅首次解析）
                if (!headers_complete) {
                    size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符
                    if (header_end != std::string::npos) {
                        // 解析头字段（Content-Length、Content-Type、userid、videoid）
                        std::istringstream iss(request.substr(0, header_end));
                        std::string line;
                        while (std::getline(iss, line)) {
                            // 移除Windows换行符残留的 \r
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }

                            // 解析 Content-Length（必选，用于判断上传是否完成）
                            if (findCaseInsensitive(line,"Content-Length:") != std::string::npos) {
                                size_t colon_pos = line.find(':');
                                if (colon_pos != std::string::npos) {
                                    content_length = std::stoul(line.substr(colon_pos + 1));
                                }
                            }
                            // 解析 Content-Type（获取文件类型和扩展名）
                            else if (findCaseInsensitive(line, "Content-Type:") != std::string::npos) {
                                size_t colon_pos = line.find(':');
                                if (colon_pos != std::string::npos) {
                                    std::string content_type = line.substr(colon_pos + 1);
                                    content_type.erase(0, content_type.find_first_not_of(" \t"));
                                    // 提取文件类型（如 "image/jpeg" → "image"）和扩展名（"jpeg"）
                                    size_t slash_pos = content_type.find('/');
                                    if (slash_pos != std::string::npos) {
                                        file_type= content_type.substr(0, slash_pos);
                                        file_extention = content_type.substr(slash_pos + 1);
                                    }
                                }
                            }
                            // 解析 userid（上传目录依赖）
                            else if (findCaseInsensitive(line, "userid:") != std::string::npos) {
                                size_t colon_pos = line.find(':');
                                if (colon_pos != std::string::npos) {
                                    userid = line.substr(colon_pos + 1);
                                    userid.erase(0, userid.find_first_not_of(" \t"));
                                }
                            }
                            else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    origin = line.substr(pos + 1);
                                    origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                                }
                            }
                        }

                        // 3. 头解析后验证与初始化
                        // 验证文件大小（超过1MB返回413）
                        if (content_length > MAX_FILE_SIZE_1) {
                            const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                            ssl_safe_write(client, response);
                            return 0;
                        }

                        // 验证 userid 非空（否则返回400）
                        if (userid.empty()) {
                            const std::string response =
                                "HTTP/1.1 400 Bad Request (empty userid)\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n\r\n";
                            ssl_safe_write(client, response);
                            return 0;
                        }

                        // 创建用户目录和文件类型目录（如 uploadDir/123/images）
                        userfileDir = uploadDir + '/' + userid;
						IconfilerDir = userfileDir + "/usericon";
                        try {
                            if (!fs::exists(IconfilerDir)) fs::create_directories(IconfilerDir);
                        }
                        catch (const fs::filesystem_error& e) {
                            throw std::runtime_error("Failed to create user/file dir: " + std::string(e.what()));
                        }
                        auto& gen = get_thread_safe_generator();
                        std::uniform_int_distribution<int> dist(1, 100000);
                         int random_id = dist(gen); 
                        filename = IconfilerDir+'/'+file_type+userid+'_'+std::to_string(random_id)+'.'+ file_extention;

                        // 打开文件流（二进制追加模式，准备写入数据）
                        file.open(filename, std::ios::binary | std::ios::app);
                        if (!file.is_open()) {
                            throw std::runtime_error("Failed to open file: " + filename);
                        }

                        // 计算请求体的起始位置（头结束位置 + 4字节分隔符 "\r\n\r\n"）
                        size_t body_start = header_end + 4;
                        // 写入当前请求中的请求体数据（头解析后可能已包含部分体数据）
                        if (current_request_size > body_start) {
                            size_t body_size = current_request_size - body_start;
                            file.write(request.c_str() + body_start, body_size);
                            received_total += body_size;
                        }

                        // 标记头解析完成，后续仅接收请求体
                        headers_complete = true;
                        // 清空请求缓冲区（后续数据直接写入文件，避免内存堆积）
                        request.clear();
                    }
                }
                // 4. 头解析完成后，直接接收请求体并写入文件
                else {
                    // 写入本次读取的所有数据（request已清空，buffer是纯请求体）
                    file.write(buffer, bytes_read);
                    received_total += bytes_read;

                    // 5. 检查上传是否完成（已接收字节数 == Content-Length）
                    if (received_total >= content_length) {
                        printf("Received Husericon content_length : %zu bytes (total: %zu)\n", content_length, received_total);
                        if (received_total > content_length) {
                            size_t excess = received_total - content_length;
                            fs::resize_file(filename, content_length); // 直接调整文件大小
                        }
                        file.close(); // 关闭文件流
                        std::string final_path = removeHtdocPrefix(filename); // 处理路径前缀

                        // 6. 区分视频/图片，生成SQL并返回响应
                        json sql,content;
                        std::string response;
                            // 图片：更新数据库
                            sql["index"] = "UPDATE"; // 修正原拼写 "UPDATA"
                            sql["execute_type"] = "updata_usericon";
                            sql["uid"] = std::stoul(userid);
                            sql["usericon"] = final_path;
                            Produce_Sql(sql.dump(), sql["uid"].get<int>());
							content["status"] = "success";
							content["code"] = 200;
							content["newUserIconUrl"] = final_path;
                            // 构建成功响应
                            response = build_http_response(200, "application/json", content.dump(), origin);

                        // 通过SSL发送成功响应
                        ssl_safe_write(client, response);
                        return 1;
                    }
                }
            }
        }
        // 7. 异常处理（网络错误、文件错误等）
        catch (const std::exception& e) {
            std::cerr << "Upload error: " << e.what() << std::endl;
            // 关闭文件流（若已打开）
            if (file.is_open()) file.close();
            // 删除不完整的上传文件
            if (!filename.empty() && fs::exists(filename)) {
                fs::remove(filename);
                std::cerr << "Incomplete file deleted: " << filename << std::endl;
            }
            // 返回500错误响应
            const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);
            ssl_safe_write(client, response);
            return 0;
        }

        return 1;
    }

    int Handle_update_profile(SSL* client) {
        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 1KB 缓冲区，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {};
        std::string request;
        size_t content_length = 0;
        bool headers_complete = false;
        size_t received_total = 0;
        std::string json_str;
        size_t pos;
        size_t body_start = 0;

        try {
            while (true) {
                // 2. 读取数据：recv() → ssl_safe_read()（SSL 解密后读取明文）
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_2);
                if (bytes_read == 0) {
                    continue; // 暂时无数据，继续循环等待
                }

                // 将读取的明文追加到请求缓冲区
                request.append(buffer, bytes_read);

                size_t current_request_size = request.size();
                if (!headers_complete) {
                    size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符（\r\n\r\n）
                    if (header_end != std::string::npos) {
                        std::istringstream iss(request.substr(0, header_end));
                        std::string line;
                        while (std::getline(iss, line)) {
                            // 移除 Windows 换行符残留的 \r（避免解析无效字符）
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }

                            // 解析 Content-Length（判断请求体是否完整）
                            if (findCaseInsensitive(line, "Content-Length:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    std::string len_str = line.substr(pos + 1);
                                    len_str.erase(0, len_str.find_first_not_of(" \t"));
                                    content_length = std::stoul(len_str);
                                }
                            }
                            else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    origin = line.substr(pos + 1);
                                    origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                                }
                            }

                        }

                        // 4. 头解析后验证：请求体大小是否超过 1MB 限制
                        if (content_length > MAX_FILE_SIZE_1) {
                            const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                            ssl_safe_write(client, response); // SSL 发送 413 响应
                            return 0;
                        }

                        body_start = header_end + 4; // 4 = "\r\n\r\n" 的字节数
                        // 提取当前请求中的首次 JSON 数据（头后可能已包含部分体数据）
                        if (current_request_size > body_start) {
                            size_t initial_body_size = current_request_size - body_start;
                            json_str.append(request.c_str() + body_start, initial_body_size);
                            received_total += initial_body_size;
                        }

                        // 标记头解析完成，后续仅接收 JSON 数据
                        headers_complete = true;
                        // 清空请求缓冲区（后续数据直接追加到 json_str，避免内存堆积）
                        request.clear();
                    }
                }
                // 6. 头解析完成后，直接接收剩余 JSON 数据
                else {
                    json_str.append(buffer, bytes_read);
                    received_total += bytes_read;
                }

                // 7. 检查 JSON 数据是否接收完整（用 >= 避免多读导致阻塞）
                if (received_total >= content_length) {
                    // 截断超出 Content-Length 的冗余数据（防止 JSON 解析错误）
                    if (received_total > content_length) {
                        json_str.resize(content_length);
                        printf("Truncated excess data: %zu bytes (final JSON size: %zu)\n",
                            received_total - content_length, content_length);
                    }
                    try {
                        int sock = SSL_get_fd(client);
                        json comment_content = json::parse(json_str); // 解析请求参数
                        std::string file_path;
                        json sql;
                        sql["index"] = "UPDATE";
                        sql["execute_type"] = "update_profile";
                        sql["socket"] = sock;
                        sql["origin"] = origin;
                        sql.update(comment_content);
                        Produce_Sql(sql.dump(), std::stoul((comment_content["uid"].get<std::string>())));
                        connectionPool.setConnectionState(sock, ConnectionState::Wait);
                        return 1;

                    }
                    catch (const json::parse_error& e) {
                        // 处理 JSON 解析错误（返回 400 错误）
                        throw std::runtime_error("Invalid update_profile JSON: " + std::string(e.what()));
                    }
                }
            }
        }
        // 11. 异常处理（网络错误、JSON 解析错误等）
        catch (const std::exception& e) {
            std::cerr << "update_profile request error: " << e.what() << std::endl;
            // 发送 500 错误响应（SSL 加密）
            const std::string response = build_http_response(500, "text/plain", "Internal Server Error", origin);
            ssl_safe_write(client, response);

            // 异常时关闭 SSL 连接
            if (client != NULL) {
                SOCKET sock = static_cast<SOCKET>(SSL_get_fd(client));
                connectionPool.setConnectionState(sock, ConnectionState::Closed);
                connectionPool.closeConnection(sock);
            }
            return 0;
        }

        return 1;
    }



    int Handle_user_login(SSL* client)
    {

        Buffer_Malloc bufferObj(BUFFER_SIZE_1);  // 10KB 缓冲区，延长生命周期
        ElementType* buffer = bufferObj.get(); // 获取缓冲区指针
        if (buffer == NULL) {
            std::cerr << "Buffer allocation failed" << std::endl;
            return 0;
        }
        std::string origin = {}; // 用于存储 Origin 头字段
        // 核心变量初始化
        std::string request;               // 存储接收的请求数据（头+体）
        size_t content_length = 0;         // HTTP 头中的 Content-Length
        bool headers_complete = false;     // 标记请求头是否解析完成
        size_t received_total = 0;         // 已接收的 JSON 数据总长度
        std::string json_str;              // 存储删除请求的 JSON 数据
        std::string handle_type;   // 业务字段：用户ID、删除类型
        size_t pos;                        // 字符串查找辅助变量
        size_t body_start = 0;             // 请求体起始位置（仅计算一次，修复原重复累加问题）
        try {
            while (true) {
                // 2. 读取数据：recv() → ssl_safe_read()（SSL 解密后读取明文）
                int bytes_read = ssl_safe_read(client, buffer, BUFFER_SIZE_2);
                if (bytes_read == 0) {
                    continue; // 暂时无数据，继续循环等待
                }

                // 将读取的明文追加到请求缓冲区
                request.append(buffer, bytes_read);
                size_t current_request_size = request.size();

                // 3. 解析 HTTP 请求头（仅首次解析，避免重复计算）
                if (!headers_complete) {
                    size_t header_end = request.find("\r\n\r\n"); // 头与体的分隔符（\r\n\r\n）
                    if (header_end != std::string::npos) {
                        // 解析头字段：Content-Length、handle_type、userid
                        std::istringstream iss(request.substr(0, header_end));
                        std::string line;
                        while (std::getline(iss, line)) {
                            // 移除 Windows 换行符残留的 \r（避免解析无效字符）
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
							std::cout << line << std::endl;
                            // 解析 Content-Length（判断请求体是否完整）
                            if (findCaseInsensitive(line, "Content-Length:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    std::string len_str = line.substr(pos + 1);
                                    len_str.erase(0, len_str.find_first_not_of(" \t"));
                                    content_length = std::stoul(len_str);
                                }
                            }
                            else if (findCaseInsensitive(line, "Handle_type:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    handle_type = line.substr(pos + 1);
                                    handle_type.erase(0, handle_type.find_first_not_of(" \t"));
                                }
                            }
                            else if (findCaseInsensitive(line, "Origin:") != std::string::npos) {
                                pos = line.find(':');
                                if (pos != std::string::npos) {
                                    origin = line.substr(pos + 1);
                                    origin.erase(0, origin.find_first_not_of(" \t")); // 去除空格
                                }
                            }

                   
                        }

                        // 4. 头解析后验证：请求体大小是否超过 1MB 限制
                        if (content_length > MAX_FILE_SIZE_1) {
							const std::string response = build_http_response(413, "text/plain", "Payload Too Large", origin);
                            ssl_safe_write(client, response); // SSL 发送 413 响应
                            return 0;
                        }

                        body_start = header_end + 4; // 4 = "\r\n\r\n" 的字节数
                        if (current_request_size > body_start) {
                            size_t initial_body_size = current_request_size - body_start;
                            json_str.append(request.c_str() + body_start, initial_body_size);
                            received_total += initial_body_size;
                        }

                        headers_complete = true;
                        // 清空请求缓冲区（后续数据直接追加到 json_str，避免内存堆积）
                        request.clear();
                    }
                }
                // 6. 头解析完成后，直接接收剩余 JSON 数据
                else {
                    json_str.append(buffer, bytes_read);
                    received_total += bytes_read;
                }

                // 7. 检查 JSON 数据是否接收完整（用 >= 避免多读导致阻塞）
                if (headers_complete && received_total >= content_length) {
                    // 截断超出 Content-Length 的冗余数据（防止 JSON 解析错误）
                    if (received_total > content_length) {
                        json_str.resize(content_length);
                        printf("Truncated excess data: %zu bytes (final JSON size: %zu)\n",
                            received_total - content_length, content_length);
                    }

                    try {
                        json content = json::parse(json_str); 
                        json sql;
                        int socket = connectionPool.getSocketBySsl(client);
						sql["origin"] = origin;
                        sql["index"] = "SELECT";
                        sql["socket"] = socket;
                        sql["execute_type"] = handle_type;       
                        sql.update(content);              
                        Produce_Sql(sql.dump(), -1);//登录
                        connectionPool.setConnectionState(socket, ConnectionState::Wait);
                        return 1;

                    }
                    catch (const json::parse_error& e) {
                        // 处理 JSON 解析错误（返回 400 错误）
                        throw std::runtime_error("Invalid login JSON: " + std::string(e.what()));
                    }
                }
            }
        }
        // 11. 异常处理（网络错误、JSON 解析错误等）
        catch (const std::exception& e) {
            std::cerr << "login request error: " << e.what() << std::endl;
            // 发送 500 错误响应（SSL 加密）
			const std::string response = build_http_response(500, "text/plain", "Internal Server Error",origin);
            ssl_safe_write(client, response);

            // 异常时关闭 SSL 连接
            if (client != NULL) {
                SOCKET sock = static_cast<SOCKET>(SSL_get_fd(client));
                connectionPool.setConnectionState(sock, ConnectionState::Closed);
                connectionPool.closeConnection(sock);
            }
            return 0;
        }

        return 1;

    }




}