#!/bin/sh
indent -bli0 -i8 -cli8 -npcs -l100 *.h *.c vnc/*.h vnc/*.c
# Tweak in order to support both indent 2.2.10 and 2.2.11
perl -pi -e 's|!!|! !|g' *.c

