##############################################
# rdesktop: A Remote Desktop Protocol client #
# Linux Makefile                             #
# Copyright (C) Matthew Chapman 1999-2000    #
##############################################

SOURCES=client.c parse.c tcp.c iso.c mcs.c rdp.c bitmap.c

rdesktop: $(SOURCES)
	@gcc -g -Wall -o rdesktop $(SOURCES)
