#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>
using namespace std;
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string,string> users;
int http_conn::m_epollfd=-1;
int http_conn::m_user_count=0;
int setnonblocking(int fd)
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//向内核事件表注册事件
void addfd(int epollfd,int fd,bool one_shot,int TRIGMod)
{
    epoll_event event;
    event.data.fd = fd;
    if(1==TRIGMod)
    {
        event.events=EPOLLIN|EPOLLET|EPOLLRDHUP; //ET模式
    }
    else
    {
        event.events=EPOLLIN|EPOLLRDHUP; //LT模式
    }
    if(one_shot)
    {
        event.events |= EPOLLONESHOT; //设置为单次触发
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd); //设置为非阻塞
}

void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//将事件设置为EPOLLONESHOT
void modfd(int epollfd,int fd,int ev,int TRIGMod)
{
    epoll_event event;
    event.data.fd=fd;
    if(1==TRIGMod)
    {
        event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    }
    else
    {
        event.events=ev|EPOLLONESHOT|EPOLLRDHUP; //LT模式
    }
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void http_conn::init(int sockfd, const sockaddr_in &addr, 
    char *root, int trigmode, int close_log, 
    std::string user, std::string password, 
    std::string sqlname)
{
    m_sockfd= sockfd;
    m_address= addr;
    addfd(m_epollfd,sockfd,true, trigmode);
    m_user_count++;
    m_close_log=close_log;
    LOG_INFO("client(%s) in, user count:%d", inet_ntoa(addr.sin_addr), m_user_count);
    doc_root=root;
    m_trigmode=trigmode;


    strcpy(sql_user,user.c_str());
    strcpy(sql_password,password.c_str());
    strcpy(sql_name,sqlname.c_str());
    init();
}

void http_conn::close_conn(bool real_close)
{
    if(real_close&&(m_sockfd!=-1))
    {
        //printf("close %d\n",m_sockfd);
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
    }
}

void http_conn::process()
{
    HTTP_CODE read_ret=process_read();
    if(NO_REQUEST ==read_ret)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_trigmode);
        return;
    }
    bool write_ret=process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_trigmode);
}

bool http_conn::read_once()//一次性读取客户数据
{
    if(m_read_idx>=READ_BUFFER_SIZE)
    {
        return false; //读缓冲区已满
    }
    int bytes_read=0;
    // LT模式下，读数据
    if(0==m_trigmode)
    {
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read<=0)
        {
            return false; //读取失败
        }
        m_read_idx+=bytes_read;
        return true;
    }
    //ET
    else
    {
        while(true)
        {
            bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
            if(-1==bytes_read)
            {
                if(errno==EAGAIN||errno==EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if(0==bytes_read)
            {
                return false; //连接关闭
            }
            m_read_idx+=bytes_read;
        }
        return true;
    }

}

bool http_conn::write()
{
    int temp=0;
    if(0 == bytes_to_send)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_trigmode);
        init();
        return true;
    }
    while(true)
    {
        temp=writev(m_sockfd,m_iv,m_iv_count);
        if(temp<0)
        {
            if(EAGAIN == errno)
            {
                modfd(m_epollfd,m_sockfd,EPOLLOUT,m_trigmode);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send+=temp;
        bytes_to_send-=temp;
        if(bytes_have_send>=m_iv[0].iov_len)
        {
            m_iv[0].iov_len=0;
            m_iv[1].iov_base=m_file_address+bytes_have_send-m_write_idx;
            m_iv[1].iov_len=bytes_to_send;
        }
       else
       {
            m_iv[0].iov_base=m_write_buf+bytes_have_send;
            m_iv[0].iov_len=m_iv[0].iov_len-bytes_have_send;
       }
       if(bytes_to_send<=0)
       {
            unmap();
            modfd(m_epollfd,m_sockfd,EPOLLIN,m_trigmode);
            if(m_linger)
            {
                init(); //保持连接
                return true;
            }
            else
            {
                return false; //不保持连接
            }
       }
    }
}

void http_conn::initmysql_result(connection_pool *connPool)
{
    MYSQL* mysql=nullptr;
    connectionRAII mysqlcon(&mysql,connPool);
    if(mysql_query(mysql,"SELECT username,passwd FROM user"))
    {

        LOG_ERROR("Mysql SELECT error:%s\n", mysql_error(mysql));

    }

    MYSQL_RES *result=mysql_store_result(mysql);
    
    //col number
    //int num_fields=mysql_num_fields(result);

        //返回所有字段结构的数组
    //MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    if (result) {  // 确保结果不为空
        //从结果集中获取下一行，将对应的用户名和密码，存入map中
        while (MYSQL_ROW row = mysql_fetch_row(result))
        {
            string temp1(row[0]);
            string temp2(row[1]);
            users[temp1] = temp2;
        }
        
        mysql_free_result(result);  
    }
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql=NULL;
    bytes_to_send=0;
    bytes_have_send=0;
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_linger=false;
    m_method=GET;
    m_url=0;
    m_version=0;
    m_content_length=0;
    m_host=0;
    m_start_line=0;
    m_checked_idx=0;
    m_read_idx=0;
    m_write_idx=0;
    cgi=0;
    m_state=0;
    timer_flag=0;
    improv=0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char* text=0;
    while((CHECK_STATE_CONTENT==m_check_state&&LINE_OK == line_status)||LINE_OK == (line_status=parse_line()))
    {
        text=get_line();
        m_start_line=m_checked_idx;
        LOG_INFO("%s", text);
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(BAD_REQUEST == ret)
                {
                    return BAD_REQUEST; //请求行错误
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(BAD_REQUEST == ret)
                {
                    return BAD_REQUEST;
                }
                else if(GET_REQUEST == ret)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                if(GET_REQUEST == ret)
                {
                    return do_request();
                }
                line_status=LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base=m_file_address;
                m_iv[1].iov_len=m_file_stat.st_size;
                m_iv_count=2;
                bytes_to_send=m_write_idx+m_file_stat.st_size;
                return true;
            }
            else
            {
                const char* ok_string="<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return false;
                }
            }
            break;
        }
        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count=1;
    bytes_to_send=m_write_idx;
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text," \t");
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++='\0';
    char* method=text;
    if(strcasecmp(method,"GET")==0)
    {
        m_method=GET;
    }
    else if(strcasecmp(method,"POST")==0)
    {
        m_method=POST;
        cgi=1;
    }
    else
    {
        return BAD_REQUEST;
    }
    m_url+=strspn(m_url," \t");
    m_version=strpbrk(m_url," \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++='\0';
    m_version+=strspn(m_version," \t");
    if(strcasecmp(m_version,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST; //只支持HTTP/1.1
    }
    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');     
    }
    else if(strncasecmp(m_url,"https://",8)==0)
    {
        m_url+=8;
        m_url=strchr(m_url,'/');
    }
    if(!m_url||m_url[0]!='/')
    {
        return BAD_REQUEST;
    }
    if(strlen(m_url)==1)
    {
        strcat(m_url,"judge.html"); 
    }
    m_check_state=CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if('\0' == text[0])
    {
        if(m_content_length!=0)
        {
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"Content-Length:",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atoi(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else
    {
        LOG_INFO("http_conn:unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if(m_read_idx>=m_content_length+m_checked_idx)
    {
        text[m_content_length]='\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file,doc_root);
    int len=strlen(doc_root);
    //cout<<"m_url: "<<m_url<<endl;
    const char* p=strchr(m_url,'/');
    if(strncasecmp(p,"/24",3)==0)
    {
        p+=3;
        if(strncasecmp(p,"4",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2440.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2441.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2442.html");
            }
        }
        else if(strncasecmp(p,"6",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2460.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2461.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2462.html");
            }
        }
    }
    else if(strncasecmp(p,"/23",3)==0)
    {
        p+=3;
        if(strncasecmp(p,"4",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2340.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2341.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2342.html");
            }
        }
        else if(strncasecmp(p,"6",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2360.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2361.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2362.html");
            }
        }
    }
    else if(strncasecmp(p,"/22",3)==0)
    {
        p+=3;
        if(strncasecmp(p,"4",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2240.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2241.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2242.html");
            }
        }
        else if(strncasecmp(p,"6",1)==0)
        {
            p+=1;
            if('0' == *p)
            {
                strcpy(m_real_file+len,"/2260.html");
            }
            else if('1' == *p)
            {
                strcpy(m_real_file+len,"/2261.html");
            }
            else if('2' == *p)
            {
                strcpy(m_real_file+len,"/2262.html");
            }
        }
    }
    else
    {
        if(1==cgi&&('2' == *(p+1)||'3' == *(p+1)))//2登录校验3注册校验
        {
            char* m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/");
            strcat(m_url_real,m_url+2);
            strncpy(m_real_file+len,m_url_real,FILENAME_LEN-len-1);
            free(m_url_real);
            //将用户名和密码提取出来
            //user=123&password=123
            char name[100],password[100];
            int i;
            for(i=5;m_string[i]!='&';++i)
            {
                name[i-5]=m_string[i];
            }
            name[i-5]='\0';
            int j=0;
            for(i=i+10;m_string[i]!='\0';++i,++j)
            {
                password[j]=m_string[i];
            }
            password[j]='\0';
            if('3' == *(p+1))
            {
            if(users.find(name)==users.end())
            {
                char*sql_insert=(char*)malloc(sizeof(char*)*200);
                strcpy(sql_insert,"INSERT into user (username,passwd) VALUES ('");
                strcat(sql_insert,name);
                strcat(sql_insert,"','");
                strcat(sql_insert,password);
                strcat(sql_insert,"');");

                m_lock.lock();
                int res =mysql_query(mysql,sql_insert);
                users.insert(pair<string,string>(name,password));
                m_lock.unlock();

                if(!res)
                {
                    strcpy(m_url,"/log.html");
                }
                else
                {
                    strcpy(m_url,"/registerError.html");
                }
            }
            else
            {
                strcpy(m_url,"/registerError.html");
            }  
            }
            else if('2' == *(p+1))
            {
                if(users.find(name) != users.end() && users[name] == password)
                {
                    strcpy(m_url,"/welcome.html");
                }
                else
                {
                    strcpy(m_url,"/logError.html");
                }
            }
        }

        if('0' == *(p+1))
        {
            char *m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/register.html");
            strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
            free(m_url_real);
        }
        else if('1' == *(p+1))
        {
            char *m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/log.html");
            strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
            free(m_url_real);
        }
        else if( '5' == *(p+1))
        {
            char *m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/picture.html");
            strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
            free(m_url_real);
        }
        else if('6' == *(p+1))
        {
            char *m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/video.html");
            strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
            free(m_url_real);
        }
        else if('7' == *(p+1))
        {
            char *m_url_real=(char*)malloc(sizeof(char)*200);
            strcpy(m_url_real,"/fans.html");
            strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
            free(m_url_real);
        } 
        else
        {
            strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
        }
    }
    //cout<<"m_real_file: "<<m_real_file<<endl;
    if(stat(m_real_file,&m_file_stat)<0)
    {
        //cout<<"no resource"<<endl;
        return NO_RESOURCE; //资源不存在
    }
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        //cout<<"forbidden request"<<endl;
        return FORBIDDEN_REQUEST;        
    }


    if (S_ISDIR(m_file_stat.st_mode))
    {
       // cout<<"is a directory"<<endl;
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;    
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(;m_checked_idx<m_read_idx;m_checked_idx++)
    {
        temp=m_read_buf[m_checked_idx];
        if('\r'==temp)
        {
            if(m_checked_idx+1==m_read_idx)
            {
                return LINE_OPEN;
            }
            else if('\n'==m_read_buf[m_checked_idx+1])
            {
                m_read_buf[m_checked_idx++]='\0'; //将\r替换为\0
                m_read_buf[m_checked_idx++]='\0'; //将\n替换为\0
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if('\n' == temp)
        {
            if(m_checked_idx>1&&'\r' == m_read_buf[m_checked_idx-1])// 
            {
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;

}

void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if(m_write_idx>=WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len=vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-m_write_idx-1,format,arg_list);
    if(len>=WRITE_BUFFER_SIZE-m_write_idx-1)
    {
        va_end(arg_list);
        return false; 
    }
    m_write_idx+=len;                                               
    va_end(arg_list);
    //log
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s",content);
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers(int content_length)
{
    return add_content_length(content_length)&&
           add_linger() &&
           add_blank_line();
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length:%d\r\n", content_length);
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n",(true == m_linger)?"keep-alive":"close");

}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
