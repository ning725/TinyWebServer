#include "webserver.h"

WebServer::WebServer() {
	//http_conn类对象
	users = new http_conn[MAX_FD];

	//root文件夹路径
	char server_path[200];
	getcwd(server_path, 200);
	char root[6] = "/root";
	m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(m_root, server_path);
	strcat(m_root, root);

	//定时器
	users_timer = new client_data[MAX_FD];


}

WebServer::~WebServer() {

	close(m_listenfd);
	close(m_pipefd[0]);
	close(m_pipefd[1]);
	delete[] m_pool;
	delete[] users_timer;
	delete[] users;

}

void WebServer::init(int port, string user, string passwd, string databasename,
	int log_write, int opt_linger, int trigmode, int sql_num,
	int thread_num, int close_log, int actor_model) {

	m_port = port;
	m_user = user;
	m_passwd = passwd;
	m_databasename = databasename;
	m_log_write = log_write;
	m_opt_linger = opt_linger;
	m_TRIGmode = trigmode;
	m_sql_num = sql_num;
	m_thread_num = thread_num;
	m_close_log = close_log;
	m_actor_model = actor_model;

}

void WebServer::log_write() {
	//不关闭日志
	if (0 == m_close_log) {
		//初始化日志
		if (1 == m_log_write) {
			Log::get_instance()->init("./LogRecord/ServerLog",m_close_log,2000,80000,800);
		}
		else {
			Log::get_instance()->init("./LogRecord/ServerLog", m_close_log, 2000, 80000, 0);
		}
	}

}

void WebServer::sql_pool() {
	//初始化数据库连接池
	m_connPool = sql_connection_pool::GetInstance();
	m_connPool->init("localhost",m_user,m_passwd,m_databasename,3306,m_sql_num,m_close_log);

	//初始化数据库读取表
	users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
	m_pool = new threadpool<http_conn>(m_actor_model, m_connPool, m_thread_num);
}

void WebServer::trig_mode() {
	//LT + LT
	if (0 == m_TRIGmode) {
		m_listenTrigmode = 0;
		m_connTrigmode = 0;
	}

	//LT + ET
	if (1 == m_TRIGmode) {
		m_listenTrigmode = 0;
		m_connTrigmode = 1;
	}

	//ET + LT
	if (2 == m_TRIGmode) {
		m_listenTrigmode = 1;
		m_connTrigmode = 0;
	}

	//ET + ET
	if (3 == m_TRIGmode) {
		m_listenTrigmode = 1;
		m_connTrigmode = 1;
	}
}

void WebServer::eventListen(){
	//网络编程基本步骤
	m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(m_listenfd >= 0);

	//优雅关闭
	if (0 == m_opt_linger) {
		struct linger tmp = {0,1};
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	else if (1 == m_opt_linger) {
		struct linger tmp = { 1, 1 };
		setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
	}

	int ret = 0;
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(m_port);

	int flag = 1;
	setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret >= 0);

	ret = listen(m_listenfd, 5);
	assert(ret >= 0);

	utils.init(TIMESLOT);

	//epoll创建内核时间表
	epoll_event events[MAX_EVENT_NUMBER];
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	utils.addfd(m_epollfd, m_listenfd, false, m_listenTrigmode);
	http_conn::m_epollfd = m_epollfd;

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
	assert(ret != -1);
	utils.setnonblocking(m_pipefd[1]);
	utils.addfd(m_epollfd, m_pipefd[0], false, 0);
	
	utils.addsig(SIGPIPE, SIG_IGN);
	utils.addsig(SIGALRM, utils.sig_handler, false);
	utils.addsig(SIGTERM, utils.sig_handler, false);

	alarm(TIMESLOT);

	Utils::u_epollfd = m_epollfd;
	Utils::u_pipefd = m_pipefd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {
	users[connfd].init(connfd,client_address,m_root,m_TRIGmode,m_close_log,m_user,m_passwd,m_databasename);

	//初始化client_data数据
	//创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表
	users_timer[connfd].address = client_address;
	users_timer[connfd].sockfd = connfd;
	util_timer* timer = new util_timer;
	timer->user_data = &users_timer[connfd];
	timer->cb_func = cb_func;
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	users_timer[connfd].timer = timer;
	utils.m_timer_list.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表的位置进行调整
void WebServer::adjust_timer(util_timer *timer) {
	time_t cur = time(NULL);
	timer->expire = cur + 3 * TIMESLOT;
	utils.m_timer_list.adjust_timer(timer);

	LOG_INFO("%s","adjust timer once");
}

//超时从树上摘下sockfd，并从链表中删除
void WebServer::deal_timer(util_timer *timer,int sockfd) {
	
	timer->cb_func(&users_timer[sockfd]);
	if (timer) {
		utils.m_timer_list.del_timer(timer);
	}

	LOG_INFO("close fd %d",users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() {
	struct sockaddr_in client_address;
	socklen_t client_addrlen = sizeof(client_address);
	//0是LT,1是ET 默认为LT
	//为LT,LT只要还有未读出的数据，将会一直触发
	if (0 == m_listenTrigmode) {
		int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlen);
		//如果accept错误
		if (connfd < 0) {
			LOG_ERROR("%s:errno is %d","accept error",errno);
			return false;
		}

		//如果人数大于最大值
		if (http_conn::m_user_count >= MAX_FD) {
			utils.show_error(connfd, "Internal server busy");
			LOG_ERROR("%s", "Internal server busy");
			return false;
		}
		timer(connfd,client_address);
	}

	//为ET，ET只会触发一次，则循环处理
	else {
		//循环
		while (true) {
			int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlen);
			//判断是否accept错误
			if (connfd < 0) {
				LOG_ERROR("%s: errno is","accept error", errno);
				break;
			}

			//判断是否人数大于最大值
			if (http_conn::m_user_count >= MAX_FD) {
				utils.show_error(connfd, "Internal server busy");
				LOG_ERROR("%s","Internal server busy");
				break;
			}
			timer(connfd, client_address);
		}
		return false;
	}
	return true;
}

bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
	int ret = 0;
	int sig;
	char signals[1024];
	ret = recv(m_pipefd[0], signals, sizeof(signals), 0);

	if (ret == -1) {
		return false;
	}

	else if (ret == 0) {
		return false;
	}
	else {
		for (int i = 0; i < ret; i++) {
			switch (signals[i]) {
				case  SIGALRM:
				{
					timeout = true;
					break;
				}
				case SIGTERM:
				{
					stop_server = true;
					break;
				}
			}
		}
	}
	return true;
}

void WebServer::dealwithread(int sockfd) {
	util_timer* timer = users_timer[sockfd].timer;

	//reactor
	//reactor： 主线程只监听是否有事件发生，有的话通知工作线程
	if (1 == m_actor_model) {
		if (timer) {
			adjust_timer(timer);
		}

		//若检测到读事件，把该事件放入请求队列
		m_pool->append(users + sockfd,0);


		//循环判断
		while (true) {
			if (1 == users[sockfd].improv) {
				if (1 == users[sockfd].timer_flag) {
					deal_timer(timer, sockfd);
					users[sockfd].timer_flag = 0;
				}
				users[sockfd].improv = 0;
				break;
			}
		}
	}
	
	//proactor
	//主线程执行数据读操作，读写完成通知工作线程进行写操作
	else {
		//读到数据，（read_once函数为一次读完数据）
		if (users[sockfd].read_once()) {
			LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
			m_pool->append_p(users + sockfd);
			
			if (timer) {
				adjust_timer(timer);
			}
		}
		//没读到数据
		else {
			deal_timer(timer, sockfd);
		}
	}
}

void WebServer::dealwithwrite(int sockfd) {
	util_timer* timer = users_timer[sockfd].timer;

	//reactor
	if (1 == m_actor_model) {
		if (timer) {
			adjust_timer(timer);
		}

		m_pool->append(users + sockfd, 1);
		
		while (true) {
			if (1 == users[sockfd].improv) {
				if (1 == users[sockfd].timer_flag) {
					deal_timer(timer, sockfd);
					users[sockfd].timer_flag = 0;
				}
				users[sockfd].improv = 0;
				break;
			}
		}
	}

	//proactor
	else {

		//写成功
		if (users[sockfd].write()) {
			LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
		
			if (timer) {
				adjust_timer(timer);
			}
		}
		else {
			deal_timer(timer, sockfd);
		}
	}
}

void WebServer::eventLoop() {
	bool timeout = false;
	bool stop_server = false;

	while (!stop_server) {
		int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if (number < 0 && errno != EINTR) {
			LOG_ERROR("%s","epoll_wait failure");
			printf("number < 0\n");
			break;
		}
		for (int i = 0; i < number; i++) {
			int sockfd = events[i].data.fd;

			//处理新的客户端连接
			if (sockfd == m_listenfd) {
				bool flag = dealclientdata();
				if (false == flag) {
					continue;
				}
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				//EPOLLRDHUP 客户端已经关闭了写操作，或TCP连接被客户端关闭
				//内核不能再往内核缓冲区增加内容，但已经再缓冲区的内容，用户态能够读取到

				//EPOLLHUP	表示读操作写操作都关闭

				//EPOLLERR	客户端 网络错误、资源不足或异常关闭

				//客户端错误断开或主动关闭连接，移除对应的定时器
				util_timer* timer = users_timer[sockfd].timer;
				deal_timer(timer, sockfd);
			}

			//处理信号
			else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
				bool flag = dealwithsignal(timeout,stop_server);
				if (false == flag) {
					LOG_ERROR("%s","dealclientdata failure");
				}
			}
			else if (events[i].events & EPOLLIN) {
				dealwithread(sockfd);
			}
			else if (events[i].events & EPOLLOUT) {
				dealwithwrite(sockfd);
			}
		}
		if (timeout) {
			utils.timer_handler();

			LOG_INFO("%s","timer tick");
		
			timeout = false;
		}
	}
}