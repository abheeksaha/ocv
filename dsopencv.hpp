#ifndef DSOPENCV_HPP
#define  DSOPENCV_HPP

#include "precomp.hpp"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
typedef struct {
	int height;
	int width;
	gint channels;
	bool isOutputByteBuffer ;
	int framerate ;
	int num_frames ;
}  dcvFrameData_t ;

typedef int (* dcvStageFn_t )(cv::Mat img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
namespace cv {
gboolean retrieveFrame(GstBuffer * buf, GstCaps * caps, cv::Mat * img,dcvFrameData_t *df) ;
GstBuffer * writeFrame( const cv::Mat * img, dcvFrameData_t *df) ;
} 
GstBuffer * dcvProcessStage(GstBuffer *vbuf, GstCaps *gcaps, GstBuffer *dbuf,dcvFrameData_t *df, dcvStageFn_t stage, GstBuffer **newvb) ;
int stage1(cv::Mat img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
int stage2(cv::Mat img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
int stagen(cv::Mat img, void *pointlist, int size, void *dataout, int outdatasize) ;

#endif
