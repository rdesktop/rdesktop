#!/bin/sh
echo deleting
rm qtrdesktop
rm *.o
rm moc_*
echo compiling
# rdesktop files
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../tcp.c -o tcp.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../iso.c -o iso.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../mcs.c -o mcs.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../secure.c -o secure.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../rdp.c -o rdp.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../rdp5.c -o rdp5.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../orders.c -o orders.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../cache.c -o cache.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../mppc.c -o mppc.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../licence.c -o licence.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../bitmap.c -o bitmap.o
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -c ../channels.c -o channels.o
# qt files
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -I/usr/local/qt/include -c qtwin.cpp -o qtwin.o
# moc stuff
echo doing moc
/usr/local/qt/bin/moc qtwin.h > moc_qtwin.cpp
g++ -DWITH_OPENSSL -DL_ENDIAN -O2 -Wall -I/usr/local/qt/include -c moc_qtwin.cpp -o moc_qtwin.o
echo linking
g++ -o qtrdesktop moc_qtwin.o qtwin.o tcp.o iso.o mcs.o secure.o rdp.o rdp5.o orders.o cache.o mppc.o licence.o bitmap.o channels.o -lcrypto -L/usr/local/qt/lib -L/usr/X11R6/lib -lqt -lXext -lX11 -lm
echo done
