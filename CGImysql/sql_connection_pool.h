#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include"../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
    public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL* conn);
    int GetFreeConn();
    void DestoryPool();

    //单例模式
    static connection_pool *GetInstance();

    void init(string url,string User,string passWord,string DataBasename,int Port,int MaxConn,int close_log);

    private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;
    int m_CurConn;
    int m_FreeConn;
    locker lock;
    list<MYSQL*> connList;//连接池
    sem reserve;

    public:
    string m_Url;//主机地址
    string m_Port;
    string m_User;
    string m_PassWord;
    string m_DataBaseName;
    int m_close_log;//日志开关
};

class connectionRAII
{
    public:
  connectionRAII(MYSQL** SQL,connection_pool*connPool);
  ~connectionRAII();
    private:
    MYSQL *conRAII;
    connection_pool* poolRAII;
};

#endif