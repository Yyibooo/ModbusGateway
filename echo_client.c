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
        printf("连接服务端成功\n");
                send(sock_fd,"hello",5,MSG_NOSIGNAL);
            while(1){  recv_size= recv(sock_fd,buff,sizeof(buff)-1,0);
                 if (recv_size<0)
        {
            perror("recv");
            close(sock_fd);
            return EXIT_FAILURE;
            break;
        }
        else if (recv_size == 0) {
            printf("客户端已关闭连接\n");
            break;
        } 
        else{

            buff[recv_size] = '\0';
           printf("服务端已关闭连接\n");
       break;
}  
        }
        close(sock_fd);
        return 0;
    }
    