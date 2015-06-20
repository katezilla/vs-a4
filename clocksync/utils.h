


struct log {
  struct timeval tv;
  int event;
};




void saveHistogram( struct log* log, int cnt, struct timeval* tv0, int histocnt, int histostep, int event, const char* filename );
void saveLog( struct log* log, int cnt, struct timeval* tv0, const char* filename );
