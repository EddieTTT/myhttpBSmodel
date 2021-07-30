#include <stdio.h>
#include <stdlib.h>
#include<unistd.h>
#include"epoll_server.h"


int main(int argc,char*argv[])
{
    //命令行参数获取 端口和server提供的目录
    if(argc<3)
    {
        printf("eg:./a.out port path\n");
        exit(1);
    }
    //获取用户输入的端口
    int port = atoi(argv[1]);
    //改变进程工作目录
    int ret = chdir(argv[2]);
    if(ret!=0)//ret == -1
    {
        perror("chdir error");
        exit(1);
    }
    //启动epoll监听
    epoll_run(port);

    return 0;
}
