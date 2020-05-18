#ifndef DSFTC_HPP
#define DSFTC_HPP
#include <arpa/inet.h>
gboolean dcvSendBuffer (GstBuffer *b, gpointer d) ;

#define SIZEOFFRAMEHDR (4*sizeof(unsigned int)) 
#define MAXBYTESINARRAY  32768

typedef struct {
	char *obuf;
	char *pbuf ;
	int totalbytes;
	int spaceleft ;
	struct timeval lastSent;
	struct timeval lastRcvd;
	sockaddr_in cli_addr ;
	sockaddr_in serv_addr;
	sockaddr_in dstaddr ;
	int insock;
	int outsock;
	int servsock;
	int sequence ;
	int seqExpected;
	gboolean eosOut;
	gboolean eosIn;
}dcv_ftc_t ;

gboolean dcvFtConnStart(dcv_ftc_t *D) ;
dcv_ftc_t * dcvFtConnInit(char *inaddress, unsigned short inport, char *outaddress, unsigned short outport) ;

#define uw 0xf3487655

void dcvAppSrcFrameWrite(GstAppSrc *slf, guint length, gpointer d) ;
void dcvAppSrcFrameStop(GstAppSrc *slf, gpointer d ) ;
GstFlowReturn dcvAppSinkNewPreroll(GstAppSink *slf, gpointer d) ;
GstFlowReturn dcvAppSinkNewSample(GstAppSink *slf, gpointer d) ;
#endif
