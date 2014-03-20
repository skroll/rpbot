#include <string.h>
#include <rp_string.h>

int
rp_strtoken(rp_str_t *str, rp_str_t *token)
{
	char first = 0;

	while (str->len) {
		if (!first) {
			if (*str->ptr != ' ') {
				token->ptr = str->ptr;
				token->len = 0;
				first = 1;
			}
		} else {
			token->len++;

			if (*str->ptr == ' ') {
				return 1;
			}
		}

		str->ptr++;
		str->len--;
	}

	if (first) {
		token->len++;
		return 1;
	}

	return 0;
}

int
rp_strstr(rp_str_t *in, rp_str_t *str)
{
	char c;
	char *p = in->ptr;
	size_t in_len = in->len;

	if (str->len == 0) {
		return 0;
	}

	c = *str->ptr;
	size_t len = str->len - 1;

	do {
		char sc;

		do {
			sc = *p++;
			in_len--;

			if (in_len < len) {
				return -1;
			}
		} while (sc != c);
	} while (memcmp(p, str->ptr + 1, len) != 0);

	return p - in->ptr - 1;
}

