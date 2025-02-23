#include <stdio.h>
#include <err.h>
#include "boruta.h"

static void
run(void)
{
	unsigned i;
	if (boruta_err) {
		warnx("%s", boruta_err);
		return;
	}
	while (boruta(0)) {
		if (boruta_err) {
			warnx("%s", boruta_err);
			continue;
		}
		if (boruta_cols) {
			for (i=0; boruta_cols[i]; i++)
				printf("%s\t", boruta_cols[i]);
			printf("\n");
		}
		if (boruta_row) {
			for (i=0; boruta_row[i]; i++)
				printf("%s\t", boruta_row[i]);
			printf("\n");
		}
	}
}

int
main(int argc, char **argv)
{
	char buf[4096];
	if (argc > 1) {
		boruta("%s LOAD", argv[1]);
		run();	
	}
	while (1) {
		fprintf(stderr, "boruta> ");
		if (!fgets(buf, sizeof buf, stdin))
			break;
		boruta(buf);
		run();
	}
	printf("\n");
	return 0;
}
