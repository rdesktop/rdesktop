##############################################
# rdesktop: A Remote Desktop Protocol client #
# Basic Makefile                             #
# Copyright (C) Matthew Chapman 1999-2000    #
##############################################

# Uncomment to enable debugging
# DEBUG = -g -DRDP_DEBUG

CC     = gcc
CFLAGS = -O2 -Wall $(DEBUG) -I/usr/X11R6/include
LIBS   = -L/usr/X11R6/lib -lX11

RDPOBJ = rdesktop.o tcp.o iso.o mcs.o secure.o licence.o rdp.o orders.o bitmap.o cache.o xwin.o
CRYPTOBJ = crypto/rc4_enc.o crypto/rc4_skey.o crypto/md5_dgst.o crypto/sha1dgst.o crypto/arith.o

rdesktop: $(RDPOBJ) $(CRYPTOBJ)
	@$(CC) $(CFLAGS) -o rdesktop $(LIBS) $(RDPOBJ) $(CRYPTOBJ)

proto:
	@cproto -DMAKE_PROTO -o proto.h *.c

clean:
	rm -f *.o crypto/*.o *~
