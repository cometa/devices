CC=gcc
CUSTOM_CFLAGS=-Wall -ggdb3 -O3
SYS_CFLAGS=-std=gnu99 `pkg-config --cflags libcometa`
LIBS=`pkg-config --libs libcometa` -lpthread `pkg-config --libs libcrypto` -lssl

PKG_CONFIG_PATH=$(LIBDIR)/pkgconfig

CFLAGS=$(CUSTOM_CFLAGS) $(SYS_CFLAGS)

OBJS=cometa-client.o

all: cometa-client

cometa-client: cometa-client.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f *.o cometa-client

install:

-include ../Makefile.lib
