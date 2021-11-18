#ifndef GPIPE_H
#define GPIPE_H

extern char outdesc[], procdesc[], indesc[] ;

gboolean configurePortsOutdesc(int portstart, char * destIp, GstElement *bin) ;
gboolean configurePortsIndesc(int portstart, char * srcIp, GstElement *bin) ;

#endif
