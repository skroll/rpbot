#ifndef IRC_H
#define IRC_H

#include <stdlib.h>

struct irc_str {
	int len;
	char buf[1];
};

#define IRC_STR(_name, _len) \
	struct { \
		int len; \
		char buf[_len]; \
	} _name;

struct irc_state {
	int cs;

	IRC_STR(message_code, 16)
	IRC_STR(params, 2048)
};

void rp_irc_init(struct irc_state *);

int rp_irc_parse(struct irc_state *state, const char *src, size_t *len);

#endif // IRC_H

