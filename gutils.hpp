#ifndef GBUS_HPP
#define GBUS_HPP

#define validstate(k) ((k>=0) && (k<=4))
void tagbuffer(void *A, int isz, void *B, int osz) ;
gboolean matchbuffer(void *A, int isz, void *B, int osz) ;

void eosRcvd(GstAppSink *slf, gpointer D) ;
gboolean listenToBus(GstElement *pipeline, GstState * cstate, GstState *ostate, unsigned int tms) ;
void testStats(GstElement *tst) ;
void sink_newpreroll(GstAppSink *slf, gpointer d) ;
void sink_newsample(GstAppSink *slf, gpointer d) ;

#endif
