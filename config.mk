VERSION = 0.0.1

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

INCS = -I.

LIBS = -lanl -lyajl

CPPFLAGS = -DVERSION=\"${VERSION}\" -D_GNU_SOURCE -DRPBOT_PTR_SIZE=8
CFLAGS += -g -std=c99 -pedantic -Wall -Wvariadic-macros -Os ${INCS} ${CPPFLAGS}

LDFLAGS += -g ${LIBS}

CC ?= cc

