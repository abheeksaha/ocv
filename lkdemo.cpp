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

#define MAXOPDATASIZE 120000
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
    extern int foeDebug ;
    int fno=0;

    guint majr, minr, micro, nano;
    gst_version(&majr, &minr, &micro, &nano) ;
    printf("GST Version :%d:%d:%d:%d\n", majr, minr, micro,nano) ;

    help();
    cv::CommandLineParser parser(argc, argv, "{@input|0|}");
    string input = parser.get<string>("@input");
    if( input.size() == 1 && isdigit(input[0]) ) {
        cap.open("filesrc location=cardrivingIndia.mkv ! matroskademux ! parsebin ! avdec_h264 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
    }
    else {
        cap.open(input);
    }
	
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
//	size1 = stage1(image,NULL,0,op,MAXOPDATASIZE) ;
//	size2 = stage2(image,op,size1,&op[size1],MAXOPDATASIZE-size1) ;
//	npoints = points[0].size() ;
//	if (npoints > 10) npoints = 10 ;
//	printf("%d points: ",npoints) ;
	size1 = foe::foeDetectContours(image,op,MAXOPDATASIZE) ;
	printf("foe Stage1 returns %d bytes\n",size1) ;
//	size2 = stage2(image,op,size1,&op[size1],MAXOPDATASIZE-size1) ;
//	readFromBuffer2vec(&op[size1],size2 ,points[0], points[1]) ;
		
	// Point2f foe = foe::foeEstimate(image, npoints,points[0], points[1], 640,480) ;
	// printf("Foe at %.4g %.4g\n",foe.x,foe.y) ;
#endif
        imshow("LK Demo", image);
	//if( out.isOpened()) out.write(image);

        char c = (char)waitKey(10);
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
            break;
        }
        cv::swap(prevGray, gray);
	if (++fno > 500) break ;
    }

    return 0;
}
