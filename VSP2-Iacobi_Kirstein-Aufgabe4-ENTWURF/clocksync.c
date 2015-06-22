/* clocksync.c
 *
 * HAW VS Praktikum, Versuch 4, Gruppe 2
 * Katja Kirstein 2125137 katja.kirstein@haw-hamburg.de, 
 * Jannik Iacobi 2144481 jannik.iacobi@haw-hamburg.de
 * 
 * Sources: Sniffer-Archive by Prof. Heitmann
 *          cplusplus.com
 */
//#define DEBUG
 
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "datagram.h"


#define gettid() syscall(__NR_gettid)
#define sigev_notify_thread_id _sigev_un._tid

#define PRIO 80
#define POLICY SCHED_FIFO
//#define POLICY SCHED_RR
//#define POLICY SCHED_OTHER
#define CLOCK CLOCK_MONOTONIC

#define ARG_COUNT 4
#define TIME_START_WAIT_SUPERFRAMES 3

typedef enum {START,AWAIT_BEACON,AWAIT_SLOT_TIME} time_state;

/*
 *
 */
int main(int argc, char** argv) {

    time_state state = START;

    struct timespec now;

    int fd;
    char* sourceaddr;
    int sourceport;

    char buf[1024];
    char output[1024];

    int rc;
    int signr;
    struct sigevent sigev;
    timer_t timer;
    struct sched_param schedp;
    sigset_t sigset;
    uint64_t superframeStartTime;
    uint64_t timeOffset;
    uint64_t timeOffsetExtern;
    int finished;

    struct itimerspec tspec;

    unsigned int frameCounter;

    uint32_t beaconDelay;

    /* Parse parameters */
    if( argc != ARG_COUNT+1 ){
      printf("Usage: clocksync <hostname> <portnummer> <adresse> <slotnummer>\n");
      exit(1);
    }
    char hostname[128];
    strncpy(hostname, argv[1], 127);
    hostname[127] = '\0';
    int port = atoi( argv[2] );
    char * adresse = argv[3];
    int slotnummer = atoi( argv[4] );
    uint64_t timeOffsetMidOwnSlot = (20LL /*Beacon-Fenster*/ + 4 /*Sicherheitspause*/ 
      + (slotnummer-1)*4 /*Zeit bis zu eigenem slot*/ + (4/2) /*halbe slotlaenge*/);
    
    printf("starting clocksync as: %s, at %s:%d with slotnummer %d\n",hostname,adresse,port,slotnummer);

    //Initialisiere Socket.
    //Trete der Multicast-Gruppe bei
    //Aktiviere Signal SIGIO
    fd = initSocket( adresse, port );
    if( fd < 0 ){
      printf("Failed initializing socket at: %s:%d.", adresse, port);
      exit(1);
    }

    //Definiere Ereignis fuer den Timer
    //Beim Ablaufen des Timers soll das Signal SIGALRM
    //an die aktuelle Thread gesendet werden.
    sigev.sigev_notify = SIGEV_THREAD_ID | SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    sigev.sigev_notify_thread_id = gettid();

    //Erzeuge den Timer
    timer_create(CLOCK, &sigev, &timer);




    //Umschaltung auf Real-time Scheduler.
    //Erfordert besondere Privilegien.
    //Deshalb hier deaktiviert.
    /*
    memset(&schedp, 0, sizeof (schedp));
    schedp.sched_priority = PRIO;
    sched_setscheduler(0, POLICY, &schedp);
    */


    //Lege fest, auf welche Signale beim
    //Aufruf von sigwaitinfo gewartet werden soll.
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGIO);                  //Socket hat Datagramme empfangen
    sigaddset(&sigset, SIGALRM);                //Timer ist abgelaufen
    sigaddset(&sigset, SIGINT);                 //Cntrl-C wurde gedrueckt
    sigprocmask(SIG_BLOCK, &sigset, NULL);


    //Framecounter initialisieren
    frameCounter = 0;
    superframeStartTime = 0;

    //Differenz zwischen der realen Zeit und der synchronisierten Anwendungszeit.
    //Die synchronisierte Anwendungszeit ergibt sich aus der Beaconnummer.
    //Sie wird gerechnet vom Startzeitpunkt des Superframes mit der Beaconnummer 0
    timeOffset = 0;
    timeOffsetExtern = 0;

    /* wait 3 superframes for beacon from other units !TODO: check!*/
    clock_gettime(CLOCK, &now);
    tspec.it_interval.tv_sec = 0;
    tspec.it_interval.tv_nsec = 0;
    nsec2timespec( &tspec.it_value, TIME_START_WAIT_SUPERFRAMES * 100LL /*msec*/ *1000*1000 + timespec2nsec(&now));
    timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
    printf("Waiting for first beacon.\n");
    
    //Merker fuer Programmende
    finished = 0;
    while( finished == 0 ){

        //Lese empfangene Datagramme oder warte auf Signale
        //Diese Abfrage ist ein wenig tricky, da das I/O-Signal (SIGIO)
        //flankengesteuert arbeitet.
        signr=0;
        while( signr == 0 ){
          //Pruefe, ob bereits Datagramme eingetroffen sind.
          //Die muessen erst gelesen werden, da sonst fuer diese kein SIGIO-Signal ausgeloest wird.
          //Signal wird erst gesendet beim Uebergang von Non-Ready nach Ready (Flankengesteuert!)
          //Also muss Socket solange ausgelesen werden, bis es Non-Ready ist.
          //Beachte: Socket wurde auf nonblocking umgeschaltet.
          //Wenn keine Nachricht vorhanden ist, kehrt Aufruf sofort mit -1 zurueck. errno ist dann EAGAIN.
          rc = recvMessage( fd, buf, sizeof(buf), &sourceaddr, &sourceport );
          if( rc > 0 ){
            //Ok, Datagram empfangen. Beende Schleife
            signr = SIGIO;
            break;
          }
          //Warte auf ein Signal.
          //Die entsprechenden Signale sind oben konfiguriert worden.
          siginfo_t info;
          if (sigwaitinfo(&sigset, &info) < 0){
            perror( "sigwait" );
            exit(1);
          }
          if( info.si_signo == SIGALRM ){
            //Timer ist abgelaufen
            signr = SIGALRM;
            break;
          }else if( info.si_signo == SIGINT ){
            //Cntrl-C wurde gedrueckt
            signr = SIGINT;
            break;
          }
        }

        //So, gueltiges Ereignis empfangen.
        //Nun geht es ans auswerten.
        /* Get current time */
        clock_gettime(CLOCK, &now);

#ifdef DEBUG
            printf("received signal %d at state %d.\n",signr,state);
#endif // DEBUG
        switch(state){
          case START:
            switch(signr){
              case SIGALRM:
                // no beacon received, I'm first to arrive
                printf("no beacon arrived within %d superframes, so this must be the first unit.\n",TIME_START_WAIT_SUPERFRAMES);

                // configure timer for next slottime
                tspec.it_interval.tv_sec = 0;
                tspec.it_interval.tv_nsec = 0;
                superframeStartTime = timespec2nsec(&now);
                nsec2timespec( &tspec.it_value, superframeStartTime + timeOffsetMidOwnSlot);
                timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                
                timeOffset = timespec2nsec(&now); // init offset time
                state = AWAIT_SLOT_TIME;
                break; // case SIGALRM
              case SIGIO:
                if( buf[0] == 'B' ){
                  rc = decodeBeacon( buf, &frameCounter, &beaconDelay, NULL, 0);
                  if( rc < 0 ){
                    printf( "### Invalid Beacon: '%s'\n", buf );
                  } else {
                    printf("beacon arrived, this is not the first unit.\n");
                    //Berechne den Zeitpunkt, an dem der Superframe begann
                    superframeStartTime = timespec2nsec( &now ) - beaconDelay;

                    //Starte Zeitmessung mit dem ersten empfangenen Beacon
                    //Differenz zwischen der realen Zeit und der synchronisierten Anwendungszeit.
                    //Die synchronisierte Anwendungszeit ergibt sich aus der Beaconnummer.
                    //Sie wird gerechnet vom Startzeitpunkt des Superframes mit der Beaconnummer 0
                    timeOffset = superframeStartTime - frameCounter * 100LL /* msec */ * 1000 * 1000;

                    // configure timer for next slottime
                    tspec.it_interval.tv_sec = 0;
                    tspec.it_interval.tv_nsec = 0;
                    nsec2timespec( &tspec.it_value, superframeStartTime + timeOffsetMidOwnSlot);
                    timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                    
                    state = AWAIT_SLOT_TIME;
                  }
                } else {
                  printf("### Received Message is no beacon: '%s'\n", buf );
                }
                break; // case SIGIO
              case SIGINT:
                printf("received SIGINT, shutting down.\n");
                finished = 1;
                break; // case SIGINT
            } // switch signr
            break; // case START
          
          case AWAIT_BEACON:
            switch(signr){
              case SIGALRM:
                // send beacon, since no beacon arrived before
                frameCounter++;
                // send own beacon
                encodeBeacon(output, sizeof(output), frameCounter, beaconDelay, hostname);
                sendMessage(fd, output, adresse, port);
                
                superframeStartTime = (frameCounter * 100LL /* msec */*1000*1000) + timeOffset;
					
                // configure timer for next slottime
                tspec.it_interval.tv_sec = 0;
                tspec.it_interval.tv_nsec = 0;
                nsec2timespec( &tspec.it_value, superframeStartTime + timeOffsetMidOwnSlot);
                timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                
                state = AWAIT_SLOT_TIME;
                break; // case SIGALRM
              case SIGIO:
                if( buf[0] == 'B' ){
                  rc = decodeBeacon( buf, &frameCounter, &beaconDelay, NULL, 0);
                  if( rc < 0 ){
                    printf( "### Invalid Beacon: '%s'\n", buf );
                  } else {
                    printf("beacon arrived before sending own beacon.\n");
                    timeOffsetExtern = timespec2nsec(&now) - (frameCounter * 100LL /* msec */*1000*1000) - beaconDelay;
                    
                    // check, if other starttime is before own calculated offset and adjust offset if needed
                    if(timeOffsetExtern < timeOffset){
                      timeOffset = timeOffsetExtern;
                    }
                    
                    //Berechne den Zeitpunkt, an dem der Superframe begann
                    superframeStartTime = (frameCounter * 100LL /* msec */*1000*1000) + timeOffset;

                    // configure timer for next slottime
                    tspec.it_interval.tv_sec = 0;
                    tspec.it_interval.tv_nsec = 0;
                    nsec2timespec( &tspec.it_value, superframeStartTime + timeOffsetMidOwnSlot);
                    timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                    
                    state = AWAIT_SLOT_TIME;
                  }
                } else {
#ifdef DEBUG
            printf("### Received Message is no beacon: '%s'\n",buf);
#endif // DEBUG
                }
                break; // case SIGIO
              case SIGINT:
                printf("received SIGINT, shutting down.\n");
                finished = 1;
                break; // case SIGINT
            } // switch signr
            break; // case AWAIT_BEACON
          
          case AWAIT_SLOT_TIME:
            switch(signr){
              case SIGALRM:
                // send own datagram
                encodeSlotMessage(output, sizeof(output), slotnummer, hostname);
                sendMessage(fd, output, adresse, port);
                
                // calculate own random delay for next beacon
                beaconDelay = randomNumber(20 /*msec*/ *1000*1000);
                
                // configure timer for next beacon send time
                tspec.it_interval.tv_sec = 0;
                tspec.it_interval.tv_nsec = 0;
                nsec2timespec( &tspec.it_value, superframeStartTime + 100LL /*msec*/ *1000*1000 + beaconDelay);
                timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                
                state = AWAIT_BEACON;
                break; // case SIGALRM
              case SIGIO:
                //TODO: maybe error message?
                break; // case SIGIO
              case SIGINT:
                printf("received SIGINT, shutting down.\n");
                finished = 1;
                break; // case SIGINT
            } // switch signr
            break; // case AWAIT_SLOT_TIME
        } // switch state
    } // while finished == 0

    ////////////////////////////////////////////////////

    //und aufraeumen
    timer_delete(timer);


    /* switch to normal */
    schedp.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &schedp);



    return 0;
}

