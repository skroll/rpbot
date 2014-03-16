#include <stdio.h>
#include <rp_options.h>

static void
usage(void)
{
	printf("\n");
	printf("Usage: rpbot [OPTIONS] [CONFIG]\n\n");
}

int
rp_parse_opts(int argc, const char **argv, const char **config)
{
	(void)argc;
	(void)argv;
	(void)config;

	if (argc < 2) {
		usage();
		return 1;
	}

	*config = argv[1];

	return 0;
}

