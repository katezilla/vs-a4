/*
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


#include "datagram.h"


///////////////////////////////////////////////////////////////////////////
/**
 */
int randomNumber(unsigned int limit){
    unsigned int n;
    unsigned int mask = 0xFFFFFFFF;

    while(mask > limit * 2) mask >>= 1;

    do {
        n = rand();
        n &= mask;
    } while (n >= limit);

    return n;
}



///////////////////////////////////////////////////////////////////////////
/**
 */
uint64_t timespec2nsec( struct timespec *ts ){
  uint64_t t = ts->tv_nsec + ts->tv_sec*1000000000LL;
  return t;
}


///////////////////////////////////////////////////////////////////////////
/**
 */
void nsec2timespec(struct timespec *ts, uint64_t nsec ){
  ts->tv_sec = nsec / 1000000000LL;
  ts->tv_nsec = nsec - ts->tv_sec * 1000000000LL;
}

///////////////////////////////////////////////////////////////////////////
/**
 */
int decodeBeacon( const char* datagram, unsigned int* frameNr, unsigned int* beaconDelay, char* hostname, int hostnamelen ){
  char* strend;

  if( datagram[0] != 'B' ){
    return -1;
  }

  *frameNr = strtoul( &datagram[1], &strend, 10 );
  if( *strend != ':' ){
    return -2;
  }

  *beaconDelay = strtoul( &strend[1], &strend, 10 );
  if( *strend != ':' ){
    return -3;
  }

  strncpy( hostname, &strend[1], hostnamelen );

  return 0;
}

///////////////////////////////////////////////////////////////////////////
/**
 */
int encodeBeacon( char* datagram, int datagramsize,  unsigned int frameNr, unsigned int beaconDelay, char* hostname ){
  int rc;

  rc = snprintf( datagram, datagramsize, "B%u:%u:%s", frameNr, beaconDelay, hostname );
  if( rc < 0 || rc >= datagramsize ){
    return -1;
  }

  return rc;
}

///////////////////////////////////////////////////////////////////////////
/**
 */
int decodeSlotMessage( const char* datagram, int slot, const char* hostname  ){
  return 0;
}



///////////////////////////////////////////////////////////////////////////
/**
 */
int encodeSlotMessage( char* datagram, int datagramsize, int slot, const char* hostname ){
  int rc;

  rc = snprintf( datagram, datagramsize, "D%d:%s", slot, hostname );
  if( rc < 0 || rc >= datagramsize ){
    return -1;
  }

  return rc;
}



///////////////////////////////////////////////////////////////////////////
/**
 */
int sendMessage( int fd, const char* message, const char* mcastAdr, int port ){
  struct sockaddr_in  sa;
  int                 salen;
  int                 rc;


  //send telegram
  memset( &sa, 0, sizeof(sa) );
  sa.sin_family      = AF_INET;
  inet_aton( mcastAdr, &sa.sin_addr );
  sa.sin_port        = htons( port );
  salen = sizeof( struct sockaddr_in );

  rc = sendto( fd, message, strlen(message),
                            0, (struct sockaddr*)&sa, salen );
  if( rc<0 ) {
    perror( "sendto" );
    exit(1);
  }

  return 0;
}



///////////////////////////////////////////////////////////////////////////
/**
 */
int recvMessage( int fd, char* message, int size, char** mcastAdr, int *port ){
  struct sockaddr_in  sa;
  socklen_t           salen;
  int                 rc;


  memset( &sa, 0, sizeof(sa) );
  sa.sin_family      = AF_INET;
  salen = sizeof( struct sockaddr_in );

  rc = recvfrom( fd, message, size-1,
                            0, (struct sockaddr*)&sa, &salen );
  if( rc<0 ) {
    if( errno == EAGAIN ){
      return 0;
    } else {
      perror( "recvfrom" );
      return -1;
    }
  }
  *mcastAdr = inet_ntoa( sa.sin_addr );
  *port     = ntohs( sa.sin_port );

  //add terminating 0
  message[rc] = 0;
  return rc;
}

///////////////////////////////////////////////////////////////////////////
/**
 */
int initSocket( const char* mcastAdr, int port ){
  struct sockaddr_in  sa;
  int                 salen;
  int                 rc;
  struct ip_mreq      req;
  int                 fd;

  //Create Socket
  fd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
  if( fd < 0 ){
    perror( "socket" );
    return -1;
  }
  
  int option = 1;
  setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option) );


  //Bind to specified port
  bzero( &sa, sizeof(sa) );
  sa.sin_family      = AF_INET;
  inet_aton( mcastAdr, &sa.sin_addr );
  sa.sin_port        = htons( port );
  salen = sizeof( struct sockaddr_in );

  rc = bind( fd, (struct sockaddr*)&sa, salen );
  if( rc<0 ){
    perror( "bind" );
    return -1;
  }

  //Join multicast group
  memset( &req, 0, sizeof( req ) );
  req.imr_multiaddr.s_addr = sa.sin_addr.s_addr;
  req.imr_interface.s_addr = htonl( INADDR_ANY );
  if( setsockopt( fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof ( req ) ) ) {
      perror( "Could not join multicast group: " );
      return -1;
  }

  fcntl(fd,F_SETOWN,getpid() );
  fcntl(fd,F_SETSIG,0);
  fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK|O_ASYNC);

  return fd;
}

