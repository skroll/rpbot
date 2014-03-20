#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rp_ircsm.h>

static const char *test_list[] = {
	":Angel!wings@irc.org PRIVMSG Wiz :Are you receiving this message ?\r\n",
	":WiZ!jto@tolsun.oulu.fi MODE #eu-opers -l\r\n",
	":WiZ!jto@tolsun.oulu.fi TOPIC #test :New topic\r\n",
	":WiZ!jto@tolsun.oulu.fi JOIN #Twilight_zone\r\n",
	":WiZ!jto@tolsun.oulu.fi PART #playzone :I lost\r\n",
	"PING :irc.funet.fi\r\n",
};

void
print_rp_str(const char *prefix, rp_str_t *str)
{
	char buf[2048];
	memcpy(buf, str->ptr, str->len);
	buf[str->len] = '\0';

	printf("%s: \"%s\"\n", prefix, buf);
}

void
do_test(int *cs, struct rp_ircsm_msg *msg, const char *str)
{
	size_t len = strlen(str);

	if (rp_ircsm_parse(msg, cs, str, &len)) {
		if (msg->is_hostmask) {
			print_rp_str("nick", &msg->hostmask.nick);
			print_rp_str("user", &msg->hostmask.user);
			print_rp_str("host", &msg->hostmask.host);
		} else if (msg->is_servername) {
			print_rp_str("servername", &msg->servername);
		}

		print_rp_str("code", &msg->code);
		print_rp_str("params", &msg->params);
	}
}

int
main(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	rp_pool_t *pool = rp_create_pool(RP_DEFAULT_POOL_SIZE);

	struct rp_ircsm_msg msg;
	rp_ircsm_msg_init(pool, &msg);

	int i;
	int cs;
	rp_ircsm_init(&cs);

	for (i = 0; i < (int)(sizeof(test_list) / sizeof(test_list[0])); i++) {
		do_test(&cs, &msg, test_list[i]);
		printf("\n");
	}

	return 0;
}

