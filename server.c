/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
     int sockfd, newsockfd, portno;
     int outf;
     socklen_t clilen;
     char buffer[8192];
     struct sockaddr_in serv_addr, cli_addr;
     int n;
     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }
     outf = open("serv.dat",O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG) ;
     if (outf == -1) {
	     fprintf(stderr,"Couldn't create file for writing!:%s\n",strerror(errno)) ;
	     exit(1) ;
     }
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        error("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     listen(sockfd,5);
     printf("Listening completed...something has come\n") ;
     clilen = sizeof(cli_addr);
     newsockfd = accept(sockfd, 
                 (struct sockaddr *) &cli_addr, 
                 &clilen);
     if (newsockfd < 0) 
          error("ERROR on accept");
     else
	     printf("Gotten connection req from:%s:%d\n",
			     inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
     printf("Starting:Received....") ;
     while (1==1) {
	     bzero(buffer,8192);
	     n = read(newsockfd,buffer,8192);
	     if (n < 0) { printf("ERROR reading from socket"); break ; }
	     else if (n == 0) { printf("Connection closed\n") ; break ; }
	     write(outf,buffer,n);
	     printf("(%d bytes) ",n) ;
     }
     printf("\n") ;
   close(newsockfd);
     close(sockfd);
     close(outf) ;
    return 0; 
}
