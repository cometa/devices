CC=gcc
PREFIX=/usr/local
LIBDIR=$(PREFIX)/lib
INCDIR=$(PREFIX)/include
PKG_CONFIG_PATH=$(LIBDIR)/pkgconfig

export PREFIX LIBDIR INCDIR

# We ignore examples/, they shall not be built by default and
# their makefiles depend on libpubnub already being installed anyway.
SUBDIRS=libcometa

all: all-recursive

clean: clean-recursive

install: install-recursive

-include Makefile.lib
