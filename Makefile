include config.mk

SRC = rpbot.c event.c fifo.c irc_sm.c
HDR = rpbot.h event.h fifo.h irc.h rp_math.h
OBJ = ${SRC:.c=.o}

all: rpbot

config.h:
	cp config.def.h config.h

irc_sm.c: irc_sm.rl
	ragel irc_sm.rl

.c.o:
	@echo cc $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk irc_sm.c ${HDR}

rpbot: ${OBJ}
	@echo cc -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f rpbot irc_sm.c ${OBJ}

.PHONY: all clean

