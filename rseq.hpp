#ifndef RSEQ_H
#define RSEQ_H

#define RSEQSIZE 5

static unsigned int rseq[] = {
1838,
2305,
4664,
907,
9582,
4115,
1530,
4280,
5738,
8747,
5795,
5818,
7640,
} ;

typedef unsigned int u32 ;
typedef struct {
	u32 count;
	u32 tstmp;
	u32 seqsize;
	u32 seq[RSEQSIZE] ;
	u32 checksum;
} tag_t ;

#endif
