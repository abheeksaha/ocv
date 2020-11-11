CC = gcc -g
CPP = g++ -g
LD = gcc -g
LDCPP = g++ -g
SRCHOME=/home/ggne0015/src/
OPENCV_DIR=$(SRCHOME)/opencv-4.1.1
GSTREAMER_DIR=$(SRCHOME)/gstreamer/
LIBDIR = /usr/local/lib
CFLAGS = -pthread -I/usr/local/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
	 -I$(GSTREAMER_DIR)/gstreamer-1.16.0/ -I$(GSTREAMER_DIR)gst-plugins-good-1.16.0/ -I/usr/local/include/opencv4/ -Idcv/ \
	 -I$(OPENCV_DIR)/modules/core/include -I$(OPENCV_DIR)/build/  -I$(OPENCV_DIR)/modules/videoio/include
CPPFLAGS = -fpermissive -DFOESTAGE -DRTP_MUX -DVP9PAY -DVP9PAYDS $(CFLAGS) 
LDFLAGS =  -L$(LIBDIR) -lgstreamer-1.0 -lgstapp-1.0 -lgobject-2.0 -lglib-2.0 
DEPFILES = rseq.h gutils.hpp gsftc.hpp dsopencv.hpp
OLIBDIR = /home/ggne0015/src/opencv-4.1.1/build/lib 
.cpp.o:
	$(CPP) $(CPPFLAGS) -fPIC -c $*.cpp
.c.o:
	$(CC) $(CFLAGS) -fPIC -c $*.c

all: gdyn grcvr gsproc dcv.so dcvrtpmux.so

test: client server

gdyn: gdyn.o gsftc.o gutils.o dsopencv.o $(DEPFILES)
	$(LDCPP) -o $@ gdyn.o gsftc.o gutils.o dsopencv.o  $(LDFLAGS) -lopencv_world -lm

grcvr : grcvr.o gutils.o gsftc.o dsopencv.o $(DEPFILES)
	$(LDCPP) -o $@ grcvr.o gutils.o gsftc.o dsopencv.o $(LDFLAGS) -lopencv_world  -lm

server: server.o
	$(LD) -o $@ server.o

foe: foeTest.o foe.o
	$(LD) -o $@ foeTest.o foe.o -lgsl -lgslcblas  -lm

LKDEMOOPT = lkdemo.o dsopencv.o foe.o gutils.o
LKDEMOORIG = lkdemoOrig.o
lkdemo: $(LKDEMOOPT)
	$(LDCPP) -o $@ $(LKDEMOOPT) $(LDFLAGS) -lopencv_world -lgsl -lgslcblas -lm

dcv.so: 
	cd dcv ; make dcv.so

dcvrtpmux.so:
	cd dcv ; make dcvrtpmux.so
