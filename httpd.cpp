/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */

#define _CRT_SECURE_NO_WARNINGS

#include "video_upload.h"

#include <WS2tcpip.h>//socket_t 统一长度
#include <thread>
#include <string>


#define S_IXUSR 00100
#define S_IXGRP 00010
#define S_IXOTH 00001
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: zshhttpd/1.0.1\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2
#define Location ""
void accept_request(SSL* );
void bad_request(SSL*);
void cat(SSL*, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(SSL*, const char *, const char *, const char *);
int get_line(SSL*, char *, int);
void headers(SSL*, const char *,FILE*);
void not_found(SSL*);
void serve_file(SSL*, const char *);
int startup(u_short *);
void unimplemented(SSL*);

struct PipeHandles {
    HANDLE hRead;
    HANDLE hWrite;
};





HANDLE g_goProcessHandle = NULL;// 全局保存 Go 进程句柄

// 确保程序退出时终止 Go 进程
void CleanupGoProcess() {
    if (g_goProcessHandle != NULL) {
        // 终止 Go 进程（退出码 0 表示正常终止）
        TerminateProcess(g_goProcessHandle, 0);
        std::cout << "Go 进程已被终止" << std::endl;
        CloseHandle(g_goProcessHandle);
        g_goProcessHandle = NULL;
    }
}

// 启动 Go 程序并关联到作业对象（确保同生共死）
bool StartGoProgram(const std::string& goExePath, const std::string& args) {
    // 创建作业对象（用于关联子进程，父进程退出时自动终止子进程）
    HANDLE hJob = CreateJobObject(NULL, NULL);
    if (hJob == NULL) {
        std::cerr << "CreateJobObject fail，errcode: " << GetLastError() << std::endl;
        return false;
    }

    // 配置作业对象：父进程退出时终止所有关联的子进程
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        std::cerr << "SetInformationJobObject fail，errcode: " << GetLastError() << std::endl;
        CloseHandle(hJob);
        return false;
    }

    // 启动 Go 进程
    std::string cmdLine = "\"" + goExePath + "\" " + args;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    bool success = CreateProcess(
        NULL,
        const_cast<char*>(cmdLine.c_str()),
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED,  // 先暂停进程，便于关联到作业对象
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!success) {
        std::cerr << "CreateProcess fail,errcode: " << GetLastError() << std::endl;
        CloseHandle(hJob);
        return false;
    }

    // 将 Go 进程关联到作业对象
    if (!AssignProcessToJobObject(hJob, pi.hProcess)) {
        std::cerr << "AssignProcessToJobObject fail,errcode: " << GetLastError() << std::endl;
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hJob);
        return false;
    }

    // 恢复 Go 进程执行
    ResumeThread(pi.hThread);

    // 保存进程句柄（用于主动终止）
    g_goProcessHandle = pi.hProcess;
    // 关闭线程句柄（不再需要）
    CloseHandle(pi.hThread);
    // 不关闭作业对象句柄，确保父进程退出时作业对象关闭，触发子进程终止

    std::cout << "Go progress ,PID: " << pi.dwProcessId << std::endl;
    return true;
}



// 专门处理OPTIONS预检请求
void handle_options_request(SSL* client) {
    char buf[1024];
    size_t numchars;
    std::string origin;
    std::string request_method;
    std::string request_headers;


    numchars = get_line(client, buf, sizeof(buf));
    while (numchars > 0) {
        // 移除行尾的换行符（处理 \r\n 或 \n）
        buf[strcspn(buf, "\r\n")] = '\0';//strcspn(buf, "\r\n") 返回buf中连续且不含后面字符的长度

        if (findCaseInsensitive(buf,"Origin:") == 0) {
            origin = buf + 8;
            origin.erase(0, origin.find_first_not_of(" \r"));//手动去空格
        }
        // 解析请求的方法
        else if (findCaseInsensitive(buf,"Access-Control-Request-Method:") == 0) {
            request_method = buf + 30;
            request_method.erase(0, request_method.find_first_not_of(" \r"));
        }
        // 解析请求的自定义头
        else if (findCaseInsensitive(buf, "Access-Control-Request-Headers:") == 0) {
            request_headers = buf + 31;
            request_headers.erase(0, request_headers.find_first_not_of(" \r"));
        }

        numchars = get_line(client, buf, sizeof(buf));
        if (numchars == 2 && buf[0] == '\r' && buf[1] == '\n') break;
        if (numchars == 1 && buf[0] == '\n') break;
    }

    // 生成CORS响应头
    std::string cors_headers;
    cors_headers += "Access-Control-Allow-Origin: " + origin + "\r\n";
    cors_headers += "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, " + request_method + "\r\n";
    if (!request_headers.empty()) {
        cors_headers += "Access-Control-Allow-Headers: " + request_headers + "\r\n";
    }
    cors_headers += "Access-Control-Max-Age: 86400\r\n";
    cors_headers += "Access-Control-Allow-Credentials: true\r\n";

    // 返回204响应（预检请求无需响应体）
    std::string response = "HTTP/1.1 204 No Content\r\n" + cors_headers + "\r\n";
    std::cout << response << std::endl;
    SSL_write(client, response.c_str(), response.size());
    connectionPool.closeConnection(SSL_get_fd(client));
}





/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(SSL* Socket)
{
    
    try{
       SSL* client = Socket;
        char buf[1024];
        size_t numchars;
        char method[255];
        char url[255];
        char path[512];
        size_t i, j;
        struct stat st;
        int cgi = 0;      /* becomes true if server decides this is a CGI
                           * program */
        char* query_string = NULL;

        numchars = get_line(client, buf, sizeof(buf));
        std::cout << buf << std::endl;
        i = 0; j = 0;
        //http首行为get http....
       while (i < numchars && i < sizeof(buf) && !ISspace(buf[i]) && (i < sizeof(method) - 1))  //buf边界！！！！       
        {
            method[i] = buf[i];
            i++;
        }
        j = i;
        method[i] = '\0';
        if (strcmp(method, "OPTIONS") == 0) {
            handle_options_request(client);
            return;
        }
        if (strcmp(method, "GET") && strcmp(method, "POST"))
        {
            unimplemented(client);
            return;
        }

        if (strcmp(method, "POST") == 0)
            cgi = 1;

        i = 0;
        while (ISspace(buf[j]) && (j < numchars))//万一有多个空格
            j++;
        while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
        {
            url[i] = buf[j];
            i++; j++;
        }
        url[i] = '\0';

        if (strcmp(method, "GET") == 0)
        {
            query_string = url;
            while ((*query_string != '?') && (*query_string != '\0'))
                query_string++;
            if (*query_string == '?')
            {
                cgi = 1;
                *query_string = '\0';
                query_string++;
            }
        }
        sprintf(path, "htdocs/");
        // 跳过url开头的斜杠（如果有的话）
        const char* url_start = url[0] == '/' ? url + 1 : url;
        strcat(path, url_start);
        if (path[strlen(path) - 1] == '/')
            strcat(path, "index.html");
        if (stat(path, &st) == -1) {//judge exist path;//文件（.*）为严格判断，
            while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
                numchars = get_line(client, buf, sizeof(buf));
            not_found(client);
        }
        else
        {
            if ((st.st_mode & S_IFMT) == S_IFDIR)//判断是不是目录
                //strcat(path, "/index.html");//
                if ((st.st_mode & S_IXUSR) ||
                    (st.st_mode & S_IXGRP) ||
                    (st.st_mode & S_IXOTH))
                    cgi = 1;
            if (!cgi)
                serve_file(client, path);
            else
                execute_cgi(client, path, method, query_string);
        }
        //closesocket(client);
    }
    catch (const std::exception& e) {
        std::cerr << "Thread exception may  accept_request : " << e.what() << std::endl;
    }
      
    connectionPool.closeConnection(SSL_get_fd(Socket));
}


/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(SSL* client)
{
    char buf[1024];
    int ret; 

    // 1. 发送HTTP 400状态行
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    ret = SSL_write(client, buf, strlen(buf)); // 用strlen获取实际长度，避免发送冗余0
    if (ret <= 0) {
        // 可选：添加SSL错误处理（如打印日志）
        int ssl_err = SSL_get_error(client, ret);
        fprintf(stderr, "SSL_write failed (status line): error code %d\n", ssl_err);
        ERR_print_errors_fp(stderr);
    }

    // 2. 发送Content-type头部
    sprintf(buf, "Content-type: text/html\r\n");
    SSL_write(client, buf, strlen(buf)); // 同样使用实际字符串长度

    // 3. 发送头部结束分隔符（空行）
    sprintf(buf, "\r\n");
    SSL_write(client, buf, strlen(buf));

    // 4. 发送HTML响应内容（分两段拼接）
    sprintf(buf, "<P>Your browser sent a bad request, ");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    SSL_write(client, buf, strlen(buf));
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
// 通过SSL连接发送文件内容


void cat(SSL* client, FILE* resource) {
    char buf[8192];
    size_t bytes_read;
    int error_occurred = 0;

    while (!error_occurred && (bytes_read = fread(buf, 1, sizeof(buf), resource)) > 0) {
        size_t total_sent = 0;

        while (total_sent < bytes_read) {
            int bytes_sent = SSL_write(client, buf + total_sent, bytes_read - total_sent);

            if (bytes_sent <= 0) {
                int ssl_err = SSL_get_error(client, bytes_sent);
                // 判断是否为可重试的非致命错误
                if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
                    // 暂时等待：SSL_ERROR_WANT_WRITE 需等待可写，SSL_ERROR_WANT_READ 需等待可读
                    printf("Need to wait (error: %d), retrying...\n", ssl_err);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue; // 重试 SSL_write()
                }
                else {
                    // 致命错误：记录并终止
                    fprintf(stderr, "SSL_write failed (fatal error: %d)\n", ssl_err);
                    ERR_print_errors_fp(stderr);
                    error_occurred = 1;
                    break;
                }
            }

            total_sent += bytes_sent;
        }

        if (error_occurred) break;
    }

    if (error_occurred) {
        fprintf(stderr, "Aborted due to SSL send error\n");
    }
    else if (ferror(resource)) {
        fprintf(stderr, "Error reading file\n");
    }
    else {
        printf("File sent completely\n");
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
	std::cout << "error_die may be : " << sc << std::endl;
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
std::wstring MutilByteToWide(const std::string& _src)
{
    //计算字符串 string 转成 wchar_t 之后占用的内存字节数
    int nBufSize = MultiByteToWideChar(GetACP(), 0, _src.c_str(), -1, NULL, 0);

    //为 wsbuf 分配内存 BufSize 个字节
    wchar_t* wsBuf = new wchar_t[nBufSize];

    //转化为 unicode 的 WideString
    MultiByteToWideChar(GetACP(), 0, _src.c_str(), -1, wsBuf, nBufSize);

    std::wstring wstrRet(wsBuf);

    delete[]wsBuf;
    wsBuf = NULL;

    return wstrRet;
}

std::vector<std::string> headers_vec = {"videostart"};//重定向不发默认请求头

bool is_in_headers_vec(const std::string& str) {
	for (const auto& header : headers_vec) {
		if (str.find(header) != std::string::npos) {
			return true;
		}
	}
	return false;
}
void execute_cgi(SSL* client, const char* path,
    const char* method, const char* query_string)
{

    char buf[1024] = { 0 };
    int numchars = 1;
    int content_length = -1;
    std::string execute_type = {};
    int Request_VideoId = -1;
	int Request_UserId = -1;
    std::string Content_Type = {};
    std::string origin = {};
    buf[0] = 'A'; buf[1] = '\0';
    if (strcmp(method, "GET") == 0)
    {
        //while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */   
        //    numchars = get_line(client, buf, sizeof(buf));   
        if (!strstr(path, ".cgi"))
        {


            if (strstr(path, "user-videos"))
                Handle_Select_request(client);
            else if (strstr(path, "guestvideos"))
                Handle_command_guest(client, query_string);
            else if (strstr(path, "loginvideos"))
                Handle_command_login(client, query_string);
            else if (strstr(path, "video.html"))
            {
                serve_file(client, path);
                //
            }
            else if (strstr(path, "author_public.html"))
            {
				serve_file(client, path);
            }
            else if (strstr(path, "AuthorData_vneed"))
                user_upload::Handle_userdata_vneed(client);
            else if (strstr(path, "related-guest-videos"))
                Handle_command_guest_related_video(client);
            else if (strstr(path, "getcomments"))
            {
                Handle_getcomments(client, query_string);
            }
            else if(strstr(path, "result_search.html"))
                serve_file(client, path);
            else if (strstr(path, "result_search"))
                Handle_video_search(client, query_string);
            else if (strstr(path, "subscribe"))
            {
                user_upload::Handle_user_subscribe(client);
            }
            else if(strstr(path,"user-thumbsup")){ 
                user_upload::Handle_userthumbsup(client);
            }
            else if(strstr(path,"user-unlike"))
                user_upload::Handle_user_unlike(client);     
            else

				not_found(client);
            return;
        }
    }
    else if (strcmp(method, "POST") == 0) /*POST*/
    {

        if (!strstr(path, ".cgi"))
        {
            if (strstr(path, "login"))
            {
                user_upload::Handle_user_login(client);
            }
            else if (strstr(path, "postcomments"))
            {
                Handle_postcomment_request(client);
            }
            else if (strstr(path, "vp"))
            {
                Handle_pv_Upload(client);
            }
            else if (strstr(path, "vinf"))
            {
                Handle_videoinf_Upload(client);
            }
            else if (strstr(path, "upload_userIcon"))
            {
				user_upload::Handle_usericon_Upload(client);
			}
            else if (strstr(path, "update_profile"))
            {
				user_upload::Handle_update_profile(client);
            }
            else if (strstr(path, "del"))
            {
                Handle_Delete_request(client);
            }
        
            else
                not_found(client);
            return;
        }
        else {
            numchars = get_line(client, buf, sizeof(buf));
            while (numchars > 0) {
                // 移除行尾的换行符（处理 \r\n 或 \n）
                buf[strcspn(buf, "\r\n")] = '\0';//strcspn(buf, "\r\n") 返回buf中连续且不含后面字符的长度
                if (findCaseInsensitive(buf, "Content-Length:") != std::string::npos) {

                    content_length = atoi(buf + 16);  //仅包含一个空格
                }
                else if (findCaseInsensitive(buf, "Content-Type:") == 0)
                {
                    Content_Type = std::string(buf + 14);
                }

                else if (strncmp(buf, "execute-type:", 13) == 0) {

                    execute_type = std::string(buf + 14);
                }
                else if (findCaseInsensitive(buf, "Request-VideoId:") == 0) {
                    Request_VideoId = atoi(buf + 17);
                }
                else if (findCaseInsensitive(buf, "Request-UserId:") == 0) {
                    Request_UserId = atoi(buf + 16);
                }
                else if (findCaseInsensitive(buf, "Origin:") == 0)
                {
                    origin = std::string(buf + 8);
                }
                numchars = get_line(client, buf, sizeof(buf));
                if (numchars == 2 && buf[0] == '\r' && buf[1] == '\n') break;
                if (numchars == 1 && buf[0] == '\n') break;
            }
            if (content_length == -1 || content_length >= 1024 * 1024 * 1) {   //1MB
                const std::string response = build_http_response(413, "application/json", "Payload Too Large", origin);
                ssl_safe_write(client, response);
                return;
            }


            if (execute_type == "videostart")
            {
                if (Request_UserId != -1 && Request_VideoId != -1)
                {

                    user_upload::Handle_videostart(client, Request_UserId, Request_VideoId);
                    const std::string response = build_http_response(200, "application/json", "success", origin);
                    ssl_safe_write(client, response);
                    return;
                }
                else
                {

                    bad_request(client);
                    return;
                }
            }

        }

    }
    else/*HEAD or other*/
    {
        printf("other request type");
        return;
    }



    char CL[255];
    std::string env_str;//
	std::cout << content_length << std::endl;
    sprintf(CL, "%d", content_length);
    // 添加REQUEST_METHOD
    env_str += "REQUEST_METHOD=";
    env_str += method;
    env_str += '\0';// 显式添加分隔符
    env_str += "CONTENT_TYPE=";
    env_str += Content_Type;
    env_str += '\0';
	if (strcmp(method, "GET") == 0) {// query_string为问号后面的参数
        env_str += "QUERY_STRING=";
        env_str += query_string;
        env_str.push_back('\0'); // 显式添加分隔符
    }
    else if (strcmp(method, "POST") == 0) {
        env_str += "CONTENT_LENGTH="; // 注意变量名拼写
        env_str += CL;
        env_str += '\0'; // 显式添加分隔符
    }

    // 添加两个空字符结尾
    env_str.push_back('\0'); // 第二个空字符

    // 配置子进程标准输入输出
    PipeHandles stdinPipe, stdoutPipe;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    // 输入管道：父进程写，子进程读
    if (!CreatePipe(&stdinPipe.hRead, &stdinPipe.hWrite, &sa, 0)) {
        std::cerr << "CreatePipe (stdin) failed: " << GetLastError() << std::endl;
        return;
    }

    // 输出管道：父进程读，子进程写
    if (!CreatePipe(&stdoutPipe.hRead, &stdoutPipe.hWrite, &sa, 0)) {
        std::cerr << "CreatePipe (stdout) failed: " << GetLastError() << std::endl;
        return;
    }


    STARTUPINFO si = { sizeof(STARTUPINFO) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdinPipe.hRead;   // 子进程从 stdinPipe 读取输入
    si.hStdOutput = stdoutPipe.hWrite; // 子进程写入 stdoutPipe
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = { 0 };
    std::string perl_path = "D:/sometools/vcpkg/downloads/tools/perl/5.42.0.1/perl/bin/perl.exe"; // 根据实际安装路径调整

    // std::string command_line = "\"" + perl_path + "\" \"" + path + "\"";
    std::string Url = "C:\\project\\C++\\simple httpd";
    std::string script_path = "C:\\project\\color.cgi";
    std::string command_line = "\"" + perl_path + "\" \"" + path + "\"";
    //std::wstring cmd = MutilByteToWide(command_line);
    // 创建子进程（继承句柄并传递环境变量）//

    if (!CreateProcess(
        NULL,
        const_cast<char*>(command_line.c_str()),
        NULL, NULL,
        TRUE, // 继承句柄
        0,
        (LPVOID)env_str.c_str(),
        NULL,
        &si,
        &pi
    )) {
        std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
        return;
    }

    // 父进程关闭不需要的句柄
    CloseHandle(stdinPipe.hRead);
    CloseHandle(stdoutPipe.hWrite);



    if (!is_in_headers_vec(path))
    {
        sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 修正原错误：HTTP\\1.0 → HTTP/1.0（符合HTTP协议规范）
        ssl_safe_write(client, buf); // 替换send为SSL写入辅助函数
    }
    //if (strcmp(method, "POST") == 0) {
    //    char post_data[4096];
    //    DWORD bytes_read = 0;
    //    DWORD bytes_written = 0;

    //    while ((bytes_read = recv(client, post_data, sizeof(post_data), 0)) > 0) {
    //        WriteFile(hInputPipe, post_data, bytes_read, &bytes_written, NULL);
    //    }
    //    CloseHandle(hInputPipe); // 关闭写入端
    //}

    if (strcmp(method, "POST") == 0) {
        char buffer[4096] = { 0 };
        DWORD bytesRead = 0, bytesWritten = 0;
        int total_read = 0; // 新增：记录已读取的POST数据总长度（避免原逻辑中bytesRead覆盖问题）

        // 从客户端读取数据并写入子进程的输入管道（替换recv为SSL读取辅助函数）
        while (total_read < content_length)
        {
            // 每次读取不超过缓冲区大小，且不超过剩余未读长度
            int read_len = ssl_safe_read(client, buffer, (std::min)((int)sizeof(buffer), (content_length - total_read)));
            if (read_len <= 0) break; // 读取失败或连接关闭

            bytesRead = read_len; // 同步到原逻辑的bytesRead变量
            if (!WriteFile(stdinPipe.hWrite, buffer, bytesRead, &bytesWritten, NULL))
            {
                std::cerr << "Write to stdin failed: " << GetLastError() << std::endl;
                break;
            }
            total_read += bytesRead; // 更新已读总长度
        }

        // 关闭输入管道写入端，通知子进程输入结束
        CloseHandle(stdinPipe.hWrite);
    }

    // 读取子进程的输出（替换send为SSL写入辅助函数）
    char outputBuffer[4096] = { 0 };
    DWORD bytesRead;
    while (ReadFile(stdoutPipe.hRead, outputBuffer, sizeof(outputBuffer), &bytesRead, NULL) && bytesRead > 0) {
		std::cout << outputBuffer << std::endl;
        ssl_safe_write(client, outputBuffer); // 子进程输出通过SSL发送给客户端
        memset(outputBuffer, 0, sizeof(outputBuffer)); // 清空缓冲区（新增：避免残留数据）
    }

    /* child: CGI script */

       //char meth_env[255];
      // char query_env[255];
       //char length_env[255];

       //_dup2(cgi_output[1], STDOUT);
      // _dup2(cgi_input[0], STDIN);
       //closesocket(cgi_output[0]);
      // closesocket(cgi_input[1]);
       //sprintf(meth_env, "REQUEST_METHOD=%s", method);
      // _putenv(meth_env);
      // if (strcmp(method, "GET") == 0) {
         //  sprintf(query_env, "QUERY_STRING=%s", query_string);
       //    _putenv(query_env);
     //  }
     //  else {   /* POST */
     //      sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
     //      _putenv(length_env);
     //  }
      // _execl(path,path, NULL);
     //  exit(0);

    /* parent */

      // closesocket(cgi_output[1]);
       //closesocket(cgi_input[0]);
       //if (strcmp(method, "POST") == 0)
          // for (i = 0; i < content_length; i++) {
              // recv(client, &c, 1, 0);
               // _write(cgi_input[1], &c, 1);
          // }
     //  while (_read(cgi_output[0], &c, 1) > 0)
         //  send(client, &c, 1, 0);

      // closesocket(cgi_output[0]);
     //  closesocket(cgi_input[1]);
       //.wait
    CloseHandle(stdoutPipe.hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

}


/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(SSL* ssl, char* buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    char unread_char = '\0';  // 静态变量缓存预读的字符（替代SSL_unread）

    // 确保缓冲区大小至少为1（留一个位置给终止符）
    if (size <= 1) {
        *buf = '\0';
        return 0;
    }

    while ((i < size - 1) && (c != '\n'))
    {
        // 优先使用上次缓存的字符（如果有）
        if (unread_char != '\0') {
            c = unread_char;
            unread_char = '\0';  // 清空缓存
        }
        else {
            // 读取新字符
            n = SSL_read(ssl, &c, 1);

            if (n <= 0)  // 读取失败或连接关闭
            {
                int ssl_err = SSL_get_error(ssl, n);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    // 暂时无数据或需要重试，继续循环等待
                    continue;
                }
                else {
                    // 致命错误或连接关闭，强制结束
                    c = '\n';
                }
            }
        }

        // 处理回车符\r
        if (c == '\r') {
            // 预读下一个字符
            char peek_c;
            int peek_n = SSL_read(ssl, &peek_c, 1);

            if (peek_n > 0) {
                if (peek_c == '\n') {
                    // 下一个是换行符，直接使用\n作为行结束
                    c = '\n';
                }
                else {
                    // 下一个不是换行符，缓存该字符供下次读取
                    unread_char = peek_c;
                    c = '\n';  // 单独的\r视为行结束
                }
            }
            else {
                // 读取失败或连接关闭，将\r转换为\n
                c = '\n';
            }
        }

        buf[i] = c;
        i++;
    }

    buf[i] = '\0';  // 确保字符串终止
    return i;
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(SSL* client, const char* filename, FILE* resource)
{
    char buf[1024];
    (void)filename;  /* 可用于根据文件名确定文件类型 */

    // 发送HTTP 200状态行
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    SSL_write(client, buf, strlen(buf));

    // 发送服务器标识
    strcpy(buf, SERVER_STRING);
    SSL_write(client, buf, strlen(buf));

    // 根据文件扩展名设置Content-Type
    if (strstr(filename, ".html"))
        sprintf(buf, "Content-Type: text/html\r\n");
    else if (strstr(filename, ".css"))
        sprintf(buf, "Content-Type: text/css\r\n");
    else if (strstr(filename, ".png"))
        sprintf(buf, "Content-Type: image/png\r\n");
    else if (strstr(filename, ".js"))
        sprintf(buf, "Content-Type: application/javascript\r\n");
    else if (strstr(filename, ".ttf"))
        sprintf(buf, "Content-Type: font/ttf\r\n");
    else if (strstr(filename, ".mp4"))
        sprintf(buf, "Content-Type: video/mp4\r\n");
    else if (strstr(filename, ".jpg") || strstr(filename, ".jpeg"))
        sprintf(buf, "Content-Type: image/jpeg\r\n");
    else if (strstr(filename, ".gif"))
        sprintf(buf, "Content-Type: image/gif\r\n");
    else if (strstr(filename, ".svg"))
        sprintf(buf, "Content-Type: image/svg+xml\r\n");
    else if (strstr(filename, ".ico"))
        sprintf(buf, "Content-Type: image/x-icon\r\n");
    else if (strstr(filename, ".webp"))
        sprintf(buf, "Content-Type: image/webp\r\n");
    else
        sprintf(buf, "Content-Type: text/html\r\n");
    SSL_write(client, buf, strlen(buf));

    // 计算并发送Content-Length
    fseek(resource, 0, SEEK_END);
    long file_size = ftell(resource);  // 获取文件大小
    fseek(resource, 0, SEEK_SET);      // 重置文件指针到开头
    snprintf(buf, sizeof(buf), "Content-Length: %ld\r\n", file_size);
    SSL_write(client, buf, strlen(buf));

    // 发送头部结束分隔符
    strcpy(buf, "\r\n");
    SSL_write(client, buf, strlen(buf));
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
void not_found(SSL* client)
{
    char buf[1024];
    int ret;

    // 发送HTTP 404状态行（修正原代码中的斜杠错误：\ 改为 /）
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    ret = SSL_write(client, buf, strlen(buf));
    if (ret <= 0) {
        // 处理SSL写入错误（可根据需要添加日志）
        int ssl_err = SSL_get_error(client, ret);
		std::cout << "SSL_write error: " << ssl_err << std::endl;
    }

    // 发送服务器标识
    sprintf(buf, SERVER_STRING);
    SSL_write(client, buf, strlen(buf));

    // 发送Content-Type头部
    sprintf(buf, "Content-Type: text/html\r\n");
    SSL_write(client, buf, strlen(buf));

    // 发送头部结束分隔符
    sprintf(buf, "\r\n");
    SSL_write(client, buf, strlen(buf));

    // 发送HTML响应内容
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "your request because the resource specified\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "is unavailable or nonexistent.\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "</BODY></HTML>\r\n");
    SSL_write(client, buf, strlen(buf));
}
/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/

// 辅助函数：发送部分文件内容（指定长度）
void cat_partial(SSL* client, FILE* resource, off_t length)
{
    char buf[4096];
    size_t bytes_read;
    off_t remaining = length;
    // 只发送指定长度的内容
    while (remaining > 0 && (bytes_read = fread(buf, 1, sizeof(buf), resource)) > 0) {
        size_t send_size = (remaining < bytes_read) ? remaining : bytes_read;
        SSL_write(client, buf, send_size);
        remaining -= send_size;
    }

}

// 辅助函数：获取文件的MIME类型（示例实现）
const char* get_mime_type(const char* filename)
{
    if (strstr(filename, ".html") || strstr(filename, ".htm"))
        return "text/html";
    else if (strstr(filename, ".jpg") || strstr(filename, ".jpeg"))
        return "image/jpeg";
    else if (strstr(filename, ".png"))
        return "image/png";
    else if (strstr(filename, ".mp4"))
        return "video/mp4";
    else if (strstr(filename, ".pdf"))
        return "application/pdf";
    else
        return "application/octet-stream";  // 默认二进制类型
}

void serve_file(SSL* client, const char* filename)
{
    FILE* resource = nullptr;
    int numchars = 1;
    char buf[1024];
    off_t start = 0, end = 0;  // 字节范围起始和结束位置
    off_t file_size = 0;       // 文件总大小
    int range_requested = 0;   // 是否为Range请求

    // 1. 读取并解析请求头，检查是否有Range头
    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) {
        numchars = get_line(client, buf, sizeof(buf));
        // 检测Range请求头 (格式: Range: bytes=start-end)
        if (findCaseInsensitive(buf, "Range: bytes=") == 0) {
            range_requested = 1;
            // 提取范围值 (例如从 "bytes=100-200" 中提取 100 和 200)
            char* range_str = buf + 13;
            char* dash = strchr(range_str, '-');
            if (dash) {
                *dash = '\0';  // 分割start和end
                start = atoll(range_str);  // 转换起始位置

                // 处理 "bytes=100-" 或 "bytes=-200" 格式
                if (dash[1] != '\0') {
                    end = atoll(dash + 1);
                }
            }
        }
    }

    // 2. 打开文件并获取文件大小
    resource = fopen(filename, "rb");
    if (resource == nullptr) {
        not_found(client);
        return;
    }

    // 获取文件总大小
    fseek(resource, 0, SEEK_END);
    file_size = ftell(resource);
    fseek(resource, 0, SEEK_SET);  // 重置文件指针到开头

    // 3. 处理Range请求范围
    if (range_requested) {
        // 处理特殊情况: "bytes=-200" (最后200字节)bytes=100-200
        if (start == 0 && end == 0 && strchr(buf, '-') != nullptr) {
            start = (file_size > end) ? (file_size - end) : 0;
            end = file_size - 1;
        }
        // 处理 "bytes=100-" (从100到结尾)
        else if (end == 0 || end >= file_size) {
            end = file_size - 1;
        }

        // 验证范围有效性
        if (start > end || start >= file_size) {
            // 范围无效，返回416错误
            char msg[512];
            snprintf(msg, sizeof(msg),
                "HTTP/1.1 416 Range Not Satisfiable\r\n"
                "Content-Range: bytes */%lld\r\n"
                "Connection: close\r\n\r\n",
                (long long)file_size);
            SSL_write(client, msg, strlen(msg));
            fclose(resource);
            return;
        }

        // 发送206部分内容响应头
        char headers[1024];
        const char* mime = get_mime_type(filename);

        snprintf(headers, sizeof(headers),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Content-Length: %lld\r\n"
            "Connection: close\r\n\r\n",
            mime,
            (long long)start,
            (long long)end,
            (long long)file_size,
            (long long)(end - start + 1));

        SSL_write(client, headers, strlen(headers));

        // 定位到请求的起始位置
        fseek(resource, start, SEEK_SET);
        // 发送范围内的内容
        cat_partial(client, resource, end - start + 1);
    }
    // 4. 非Range请求，发送完整文件
    else {
        headers(client, filename, resource); 
        cat(client, resource);                
    }

    fclose(resource);
}


/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;//端口复用
    struct sockaddr_in name;
    /*其中family指明协议族，tinyhttp指定为PF_INET，即IPv4协议；
      type指明套接字类型，tinyhttp指定为SOCK_STREAM，即字节流套接字；
  protocol指定传输协议，设为0时，系统会根据family和type组合选择系统默认值；*/
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));//每个字节都用0填充
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (const char*) & on, sizeof(on))) < 0)
    {  
        error_die("setsockopt failed");
    }
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)  
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }//listen函数的作用是将套接字设置为被动监听状态，等待客户端的连接请求，不会阻塞程序，它只是通知操作系统将套接字设置为监听状态。
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return httpd;
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(SSL* client)
{
    char buf[1024];
    int bytes_sent;

    // 发送HTTP 501状态行
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    bytes_sent = SSL_write(client, buf, strlen(buf));
    if (bytes_sent <= 0) {
        // 处理SSL写入错误
        int ssl_err = SSL_get_error(client, bytes_sent);
        // 可根据需要添加错误日志，例如：
         fprintf(stderr, "SSL_write error: %d\n", ssl_err);
    }

    // 发送服务器标识
    sprintf(buf, SERVER_STRING);
    SSL_write(client, buf, strlen(buf));

    // 发送Content-Type头部
    sprintf(buf, "Content-Type: text/html\r\n");
    SSL_write(client, buf, strlen(buf));

    // 发送头部结束分隔符
    sprintf(buf, "\r\n");
    SSL_write(client, buf, strlen(buf));

    // 发送HTML响应内容
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "</TITLE></HEAD>\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    SSL_write(client, buf, strlen(buf));

    sprintf(buf, "</BODY></HTML>\r\n");
    SSL_write(client, buf, strlen(buf));
}


void init_openssl() {
    try {
        // 1. 初始化 SSL 核心库（含 SSL 协议、错误信息字符串）
        // 标志说明：
        // - OPENSSL_INIT_LOAD_SSL_STRINGS：加载 SSL 相关错误信息（便于调试）
        // - OPENSSL_INIT_LOAD_CRYPTO_STRINGS：加载加密库相关错误信息
        // 注意：无需添加任何算法加载标志，3.5.2 自动通过默认提供者加载所有算法
        OPENSSL_init_ssl(
            OPENSSL_INIT_LOAD_SSL_STRINGS |
            OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
            NULL  // 无自定义配置（使用默认配置）
        );

        // 2. 初始化加密库核心（3.5.2 中此步骤可省略，但保留以兼容低版本 3.x）
        // 第一个参数设为 0：表示使用默认行为（自动加载所有算法）
        OPENSSL_init_crypto(0, NULL);

        

    }
    catch (const std::exception& e) {
        std::cerr << "OpenSSL init error: " << e.what() << std::endl;
        // 初始化失败时强制退出（SSL 依赖核心库，初始化失败无法继续）
        exit(EXIT_FAILURE);
    }
}

// ---------------------- 清理 OpenSSL（vcpkg 现代版本适配） ----------------------

// ---------------------- 主函数中 OpenSSL 相关逻辑（完整适配） ----------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return -1;
    }

    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);

    // 初始化 OpenSSL（vcpkg 版本专用逻辑）
    init_openssl();

    // 创建 SSL 上下文（使用服务器端 TLS 方法）
    const SSL_METHOD* method = TLS_server_method();  // 1.1.0+ 推荐使用
    if (method == NULL) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        ERR_print_errors_fp(stderr);
        return -1;
    }
    // 配置上下文：禁用 SSLv2/SSLv3（强制使用安全协议）
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    // 关键：明确指定支持的 TLS 版本（与 Nginx 保持一致，例如 TLSv1.2 ~ TLSv1.3）
     SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);  // 最低支持 TLSv1.2
     SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);  // 最高支持 TLSv1.3（若 OpenSSL 支持）
    // 配置证书和密钥（vcpkg 安装的 OpenSSL 支持 PEM 格式）
    // 注意：替换为你的证书和密钥路径（相对路径或绝对路径均可）
    if (SSL_CTX_use_certificate_file(ctx, "C:/project/C++/simple httpd/ssl/mhwvideo.site_nginx/mhwvideo.site_bundle.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "C:/project/C++/simple httpd/ssl/mhwvideo.site_nginx/mhwvideo.site.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return -1;
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        std::cerr << "Private key does not match certificate." << std::endl;
        SSL_CTX_free(ctx);
        return -1;
    }

    // 启动服务器（创建监听 Socket，原逻辑保留）
    server_sock = startup(&port);  // 你的 startup 函数
    printf("httpd running on port %d (HTTPS)\n", port);

	std::thread newthread(execute_sql,1);
	newthread.detach();  
	std::thread newthread2(execute_sql,2);
	newthread2.detach();
    std::thread newthread_user(user_upload::execute_sql, 101);
    newthread_user.detach();
    std::thread newthread_user2(user_upload::execute_sql, 102);
    newthread_user2.detach();
	MYSQL_MS().inti_id();
    ThreadPool handerRequset(16);
    // 注册退出清理函数（程序正常退出时调用）
    atexit(CleanupGoProcess);
    std::string goExe = "D:/go_project/go_updater/cmd/api/go_updater.exe";
    std::string args = "";  // 无参数
    if (!StartGoProgram(goExe, args)) {
        std::cout << "fail to init go_updater";
        return 1;
    }
    // 主循环接受连接（核心修改：使用 SSL_accept 而非 SSL_connect）
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_name, &client_name_len);
        if (client_sock == -1) {
            error_die("accept");  // 你的错误处理函数
        }

        // 打印客户端信息
        char ipstr[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &client_name.sin_addr, ipstr, sizeof(ipstr));
        u_short client_port = ntohs(client_name.sin_port);
        printf("client IP: %s, port: %d\n", ipstr, client_port);

        // 创建 SSL 对象并绑定 Socket
        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            ERR_print_errors_fp(stderr);
            closesocket(client_sock);
            continue;
        }
        SSL_set_fd(ssl, client_sock);
		if (SSL_accept(ssl) <= 0) {//ssl shakehand
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            closesocket(client_sock);
            continue;
        }
        
        std::cout << "SSL handshake success, HTTPS connected." << std::endl;
		connectionPool.addConnection(client_sock,ssl);
        // 处理请求（启动线程，传入 SSL 对象）
        handerRequset.enqueue(accept_request, ssl);
    }

    // 程序退出时清理（现代 OpenSSL 简化版）
    SSL_CTX_free(ctx);  // 释放 SSL 上下文
  /*  cleanup_openssl();*/  // vcpkg 版本无需额外操作
    WSACleanup();
    closesocket(server_sock);
    return 0;
}


