    #include <stdio.h>       // printf、perror
    #include <stdlib.h>      // EXIT_SUCCESS、EXIT_FAILURE
    #include <string.h>      // memset、strlen
    #include <unistd.h>      // close

    #include <sys/socket.h>  // socket、bind、listen、accept、recv、send
    #include <netinet/in.h>  // struct sockaddr_in、htons
    #include <arpa/inet.h>   // inet_pton：将 IP 字符串转换为网络地址
    int main(int argc, char const *argv[])
    {   int temp_ret;
        int cli_fd;
        int listen_fd;
        ssize_t recv_buff_size;
        char buff[1024];
        struct sockaddr_in addr={0};//创建结构体默认初始化
        
        
        listen_fd=socket(AF_INET,SOCK_STREAM,0);//socket返回 出来listenfd
        if (listen_fd<0)
        {
            perror("socket");
           
            return EXIT_FAILURE;
        }
        

    /* 2. 设置本机监听地址 */
        addr.sin_port=htons(1502);
        addr.sin_family=AF_INET;
    temp_ret= inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
        if (temp_ret!=1)
        {
            perror("inet_pton");
            close(listen_fd);
            return EXIT_FAILURE;
        }
        /* 3. 绑定地址 */
    temp_ret= bind(listen_fd,(struct sockaddr * )&addr,sizeof(addr));
        if (temp_ret<0)
        {
            perror("bind");
            close(listen_fd);
            return EXIT_FAILURE;
        }

    
        /* 4. 开始监听 */
    
        temp_ret= listen(listen_fd,128);

    if (temp_ret<0)
        {
            perror("listen");
            close(listen_fd);
            return EXIT_FAILURE;
        }
        printf("等待客户端连接：127.0.0.1:1502\n");
    /* 5. 接受一个连接，获得新的通信描述符 */
    cli_fd=   accept(listen_fd,NULL,NULL);
    if (cli_fd<0)
        {
            perror("accept");
            close(listen_fd);

            return EXIT_FAILURE;
        }
    printf("客户端(accept)已连接\n");
    /* 6. 接收一段数据，留一个字节放字符串结束符 */
    //recv(fd, buff, len, flags) = "从 fd 这条连接上，最多收 len 个字节，放进 buff，按 flags 的方式收"。
        recv_buff_size =recv(cli_fd,buff,sizeof(buff)-1,0);

    if (recv_buff_size<0)
        {
            perror("recv");
            close(listen_fd);
            close(cli_fd);
            return EXIT_FAILURE;
        }
        else if (recv_buff_size == 0) {
            printf("客户端已关闭连接\n");
        } 
        else{

    buff[recv_buff_size] = '\0';
            printf("OK 收到 %zd 字节: %s\n", recv_buff_size, buff);
            send(cli_fd, "OK", 2, MSG_NOSIGNAL);


        }

        close(cli_fd);
        close(listen_fd);

        return 0;
    }
