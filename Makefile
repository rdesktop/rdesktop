#
# rdesktop: A Remote Desktop Protocol client
# Makefile
# Copyright (C) Matthew Chapman 1999-2001
#

# Configuration defaults

prefix      = /usr/local
exec_prefix = $(prefix)
bindir      = $(exec_prefix)/bin
mandir      = $(prefix)/man
datadir     = $(prefix)/share/rdesktop

KEYMAP_PATH = $(datadir)/keymaps/

RDPOBJ   = rdesktop.o tcp.o iso.o mcs.o secure.o licence.o rdp.o orders.o bitmap.o cache.o xwin.o xkeymap.o ewmhints.c
CRYPTOBJ = crypto/rc4_enc.o crypto/rc4_skey.o crypto/md5_dgst.o crypto/sha1dgst.o crypto/bn_exp.o crypto/bn_mul.o crypto/bn_div.o crypto/bn_sqr.o crypto/bn_add.o crypto/bn_shift.o crypto/bn_asm.o crypto/bn_ctx.o crypto/bn_lib.o

include Makeconf  # configure-generated


rdesktop: $(RDPOBJ) $(CRYPTOBJ)
	$(CC) $(CFLAGS) -o rdesktop $(RDPOBJ) $(CRYPTOBJ) $(LDFLAGS)

Makeconf:
	./configure

install: installbin installkeymaps installman

installbin: rdesktop
	mkdir -p $(DESTDIR)/$(bindir)
	install rdesktop $(DESTDIR)/$(bindir)
	strip $(DESTDIR)/$(bindir)/rdesktop
	chmod 755 $(DESTDIR)/$(bindir)/rdesktop

installman: doc/rdesktop.1
	mkdir -p $(DESTDIR)/$(mandir)/man1
	cp doc/rdesktop.1 $(DESTDIR)/$(mandir)/man1
	chmod 644 $(DESTDIR)/$(mandir)/man1/rdesktop.1

installkeymaps:
	mkdir -p $(DESTDIR)/$(KEYMAP_PATH)
# Prevent copying the CVS directory
	cp keymaps/?? keymaps/??-?? $(DESTDIR)/$(KEYMAP_PATH)
	cp keymaps/common $(DESTDIR)/$(KEYMAP_PATH)
	cp keymaps/modifiers $(DESTDIR)/$(KEYMAP_PATH)
	chmod 644 $(DESTDIR)/$(KEYMAP_PATH)/*

proto:
	cproto -DMAKE_PROTO -o proto.h *.c

clean:
	rm -f *.o crypto/*.o *~ rdesktop

dist:
	mkdir -p /tmp/rdesktop-make-dist-dir
	ln -sf `pwd` /tmp/rdesktop-make-dist-dir/rdesktop
	(cd /tmp/rdesktop-make-dist-dir; \
	tar zcvf rdesktop/rdesktop.tgz \
	rdesktop/COPYING \
	rdesktop/crypto/README \
	rdesktop/crypto/*.c \
	rdesktop/crypto/*.h \
	rdesktop/*.c \
	rdesktop/*.h \
	rdesktop/keymaps/?? \
	rdesktop/keymaps/??-?? \
	rdesktop/keymaps/common \
	rdesktop/keymaps/modifiers \
	rdesktop/keymaps/convert-map \
	rdesktop/doc/HACKING \
	rdesktop/doc/TODO \
	rdesktop/doc/keymapping.txt \
	rdesktop/doc/rdesktop.1 \
	rdesktop/Makefile \
	rdesktop/configure \
	rdesktop/rdesktop.spec)
	rm -rf /tmp/rdesktop-make-dist-dir

.SUFFIXES:
.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

