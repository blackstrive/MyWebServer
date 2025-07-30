#include "sql_connection_pool.h"
#include <pthread.h>
#include <stdlib.h>
using namespace std;

MYSQL *connection_pool::GetConnection()
{
    MYSQL* con=NULL;
    if(0==connList.size())
    {
        return nullptr;
    }
    reserve.wait();
    lock.lock();
    con =connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;
    lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn)
    {
        return false;
    }
    lock.lock();
    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;
    lock.unlock();
    reserve.post();
    return true;
}

int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

void connection_pool::DestoryPool()
{
    lock.lock();
    if(connList.size()>0)
    {
        for(auto it=connList.begin();it!=connList.end();it++)
        {
            MYSQL* con=*it;
            mysql_close(con);
        }
        m_CurConn=0;
        m_FreeConn=0;
        connList.clear();
    }
    lock.unlock();
}

connection_pool *connection_pool::GetInstance()
{
    static connection_pool connPoll;
    return &connPoll;
}

void connection_pool::init(string url, string User, string passWord, string DataBasename, int Port, int MaxConn, int close_log)
{
    m_Url=url;
    m_User=User;
    m_Port=Port;
    m_PassWord=passWord;
    m_DataBaseName=DataBasename;
    m_close_log=close_log;

    for(int i=0;i<MaxConn;i++)
    {
        MYSQL*con=nullptr;
        con=mysql_init(con);
        if(nullptr==con)
        {
            //日志
            //cout<<"Mysql error 1"<<endl;
            LOG_ERROR("MySQL Error1");
            exit(1);
        }
        con=mysql_real_connect(con,url.c_str(),User.c_str(),passWord.c_str(),DataBasename.c_str(),Port,NULL,0);
        if(NULL == con)
        {
            //cout<<"mysql error 2"<<endl;
            LOG_ERROR("MySQL Error2");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }
    reserve=sem(m_FreeConn);
    m_MaxConn=m_FreeConn;
}

connection_pool::connection_pool()
{
    m_CurConn=0;
    m_FreeConn=0;
}

connection_pool::~connection_pool()
{
    DestoryPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL=connPool->GetConnection();
    conRAII=*SQL;
    poolRAII=connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}
