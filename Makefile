##############################################
# rdesktop: A Remote Desktop Protocol client #
# Linux Makefile                             #
# Copyright (C) Matthew Chapman 1999-2000    #
##############################################

CC     = gcc
CFLAGS = -g -Wall -DDUMP
LIBS   = -L/usr/X11R6/lib -lX11
OBJECTS = client.o parse.o tcp.o iso.o mcs.o rdp.o process.o bitmap.o cache.o xwin.o misc.o

rdesktop: $(OBJECTS)
	@$(CC) $(CFLAGS) -o rdesktop $(LIBS) $(OBJECTS)

proto:
	@cproto -D MAKE_PROTO -o proto.h *.c

clean:
	rm -f *.o
