
SUBDIRS=libuinet libev libnv libhttp_parser

TOPDIR?=${CURDIR}/..
include ${TOPDIR}/cflags.mk

default: all

config:
	(cd libev ; env CFLAGS="${DEBUG}" ./configure --with-uinet=../libuinet/api_include )

all:
	for d in $(SUBDIRS); do ( cd $$d; $(MAKE) all ) ; done

clean:
	for d in $(SUBDIRS); do ( cd $$d; $(MAKE) clean ) ; done

install:
	for d in $(SUBDIRS); do ( cd $$d; $(MAKE) install ) ; done

default: config all
