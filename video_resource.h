//{{NO_DEPENDENCIES}}
// Microsoft Visual C++ generated include file.
// Used by simple httpd.rc

// 新对象的下一组默认值
// 
#ifndef VIDEO_RESOURCE_H
#define VIDEO_RESOURCE_H


#include<iostream>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/exception.h>

#include <jdbc/cppconn/prepared_statement.h>
#include "jdbc/mysql_driver.h"
#include<vector>
#include <nlohmann/json.hpp>
#include<set>
#include <chrono>
#include<random>
#include<queue>
#include <unordered_set>
using json = nlohmann::json;
static int Videoid_Max = 1;
static int Videoid_Min = 1;
static int Uid_Max = 1;
static int Pid_Max = 1;


struct Video_Data
{
    int vid = 0;
    int uid = 0;
    int pid = 0;
    std::string videoname = {};
    std::string picaddress = {};
    std::string videoaddress = {};
  
    json Video_inf
    {
        {"self_done"," "},
        {"subarea"," "},
        {"tag", {" "," "," "," "," "}},
        {"video_click",0},
        {"video_like",0},
        {"introduce" ," "},
        {"launch_time"," "},
        {"video_type" ," "},//short or video
        {"duration",""},
        {"username",""},
        {"usericon",""}
    };
    int videostatus = 0;
    //extra userinf
    std::string usericon = {};
    std::string username = {};
};
enum class VideoCommentStatus : int8_t {
    kPendingReview = 0,  // 0：待审核
    kApproved = 1,       // 1：已通过
    kDeleted = 2         // 2：已删除（逻辑删除，业务层标记后不展示）
};

struct VideoComment {

    int32_t id=0;

 
    int32_t video_id=0;

    int32_t user_id=0;

    int32_t parent_id =0;

    std::string content = {};


    int32_t like_count=0;


    int32_t reply_count=0;

    VideoCommentStatus status = {};

    std::string created_at = {};

    std::string  updated_at = {};
    //extra userinf
	std::string username = {};
	std::string usericon = {};
};

#include <winsock2.h>
#include <unordered_map>
#include <mutex>
#include <stdexcept>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
enum class ConnectionState {
    Active,  // 活跃状态
    Wait,    // 等待状态（不关闭）
    Closed   // 已关闭状态
};

class SslSocketConnectionPool {
private:
    // 存储Socket、对应的SSL对象及其状态
    struct ConnectionData {
        SSL* ssl;
        ConnectionState state;
    };

    std::unordered_map<SOCKET, ConnectionData> connections;
    mutable std::mutex mtx;                  // 互斥锁，确保线程安全

public:
    // 构造函数和析构函数
    SslSocketConnectionPool() = default;
    ~SslSocketConnectionPool() {
        closeAllConnections();  // 析构时关闭所有连接
    }

    // 禁止拷贝构造和赋值，避免线程安全问题
    SslSocketConnectionPool(const SslSocketConnectionPool&) = delete;
    SslSocketConnectionPool& operator=(const SslSocketConnectionPool&) = delete;

    // 添加新连接，包含SSL对象，初始状态为Active
    void addConnection(SOCKET sock, SSL* ssl) {
        if (sock == INVALID_SOCKET || !ssl) {
            throw std::invalid_argument("Invalid socket or SSL object");
        }
        std::lock_guard<std::mutex> lock(mtx);
        // 如果已存在则更新，否则添加新条目
        connections[sock] = { ssl, ConnectionState::Active };
    }

    // 手动设置连接的状态
    bool setConnectionState(SOCKET sock, ConnectionState state) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = connections.find(sock);
        if (it != connections.end()) {
            // 已关闭的连接不能再修改状态
            if (it->second.state == ConnectionState::Closed) {
                return false;
            }
            it->second.state = state;
            return true;
        }
        return false;  // 未找到该连接
    }

    // 获取连接的当前状态
    ConnectionState getConnectionState(SOCKET sock) const {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = connections.find(sock);
        if (it != connections.end()) {
            return it->second.state;
        }
        // 不存在的连接视为已关闭
        return ConnectionState::Closed;
    }

    // 通过socket获取对应的SSL对象
    SSL* getSslBySocket(SOCKET sock) const {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = connections.find(sock);
        if (it != connections.end()) {
            return it->second.ssl;
        }
        return nullptr;  // 未找到该连接
    }

    // 通过SSL对象获取对应的socket
    SOCKET getSocketBySsl(SSL* ssl) const {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& pair : connections) {
            if (pair.second.ssl == ssl) {
                return pair.first;
            }
        }
        return INVALID_SOCKET;  // 未找到对应的socket
    }

    // 获取所有连接的socket
    std::vector<SOCKET> getAllSockets() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<SOCKET> result;
        for (const auto& pair : connections) {
            result.push_back(pair.first);
        }
        return result;
    }

    // 获取所有SSL对象
    std::vector<SSL*> getAllSslObjects() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<SSL*> result;
        for (const auto& pair : connections) {
            result.push_back(pair.second.ssl);
        }
        return result;
    }

    // 获取指定状态的所有Socket
    std::vector<SOCKET> getSocketsByState(ConnectionState state) const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<SOCKET> result;
        for (const auto& pair : connections) {
            if (pair.second.state == state) {
                result.push_back(pair.first);
            }
        }
        return result;
    }

    // 获取指定状态的所有SSL对象
    std::vector<SSL*> getSslByState(ConnectionState state) const {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<SSL*> result;
        for (const auto& pair : connections) {
            if (pair.second.state == state) {
                result.push_back(pair.second.ssl);
            }
        }
        return result;
    }

    // 关闭指定连接并从池中移除
    bool closeConnection(SOCKET sock) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = connections.find(sock);
        if (it != connections.end()) {
            ConnectionState currentState = it->second.state;

            // 1. 仅当状态为 Active/Closed 时，才释放 SSL 并关闭 Socket
            if (currentState == ConnectionState::Active || currentState == ConnectionState::Closed) {
                // 处理 SSL 对象：关闭并释放
                if (it->second.ssl) {
                    SSL_shutdown(it->second.ssl);  // 关闭 SSL 会话（发送关闭通知）
                    SSL_free(it->second.ssl);      // 释放 SSL 对象内存
                }
                // 处理 Socket：关闭并从池中移除
                closesocket(it->first);  // 关闭底层 TCP 套接字
                connections.erase(it);   // 从连接池删除该条目
                return true;  // 表示已实际关闭并移除
            }
            // 2. 状态为 Wait 时：不释放 SSL、不关闭 Socket，等待其他处理
            else if (currentState == ConnectionState::Wait) {
                return false;  
            }
        }
        return false;  // 未找到该 Socket（或其他异常情况）
    }

    // 关闭所有连接
    void closeAllConnections() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = connections.begin(); it != connections.end(); ) {
            // 清理SSL资源
            if (it->second.ssl) {
                SSL_shutdown(it->second.ssl);
                SSL_free(it->second.ssl);
            }
            // 关闭Socket
            closesocket(it->first);
            it = connections.erase(it);
        }
    }

    // 检查连接是否存在（无论状态如何）
    bool hasConnection(SOCKET sock) const {
        std::lock_guard<std::mutex> lock(mtx);
        return connections.find(sock) != connections.end();
    }

    // 检查SSL对象是否在池中
    bool hasSslObject(SSL* ssl) const {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& pair : connections) {
            if (pair.second.ssl == ssl) {
                return true;
            }
        }
        return false;
    }

    // 获取当前连接数
    size_t getConnectionCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return connections.size();
    }

    // 获取指定状态的连接数
    size_t getConnectionCountByState(ConnectionState state) const {
        std::lock_guard<std::mutex> lock(mtx);
        size_t count = 0;
        for (const auto& pair : connections) {
            if (pair.second.state == state) {
                count++;
            }
        }
        return count;
    }

    bool setConnectionStateByssl(SSL* ssl, ConnectionState state) {
        if (!ssl) {  // 校验 SSL 对象有效性
            return false;
        }
        // 1. 通过 SSL 反向找到对应的 Socket（复用已有方法）
        SOCKET sock = getSocketBySsl(ssl);
        if (sock == INVALID_SOCKET) {  // 未找到该 SSL 对应的连接
            return false;
        }
        // 2. 复用 Socket 版本的 setConnectionState 逻辑（保证逻辑一致性）
        return setConnectionState(sock, state);
    }

    bool closeConnectionByssl(SSL* ssl) {
        if (!ssl) {  // 校验 SSL 对象有效性
            return false;
        }
        // 1. 加锁：避免 getSocketBySsl 和 closeConnection(SOCKET) 的锁竞争
        std::lock_guard<std::mutex> lock(mtx);
        // 2. 通过 SSL 找到对应的 Socket（直接遍历，避免二次加锁）
        SOCKET sock = INVALID_SOCKET;
        auto it = connections.begin();
        for (; it != connections.end(); ++it) {
            if (it->second.ssl == ssl) {
                sock = it->first;
                break;
            }
        }
        if (sock == INVALID_SOCKET) {  // 未找到该 SSL 对应的连接
            return false;
        }
        // 3. 复用原 closeConnection(SOCKET) 的核心逻辑（避免代码重复）
        ConnectionState currentState = it->second.state;
        if (currentState == ConnectionState::Active || currentState == ConnectionState::Closed) {
            // 清理 SSL 资源
            SSL_shutdown(ssl);
            SSL_free(ssl);
            // 关闭 Socket 并移除连接
            closesocket(sock);  // Windows 专用；Linux 替换为 close(sock)
            connections.erase(it);
            return true;
        } else if (currentState == ConnectionState::Wait) {
            return false;  // Wait 状态不关闭连接
        }
        return false;
    }

}connectionPool;


thread_local std::mt19937 thread_local_gen;

// 初始化当前线程的随机数生成器（仅首次调用时初始化）
void init_thread_local_generator() {
    // 静态局部变量确保每个线程只初始化一次
    static thread_local bool is_initialized = false;
    if (!is_initialized) {
        // 使用随机设备生成种子（每个线程的种子不同）
        std::random_device rd;
        thread_local_gen.seed(rd());
        is_initialized = true;
    }
}
// 获取当前线程的随机数生成器（自动初始化）
std::mt19937& get_thread_safe_generator() {
    init_thread_local_generator();
    return thread_local_gen;
}


// 通用状态枚举
enum class CommonStatus {
    SUCCESS_RECEIVED = 0,    // 成功接收（已存入缓冲区）
    INVALID_PARAMETER = 1,   // 参数非法
    BUFFER_ERROR = 2,        // 缓冲区操作失败
    UNKNOWN_ERROR = 3
};

// 批量提交配置
const size_t BATCH_SIZE_THRESHOLD = 100;       // 数量阈值
const std::chrono::milliseconds TIME_THRESHOLD = std::chrono::milliseconds(500);  // 时间阈值

// 线程安全的评论缓冲区
struct CommentBuffer {
    std::mutex mtx;                          // 互斥锁
    std::queue<VideoComment> buffer;         // 待提交队列
    std::chrono::system_clock::time_point last_commit_time;  // 上次提交时间
    size_t count = 0;                        // 当前计数

    // 初始化上次提交时间
    CommentBuffer() : last_commit_time(std::chrono::system_clock::now()) {}
};
static CommentBuffer g_comment_buffer;  // 全局缓冲区




class MYSQL_MS
{
private:
    sql::mysql::MySQL_Driver* driver = nullptr; // MySQL驱动实例
    sql::Connection* con = nullptr;             // 数据库连接对象
    sql::Connection* con_comdb = nullptr;
    sql::Statement* stmt = nullptr;             // SQL语句执行对象
    sql::ResultSet* res = nullptr;              // 查询结果集对象
    
    // 数据库连接配置信息（根据实际情况修改）
    const std::string url = "tcp://127.0.0.1:3306?characterEncoding=utf8mb4"; // 连接地址格式：协议://主机:端口
    const std::string user = "root";                 // 数据库用户名
    const std::string password = "123456";           // 数据库密码
    const std::string database = "mhwnet";  // 要操作的数据库名称
    const std::string database_comdb = "comdb";

public:
    MYSQL_MS()
    {
        try {
            // ==================== 1. 建立数据库连接 ====================
            // 获取MySQL驱动实例（单例模式，无需手动释放）
            driver = sql::mysql::get_mysql_driver_instance();
            if (driver == nullptr) {
                std::cerr << "Error: Failed to get MySQL driver instance!" << std::endl;
                return ;
            }
            // 创建数据库连接（重要：需要手动释放内存）
            // 参数格式：url, user, password
            con = driver->connect(url, user, password);

            // 选择具体数据库（相当于USE语句）
            con->setSchema(database);
            con->setClientOption("CHARSET", "utf8mb4");
            con->setClientOption("sslMode", "DISABLED");
            con_comdb = driver->connect(url, user, password);
            con_comdb->setSchema(database_comdb);
            con_comdb->setClientOption("sslMode", "DISABLED");
            con_comdb->setClientOption("CHARSET", "utf8mb4");
            // ==================== 2. 创建Statement对象 ====================
            // 创建用于执行SQL语句的对象（需要手动释放）
            //stmt = con->createStatement();



            //// ==================== 4. 插入数据（增操作） ====================
            //// 使用executeUpdate执行INSERT/UPDATE/DELETE语句
            //// 返回值是受影响的行数
            //stmt->executeUpdate("INSERT INTO users(name, age) VALUES ('张三', 25)");
            //std::cout << "插入数据成功！" << std::endl;

            //// ==================== 5. 删除数据（删操作） ====================
            //// 删除name为'李四'的记录
            //int deleteCount = stmt->executeUpdate("DELETE FROM users WHERE name = '李四'");
            //std::cout << "删除影响行数：" << deleteCount <<std:: endl;

            //// ==================== 6. 更新数据（改操作） ====================
            //// 将name为'张三'的age更新为26
            //int updateCount = stmt->executeUpdate("UPDATE users SET age = 26 WHERE name = '张三'");
            //std::cout << "更新影响行数：" << updateCount <<std::endl;

            //// ==================== 7. 查询数据（查操作） ====================
            //// 使用executeQuery执行SELECT语句
            //res = stmt->executeQuery("SELECT id, name, age FROM users");

            //// 遍历结果集
            //std::cout << "\n查询结果：" << std::endl;
            //while (res->next()) { // 移动到下一条记录
            //    // 通过字段名获取数据（也可以使用索引，如getInt(1)）
            //    std::cout << "ID: " << res->getInt("id")
            //        << ", 姓名: " << res->getString("name")
            //        << ", 年龄: " << res->getInt("age") << std::endl;
            //}

            //// ==================== 8. 释放资源 ====================
            //// 注意释放顺序：ResultSet -> Statement -> Connection
            //delete res;   // 释放结果集
            //delete stmt;  // 释放语句对象
            //delete con;    // 关闭数据库连接
        }
        catch (sql::SQLException& e) {
            // 异常处理：捕获所有MySQL相关的异常
            std::cout << "SQL错误 #" << e.getErrorCode() << ": " << e.what() << std::endl;
        }

    }
    // 析构时释放连接（关键）
    ~MYSQL_MS() {
        if (con != nullptr) {
            delete con; // 只在对象销毁时释放一次
            con = nullptr;
        }
        if (con_comdb != nullptr)
        {
            delete con_comdb;
            con_comdb = nullptr;
        }
    }

    // 禁用拷贝构造和赋值（避免连接被重复释放）
    MYSQL_MS(const MYSQL_MS&) = delete;
    MYSQL_MS& operator=(const MYSQL_MS&) = delete;





    void Add_videoInf(json& json_sql)
    {
        std::string sql_insert;  // 存储INSERT语句
        std::string sql_update;  // 存储UPDATE语句
        // 确保数据库连接con有效且未被其他事务占用（建议通过连接池管理）
        if (con == nullptr) {
            std::cout << "Database connection is null, unable to execute transaction" << std::endl;
            return;
        }

        try
        {
            if (json_sql["execute_type"] == "insert_video") {
                // 1. 关闭自动提交，开启事务
                con->setAutoCommit(false);  
                std::cout << "Transaction started successfully" << std::endl;

   
                sql_insert = "INSERT INTO vpconnect (videoid, userid, picid, videoaddress) VALUES (?, ?, ?, ?)";
                std::unique_ptr<sql::PreparedStatement> pstmt_insert(con->prepareStatement(sql_insert));
                // 绑定INSERT参数
                pstmt_insert->setUInt(1, json_sql["vid"].get<int>());    
                pstmt_insert->setUInt(2, json_sql["uid"].get<int>());    
                pstmt_insert->setUInt(3, 0);                             
                pstmt_insert->setString(4, json_sql["videoaddress"].get<std::string>());  
                pstmt_insert->executeUpdate();  // 执行INSERT
                std::cout << "INSERT statement executed successfully" << std::endl;

                sql_update = "UPDATE usertable SET videonum = videonum + 1 WHERE userid = ?";
                std::unique_ptr<sql::PreparedStatement> pstmt_update(con->prepareStatement(sql_update));
                pstmt_update->setUInt(1, json_sql["uid"].get<int>());
                pstmt_update->executeUpdate();  
                std::cout << "UPDATE statement executed successfully" << std::endl;

                // 无异常则提交事务（两条SQL同时生效）
                con->commit();
                std::cout << "Transaction committed successfully: Video record inserted + user's video count updated" << std::endl;
            }
        }
        catch (sql::SQLException& e) {
            if (con != nullptr) {
                con->rollback();
                std::cout << "SQL exception, transaction rolled back: " << e.getErrorCode() << ": " << e.what() << std::endl;
            }
            std::cout << "Exception-related SQL - INSERT: " << sql_insert << std::endl;
            std::cout << "Exception-related SQL - UPDATE: " << sql_update << std::endl;
        }
        catch (std::exception& e) {
            if (con != nullptr) {
                con->rollback();
                std::cout << "Non-SQL exception, transaction rolled back: " << e.what() << std::endl;
            }
        }
 
       if (con != nullptr) {
                con->setAutoCommit(true);
            }
        
    }
    void Updata_videoInf(json& json_sql)
    {
        int updateCount = 0;
        std::string sql = {};

        if (json_sql["execute_type"] == "updata_pic")
        {
            sql = "UPDATE vpconnect SET picid=? ,picaddress=? where userid =? and videoid=?";
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
            pstmt->setUInt(1, json_sql["pid"].get<int>());
            pstmt->setString(2, json_sql["picaddress"].get<std::string>());
            pstmt->setUInt(3, json_sql["uid"].get<int>());
            pstmt->setUInt(4, json_sql["vid"].get<int>());
            updateCount = pstmt->executeUpdate();
        }//
        try {
            if (json_sql["execute_type"] == "videoinf_json")
            {
                json  origin = {};
                std::string origin_videoinf = {};
                std::string origin_videoname = {};
                int origin_videostatus = {};
                sql = "SELECT videoname, videoinf,videostatus FROM vpconnect WHERE videoid=? limit 1";
                std::unique_ptr<sql::PreparedStatement> pstmt_o(con->prepareStatement(sql));
                pstmt_o->setUInt(1, json_sql["vid"].get<int>());
                std::unique_ptr<sql::ResultSet> res(pstmt_o->executeQuery());
                sql = "UPDATE vpconnect SET videoname=? ,videoinf=?,videostatus=? where  videoid=?";
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                if (res->next())
                {
                    origin_videoinf = res->getString("videoinf");
                    origin_videoname = res->getString("videoname");
                    origin_videostatus = res->getInt("videostatus");
                }
                else {
                    //notfound
                }
                if (origin_videoinf != "{}")
                {

                    origin = json::parse(origin_videoinf);//原数据
                    pstmt->setUInt(4, json_sql["vid"].get<int>());
                    if (json_sql.contains("videotitle"))
                    {
                        pstmt->setString(1, json_sql["videotitle"].get<std::string>());
                        json_sql.erase("videotitle");
                    }
                    else {
                        pstmt->setString(1, origin_videoname);
                    }
                    if (json_sql.contains("status"))
                    {
                        pstmt->setInt(3, json_sql["status"].get<int>());
                        json_sql.erase("status");
                    }
                    else
                    {
                        pstmt->setInt(3, origin_videostatus);
                    }
                    json_sql.erase("execute_type");
                    json_sql.erase("vid");
                    json_sql.erase("uid");
                    origin.update(json_sql);//json_sql有就更新js端处理

                    pstmt->setString(2, origin.dump(4));
                }
                else
                {
                    pstmt->setUInt(4, json_sql["vid"].get<int>());
                    pstmt->setString(1, json_sql["videotitle"].get<std::string>());
                    pstmt->setInt(3, json_sql["status"].get<int>());
                    json_sql.erase("execute_type");
                    json_sql.erase("vid");
                    json_sql.erase("uid");
                    json_sql.erase("status");
                    json_sql.erase("videotitle");

                    pstmt->setString(2, json_sql.dump(4));
                }
                updateCount = pstmt->executeUpdate();
            }
        }
        catch (const json::parse_error& e) {
            std::cerr << "JSON: " << e.what() << json_sql << std::endl;
        }
        catch (const sql::SQLException& e) {
            std::cerr << "SQL错误 #" << e.getErrorCode() << ": " << e.what() << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "sql updata error: " << e.what() << std::endl;
        }
        std::cout << "updata column: " << updateCount << std::endl;
        //delete con;    // 关闭数据库连接 链接复用

    }
    void Delete_Inf(json& json_sql)
    {
        int totalUpdateCount = 0;       // 记录逻辑删除的总有效条数（仅update_video时统计）
        bool isTransaction = false;     // 标记事务是否已开启
        std::string sql = {};
        int userid = 0;                 // 待更新videonum的用户ID（从JSON中获取）

        try {
            // 1. 优先获取并验证userid（逻辑删除视频时必须用到，删除用户时可选）
            if (!json_sql.contains("uid") || !json_sql["uid"].is_number_integer()) {
                throw std::invalid_argument("uid must be an integer (user ID for videonum update)");
            }
            userid = json_sql["uid"].get<int>();
            if (userid <= 0) {  // 确保userid为正整数（符合业务逻辑）
                throw std::invalid_argument("uid must be a positive integer");
            }

            // 2. 验证execute_type是否存在且为字符串类型
            if (!json_sql.contains("execute_type") || !json_sql["execute_type"].is_string()) {
                throw std::invalid_argument("execute_type must be a string");
            }
            std::string execType = json_sql["execute_type"].get<std::string>();

            // 3. 根据执行类型确定数据库字段名、逻辑删除SQL（区分是否需要更新videonum）
            std::string idField, idListKey;
            bool needUpdateVideonum = (execType == "delete_video");  // 仅逻辑删除视频时需更新videonum
            if (execType == "delete_video") {
                idField = "videoid";     // 视频ID对应的数据库字段
                idListKey = "videoid_list";  // JSON中视频ID列表的键名
                sql = "UPDATE vpconnect SET videostatus = 2 WHERE " + idField + " = ?";
            }
            else if (execType == "delete_user") {  // 执行类型改为update_user（对应逻辑删除用户）
                idField = "userid";      // 用户ID对应的数据库字段
                idListKey = "userid_list";   // JSON中用户ID列表的键名
 
                sql = "UPDATE usertable SET userstatus = 1 WHERE " + idField + " = ?";
            }
            else {
                throw std::invalid_argument("Unsupported execute_type: " + execType + " (only support delete_video/delete_user)");
            }

            // 4. 验证ID列表是否存在且为数组类型
            if (!json_sql.contains(idListKey) || !json_sql[idListKey].is_array()) {
                throw std::invalid_argument(idListKey + " must be an array");
            }
            auto& idArray = json_sql[idListKey];
            if (idArray.empty()) {
                std::cout << idListKey << " is empty, no logical deletion needed" << std::endl;
                // 即使无删除，也可检查用户是否存在（可选：避免无效userid）
                if (needUpdateVideonum) {
                    std::cout << "Note: No videos logically deleted, so user(" << userid << ") videonum remains unchanged" << std::endl;
                }
                return;
            }

            // 5. 提取并验证数组中的ID（确保都是无符号整数，避免非法值）
            std::vector<uint64_t> ids;
            for (size_t i = 0; i < idArray.size(); ++i) {
                if (!idArray[i].is_number_unsigned()) {
                    throw std::invalid_argument(idListKey + " element " + std::to_string(i + 1) + " must be a positive integer");
                }
                ids.push_back(idArray[i].get<uint64_t>());
            }

            // 6. 开启事务：确保“逻辑删除 + videonum更新”原子性（要么全成功，要么全回滚）
            con->setAutoCommit(false);
            isTransaction = true;
            std::cout << "Transaction started. Target user for videonum update: " << userid << std::endl;

            // 7. 准备参数化SQL语句（复用PreparedStatement，减少数据库连接开销）
            std::unique_ptr<sql::PreparedStatement> pstmt_update(con->prepareStatement(sql));

            // 8. 循环执行逻辑删除，统计有效条数（仅update_video时统计）
            for (size_t i = 0; i < ids.size(); ++i) {
                try {
                    uint64_t id = ids[i];
                    pstmt_update->setUInt64(1, id);  // 绑定当前ID参数

                    // 执行更新并获取受影响的行数（1=逻辑删除成功，0=记录不存在或已删除）
                    int rowAffected = pstmt_update->executeUpdate();

                    // 仅逻辑删除视频时，累计有效条数（用于后续更新videonum）
                    if (needUpdateVideonum) {
                        totalUpdateCount += rowAffected;
                    }

                    // 输出单条记录的处理结果
                    std::cout << "Processed " << idField << "=" << id << ": "
                        << (rowAffected > 0 ? "Logically deleted (status updated)" : "No record found or already deleted") << std::endl;
                }
                catch (sql::SQLException& e) {
                    // 单条记录逻辑删除失败：记录错误，继续处理下一条（不中断批量操作）
                    std::cerr << "Failed to logically delete " << idField << "=" << ids[i] << ": "
                        << "SQL Error #" << e.getErrorCode() << ": " << e.what() << std::endl;
                }
            }

            // 9. 核心：逻辑删除视频后，同步更新用户的videonum（减去总有效删除条数）
            if (needUpdateVideonum && totalUpdateCount > 0) {
                std::string sql_update = "UPDATE usertable SET videonum = videonum - ? WHERE userid = ?";
                std::unique_ptr<sql::PreparedStatement> pstmt_videonum(con->prepareStatement(sql_update));

                // 绑定参数：第一个?=总有效逻辑删除条数，第二个?=目标用户ID
                pstmt_videonum->setInt(1, totalUpdateCount);
                pstmt_videonum->setInt(2, userid);

                // 执行更新并获取受影响行数（验证用户是否存在）
                int updateAffected = pstmt_videonum->executeUpdate();
                if (updateAffected > 0) {
                    std::cout << "Successfully updated user(" << userid << ") videonum: decreased by " << totalUpdateCount << std::endl;
                }
                else {
                    // 若用户不存在，抛出异常触发回滚（避免“视频删了但videonum没更”的不一致）
                    throw std::runtime_error("Failed to update videonum: user(" + std::to_string(userid) + ") not found in usertable");
                }
            }
            else if (needUpdateVideonum && totalUpdateCount == 0) {
                std::cout << "No valid videos logically deleted, so user(" << userid << ") videonum remains unchanged" << std::endl;
            }

            // 10. 提交事务：所有操作（逻辑删除+videonum更新）统一生效
            con->commit();
            std::cout << "\nBulk logical deletion completed. "
                << (needUpdateVideonum ? ("Total logically deleted videos: " + std::to_string(totalUpdateCount) + ", videonum updated") :
                    "Total logically deleted users: " + std::to_string(totalUpdateCount))
                << std::endl;
        }
        catch (const std::invalid_argument& e) {
            // 处理参数错误（如uid缺失、execute_type无效、ID列表非法）
            std::cerr << "Invalid argument: " << e.what() << std::endl;
            if (isTransaction) con->rollback();  // 错误时回滚事务，避免脏数据
        }
        catch (sql::SQLException& e) {
            // 处理数据库级错误（如连接异常、SQL语法错误、权限不足）
            std::cerr << "Transaction SQL Error #" << e.getErrorCode() << ": " << e.what() << std::endl;
            if (isTransaction) con->rollback();  // 回滚事务
        }
        catch (const std::runtime_error& e) {
            // 处理业务逻辑错误（如用户不存在导致videonum更新失败）
            std::cerr << "Business error: " << e.what() << std::endl;
            if (isTransaction) con->rollback();  // 回滚事务
        }
        catch (const std::exception& e) {
            // 处理其他未知异常
            std::cerr << "Unexpected error: " << e.what() << std::endl;
            if (isTransaction) con->rollback();  // 回滚事务
        }

        // 11. 清理操作：无论成功/失败，恢复数据库自动提交模式
        if (con && isTransaction) {
            try {
                con->setAutoCommit(true);
                std::cout << "Database auto-commit mode restored" << std::endl;
            }
            catch (sql::SQLException& e) {
                std::cerr << "Failed to restore auto-commit: " << e.what() << std::endl;
            }
        }
    }

    void Select_video_Inf(json& json_sql, int batch_size, const std::function<void(const std::vector<Video_Data>&, SOCKET,std::string&)>& handler) {
        try {
            // 1. 提取并验证客户端socket
            if (!json_sql.contains("socket") || !json_sql["socket"].is_number_integer()) {
                throw std::invalid_argument("缺少有效socket字段");
            }
            SOCKET client = json_sql["socket"].get<int>();
            std::string  cor_origin = json_sql.value("origin", "");
            // 2. 确定查询类型和参数
            std::string sql;
            int param_value = -1;
            const std::string execute_type = json_sql["execute_type"].get<std::string>();

            if (execute_type == "select_video_uid") {
                param_value = json_sql.value("uid", -1);
                if (param_value == -1) throw std::invalid_argument("缺少有效uid字段");
                sql = "SELECT userid, videoid, picid, videoname, picaddress, videoaddress, videoinf from vpconnect where userid = ? and videostatus = 0 ";
            }
            else if (execute_type == "select_video_videoid") {
                param_value = json_sql.value("vid", -1);
                if (param_value == -1) throw std::invalid_argument("缺少有效vid字段");
                sql = "SELECT userid, videoid, picid, videoname, picaddress, videoaddress, videoinf from vpconnect where videoid = ? and videostatus = 0";
            }
            else {
                throw std::invalid_argument("未知execute_type: " + execute_type);
            }

            // 3. 执行查询
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
            pstmt->setInt(1, param_value);
            std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // 4. 处理结果集（包括无数据情况）
            std::vector<Video_Data> batch_data;
            batch_data.reserve(batch_size);
            bool has_data = false;

            while (res->next()) {
                has_data = true;
                Video_Data record;

                // 基础字段赋值
                record.uid = res->getInt("userid");
                record.vid = res->getInt("videoid");
                record.pid = res->getInt("picid");
                record.videoname = res->getString("videoname");
                record.picaddress = res->getString("picaddress");
                record.videoaddress = res->getString("videoaddress");

                // 解析JSON字段
                try {
                    std::string json_str = res->getString("videoinf");
                    record.Video_inf.update(json::parse(json_str));
                }
                catch (const json::parse_error& e) {
                    std::cerr << "JSON parse_error(vid=" << record.vid << "): " << e.what() << std::endl;
                }
                catch (const std::exception& e) {
                    std::cerr << "JSON exception error(vid=" << record.vid << "): " << e.what() << std::endl;
                }

                // 批次处理
                batch_data.push_back(record);
                if (batch_data.size() >= batch_size) {
                    handler(batch_data, client,cor_origin);
                    batch_data.clear();
                }
            }

            // 处理剩余数据或无数据情况
            if (has_data) {
                if (!batch_data.empty()) {
                    handler(batch_data, client,cor_origin);
                }
            }
            else {
                // 无数据时调用handler传递空向量，便于上层统一处理
                handler({}, client,cor_origin);
            }

        }
        catch (const sql::SQLException& e) {
            std::cerr << "SQL error Select_video_Inf (" << e.getErrorCode() << "): " << e.what() << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "error Select_video_Inf: " << e.what() << std::endl;
            throw;
        }
    }
    std::vector<Video_Data> command_guest(
        json& json_sql,
        const std::function<void(const std::vector<Video_Data>&, SOCKET,std::string&)>& handler,
        int command_size = 6,
        const std::set<int>& fetched_ids = {}
    ) {
        std::vector<Video_Data> batch_data;
        batch_data.reserve(command_size);
        SOCKET client = -1;  // 初始化无效值，避免未初始化使用
        std::string sql;
		std::string cor_origin = json_sql.value("origin", "");
        try {
            // 1. 提取并校验基础参数
            if (!json_sql.contains("socket") || !json_sql["socket"].is_number_integer()) {
                throw std::invalid_argument("Missing valid 'socket' field in JSON");
            }
            client = json_sql["socket"].get<int>();

            if (Videoid_Min > Videoid_Max || command_size <= 0) {
                throw std::invalid_argument("Invalid ID range or command_size (must be > 0)");
            }

            // 2. 提取subarea和分页参数（PageNum）
            std::string subarea = "all";
            if (json_sql.contains("subarea") && json_sql["subarea"].is_string()) {
                subarea = json_sql["subarea"].get<std::string>();
            }

            int page_num = 1;  // 默认第一页
            if (json_sql.contains("pageNum") && json_sql["pageNum"].is_string()) {
                page_num = std::stoul(json_sql["pageNum"].get<std::string>());
                if (page_num < 1) {
                    throw std::invalid_argument("pageNum must be greater than 0");
                }
            }

            std::set<int> local_fetched = fetched_ids;
            const bool need_return = !local_fetched.empty();
            const int max_attempts = command_size * 3;
            int attempts = 0;

            // 3. 根据subarea生成不同SQL
            if (subarea != "all") {
                // 3.1 subarea筛选：按subarea查询，video_click倒序，分页
                sql = "SELECT "
                    "v.userid, v.videoid, v.picid, v.videoname, "
                    "v.picaddress, v.videoaddress, v.videoinf, "
                    "u.username, u.usericon "
                    "FROM vpconnect v "
                    "LEFT JOIN usertable u ON v.userid = u.userid "
                    "WHERE JSON_EXTRACT(v.videoinf, '$.subarea') = ? "  // 提取JSON中的subarea
                    " AND  v.videostatus=0 AND v.videoid >= ? "  // 结合minid过滤
                    "ORDER BY JSON_EXTRACT(v.videoinf, '$.video_click') DESC "  // 按video_click倒序
                    "LIMIT ?, ?";  // 分页：offset, size

                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                // 设置参数：subarea值、minid、offset、command_size
                pstmt->setString(1, subarea);
                pstmt->setInt(2, Videoid_Min);
                pstmt->setInt(3, (page_num - 1) * command_size);  // 计算偏移量
                pstmt->setInt(4, command_size);

                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // 处理查询结果（无需循环多次，分页查询一次即可）
                while (res->next() && batch_data.size() < command_size) {
                    Video_Data record;
                    record.uid = res->getInt("userid");
                    record.vid = res->getInt("videoid");
                    record.pid = res->getInt("picid");
                    record.videoname = res->getString("videoname");
                    record.picaddress = res->getString("picaddress");
                    record.videoaddress = res->getString("videoaddress");
                    record.username = res->getString("username");
                    record.usericon = res->getString("usericon");

                    // 解析videoinf JSON字段
                    try {
                        std::string json_str = res->getString("videoinf");
                        if (!json_str.empty()) {
                            record.Video_inf.update(json::parse(json_str));
                        }
                    }
                    catch (const json::parse_error& e) {
                        std::cerr << "JSON parse error (videoid=" << record.vid << "): " << e.what() << std::endl;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "JSON process error (videoid=" << record.vid << "): " << e.what() << std::endl;
                    }

                    // 去重并添加记录
                    if (local_fetched.insert(record.vid).second) {
                        batch_data.push_back(std::move(record));
                    }
                }
            }
            else {
                // 3.2 subarea为all：保持原有随机查询逻辑
                auto& gen = get_thread_safe_generator();
                std::uniform_int_distribution<int> dist(Videoid_Min, Videoid_Max);

                sql = "SELECT "
                    "v.userid, v.videoid, v.picid, v.videoname, "
                    "v.picaddress, v.videoaddress, v.videoinf, "
                    "u.username, u.usericon "
                    "FROM ("
                    "    SELECT userid, videoid, picid, videoname, "
                    "           picaddress, videoaddress, videoinf "
                    "    FROM vpconnect "
                    "    WHERE   videostatus=0 and videoid >= ? LIMIT 1 "
                    ") AS v "
                    "LEFT JOIN usertable AS u ON v.userid = u.userid";

                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));

                while (batch_data.size() < command_size && attempts < max_attempts) {
                    attempts++;
                    const int random_id = dist(gen);
                    pstmt->setInt(1, random_id);
                    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                    if (res->next()) {
                        Video_Data record;
                        // 填充字段（与原有逻辑一致）
                        record.uid = res->getInt("userid");
                        record.vid = res->getInt("videoid");
                        record.pid = res->getInt("picid");
                        record.videoname = res->getString("videoname");
                        record.picaddress = res->getString("picaddress");
                        record.videoaddress = res->getString("videoaddress");
                        record.username = res->getString("username");
                        record.usericon = res->getString("usericon");

                        // 解析JSON字段
                        try {
                            std::string json_str = res->getString("videoinf");
                            if (!json_str.empty()) {
                                record.Video_inf.update(json::parse(json_str));
                            }
                        }
                        catch (const json::parse_error& e) {
                            std::cerr << "JSON parse error (videoid=" << record.vid << "): " << e.what() << std::endl;
                        }
                        catch (const std::exception& e) {
                            std::cerr << "JSON process error (videoid=" << record.vid << "): " << e.what() << std::endl;
                        }

                        // 去重并添加记录
                        if (local_fetched.insert(record.vid).second) {
                            batch_data.push_back(std::move(record));
                        }
                    }
                }
            }

            if (need_return) {
                return batch_data;
            }
            // 4. 结果处理
            if (!batch_data.empty()) {
                    handler(batch_data, client,cor_origin);
            }
            else {
                std::cerr << "No valid records found (subarea=" << subarea << ", attempts: " << attempts << ")" << std::endl;
                handler(batch_data, client, cor_origin);
            }
        }
        catch (const sql::SQLException& e) {
            std::cerr << "SQL Error (subarea): " << e.getErrorCode() << " - " << e.what()
                << ", SQL: " << sql << std::endl;  // 输出SQL便于调试
            handler({}, client,cor_origin);
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "Error (subarea): " << e.what() << std::endl;
            handler({}, client,cor_origin);
            throw;
        }

        return {};
    }
     std::vector<Video_Data> command_login(json& json_sql,
        const std::function<void(const std::vector<Video_Data>&, SOCKET, std::string&)>& handler, 
        int command_size = 6,
        const std::set<int>& fetched_ids = {}) {
        std::vector<Video_Data> batch_data;
        batch_data.reserve(command_size);  // 预分配内存
        SOCKET client = -1;                // 初始化客户端套接字
        std::string cor_origin = json_sql.value("origin", "");  // 原始请求标识
        std::set<int> fetched_vids=fetched_ids;        // 已获取的视频ID（去重）
        client = json_sql["socket"].get<SOCKET>();
        const bool need_return = !fetched_vids.empty();
        // 最终回调处理（确保无论正常/异常都执行回调）
        auto finally = [&]() {
            handler(batch_data, client, cor_origin);
            };

        try {
            // 获取并验证userid
            if (!json_sql.contains("userid") || !json_sql["userid"].is_string()) {
                throw std::invalid_argument("Missing or invalid 'userid' field (required for login recommendation)");
            }
            int userid = std::stoul(json_sql["userid"].get<std::string>());

            // 获取并验证pageNum
            if (!json_sql.contains("pageNum") || !json_sql["pageNum"].is_string()) {
                throw std::invalid_argument("Invalid 'pageNum' (must be a positive integer)");
            }
            int pagenum = std::stoul(json_sql["pageNum"].get<std::string>());
            int pos = (pagenum - 1) * command_size;  // 分页偏移量

            // 获取subarea字段（默认为"all"）
            std::string subarea = json_sql.value("subarea", "all");
            std::cout << "subarea: " << subarea << ", pagenum: " << pagenum << ", command_size: " << command_size << std::endl;

            // 1. 从comdb.comvideo表查询全部推荐视频ID（无论subarea是否为all，都先获取完整列表）
            std::vector<int> recommended_vids;
            {
                std::string com_sql = "SELECT Comvid FROM comvideo WHERE Userid = ? LIMIT 1";
                std::unique_ptr<sql::PreparedStatement> com_pstmt(con_comdb->prepareStatement(com_sql));
                com_pstmt->setInt(1, userid);
                std::unique_ptr<sql::ResultSet> com_res(com_pstmt->executeQuery());

                if (com_res->next()) {
                    std::string comvideo_str = com_res->getString("Comvid");
                    if (!comvideo_str.empty()) {
                        json comvideo_json = json::parse(comvideo_str);
                        if (comvideo_json.is_array()) {
                            for (const auto& elem : comvideo_json) {
                                if (elem.is_number_integer()) {
                                    recommended_vids.push_back(elem.get<int>());
                                }
                            }
                        }
                    }
                }
                std::cout << "Fetched " << recommended_vids.size() << " total recommended video IDs for user " << userid << std::endl;
            }

            // 2. 根据subarea处理查询逻辑
            if (subarea == "all") {
                // 2.1 subarea为all：原有逻辑（分页截取推荐视频ID并查询）
                std::vector<int> paginated_vids;
                if (pos < recommended_vids.size()) {
                    int end = (std::min)(pos + command_size, (int)recommended_vids.size());
                    paginated_vids = std::vector<int>(recommended_vids.begin() + pos, recommended_vids.begin() + end);
                }
                std::cout << "Pagination (all): pos=" << pos << ", size=" << paginated_vids.size() << std::endl;

                if (!paginated_vids.empty()) {
                    // 构建IN查询条件
                    std::string placeholders;
                    for (size_t i = 0; i < paginated_vids.size(); ++i) {
                        if (i > 0) placeholders += ", ";
                        placeholders += "?";
                    }

                    std::string vp_sql = "SELECT "
                        "v.userid, v.videoid, v.picid, v.videoname, "
                        "v.picaddress, v.videoaddress, v.videoinf, "
                        "u.username, u.usericon "
                        "FROM vpconnect AS v "
                        "LEFT JOIN usertable AS u ON v.userid = u.userid "
                        "WHERE v.videostatus = 0 AND v.videoid IN (" + placeholders + ") "
                        "ORDER BY FIELD(v.videoid, " + placeholders + ")";  // 保持推荐顺序

                    std::unique_ptr<sql::PreparedStatement> vp_pstmt(con->prepareStatement(vp_sql));
                    // 绑定参数（IN条件和ORDER BY FIELD）
                    for (size_t i = 0; i < paginated_vids.size(); ++i) {
                        vp_pstmt->setInt(i + 1, paginated_vids[i]);
                        vp_pstmt->setInt(i + 1 + paginated_vids.size(), paginated_vids[i]);
                    }

                    std::unique_ptr<sql::ResultSet> vp_res(vp_pstmt->executeQuery());
                    while (vp_res->next()) {
                        Video_Data video;
                        video.uid = vp_res->getInt("userid");
                        video.vid = vp_res->getInt("videoid");
                        video.pid = vp_res->getInt("picid");
                        video.videoname = vp_res->getString("videoname");
                        video.picaddress = vp_res->getString("picaddress");
                        video.videoaddress = vp_res->getString("videoaddress");
                        video.usericon = vp_res->getString("usericon");
                        video.username = vp_res->getString("username");

                        // 解析videoinf字段
                        std::string videoinf_str = vp_res->getString("videoinf");
                        if (!videoinf_str.empty()) {
                            try {
                                json videoinf_json = json::parse(videoinf_str);
                                video.Video_inf.update(videoinf_json);
                            }
                            catch (const json::parse_error& e) {
                                std::cerr << "Parse videoinf failed (vid=" << video.vid << "): " << e.what() << std::endl;
                            }
                        }

                        // 去重添加
                        if (fetched_vids.insert(video.vid).second) {
                            batch_data.push_back(video);
                        }
                    }
                }
            }
            else {
                // 2.2 subarea不为all：按subarea筛选并分页，结果按"是否在推荐列表"排序
                // 2.2.1 按subarea查询符合条件的视频（分页）
                std::vector<Video_Data> subarea_videos;
                {
                    // 参考提供的subarea分页查询SQL
                    std::string vp_sql = "SELECT "
                        "v.userid, v.videoid, v.picid, v.videoname, "
                        "v.picaddress, v.videoaddress, v.videoinf, "
                        "u.username, u.usericon "
                        "FROM vpconnect v "
                        "LEFT JOIN usertable u ON v.userid = u.userid "
                        "WHERE JSON_EXTRACT(v.videoinf, '$.subarea') = ? "  // 筛选subarea
                        "AND v.videostatus = 0 "
                        "AND v.videoid >= 0 "  // 若有minid可替换，这里默认不限制
                        "ORDER BY JSON_EXTRACT(v.videoinf, '$.video_click') DESC "  // 按点击量倒序
                        "LIMIT ?, ?";  // 分页：offset, size

                    std::unique_ptr<sql::PreparedStatement> vp_pstmt(con->prepareStatement(vp_sql));
                    // 绑定参数：subarea, offset, size
                    vp_pstmt->setString(1, subarea);
                    vp_pstmt->setInt(2, pos);         // 偏移量
                    vp_pstmt->setInt(3, command_size); // 每页条数

                    std::unique_ptr<sql::ResultSet> vp_res(vp_pstmt->executeQuery());
                    while (vp_res->next()) {
                        Video_Data video;
                        video.uid = vp_res->getInt("userid");
                        video.vid = vp_res->getInt("videoid");
                        video.pid = vp_res->getInt("picid");
                        video.videoname = vp_res->getString("videoname");
                        video.picaddress = vp_res->getString("picaddress");
                        video.videoaddress = vp_res->getString("videoaddress");
                        video.usericon = vp_res->getString("usericon");
                        video.username = vp_res->getString("username");

                        // 解析videoinf字段
                        std::string videoinf_str = vp_res->getString("videoinf");
                        if (!videoinf_str.empty()) {
                            try {
                                json videoinf_json = json::parse(videoinf_str);
                                video.Video_inf.update(videoinf_json);
                            }
                            catch (const json::parse_error& e) {
                                std::cerr << "Parse videoinf failed (vid=" << video.vid << "): " << e.what() << std::endl;
                            }
                        }

                        subarea_videos.push_back(video);
                        fetched_vids.insert(video.vid); // 记录已获取的vid，用于去重
                    }
                    std::cout << "Fetched " << subarea_videos.size() << " videos for subarea: " << subarea << std::endl;
                }

                // 2.2.2 对subarea查询结果排序：推荐视频（在recommended_vids中）排在前面
                // 转换推荐列表为set，提高查询效率
                std::unordered_set<int> recommended_set(recommended_vids.begin(), recommended_vids.end());
                // 排序：在推荐列表中的视频优先，其余按原顺序（video_click倒序）
                std::sort(subarea_videos.begin(), subarea_videos.end(), [&](const Video_Data& a, const Video_Data& b) {
                    bool a_in_recommended = recommended_set.count(a.vid) > 0;
                    bool b_in_recommended = recommended_set.count(b.vid) > 0;
                    if (a_in_recommended && !b_in_recommended) return true;  // a优先
                    if (!a_in_recommended && b_in_recommended) return false; // b优先
                    return false; // 都在或都不在推荐列表，保持原查询的排序（video_click倒序）
                    });

                // 2.2.3 合并到结果集
                batch_data = std::move(subarea_videos);
            }

            // 3. 计算缺失数量，调用随机推荐补充（仅subarea为all时可能需要）
            if (subarea == "all")
            {
                int missing = command_size - batch_data.size();
                if (missing > 0) {
                    std::cout << "Need " << missing << " more videos, fetching random..." << std::endl;
                    fetched_vids.insert(0);  // 避免空集合导致SQL异常
                    std::vector<Video_Data> random_videos = command_guest(json_sql, handler, missing, fetched_vids);
                    // 合并随机视频
                    batch_data.insert(batch_data.end(),
                        std::make_move_iterator(random_videos.begin()),
                        std::make_move_iterator(random_videos.end()));
                }
            }
            // 4. 确保结果不超过请求数量
            if (batch_data.size() > command_size) {
                batch_data.resize(command_size);
            }
            if (need_return)//其他调用
                return batch_data;
            std::cout << "Login recommendation completed: " << batch_data.size() << "/" << command_size << " videos" << std::endl;
            finally();  // 正常回调
            return {};

        }
        catch (const sql::SQLException& e) {
            std::cerr << "SQL Error (login recommendation): " << e.getErrorCode() << " - " << e.what() << std::endl;
            finally();  // 异常回调
            return {};
            throw;
        }
        catch (const json::parse_error& e) {
            std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            finally();
            return {};
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "Error (login recommendation): " << e.what() << std::endl;
            finally();
            return {};
            throw;
        }
        
    }


    void command_related_video(json& json_sql, const std::function<void(const std::vector<Video_Data>&, SOCKET,std::string&)>& handler, int command_size = 6) {
        // 初始化必要变量，确保异常场景下也能正常回调
        SOCKET client = -1;
        std::vector<Video_Data> related_data; // 存储按uid查询的关联视频
        std::set<int> fetched_ids;            // 全局去重：存储已获取的videoid
        related_data.reserve(command_size);   // 预分配内存，提升性能
		std::string cor_origin = json_sql.value("origin", "");
        try {
            client = json_sql["socket"].get<int>();

            if (!json_sql.contains("authorid") || !json_sql["authorid"].is_number_integer()) {
                throw std::invalid_argument("Missing valid 'authorid' field in JSON (user ID is required for related video query)");
            }
            int target_uid = json_sql["authorid"].get<int>();
            int pos = (json_sql["pagenum"].get<int>() - 1) * command_size;
            if (!json_sql.contains("uid") || !json_sql["uid"].is_number_integer())
            {
                throw std::invalid_argument("Missing valid 'authorid' field in JSON (user ID is required for related video query)");

            }
            int uid = json_sql.value("uid", -1);
            bool is_guest = {};
            if (uid <= 0)
                is_guest = true;
            else {
                is_guest = false;
                json_sql.erase("uid");
                json_sql["userid"] = std::to_string(uid);
                json_sql["pageNum"] = std::to_string(json_sql["pagenum"].get<int>());
                json_sql.erase("pagenum");
            }
            // 2. 校验查询数量有效性
            if (command_size <= 0) {
                throw std::invalid_argument("Invalid query count (command_size must be greater than 0)");
            }

            // 3. 优先按uid查询关联视频（查询该用户发布的所有视频）
            std::string sql = "SELECT "
                "v.userid,              "
                "v.videoid,             "
                "v.picid,               "
                "v.videoname,           "
                "v.picaddress,          "
                "v.videoaddress,        "
                "v.videoinf,            "
                "u.username,            "
                "u.usericon             "
                "FROM                   "
                "(                      "
                "    SELECT userid, videoid, picid, videoname, picaddress, videoaddress, videoinf"
                "    FROM vpconnect "
                "    WHERE  videostatus=0 and userid = ? "
                ") AS v  "
                "LEFT JOIN "
                "usertable AS u ON v.userid = u.userid "
                "ORDER BY "
                "v.videoid DESC "
                "LIMIT ? , ? ; ";
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
            pstmt->setInt(1, target_uid);
            pstmt->setInt(2, pos);
            pstmt->setInt(3, command_size);
            std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // 4. 处理uid关联的查询结果（去重并填充related_data）
            while (res->next() && related_data.size() < command_size) {
                Video_Data record;
                record.uid = res->getInt("userid");
                record.vid = res->getInt("videoid");
                record.pid = res->getInt("picid");
                record.videoname = res->getString("videoname");
                record.picaddress = res->getString("picaddress");
                record.videoaddress = res->getString("videoaddress");
                record.usericon = res->getString("usericon");
                record.username = res->getString("username");

                // 解析videoinf JSON字段（复用command_guest逻辑）
                std::string json_str = res->getString("videoinf");
                try {
                    json parsed_json = json::parse(json_str);
                    record.Video_inf.update(parsed_json);
                }
                catch (const json::parse_error& e) {
                    // JSON解析失败：输出错误日志（含具体videoid便于定位）
                    std::cerr << "Failed to parse JSON (videoid=" << record.vid << "): " << e.what() << std::endl;
                }
                catch (const std::exception& e) {
                    // JSON处理异常：输出错误日志（含具体videoid便于定位）
                    std::cerr << "Error occurred while processing JSON (vid=" << record.vid << "): " << e.what() << std::endl;
                }


                // 去重：仅添加未出现过的视频（避免同uid下重复视频）
                if (fetched_ids.find(record.vid) == fetched_ids.end()) {
                    fetched_ids.insert(record.vid);
                    related_data.push_back(record);
                }
            }

            // 5. 检查关联视频数量是否满足需求：不足则调用command_guest补充
            int missing_count = command_size - related_data.size();
            if (missing_count > 0) {
                // 输出补充查询日志：说明当前关联视频数量及需补充的随机视频数量
                std::cerr << "Queried(related video query) " << related_data.size() << " videos by uid, need to supplement " << missing_count << " fill videos" << std::endl;

                // 调用command_guest获取补充视频：传入当前已获取的fetched_ids（确保全局去重）
                fetched_ids.insert(0);//确保fetched_ids不为空
                std::vector<Video_Data> fill_data = {};
                if(is_guest)
                   fill_data =std::move(command_guest(json_sql, handler, missing_count, fetched_ids));
                else
                     fill_data = std::move(command_login(json_sql, handler, missing_count, fetched_ids));

                // 合并随机视频到关联视频列表（保持related_data的完整性）
                related_data.insert(related_data.end(),
                    std::make_move_iterator(fill_data.begin()),
                    std::make_move_iterator(fill_data.end()));
            }

            // 6. 回调处理结果（无论是否满额，均返回已获取的视频）
            handler(related_data, client,cor_origin);
            // 输出查询完成日志：说明最终返回数量与目标数量，便于调试
            std::cout << "Related video query completed: Returned " << related_data.size() << " videos (target: " << command_size << ")" << std::endl;

        }
        catch (const sql::SQLException& e) {
            // 数据库异常：回调空列表+输出详细错误日志（含错误码便于定位）
            std::cerr << "SQL Error (related video query): " << e.getErrorCode() << " - " << e.what() << std::endl;
            handler({}, client,cor_origin);
            throw;
        }
        catch (const std::invalid_argument& e) {
            // 参数错误：回调空列表+输出错误日志（明确参数问题类型）
            std::cerr << "Parameter Error (related video query): " << e.what() << std::endl;
            handler({}, client, cor_origin);
            throw;
        }
        catch (const std::exception& e) {
            // 其他异常：回调空列表+输出错误日志（覆盖所有标准异常场景）
            std::cerr << "Processing Error (related video query): " << e.what() << std::endl;
            handler({}, client, cor_origin);
            throw;
        }
    }


    void search_video(json& json_sql, const std::function<void(const std::vector<Video_Data>&, SOCKET, std::string&)>& handler, int command_size = 6) {
        SOCKET client = {};
        std::vector<Video_Data> search_result = {};
        std::set<int> fetched_ids;
        search_result.reserve(command_size);
        std::string cor_origin = json_sql.value("origin", "");

        try {
            client = json_sql["socket"].get<int>();

            // 1. 验证并获取搜索关键词
            if (!json_sql.contains("search") || !json_sql["search"].is_string()) {
                throw std::invalid_argument("Missing valid 'search' field (must be a non-empty string)");
            }
            std::string search_term = json_sql["search"].get<std::string>();

            // 2. 验证并获取页码
            if (!json_sql.contains("searchPage") || !json_sql["searchPage"].is_string()) {
                throw std::invalid_argument("Missing valid 'searchPage' field (must be a positive integer string)");
            }
            int search_page = std::stoul(json_sql["searchPage"].get<std::string>());
            if (search_page < 1) {
                throw std::invalid_argument("Invalid 'searchPage' (must be greater than 0)");
            }

            // 3. 验证查询数量
            if (command_size <= 0) {
                throw std::invalid_argument("Invalid 'command_size' (must be greater than 0)");
            }

            // 4. 获取并解析uid（默认为-1，<0时走原有逻辑）
            int uid = -1;
            if (json_sql.contains("uid") && json_sql["uid"].is_string()) {
                try {
                    uid = std::stoi(json_sql["uid"].get<std::string>());
                }
                catch (const std::exception& e) {
                    std::cerr << "Warning: Invalid uid format, using default (-1): " << e.what() << std::endl;
                    uid = -1;
                }
            }
            std::cout << "Search video: uid=" << uid << ", keyword='" << search_term << "', page=" << search_page << std::endl;

            // 5. 计算分页偏移量
            int pos = (search_page - 1) * command_size;

            // 6. 执行搜索查询，获取符合条件的视频
            std::string sql = "SELECT "
                "v.userid, v.videoid, v.picid, v.videoname, "
                "v.picaddress, v.videoaddress, v.videoinf, "
                "u.username, u.usericon "
                "FROM vpconnect v "
                "LEFT JOIN usertable u ON v.userid = u.userid "
                "WHERE v.videostatus = 0 AND (v.videoname LIKE ? or u.username LIKE ?)"
                "ORDER BY v.videoid DESC "
                "LIMIT ?, ?;";

            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
            pstmt->setString(1, "%" + search_term + "%");  // 模糊匹配关键词
            pstmt->setString(2, "%" + search_term + "%");
            pstmt->setInt(3, pos);                          // 偏移量
            pstmt->setInt(4, command_size);                 // 每页数量

            std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            std::vector<Video_Data> raw_results;  // 临时存储原始搜索结果
            while (res->next()) {
                Video_Data record;
                record.uid = res->getInt("userid");
                record.vid = res->getInt("videoid");
                record.pid = res->getInt("picid");
                record.videoname = res->getString("videoname");
                record.picaddress = res->getString("picaddress");
                record.videoaddress = res->getString("videoaddress");
                record.username = res->getString("username");
                record.usericon = res->getString("usericon");

                // 解析videoinf字段
                std::string json_str = res->getString("videoinf");
                try {
                    if (!json_str.empty()) {
                        json parsed_json = json::parse(json_str);
                        record.Video_inf.update(parsed_json);
                    }
                }
                catch (const json::parse_error& e) {
                    std::cerr << "Failed to parse videoinf (vid=" << record.vid << "): " << e.what() << std::endl;
                }
                catch (const std::exception& e) {
                    std::cerr << "Error processing videoinf (vid=" << record.vid << "): " << e.what() << std::endl;
                }

                if (fetched_ids.insert(record.vid).second) {  // 去重
                    raw_results.push_back(std::move(record));
                }
            }

            // 7. 处理排序：uid>0时，推荐视频优先
            if (uid > 0) {
                // 7.1 查询该用户的推荐视频ID列表（参考command_login逻辑）
                std::vector<int> recommended_vids;
                {
                    std::string com_sql = "SELECT Comvid FROM comvideo WHERE Userid = ? LIMIT 1";
                    std::unique_ptr<sql::PreparedStatement> com_pstmt(con_comdb->prepareStatement(com_sql));
                    com_pstmt->setInt(1, uid);
                    std::unique_ptr<sql::ResultSet> com_res(com_pstmt->executeQuery());

                    if (com_res->next()) {
                        std::string comvideo_str = com_res->getString("Comvid");
                        if (!comvideo_str.empty()) {
                            json comvideo_json = json::parse(comvideo_str);
                            if (comvideo_json.is_array()) {
                                for (const auto& elem : comvideo_json) {
                                    if (elem.is_number_integer()) {
                                        recommended_vids.push_back(elem.get<int>());
                                    }
                                }
                            }
                        }
                    }
                    std::cout << "Fetched " << recommended_vids.size() << " recommended video IDs for uid=" << uid << std::endl;
                }

                // 7.2 转换为unordered_set，提高查找效率
                std::unordered_set<int> recommended_set(recommended_vids.begin(), recommended_vids.end());

                // 7.3 排序：推荐视频（在recommended_set中）排在前面，其余保持原顺序
                std::sort(raw_results.begin(), raw_results.end(), [&](const Video_Data& a, const Video_Data& b) {
                    bool a_in_recommended = recommended_set.count(a.vid) > 0;
                    bool b_in_recommended = recommended_set.count(b.vid) > 0;

                    if (a_in_recommended && !b_in_recommended) return true;   // a优先
                    if (!a_in_recommended && b_in_recommended) return false;  // b优先
                    return false;  // 都在或都不在推荐列表，保持原查询的videoid倒序
                    });
            }

            // 8. 最终结果赋值（经过排序/去重的结果）
            search_result = std::move(raw_results);

            // 9. 回调结果
            handler(search_result, client, cor_origin);
            std::cout << "Search completed: keyword='" << search_term << "', page=" << search_page
                << ", returned=" << search_result.size() << ", target=" << command_size << std::endl;

        }
        catch (const sql::SQLException& e) {
            std::cerr << "SQL Error (search video, keyword='" << json_sql.value("search", "unknown") << "'): "
                << e.getErrorCode() << " - " << e.what() << std::endl;
            handler({}, client, cor_origin);
            throw;
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Parameter Error (search video): " << e.what() << std::endl;
            handler({}, client, cor_origin);
            throw;
        }
        catch (const std::exception& e) {
            std::cerr << "Processing Error (search video): " << e.what() << std::endl;
            handler({}, client, cor_origin);
            throw;
        }
    }

  void get_video_comments(json& json_sql, const std::function<void(const std::vector<VideoComment>&, SOCKET, std::string&)>& handler) {
      // 1. 核心变量初始化（与add_video_comment逻辑对齐）
      SOCKET client = -1;                          
      std::vector<VideoComment> resp_comments;     
      std::string origin = json_sql.value("origin", "");  
      int resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);  

      // 分页与查询核心参数
      int32_t video_id = -1;                       
      int32_t page_num = 1;                       
      const int32_t PAGE_SIZE = 3;                
      int32_t offset = 0;                         

      auto finally = [&]() {
          handler(resp_comments, client, origin);
          };

      try {
		  client = static_cast<SOCKET>(json_sql["socket"].get<int>());//url解码的数据为字符串数据 videoId和pageNum

          if (!json_sql.contains("videoId") || !json_sql["videoId"].is_string()) {
              std::cerr << "get_video_comments: invalid or missing 'videoId' parameter (must be string)" << std::endl;
              finally();
              return;
          }
          video_id = std::stoul(json_sql["videoId"].get<std::string>());

          // 2.3 处理pageNum（可选，默认第1页，容错非法值）
          if (json_sql.contains("pageNum") && json_sql["pageNum"].is_string()) {
              page_num = std::stoul(json_sql["pageNum"].get<std::string>());
              page_num = (page_num < 1) ? 1 : page_num;  // 防止页码为0或负数
          }
          // 计算分页偏移量（核心：LIMIT分页的起始位置）
          offset = (page_num - 1) * PAGE_SIZE;
          std::cout << "get_video_comments: threadID=" << std::this_thread::get_id()
              << ", videoId=" << video_id << ", pageNum=" << page_num
              << ", offset=" << offset << ", pageSize=" << PAGE_SIZE << std::endl;

          // 3. 数据库查询：关联video_comments和usertable，分页获取评论
          std::unique_ptr<sql::PreparedStatement> pstmt;  // 预编译SQL语句（防SQL注入）
          std::unique_ptr<sql::ResultSet> res;            // 存储查询结果集

          try {
              // 3.1 构建SQL：LEFT JOIN关联用户表，按创建时间倒序（最新评论在前）
              const std::string query_sql = R"(
                SELECT 
                    vc.id, vc.video_id, vc.user_id, vc.parent_id, vc.content,
                    vc.like_count, vc.reply_count, vc.status,
                    vc.created_at, vc.updated_at,
                    ut.username, ut.usericon  
                FROM 
                    video_comments vc
                LEFT JOIN 
                    usertable ut ON vc.user_id = ut.userid  
                WHERE 
                    vc.video_id = ?  
                    AND vc.status = ?  -- 只返回已审核的评论（kApproved对应枚举值，假设为1）
                ORDER BY 
                    vc.created_at DESC  -- 按创建时间倒序（最新评论优先）
                LIMIT ? OFFSET ?;     
            )";

              // 3.2 预编译SQL并绑定参数（避免SQL注入，提升性能）
              pstmt = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(query_sql));
              pstmt->setInt(1, video_id);  // 参数1：视频ID
              pstmt->setInt(2, static_cast<int8_t>(VideoCommentStatus::kApproved));  // 参数2：审核状态
              pstmt->setInt(3, PAGE_SIZE);  // 参数3：每页条数
              pstmt->setInt(4, offset);     // 参数4：分页偏移量

              // 3.3 执行查询并获取结果集
              res = std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());
              std::cout << "get_video_comments: query executed, fetching comments..." << std::endl;

              // 3.4 解析结果集到VideoComment结构体（含用户信息）
              while (res->next()) {
                  VideoComment comment;
                  // 3.4.1 填充video_comments表字段
                  comment.id = res->getInt("id");
                  comment.video_id = res->getInt("video_id");
                  comment.user_id = res->getInt("user_id");
                  comment.parent_id = res->getInt("parent_id");
                  comment.content = res->getString("content");
                  comment.like_count = res->getInt("like_count");
                  comment.reply_count = res->getInt("reply_count");
                  // 状态枚举转换（数据库存储的int -> VideoCommentStatus）
                  comment.status = static_cast<VideoCommentStatus>(res->getInt("status"));
                  // 时间字段（数据库datetime -> string）
                  comment.created_at = res->getString("created_at");
                  comment.updated_at = res->getString("updated_at");

                  // 3.4.2 填充usertable关联字段（处理NULL：用户不存在时设为空字符串）
                  comment.username = res->isNull("username") ? "" : res->getString("username");
                  comment.usericon = res->isNull("usericon") ? "" : res->getString("usericon");

                  // 添加到响应列表
                  resp_comments.push_back(comment);
              }

              // 3.5 打印查询结果统计
              std::cout << "get_video_comments: fetched " << resp_comments.size()
                  << " comments for videoId=" << video_id << ", pageNum=" << page_num << std::endl;
              resp_code = static_cast<int>(CommonStatus::SUCCESS_RECEIVED);  // 查询成功

          }
          catch (const sql::SQLException& e) {
              // 数据库异常处理（SQL错误、连接问题等）
              std::cerr << "get_video_comments SQL error: " << e.what()
                  << " (MySQL code: " << e.getErrorCode()
                  << ", SQLState: " << e.getSQLState() << ")" << std::endl;
              throw std::runtime_error("database query failed");  // 抛异常触发外层catch
          }

          // 4. 调用回调响应客户端（查询成功，返回评论列表）
          finally();

      }
      catch (const std::exception& e) {
          // 业务异常处理（参数错误、数据库异常等）
          std::cerr << "get_video_comments exception: threadID=" << std::this_thread::get_id()
              << ", error: " << e.what() << std::endl;
          resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
          finally();  // 即使异常，也需回调响应客户端
      }
      catch (...) {
          // 未知异常处理（防止程序崩溃）
          std::cerr << "get_video_comments unknown exception: threadID=" << std::this_thread::get_id() << std::endl;
          resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
          finally();
      }
  }


  void add_video_comment(json& json_sql, const std::function<void(const std::vector<VideoComment>&, SOCKET, std::string&)>& handler) {
      
      int client = json_sql["socket"].get<int>();
      std::vector<VideoComment> resp_comments = {};  // 响应数据
      int resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
      std::string origin = json_sql.value("origin", "");
      // 回调函数（确保始终响应客户端）
      auto finally = [&]() {
          handler(resp_comments, client,origin);
          };

      try {
          // 参数校验
          if (!json_sql.contains("video_id") || !json_sql["video_id"].is_string() ||
              !json_sql.contains("user_id") || !json_sql["user_id"].is_string() ||
              !json_sql.contains("content") || !json_sql["content"].is_string()) 
          {
              resp_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
              finally();
              return;
          }

          // 提取参数
       
          VideoComment current_comment;
          current_comment.video_id = std::stoul(json_sql["video_id"].get<std::string>());
          current_comment.user_id = std::stoul(json_sql["user_id"].get<std::string>());
          current_comment.content = json_sql["content"].get<std::string>();
          current_comment.parent_id = json_sql.value("parent_id", 0);
          current_comment.status = VideoCommentStatus::kApproved;
          current_comment.like_count = 0;
          current_comment.reply_count = 0;

          // 存入缓冲区（线程安全）
          {
              std::unique_lock<std::mutex> lock(g_comment_buffer.mtx);
              if (g_comment_buffer.count > 10000) {
                  throw std::runtime_error("comment buffer overflow（10000),reject");
              }
              g_comment_buffer.buffer.push(current_comment);
              g_comment_buffer.count++;
              std::cout << "thread（ID:" << std::this_thread::get_id() << "）：SOCKET=" << client
                  << " comment buffer count =" << g_comment_buffer.count << std::endl;
              lock.unlock();
              resp_comments.push_back(current_comment);
              resp_code = static_cast<int>(CommonStatus::SUCCESS_RECEIVED);
             
          }

          // 立即响应客户端
          finally();

          // 检查是否需要批量提交
          bool need_commit = false;
          std::vector<VideoComment> batch_comments = {};

      {
              std::unique_lock<std::mutex> lock(g_comment_buffer.mtx);
              auto current_time = std::chrono::system_clock::now();
      
      
          if (g_comment_buffer.count >= BATCH_SIZE_THRESHOLD ||
              std::chrono::duration_cast<std::chrono::milliseconds>(current_time - g_comment_buffer.last_commit_time) >= TIME_THRESHOLD) {
              need_commit = true;
              batch_comments.reserve(g_comment_buffer.count);
              while (!g_comment_buffer.buffer.empty()) {
                  batch_comments.push_back(std::move(g_comment_buffer.buffer.front()));
                  g_comment_buffer.buffer.pop();
              }
              g_comment_buffer.count = 0;
              g_comment_buffer.last_commit_time = current_time;
          }
      }

          // 执行批量插入
      if (need_commit && !batch_comments.empty()) {
          std::unique_ptr<sql::PreparedStatement> pstmt_insert;  // Prepared statement for inserting comments
          std::unique_ptr<sql::PreparedStatement> pstmt_update;  // Prepared statement for updating parent comments
          try {
              con->setAutoCommit(false);  // Start transaction to ensure atomicity

            
              std::vector<int32_t> parent_ids;  // Store parent comment 
              for (const auto& comment : batch_comments) {
                  if (comment.parent_id != 0) {
                      parent_ids.push_back(comment.parent_id);
                  }
              }

              // 2. Batch insert comments
              const std::string insert_sql = R"(
            INSERT INTO video_comments 
            (video_id, user_id, parent_id, content, like_count, reply_count, status, created_at, updated_at)
            VALUES (?, ?, ?, ?, 0, 0, ?, NOW(), NOW())
        )";
              pstmt_insert = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(insert_sql));

              int affected_rows_insert = 0;  // Rows affected by insert operations
              for (const auto& comment : batch_comments) {
                  pstmt_insert->setInt(1, comment.video_id);
                  pstmt_insert->setInt(2, comment.user_id);
                  pstmt_insert->setInt(3, comment.parent_id);
                  pstmt_insert->setString(4, comment.content);
                  pstmt_insert->setInt(5, static_cast<int8_t>(comment.status));
                  affected_rows_insert += pstmt_insert->executeUpdate();
              }

              // 3. Batch update parent comments' reply_count (only if there are parent comments to update)
              int affected_rows_update = 0;  // Rows affected by update operations
              if (!parent_ids.empty()) {
                  const std::string update_sql = R"(
                UPDATE video_comments 
                SET reply_count = reply_count + 1 
                WHERE id = ?
            )";
                  pstmt_update = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(update_sql));

                  for (int32_t parent_id : parent_ids) {
                      pstmt_update->setInt(1, parent_id);
                      affected_rows_update += pstmt_update->executeUpdate();
                  }
              }

              // 4. Check if all operations succeeded
              // Success condition: inserted rows = total comments AND updated rows = total parent comments (each parent exists)
              bool all_success = (affected_rows_insert == static_cast<int>(batch_comments.size())) &&
                  (affected_rows_update == static_cast<int>(parent_ids.size()));

              if (all_success) {
                  con->commit();  // Commit transaction if all succeeded
                  std::cout << "Post comment batch committed successfully! ThreadID:" << std::this_thread::get_id()
                      << ", inserted comments count:" << affected_rows_insert
                      << ", updated parent comments count:" << affected_rows_update << std::endl;
              }
              else {
                  con->rollback();  // Rollback if partial failure
                  std::cerr << "Post comment batch commit failed!"
                      << " Expected inserts:" << batch_comments.size() << ", actual inserts:" << affected_rows_insert
                      << "; Expected updates:" << parent_ids.size() << ", actual updates:" << affected_rows_update << std::endl;
              }

          }
          catch (const sql::SQLException& e) {
              con->rollback();
              std::cerr << "SQL post comment error：" << e.what()
                  << " (MySQL error code:" << e.getErrorCode()
                  << ", SQLState:" << e.getSQLState() << ")" << std::endl;
          }
          catch (const std::exception& e) {
              con->rollback();
              std::cerr << "Post comment batch error：" << e.what() << std::endl;
          }

          con->setAutoCommit(true); 
      }

    }
    catch (const std::exception& e) {
        resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
        std::cerr << "threadID:" << std::this_thread::get_id() << "postcomment unknown error:" << e.what() << std::endl;
        finally();
    }
    catch (...) {
        resp_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
        std::cerr << "threadID:" << std::this_thread::get_id() << "postcomment unknown error" << std::endl;
        finally();
    }
}





  void inti_id()
    {
      try {
          // 查找最大的videoid（倒序取第一条）
          std::unique_ptr<sql::PreparedStatement> pstmt_Maxvid(
              con->prepareStatement("SELECT videoid FROM vpconnect ORDER BY videoid DESC LIMIT 1")
          );
          std::unique_ptr<sql::ResultSet> rs_vid(pstmt_Maxvid->executeQuery());
          if (rs_vid->next()) {
              Videoid_Max = rs_vid->getInt("videoid")+1;
          }
          std::unique_ptr<sql::PreparedStatement> pstmt_Minvid(
              con->prepareStatement("SELECT videoid FROM vpconnect ORDER BY videoid LIMIT 1")
          );
          std::unique_ptr<sql::ResultSet> rs_minvid(pstmt_Minvid->executeQuery());
          if (rs_minvid->next()) {
              Videoid_Min = rs_minvid->getInt("videoid");
          }

          // 查找最大的userid
          std::unique_ptr<sql::PreparedStatement> pstmt_uid(
              con->prepareStatement("SELECT userid FROM usertable ORDER BY userid DESC LIMIT 1")
          );
          std::unique_ptr<sql::ResultSet> rs_uid(pstmt_uid->executeQuery());
          if (rs_uid->next()) {
              Uid_Max = rs_uid->getInt("userid")+1;
          }

          // 查找最大的picid
          std::unique_ptr<sql::PreparedStatement> pstmt_pid(
              con->prepareStatement("SELECT picid FROM vpconnect ORDER BY picid DESC LIMIT 1")
          );
          std::unique_ptr<sql::ResultSet> rs_pid(pstmt_pid->executeQuery());
          if (rs_pid->next()) {
              Pid_Max = rs_pid->getInt("picid")+1;
          }
      }
      catch (const sql::SQLException& e) {
          throw std::runtime_error("MySQL error: " + std::string(e.what()) +
              " (Error code: " + std::to_string(e.getErrorCode()) + ")");
      }
      catch (...) {
          throw std::runtime_error("Unknown error occurred while initializing IDs");
      }


    }

    private:
      
};




class json_execute
{
public:
    json_execute()
    {

    };
    json_execute(std::string uid, std::string vid, std::string v_title, std::string pid)
    {
        VPconnect["userid"] = uid;
        VPconnect["videoid"] = vid;
        VPconnect["videotitle"] = v_title;
        VPconnect["picture"] = pid;

    };
    void json_adduserid(std::string uid)
    {
        VPconnect["userid"] = uid;
    }
    void json_addvid(std::string  vid)
    {
        VPconnect["videoid"] = vid;

    }
    void json_addvt(std::string v_title)
    {
        VPconnect["videotitle"] = v_title;
    }
    void json_addpid(std::string pid)
    {
        VPconnect["pictureid"] = pid;

    }
    void json_addvaddress(std::string v_address)
    {
        VPconnect["videoaddress"] = v_address;
    }
    void json_addpaddress(std::string p_address)
    {
        VPconnect["piaddress"] = p_address;
    }
public:
    std::string json_getvid()
    {
        return VPconnect["videoid"];
    }
    std::string json_getpid()
    {
        return VPconnect["pictureid"];
    }

    std::string json_getvaddress()
    {
        return VPconnect["videoaddress"];
    }
    std::string json_getpaddress()
    {
        return VPconnect["picaddress"];
    }
    std::string json_getuid()
    {
        return VPconnect["userid"];
    }
private:
    json VPconnect
    {
        {"userid"," "},
        {"videoid"," " },
        {"videotitle"," "},
        {"pictureid"," "},
        {"videoaddress"," "},
        {"picaddress"," "}
       

    };
    json Video_inf
    {
        {"self_done"," "},
        {"subarea"," "},
        {"tag", {" "," "," "," "," "}},
        {"video_click"," "},
        {"video_like"," "},
        {"introduce" ," "},
        {"launch_time"," "},
        {"video_type" ," "}//short or video
        
    };

};
//class VPconnect
//{
//public:
//      char* video_filename;
//	  char* picture_filename;
//	  wchar_t* title;
//private:
//		const long int video_id;
//		const long int picture_id;
//
//		const wchar_t* auther_inf;//mysql
//	
//};
#endif