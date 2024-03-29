/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2008, 2011, Nils Hasler, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

/*!
 * \file cap_gstreamer.cpp
 * \author Nils Hasler <hasler@mpi-inf.mpg.de>
 *         Max-Planck-Institut Informatik
 * \author Dirk Van Haerenborgh <vhdirk@gmail.com>
 *
 * \brief Use GStreamer to read/write video
 */

#include "precomp.hpp"

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include <iostream>
#include <string.h>

#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/video/video.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/riff/riff-media.h>

#define VERSION_NUM(major, minor, micro) (major * 1000000 + minor * 1000 + micro)
#define FULL_GST_VERSION VERSION_NUM(GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO)

#include <gst/pbutils/encoding-profile.h>
//#include <gst/base/gsttypefindhelper.h>

#include "rseq.h"
#include "gutils.hpp"
#include "gstdcv.h"
#include "dcvopencv.hpp"
#define CV_WARN(...) CV_LOG_WARNING(NULL, "OpenCV | GStreamer warning: " << __VA_ARGS__)

#define COLOR_ELEM "videoconvert"
#define COLOR_ELEM_NAME COLOR_ELEM

#define CV_GST_FORMAT(format) (format)


int dcvOptDebug = 0;
#ifdef FOE
#include "foe.hpp"
#else
int foeDebug() { return 0 ; }
#endif

namespace cv {

static void toFraction(double decimal, CV_OUT int& numerator, CV_OUT int& denominator);
static gboolean determineFrameDims(Size *sz, gint* channels, gboolean * isOutputByteBuffer, GstCaps *frame_caps) ;

//==================================================================================================

/*!
 * \brief CvCapture_GStreamer::retrieveFrame
 * \return IplImage pointer. [Transfer Full]
 *  Retrieve the previously grabbed buffer, and wrap it in an IPLImage structure
 */
gboolean retrieveFrame(GstBuffer * buf, GstCaps * caps, Mat * img,dcvFrameData_t *df)
{
    if (!buf)
        return false;
    Size sz;
    df->channels = 0;
    df->isOutputByteBuffer = false ;
    if (determineFrameDims(&sz, &df->channels, &df->isOutputByteBuffer, caps) != true)
        return false;
    df->height = sz.height ;
    df->width = sz.width ;

    // gstreamer expects us to handle the memory at this point
    // so we can just wrap the raw buffer and be done with it
    GstMapInfo info = {};
    if (!gst_buffer_map(buf, &info, GST_MAP_READ))
    {
        //something weird went wrong here. abort. abort.
        CV_WARN("Failed to map GStreamer buffer to system memory");
        return false;
    }

    try
    {
        if (df->isOutputByteBuffer)
            *img = Mat(Size(info.size, 1), CV_8UC1, info.data);
        else
            *img = Mat(sz, CV_MAKETYPE(CV_8U, df->channels), info.data);
        CV_Assert(img->isContinuous());
    }
    catch (...)
    {
        gst_buffer_unmap(buf, &info);
        throw;
    }
    gst_buffer_unmap(buf, &info);

    return true;
}

static gboolean determineFrameDims(Size *sz, gint* channels, gboolean * isOutputByteBuffer, GstCaps *frame_caps)
{
    guint32 width,height;

    // bail out in no caps
    if (!GST_CAPS_IS_SIMPLE(frame_caps))
        return false;
    if (dcvOptDebug) g_print("Caps says: %s\n",gst_caps_to_string(frame_caps)) ;

    GstStructure* structure = gst_caps_get_structure(frame_caps, 0);  // no lifetime transfer

    // bail out if width or height are 0
    if (!gst_structure_get_int(structure, "width", &width)
        || !gst_structure_get_int(structure, "height", &height))
    {
        CV_WARN("Can't query frame size from GStreeamer buffer");
        return false;
    }

    *sz = Size(width, height);

    const gchar* name_ = gst_structure_get_name(structure);
    if (!name_)
        return false;
    std::string name = toLowerCase(std::string(name_));

    // we support 11 types of data:
    //     video/x-raw, format=BGR   -> 8bit, 3 channels
    //     video/x-raw, format=GRAY8 -> 8bit, 1 channel
    //     video/x-raw, format=UYVY  -> 8bit, 2 channel
    //     video/x-raw, format=YUY2  -> 8bit, 2 channel
    //     video/x-raw, format=YVYU  -> 8bit, 2 channel
    //     video/x-raw, format=NV12  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=NV21  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=YV12  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-raw, format=I420  -> 8bit, 1 channel (height is 1.5x larger than true height)
    //     video/x-bayer             -> 8bit, 1 channel
    //     image/jpeg                -> 8bit, mjpeg: buffer_size x 1 x 1
    // bayer data is never decoded, the user is responsible for that
    // everything is 8 bit, so we just test the caps for bit depth
    if (name == "video/x-raw")
    {
        const gchar* format_ = gst_structure_get_string(structure, "format");
        if (!format_)
            return false;
        std::string format = toUpperCase(std::string(format_));

        if (format == "BGR")
        {
            *channels = 3;
        }
        else if (format == "UYVY" || format == "YUY2" || format == "YVYU")
        {
            *channels = 2;
        }
        else if (format == "NV12" || format == "NV21" || format == "YV12" || format == "I420")
        {
            *channels = 1;
            sz->height = sz->height * 3 / 2;
        }
        else if (format == "GRAY8")
        {
            *channels = 1;
        }
        else
        {
            CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer format: %s", format.c_str()));
        }
    }
    else if (name == "video/x-bayer")
    {
        *channels = 1;
    }
    else if (name == "image/jpeg")
    {
        // the correct size will be set once the first frame arrives
        *channels = 1;
        *isOutputByteBuffer = true;
    }
    else
    {
        CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer layer type: %s", name.c_str()));
    }
    if (dcvOptDebug) g_print("Channels=%d sz.height=%d sz.width=%d\n",*channels,sz->height,sz->width) ;
    return true;
}


/*!
 * \brief CvVideoWriter_GStreamer::writeFrame
 * \param image
 * \return
 * Pushes the given frame on the pipeline.
 * The timestamp for the buffer is generated from the framerate set in open
 * and ensures a smooth video
 */
GstBuffer * writeFrame( const Mat * img, dcvFrameData_t *df)
{
	GstClockTime duration, timestamp;
	GstBuffer *gb ;
	int size;
	IplImage image;
	CvSize sz = { df->width, df->height };
	cvInitImageHeader(&image, sz, IPL_DEPTH_8U, df->channels);
	cvSetData(&image, const_cast<unsigned char*>(img->data), img->step);

	size = image.imageSize;
	duration = ((double)1/df->framerate) * GST_SECOND;
	timestamp = df->num_frames * duration;
	
	//gst_app_src_push_buffer takes ownership of the buffer, so we need to supply it a copy
	gb = gst_buffer_new_allocate(NULL, size, NULL);
	GstMapInfo info;
	gst_buffer_map(gb, &info, (GstMapFlags)GST_MAP_READ);
	memcpy(info.data, (guint8*)image.imageData, size);
	gst_buffer_unmap(gb, &info);
//	GST_BUFFER_DURATION(gb) = duration;
//	GST_BUFFER_PTS(gb) = timestamp;
//	GST_BUFFER_DTS(gb) = timestamp;
	//set the current number in the frame
//	GST_BUFFER_OFFSET(gb) = df->num_frames;
	
	return gb;
}
	
	// utility functions
	
void toFraction(const double decimal, int &numerator_i, int &denominator_i)
{
	double err = 1.0;
	int denominator = 1;
	double numerator = 0;
	for (int check_denominator = 1; ; check_denominator++)
	{
	        double check_numerator = (double)check_denominator * decimal;
	        double dummy;
	        double check_err = modf(check_numerator, &dummy);
	        if (check_err < err)
	        {
	            err = check_err;
	            denominator = check_denominator;
	            numerator = check_numerator;
	            if (err < FLT_EPSILON)
	                break;
	        }
		if (check_denominator == 100)  // limit
			break;
	}
	numerator_i = cvRound(numerator);
	denominator_i = denominator;
	//printf("%g: %d/%d    (err=%g)\n", decimal, numerator_i, denominator_i, err);
}
	
} //End of cv namespace
	
using namespace cv;
using namespace std;
void putFrameNum(Mat img, int fno) ;
#include <vector>
	/** Write a vector of points to an output array and vice versa **/
#if 0
int writeToArray(vector<Point> vlist, char *op, int opsize)
{
	char *pop = op ;
	int step, tstep=0;
	step = sprintf(pop,"Size:%d\n",vlist.size()) ;
	if (vlist.size() == 0) {
		return step ;
	}
	pop += step ; tstep  += step;
	if (foeDebug()) { printf("Writing vector of points:%d\n",vlist.size()) ; }
	for (vector<Point>::iterator it = vlist.begin() ; it != vlist.end(); ++it)
	{
		step = sprintf(pop,"%.4g,%.4g\n",(float)it->x, (float)it->y);
		if (foeDebug()) printf("[%.4g %.4g] ",(float)it->x,(float)it->y) ;
		pop += step ; tstep  += step;
	}
	if (foeDebug()) printf("\n") ;
	return tstep ;
}
int writeToArray(vector<Point2f> vlist, char *op, int opsize)
{
	char *pop = op ;
	int step, tstep=0;
	step = sprintf(pop,"Size:%d\n",vlist.size()) ;
	if (vlist.size() == 0) {
		return step ;
	}
	pop += step ; tstep  += step;
	for (vector<Point2f>::iterator it = vlist.begin() ; it != vlist.end(); ++it)
	{
		step = sprintf(pop,"%.4g,%.4g\n",it->x, it->y);
		pop += step ; tstep  += step;
	}
	return tstep ;
}

#ifdef FOESTAGE
static int writeToArrayContours(vector<vector<Point2f>> vlist, char *op, int opsize)
{
	char *pop = op ;
	int step, tstep=0;
	int it;

	step = sprintf(pop,"List:%d\n",vlist.size()) ;
	if (vlist.size() == 0) {
		return step ;
	}
	pop += step ; tstep  += step;
	for (it = 0 ; it < vlist.size(); ++it)
	{
		if (opsize < tstep) break ;
		step = writeToArray(vlist[it],pop,opsize - tstep) ;
		tstep += step ;
		pop += step ;
	}
	return it ;
}
#endif
	
//
// Buffer format
// Size:<number of entries>
// Entry can be a point or a list of points.
// List of points are given with a starting [Size:]
//
static int readFromBuffer(char *op,int sz, vector<Point2f> & pvlist)
{
	char *pop = op ;
	char *tok ;
	int nitems;
	int it;
	bool contour=false ;
	
	pop = strtok(op,"\n") ;
	tok = pop ; if (strncmp(tok,"Size:",5) != 0) {return false ;}
	tok += strlen("Size:") ; 
	nitems = atoi(tok) ;

	if (nitems == 0) return 0 ; /** Don't mess with pvlist **/
	if (pvlist.size() != nitems)
		pvlist.resize(nitems) ; 
	
	for (it=0; it<nitems; it++ )
	{
		float xv,yv ;
		pop = strtok(NULL,",\n") ; tok = pop ; if (!tok) break ;xv = atof(tok) ;
		pop = strtok(NULL,",\n") ; tok = pop ; if (!tok) break ;yv = atof(tok) ;
		pvlist[it].x = xv;
	       	pvlist[it].y = yv;
	}
	return it ;
}
#ifdef FOESTAGE
static int readFromBufferContours(char *op, int sz, vector <vector<Point2f>> &pvlist)
{
	char *pop = op ;
	char *tok ;
	int ncontours;
	int it,tstep=0,lsz;
	
	tok = strtok(op,"\n") ;
	pop = &op[strlen(tok)] ;
	lsz = sz - strlen(tok) ;
	if (strncmp(tok,"List:",5) != 0) {return -1 ;}
	tok += strlen("Size:") ; ncontours = atoi(tok) ;
	printf("Number of lists:%d\n",ncontours) ;


	if (ncontours == 0) return 0 ; /** Don't mess with pvlist **/
	if (pvlist.size() != ncontours)
		pvlist.resize(ncontours) ; 
	
	for (it=tstep=0; it<ncontours; it++ )
	{
		pop += tstep;
		lsz -= tstep;
		if (lsz <= 0) break ;
		tstep += readFromBuffer(pop,lsz,pvlist[it]) ;
	}
	return it;
}
#endif
#endif
	
static gboolean frameToImg(GstBuffer *buf, GstCaps *caps, Mat *img, dcvFrameData_t *df)
{
	gboolean retval =  retrieveFrame(buf,caps,img, df);
	return retval;
}
	
TermCriteria termcrit(TermCriteria::COUNT|TermCriteria::EPS,20,0.03);
#define THRESH 1
static gboolean bigdiff(vector<Point2f> v1, vector<Point2f> v2)
{
	float diff = 0 ;
	vector<Point2f>::iterator it;
	vector<Point2f>::iterator it2;
	if (v1.empty() || v2.empty()) return true ;
	else if (v1.size() != v2.size()) return true ;
	for (it = v1.begin(),it2 = v2.begin() ; it != v1.end() && it2 != v2.end(); ++it,++it2)
	{
		Point2f x1,x2 ;
		x1 = *it ;
		x2 = *it2;
		diff += pow(x1.x - x2.x,2) + pow(x1.y - x2.y,2) ;
	}
	if (sqrt(diff)/(float)v1.size() > THRESH) return true ;
	else return false ;
}
	
#if 0
#endif
	
/** Main platform functions **/
#define MAXSTAGEDATASIZE 16384
extern "C" 
GstBuffer * dcvProcessStage(GstBuffer *vbuf, GstCaps *gcaps, GstBuffer *dbuf,dcvFrameData_t *df, dcvStageFn_t stage, GstBuffer **newvb)
{
	Mat img;
	gboolean res  ;
	char opdata[MAXSTAGEDATASIZE] ;
	int size;
	GstBuffer *newdb = NULL ;
	g_print("In DCV Process Stage:\n") ;
	if (frameToImg(vbuf,gcaps,&img,df) == false) {
		printf("Something went wrong extracting image\n") ;
		return NULL ;
	}
	if (dbuf != NULL ) {
		GstMemory *odmem = gst_buffer_get_all_memory(dbuf) ;
		GstMapInfo odmap ;
		g_assert(odmem) ;
		if (gst_memory_map(odmem, &odmap, GST_MAP_READ) != TRUE) {
			g_print("Memory mapping for data buf failed!\n") ;
			return NULL ;
		}
		else 
		{
			char *op = odmap.data ;
			op += getTagSize() ;
			if (stage) size  = stage(img,op,odmap.size - getTagSize(),opdata,MAXSTAGEDATASIZE) ;
			else size = 0 ;
			gst_memory_unmap(odmem,&odmap) ;
		}
	}
	else if (stage != NULL){
		size = stage((cv::Mat)img,NULL,NULL, opdata,MAXSTAGEDATASIZE) ;
	}
	else
		size = 0 ;
	if (dcvOptDebug) g_print("Stage fn writes %d bytes\n",size) ;
	
	if (size > 0)
	{
		GstMemory *vmem,*dmem;
		GstMapInfo vmap,dmap ;
		char *obuf ;
		vmem = gst_buffer_get_all_memory(vbuf) ;
		if (gst_memory_map(vmem, &vmap, GST_MAP_READ) != TRUE) {
			g_print("Memory mapping for old video buf failed!\n") ;
			return NULL ;
		}
		newdb = gst_buffer_new_allocate(NULL,getTagSize() + size,NULL) ;
		dmem = gst_buffer_get_all_memory(newdb) ;
		if (gst_memory_map(dmem, &dmap, GST_MAP_READ) != TRUE) {
			g_print("Memory mapping for new data buf failed!\n") ;
			return NULL ;
		}
		obuf = dmap.data ;
		dcvTagBuffer(vmap.data,vmap.size,(void *)dmap.data,getTagSize()) ;
	
		obuf += getTagSize() ;
		memcpy(obuf,opdata,size) ;
		gst_memory_unmap(dmem,&dmap) ;
		gst_memory_unmap(vmem,&vmap) ;
	}
	else
		newdb = NULL ;
	
	/** Now convert img back to buffer **/
	*newvb = cv::writeFrame(&img, df) ;
	return newdb ;
}
