TOPDIR?=${CURDIR}/../..

PROG=sysctl

SRCS=sysctl.c

UINET_LIBS=uinet

CFLAGS= -I${TOPDIR}/lib/libev -I../passive/
LDADD= ${TOPDIR}/lib/libev/.libs/libev.a -lm -lpcap

DEBUG_FLAGS=-g -O0

include ${TOPDIR}/mk/prog.mk
