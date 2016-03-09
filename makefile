CC     = g++
CFLAGS = -g
INCLUDES = ether.h ip.h
CFLAGS = -c -g

all: bridge station 

bridge: bridge.o ether.o lan.o 
	$(CC) $(CFLAGS) bridge.o ether.o lan.o -o bridge

station: station.o ether.o ypage.o lan.o ip.o 
	$(CC) $(CFLAGS) station.o ether.o ypage.o lan.o ip.o -o station

clean : 
	rm -f bridge station *.o

%.o : %.c $(INCLUDES)
	$(CC) $(CFLAGS) $<

