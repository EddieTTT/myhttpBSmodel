#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<dirent.h>
#include<sys/stat.h>
#include<ctype.h>
#include"epoll_server.h"

#define MAXSIZE 2000

void send_error(int cfd,int status,char*title,char*text)
{
    char buf[4096] ={0};
    sprintf(buf,"%s %d %s\r\n","HTTP/1.1",status,text);
    sprintf(buf+strlen(buf),"Content-Type:%s\r\n","text/html");
    sprintf(buf+strlen(buf),"Content-Length:%d\r\n",-1);
    sprintf(buf+strlen(buf),"Connection: close\r\n");
    send(cfd,buf,strlen(buf),0);
    send(cfd,"\r\n",2,0);
    memset(buf,0,sizeof(buf));


    sprintf(buf,"<html><head><title>%d %s</title></head>\n",status,title);
    sprintf(buf+strlen(buf),"<body bgcolor=\"#cc99cc\"> <h2 align=\"center\">%d %s</h4>\n",status,title);
    sprintf(buf+strlen(buf),"%s\n",text);
    sprintf(buf+strlen(buf),"<hr>\n</body>\n</html>\n");
    send(cfd,buf,strlen(buf),0);

    return;

}
//获取一行/r/n结尾的数据
int get_line(int sock,char*buf,int size)
{
    int i=0;
    char c ='\0';
    int n;
    while((i<size-1)&&(c!='\n'))
    {
        n = recv(sock,&c,1,0);
        if(n>0)
        {
            if(c=='\r')
            {
                n = recv(sock,&c,1,MSG_PEEK);//MSG_PEEK模拟读一次 拷贝读一次
                if((n>0)&&(c=='\n'))
                {
                    recv(sock,&c,1,0);//实际读
                }
                else{
                    c='\n';
                }
            }
            buf[i]=c;
            i++;
        }
        else{
            c='\n';
        }
    }
    buf[i] = '\0';
    return i;
}
//读数据
void do_read(int cfd,int epfd)
{
    //读取一行http协议，拆分 获取get 方法 文件 协议号
        char method[16],path[256],protocol[16];
    char line[1024]={0};
    int len = get_line(cfd,line,sizeof(line));//读取http请求协议的首行 GET/HELLO.c HTTP/1.1
    if(len == 0)
    {
        printf("服务器检测到客户端关闭\n");
        //关闭套接字，cfd从epoll树上移除
        disconnect(cfd,epfd);
    }else
    {
        sscanf(line,"%[^ ] %[^ ] %[^ ]",method,path,protocol);
        printf("method:%s,path:%s,protocol:%s\n",method,path,protocol);
        printf("==============请求头==============\n");
        printf("请求行数据:%s",line);
        while(1)
        {
            char buf [1024] = {0};
            len = get_line(cfd,buf,sizeof(buf));
            if(buf[0] == '\n')//'\n' -1
            {
                break;
            }
            else if(len == -1)
            {
                break;
            }

        }
           printf("==============The End==============\n");
    }
    //判断get 请求
    if(strncasecmp("get",line,3)==0)
    {
        //处理Http请求
        http_request(line,cfd);
        //关闭套接字 cfd从epoll上Del
        disconnect(cfd,epfd);
    }

}

int init_listen_fd(int port,int epfd)
{
    //创建监听套接字lfd
    int lfd = socket(AF_INET,SOCK_STREAM,0);
    if(lfd == -1)
    {
        perror("socket error");
        exit(1);
    }
    //创建服务器地址结构IP+ PORT
    struct sockaddr_in srv_addr;
    bzero(&srv_addr,sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //设置端口复用
    int opt = 1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    //给lfd绑定地址结构
    int ret = bind(lfd,(struct sockaddr*)&srv_addr,sizeof(srv_addr));
    if(ret == -1)
    {
        perror("bind error");
        exit(1);
    }
    //设置监听上限
    ret = listen(lfd,128);
    if(ret == -1)
    {
        perror("listen error");
        exit(1);
    }
    //将lfd 挂到epoll树上
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    ret = epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
    if(ret == -1)
    {
        perror("epoll_ctl add lfd error");
        exit(1);
    }

    return lfd;

    }
//接受新连接处理
void do_accept(int lfd,int epfd)
{
    struct sockaddr_in clt_addr;
    socklen_t clt_addr_len = sizeof(clt_addr);
    int cfd = accept(lfd,(struct sockaddr*)&clt_addr,&clt_addr_len);
    if(cfd == -1)
    {
        perror("accept error");
        exit(1);
    }
    //打印 客户端信息
    char client_ip[64]={0};
    printf("New client IP:%s , Port:%d , cfd =%d\n",inet_ntop(AF_INET,&clt_addr,client_ip,sizeof(client_ip)),ntohs(clt_addr.sin_port),cfd);
    //设置cfd为 非阻塞
    int flag = fcntl(cfd,F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd,F_SETFL,flag);
    //将cfd挂到树上
    struct epoll_event ev;
    ev.data.fd = cfd;
    //边沿非阻塞模式
    ev.events = EPOLLIN|EPOLLET;

    int ret = epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);
    if(ret == -1)
    {
        perror("epoll_ctl add cfd error");
        exit(1);
    }
}

void epoll_run(int port)
{
    int i=0;
    struct epoll_event all_events[MAXSIZE];
    //创建一个epoll监听树
    int epfd = epoll_create(MAXSIZE);
    if(epfd == -1)
    {
        perror("epoll_create error");
        exit(1);
    }
    //创建lfd，并添加至监听树
    int lfd = init_listen_fd(port,epfd);
    while(1)
    {
        //监听节点对应事件
        int ret = epoll_wait(epfd,all_events,MAXSIZE,-1);
        if(ret == -1)
        {
            perror("epoll_wait error");
            exit(1);
        }
        for(i=0;i<ret;i++)
        {
            struct epoll_event *pev = &all_events[i];
            //不是读事件 读事件:lfd连接事件、cfd的数据读写事件
            if(!(pev->events&EPOLLIN))
            {
                continue;
            }
            //lfd连接事件
            if(pev->data.fd == lfd)
            {
                do_accept(lfd,epfd);
            }else{
            do_read(pev->data.fd,epfd);
            }
        }
    }
}
//断开连接的函数
void disconnect(int cfd,int epfd)
{
    int ret = epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,NULL);
    if(ret == -1)
    {
        perror("epoll_ctl del error");
        exit(1);
    }
    close(cfd);
}


//http请求处理 判断文件是否存在，再回发
void http_request(const char*request,int cfd)
{
    //拆分http请求行
    char method[12],path[1024],protocol[12];
    sscanf(request,"%[^ ] %[^ ] %[^ ]",method,path,protocol);
    printf("method = %s,path = %s ,protocol = %s\n",method,path,protocol);

    //转码 将不能识别的中文乱码 -》中文
    //解码 %23 %34 %5f
    decode_str(path,path);

    char*file = path+1;//去掉path中的/ 获取访问的文件名

    //如果没有制定访问的资源，默认现实资源目录中的内容
    if(strcmp(path,"/")==0)
    {
        file = "./";
    }
    //获取文件属性
    struct stat sbuf;
    int ret = stat(file,&sbuf);
    if(ret == -1)
    {
        send_error(cfd,404,"Not Found","No such file or direntry");
        return;
    }
    //判断是目录还是文件
    if(S_ISDIR(sbuf.st_mode))
    {
        //目录
        send_respond_head(cfd,200,"OK",get_file_type(".html"),-1);
        send_dir(cfd,file);
    }
    else if(S_ISREG(sbuf.st_mode))
    {
        //发送消息报头
        send_respond_head(cfd,200,"OK",get_file_type(file),sbuf.st_size);
        //发送文件
        send_file(cfd,file);
    }

}

//发送目录内容
void send_dir(int cfd,const char*dirname)
{
    int i,ret;

    //拼接一个 html页面<table></table>
    char buf[4096]={0};
    sprintf(buf,"<html><head><title>目录名：%s</title></head>",dirname);
    sprintf(buf+strlen(buf),"<body><h1>当前目录 :%s</h1></table>",dirname);
    char enstr[1024]={0};
    char path[1024]={0};
    //目录二级指针
    struct dirent**ptr;
    int num = scandir(dirname,&ptr,NULL,alphasort);
    //遍历
    for(i=0;i<num;++i)
    {
        char*name = ptr[i]->d_name;
        //拼接文件的完整路径
        sprintf(path,"%s/%s",dirname,name);
        printf("path = %s ============\n",path);
        struct stat sbuf;
        stat(path,&sbuf);

        //中文编码
        encode_str(enstr,sizeof(enstr),name);

        //如果是文件
        if(S_ISREG(sbuf.st_mode))
        {
            sprintf(buf+strlen(buf),
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr,name,(long)sbuf.st_size);
        }else if(S_ISDIR(sbuf.st_mode))
        {
            sprintf(buf+strlen(buf),
                    "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
                    enstr,name,(long)sbuf.st_size);
        }
        ret = send(cfd,buf,strlen(buf),0);
        if(ret == -1)
        {
            if(errno == EAGAIN)
            {
                perror("send error");
                continue;
            }
            else if(errno == EINTR)
            {
                perror("send error");
                continue;
            }
            else
            {
                perror("send error");
                exit(1);
            }
        }
        memset(buf,0,sizeof(buf));
    }
    sprintf(buf +strlen(buf),"</table></body></html>");
    send(cfd,buf,strlen(buf),0);
    printf("dir message send OK!!!\n");
}
/*
Http1.1 200 OK
2. Server: xhttpd
Content-Type：text/plain; charset=iso-8859-1
3. Date: Fri, 18 Jul 2014 14:34:26 GMT
5. Content-Length: 32  （ 要么不写 或者 传-1， 要写务必精确 ！ ）
6. Content-Language: zh-CN
7. Last-Modified: Fri, 18 Jul 2014 08:36:36 GMT
8. Connection: close
\r\n
*/
//发送响应头
void send_respond_head(int cfd,int no,const char*desp,const char*type,long len)
{
    char buf[1024] = {0};
    //状态行
    sprintf(buf,"http/1.1 %d %s\r\n",no,desp);
    send(cfd,buf,strlen(buf),0);
    //消息报头
    sprintf(buf,"Content-Type:%s\r\n",type);
    sprintf(buf+strlen(buf),"Content-Length:%ld\r\n",len);
    send(cfd,buf,strlen(buf),0);
    //空行
    send(cfd,"\r\n",2,0);
}

//发送文件
void send_file(int cfd,const char*filename)
{
    //打开文件
    int fd = open(filename,O_RDONLY);
    if(fd==-1)
    {
        send_error(cfd,404,"Not Found","No suck file or direntry");
        exit(1);
    }
    //循环读文件
    char buf[4096]={0};
    int len =0,ret = 0;
    while((len = read(fd,buf,sizeof(buf)))>0)
    {
        ret = send(cfd,buf,len,0);
        if(ret == -1)
        {
            if(errno == EAGAIN)
            {
                perror("send error");
                continue;
            }else if(errno == EINTR)
            {
                perror("send error");
                continue;
            }else{
            perror("send error");
            exit(1);
            }
        }
    }
    if(len == -1)
    {
        perror("read file error");
        exit(1);
    }
    close(fd);
}

//16进制转换为十进制
int hexit(char c)
{
    if(c>='0'&&c<='9')
        return c-'0';
    if(c>='a'&&c<='f')
        return c-'a'+10;
    if(c>='A'&&c<='F')
    return c-'A'+10;

    return 0;
}

//unicode解码
void encode_str(char*to,int tosize,const char*from)
{
    int tolen;
    for(tolen =0;*from!='\0'&&tolen+4<tosize;++from)
    {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0){
            *to = *from;
            ++to;
            ++tolen;
        }else{
        sprintf(to,"%%%02x",(int)*from&0xff);
        to+=3;
        tolen+=3;
        }

    }
    *to = '\0';
}

//unicode 编码
void decode_str(char*to,char*from)
{
    for(;*from!='\0';++to,++from)
    {

        if(from[0]=='%' && isxdigit(from[1])&&isxdigit(from[2]))
        {
            *to = hexit(from[1])*16 + hexit(from[2]);
            from +=2;
        }
        else{
            *to = *from;
        }
    }
    *to = '\0';
}

//通过文件名获取文件的类型
const char* get_file_type(const char* name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}
