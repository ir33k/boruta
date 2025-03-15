#include <stdio.h>
#include "boruta.h"

static void
cb(void *ctx, const char *why, int ri, int cn, char **cols, char **rows)
{
	int i;

	(void)ctx;

	if (why) {
		fprintf(stderr, "boruta: %s\n", why);
		return;
	}

	if (ri == 0) {
		for (i=0; i<cn; i++)
			printf("%s\t", cols[i]);

		printf("\n");
	}

	for (i=0; i<cn; i++)
		printf("%s\t", rows[i]);

	printf("\n");
}

int
main(int argc, char **argv)
{
	char buf[4096];

	if (argc > 1)
		boruta(cb, 0, "%s LOAD", argv[1]);

	while (1) {
		fprintf(stderr, "boruta> ");

		if (!fgets(buf, sizeof buf, stdin))
			break;

		boruta(cb, 0, buf);
	}
	printf("\n");

	return 0;
}
