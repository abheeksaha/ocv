/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>

void help(const char *msg)
{
    fprintf(stderr,msg);
    exit(1);
}

#define MAXBUFFERSIZE 32768
int main(int c, char **v)
{
     int sockfd, newsockfd, portno;
     socklen_t clilen;
     char buffer[MAXBUFFERSIZE];
     struct sockaddr_in serv_addr, cli_addr;
     int n,bytesrcvd=0;
     char ch ;
     extern char *optarg ;
     portno = 50018;
     
     while ((ch = getopt(c,v,"r:h")) != -1) {
	     switch(ch) {
		     case 'h': {
			help("server -r <port on which to listen:default 50018>\n") ;
			exit(1) ;
		    }
			case 'r': portno = atoi(optarg) ; break ;
	     }
     }

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        help("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              help("ERROR on binding");
     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     newsockfd = accept(sockfd, 
                 (struct sockaddr *) &cli_addr, 
                 &clilen);
     if (newsockfd < 0) 
          help("ERROR on accept");
     bzero(buffer,MAXBUFFERSIZE);
     do {
     	n = recv(newsockfd,buffer,MAXBUFFERSIZE,MSG_DONTWAIT);
     	if (n < 0 ) {
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			usleep(1000) ;
		else  {
			printf("errno = %s, unrecoverable error!\n",strerror(errno)) ;
			break ;
		}
	}		
	else if (n > 0) {
		bytesrcvd += n ;
		printf("Received %d total %d\n",n,bytesrcvd) ;
	}
	else if (n == 0) {
		printf("Connection closed\n") ;
	}

     } while (n != 0) ;
	printf("Received %d total %d\n",n,bytesrcvd) ;
     close(newsockfd);
     close(sockfd);
     return 0; 
}
