# include <stdio.h>
#include <math.h>
#include <gsl/gsl_vector.h>

#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "precomp.hpp"
#include <opencv2/imgproc.hpp>
#include "foe.hpp"
#include <gst/gst.h>
//#include "dsopencv.hpp"

int foeDebug ;
using namespace cv;
using namespace std;

namespace foe {
double foeDistanceLine(Point2f pt, line_t l)
{
	double d = ((l.pt2.y - l.pt1.y)*pt.x - (l.pt2.x - l.pt1.x)*pt.y + l.pt2.x*l.pt1.y - l.pt2.y*l.pt1.x)/
		sqrt(pow(l.pt2.y - l.pt1.y,2.0) + pow(l.pt2.x - l.pt1.x,2.0)) ;
	if (foeDebug) { printf("foeDistance:Distance from %.4f:%.4f to line (%.4f:%.4f,%.4f:%.4f) is %.4f\n",
			pt.x,pt.y,
			l.pt1.x,l.pt1.y,l.pt2.x,l.pt2.y,
			d); 
	}
	return d ;
}

double foeDistancePt(Point2f pt, Point2f pt2)
{
	double d = sqrt(pow(pt.x - pt2.x,2.0) + pow(pt.y - pt2.y,2.0)) ;
	return d ;
}

double foeDistGradient(Point2f pt, line_t l, double *delx, double *dely)
{
	double den = sqrt(pow(l.pt2.y - l.pt1.y,2.0) + pow(l.pt2.x - l.pt1.x,2.0)) ;
	double dist = foeDistanceLine(pt,l) ;
	*delx = (l.pt2.y - l.pt1.y)*2.0*dist/den ;
	*dely = -(l.pt2.x - l.pt1.x)*2.0*dist/den;
}

#include <unistd.h>
#include <assert.h>
#define sgn(a) ((a) > 0 ? 1:-1)
line_t foeLineFromPoints(Point2f p1, Point2f p2)
{
	line_t l ;
	if (foeDebug) {
		printf("Converting from pts %.3f:%.3f %.4f:%.4f to line",
				p1.x,p1.y,p2.x,p2.y) ;
		}
	l.pt1 = p1 ;
	l.pt2 = p2 ;
	l.wt = sqrt(pow(p1.x - p2.x,2.0)  + pow(p1.y - p2.y,2.0)) ;
	return l;
}

double foeDistVal(const gsl_vector *x, void *params)
{
	lineset_t *s = (lineset_t *)params;
	Point2f p;
	int ctr;
	double rv=0;

	p.x = gsl_vector_get(x,0) ;
	p.y = gsl_vector_get(x,1);
	for (ctr=0; ctr<s->n; ctr++) {
		rv += pow(foeDistanceLine(p,s->line[ctr]),2.0)/(double)(s->n) ;
	}
	if (foeDebug) printf("Distance Value:%.4g\n",rv) ;
	return rv;
}

void foeDistGradientV(const gsl_vector *x, void *params, gsl_vector * grad)
{
	lineset_t *s = (lineset_t *)params;
	Point2f p;
	int ctr;
	double rv=0;
	double delx,dely;

	p.x = gsl_vector_get(x,0) ;
	p.y = gsl_vector_get(x,1);
	delx = dely= 0;
	for (ctr=0; ctr<s->n; ctr++) {
		double dx,dy;
		foeDistGradient(p,s->line[ctr],&dx,&dy) ;
		if (foeDebug) printf("Gradient for line %d: %.4g %.4g\n",ctr,dx,dy) ;
		delx += dx ;
		dely += dy ;
	}
	gsl_vector_set(grad,0,delx) ;
	gsl_vector_set(grad,1,dely) ;
	if (foeDebug) printf("Distance Gradient Value:%.4g:%.4g\n",gsl_vector_get(grad,0), gsl_vector_get(grad,1)) ;
	return;
}

void foeDistCombined(const gsl_vector *x, void *params, double *rv, gsl_vector *grad)
{
	lineset_t *s = (lineset_t *)params;
	Point2f p;
	int ctr;
	double delx,dely;

	p.x = gsl_vector_get(x,0) ;
	p.y = gsl_vector_get(x,1);
	for (ctr=0; ctr<s->n; ctr++) {
		*rv += pow(foeDistanceLine(p,s->line[ctr]),2.0) ;
		delx = dely= 0;
		foeDistGradient(p,s->line[ctr],&delx,&dely) ;
		gsl_vector_set(grad,0,gsl_vector_get(grad,0)+delx) ;
		gsl_vector_set(grad,1,gsl_vector_get(grad,1)+dely) ;
	}
	if (foeDebug) printf("Distance Value:%.4g\n",*rv) ;
	if (foeDebug) printf("Distance Gradient Value:%.4g:%.4g\n",gsl_vector_get(grad,0), gsl_vector_get(grad,1)) ;
	return;
}

#define MINDISTANCE 1.00000001
#define MAXLINES 512
#include <gsl/gsl_multimin.h>
int lineCompare(void *la, void *lb)
{
	line_t *pl1 = (line_t *)la ;
	line_t *pl2 = (line_t *)lb ;
	double d1 = foeDistancePt(pl1->pt1,pl1->pt2) ;
	double d2 = foeDistancePt(pl2->pt1,pl2->pt2) ;
	if (d1 > d2) return -1 ;
	else if (d1 < d2) return 1 ;
	else return 0 ;
}

gsl_multimin_function_fdf * foeEstimatorInit(int nPoints, vector<Point2f> pts, vector<Point2f> predicted)
{
	int ctr,npts,nline;
	static char selector[MAXLINES];
	double distance ;
	{
		int it,it2; double tdist;
		if (foeDebug) printf("Input vectors\n") ;
		for (it=0,npts=0; it<nPoints; it++) {
			distance = foeDistancePt(pts[it],predicted[it]) ;
			if (foeDebug)
				printf("PT: %.4g:%.4g PREDL%.4g,%.4g dist=%.4g\n",pts[it].x,pts[it].y, predicted[it].x,predicted[it].y,distance) ;
			if (distance > MINDISTANCE) {
				npts++ ;
				selector[it] = 1 ;
			}
			else 
				selector[it] = 0 ;
		}
	}
	printf("Selected %d points out of a total of %d\n",npts,nPoints); 
	if (npts < 2) return NULL ;

	gsl_multimin_function_fdf *f = (gsl_multimin_function_fdf *)malloc(sizeof(gsl_multimin_function_fdf)) ;
	f->n = 2;
	f->f = foeDistVal ;
	f->df = foeDistGradientV ;
	f->fdf = foeDistCombined ;

	lineset_t * ls= (lineset_t *)malloc(sizeof(lineset_t)) ;
	ls->n = npts ;
	ls->line = (line_t *)calloc(npts,sizeof(line_t)) ;
	for (ctr = nline = 0; ctr < nPoints && nline < npts;ctr++) {
		if (selector[ctr] == 1)
			ls->line[nline++] = foeLineFromPoints(pts[ctr],predicted[ctr]) ;
	}
	qsort(ls->line,npts,sizeof(line_t),lineCompare) ;
	int it;
	for (it=0; it<npts; it++) {
		distance = foeDistancePt(ls->line[it].pt1, ls->line[it].pt2) ;
		if (foeDebug)
			printf("PT: %.4g:%.4g PREDL %.4g,%.4g dist=%.4g\n",
					ls->line[it].pt1.x, ls->line[it].pt1.y,
					ls->line[it].pt2.x, ls->line[it].pt2.y,
					distance ) ;
	}
	f->params = ls ;
	return f ;
}

Point2f foeEstimate(int nPoints, vector<Point2f> pts, vector<Point2f> pred, int width, int height) 
{
	gsl_multimin_function_fdf *foeMM = foeEstimatorInit(nPoints, pts, pred) ;
	gsl_multimin_fdfminimizer *s;
	size_t iter = 0;
	int status,i;
	const gsl_multimin_fdfminimizer_type *T;
	lineset_t *ls;

	if (foeMM == NULL) return Point2f(0,0) ;
	ls = (lineset_t *)foeMM->params ;

  	gsl_vector *x;
	Point2f rv;
  /* Starting point, x = (5,7) */
  	x = gsl_vector_alloc (2);
  	gsl_vector_set (x, 0, width/3);
  	gsl_vector_set (x, 1, height/3);

	T = gsl_multimin_fdfminimizer_conjugate_fr;
	s = gsl_multimin_fdfminimizer_alloc (T, 2);

	gsl_multimin_fdfminimizer_set (s, foeMM, x, 0.01, 1e-4);

	do
	{
		iter++;
		status = gsl_multimin_fdfminimizer_iterate (s);
		if (status)
		{
			printf("Failure in minimization:error code=%s!\n",gsl_strerror(status)) ;
			iter = 100;
		}

		status = gsl_multimin_test_gradient (s->gradient, 1e-3);

		printf ("Updated to: %5d %.5f %.5f %10.5f\n", 
					iter, gsl_vector_get (s->x, 0), gsl_vector_get (s->x, 1), s->f);
		if (status == GSL_SUCCESS)
		{
			printf ("Minimum found at: %5d %.5f %.5f %10.5f\n", 
					iter, gsl_vector_get (s->x, 0), gsl_vector_get (s->x, 1), s->f);
		}
	} while (status == GSL_CONTINUE && iter < 100);
#if 0
	for (i=0; i<ls->n; i++)
	{
            	int line_thickness = 4;
		/* CV_RGB(red, green, blue) is the red, green, and blue components
		 * of the color you want, each out of 255.  */	

		Scalar line_color = CV_RGB(255,129,0);
		Point2f p,q;
		p = ls->line[i].pt1 ;
		q = ls->line[i].pt2 ;
		line( img, p, q, line_color, line_thickness, 16 ,0 );   
	}
#endif
	rv.x = gsl_vector_get(s->x,0) ;
	rv.y = gsl_vector_get(s->x,1) ;
	gsl_multimin_fdfminimizer_free (s);
	gsl_vector_free (x);
	return rv;
}
    //Prepare the image for findContours

int foeDetectEdges(Mat image, char *op, int maxdata)
{
}
int foeDetectContours(Mat & gray, vector<cntr_t>& cntrsdb, int fno, double * maxsize,double * minsize, int threshval)
{
	Mat gray2,cannyOp;
	int i,j, tsize=0;
	char *pop;
	vector <Vec4i> hierarchy ;
	vector < vector<Point>> cntrs ;
	int np,tpoints = 0;
	int cthresh=100 ;
	double Msz , msz ;
	vector <Point> approx;
	double c_area ;
	int outsize=0;
	Msz = msz = -1 ;

	gray.copyTo(gray2) ;
	gray2.convertTo(gray2,CV_8UC1);
	cvtColor(gray2,gray2,COLOR_BGR2GRAY) ;
    	//threshold(gray2, cannyOp, threshval, 1, THRESH_BINARY);
//	Canny(gray2, cannyOp,cthresh, cthresh*2) ;
	adaptiveThreshold(gray2,cannyOp,1,ADAPTIVE_THRESH_GAUSSIAN_C,THRESH_BINARY,threshval,0) ;
//    findContours( cannyOp, cntrs, hierarchy, RETR_CCOMP, CHAIN_APPROX_SIMPLE );
    findContours( cannyOp, cntrs, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE );
    if (cntrs.empty()) cout << "No contours found\n" ;
    else cout << cntrs.size() << " contours found: Existing contours: " << cntrsdb.size() << "\n" ;

    if (cntrsdb.size() == 0) {
	    int added=0;
	    cntrsdb.resize(cntrs.size()) ;
	    //for (i=0; i >= 0; i = hierarchy[i][0] ) {
	//	if ((hierarchy[i][2] < 0 && hierarchy[i][3] < 0))
	//		continue ;
		for (i=0; i<cntrs.size(); i++) {
		approxPolyDP(cntrs[i],approx,arcLength(cntrs[i], true)*0.02,true) ;
		c_area = fabs(contourArea(approx,false)) ;
		if ( (*maxsize != -1 && *minsize != -1) && 
			(c_area > *maxsize || c_area < *minsize) )
		{
			outsize++ ;
			continue ;
		}
//		printf("Adding contour of area %g Msz=%g msz = %g\n",c_area,Msz,msz) ;
		if (Msz == -1 || c_area > Msz) Msz = c_area ;
		else if (msz == -1 || c_area < msz) msz = c_area ;
		cntrsdb[added].cntr = cntrs[i] ;
		cntrsdb[added].fframe = fno ;
		cntrsdb[added].lframe = fno ;
		added++;
	    }
	   cntrsdb.resize(added) ;
	    printf("Stored %u reference contours, %d contours eliminated due to size\n",cntrsdb.size(),outsize) ;
    }
    else {
	    int elim=0;
	    for (i=0; i<cntrs.size(); i++) {
		    double mingap=10;
		    int minpos = -1 ;
		c_area = 10 ;
		approxPolyDP(cntrs[i],approx,arcLength(cntrs[i], true)*0.02,true) ;
		c_area = fabs(contourArea(approx,false)) ;
		if ( (*maxsize != -1 && *minsize != -1) && 
			(c_area > *maxsize || c_area < *minsize) )
		{
			outsize++ ;
			continue ;
		}
//		printf("Testing contour of area %g max=%g min = %g\n",c_area,*maxsize,*minsize) ;
		if (Msz == -1 || c_area > Msz) Msz = c_area ;
		else if (msz == -1 || c_area < msz) msz = c_area ;
		    for (j=0; j<cntrsdb.size(); j++) {
			    double gap=matchShapes(cntrsdb[j].cntr,cntrs[i],CONTOURS_MATCH_I1,0) ;
			    if (fabs(gap) < fabs(mingap)) { mingap = gap ; minpos = j ; }
		    }
		    if (minpos != -1 && fabs(mingap) < 1) {
			cntrsdb[minpos].lframe = fno ;
		    }
		    else {
			cntr_t c ;
			c.cntr = cntrs[i] ;
			assert(c.cntr.size() > 0) ;
			c.fframe = fno ;
			c.lframe = fno ;
			cntrsdb.emplace(cntrsdb.end(),c) ;
			    elim++ ;
		    }
	    }
	    printf("Added %d contours (%d eliminated due to size) Msz=%g msz=%g\n", elim,outsize, Msz,msz) ;
    }
	uint cs = 0 ;
	for (auto it = cntrsdb.begin(); it != cntrsdb.end();) {
		if (it->lframe < fno - 30) {
			it = cntrsdb.erase(it) ;
			cs++ ;
		}
		else
			++it ;
	}
	printf("Cleaned %d contours maxarea = %g minarea=%g\n",cs,Msz,msz) ;
//	*maxsize = Msz ;
//	*minsize = msz ;
    return cntrsdb.size() ;


#if 0
    pop = op ;
    cntrs_s.resize(cntrs.size()) ;
    for (i=j=0; i<cntrs.size(); i++)
    {
	    if (cntrs[i].size() == 0) continue ;
	    double eps = 0.005*arcLength(cntrs[i],true) ;
	    approxPolyDP(cntrs[i],cntrs_s[j],eps,true) ;
	    tpoints += cntrs_s[j].size() ;
	    j++ ;
    }
    cntrs_s.resize(j) ;
    cout << "Total contours " << j << " contour points " << tpoints << " " ;
#endif
//    vector <Point2f> cntrpoints(tpoints) ;
//   for (i=0,np=0; i<cntrs_s.size(); i++) {
//	for (vector<Point>::iterator it = cntrs_s[i].begin() ; it != cntrs_s[i].end(); ++it)
//		cntrpoints[np++] = Point2f((float)it->x, (float)it->y) ;
//   }
//  cout << np << " points written\n" ;

//    Scalar fgcolor(240,113,113) ;
//    tsize = writeToArray(cntrpoints,pop,maxdata) ;
//	cvtColor(gray,gray,COLOR_GRAY2BGR) ;
//	drawContours(gray,cntrs_s,-1,fgcolor,2) ;
//    return tsize ;
}
} // Namespace foe
