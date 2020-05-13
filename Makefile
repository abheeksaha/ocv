CC = gcc -O2
CPP = g++ -O2
LD = gcc -O2
LDCPP = g++ -O2
SRCHOME=/home/ggne0015/src/
OPENCV_DIR=$(SRCHOME)/opencv-4.1.1
GSTREAMER_DIR=$(SRCHOME)/gstreamer/
LIBDIR = /usr/local/lib
CFLAGS = -pthread -I/usr/local/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I$(GSTREAMER_DIR)/gstreamer-1.16.0/ \
-I$(GSTREAMER_DIR)gst-plugins-good-1.16.0/ -I$(OPENCV_DIR)/modules/core/include -I$(OPENCV_DIR)/build/  -I$(OPENCV_DIR)/modules/videoio/include
CPPFLAGS = -fpermissive -DRTP_PIPE_BY_HAND -DRTP_MUX -DVP9PAY -DVP9PAYDS $(CFLAGS) 
LDFLAGS = -L/usr/local/lib -lgstreamer-1.0 -lgstapp-1.0 -lgobject-2.0 -lglib-2.0 
DEPFILES = rseq.hpp gutils.hpp

.cpp.o:
	$(CPP) $(CPPFLAGS) -c $*.cpp
.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: gdyn grcvr gsproc

gsproc: dsbase.o gutils.o $(DEPFILES)
	$(LDCPP) -o $@ gutils.o dsbase.o $(LDFLAGS) -lopencv_world -lm

gdyn: gdyn.o gutils.o $(DEPFILES)
	$(LDCPP) -o $@ gdyn.o gutils.o $(LDFLAGS) -lm

grcvr : grcvr.o gutils.o $(DEPFILES)
	$(LDCPP) -o $@ grcvr.o gutils.o $(LDFLAGS) -lm


lkdemo: lkdemo.o
	$(LD) -o $@ lkdemo.o -L$(LIBDIR) -lgstreamer-1.0 -lopencv_world -lm
