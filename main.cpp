#include<iostream>
#include"./CGImysql/sql_connection_pool.h"
#include"./http/http_conn.h"
#include"./threadpool/threadpool.h"
#include"./webserver/webserver.h"
#include "./log/log.h"
using namespace std;

int main()
{

    string user="zx";
    string passwd="123456";
    string databasename="yourdb";

    char server_Path[100];
    getcwd(server_Path, sizeof(server_Path));
    WebServer server(server_Path);
    server.init(9006,user,passwd,databasename,1,0,0,8,8,0,0);

    server.log_write();
    server.sql_pool();
    server.thread_pool();
    server.trig_mod();
    server.eventListen();
    server.eventLoop();
    
    return 0;
}