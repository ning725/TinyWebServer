#include "config.h"


Config::Config() {

	//端口号默认为10000
	PORT = 10000;

	//日志写入方式，默认同步
	LOGWrite = 0;

	//触发组合模式，默认listenfd LT + connfd LT
	TRIGMode = 0;

	//listenfd触发模式，默认LT
	LISTENTrigmode = 0;

	//connfd触发模式，默认LT
	connfdTrigmode = 0;

	//优雅关闭连接，默认不使用
	OPT_LINGER = 0;

	//数据库连接池数量,默认8
	sql_num = 8;

	//线程池内的线程数量,默认8
	thread_num = 8;

	//关闭日志,默认不关闭
	close_log = 0;

	//并发模型,默认是proactor
	actor_model = 0;
}


//用户可以通过命令行参数来覆盖初始值
void Config::parse_arg(int argc, char *argv[]) {
	int opt;
	const char* str = "p:l:m:o:s:t:c:a:";
	while ((opt = getopt(argc, argv, str) != -1)) {
		switch (opt) {
			case 'p': {
				PORT = atoi(optarg);
				break;
			}
			case 'l': {
				LOGWrite = atoi(optarg);
				break;
			}
			case 'm': {
				TRIGMode = atoi(optarg);
				break;
			}
			case 'o': {
				OPT_LINGER = atoi(optarg);
				break;
			}
			case 's': {
				sql_num = atoi(optarg);
				break;
			}
			case 't': {
				thread_num = atoi(optarg);
				break;
			}
			case 'c': {
				close_log = atoi(optarg);
				break;
			}
			case 'a': {
				actor_model = atoi(optarg);
				break;
			}
			default:
				break;
		}
	}


}