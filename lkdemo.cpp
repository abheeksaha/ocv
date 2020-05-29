#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include <unistd.h>

#include <iostream>
#include <ctype.h>

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

#include <gst/gst.h>
extern int writeToBuffer(vector<Point2f> vlist, char *op) ;
extern gboolean readFromBuffer(char *op,int sz, vector<Point2f> * pvlist) ;
int main( int argc, char** argv )
{
    VideoCapture cap;
    TermCriteria termcrit(TermCriteria::COUNT|TermCriteria::EPS,20,0.03);
    Size subPixWinSize(10,10), winSize(31,31);

    const int MAX_COUNT = 500;
    bool needToInit = true;
    bool nightMode = false;
    int pktsout=0;

    guint majr, minr, micro, nano;
    gst_version(&majr, &minr, &micro, &nano) ;
    printf("GST Version :%d:%d:%d:%d\n", majr, minr, micro,nano) ;

    help();
    cv::CommandLineParser parser(argc, argv, "{@input|0|}");
    string input = parser.get<string>("@input");

//    if( input.size() == 1 && isdigit(input[0]) ) {
        //cap.open(input[0] - '0');
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert !appsink",CAP_GSTREAMER);	
        //cap.open("rtspsrc location=rtsp://192.168.1.3:8554/test ! rtpvp8depay ! queue ! avdec_vp8 ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
        cap.open("filesrc location=v1.webm ! matroskademux !  vp9dec ! videoconvert ! videoscale ! video/x-raw,width=640,height=480 !appsink",CAP_GSTREAMER);	
	
 //   }
  //  else {
 //       cap.open(input);
 //   }

//    VideoWriter out("appsrc ! videoconvert ! videoscale ! vp9enc ! rtpvp9pay ! udpsink host=192.168.1.71 port=50019",CAP_GSTREAMER,0,30,Size(640,480),true);
    

    if( !cap.isOpened() )
    {
        cout << "Could not initialize capturing...\n";
        return 0;
    }

    namedWindow( "LK Demo", 1 );
    setMouseCallback( "LK Demo", onMouse, 0 );

    Mat gray, prevGray, image, frame;
    vector<Point2f> points[2];
    vector<Point2f> pointsg;

    for(;;)
    {
        cap.read(frame);
        if( frame.empty() )
            break;

	sleep(1) ;
        frame.copyTo(image);
        cvtColor(image, gray, COLOR_BGR2GRAY);

        if( nightMode )
            image = Scalar::all(0);

        if( needToInit )
        {
            // automatic initialization
	    char op[8192] ;
	    int nb ;
            goodFeaturesToTrack(gray, pointsg, MAX_COUNT, 0.01, 10, Mat(), 3, 3, 0, 0.04);
            cornerSubPix(gray, pointsg, subPixWinSize, Size(-1,-1), termcrit);
            addRemovePt = false;
	    nb = writeToBuffer(pointsg,op) ;
	    printf("Number of bytes written:%d\n",nb) ;
		if (readFromBuffer(op,8192, &points[1]) == false) {
			fprintf(stderr,"Read/Write operation failed!\n") ; 
		}
        }
        else if( !points[0].empty() )
        {
            vector<uchar> status;
            vector<float> err;
            if(prevGray.empty())
                gray.copyTo(prevGray);
            calcOpticalFlowPyrLK(prevGray, gray, points[0], points[1], status, err, winSize,
                                 3, termcrit, 0, 0.001);
            size_t i, k;
            for( i = k = 0; i < points[1].size(); i++ )
            {
                if( addRemovePt )
                {
                    if( norm(point - points[1][i]) <= 5 )
                    {
                        addRemovePt = false;
                        continue;
                    }
                }

                if( !status[i] )
                    continue;

                points[1][k++] = points[1][i];
                circle( image, points[1][i], 3, Scalar(0,255,0), -1, 8);
            }
            points[1].resize(k);
        }

        if( addRemovePt && points[1].size() < (size_t)MAX_COUNT )
        {
            vector<Point2f> tmp;
            tmp.push_back(point);
            cornerSubPix( gray, tmp, winSize, Size(-1,-1), termcrit);
            points[1].push_back(tmp[0]);
            addRemovePt = false;
        }

        needToInit = false;
	pktsout++ ; printf("Frame number:%d\n",pktsout) ;
       imshow("LK Demo", image);
//	if( out.isOpened()) { pktsout++ ; out.write(image); }

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

        std::swap(points[1], points[0]);
        cv::swap(prevGray, gray);
    }
    printf("%d Packets out\n", pktsout)  ;

    return 0;
}
