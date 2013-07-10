CC=gcc
CUSTOM_CFLAGS=-Wall -ggdb3 -O3
SYS_CFLAGS=-std=gnu99 `pkg-config --cflags libcometa`
LIBS=`pkg-config --libs libcometa` -lpthread -lssl -lcrypto

OBJS=cometa-client.o client.o

all: cometa-client client

cometa-client: cometa-client.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

client: client.o json.o hmacsha.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) -levent -lcrypto -lm


clean:
	rm -f *.o cometa-client

install:

-include ../../Makefile.lib
