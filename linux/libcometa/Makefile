CC=gcc
PREFIX=/usr/local
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib
PKG_CONFIG_PATH=$(LIBDIR)/pkgconfig

INSTALL=install

# Compile using -DUSE_SSL to use SSL
CUSTOM_CFLAGS=-Wall -ggdb3 -O3 # -DUSE_SSL

SOFLAGS=-fPIC 
#SOFLAGS=

# compile with DEBUG=1 to enable debug code 
SYS_CFLAGS=-std=gnu99 $(SOFLAGS) -I. -pthread -DDEBUG=1

LIBS=`pkg-config --libs libcrypto`

#LDFLAGS=$(SOFLAGS) -shared -Wl,libcometa.so.0
LDFLAGS=$(SOFLAGS) -shared -Wl,-soname,libcometa.so.0

CFLAGS=$(CUSTOM_CFLAGS) $(SYS_CFLAGS)

OBJS=cometa.o http_parser.o

all: libcometa.so.0.1 libcometa.pc

libcometa.so.0.1: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

libcometa.pc: libcometa.pc.in
	sed -e 's#@LIBDIR@#$(LIBDIR)#g; s#@INCDIR@#$(INCDIR)#g' $^ >$@

clean:
	rm -f *.o libcometa.so.0.1 libcometa.pc

install:
	$(INSTALL) -D -m 0644 cometa.h $(DESTDIR)$(INCDIR)/cometa.h
	$(INSTALL) -D -m 0755 libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0.1
	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0
	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so
	$(INSTALL) -D -m 0644 libcometa.pc $(DESTDIR)$(LIBDIR)/pkgconfig/libcometa.pc
	ldconfig        

-include ../Makefile.lib
