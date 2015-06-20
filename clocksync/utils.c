/*
 */

#include <sys/time.h>
#include <stdio.h>

#include <time.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "utils.h"






void saveHistogram( struct log* log, int cnt, struct timeval* tv0, int histocnt, int histostep, int event, const char* filename ){
    int i;
    int histo[ histocnt ];

    memset( histo, 0, sizeof(int)*histocnt);

    int t0 = 0;
    int j;
    for (i = 0; i < cnt; i++) {
      if( log[i].event == event ){
        int t = (log[i].tv.tv_sec - tv0->tv_sec)*1000000 + log[i].tv.tv_usec - tv0->tv_usec;
        //printf("%8d %8d \n", t, t - t0);

        j = (t-t0)/histostep;
        if( j >= histocnt ){
            j = histocnt-1;
        }
        histo[j] ++;
        t0 = t;
      }
    }

    FILE* fout = fopen(filename, "w");
    if( fout==NULL ){
      perror( "Histogram" );
      exit(1);
    }

    for( i=0; i<histocnt; i++ ){
        fprintf( fout, "%8d %8d\n", i*histostep, histo[i] );
    }
    fclose(fout);
    printf( "Histogramm in %s\n", filename);
}



void saveLog( struct log* log, int cnt, struct timeval* tv0, const char* filename ){
    int i;

    FILE* fout = fopen(filename, "w");
    if( fout==NULL ){
      perror( "Log" );
      exit(1);
    }


    for (i = 0; i < cnt; i++) {
        int t = (log[i].tv.tv_sec - tv0->tv_sec)*1000000 + log[i].tv.tv_usec - tv0->tv_usec;
        fprintf( fout, "%8d %2d\n", t, log[i].event );
    }

    fclose(fout);
    printf( "Log in %s\n", filename);
}





