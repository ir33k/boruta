#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "boruta.h"

#define SIZE(array) ((sizeof array) / (sizeof array[0]))

char *boruta_err;
char **boruta_cols;
char **boruta_row;

struct row {
	char *cells[32];
	struct row *next;
};

static char *storage = 0;
static size_t storagen = 0;
static struct row *rows = 0;
static struct row *tables[16] = {0};
static char command[BUFSIZ];
static char *commandp = "";
static char *stack[128];
static unsigned stacki = 0;

static char *
next(void)
{
	char terminate, *word;

	/* Skip whitespaces */
	while (*commandp && *commandp <= ' ')
		commandp++;

	if (!*commandp)
		return 0;

	switch (*commandp) {
	case '"': case '\'':	/* Explicite strings */
		terminate = *commandp;
		(*commandp)++;
		break;
	default:
		terminate = ' ';
	}

	word = commandp;

	while (*commandp && *commandp != '\n' && *commandp != terminate)
		commandp++;

	*commandp = 0;
	commandp++;
	return word;
}

static void
push(char *word)
{
	stack[stacki++] = word;
}

static char *
pop(void)
{
	return stacki ? stack[--stacki] : 0;
}

static char *
Unimplemented(void)
{
	return "Unimplemented word";
}

static char *
ltrim(char *str)
{
	while (*str && *str <= ' ') str++;
	return str;
}

static char *
cell_end(char *str)
{
	/* End is defined as EOF, EOL or 2 spaces */
	while (*str != 0 && *str != '\n' && strncmp(str, "  ", 2))
		str++;
	return str;
}

static char *
line_end(char *str)
{
	while (*str != 0 && *str != '\n')
		str++;
	return str;
}

static struct row *
rows_add(char *line)
{
	static struct row *last;
	static unsigned tablesn;
	struct row *new;
	unsigned i;

	assert(line);

	line = ltrim(line);
	if (*line == 0 && (!last || last->cells[0] == 0))
		/* Avoid multiple empty rows */
		return last;

	new = malloc(sizeof(*new));
	if (!new)
		return 0;

	new->next = 0;
	if (rows)
		last->next = new;
	else {
		tablesn = 0;
		rows = new;
	}

	/* First table row with table name */
	if (!last || last->cells[0] == 0)
		tables[tablesn++] = new;

	last = new;

	i = 0;
	while (*line) {
		new->cells[i++] = line;
		line = cell_end(line);
		if (!*line)
			break;
		*line = 0;
		line++;
		line = ltrim(line);
	}
	new->cells[i] = 0;

	return new;
}

static char *
Unload(void)
{
	struct row *row = rows;
	while (row) {
		rows = row->next;
		free(row);
		row = rows;
	}
	rows = 0;
	tables[0] = 0;
	storagen = 0;
	if (storage)
		free(storage);
	return 0;
}

static char *
Load(void)
{
	char *path, *pt, *line;
	struct stat fs = {0};
	FILE *fp;
	size_t sz;

	Unload();

	/* Step 1: Read file content to storage */

	if (!(path = pop()))
		return "Missing file path";

	if (stat(path, &fs) == -1)
		return "Failed to read file stats";

	storagen = fs.st_size +1;	/* +1 for null terminator */
	if (!(storage = malloc(storagen)))
		return "Failed to allocate memory";

	if (!(fp = fopen(path, "r")))
		return "Failed to load";

	sz = fread(storage, 1, fs.st_size, fp);
	storage[sz++] = 0;

	if (fclose(fp))
		return "Failed to close file";

	if (sz != storagen)
		return "Failed to read entire file";

	/* Step 2: Parse file as rows of cells */

	pt = storage;
	while (*pt) {
		line = pt;
		pt = line_end(line);
		if (pt) {
			*pt = 0;
			pt++;
		}
		if (!rows_add(line))
			return "Failed to create new row";
	}

	return 0;
}

static char *
Select(void)
{
	struct row *row;
	unsigned i;

	for (i=0; tables[i]; i++) {
		printf("%d: %s\n", i, tables[i]->cells[0]);
	}

	for (row = rows; row; row = row->next) {
		for (i=0; row->cells[i]; i++) {
			printf("%s\t", row->cells[i]);
		}
		printf("\n");
	}

	return 0;
}

static char *
Null(void)
{
	static char *str = "---";
	push(str);
	return 0;
}

static struct {
	char *name;
	char *(*handle)(void);
} words[] = {
	// File
	{ "UNLOAD",	Unload },
	{ "LOAD",	Load },
	{ "SAVE",	Unimplemented },
	// Context
	{ "TABLE",	Unimplemented },
	// Actions
	{ "CREATE",	Unimplemented },
	{ "INSERT",	Unimplemented },
	{ "SELECT",	Select },
	{ "UPDATE",	Unimplemented },
	{ "DELETE",	Unimplemented },
	{ "DROP",	Unimplemented },
	// Conditions
	{ "EQUAL",	Unimplemented },
	{ "DIFFERENT",	Unimplemented },
	{ "BEGINS",	Unimplemented },
	{ "ENDS",	Unimplemented },
	// Constants
	{ "NULL",	Null },
};

int
boruta(char *fmt, ...)
{
	char *word;
	va_list ap;
	unsigned i, n;

	boruta_err = 0;
	boruta_cols = 0;
	boruta_row = 0;

	/* New command */
	if (fmt) {
		stacki = 0;
		va_start(ap, fmt);
		n = vsnprintf(command, sizeof command, fmt, ap);
		va_end(ap);
		commandp = command;
		if (n >= sizeof command)
			boruta_err = "Command length exceeded";
		return 0;
	}

	word = next();
	if (!word)
		return 0;	/* End */

	/* Run handler if this is one of defined words */
	for (i=0; i < SIZE(words); i++) {
		if (!strcmp(words[i].name, word)) {
			boruta_err = (*words[i].handle)();
			return 1;
		}
	}

	/* Else push data to stack */
	push(word);
	return 1;
}
