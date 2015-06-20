/*
 */

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




/*
 *
 */
int main(int argc, char** argv) {

    struct timespec now;

    int fd;
    char* sourceaddr;
    int sourceport;

    char buf[1024];
    char buftmp[1024];
    char output[1024];

    int rc;
    int signr;
    struct sigevent sigev;
    timer_t timer;
    struct sched_param schedp;
    sigset_t sigset;
    uint64_t superframeStartTime;
    int64_t superframeStartTimeError;
    uint64_t timeOffset;
    uint64_t nsecNow;
    int finished;

    struct itimerspec tspec;

    unsigned int frameCounter;
    unsigned int lastFrameCounter;

    uint32_t beaconDelay;
    char hostname[128];

    FILE* file;

    if( argc != 2 ){
      printf("Usage: sniffer <portnummer>\n");
      exit(1);
    }
    int port = atoi( argv[1] );

    //Initialisiere Socket.
    //Trete der Multicast-Gruppe bei
    //Aktiviere Signal SIGIO
    fd = initSocket( "233.0.0.1", port );
    if( fd < 0 ){
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


    //Erzeuge Datei zum Mittschnitt der Ereignisse
    file = fopen( "capture.dat", "w" );
    if( file == NULL ){
      perror( "fopen" );
      exit(1);
    }


    //Framecounter initialisieren
    frameCounter = 0;
    lastFrameCounter = 0;
    superframeStartTime = 0;

    //Differenz zwischen der realen Zeit und der synchronisierten Anwendungszeit.
    //Die synchronisierte Anwendungszeit ergibt sich aus der Beaconnummer.
    //Sie wird gerechnet vom Startzeitpunkt des Superframes mit der Beaconnummer 0
    timeOffset = 0;

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

        switch( signr ){
          case SIGALRM:
            //Timer ist abgelaufen.
            //hier keine Aktion.
            if( frameCounter != lastFrameCounter+1 ){
              //Fehler aufgetreten
              nsecNow = timespec2nsec( &now ) - timeOffset;
              printf( "***: %11.6f  Alarm: Kein Beacon empfangen!!\n", (nsecNow)/1.e9 );

              //Alles zuruecksetzen
              timeOffset = 0;
            } else {
              lastFrameCounter = frameCounter;

              //Konfiguriere Alarm zur Ueberwachung des naechsten Beacons
              //Alarm wird so eingestellt, dass vor Ablauf des Alarms
              //mindestens ein Beacon aufgetreten sein soll!
              tspec.it_interval.tv_sec = 0;
              tspec.it_interval.tv_nsec = 0;
              nsec2timespec( &tspec.it_value, superframeStartTime + (100LL + 20 + 4) /*msec*/ *1000*1000 );
              timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
            }
            break;
          case SIGINT:
            //Cntrl-C wurde gedrueckt.
            //Programm beenden.
            finished = 1;
            break;
          case SIGIO:
            //Datagramm empfangen.

            if( buf[0] == 'B' ){
              //Empfangenes Datagram ist ein Beacon
              rc = decodeBeacon( buf, &frameCounter, &beaconDelay, hostname, sizeof(hostname) );
              if( rc < 0 ){
                printf( "### Invalid Beacon: '%s'\n", buf );
              } else {
                //Berechne den Zeitpunkt, an dem der Superframe begann
                superframeStartTime = timespec2nsec( &now ) - beaconDelay;

                //Starte Zeitmessung mit dem ersten empfangenen Beacon
                if( timeOffset == 0 ){
                  //Differenz zwischen der realen Zeit und der synchronisierten Anwendungszeit.
                  //Die synchronisierte Anwendungszeit ergibt sich aus der Beaconnummer.
                  //Sie wird gerechnet vom Startzeitpunkt des Superframes mit der Beaconnummer 0
                  timeOffset = superframeStartTime - frameCounter * 100LL /* msec */ * 1000 * 1000;

                  lastFrameCounter = frameCounter;


                  //Konfiguriere Alarm zur Ueberwachung des naechsten Beacons
                  tspec.it_interval.tv_sec = 0;
                  tspec.it_interval.tv_nsec = 0;
                  nsec2timespec( &tspec.it_value, timeOffset + ((frameCounter+1)*100LL + 20 + 4) /*msec*/ *1000*1000 );
                  timer_settime(timer, TIMER_ABSTIME, &tspec, NULL);
                }

                //Berechne nsec seit dem Empfang des ersten Beacons
                nsecNow = timespec2nsec( &now ) - timeOffset;

                //Berechne den Fehler zwischen dem tatsaechlichen Startzeitpunkt des Superframes und dem erwarteten Zeitpunkt
                superframeStartTimeError = superframeStartTime - timeOffset - frameCounter * 100LL /* msec */ * 1000 * 1000;

                snprintf( buftmp, sizeof(buftmp), "'%s'", buf );
                snprintf( output, sizeof(output), "---: %11.6f %-37s %9.3f\n", (nsecNow)/1.e9, buftmp, superframeStartTimeError/1.e6 );
                fputs( output, stdout );
                fputs( output, file );


              }
            } else if( buf[0] == 'D' ){
              //Empfangenes Datagram ist Slot Message

              //Berechne nsec seit dem Empfang des ersten Beacons
              nsecNow = timespec2nsec( &now ) - timeOffset;
              snprintf( buftmp, sizeof(buftmp), "'%s'", buf );
              snprintf( output, sizeof(output), "   : %11.6f %s\n", (nsecNow)/1.e9, buftmp );
              fputs( output, stdout );
              fputs( output, file );
            } else {
              //Unknown Message
              snprintf( output, sizeof(output), "### Unknown Message: '%s'\n", buf );
              fputs( output, stdout );
              fputs( output, file );
            }

            break;
        }
    }

    ////////////////////////////////////////////////////

    fclose( file );

    //und aufraeumen
    timer_delete(timer);


    /* switch to normal */
    schedp.sched_priority = 0;
    sched_setscheduler(0, SCHED_OTHER, &schedp);



    return 0;
}

