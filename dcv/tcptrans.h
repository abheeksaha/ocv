#ifndef TCPTRANS__H
#define TCPTRANS__H
int getMsgWithTimeout(int sockfd, char * hdr, int hdrsize, int toutmsec, int waitformore) ;
int writeConfirmWithTimeout(int sockfd, char *msg, int mlen, int toutmsec) ;
int readConfirmWithTimeout(int sockfd, char **msg, int toutmsec) ;
#endif
