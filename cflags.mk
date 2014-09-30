HOST_OS:=$(shell uname -s)

DEBUG_FLAGS	?= -O -gdwarf-2

UINET_DESTDIR	?= /usr/local/

#CFLAGS+=	-fPIC
#LDFLAGS+=	-fPIC

UINET_INSTALL	?= install
UINET_INSTALL_DIR ?= $(UINET_INSTALL) -m 0755
UINET_INSTALL_LIB ?= $(UINET_INSTALL) -m 0644
UINET_INSTALL_INC ?= $(UINET_INSTALL) -m 0644
UINET_INSTALL_BIN ?= $(UINET_INSTALL) -m 0755
