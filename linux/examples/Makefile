CC=gcc
CUSTOM_CFLAGS=-Wall -ggdb3 -O3
SYS_CFLAGS=-std=gnu99 `pkg-config --cflags libcometa`
LIBS=`pkg-config --libs libcometa` -lpthread

OBJS=cometa-client.o

all: cometa-client

cometa-client: cometa-client.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o cometa-client

install:

-include ../../Makefile.lib
