#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <pthread.h>
#include "lock.h"
#include "sql_connection_pool.h"

// 线程池类 定义成模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool {
public:
	threadpool(int actor_model, sql_connection_pool *connPool,
		int thread_number = 8, int max_requests = 10000);
	~threadpool();
	bool append(T *request,int state);
	bool append_p(T *request);

private:
	int m_actor_model;					//模型
	int m_thread_number;				//线程池中的线程数
	int m_max_requests;					//请求队列中允许的最大请求数
	pthread_t* m_threads;				//线程池的数组，其大小为m_thread_number
	sql_connection_pool* m_connPool;		//数据库
	int actor_model;					//模型

	locker m_queuelocker;				//保护队列的互斥锁
	std::list<T*> m_workqueue;			//工作队列
	sem m_queuestat;					//信号量，判断是否有任务

	static void* worker(void *arg);
	void run();
};

template <typename T>
threadpool<T>::threadpool(int actor_model, sql_connection_pool* connPool, int thread_number, int max_requests) :
	m_actor_model(actor_model),
	m_connPool(connPool),
	m_thread_number(thread_number),
	m_max_requests(max_requests),
	m_threads(NULL)
{
	if (thread_number <= 0 || max_requests <= 0) {
		throw std::exception();
	}

	m_threads = new pthread_t[m_thread_number];

	if (!m_threads) {
		throw std::exception();
	}

	for (int i = 0; i < m_thread_number; i++) {
		if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
			delete[] m_threads;
			throw std::exception();
		}

		if (pthread_detach(m_threads[i])) {
			delete[] m_threads;
				throw std::exception();
		}
	}
}
template<typename T>
//reactor
bool threadpool<T>::append(T *request, int state){
	m_queuelocker.lock();
	if (m_workqueue.size() >= m_max_requests) {
		m_queuelocker.unlock();
		return false;
	}
	request->m_state = state;
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	return true;
}

template<typename T>
//proactor
bool threadpool<T>::append_p(T *request) {
	m_queuelocker.lock();
	if (m_workqueue.size() >= m_max_requests) {
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();
	return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg) {
	threadpool* pool = (threadpool*)arg;
	pool->run();

	return pool;
}

template <typename T>
void threadpool<T>::run() {
	while (true) {
		
		m_queuestat.wait();
		m_queuelocker.lock();
		if (m_workqueue.empty()) {
			//如果工作队列内为空，释放锁,continue
			m_queuelocker.unlock();
			continue;
		}

		//如果不为空，就把任务取出来，再根据模型选择处理模式 (ET or LT)
		T* request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();

		if (!request) {
			continue;
		}

		//reactor
		if (1 == m_actor_model) 
		{
			//判断是否为读，读为0
			if (0 == request->m_state) 
			{
				//读到数据
				if (request->read_once()) 
				{
					request->improv = 1;
					connectionRAII mysqlcon(&request->mysql, m_connPool);
					request->process();
				}
				//未读到数据
				else 
				{
					request->improv = 1;
					request->timer_flag = 1;
				}
			}
			//写，写为1
			else 
			{
				//写成功
				if (request->write()) 
				{
					printf("write success\n");
					request->improv = 1;
				}
				//写失败
				else 
				{
					request->improv = 1;
					request->timer_flag = 1;
				}
			}
		}
		//proactor
		else {
			connectionRAII mysqlcon(&request->mysql, m_connPool);
			request->process();	
		}
	}
}

template <typename T>
threadpool<T>::~threadpool() {
	delete[] m_threads;
}
#endif