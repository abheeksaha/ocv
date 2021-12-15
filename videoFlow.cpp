#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

#include <iostream>
#include <ctype.h>
#include <unistd.h>

#include "foe.hpp"
#include <string.h>

using namespace cv;
using namespace std;

//static void drawOptFlowMap(const Mat& flow, Mat& cflowmap, vector<Point2f> points,  const Scalar& color) ;
static void drawOptFlowMap(const Mat& flow, Mat& cflowmap, vector<Point2f> refpoints, vector<Point2f> cpts, const Scalar& color, const Scalar& color2) ;
static float flowMapAnalyze(Mat &reference, Mat &current, Mat &warp, int blocksize) ;
extern int foeDebug ;
static void help()
{
    // print a welcome message, and the OpenCV version
    cout << "\nThis is a demo of Lukas-Kanade optical flow lkdemo(),\n"
            "Using OpenCV version " << CV_VERSION << endl;
    cout << "\nIt uses camera by default, but you can provide a path to video as an argument.\n";
    cout << "Arguments -m <maxsize,minsize> -g gap -t <window size for adaptive threshold>\n" ;
}

Point2f point;
bool addRemovePt = false;

static void onMouse( int event, int x, int y, int /*flags*/, void* /*param*/ )
{
    if( event == EVENT_LBUTTONDOWN )
    {
        point = Point2f((float)x, (float)y);
        addRemovePt = true;
    }
}

#define MAXFRAMES 200
#include <gst/gst.h>
int main( int argc, char** argv )
{
    VideoCapture cap;
    TermCriteria termcrit(TermCriteria::COUNT|TermCriteria::EPS,60,0.1);
    Size subPixWinSize(10,10); 
    char ch;
    extern char *optarg;
    uint gap=5 ;
    int tval=11;

    const int MAX_COUNT = 500;
    uint nextindex = 0;
    uint searchwindow = 10 ;
    bool nightMode = false;
    uint maxflows = 500 ;
	uint method = 3;
    static double maxsize = -1 ;
    static double minsize = -1 ;

    guint majr, minr, micro, nano;
    gst_version(&majr, &minr, &micro, &nano) ;
    printf("GST Version :%d:%d:%d:%d\n", majr, minr, micro,nano) ;

	while ((ch = getopt(argc,argv,"w:hg:m:t:")) != -1)
	{
		switch(ch) {
			case 'w': searchwindow = atoi(optarg) ; break ;
			case 'g': gap = atoi(optarg) ; break ;
			case 'm': {
					maxsize = atof(strtok(optarg,",")) ;
					minsize = atof(strtok(NULL,",\n\t ")) ;
					if (maxsize == 0 || minsize == 0)
					{
						fprintf(stderr,"Couldn't scan field %s for max and minsize\n",optarg) ;
						maxsize = 10000 ;
						minsize = 2;
					}
				  } 
				break ;
			case 't': tval = atoi(optarg) ; break ;
			case 'h': help() ; exit(1) ;
		}
	}
		

    Size winSize(searchwindow,searchwindow);
#if 0
    cv::CommandLineParser parser(argc, argv, "{@input|0|}");
    string input = parser.get<string>("@input");

    if( input.size() == 1 && isdigit(input[0]) ) {
        //cap.open(input[0] - '0');
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert !appsink",CAP_GSTREAMER);	
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
    }
    else {
        cap.open(input);
    }
#endif
    cap.open("filesrc location=cardrivingIndia.mkv ! matroskademux ! parsebin ! avdec_h264 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
	

//    VideoWriter out("appsrc ! videoconvert ! x264enc tune=zerolatency rc-lookahead=5 speed-preset=2 ! rtph264pay pt=96 name=pay0 ! udpsink host=127.0.0.1 port=5000",CAP_GSTREAMER,0,30,Size(640,480),true);
    

    if( !cap.isOpened() )
    {
        cout << "Could not initialize capturing...\n";
        return 0;
    }
 //   else if (!out.isOpened())
  //  {
//	    cout << "Could not initialize output...\n" ;
 //   }

    namedWindow( "LK Demo", 1 );
    setMouseCallback( "LK Demo", onMouse, 0 );

    Mat gray, refGray, image, frame;
    vector<Point2f> refpoints,currentpoints;
    std::vector<std::vector<Point2f>> points(MAXFRAMES);
    int t=0;
    std::vector<Mat> grayframes(MAXFRAMES) ;
    std::vector<foe::cntr_t> cntrsdb ;
    

    foeDebug =0  ;
    uint fno = 0;
    for(;;)
    {
	int pn ;
	t++ ;
        cap.read(frame);
        if( frame.empty() )
            break;

	
        frame.copyTo(image);
/** Fake **/
#if 0
	image.setTo(Scalar(128,128,128)) ;
	circle( image, Point2f(image.rows/2, image.cols/2), 3, Scalar(255,0,0), -1, 4);
	for (pn=0; pn<10; pn++) {
		int colv = int(image.cols*(pn+1)/20) % image.cols ;
		int rowv = int(image.rows*(0.5 + ((double)(t*pn)/100.0))) % image.rows ;
		circle( image, Point2f(rowv, colv), 3, Scalar(0,255,0), -1, 4);
	}
#endif
#if 1
        cvtColor(image, gray, COLOR_BGR2GRAY);

        if( nightMode )
            image = Scalar::all(0);

	printf("[%d] Initializing image of size %u by %u\n",fno,gray.rows,gray.cols) ;
	gray.copyTo(grayframes[nextindex]) ;

        goodFeaturesToTrack(gray, currentpoints, MAX_COUNT, 0.01, 10, Mat(), 3, 3, 0, 0.4);
	printf("GoodFeatures Returns: %u points\n", currentpoints.size()) ;
        cornerSubPix(gray, currentpoints, subPixWinSize, Size(-1,-1), termcrit);
	points[nextindex] = currentpoints ;
	if (points[(nextindex-gap)%(MAXFRAMES)].size() > 0)
	{
		refpoints = points[(nextindex-gap)%(MAXFRAMES)] ;
		grayframes[(nextindex-gap)%(MAXFRAMES)].copyTo(refGray) ;
	}
	else
		refpoints.resize(0) ;
#if 0
        if( needToInit == 0 )
        {
            // automatic initialization
        	gray.copyTo(refGray );
		refpoints = currentpoints; 
        }
        else 
#endif
	if( !currentpoints.empty() && !refpoints.empty())
        {
            vector<uchar> status;
            vector<float> err;
    		vector<Point2f> p, p_pred;
            size_t i, k;
	    uint nflows = 0 ;

		if (currentpoints.size() > refpoints.size()) {
			currentpoints.resize(refpoints.size()) ;
		}
		else if (currentpoints.size() < refpoints.size()) {
			refpoints.resize(currentpoints.size()) ;
		}
	    if (method == 0)
	    {
    		std::vector<Mat> refPyramid, pyramid ;
		std::vector<Point2f> newPoints ;
		int levels[2];
		int np[2];
#if 0
		np[0] = refpoints.checkVector(2,CV_32F,true) ;
		np[1] = currentpoints.checkVector(2,CV_32F,true) ;
		printf("Reference vector %d: New vector:%d\n",np[0],np[1]) ;
#endif
		levels[0] = buildOpticalFlowPyramid(refGray,refPyramid,winSize,3,false) ;
		levels[1] = buildOpticalFlowPyramid(gray,pyramid,winSize,3,false) ;
		printf("Reference: %d levels,  Current:%d levels\n",levels[0],levels[1]) ;
            	calcOpticalFlowPyrLK(refPyramid, pyramid, refpoints, currentpoints, status, err, winSize,
            	//calcOpticalFlowPyrLK(refGray, gray, refpoints, newPoints, status, err, winSize,
//                                 3, termcrit, OPTFLOW_USE_INITIAL_FLOW, 0.1);
                                 3, termcrit, 0, 0.1);

	    	p.resize(currentpoints.size()) ;
	    	p_pred.resize(refpoints.size()) ;

	        for( i = 0; i < currentpoints.size() ; i++ )
       	     	{

       	         	if( status[i] )
			{
				circle( image, currentpoints[i], 8, Scalar(128,128,0), -1, 8);
				circle( image, refpoints[i], 8, Scalar(128,128,0), 4, 8);
			//g_print("Flow found at point %u,%u\n",refpoints[i].x,refpoints[i].y) ;
				p[nflows] = currentpoints[i] ;
				p_pred[nflows] = refpoints[i] ;
				arrowedLine(image, refpoints[i], currentpoints[i], Scalar(0,255,0)) ;
				nflows++ ;
			}

       	     	}
		if (nflows) 
		{
			printf("LYK: %d flows found\n",nflows) ;
			if (maxflows < nflows) {
				nflows = maxflows ;
				p.resize(nflows) ;
				p_pred.resize(nflows) ;
			}
			Point2f foe =  foe::foeEstimate(nflows, p, p_pred, image.cols, image.rows)  ;
			printf("foe=%.4g %.4g\n",foe.x,foe.y) ;
			circle( image, foe, 8, Scalar(0,0,255), 3, cv::FILLED , 0);
		}
	    }
	    else if (method == 1)
	    {
		Mat uflow;
		printf("Calculating Farneback optical flow\n") ;
            	calcOpticalFlowFarneback(refGray, gray, uflow, 0.5, 3, searchwindow, 3, 5, 1.2, 0);
		Mat dvals(uflow.rows, uflow.cols,uflow.type()) ;
		drawOptFlowMap(uflow,image,refpoints,currentpoints,Scalar(0,128,128),Scalar(240,240,128)) ;
	    }
	    else if (method == 2)
	    {
		Mat warp(gray.rows,gray.cols,gray.type()) ;
		uint blocksize=10 ;
		flowMapAnalyze(refGray,gray,warp,blocksize) ;
	    }
	   else if (method == 3)
	   {
		int nc = foe::foeDetectContours(image, cntrsdb, fno,&maxsize,&minsize,tval);
	    	Scalar fgcolor(240,113,113) ;
	    	Scalar fgcolor2(101,255,113) ;
		vector < vector<Point>> fcntrs,ocntrs ;
		
		printf("Maxsize = %e minsize = %e\n",maxsize,minsize) ;
		for (auto it=cntrsdb.begin(); it != cntrsdb.end() ; it++) {
			std::vector<Point> tcntr = it->cntr;
			assert(tcntr.size() > 0) ;
			if (it->lframe == fno && (it->fframe > fno-10))
				fcntrs.emplace(fcntrs.end(),tcntr) ;
			else if (it->lframe == fno && (it->fframe < fno-10))
				ocntrs.emplace(ocntrs.end(),tcntr) ;
		}
		if (fcntrs.size() > 0) drawContours(image,fcntrs,-1,fgcolor,1) ;
		if (ocntrs.size() > 0) drawContours(image,ocntrs,-1,fgcolor2,1) ;
	   }
        }


	nextindex = (nextindex + 1)%(MAXFRAMES) ;
#endif
        imshow("LK Demo", image);
	//if( out.isOpened()) out.write(image);

        char c = (char)waitKey(10);
        if( c == 27 )
            break;
        switch( c )
        {
        case 'r':
            nextindex = true;
            break;
        case 'c':
            refpoints.clear();
            currentpoints.clear();
            break;
        case 'n':
            nightMode = !nightMode;
            break;
        }
	fno++ ;

    }

    return 0;
}
static void drawOptFlowMap(const Mat& flow, Mat& cflowmap, vector<Point2f> refpoints, vector<Point2f> cpts, const Scalar& color, const Scalar& color2)
{
    for(int y = 0; y < refpoints.size() && y < 3; y++)
    {
	  Point2f refpt = refpoints[y] ;
	  Point2f cpt = cpts[y] ;
            const Point2f& fxy = flow.at<Point2f>(refpt.y, refpt.x);
		Point2f fd(0,0);
            line(cflowmap, refpt, Point(cvRound(refpt.x+fxy.x), cvRound(refpt.y+fxy.y)),
                 Scalar(14,15,14));
            circle(cflowmap, refpt, 2, color,4 );
            circle(cflowmap, cpt, 2, color2, 4);
    }
}

#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>


static float flowMapAnalyze(Mat &reference, Mat &current, Mat &tw, int blocksize)
{
	int i,j,k,l;
	Mat_<unsigned char> inputmask(reference.rows,reference.cols) ;
	Mat_<float> warp(2,3);
	warp.zeros(2,3) ;
	
	double cc = findTransformECC(current,current,warp);
	printf("i=%d j=%d cc=%.4g ",cc) ;
#if 0
	for (i=0; i*blocksize/2<reference.rows - blocksize; i++) {
		for (j=0; j*blocksize/2<reference.cols-blocksize; j++) {
			inputmask.ones(reference.rows,reference.cols) ;
			for (k=0; k < blocksize; k++) 
				for (l=0; l<blocksize; l++) 
					inputmask.at<unsigned char>(i*blocksize/2+k,j*blocksize/2+l) = 1 ;
//			double cc = findTransformECC(reference,reference,warp,MOTION_TRANSLATION,
//           			TermCriteria(TermCriteria::COUNT+TermCriteria::EPS, 50, -1), 
//					inputmask);
			double cc = findTransformECC(reference,reference,warp);
			printf("i=%d j=%d cc=%.4g ",cc) ;
		
		}
	}
	printf("\n") ;
#endif
}
