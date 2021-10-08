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

#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "tcptrans.h"

void help(const char *msg)
{
    fprintf(stderr,msg);
    exit(1);
}

#define MAXBUFFERSIZE 32768

int recvfunc(int sockfd, FILE *fout) 
{ 
	int n,hdr; 
	char *ptr ;
	
	while((n=readConfirmWithTimeout(sockfd,&ptr,500)) > 0)
	{ 
		printf("%d bytes received\n",n) ;
		if ( (hdr=fwrite(ptr,sizeof(char), n, fout)) < 0 ) {
			fprintf(stderr,"File writing error:%s\n",strerror(errno)) ;
			exit(1) ;
		}
		printf("Written %d bytes to output file\n",hdr) ;
		free(ptr) ;
	}
} 
int main(int c, char **v)
{
     int sockfd, newsockfd, portno;
     socklen_t clilen;
     struct sockaddr_in serv_addr, cli_addr;
     int n;
     char ch ;
     extern char *optarg ;
	FILE *fout ;
     portno = 50018;
     
     while ((ch = getopt(c,v,"r:o:h")) != -1) {
	     switch(ch) {
		     case 'h': {
			help("server -r <port on which to listen:default 50018> -o <output file>\n") ;
			exit(1) ;
		    }
			case 'r': portno = atoi(optarg) ; break ;
			case 'o': {
				printf("Using output file:%s\n",optarg) ;
				if ((fout = fopen(optarg,"w")) == NULL) {
					fprintf(stderr,"File open error:%s\n",strerror(errno)) ;
					exit(1) ;
				}
				break ;
			}
	     }
     }
	if (fout == NULL) exit(3) ;

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
	recvfunc(newsockfd,fout) ;
     close(newsockfd);
     close(sockfd);
}
