
TOPDIR?=${CURDIR}/../..
include ${TOPDIR}/lib.mk
include ${TOPDIR}/cflags.mk

CFLAGS+=	${DEBUG_FLAGS}

LIB=	nv

SRCS=	dnvlist.c msgio.c nvlist.c nvpair.c
OBJS=	dnvlist.o msgio.o nvlist.o nvpair.o
INCS=	dnv.h nv.h

all: libnv.a

libnv.a: $(OBJS)
	$(AR) -c -r libnv.a $(OBJS)

clean:
	$(RM) $(OBJS) libnv.a

