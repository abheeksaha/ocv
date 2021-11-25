CC = g++ -g
CPP = g++ -g
LD = g++ -g
LDCPP = g++ -g
SRCHOME=/home/ggne0015/src/
OPENCV_DIR=$(SRCHOME)/opencv
GSTREAMER_DIR=$(SRCHOME)/gstreamer/
LIBDIR = /usr/local/lib
CFLAGS = -pthread -I/usr/local/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
	 -I$(GSTREAMER_DIR)/gstreamer-1.16.0/ -I$(GSTREAMER_DIR)gst-plugins-good-1.16.0/ -I/usr/local/include/opencv4/ \
	 -I$(OPENCV_DIR)/modules/core/include -I$(OPENCV_DIR)/build/  -I$(OPENCV_DIR)/modules/videoio/include -I$(OPENCV_DIR)/modules/core/include -I$(SRCHOME)/ocvStages/foe/ -I$(SRCHOME)/ocvStages/foe/dcv
CPPFLAGS = -fpermissive -DFOESTAGE -DRTP_MUX -DVP9PAY -DVP9PAYDS $(CFLAGS) -fPIC
LDFLAGS =  -L$(LIBDIR) -lgstreamer-1.0 -lgstapp-1.0 -lgobject-2.0 -lglib-2.0 
DEPFILES = rseq.hpp gutils.hpp gsftc.hpp dsopencv.hpp
OLIBDIR = /home/ggne0015/src/opencv-4.1.1/build/lib 
VPATH = .:../
#.cpp.o:
#	$(CPP) $(CPPFLAGS) -fPIC -c $*.cpp
#.c.o:
#	$(CPP) $(CPPFLAGS) -fPIC -c $*.c

all: dcv.so dcvrtpmux.so gstr3p.so
DCVDEPFILES = gstdcv.o dcvutils.o dsopencv.o
DCVR3PDEPFILES = gstr3p.o gstr3psink.o gstr3psrc.o tcptrans.o

dcv.so: $(DCVDEPFILES)
	$(CPP) --shared -o dcv.so $(DCVDEPFILES) -lopencv_world -lm

dcvrtpmux.so: gstdcvrtpmux.o
	$(CPP) --shared -o dcvrtpmux.so gstdcvrtpmux.o -lgstrtp-1.0

gstr3p.so: $(DCVR3PDEPFILES)
	$(CPP) --shared -o gstr3p.so $(DCVR3PDEPFILES) -lgstreamer-1.0 -lgstbase-1.0 -lgstapp-1.0 -lgobject-2.0 -lglib-2.0 -lgio-2.0  -lm

test: client gserver

client: client.o tcptrans.o
	$(LD) -o $@ client.o tcptrans.o -lglib-2.0

server: server.o tcptrans.o
	$(LD) -o $@ server.o tcptrans.o -lglib-2.0

gserver: gserver.o tcptrans.o
	$(LD) -o $@ gserver.o tcptrans.o -lgio-2.0 -lglib-2.0
