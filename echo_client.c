    #include <stdio.h>       // printf、perror
    #include <stdlib.h>      // EXIT_SUCCESS、EXIT_FAILURE
    #include <string.h>      // memset、strlen
    #include <unistd.h>      // close

    #include <sys/socket.h>  // socket、bind、listen、accept、recv、send
    #include <netinet/in.h>  // struct sockaddr_in、htons
    #include <arpa/inet.h>   // inet_pton：将 IP 字符串转换为网络地址

    int main()
    {   char buff[1024];
        ssize_t recv_size;
        int sock_fd;
        int  temp_ret;
        struct sockaddr_in addr={0};
        sock_fd=socket(AF_INET,SOCK_STREAM,0);
        if (sock_fd<0)
        {
            perror("socket");
            return EXIT_FAILURE;
        }
        //设置服务端地址
        addr.sin_family=AF_INET;
        addr.sin_port=htons(1502);
        temp_ret= inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);  
                if (temp_ret!=1)
                {
                    perror("inet_pton"); 
                    close(sock_fd);
                    return EXIT_FAILURE;
                
                }
       //链接
        temp_ret= connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
            if (temp_ret < 0)
                {
                    perror("connect");
                    close(sock_fd);
                    return EXIT_FAILURE;
                }
                const char msg[] = "hello";
                size_t len = sizeof(msg) - 1;  // 只发送 hello 这 5 个字节
                size_t sent = 0;
                 
                printf("连接服务端成功\n")   ;
                while (sent < len){
                ssize_t send_size = send(sock_fd, msg + sent, len - sent, MSG_NOSIGNAL);
                if (send_size <= 0)
                    {
                        fprintf(stderr, "发送失败\n");
                        close(sock_fd);
                        return EXIT_FAILURE;
                    }

                sent += send_size;

                }
          
            size_t total=0;

            while(1){  
                
                recv_size = recv(sock_fd, buff + total, 2 - total, 0);
                
                if (recv_size<0)
                {
                    perror("recv");
                    close(sock_fd);
                    return EXIT_FAILURE;
                    break;
                }
             else if (recv_size == 0)
                    {
                        fprintf(stderr, "服务端提前关闭连接，已收到 %zu/2 字节\n", total);
                        close(sock_fd);
                        return EXIT_FAILURE;
                    }
                else{
                total=total+recv_size;
                        if (total == 2)
                        {
                            buff[total] = '\0';
                            printf("收到 %zu 字节: %s\n", total, buff);
                            break;
                        }
                      }  
                        }
        close(sock_fd);
        return 0;
    }
    