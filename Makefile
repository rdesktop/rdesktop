#
# rdesktop: A Remote Desktop Protocol client
# Makefile
# Copyright (C) Matthew Chapman 1999-2001
#

# Configuration defaults

CC       = cc
CFLAGS   = -O2
INCLUDES = -I/usr/X11R6/include
LDLIBS   = -L/usr/X11R6/lib -lX11

PREFIX   = /usr/local
EPREFIX  = $(PREFIX)
BINDIR   = $(EPREFIX)/bin
MANDIR   = $(PREFIX)/man

RDPOBJ   = rdesktop.o tcp.o iso.o mcs.o secure.o licence.o rdp.o orders.o bitmap.o cache.o xwin.o
CRYPTOBJ = crypto/rc4_enc.o crypto/rc4_skey.o crypto/md5_dgst.o crypto/sha1dgst.o crypto/arith.o

include Makeconf  # local configuration


rdesktop: $(RDPOBJ) $(CRYPTOBJ)
	$(CC) $(CFLAGS) -o rdesktop $(RDPOBJ) $(CRYPTOBJ) $(LDDIRS) $(LDLIBS)

Makeconf:
	./configure

install: installbin

installbin: rdesktop
	mkdir -p $(BINDIR)
	cp rdesktop $(BINDIR)
	strip $(BINDIR)/rdesktop
	chmod 755 $(BINDIR)/rdesktop

installman: rdesktop.1
	mkdir -p $(MANDIR)/man1
	cp rdesktop.1 $(MANDIR)/man1
	chmod 755 $(MANDIR)/man1/rdesktop.1

proto:
	cproto -DMAKE_PROTO -o proto.h *.c

clean:
	rm -f *.o crypto/*.o *~

.SUFFIXES:
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ -c $<

