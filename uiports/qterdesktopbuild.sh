#!/bin/sh
# qterdesktop build script

rm -f qterdesktop
rm -f *.o
rm -f moc_*

cc="g++"
cflags="-pipe -DQT_QWS_EBX -DQT_QWS_CUSTOM -DQWS -fno-exceptions -fno-rtti -Wall -O2 -fno-default-inline -DNO_DEBUG -DWITH_OPENSSL"
# uncomment the following line to turn on debug
#cflags=$cflags" -DWITH_DEBUG"
# uncomment the following line to turn on sound
cflags=$cflags" -DWITH_RDPSND"
lflags="-L/usr/local/qt/lib"
libs="-lqte -lcrypto"

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
$cc $cflags -c ../rdpsnd.c -o rdpsnd.o
$cc $cflags -c ../rdpsnd_oss.c -o rdpsnd_oss.o

# qt files
$cc $cflags -I/usr/local/qt/include -c qtewin.cpp -o qtewin.o

# moc stuff
/usr/local/qt/bin/moc qtewin.h > moc_qtewin.cpp

$cc $cflags -I/usr/local/qt/include -c moc_qtewin.cpp -o moc_qtewin.o

$cc $lflags -o qterdesktop moc_qtewin.o qtewin.o tcp.o iso.o mcs.o secure.o rdp.o rdp5.o orders.o cache.o mppc.o licence.o bitmap.o channels.o pstcache.o rdpsnd.o rdpsnd_oss.o $libs
