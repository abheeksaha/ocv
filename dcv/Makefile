CC = gcc -g
CPP = g++ -g
LD = gcc -g
LDCPP = g++ -g
SRCHOME=/home/ggne0015/src/
OPENCV_DIR=$(SRCHOME)/opencv-4.1.1
GSTREAMER_DIR=$(SRCHOME)/gstreamer/
LIBDIR = /usr/local/lib
CFLAGS = -pthread -I/usr/local/include/gstreamer-1.0 -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
	 -I$(GSTREAMER_DIR)/gstreamer-1.16.0/ -I$(GSTREAMER_DIR)gst-plugins-good-1.16.0/ -I/usr/local/include/opencv4/ \
	 -I$(OPENCV_DIR)/modules/core/include -I$(OPENCV_DIR)/build/  -I$(OPENCV_DIR)/modules/videoio/include -I$(SRCHOME)/ocvStages/foe -I$(SRCHOME)/ocvSTages/foe/dcv
CPPFLAGS = -fpermissive -DFOESTAGE -DRTP_MUX -DVP9PAY -DVP9PAYDS $(CFLAGS) 
LDFLAGS =  -L$(LIBDIR) -lgstreamer-1.0 -lgstapp-1.0 -lgobject-2.0 -lglib-2.0 
DEPFILES = rseq.hpp gutils.hpp gsftc.hpp dsopencv.hpp
OLIBDIR = /home/ggne0015/src/opencv-4.1.1/build/lib 
.cpp.o:
	$(CPP) $(CPPFLAGS) -fPIC -c $*.cpp
.c.o:
	$(CPP) $(CPPFLAGS) -fPIC -c $*.c

dcv.so: gstdcv.o gutils.o ../dsopencv.o
	gcc --shared -o dcv.so gstdcv.o ../dsopencv.o gutils.o

all: dcv.so
