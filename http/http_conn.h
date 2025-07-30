#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <iostream>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

using namespace std;
class http_conn 
{
public:
    static const int FILENAME_LEN=200; //文件名的最大长度
    static const int READ_BUFFER_SIZE=2048; //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE=1024; //写缓冲区的大小

    enum METHOD//HTTP请求方法
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    enum CHECK_STATE //主状态机的状态
    {
        CHECK_STATE_REQUESTLINE = 0, //正在分析请求行
        CHECK_STATE_HEADER, //正在分析头部字段
        CHECK_STATE_CONTENT //正在分析消息体
    };
    enum HTTP_CODE
    {
        NO_REQUEST=0, //请求不完整，需要继续读取
        GET_REQUEST, //获得了完整的HTTP请求
        BAD_REQUEST, //HTTP请求有语法错误
        NO_RESOURCE, //没有资源
        FORBIDDEN_REQUEST, //没有权限访问资源
        FILE_REQUEST, //文件请求，获取文件成功
        INTERNAL_ERROR, //服务器内部错误
        CLOSED_CONNECTION //连接关闭
    };
    enum LINE_STATUS //行的读取状态
    {
        LINE_OK = 0, //读取到一个完整的行
        LINE_BAD, //行出错
        LINE_OPEN //行未完整
    };

public:
    http_conn() {}
    ~http_conn() {}
    void init(int sockfd,const sockaddr_in &addr,char *root, int trigmode, int close_log, std::string user, std::string password, std::string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address() { return &m_address; }
    void initmysql_result(connection_pool *connPool);
    int timer_flag; //定时器标志
    int improv; //是否需要重置定时器

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
public:
    static int m_epollfd; //epoll的文件描述符
    static int m_user_count; //统计用户数量
    MYSQL *mysql; //数据库连接
    int m_state; //读为0，写为1
private:
    int m_sockfd; //socket文件描述符
    sockaddr_in m_address; //客户端地址
    char m_read_buf[READ_BUFFER_SIZE]; //读缓冲区
    int m_read_idx; //读缓冲区最后一个字节的下一个位置
    int m_checked_idx; //当前正在分析的字符在读缓冲区的位置
    int m_start_line; //已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE]; //写缓冲区
    int m_write_idx; //写缓冲区的长度   
    CHECK_STATE m_check_state; //主状态机状态
    METHOD m_method; //请求方法
    char m_real_file[FILENAME_LEN]; //请求的文件名
    char *m_url; //请求的URL  
    char *m_version; //HTTP版本
    char *m_host; //主机名
    long m_content_length; //HTTP请求的消息体长度
    bool m_linger; //是否保持连接
    char *m_file_address; //内存映射到文件的地址
    struct stat m_file_stat; //文件状态
    struct iovec m_iv[2]; //io向量结构体
    int m_iv_count; //io向量的数量
    int cgi; //是否启用POST
    char *m_string; //请求头字段
    int bytes_to_send; //剩余发送字节数
    int bytes_have_send; //已经发送的字节数
    char* doc_root; //网站根目录

    map<std::string, std::string> m_users; //用户信息，用户名和密码的映射
    int m_trigmode; //触发模式
    int m_close_log; //日志开关

    char sql_user[100]; //数据库用户名
    char sql_password[100]; //数据库密码
    char sql_name[100]; //数据库名
};

#endif