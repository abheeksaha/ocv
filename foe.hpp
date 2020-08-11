#ifndef FOE_HPP
#define FOE_HPP

using namespace cv;
using namespace std;

namespace foe{
typedef struct {
	Point2f pt1;
	Point2f pt2;
	double wt;
} line_t ;

typedef struct {
	int n;
	line_t *line;
	int debug;
} lineset_t ;


Point2f foeEstimate(Mat img, int nPoints, vector<Point2f> pts, vector<Point2f> pred, int width, int height)  ;
int foeStage1(Mat image, char *op, int maxdata) ;
}
#endif
