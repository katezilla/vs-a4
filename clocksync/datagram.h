

#define MAXHOSTNAMELEN 21

struct beacon1 {
  uint64_t time;
  uint32_t ofs;
  char hostname[MAXHOSTNAMELEN];
};

int randomNumber(unsigned int limit);

uint64_t timespec2nsec( struct timespec *ts );
void nsec2timespec(struct timespec *ts, uint64_t nsec );

int decodeBeacon( const char* datagram, unsigned int* frameNr, unsigned int* difftime, char* hostname, int hostnamelen );
int encodeBeacon( char* datagram, int datagramsize,  unsigned int frameNr, unsigned int difftime, char* hostname );

int decodeSlotMessage( const char* datagram, int slot, const char* hostname  );
int encodeSlotMessage( char* datagram, int datagramsize, int slot, const char* hostname );

int sendMessage( int fd, const char* message, const char* mcastAdr, int port );
int recvMessage( int fd, char* message, int size, char** mcastAdr, int* port );

int initSocket( const char* mcastAdr, int port );
