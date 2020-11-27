#ifndef DSOPENCV_HPP
#define  DSOPENCV_HPP

#include "precomp.hpp"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>


namespace cv {
gboolean retrieveFrame(GstBuffer * buf, GstCaps * caps, cv::Mat * img,dcvFrameData_t *df) ;
GstBuffer * writeFrame( const cv::Mat * img, dcvFrameData_t *df) ;
} 
extern "C" GstBuffer * dcvProcessStage(GstBuffer *vbuf, GstCaps *gcaps, GstBuffer *dbuf,dcvFrameData_t *df, dcvStageFn_t stage, GstBuffer **newvb) ;
int stage1(cv::Mat img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
int stage2(cv::Mat img, void *dataIn, int insize, void * pointlist, int outdatasize) ;
int stagen(cv::Mat img, void *pointlist, int size, void *dataout, int outdatasize) ;

int writeToArray(std::vector<cv::Point2f> vlist, char *op, int opsize) ;
int writeToArray(std::vector<cv::Point> vlist, char *op, int opsize) ;
int writeToArrayContours(std::vector <std::vector<cv::Point2f>> vlist, char *op, int opsize) ;
int readFromBuffer(char *op,int sz, std::vector<cv::Point2f> & pvlist) ;
int readFromBufferContours(char *op, int sz, std::vector <std::vector<cv::Point2f>> &pvlist) ;
#endif
