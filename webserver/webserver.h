#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"
#include "../log/log.h"

const int MAX_FD = 65536; //最大文件描述符数量
const int MAX_EVENT_NUMBER =10000;
const int TIMESLOT = 5; //定时器时间间隔

class WebServer
{
public:
    WebServer(char *server_Path);
    ~WebServer();
    
    void init(int port, string user, string passwd,string databaseName,
             int log_write, int opt_linger, int trigmode, int sql_num,
             int thread_num, int close_log, int actor_model);
    
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mod();
    void eventListen();
    void eventLoop();
    void timer(int connfd, sockaddr_in client_address);//
    void adjust_timer(util_timer *timer);//
    void deal_timer(util_timer *timer, int sockfd);//
    bool dealClientConn();
    bool dealSignal(bool &timeout, bool &stop_server);//
    void dealRead(int sockfd);
    void dealWrite(int sockfd);

public:
    int m_port; //服务器端口
    char *m_root; 
    int m_log_write;
    int m_close_log;
    int m_actormodel;
    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //datebase
    connection_pool *m_connPool;
    string m_user;
    string m_passWord;
    string m_databaseName;
    int m_sql_num;

    //threadpool
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_OPT_LINGER; //优雅关闭连接
    int m_TRIGMode; //触发模式
    int m_listen_trigmode; //监听socket触发模式
    int m_conn_trigmode; //连接socket触发模式

    //timer
    client_data *users_timer;//
    Utils utils; //定时器工具类
};
#endif