#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h>
#include <sys/socket.h> 
#define MAX 80 
#include <errno.h>
extern int errno ;
#include "tcptrans.h"


int getMsgWithTimeout(int sockfd, char * hdr, int hdrsize, int toutmsec)
{
	struct timeval tv ;
	int bytes ;
	tv.tv_sec = toutmsec/1000 ;
	tv.tv_usec = (toutmsec*1000)%1000000 ;
	if (tv.tv_sec > 0 || tv.tv_usec > 0) {
		if (setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,(char *)&tv,sizeof(tv)) != 0) {
			fprintf(stderr,"Couldn't set socket timeout:%s\n",strerror(errno)) ;
			return -1 ;
		}
	}
	bzero(hdr,hdrsize) ;
	bytes = read (sockfd, hdr, hdrsize) ;
	if (bytes == 0) {
		printf("Timeout\n") ;
		return 0 ;
	}
	else if (bytes  < 0) {
		fprintf(stderr,"Couldn't write to socket:%s\n",strerror(errno)) ;
		return -1 ;
	}
	return bytes ;
}

int writeConfirmWithTimeout(int sockfd, char *msg, int mlen, int toutmsec)
{
	char hdr[2048] ;
	int hdrsize = sprintf(hdr,"Receive msg of size %d\n", mlen) ;
	if (write(sockfd, hdr,hdrsize) < hdrsize) {
		fprintf(stderr,"Couldn't write on socket:%s\n",strerror(errno)) ;
		return -1 ;
	}
	do {
		hdrsize = getMsgWithTimeout(sockfd,hdr,2048,500) ;
	} while (hdrsize == 0) ;
	if (hdrsize == -1) return -1 ;

	if (!strncmp(hdr,"OK",2)) {
		if (write(sockfd, msg, mlen) < mlen) {
			fprintf(stderr,"Couldn't write actual msg:%s\n",strerror(errno)) ;
			return -1 ;
		}
		printf("Written %d bytes to server\n",mlen) ;
	
		hdrsize = getMsgWithTimeout(sockfd,hdr,2048,500) ;
		
		if (!strncmp(hdr,"Exit",4)) {
			printf("Server initiated Exit...cannot transmit data\n"); 
			return -2; 
		}
		else if (!strncmp(hdr,"OK",2)) {
			return mlen ;
		}
		else
			return -1 ;
		
	}
	else 
		return -1 ;
}
int readConfirmWithTimeout(int sockfd, char **msg, int toutmsec)
{
	int tlen,mlen,nread ;
	char hdr[2048] ;
	int hdrsize = getMsgWithTimeout(sockfd,hdr,2048,500) ;
	if (hdrsize <= 0) return hdrsize ;

	if ( (nread = sscanf(hdr,"Receive msg of size %d\n",&tlen)) >= 1){ 
		*msg = calloc(tlen+1,sizeof(char)) ;
		if (*msg == NULL) {
			return -1 ;
		}
		bzero(*msg,tlen+1) ;

		if (send(sockfd,"OK",2,0) < 0) {
			fprintf(stderr,"Couldn't send ok message\n") ;
			return -1 ;
		}
		if ((mlen = getMsgWithTimeout(sockfd,*msg,tlen+1,500)) != tlen) {
			return -1 ;
		}
		printf("Received %d bytes from client\n",mlen) ;
		if (send(sockfd,"OK",2,0) < 0) {
			fprintf(stderr,"Couldn't send ok message\n") ;
			return -1 ;
		}
		return tlen ;
	}
	else {
		printf("Unknown message %s (nread=%d)\n",hdr,nread) ;
		return -1 ;
	}
}
