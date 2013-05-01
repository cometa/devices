CUSTOM_CFLAGS=-Wall -ggdb3 -O3
SYS_CFLAGS=-std=gnu99 `pkg-config --cflags libcometa`
LIBS=`pkg-config --libs libcometa`

OBJS=cometa-client.o

all: cometa-client

cometa-client: cometa-client.o
	$(call cmd,link)

clean:
	rm -f *.o cometa-client

install:

-include ../../Makefile.lib