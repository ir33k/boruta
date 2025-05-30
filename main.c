#include <stdio.h>
#include "boruta.h"

static void
cb(void *ctx, char *why, int cn, char **cols, char **row)
{
	int i, *count;

	count = ctx;

	if (why) {
		fprintf(stderr, "boruta: %s\n", why);
		return;
	}

	if (*count == 0) {
		for (i=0; i<cn; i++)
			printf("%s\t", cols[i]);

		printf("\n");
	}

	for (i=0; i<cn; i++)
		printf("%s\t", row[i]);

	printf("\n");

	(*count)++;
}

int
main(int argc, char **argv)
{
	char buf[4096];
	int count;

	if (argc > 1)
		boruta(cb, 0, "%s LOAD", argv[1]);

	while (1) {
		fprintf(stderr, "boruta> ");

		if (!fgets(buf, sizeof buf, stdin))
			break;

		count = 0;
		boruta(cb, &count, buf);
		printf("%d\n", count);
	}
	printf("\n");

	return 0;
}
