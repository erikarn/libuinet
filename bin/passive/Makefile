TOPDIR?=${CURDIR}/../..
include ${TOPDIR}/cflags.mk

PROG=passive

SRCS=passive.c

UINET_LIBS=uinet uinetnv

CFLAGS= -I${TOPDIR}/lib/libev -I${TOPDIR}/lib/libnv
LDADD= -L${UINET_DESTDIR}/lib/ ${TOPDIR}/lib/libev/.libs/libev.a -lm -lpcap

ifndef NO_EXTRACT
CFLAGS+= -I${TOPDIR}/lib/libhttp_parser -DENABLE_EXTRACT
LDADD+=  -L${TOPDIR}/lib/libhttp_parser -lhttp_parser -lz
endif

maintainer-clean:
	$(MAKE) clean

include ${TOPDIR}/mk/prog.mk
