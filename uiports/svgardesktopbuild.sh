#!/bin/sh
# dumby script

rm svgardesktop
rm *.o

cc="gcc"
cflags="-DWITH_OPENSSL -DL_ENDIAN -O2 -Wall"
# uncomment the following line to turn on debug
#cflags=$cflags+" -DWITH_DEBUG"
lflags="-lcrypto -lvga"

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

# svga ui file
$cc $cflags -c svgawin.c -o svgawin.o

$cc $lflags -o svgardesktop svgawin.o tcp.o iso.o mcs.o secure.o rdp.o rdp5.o orders.o cache.o mppc.o licence.o bitmap.o channels.o pstcache.o
