TOPDIR?=${CURDIR}/../..

PROG=passive

SRCS=passive.c

UINET_LIBS=uinet nv

CFLAGS= -I${TOPDIR}/lib/libev -I${TOPDIR}/lib/libnv
LDADD= ${TOPDIR}/lib/libev/.libs/libev.a -lm -lpcap

ifndef NO_EXTRACT
CFLAGS+= -I${TOPDIR}/lib/libhttp_parser -DENABLE_EXTRACT
LDADD+=  -L${TOPDIR}/lib/libhttp_parser -lhttp_parser -lz
endif

DEBUG_FLAGS=-g -O0

include ${TOPDIR}/mk/prog.mk
