#!/bin/sh
# qtrdesktop build script

rm qtrdesktop
rm *.o
rm moc_*

cc="g++"
cflags="-DWITH_OPENSSL -DL_ENDIAN -O2 -Wall"
# uncomment the following line to turn on debug
#cflags=$cflags+" -DWITH_DEBUG"
lflags="-lcrypto"

# rdesktop core files
$cc $cflags -c ../tcp.c -o tcp.o
$cc $cflags -c ../iso.c -o iso.o
$cc $cflags -c ../mcs.c -o mcs.o
$cc $cflags -c ../secure.c -o secure.o
$cc $cflags -c ../rdp.c -o rdp.o
$cc $cflags -c ../rdp5.c -o rdp5.o
$cc $cflags -c ../orders.c -o orders.o
$cc $cflags -c ../cache.c -o cache.o
$cc $cflags -c ../mppc.c -o mppc.o
$cc $cflags -c ../licence.c -o licence.o
$cc $cflags -c ../bitmap.c -o bitmap.o
$cc $cflags -c ../channels.c -o channels.o
$cc $cflags -c ../pstcache.c -o pstcache.o

# qt files
$cc $cflags -I/usr/local/qt/include -c qtwin.cpp -o qtwin.o

# moc stuff
/usr/local/qt/bin/moc qtwin.h > moc_qtwin.cpp

$cc $cflags -I/usr/local/qt/include -c moc_qtwin.cpp -o moc_qtwin.o

$cc -o qtrdesktop moc_qtwin.o qtwin.o tcp.o iso.o mcs.o secure.o rdp.o rdp5.o orders.o cache.o mppc.o licence.o bitmap.o channels.o pstcache.o -lcrypto -L/usr/local/qt/lib -L/usr/X11R6/lib -lqt -lXext -lX11 -lm
