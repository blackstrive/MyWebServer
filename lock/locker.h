#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

class sem
{
public :
    sem()
    {
        if(0!=sem_init(&m_sem,0,0))
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if(0!=sem_init(&m_sem,0,num))
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait()
    {
        return 0==sem_wait(&m_sem);
    }
    bool post()
    {
        return 0==sem_post(&m_sem);
    }
private:
sem_t m_sem;    
};

class locker
{
public:
    locker()
    {
        if(0!=pthread_mutex_init(&m_mutex,NULL))
        {
        throw std::exception();    
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return 0==pthread_mutex_lock(&m_mutex);
    }
    bool unlock()
    {
        return 0==pthread_mutex_unlock(&m_mutex);
    }
    pthread_mutex_t* get()
    {
        return &m_mutex;
    }
private:
pthread_mutex_t m_mutex;
};
class cond
{
public:
    cond()
    {
        if(0!=pthread_cond_init(&m_cond,NULL))
        {
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret=0;
        ret=pthread_cond_wait(&m_cond,m_mutex);
        return 0==ret;
    }
    bool timewait(pthread_mutex_t *m_mutex,struct timespec t)
    {
        int ret=0;
        ret=pthread_cond_timedwait(&m_cond,m_mutex,&t);
        return 0==ret;
    }
    bool signal()
    {
        return 0==pthread_cond_signal(&m_cond);
    }
    bool broadcast()
    {
        return 0==pthread_cond_broadcast(&m_cond);
    }
private:
    pthread_cond_t m_cond;
};
#endif