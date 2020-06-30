FROM ubuntu:18.04

WORKDIR /notebooks

# This is a dev image, needed to compile OpenCV with CUDA
# Install  Gstreamer and OpenCV Pre-requisite libs
RUN  apt-get update -y && apt-get install -y \
            libgstreamer1.0-0 \
            gstreamer1.0-plugins-base \
            gstreamer1.0-plugins-good \
            gstreamer1.0-plugins-bad \
            gstreamer1.0-plugins-ugly \
            gstreamer1.0-libav \
            gstreamer1.0-doc \
            gstreamer1.0-tools \
            libgstreamer1.0-dev \
            libgstreamer-plugins-base1.0-dev
RUN  apt-get update -y && apt-get install -y  pkg-config \
 zlib1g-dev  libwebp-dev \
 libtbb2 libtbb-dev  \
 libgtk2.0-dev pkg-config libavcodec-dev libavformat-dev libswscale-dev \
 cmake
RUN apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  gcc wget unzip\
  git

WORKDIR /tmp/opencv
RUN wget -O opencv.zip https://github.com/opencv/opencv/archive/4.1.1.zip && \
    unzip opencv.zip && \
    wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/4.1.1.zip && \
    unzip opencv_contrib.zip && \
    mkdir /tmp/opencv/opencv-4.1.1/build

WORKDIR /tmp/opencv/opencv-4.1.1/build
RUN cmake -D CMAKE_BUILD_TYPE=RELEASE -D CMAKE_INSTALL_PREFIX=/usr/local -D OPENCV_EXTRA_MODULES_PATH=/tmp/opencv/opencv_contrib-4.1.1/modules -DCMAKE_LIBRARY_PATH=/usr/local/cuda-10.1/lib64/stubs/ \ -D WITH_CUDA=ON -D WITH_TBB=ON -D ENABLE_FAST_MATH=1 -D CUDA_FAST_MATH=1 -D WITH_CUBLAS=1 -D BUILD_TIFF=ON -D WITH_QT=OFF -D WITH_GSTREAMER=ON -D WITH_GSTREAMER_0_10=OFF -D OPENCV_GENERATE_PKGCONFIG=ON -D WITH_FFMPEG=ON -D WITH_LIBV4L=ON -D WITH_V4L=ON -D WITH_V4L2=ON -D WITH_LIBV4L2=ON .. && \
     make && \
     make install &&\
     rm -rf /tmp/opencv
      
WORKDIR /notebooks/
COPY ./ocv.tar /notebooks/
RUN tar -xvf ocv.tar

RUN mkdir -p opencv
WORKDIR /notebooks/opencv

RUN git clone https://github.com/opencv/opencv.git
RUN git clone https://github.com/opencv/opencv_contrib.git

WORKDIR /notebooks/opencv/
RUN git clone https://gitlab.freedesktop.org/gstreamer/gstreamer

COPY ./gst-plugins-good-1.14.5.tar.xz /notebooks/opencv/gstreamer/

WORKDIR /notebooks/opencv/gstreamer/
RUN tar -xvf gst-plugins-good-1.14.5.tar.xz
WORKDIR /notebooks/opencv/gstreamer/gst-plugins-good-1.14.5/
RUN ./configure && make && make install


WORKDIR /notebooks/ocv/
RUN make

WORKDIR /notebooks
RUN apt-get install tcpdump -y \
                    iptables -y \
                    wget -y \
                    iproute2 -y \
                    vim -y 
RUN apt-get install -qqy x11-apps 
RUN apt-get install -y iptables
RUN apt-get install -y net-tools

WORKDIR /notebooks/ocv
CMD ./gdyn -n 50018 -p 50019 -i 172.172.172.172 -b intel
