#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>

#include "http_conn.h"
#include "threadpool.h"


const int MAX_FD = 65536;		//在最大文件描述符
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer {
public:
	WebServer();
	~WebServer();

	void init(	int port ,string user, string passwd, string databasename,
				int log_write, int opt_linger, int trigmode, int sql_num,
				int thread_num, int close_log, int actor_model);
	void log_write();
	void sql_pool();
	void thread_pool();
	void trig_mode();
	void eventListen();
	void timer(int connfd, struct sockaddr_in client_address);
	void adjust_timer(util_timer *timer);
	void deal_timer(util_timer* timer, int sockfd);
	bool dealclientdata();
	bool dealwithsignal(bool &timeout,bool &stop_ever);
	void dealwithread(int sockfd);
	void dealwithwrite(int sockfd);
	void eventLoop();

public:
	int m_port;
	char *m_root;
	int m_log_write;
	int m_close_log;
	int m_actor_model;

	//线程池
	threadpool<http_conn>* m_pool;
	int m_thread_num;

	//epoll_event相关,被触发事件存放的数组
	epoll_event events[MAX_EVENT_NUMBER];

	//数据库相关
	sql_connection_pool *m_connPool;
	string m_user;			//登录数据库用户名
	string m_passwd;		//登录数据库密码
	string m_databasename;	//使用的数据库名
	int m_sql_num;			//数据库数量

	int m_pipefd[2];
	int m_epollfd;
	http_conn* users;

	int m_listenfd;
	int m_opt_linger;		//优雅关闭，0为关闭，1为开启。延时关闭，即关闭服务时等待数据发送完整
	int m_TRIGmode;
	int m_listenTrigmode;
	int m_connTrigmode;

	//定时器
	client_data* users_timer;
	Utils utils;


};


#endif