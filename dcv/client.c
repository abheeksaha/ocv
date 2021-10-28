#include <stdio.h> 
#include <stdlib.h> 
#include <netdb.h> 
#include <string.h> 
#include <unistd.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#define MAX 90000
#define PORT 50018 
#define SA struct sockaddr 
#include <getopt.h>
extern char *optarg ;
#include <errno.h>
extern int errno ;

#include "tcptrans.h"
void func(int sockfd, FILE *fin, int maxsize) 
{ 
	char buff[MAX]; 
	int n,hdrsize; 
	
	if (maxsize > MAX) maxsize = MAX ;
	while( (hdrsize = fread(buff,sizeof(char),maxsize,fin)) != NULL)
	{ 
		if ( (n=writeConfirmWithTimeout(sockfd,buff,hdrsize,500)) < 0) break ;
		printf("%d bytes sent\n",n) ;
	}
}

int main(int c, char **v) 
{ 
	int sockfd, connfd; 
	struct sockaddr_in servaddr, cli; 
	char ch ;
	int port = PORT ;
	FILE *fin = NULL ;
	while ((ch = getopt(c,v,"p:f:")) != -1) {
		switch(ch) {
			case 'p': port = atoi(optarg) ; break ;
			case 'f': {
				printf("Using input file:%s\n",optarg) ;
				if ((fin = fopen(optarg,"r")) == NULL) {
					fprintf(stderr,"File open error:%s\n",strerror(errno)) ;
					exit(1) ;
				}
				break ;
			}
		}
	}
	printf("Using port=%d\n",port) ;
	if (fin == NULL) {
		exit(3) ;
	}

	// socket create and varification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("socket creation failed...\n"); 
		exit(0); 
	} 
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr("192.168.16.205"); 
	servaddr.sin_port = htons(port); 

	// connect the client socket to server socket 
	if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) { 
		printf("connection with the server failed...\n"); 
		exit(0); 
	} 
	else
		printf("connected to the server..\n"); 

	// function for chat 
	func(sockfd,fin,51200); 

	// close the socket 
	close(sockfd); 
} 
