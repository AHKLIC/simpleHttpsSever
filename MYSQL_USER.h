#ifndef  MYSQL_USER_H
#define MYSQL_USER_H

#include<iostream>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/exception.h>

#include <jdbc/cppconn/prepared_statement.h>
#include "jdbc/mysql_driver.h"
#include<vector>
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include<chrono>
#include<unordered_map>
#include<algorithm>


namespace sql_user {

struct User_Data
{


    int uid = 0;
    int follownum = 0;
    std::string username = {};
    std::string usericon = {};
    std::string u_password = {};
    std::string phone = {};
    std::string introduce = {};
    std::string token = {};
	int videonum = 0;
    int useratatus = 0;
    json User_inf
    {
        {"subscribe_uid",{"0","0"}},
        {"follow_uid",{"0","0"}},
         { "like_videoid" ,{"0","0"} }
    };
  

};


enum class LoginStatus {
    SUCCESS,         // 登录成功
    INVALID_USER,    // 用户名不存在
    WRONG_PASSWORD,  // 密码错误
    DB_ERROR,        // 数据库错误
    PARAM_ERROR      // 参数错误
};
enum class CommonStatus {
    SUCCESS = 0,                // 关注/取消关注成功
    SQL_ERROR = 1,              // 数据库SQL异常（如语法错误、连接问题）
    NO_ROWS_UPDATED = 2,        // 无行被更新（如用户ID不存在、已关注/已取消）
    UNKNOWN_ERROR = 3,          // 未知异常
    INVALID_EXECUTE_TYPE = 4  ,// 无效的execute_type（非sub_add/sub_minus）
     INVALID_PARAMETER =5,
     DATA_INCONSISTENCY =6  //数据不同步
}; 
    using json = nlohmann::json;

    class MYSQL_MS
    {
    private:
        sql::mysql::MySQL_Driver* driver = nullptr; // MySQL驱动实例
        sql::Connection* con = nullptr;             // 数据库连接对象
        sql::Statement* stmt = nullptr;             // SQL语句执行对象
        sql::ResultSet* res = nullptr;              // 查询结果集对象

        // 数据库连接配置信息（根据实际情况修改）
        const std::string url = "tcp://127.0.0.1:3306?characterEncoding=utf8mb4"; // 连接地址格式：协议://主机:端口
        const std::string user = "root";                 // 数据库用户名
        const std::string password = "123456";           // 数据库密码
        const std::string database = "mhwnet";           // 要操作的数据库名称
    private:
        std::unordered_map<std::string, int> SorceTypehashMap = {};
    public:
        MYSQL_MS()
        {
            try {
                // ==================== 1. 建立数据库连接 ====================
                // 获取MySQL驱动实例（单例模式，无需手动释放）
                driver = sql::mysql::get_mysql_driver_instance();

                // 创建数据库连接（重要：需要手动释放内存）
                // 参数格式：url, user, password
                con = driver->connect(url, user, password);

                // 选择具体数据库（相当于USE语句）
                con->setSchema(database);
                con->setClientOption("sslMode", "DISABLED");
                con->setClientOption("CHARSET", "utf8mb4");
                SorceTypehashMap.emplace("saw_video",10);
                SorceTypehashMap.emplace("thumbs_down", -250);
                SorceTypehashMap.emplace("thumbs_up", 250);
                SorceTypehashMap.emplace("unlike_undo", 300);
                SorceTypehashMap.emplace("unlike_do", -300);
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
        }

        // 禁用拷贝构造和赋值（避免连接被重复释放）
        MYSQL_MS(const MYSQL_MS&) = delete;
        MYSQL_MS& operator=(const MYSQL_MS&) = delete;


        //void Add_userInf(json& json_sql)
        //{
        //    std::string sql;
        //    try
        //    {
        //        if (json_sql["execute_type"] == "insert_user") {
        //            sql = "INSERT INTO user (userid, username, usericon,password,phone) VALUES (?, ?, ?, ?, ?)";
        //            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        //            // 绑定参数（防止SQL注入）
        //            pstmt->setUInt(1, json_sql["uid"].get<int>());
        //            pstmt->setString(2, json_sql["username"].get<std::string>());
        //            pstmt->setString(3, json_sql["usericon"].get<std::string>());
        //            pstmt->setString(4, json_sql["password"].get<std::string>());
        //            pstmt->setString(5, json_sql["phone"].get<std::string>());
        //            pstmt->executeUpdate();
        //        }



        //    }
        //    catch (sql::SQLException& e) {
        //        // 异常处理：捕获所有MySQL相关的异常
        //        std::cout << "SQL error add" << e.getErrorCode() << ": " << e.what() << std::endl;
        //    }
        //    std::cout << "insert user success" << std::endl;
        //}

        void AddorUpdate_sv(json& Jsql)
        {
            std::string sql= {};
            int score = 0;
            int updateCount = 0;
            bool isnull =true;
            try {
                sql = "select score from user_saw_video where videoid =? and userid = ? ";
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                pstmt->setInt(1, Jsql["vid"].get<int>());
				pstmt->setInt(2, Jsql["uid"].get<int>());
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                if (res->next())
                {
                    score = res->getInt("score");
                    isnull = false;
                }

                if (!isnull)//update cheer click?
                {
                    sql = "update user_saw_video set score = ?, update_time = now() where videoid = ? and userid =?";
                    std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                    auto it = SorceTypehashMap.find(Jsql["execute_type"].get<std::string>());
                    if(it != SorceTypehashMap.end())
                    score += it->second;
                    pstmt->setInt(1, score);
                    pstmt->setInt(2, Jsql["vid"].get<int>());
					pstmt->setInt(3, Jsql["uid"].get<int>());
                   updateCount= pstmt->executeUpdate();
                }
                else
                {
                    sql = "insert into user_saw_video (userid, videoid, score, update_time) values (?, ?, ?, now())";
                    std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                    pstmt->setInt(1, Jsql["uid"].get<int>());
                    pstmt->setInt(2, Jsql["vid"].get<int>());
                    pstmt->setInt(3, 60); // 初始分数
                   updateCount= pstmt->executeUpdate();
                }
            }
            catch (const sql::SQLException& e) {
                std::cerr << "SQL AddorUpdate_sv error #" << e.getErrorCode() << ": " << e.what() << std::endl;
            }
            catch (const std::exception& e) {
                std::cerr << "exception AddorUpdate_sv error: " << e.what() << std::endl;
            }
            std::cout << "updata column: " << updateCount << std::endl;

        }

        void Update_videoclick(json& Jsql)
        {
               std::string sql = {};
               sql = "select videoid from vpconnect where videoid = ?";
               std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
               pstmt->setInt(1, Jsql["vid"].get<int>());
               std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
               if (res->next())
               {
                     
                   sql = "UPDATE vpconnect "
                       "SET videoinf = JSON_SET(videoinf, '$.video_click', "
                       "CAST(JSON_EXTRACT(videoinf, '$.video_click') AS UNSIGNED) + 1) "
                       "WHERE videoid = ?";

                   try {
                       // 准备预处理语句
                       std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                                       // userId
                       pstmt->setInt(1, Jsql["vid"].get<int>());    // videoId

                       // 执行更新
                       int affectedRows = pstmt->executeUpdate();
                       if (affectedRows > 0) {
                           std::cout << "video_click update sucess" << std::endl;
                       }
                       else {
                           std::cout << "not found rowname" << std::endl;
                       }
                   }
                   catch (sql::SQLException& e) {
                       std::cerr << "videoclick update faild: " << e.what() << std::endl;
                   }
               }
               else
               {

                   std::cout << "not found date" << std::endl;
               }
        }

        void Update_usersub(json& Jsql, const std::function<void(SOCKET,int&, std::string&)>& handle) {
            // 初始化结果状态码（默认未知错误，后续根据场景更新）
            int result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
            SOCKET socket = Jsql["socket"].get<int>();
			std::string origin = Jsql.value("origin","");
            // 确保最终无论成功/失败，都调用handle返回结果（避免漏回调）
            auto finally = [&]() {
                handle(socket,result_code,origin); // 传递结果状态码给回调
                };

            // 初始化数据库事务（默认自动提交关闭）
            con->setAutoCommit(false);
            std::unique_ptr<sql::PreparedStatement> pstmt1;
            std::unique_ptr<sql::PreparedStatement> pstmt2;

            try {
                // 2. 校验必要参数：execute_type、uid、authorid
                if (!Jsql.contains("execute_type") || !Jsql.contains("uid") || !Jsql.contains("authorid")) {
                    result_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
                    finally(); // 无必要参数，
                    return;
                }

                std::string execute_type = Jsql["execute_type"].get<std::string>();
                int uid = Jsql["uid"].get<int>();       // 避免频繁std::stoi，直接解析为int
                int authorid = Jsql["authorid"].get<int>();

                // 3. 校验execute_type合法性
                if (execute_type != "sub_add" && execute_type != "sub_minus") {
                    result_code = static_cast<int>(CommonStatus::INVALID_EXECUTE_TYPE);
                    finally();
                    return;
                }

                std::string sql;
                int rows_affected1 = 0; 
                int rows_affected2 = 0;

                if (execute_type == "sub_minus") { // 取消关注逻辑
                    // 3.1 移除当前用户的"关注列表"（userinf->subscribe_uid）
                    sql = "UPDATE usertable "
                        "SET userinf = JSON_REMOVE(userinf, JSON_UNQUOTE(JSON_SEARCH(userinf, 'one', ?, NULL, '$.subscribe_uid[*]'))) "
                        "WHERE userid = ? AND JSON_CONTAINS(userinf->'$.subscribe_uid', JSON_QUOTE(?))";
                    pstmt1 = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql));
                    pstmt1->setString(1, std::to_string(authorid)); // JSON_SEARCH匹配authorid
                    pstmt1->setInt(2, uid);                         // 目标用户ID
                    pstmt1->setString(3, std::to_string(authorid)); // 校验是否包含该authorid
                    rows_affected1 = pstmt1->executeUpdate();

                    // 3.2 移除被取消关注用户的"粉丝列表"（userinf->follow_uid）+ 粉丝数-1
                    sql = "UPDATE usertable "
                        "SET follownum = follownum - 1, "
                        "userinf = JSON_REMOVE(userinf, JSON_UNQUOTE(JSON_SEARCH(userinf, 'one', ?, NULL, '$.follow_uid[*]'))) "
                        "WHERE userid = ? AND JSON_CONTAINS(userinf->'$.follow_uid', JSON_QUOTE(?))";
                    pstmt2 = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql));
                    pstmt2->setString(1, std::to_string(uid));       // JSON_SEARCH匹配uid（粉丝ID）
                    pstmt2->setInt(2, authorid);                     // 被关注用户ID
                    pstmt2->setString(3, std::to_string(uid));       // 校验是否包含该粉丝ID
                    rows_affected2 = pstmt2->executeUpdate();

                }
                else { // execute_type == "sub_add"（新增关注逻辑）
                    // 3.3 新增当前用户的"关注列表"（userinf->subscribe_uid）
                    sql = "UPDATE usertable "
                        "SET userinf = JSON_ARRAY_APPEND(userinf, '$.subscribe_uid', ?) "
                        "WHERE userid = ? AND NOT JSON_CONTAINS(userinf->'$.subscribe_uid', JSON_QUOTE(?))";
                    pstmt1 = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql));
                    pstmt1->setString(1, std::to_string(authorid));
                    pstmt1->setInt(2, uid);
                    pstmt1->setString(3, std::to_string(authorid));
                    rows_affected1 = pstmt1->executeUpdate();

                    // 3.4 新增被关注用户的"粉丝列表"（userinf->follow_uid）+ 粉丝数+1
                    sql = "UPDATE usertable "
                        "SET follownum = follownum + 1, "
                        "userinf = JSON_ARRAY_APPEND(userinf, '$.follow_uid', ?) "
                        "WHERE userid = ? AND NOT JSON_CONTAINS(userinf->'$.follow_uid', JSON_QUOTE(?))";
                    pstmt2 = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql));
                    pstmt2->setString(1, std::to_string(uid));
                    pstmt2->setInt(2, authorid);
                    pstmt2->setString(3, std::to_string(uid));
                    rows_affected2 = pstmt2->executeUpdate();
                }

                // 4. 校验影响行数：两个UPDATE必须都成功（都影响1行）才视为整体成功
                if (rows_affected1 == 1 && rows_affected2 == 1) {
                    con->commit(); // 事务提交成功
                    result_code = static_cast<int>(CommonStatus::SUCCESS);
                }
                else {
                    // 无行更新（如用户ID不存在、已关注/已取消），回滚事务
                    con->rollback();
                    result_code = static_cast<int>(CommonStatus::NO_ROWS_UPDATED);
                }

            }
            catch (const sql::SQLException& e) { // 数据库SQL异常
                con->rollback();
                std::cerr << "SQL Error: " << e.what()
                    << " (MySQL Code: " << e.getErrorCode()
                    << ", SQLState: " << e.getSQLState() << ")" << std::endl;
                result_code = static_cast<int>(CommonStatus::SQL_ERROR);

            }
            catch (const std::exception& e) { // 其他标准异常（如JSON解析失败）
                con->rollback();
                std::cerr << "Standard Error: " << e.what() << std::endl;
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);

            }
            catch (...) { // 未知异常
                con->rollback();
                std::cerr << "Unknown Error in Update_usersub" << std::endl;
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);

            }
           
                // 5. 恢复数据库自动提交模式（无论成功/失败都执行）
                con->setAutoCommit(true);
                // 调用回调返回结果（finally是lambda，确保必执行）
                finally();
        }

        void Update_userthumbs(json& Jsql, const std::function<void(SOCKET, int&, std::string&)>& handle) {
            // 1. 初始化结果状态码（默认未知错误）
            int result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
            SOCKET socket = socket = Jsql["socket"].get<int>(); 
			std::string origin = Jsql.value("origin", "");
            // 确保最终无论成功/失败，都调用handle返回结果（漏回调防护）
            auto finally = [&]() {
                handle(socket, result_code,origin);
                };

            // 2. 初始化数据库事务（关闭自动提交，保证"用户点赞+视频点赞数"操作原子性）
            con->setAutoCommit(false);
            // 新增：需要两个PreparedStatement（分别操作usertable和vpconnect表）
            std::unique_ptr<sql::PreparedStatement> pstmt_user;  // 操作用户表（like_videoid）
            std::unique_ptr<sql::PreparedStatement> pstmt_video; // 操作vpconnect表（video_like）

            try {
                // 3. 校验必要参数：socket、execute_type、uid、vid（缺一不可）
                if (
                    !Jsql.contains("execute_type") || !Jsql.contains("uid") || !Jsql["uid"].is_number_unsigned() ||
                    !Jsql.contains("vid") || !Jsql["vid"].is_number_unsigned()) {
                    result_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
                    finally();
                    return;
                }
               
                std::string execute_type = Jsql["execute_type"].get<std::string>();
                unsigned int uid = Jsql["uid"].get<unsigned int>();
                unsigned int vid = Jsql["vid"].get<unsigned int>();
                std::string vid_str = std::to_string(vid); // 适配JSON函数的字符串参数

                // 4. 校验execute_type合法性（仅支持点赞/取消点赞）
                if (execute_type != "thumbs_up" && execute_type != "thumbs_down") {
                    result_code = static_cast<int>(CommonStatus::INVALID_EXECUTE_TYPE);
                    finally();
                    return;
                }

                // 定义影响行数变量（两个表的操作需分别记录）
                int rows_affected_user = 0;  // usertable表影响行数
                int rows_affected_video = 0; // vpconnect表影响行数


                /*********************************************************************
                 * 第一步：操作usertable表，维护用户like_videoid数组（原有逻辑保留，适配双表事务）
                 ********************************************************************/
                if (execute_type == "thumbs_up") { // 点赞：向用户like_videoid数组添加vid（防重复）
                    std::string sql_user = "UPDATE usertable "
                        "SET userinf = JSON_ARRAY_APPEND(userinf, '$.like_videoid', ?) "
                        "WHERE userid = ? "
                        "AND NOT JSON_CONTAINS(userinf->'$.like_videoid', JSON_QUOTE(?))";
                    pstmt_user = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql_user));
                    pstmt_user->setString(1, vid_str);
                    pstmt_user->setUInt(2, uid);
                    pstmt_user->setString(3, vid_str);
                    rows_affected_user = pstmt_user->executeUpdate();

                }
                else { // 取消点赞：从用户like_videoid数组移除vid（防无元素可删）
                    std::string sql_user = "UPDATE usertable "
                        "SET userinf = JSON_REMOVE(userinf, JSON_UNQUOTE(JSON_SEARCH(userinf, 'one', ?, NULL, '$.like_videoid[*]'))) "
                        "WHERE userid = ? "
                        "AND JSON_CONTAINS(userinf->'$.like_videoid', JSON_QUOTE(?))";
                    pstmt_user = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql_user));
                    pstmt_user->setString(1, vid_str);
                    pstmt_user->setUInt(2, uid);
                    pstmt_user->setString(3, vid_str);
                    rows_affected_user = pstmt_user->executeUpdate();
                }


                /*********************************************************************
                 * 第二步：新增操作vpconnect表，维护视频videoinf的video_like字段（±1）
                 ********************************************************************/
                std::string sql_video;
                if (execute_type == "thumbs_up") { // 点赞：视频点赞数+1（需确保视频存在）
                    sql_video = "UPDATE vpconnect "
                        "SET videoinf = JSON_SET(videoinf, '$.video_like'," 
                        "CAST(JSON_EXTRACT(videoinf, '$.video_like') AS UNSIGNED) + 1) "
                        "WHERE videoid = ? " ; 
                }
                else { // 取消点赞：视频点赞数-1（防负数，需确保点赞数≥1）
                    sql_video = "UPDATE vpconnect "
                        "SET videoinf = JSON_SET(videoinf, '$.video_like'," 
                        "CAST(JSON_EXTRACT(videoinf, '$.video_like') AS UNSIGNED) - 1) "
                        "WHERE videoid = ? "
                        "AND JSON_EXTRACT(videoinf, '$.video_like') > 0"; // 避免点赞数变为负数
                }
                // 执行视频点赞数更新
                pstmt_video = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql_video));
                pstmt_video->setUInt(1, vid); // 按vid匹配视频
                rows_affected_video = pstmt_video->executeUpdate();


                /*********************************************************************
                 * 第三步：双表操作结果校验（需两个表都成功影响1行，才视为整体成功）
                 ********************************************************************/
                if (rows_affected_user == 1 && rows_affected_video == 1) {
                    AddorUpdate_sv(Jsql);
                    con->commit();
                    result_code = static_cast<int>(CommonStatus::SUCCESS);
                    std::cout << "Update_userthumbs success: uid=" << uid << ", vid=" << vid << ", type=" << execute_type << std::endl;

                }
                else if (rows_affected_user == 0 && rows_affected_video == 0) {
                    // 双表都无行更新（如：用户已点赞/已取消，且视频点赞数无需变更）
                    con->rollback();
                    result_code = static_cast<int>(CommonStatus::NO_ROWS_UPDATED);
                    std::cout << "Update_userthumbs no rows: uid=" << uid << ", vid=" << vid << " (already processed)" << std::endl;

                }
                else {
                    // 单表成功、单表失败（数据不一致风险，必须回滚）
                    con->rollback();
                    result_code = static_cast<int>(CommonStatus::DATA_INCONSISTENCY); // 新增：数据不一致状态码
                    std::cerr << "Update_userthumbs inconsistency: user_rows=" << rows_affected_user
                        << ", video_rows=" << rows_affected_video << " (uid=" << uid << ", vid=" << vid << ")" << std::endl;
                }

            }
            catch (const sql::SQLException& e) { // 数据库异常（SQL错误、连接问题等）
                con->rollback();
                std::cerr << "SQL Error in Update_userthumbs: " << e.what()
                    << " (MySQL Code: " << e.getErrorCode()
                    << ", SQLState: " << e.getSQLState() << ")" << std::endl;
                result_code = static_cast<int>(CommonStatus::SQL_ERROR);

            }
            catch (const std::exception& e) { // 标准异常（JSON解析、参数转换等）
                con->rollback();
                std::cerr << "Standard Error in Update_userthumbs: " << e.what() << std::endl;
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);

            }
            catch (...) { // 未知异常（兜底处理）
                con->rollback();
                std::cerr << "Unknown Error in Update_userthumbs" << std::endl;
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);

            }

            // 恢复数据库自动提交模式（无论结果如何，确保还原初始状态）
            con->setAutoCommit(true);
            // 调用回调返回结果
            finally();
        }
        
        

        void Select_User_VneedInf(json& Jsql, const std::function<void(const std::vector<User_Data>&, SOCKET,std::string&)>& handler)
        {
            try {
                // 1. 提取客户端socket
                if (!Jsql.contains("socket") || !Jsql["socket"].is_number_integer()) {
                    throw std::invalid_argument("JSON中缺少有效的socket字段");
                }
                SOCKET client = Jsql["socket"].get<int>();
				std::string origin = Jsql.value("origin", "");
                // 2. 根据execute_type确定查询类型和参数
                std::string sql;
                int param_value = -1;

                if (!Jsql.contains("uid") || !Jsql["uid"].is_number_integer()) {
                    throw std::invalid_argument("JSON中缺少有效的uid字段");
                }
                   param_value = Jsql["uid"].get<int>();
                   std::string json_str = {};

                   {
                       sql = "select userinf from usertable where userid =?";
                       std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                       pstmt->setInt(1, param_value);
                       std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
                       while (res->next())
                       {
                           json_str = res->getString("userinf");

                       }
                   }
                    if (!Jsql.contains("authorid") || !Jsql["authorid"].is_number_integer()) {
                        throw std::invalid_argument("JSON中缺少有效的authorid字段");
                    }
                    param_value = Jsql["authorid"].get<int>();
                    sql = "select userid,follownum,usericon,username,introduce,videonum from usertable where userid =?";

                // 3. 准备SQL语句并绑定参数
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
                pstmt->setInt(1, param_value); // 绑定参数到SQL语句的?

                // 4. 执行查询，获取结果集
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                std::vector< User_Data> batch_data;
                batch_data.reserve(1); // 预分配内存提升性能

                while (res->next()) { // 遍历每一行数据
                     User_Data record;
                    record.uid = res->getInt("userid");
                    record.follownum = res->getInt("follownum");
                    record.usericon = res->getString("usericon");
                    record.username = res->getString("username");
					record.introduce = res->getString("introduce");
					record.videonum = res->getInt("videonum");
                    record.User_inf = json_str.empty() ? json::object() : [&json_str]() -> json {  
                        // 右侧：Lambda立即执行，返回json（数组/值）                         
                            json parsed = json::parse(json_str);
                            if (parsed.contains("subscribe_uid") && parsed["subscribe_uid"].is_array()) {
                                return parsed;
                            }
                            else {
                                return json::object();  // 空
                            }
                        }();
                 

                    // 添加到当前批次
                    batch_data.push_back(record);

                }

                // 处理最后一批数据
                if (!batch_data.empty()) {
                    handler(batch_data, client,origin);
                }
            }
            catch (const sql::SQLException& e) {
                std::cerr << "SQL error：" << e.getErrorCode() << " - " << e.what() << std::endl;
                throw; // 可以选择向上抛出以便上层处理
            }
            catch (const std::invalid_argument& e) {
                std::cerr << "invalid_argument error：" << e.what() << std::endl;
                throw;
            }
            catch (const std::exception& e) {
                std::cerr << "error：" << e.what() << std::endl;
                throw;
            }
        }

        void Update_userIcon(json&Jsql)
        {
			int updateCount = 0;
            try {
             
                // 2. 根据execute_type确定查询类型和参数
                std::string sql;
                int param_value = -1;

                if (!Jsql.contains("uid") || !Jsql["uid"].is_number_integer()) {
                    throw std::invalid_argument("JSON中缺少有效的uid字段");
                }
				std::string usericon = Jsql["usericon"].get<std::string>();
                param_value = Jsql["uid"].get<int>();
                std::string json_str = {};

                {
                    sql = "update usertable set usericon=? where userid =?";
                    std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
					pstmt->setString(1, usericon);
                    pstmt->setInt(2, param_value);
					 updateCount = pstmt->executeUpdate();
                }
				std::cout << "Update_userIcon update column: " << updateCount << std::endl;

             
            }
            catch (const sql::SQLException& e) {
                std::cerr << "SQL error：" << e.getErrorCode() << " - " << e.what() << std::endl;
                throw; // 可以选择向上抛出以便上层处理
            }
            catch (const std::invalid_argument& e) {
                std::cerr << "invalid_argument error：" << e.what() << std::endl;
                throw;
            }
            catch (const std::exception& e) {
                std::cerr << "error：" << e.what() << std::endl;
                throw;
            }


        }

        void Update_userProfile(json& Jsql, const std::function<void(SOCKET, int&, std::string&)>& handler) {
            // 1. 初始化核心变量：客户端socket、跨域信息、事务状态、默认错误码
            SOCKET client_socket = static_cast<SOCKET>(Jsql["socket"].get<int>());
            std::string cor_origin = Jsql.value("origin", "");
            bool is_transaction = false;
            int result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);

            // 最终回调：确保无论成功/失败，都返回状态码和上下文
            auto finally = [&]() {
                handler(client_socket, result_code, cor_origin);
                };

            try {
                // 2. 必传参数校验：userId 合法性（对应 INVALID_PARAMETER 状态）
                if (!Jsql.contains("uid")) {
                    result_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
                    throw std::invalid_argument("Missing required parameter: userId");
                }
                if (!Jsql["uid"].is_string()) {
                    result_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
                    throw std::invalid_argument("Invalid userId type: must be string");
                }
                int userId = std::stoi(Jsql["uid"].get<std::string>());
                if (userId <= 0) {
                    result_code = static_cast<int>(CommonStatus::INVALID_PARAMETER);
                    throw std::invalid_argument("Invalid userId value: must be positive integer");
                }

                // 3. 查询用户当前数据（校验用户存在性，对应 NO_ROWS_UPDATED 状态）
                std::string sql_query = "SELECT introduce, username, phone FROM usertable WHERE userid = ?";
                std::unique_ptr<sql::PreparedStatement> pstmt_query;
                std::unique_ptr<sql::ResultSet> rs;
                try {
                    pstmt_query = std::unique_ptr<sql::PreparedStatement>(con->prepareStatement(sql_query));
                    pstmt_query->setInt(1, userId);
                    rs = std::unique_ptr<sql::ResultSet>(pstmt_query->executeQuery());
                }
                catch (sql::SQLException& e) {
                    result_code = static_cast<int>(CommonStatus::SQL_ERROR);
                    throw; 
                }

                // 若用户不存在，设置 NO_ROWS_UPDATED 状态
                if (!rs->next()) {
                    result_code = static_cast<int>(CommonStatus::NO_ROWS_UPDATED);
                    throw std::runtime_error("User not found: userId = " + std::to_string(userId));
                }

                // 读取当前值（用于后续对比是否需要更新）
                std::string old_introduce = rs->getString("introduce");
                std::string old_username = rs->getString("username");
                std::string old_phone = rs->getString("phone");

                // 4. 提取JSON字段：非空则用新值，为空则保留旧值（无状态码，仅逻辑处理）
                std::string new_introduce = (Jsql.contains("introduce") && Jsql["introduce"].is_string() && !Jsql["introduce"].get<std::string>().empty())
                    ? Jsql["introduce"].get<std::string>()
                    : old_introduce;

                std::string new_username = (Jsql.contains("userName") && Jsql["userName"].is_string() && !Jsql["userName"].get<std::string>().empty())
                    ? Jsql["userName"].get<std::string>()
                    : old_username;

                std::string new_phone = (Jsql.contains("phone") && Jsql["phone"].is_string() && !Jsql["phone"].get<std::string>().empty())
                    ? Jsql["phone"].get<std::string>()
                    : old_phone;

                // 5. 动态判断是否需要更新（无字段更新则设置 NO_ROWS_UPDATED）
                std::vector<std::string> update_fields;
                std::vector<std::string> param_values;
                if (new_introduce != old_introduce) {
                    update_fields.push_back("introduce = ?");
                    param_values.push_back(new_introduce);
                }
                if (new_username != old_username) {
                    update_fields.push_back("username = ?");
                    param_values.push_back(new_username);
                }
                if (new_phone != old_phone) {
                    update_fields.push_back("phone = ?");
                    param_values.push_back(new_phone);
                }

                // 若无字段需更新，直接设置状态并返回（无需开启事务）
                if (update_fields.empty()) {
                    result_code = static_cast<int>(CommonStatus::NO_ROWS_UPDATED);
                    std::cout << "[Update User Profile] No fields to update: userId=" << userId
                        << " (new values are empty or same as old)" << std::endl;
                    finally(); // 提前触发回调，避免后续无效流程
                    return;
                }

                // 6. 开启事务 + 执行更新（核心业务逻辑，对应 SQL_ERROR/SUCCESS 状态）
                try {
                    con->setAutoCommit(false);
                    is_transaction = true;

                    // 拼接动态SQL
                    std::string sql_update = "UPDATE usertable SET "
                        + std::accumulate(update_fields.begin(), update_fields.end(), std::string(),
                            [](const std::string& a, const std::string& b) {
                                return a.empty() ? b : a + ", " + b;
                            })
                        + " WHERE userid = ?";

                    // 绑定参数并执行更新
                    std::unique_ptr<sql::PreparedStatement> pstmt_update(con->prepareStatement(sql_update));
                    for (size_t i = 0; i < param_values.size(); ++i) {
                        pstmt_update->setString(i + 1, param_values[i]);
                    }
                    pstmt_update->setInt(param_values.size() + 1, userId);

                    int update_count = pstmt_update->executeUpdate();
                    con->commit(); // 事务提交（无异常则确认更新）

                    // 校验更新结果：若更新行数为0，设置 NO_ROWS_UPDATED；否则设置 SUCCESS
                    if (update_count <= 0) {
                        result_code = static_cast<int>(CommonStatus::NO_ROWS_UPDATED);
                        std::cout << "[Update User Profile] No rows updated: userId=" << userId
                            << " (update count=" << update_count << ")" << std::endl;
                    }
                    else {
                        result_code = static_cast<int>(CommonStatus::SUCCESS);
                        std::cout << "[Update User Profile] Success: userId=" << userId
                            << ", updated fields=" << update_fields.size()
                            << ", affected rows=" << update_count << std::endl;
                    }

                }
                catch (sql::SQLException& e) {
                    // 更新阶段SQL异常，设置 SQL_ERROR 状态并回滚
                    result_code = static_cast<int>(CommonStatus::SQL_ERROR);
                    if (con) con->rollback();
                    throw; // 抛出异常进入SQL异常处理分支
                }

                // 7. （可选）校验更新后数据一致性（对应 DATA_INCONSISTENCY 状态）
                try {
                    std::unique_ptr<sql::ResultSet> rs_updated(pstmt_query->executeQuery());
                    if (!rs_updated->next()) {
                        result_code = static_cast<int>(CommonStatus::DATA_INCONSISTENCY);
                        std::cerr << "[Update User Profile] Data inconsistency: userId=" << userId
                            << " (updated but not found)" << std::endl;
                    }
                }
                catch (sql::SQLException& e) {
                    result_code = static_cast<int>(CommonStatus::SQL_ERROR);
                    std::cerr << "[Update User Profile] Check updated data failed: SQL Error=" << e.getErrorCode()
                        << ", Message=" << e.what() << std::endl;
                }

            }
            // 8. 异常分支：根据异常类型确认状态码（已在前面分支预设置，此处仅补充日志）
            catch (const std::invalid_argument& e) {
                // 已在参数校验阶段设置 INVALID_PARAMETER
                std::cerr << "[Update User Profile] Invalid Parameter: " << e.what() << std::endl;
            }
            catch (const std::runtime_error& e) {
                // 已在用户不存在阶段设置 NO_ROWS_UPDATED
                std::cerr << "[Update User Profile] Runtime Error: " << e.what() << std::endl;
            }
            catch (sql::SQLException& e) {
                // 已在SQL操作阶段设置 SQL_ERROR（若未设置则兜底）
                if (result_code != static_cast<int>(CommonStatus::SQL_ERROR)) {
                    result_code = static_cast<int>(CommonStatus::SQL_ERROR);
                }
                std::cerr << "[Update User Profile] SQL Error: Code=" << e.getErrorCode()
                    << ", Message=" << e.what() << std::endl;
            }
            catch (const std::exception& e) {
                // 未知异常：兜底设置 UNKNOWN_ERROR
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
                std::cerr << "[Update User Profile] Unknown Error: " << e.what() << std::endl;
            }
            catch (...) {
                // 捕获所有非std::exception异常（如自定义异常），兜底设置 UNKNOWN_ERROR
                result_code = static_cast<int>(CommonStatus::UNKNOWN_ERROR);
                std::cerr << "[Update User Profile] Fatal Unknown Error: non-standard exception" << std::endl;
            }

            // 9. 清理资源：恢复自动提交模式（无论成功/失败）
            if (is_transaction && con) {
                try {
                    con->setAutoCommit(true);
                }
                catch (sql::SQLException& e) {
                    std::cerr << "[Update User Profile] Restore Auto-Commit Failed: " << e.what() << std::endl;
                    // 恢复失败不影响状态码（核心业务已处理），仅日志提示
                }
            }

            // 10. 触发最终回调：返回状态码和上下文
            finally();
        }




        void check_login(json& Jsql,const std::function<void(SOCKET, int,User_Data, std::string&)>& handle)
        {
            // 1. 初始化返回数据和状态
            User_Data user_data;
            SOCKET client_socket = static_cast<SOCKET>(Jsql["socket"].get<int>());
            int status = static_cast<int>(LoginStatus::PARAM_ERROR);  // 默认参数错误
			std::string origin = Jsql.value("origin", "");
            try {
                // 2. 验证输入参数是否存在
                if (!Jsql.contains("username") || !Jsql.contains("password")) {
                    handle(client_socket, status, user_data,origin);
                    return;
                }

                // 3. 提取用户名和密码
                std::string username = Jsql["username"].get<std::string>();
                std::string password = Jsql["password"].get<std::string>();

                // 4. 执行登录查询
                std::string query_sql = "SELECT userid, username,usericon FROM usertable WHERE username = ? AND u_password = ?";
                std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(query_sql));
                pstmt->setString(1, username);  // 绑定用户名参数
                pstmt->setString(2, password);  // 绑定密码参数
                std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                // 5. 处理查询结果
                if (res->next()) {  // 登录成功：用户名和密码匹配
                    // 5.1 从结果集提取用户信息
                    user_data.uid = res->getInt("userid");
                    user_data.username = res->getString("username");
					user_data.usericon = res->getString("usericon");
                    // 5.2 用MySQL生成UUID并更新到usertable的token字段
                    std::string update_sql = "UPDATE usertable SET token = UUID() WHERE userid = ?";
                    std::unique_ptr<sql::PreparedStatement> update_stmt(con->prepareStatement(update_sql));
                    update_stmt->setInt(1, user_data.uid);
                    update_stmt->executeUpdate();

                    // 5.3 查询生成的UUID并赋值给user_data
                    std::string get_token_sql = "SELECT token FROM usertable WHERE userid = ?";
                    std::unique_ptr<sql::PreparedStatement> token_stmt(con->prepareStatement(get_token_sql));
                    token_stmt->setInt(1, user_data.uid);
                    std::unique_ptr<sql::ResultSet> token_res(token_stmt->executeQuery());
                    if (token_res->next()) {
                        user_data.token = token_res->getString("token");
                    }

                    // 5.4 设置成功状态
                    status = static_cast<int>(LoginStatus::SUCCESS);
                }

                // 6. 无论成功与否，通过handle回调返回结果
                handle(client_socket, status, user_data,origin);

            }
            catch (const sql::SQLException& e) {  // 捕获数据库异常
                std::cerr << "SQL Error: " << e.what() << " (Error code: " << e.getErrorCode() << ")" << std::endl;
                status = static_cast<int>(LoginStatus::DB_ERROR);
                handle(client_socket, status, user_data,origin);
            }
            catch (const std::exception& e) {  // 捕获其他异常
                std::cerr << "Error: " << e.what() << std::endl;
                handle(client_socket, status, user_data,origin);
            }
        }


    };


}


#endif 

