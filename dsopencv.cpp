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

#define CV_WARN(...) CV_LOG_WARNING(NULL, "OpenCV | GStreamer warning: " << __VA_ARGS__)

#define COLOR_ELEM "videoconvert"
#define COLOR_ELEM_NAME COLOR_ELEM

#define CV_GST_FORMAT(format) (format)


namespace cv {

static void toFraction(double decimal, CV_OUT int& numerator, CV_OUT int& denominator);
static void handleMessage(GstElement * pipeline);
gboolean writeFrame( const IplImage * image, unsigned int num_frames, double framerate, GstBuffer **buffer ) ;
static gboolean determineFrameDims(Size &sz, gint& channels, bool& isOutputByteBuffer, GstSample *sample) ;



//==================================================================================================

/*!
 * \brief CvCapture_GStreamer::retrieveFrame
 * \return IplImage pointer. [Transfer Full]
 *  Retrieve the previously grabbed buffer, and wrap it in an IPLImage structure
 */
bool retrieveFrame(GstSample * sample, OutputArray dst)
{
    if (!sample)
        return false;
    Size sz;
    gint channels = 0;
    bool isOutputByteBuffer = false;
    if (!determineFrameDims(sz, channels, isOutputByteBuffer, sample))
        return false;

    // gstreamer expects us to handle the memory at this point
    // so we can just wrap the raw buffer and be done with it
    GstBuffer* buf = gst_sample_get_buffer(sample);  // no lifetime transfer
    if (!buf)
        return false;
    GstMapInfo info = {};
    if (!gst_buffer_map(buf, &info, GST_MAP_READ))
    {
        //something weird went wrong here. abort. abort.
        CV_WARN("Failed to map GStreamer buffer to system memory");
        return false;
    }

    try
    {
	    Mat src;
        if (isOutputByteBuffer)
            src = Mat(Size(info.size, 1), CV_8UC1, info.data);
        else
            src = Mat(sz, CV_MAKETYPE(CV_8U, channels), info.data);
        CV_Assert(src.isContinuous());
        src.copyTo(dst);
    }
    catch (...)
    {
        gst_buffer_unmap(buf, &info);
        throw;
    }
    gst_buffer_unmap(buf, &info);

    return true;
}

static gboolean determineFrameDims(Size &sz, gint& channels, bool& isOutputByteBuffer, GstSample *sample)
{
    GstCaps * frame_caps = gst_sample_get_caps(sample);  // no lifetime transfer
    guint32 width,height;

    // bail out in no caps
    if (!GST_CAPS_IS_SIMPLE(frame_caps))
        return false;

    GstStructure* structure = gst_caps_get_structure(frame_caps, 0);  // no lifetime transfer

    // bail out if width or height are 0
    if (!gst_structure_get_int(structure, "width", &width)
        || !gst_structure_get_int(structure, "height", &height))
    {
        CV_WARN("Can't query frame size from GStreeamer buffer");
        return false;
    }

    sz = Size(width, height);

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
            channels = 3;
        }
        else if (format == "UYVY" || format == "YUY2" || format == "YVYU")
        {
            channels = 2;
        }
        else if (format == "NV12" || format == "NV21" || format == "YV12" || format == "I420")
        {
            channels = 1;
            sz.height = sz.height * 3 / 2;
        }
        else if (format == "GRAY8")
        {
            channels = 1;
        }
        else
        {
            CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer format: %s", format.c_str()));
        }
    }
    else if (name == "video/x-bayer")
    {
        channels = 1;
    }
    else if (name == "image/jpeg")
    {
        // the correct size will be set once the first frame arrives
        channels = 1;
        isOutputByteBuffer = true;
    }
    else
    {
        CV_Error_(Error::StsNotImplemented, ("Unsupported GStreamer layer type: %s", name.c_str()));
    }
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
gboolean writeFrame( const IplImage * image, int input_pix_fmt, unsigned int num_frames, double framerate, GstBuffer **buffer )
{
    GstClockTime duration, timestamp;
    GstFlowReturn ret;
    int size;


    if (input_pix_fmt == GST_VIDEO_FORMAT_ENCODED) {
        if (image->nChannels != 1 || image->depth != IPL_DEPTH_8U || image->height != 1) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U, nChannels = 1 and height = 1.");
            return false;
        }
    }
    else
    if(input_pix_fmt == GST_VIDEO_FORMAT_BGR) {
        if (image->nChannels != 3 || image->depth != IPL_DEPTH_8U) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U and nChannels = 3.");
            return false;
        }
    }
    else if (input_pix_fmt == GST_VIDEO_FORMAT_GRAY8) {
        if (image->nChannels != 1 || image->depth != IPL_DEPTH_8U) {
            CV_WARN("cvWriteFrame() needs images with depth = IPL_DEPTH_8U and nChannels = 1.");
            return false;
        }
    }
    else {
        CV_WARN("cvWriteFrame() needs BGR or grayscale images\n");
        return false;
    }

    size = image->imageSize;
    duration = ((double)1/framerate) * GST_SECOND;
    timestamp = num_frames * duration;

    //gst_app_src_push_buffer takes ownership of the buffer, so we need to supply it a copy
    *buffer = gst_buffer_new_allocate(NULL, size, NULL);
    GstMapInfo info;
    gst_buffer_map(*buffer, &info, (GstMapFlags)GST_MAP_READ);
    memcpy(info.data, (guint8*)image->imageData, size);
    gst_buffer_unmap(*buffer, &info);
    GST_BUFFER_DURATION(*buffer) = duration;
    GST_BUFFER_PTS(*buffer) = timestamp;
    GST_BUFFER_DTS(*buffer) = timestamp;
    //set the current number in the frame
    GST_BUFFER_OFFSET(*buffer) = num_frames;

    //GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

    return true;
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

}
gboolean  retrieveFrame(unsigned long *a, unsigned long *b)
{
	cv::Mat img;
	return cv::retrieveFrame(NULL,img) ;
}