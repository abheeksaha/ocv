#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

#include <iostream>
#include <ctype.h>

#include <gst/gst.h>
#include "dsopencv.hpp"
#include "foe.hpp"

using namespace cv;
using namespace std;

static void help()
{
    // print a welcome message, and the OpenCV version
    cout << "\nThis is a demo of Lukas-Kanade optical flow lkdemo(),\n"
            "Using OpenCV version " << CV_VERSION << endl;
    cout << "\nIt uses camera by default, but you can provide a path to video as an argument.\n";
    cout << "\nHot keys: \n"
            "\tESC - quit the program\n"
            "\tr - auto-initialize tracking\n"
            "\tc - delete all the points\n"
            "\tn - switch the \"night\" mode on/off\n"
            "To add/remove a feature point click it\n" << endl;
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

#define MAXOPDATASIZE 16384
int main( int argc, char** argv )
{
    VideoCapture cap;
    TermCriteria termcrit(TermCriteria::COUNT|TermCriteria::EPS,20,0.03);
    Size subPixWinSize(10,10), winSize(31,31);
    char op[MAXOPDATASIZE] ;
    char destop[MAXOPDATASIZE] ;
    int size1,size2;

    const int MAX_COUNT = 500;
    bool needToInit = false;
    bool nightMode = false;
    extern int foeDebug ;

    guint majr, minr, micro, nano;
    gst_version(&majr, &minr, &micro, &nano) ;
    printf("GST Version :%d:%d:%d:%d\n", majr, minr, micro,nano) ;

    help();
    cv::CommandLineParser parser(argc, argv, "{@input|0|}");
    string input = parser.get<string>("@input");

    if( input.size() == 1 && isdigit(input[0]) ) {
        //cap.open(input[0] - '0');
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert !appsink",CAP_GSTREAMER);	
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
        cap.open("filesrc location=cardrivingIndia.mkv ! matroskademux ! parsebin ! avdec_h264 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
	
    }
    else {
        cap.open(input);
    }

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

    foeDebug=1;
    namedWindow( "LK Demo", 1 );
    setMouseCallback( "LK Demo", onMouse, 0 );

    Mat gray, prevGray, image, frame;
    vector<Point2f> points[2];
    vector<vector<Point2f>> contours(1024) ;

    for(;;)
    {
	int npoints,ncontours;
        cap.read(frame);
        if( frame.empty() )
            break;

        frame.copyTo(image);
#if 1
    	size1 = size2 = 0 ;
	size1 = stage1(image,NULL,0,op,MAXOPDATASIZE) ;
//	size2 = stage2(image,op,size1,&op[size1],MAXOPDATASIZE-size1) ;
//	readFromBuffer2vec(&op[size1],size2 ,points[0], points[1]) ;
//	npoints = points[0].size() ;
//	if (npoints > 10) npoints = 10 ;
//	printf("%d points: ",npoints) ;
	size1 = foe::foeStage1(frame,op,MAXOPDATASIZE) ;
	printf("foe Stage1 returns %d bytes\n",size1) ;
	size2 = stage2(image,op,size1,&op[size1],MAXOPDATASIZE-size1) ;
#if 0
	if (contours.size() != 0) printf("Identified %d contours\n",contours.size()) ;
	int datasize = MAXOPDATASIZE ;
	int outdatasize = MAXOPDATASIZE ;
	char *pop = op ;
	char *dpop = destop ;
	for (ncontours = 0; ncontours < contours.size() && datasize > 0 && outdatasize > 0; ncontours++) {
	 	size1 = writeToArray(contours[ncontours], (char *)pop, datasize) ;
		size2 = stage2(image,pop,size1,dpop,outdatasize) ;
		datasize -= size1;
		outdatasize -= size2;
		pop = &pop[size1] ;
		dpop = &dpop[size2] ;
	}
#endif
		
	// Point2f foe = foe::foeEstimate(image, npoints,points[0], points[1], 640,480) ;
	// printf("Foe at %.4g %.4g\n",foe.x,foe.y) ;
#endif
        imshow("LK Demo", image);
	//if( out.isOpened()) out.write(image);

        char c = (char)waitKey(10);
        if( c == 27 )
            break;
        switch( c )
        {
        case 'r':
            needToInit = true;
            break;
        case 'c':
            points[0].clear();
            points[1].clear();
            break;
        case 'n':
            nightMode = !nightMode;
            break;
        }

//        std::swap(points[1], points[0]);
        cv::swap(prevGray, gray);
    }

    return 0;
}
