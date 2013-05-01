PREFIX=/usr/local
INCDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib

# Compile using `make XCFLAGS=-DDEBUG=1` to enable debugging code.
CUSTOM_CFLAGS=-Wall -ggdb3 -O3

#SOFLAGS=-fPIC -fvisibility=internal
SOFLAGS=

#SYS_CFLAGS=-std=gnu99 $(SOFLAGS) -I. `pkg-config --cflags libcrypto`
SYS_CFLAGS=-std=gnu99 $(SOFLAGS) -I. -pthread -DDEBUG=1

#LIBS=`pkg-config --libs libcrypto`
LIBS=

#LDFLAGS=$(SOFLAGS) -shared -Wl,libcometa.so.0
LDFLAGS=$(SOFLAGS) -shared -Wl

OBJS=cometa.o

#all: libcometa.so.0.1 
all: libcometa.so.0.1 libcometa.pc

libcometa.so.0.1: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

libcometa.pc: libcometa.pc.in
	sed -e 's#@LIBDIR@#$(LIBDIR)#g; s#@INCDIR@#$(INCDIR)#g' $^ >$@

clean:
	rm -f *.o libcometa.so.0.1 libcometa.pc

install:
#	$(INSTALL) -D -m 0644 cometa.h $(DESTDIR)$(INCDIR)/cometa.h
#	$(INSTALL) -D -m 0755 libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0.1
#	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0
#	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so
#	$(INSTALL) -D -m 0644 libcometa.pc $(DESTDIR)$(LIBDIR)/pkgconfig/libcometa.pc
#	ldconfig

	$(INSTALL)  -m 0644 cometa.h $(DESTDIR)$(INCDIR)/cometa.h
	$(INSTALL)  -m 0755 libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0.1
	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so.0
	ln -s -f libcometa.so.0.1 $(DESTDIR)$(LIBDIR)/libcometa.so
	$(INSTALL)  -m 0644 libcometa.pc $(DESTDIR)$(LIBDIR)/pkgconfig/libcometa.pc	

-include ../Makefile.lib