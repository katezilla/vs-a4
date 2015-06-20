
O=0

sniffer: sniffer.o datagram.o
	gcc -g -Wall sniffer.o datagram.o -l rt -o sniffer

all: clocksync sniffer

clocksync: clocksync.o datagram.o utils.o
	gcc  -g -Wall clocksync.o datagram.o utils.o -l rt -o clocksync


sniffer.o: sniffer.c datagram.h
	gcc -D_GNU_SOURCE -O$(O) -c -g -Wall sniffer.c -o sniffer.o

datagram.o: datagram.c datagram.h
	gcc -D_GNU_SOURCE -O$(O)  -c -g -Wall datagram.c -o datagram.o

utils.o: utils.c utils.h
	gcc -O$(O)  -c -g -Wall utils.c -o utils.o


clocksync.o: clocksync.c datagram.h utils.h
	gcc -D_GNU_SOURCE -O$(O) -c -g -Wall clocksync.c -o clocksync.o




clean:
	rm  *.o sniffer clocksync
