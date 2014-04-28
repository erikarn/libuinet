TOPDIR?=${CURDIR}/../..

PROG=passive

SRCS=passive.c

UINET_LIBS=uinet nv

CFLAGS= -I${TOPDIR}/lib/libev -I${TOPDIR}/lib/libnv
LDADD= ${TOPDIR}/lib/libev/.libs/libev.a -lm -lpcap

DEBUG_FLAGS=-g -O

include ${TOPDIR}/mk/prog.mk
