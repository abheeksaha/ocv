/**********************************************************************
 * Simple framing protocol over TCP for RTP packets.
 * Trying to solve the corruptionn problem
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
#include <arpa/inet.h>
#include <sys/time.h>

#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/app/app.h>
#include "gsftc.hpp"

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
	D->eosIn = FALSE ;
	D->eosOut = FALSE ;
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
		if (connect(D->outsock,(struct sockaddr *) &D->dstaddr,sizeof(struct sockaddr)) < 0) {
			g_print("ERROR connecting:%s\n",strerror(errno));
			return FALSE ;
		}
	else {
		g_print("Connected to server:%s:%u\n",inet_ntoa(D->dstaddr.sin_addr),D->dstaddr.sin_port) ;
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
	if (maxbytes == 0) return maxbytes;
	GST_LOG("dcvPushBuffered:Bsize=%d Time=%d.%d sequence=%d maxbytes=%d\n",pfh[SZOFFSET],pfh[TMOFFSET]>>16,pfh[TMOFFSET] & 0x00ffff, pfh[SEQOFFSET], maxbytes) ;
	if ( (bsize = pfh[SZOFFSET])  > maxbytes) { return 0; }
	if ( bsize >  (D->totalbytes - D->spaceleft)) { return -1; }
	if (pfh[UWOFFSET] != uw) donothing() ;
	if (D->seqExpected != -1) { 
		g_assert(pfh[SEQOFFSET] == D->seqExpected) ;
		D->seqExpected++ ;
	}
	else 
		D->seqExpected = pfh[SEQOFFSET]+1 ;
	tv.tv_usec = pfh[TMOFFSET] & 0xffff ;
	tv.tv_sec = pfh[TMOFFSET] >> 16 ;
	if (bsize > D->totalbytes)
	{
		g_assert(bsize < D->totalbytes) ;
	}
	GstBuffer * gb = gst_buffer_new_allocate(NULL,bsize,NULL) ;
	if (gb == NULL ) {
		GST_ERROR("Ran out of buffers in the pool!\n") ; 
		g_assert(gb) ;
	}
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
		GST_ERROR("Couldn't push buffer to app src\n") ; 
		g_assert(ret != GST_FLOW_ERROR) ;
	}
	maxbytes -= bsize;
	D->spaceleft += SIZEOFFRAMEHDR+bsize;
	if (D->totalbytes >= D->spaceleft)
		memmove(D->obuf,&D->obuf[SIZEOFFRAMEHDR+bsize],D->totalbytes - D->spaceleft) ;
	else
		memset(D->obuf,0,D->totalbytes) ;
	D->pbuf = &D->obuf[D->totalbytes - D->spaceleft] ;
	GST_LOG("dcvPushBuffered:Reclaimed %d bytes, spaceleft=%d\n",SIZEOFFRAMEHDR+bsize,D->spaceleft) ;
	return (bsize) ;
}
int dcvPushBytes(GstAppSrc *slf, dcv_ftc_t *D, gboolean *pfinished)
{
	int nbytes,tbytes=0 ;
	gboolean forceflush=TRUE ;
	guint64 maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
	GST_LOG("dcvPushBytes: Starting with maxbytes=%u, space avail=%u\n",maxbytes,D->spaceleft) ;
	while (maxbytes > 0 || (D->spaceleft > 0 && *pfinished == FALSE)) {
		while ( D->spaceleft > 0 && *pfinished == FALSE) {
			nbytes = recv(D->insock,D->pbuf,D->spaceleft,0) ;
		/** See how much space there is in the array **/
			if (nbytes == 0) {
				GST_LOG("Connection closed!\n") ;
				forceflush = TRUE ;
				*pfinished = TRUE ;
			}
			else {
				tbytes += nbytes ;
				D->pbuf += nbytes;
				D->spaceleft -= nbytes ;
				GST_LOG("dcvPushBytes: Received %d bytes, spaceleft=%d\n",nbytes,D->spaceleft) ;
			}
		}
		while (maxbytes > 0)
		{
			int pushedbytes = dcvPushBuffered(slf,D) ;
			GST_LOG("dcvPushBytes: Pushed %d bytes\n",pushedbytes) ;
			if (pushedbytes == 0) {
				return tbytes ;
			}
			else if (pushedbytes == -1) {
			/** More data required **/
				maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
				if (*pfinished == TRUE) return tbytes ;
				else break ;
			}
			else
			{
				maxbytes = gst_app_src_get_max_bytes(slf) - gst_app_src_get_current_level_bytes(slf) ;
				tbytes += pushedbytes ;
			}
		}
	}
	return tbytes ;
}

GstFlowReturn dcvAppSinkNewPreroll(GstAppSink *slf, gpointer d)
{
	GstCaps * dbt = NULL ;
	gchar *gs;
	if (d == NULL) 
	{
		GST_INFO("New Preroll in %s:\n",GST_ELEMENT_NAME(slf)) ;
		return GST_FLOW_OK ;
	}
	GstSample *gsm ;
	if ((gsm = gst_app_sink_pull_preroll(slf)) != NULL)
	{
		GST_INFO("New Preroll in %s: --",GST_ELEMENT_NAME(slf)) ;
		dbt = gst_sample_get_caps(gsm) ;
		gs = gst_caps_to_string(dbt);
		GST_INFO(" caps %s \n",gs) ;
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
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	if (d == NULL) {
		GST_INFO("New Sample in %s:\n",GST_ELEMENT_NAME(slf)) ;
		return GST_FLOW_OK;
	}

	GstSample *gsm ;
	if ((gsm = gst_app_sink_pull_sample(slf)) != NULL) {
		GstBufferList *glb = gst_sample_get_buffer_list(gsm) ;
		if (glb != NULL) {
			if (gst_buffer_list_foreach(glb,dcvSendBuffer,D) == TRUE)
				return GST_FLOW_OK;
			else 
				return GST_FLOW_ERROR;
		}
		else 
		{
			GstBuffer *gb = gst_sample_get_buffer(gsm) ;
			if (dcvSendBuffer(gb,(gpointer)D) == TRUE)
				return GST_FLOW_OK;
			else
				return GST_FLOW_ERROR ;
		}
	}
	else
		return GST_FLOW_ERROR ;
}

gboolean dcvSendBuffer (GstBuffer *b, gpointer d)
{
	dcv_ftc_t *D = (dcv_ftc_t *)d ;
	static char framehead[SIZEOFFRAMEHDR] ;
	unsigned int *pfh = (unsigned int *)framehead ;
	struct timeval tv;
	struct timezone tz;
	unsigned int ptm;
	GstMemory *bmem;
	GstMapInfo bmap;
	bmem = gst_buffer_get_all_memory(b) ;
	if (gst_memory_map(bmem, &bmap, GST_MAP_READ) != TRUE) { GST_ERROR("Couldn't map memory in send buffer\n") ; }
	gboolean rval = TRUE ;
	pfh[UWOFFSET] = uw ; /** First 32 bits **/
	pfh[SEQOFFSET] = D->sequence++ ; /** Next 32 bits **/
	gettimeofday(&tv,&tz) ;
	pfh[TMOFFSET] = tv.tv_usec & 0xffff ;
	pfh[TMOFFSET] |= (tv.tv_sec & 0xffff) << 16 ;
	pfh[SZOFFSET] = (unsigned int)bmap.size;
	GST_LOG("dcvSendBuffer:Bsize=%d Time=%d.%d sequence=%d\n",pfh[SZOFFSET],pfh[TMOFFSET]>>16,pfh[TMOFFSET] & 0x00ffff, pfh[SEQOFFSET]) ;
//	GST_LOG("Seq %u: sending %u bytes at %u.%u\n",D->sequence-1, *pfh, tv.tv_sec,tv.tv_usec) ;
	if ( send(D->outsock,framehead,SIZEOFFRAMEHDR, MSG_MORE) == -1) {
		return FALSE ;	
	}

	if ( (send(D->outsock, (void *)bmap.data,(int)bmap.size,0)) == -1) { 
		GST_ERROR("Couldn;t map buffer for sending\n") ;
		rval = FALSE ;
	}
	gst_memory_unmap(bmem,&bmap) ;
	return rval ;
}





