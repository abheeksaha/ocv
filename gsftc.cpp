/**********************************************************************
 * Simple framing protocol over TCP for RTP packets.
 * Trying to solve the corruption problem
 *
 * *******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include "gsftc.hpp"

#include <unistd.h>

int dcvFtcDebug ;
dcv_ftc_t * dcvFtConnInit(char *inaddress, unsigned short inport, char *outaddress, unsigned short outport)
{
	dcv_ftc_t *D = malloc(sizeof(dcv_ftc_t)) ;
	D->obuf  = calloc(MAXBYTESINARRAY,sizeof(char)) ;
	D->spaceleft = MAXBYTESINARRAY ;
	D->totalbytes = MAXBYTESINARRAY ;
	D->pbuf = D->obuf ;
	D->insock = D->outsock = D->servsock = -1 ;
	D->sequence = 231131;
	D->seqExpected = -1 ;
	D->servsock = -1 ;
	D->outsock = -1 ;
	D->sentbytes = 0;
	D->recvbytes = 0;
	g_mutex_init(&D->lock) ;
	g_mutex_init(&D->sendlock) ;
	if (inaddress != NULL) {
		g_print("inport:%d\n",inport) ;
		if ( (D->servsock = socket(AF_INET,SOCK_STREAM,0)) < 0) {
			g_print("Socket creation failed!:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
		/** Create the server address **/
		memset((void *)&D->serv_addr, sizeof(sockaddr_in),0);
		D->serv_addr.sin_family = AF_INET;
		D->serv_addr.sin_addr.s_addr = INADDR_ANY ;
//		inet_aton(inaddress,&D->serv_addr.sin_addr) ;
		D->serv_addr.sin_port = htons(inport);
		if (bind(D->servsock, (struct sockaddr *) &D->serv_addr, sizeof(D->serv_addr)) < 0) {
			g_print("Bind error:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
	}
	if (outaddress != NULL) {
		g_print("Outaddress:%s outport:%d\n",outaddress,outport) ;
		if ( (D->outsock = socket(AF_INET,SOCK_STREAM,0)) < 0) {
			g_print("Outbound Socket creation failed!:%s\n",strerror(errno)) ;
			goto RETURNNULL ;
		}
		int one =1 ;
		setsockopt(D->outsock, IPPROTO_TCP, TCP_NODELAY,&one,sizeof(one)) ;
		memset((void *)&D->dstaddr, sizeof(sockaddr_in),0);
		D->dstaddr.sin_family = AF_INET;
		inet_aton(outaddress,&D->dstaddr.sin_addr) ;
		D->dstaddr.sin_port = htons(outport);
	}

	return D ;
RETURNNULL:
	free(D->obuf) ;
	free(D) ;
	return NULL ;
}

gboolean dcvFtConnStart(dcv_ftc_t *D)
{
	g_print("Trying to start connections!\n") ;
	if (D->outsock != -1) {
		while(1)
		{
			if (connect(D->outsock,(struct sockaddr *) &D->dstaddr,sizeof(struct sockaddr)) < 0) {
				g_print("ERROR connecting:%s\n",strerror(errno));
				g_print("waiting for connection ....\n");
				sleep(5);
				//return FALSE ;
			}
			else {
				g_print("Connected to server:%s:%u\n",inet_ntoa(D->dstaddr.sin_addr),D->dstaddr.sin_port) ;
				break;
			}
		}
	}
	if (D->servsock != -1) {
		g_print("Opening server connection: waiting for clients.....\n") ;
		if (listen(D->servsock,5) == -1) {
			g_print("Listen error:%s\n",strerror(errno)) ;
			return FALSE ;
		}
		unsigned int clilen = sizeof(D->cli_addr) ;
		if ((D->insock = accept(D->servsock,(sockaddr *)&D->cli_addr,&clilen)) < 0)
		{
			g_print("Connection accept failure:%s\n",strerror(errno)) ;
			return FALSE ;
		}
		else {
			g_print("Received connection request from ") ;
			g_print("%s:%u\n",inet_ntoa(D->cli_addr.sin_addr), ntohs(D->cli_addr.sin_port)) ;
			return TRUE ;
		}
	}
}

void dcvFtConnClose(dcv_ftc_t *D)
{
#if 0
	if (D->outsock != -1) close(D->outsock) ;
	if (D->insock != -1) close(D->insock) ;
	if (D->servsock != -1) close(D->servsock) ;
#endif
	g_print("Conn close: sent=%d\n",D->sentbytes) ;
}

gchar *eosToString(gboolean eos)
{
	static char *t = "true" ;
	static char *f = "false";

	if (eos == true) return t;
	else return f;
}
void dcvFtConnStatus(dcv_ftc_t *D,gboolean eosUsrc, gboolean eosUsink, gboolean eosSentUsink)
{
	g_print("Totalbytes=%d spaceleft=%d eos: [out:%s in:%s sent:%s]\n",
			D->totalbytes, D->spaceleft,
			eosToString(eosUsink), eosToString(eosUsrc),
			eosToString(eosSentUsink)) ;

}


#define NETBUFSIZE 8192
#define max(a,b) ((a) > (b) ? (a):(b))
#define min(a,b) ((a) < (b) ? (a):(b))

int donothing() {
	exit(1) ;
}

int dcvPushBuffered (GstAppSrc *slf, dcv_ftc_t *D)
{
	/** Clear out the buffer, make space **/
	GstSample *gsm ; 
	GstCaps *gcaps ;
	unsigned int *pfh ; 
	char *pfc ;
	int bsize;
	struct timeval tv ;

	pfh = (unsigned int *)D->obuf ;
	guint64 maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
	guint64 avlbytes = dcvBufferedBytes(D) ;
	if (maxbytes == 0) return maxbytes;
	if (avlbytes < SIZEOFFRAMEHDR) { 
		if (dcvFtcDebug) g_print("dcvPushBuffered:Don't even have a frame header avlbytes=%d\n",avlbytes) ; 
		return -1 ; 
	}
	bsize = pfh[SZOFFSET] ;
	if (dcvFtcDebug & 0x03) 
		g_print("dcvPushBuffered:Bsize=%d Time=%d.%d sequence=%d maxbytes=%d availbytes=%d\n",
				pfh[SZOFFSET],pfh[TMOFFSET]>>16,pfh[TMOFFSET] & 0x00ffff, pfh[SEQOFFSET], maxbytes, avlbytes) ;
	if ( bsize   > maxbytes) { 
		if (dcvFtcDebug & 0x03) 
			g_print("dcvPushBuffered:Insufficient space in %s:needed=%d avail=%d\n", GST_ELEMENT_NAME(GST_ELEMENT_CAST(slf)), bsize, maxbytes) ; 
		return 0; 
	}
	if ( bsize >  (D->totalbytes - D->spaceleft)) { 
		if (dcvFtcDebug & 0x03)
			g_print("dcvPushBuffered:buffer size (%d) too large for usrc space avail (%d)\n", bsize,D->totalbytes - D->spaceleft) ; 
		return -2; 
	}
	if (pfh[UWOFFSET] != uw) donothing() ;
	if (D->seqExpected != -1) {
		g_assert(pfh[SEQOFFSET] == D->seqExpected) ;
		D->seqExpected++ ;
	}
	else 
		D->seqExpected = pfh[SEQOFFSET]+1 ;
	tv.tv_usec = pfh[TMOFFSET] & 0xfffff ;
	tv.tv_sec = pfh[TMOFFSET] >> 20 ;
	if (bsize > D->totalbytes)
	{
		g_assert(bsize < D->totalbytes) ;
	}
	GstBuffer * gb = gst_buffer_new_allocate(NULL,bsize,NULL) ;
	if (gb == NULL ) {
		g_print("Ran out of buffers in the pool!\n") ; 
		g_assert(gb) ;
	}
	if (dcvFtcDebug & 0x03) 
		g_print("dcvPushBuffered:Constructed buffer\n") ;
	if ( bsize   > maxbytes) { return 0; }
	GstMemory *bmem;
	GstMapInfo bmap;
	bmem = gst_buffer_get_all_memory(gb) ;
	if (gst_memory_map(bmem, &bmap, GST_MAP_WRITE) != TRUE) { GST_ERROR("Couldn't map memory in send buffer for reading\n") ; }
	pfc = (char *)&pfh[4] ;
	memcpy(bmap.data,pfc,bsize) ;
	if (D->pclk != NULL) GST_BUFFER_PTS(gb) = gst_clock_get_time(D->pclk) + 50*GST_MSECOND;
#if 0
	gcaps = gst_caps_new_simple("application/x-rtp",NULL) ;
	gsm = gst_sample_new(gb,gcaps,NULL,NULL) ;
	GstFlowReturn ret = gst_app_src_push_sample(slf,gsm) ;
	gst_caps_unref(gcaps) ;
	gst_sample_unref(gsm) ;
#else
	GstFlowReturn ret = gst_app_src_push_buffer(slf,gb) ;
#endif
	if (ret == GST_FLOW_ERROR) {
		g_print("Couldn't push buffer to app src\n") ; 
		g_assert(ret != GST_FLOW_ERROR) ;
	}
	maxbytes -= bsize;
	D->spaceleft += SIZEOFFRAMEHDR+bsize;
	if (D->totalbytes >= D->spaceleft)
		memmove(D->obuf,&D->obuf[SIZEOFFRAMEHDR+bsize],D->totalbytes - D->spaceleft) ;
	else
		memset(D->obuf,0,D->totalbytes) ;
	D->pbuf = &D->obuf[D->totalbytes - D->spaceleft] ;
	if(dcvFtcDebug & 0x03) g_print("dcvPushBuffered:Reclaimed %d bytes, spaceleft=%d\n",SIZEOFFRAMEHDR+bsize,D->spaceleft) ;
	return (bsize) ;
}
int dcvPullBytesFromNet(GstAppSrc *slf, dcv_ftc_t *D, gboolean *pfinished)
{
	int nbytes,tbytes=0,tpbytes=0 ;
	gboolean forceflush=TRUE ;
	int flags = MSG_DONTWAIT ;
	if (dcvIsDataBuffered(D)) flags = MSG_DONTWAIT ;
	guint64 maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
	if (dcvFtcDebug & 0x03) g_print("dcvPullBytesFromNet: Starting with maxbytes=%u, space avail=%u pfinished=%d\n",maxbytes,D->spaceleft,*pfinished) ;
	if (maxbytes > 0 || (D->spaceleft > 0 && *pfinished == FALSE)) {
		while ( D->spaceleft > 0 && *pfinished == FALSE) {
			nbytes = recv(D->insock,D->pbuf,D->spaceleft,flags) ;
		/** See how much space there is in the array **/
			if (nbytes == 0) {
				if (dcvFtcDebug) g_print("Connection closed!\n") ;
				forceflush = TRUE ;
				*pfinished = TRUE ;
				g_print("Connection closed: total recieved bytes = %d\n",D->recvbytes) ;
			}
			else if (nbytes != -1){
				tbytes += nbytes ;
				D->pbuf += nbytes;
				D->spaceleft -= nbytes ;
				D->recvbytes += nbytes ;
				if (dcvFtcDebug & 0x03) 
					g_print("dcvPullBytesFromNet: Received %d bytes, spaceleft=%d\n",nbytes,D->spaceleft) ;
				if (maxbytes > 0) break ;
			}
			else {
				usleep(1000) ;
				break ;
			}
		}
		if (maxbytes > 0)
		{
			if (dcvFtcDebug & 0x03) 
				g_print("dcvPullBytesFromNet: Trying to push %d bytes\n",tbytes) ;
			int pushedbytes = dcvPushBuffered(slf,D) ;
			if (dcvFtcDebug & 0x03) 
				g_print("dcvPullBytesFromNet: Pushed %d bytes\n",pushedbytes) ;
			if (pushedbytes == 0) {
				return (tbytes > 0 ? -tbytes:-1) ;
			}
			else if (pushedbytes == -1) {
			/** More data required **/
				maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
				if (*pfinished == TRUE) return tbytes ;
	//			else continue ;
			}
			else if (pushedbytes == -2) {
				usleep(1000) ;
				return tpbytes;
			}
			else
			{
				maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
				tpbytes += pushedbytes ;
				return tpbytes ;
			}
		}
	}
	return tbytes ;
}

GstFlowReturn dcvAppSinkNewPreroll(GstAppSink *slf, gpointer d)
{
	GstCaps * dbt = NULL ;
	gchar *gs;
	if (dcvFtcDebug) g_print("New Preroll in %s:\n",GST_ELEMENT_NAME(slf)) ;
	if (d == NULL) 
	{
		return GST_FLOW_OK ;
	}
	GstSample *gsm ;
	if ((gsm = gst_app_sink_pull_preroll(slf)) != NULL)
	{
		if (dcvFtcDebug) g_print("New Preroll in %s: --",GST_ELEMENT_NAME(slf)) ;
		dbt = gst_sample_get_caps(gsm) ;
		gs = gst_caps_to_string(dbt);
		if (dcvFtcDebug) g_print(" caps %s \n",gs) ;
		g_free(gs) ;
		gst_sample_unref(gsm) ;
		gst_caps_unref(dbt) ;
		return GST_FLOW_OK;
	}
	else
		return GST_FLOW_ERROR;
}

GstFlowReturn dcvAppSinkNewSample(GstAppSink *slf, gpointer d)
{
	GstFlowReturn retval = GST_FLOW_ERROR ;
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	static int samples=0;
	if (d == NULL) {
		if (dcvFtcDebug & 0x07) g_print("New Sample in %s:\n",GST_ELEMENT_NAME(slf)) ;
		return GST_FLOW_OK;
	}

	g_mutex_lock(&D->lock) ;
	GstSample *gsm ;
	samples++ ;
	if (dcvFtcDebug & 0x03) g_print("New sample in %s: totalbytes:%d spaceleft=%d samples=%d\n",GST_ELEMENT_NAME(slf), D->totalbytes, D->spaceleft,samples) ;
	if ((gsm = gst_app_sink_pull_sample(slf)) != NULL) {
		GstBufferList *glb = gst_sample_get_buffer_list(gsm) ;
		if (glb != NULL) {
			if (gst_buffer_list_foreach(glb,dcvSendBuffer,D) == TRUE)
				retval= GST_FLOW_OK;
			else 
				retval= GST_FLOW_ERROR;
		}
		else 
		{
			GstBuffer *gb = gst_sample_get_buffer(gsm) ;
			if (dcvSendBuffer(gb,(gpointer)D) == TRUE)
				retval= GST_FLOW_OK;
			else
				retval= GST_FLOW_ERROR ;
		}
	}
	else
		retval= GST_FLOW_ERROR ;
	g_mutex_unlock(&D->lock) ;
	return retval;
}
void dcvEosRcvd(GstAppSink *slf, gpointer d)
{
	g_print("dcvEosRcvd:Eos on %s\n",GST_ELEMENT_NAME(GST_ELEMENT_CAST(slf))) ;
	if (d) {
	gboolean *eos = (gboolean *)d ;
	*eos = TRUE ;
	}
}

gboolean dcvSendBuffer (GstBuffer *b, gpointer d)
{
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	static char framehead[SIZEOFFRAMEHDR+1024000] ;
	unsigned int *pfh = (unsigned int *)framehead ;
	struct timeval tv;
	struct timezone tz;
	unsigned int ptm;
	char *pframehead = &framehead[SIZEOFFRAMEHDR];
	GstMemory *bmem;
	GstMapInfo bmap;
	g_mutex_lock(&D->sendlock) ;
	bmem = gst_buffer_get_all_memory(b) ;
	if (gst_memory_map(bmem, &bmap, GST_MAP_READ) != TRUE) { g_print("Couldn't map memory in send buffer\n") ; }
	gboolean rval = TRUE ;
	pfh[UWOFFSET] = uw ; /** First 32 bits **/
	pfh[SEQOFFSET] = D->sequence++ ; /** Next 32 bits **/
	gettimeofday(&tv,&tz) ;
	pfh[TMOFFSET] = tv.tv_usec & 0xfffff ;
	pfh[TMOFFSET] |= (tv.tv_sec & 0xfff) << 20 ;
	pfh[SZOFFSET] = (unsigned int)bmap.size;
	if (dcvFtcDebug & 0x03) 
		g_print("dcvSendBuffer:Bsize=%d Time=%d.%d sequence=%d\n",pfh[SZOFFSET],pfh[TMOFFSET]>>16,pfh[TMOFFSET] & 0x00ffff, pfh[SEQOFFSET]) ;

	memcpy(pframehead,bmap.data,bmap.size) ;
	gst_memory_unmap(bmem,&bmap) ;
	{
		gint tosend = SIZEOFFRAMEHDR+bmap.size;
		gint tbsent = 0 ;
		pframehead = framehead ;
		if (dcvFtcDebug) 
			g_print("dcvSendBuffer: Seq %u: sending %u bytes at %u.%u, rval=%d\n",D->sequence-1, tosend, tv.tv_sec,tv.tv_usec,rval) ;
		do {
			tbsent = send(D->outsock,pframehead,tosend,0) ;
			if (tbsent > 0) {
				tosend -= tbsent ;
				pframehead = &pframehead[tbsent] ;
				D->sentbytes += tbsent ;
			}
			else {
				rval = FALSE ;
				break ;
			}
			usleep(1000) ;
		} while (tosend > 0 && rval == TRUE) ;
		if (dcvFtcDebug) 
			g_print("dcvSendBuffer: Seq %u: sent %u bytes at %u.%u, rval=%d\n",D->sequence-1, D->sentbytes, tv.tv_sec,tv.tv_usec,rval) ;

		if (rval == FALSE) g_print("Couldn't send buffer ,%d bytes left\n",tosend) ;
	}
	g_mutex_unlock(&D->sendlock) ;
	return rval ;
}



int dcvBufferedBytes(dcv_ftc_t *P)
{
	return (P->totalbytes - P->spaceleft) ;
}

gboolean dcvIsDataBuffered(dcv_ftc_t *P)
{
	if (dcvBufferedBytes(P) > 0) return true ;
	else return false ;
}
