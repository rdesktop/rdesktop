##############################################
# rdesktop: A Remote Desktop Protocol client #
# Linux Makefile                             #
# Copyright (C) Matthew Chapman 1999-2000    #
##############################################

CC     = gcc
CFLAGS = -g -Wall
LIBS   = -L/usr/X11R6/lib -lX11
OBJECTS = client.o parse.o tcp.o iso.o mcs.o rdp.o bitmap.o xwin.o

rdesktop: $(OBJECTS)
	@$(CC) $(CFLAGS) -o rdesktop $(LIBS) $(OBJECTS)

clean:
	rm -f *.o
