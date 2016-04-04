CC     = g++
GFLAGS = -g
INCLUDES = ether.h ip.h
CFLAGS = -c -g

all: bridge station 

bridge: bridge.o ether.h
	$(CC) $(GFLAGS) bridge.o ether.h -o bridge

station: station.o ether.h ip.h 
	$(CC) $(GFLAGS) station.o ether.h ip.h -o station

clean : 
	rm -f bridge station *.o .*.addr .*.port

%.o : %.c $(INCLUDES)
	$(CC) $(CFLAGS) $<

