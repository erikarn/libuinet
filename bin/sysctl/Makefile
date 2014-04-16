TOPDIR?=${CURDIR}/../..

PROG=sysctl

SRCS=sysctl.c u_sysctl.c

UINET_LIBS=uinet

CFLAGS= -I${TOPDIR}/lib/libev -I../passive/ -I${TOPDIR}/lib/libnv
LDADD= ${TOPDIR}/lib/libev/.libs/libev.a ${TOPDIR}/lib/libnv/libnv.a  -lm -lpcap

DEBUG_FLAGS=-g -O0

include ${TOPDIR}/mk/prog.mk
