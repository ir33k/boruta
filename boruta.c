#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "boruta.h"

typedef char* err_t;

#define SIZE(array) ((sizeof array) / (sizeof array[0]))

#define CMAX 32	/* Max number of columns */

struct row {
	char *cells[CMAX];
	struct row *next;
};

struct table {
	char *name;
	unsigned width[CMAX];	/* Each column max width */
	struct row *rows, *last;
	struct table *next;
};

/* Util */
static char *msg(char *fmt, ...);
static unsigned utf8len(char *str);
static unsigned width(unsigned w, char *cell);

/* Memory */
static char *store(char *str, size_t len);

/**/
static struct table *table_get(char *name);
static struct table *table_new();
static struct row *row_new(struct table *table);

/* Parsing */
static char *skip_whitespaces(char *str);
static char *each_line(char *str);
static char *each_cell(char *str);
static err_t parse(char *str);

/* Stack */
static void push(char *word);
static char *pop();

/* Handlers */
static err_t Unimplemented();
static err_t Load();
static err_t Select();
static err_t Null();

/* External */
char  *boruta_err;
char **boruta_cols;
char **boruta_row;

/* Internal */
static struct table *tables = 0;
static char command[BUFSIZ];
static char *commandp = "";
static char *stack[128];
static unsigned stacki = 0;

static struct {
	char *name;
	err_t (*handle)();
} words[] = {
	/* File */
	{ "LOAD",	Load },
	{ "SAVE",	Unimplemented },
	/* Context */
	{ "TABLE",	Unimplemented },
	/* Actions */
	{ "CREATE",	Unimplemented },
	{ "INSERT",	Unimplemented },
	{ "SELECT",	Select },
	{ "UPDATE",	Unimplemented },
	{ "DELETE",	Unimplemented },
	{ "DROP",	Unimplemented },
	/* Conditions */
	{ "EQUAL",	Unimplemented },
	{ "DIFFERENT",	Unimplemented },
	{ "BEGINS",	Unimplemented },
	{ "ENDS",	Unimplemented },
	/* Values */
	{ "NULL",	Null },
	{ "TODAY",	Unimplemented },	/* yyyy-mm-dd */
	{ "NOW",	Unimplemented },	/* yyyy-mm-dd hh:mm:ss */
};

static char *
msg(char *fmt, ...)
{
	static char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	return buf;
}

static unsigned
utf8len(char *str)
{
	unsigned i, n;

	for (i=0, n=0; str[i]; i++)
		if ((str[i] & 0xC0) != 0x80)
			n++;
	return n;
}

static unsigned
width(unsigned w, char *cell)
{
	for (; *cell; cell++)
		if ((*cell & 0xC0) == 0x80)
			w++;
	return w;
}

static char *
store(char *str, size_t len)
{
	static char *buf;
	static size_t cap, used;
	char *pt;

	if (str && len == (size_t)-1)
		len = strlen(str) +1;

	if (used + len >= cap) {
		pt = realloc(buf, cap + len);
		if (!pt)
			return 0;
		buf = pt;
		cap += len;
	}

	pt = buf + used;
	used += len;

	if (str)
		memcpy(pt, str, len);

	return pt;
}

static struct table *
table_get(char *name)
{
	struct table *node;

	assert(name);

	for (node = tables; node; node = node->next)
		if (!strcmp(node->name, name))
			return node;

	return 0;
}

static struct table *
table_new()
{
	struct table *last, *new=0;

	new = malloc(sizeof *new);
	if (!new)
		return 0;

	memset(new, 0, sizeof *new);

	if (tables) {
		for (last = tables; last->next; last = last->next);
		last->next = new;
	} else {
		tables = new;
	}
	return new;
}

static struct row *
row_new(struct table *table)
{
	struct row *new=0;

	new = malloc(sizeof *new);
	if (!new)
		return 0;

	memset(new, 0, sizeof *new);

	if (!table->rows)
		table->rows = new;

	if (table->last)
		table->last->next = new;

	table->last = new;
	return new;
}

static char *
skip_whitespaces(char *str)
{
	while (*str && *str == ' ') str++;
	return str;
}

static char *
each_line(char *str)
{
	while (*str != 0 && *str != '\n')
		str++;

	if (*str == 0)
		return 0;

	*str = 0;
	return str +1;
}

static char *
each_cell(char *str)
{
	while (*str != 0 && *str != '\n' && !(str[0] == ' ' && str[1] == ' '))
		str++;

	if (*str == 0 || *str == '\n')
		return 0;

	*str = 0;
	return str +1;
}

static err_t
parse(char *str)
{
	struct table *table=0;
	struct row *row=0;
	unsigned i=0, w;
	char *line, *next_line, *cell, *next_cell;
	int parse_rows=0;

	for (line = str; line; line = next_line) {
		line = skip_whitespaces(line);
		next_line = each_line(line);

		if (*line == 0) {	/* Empty line */
			parse_rows = 0;
			continue;
		}

		if (parse_rows) {
			row = row_new(table);
			if (!row)
				return msg("Failed creating row for table %s", table->name);
		}

		for (i=0, cell = line; cell; i++, cell = next_cell) {
			cell = skip_whitespaces(cell);
			next_cell = each_cell(cell);

			if (i >= CMAX)
				return msg("Cells count (%d) exceeded in table %s", CMAX, table->name);

			if (parse_rows) {
				row->cells[i] = cell;
				w = utf8len(cell);
				if (w > table->width[i])
					table->width[i] = w;
				continue;
			}

			table = table_get(cell);
			if (table)
				return msg("Table %s already exist", cell);

			table = table_new();
			if (!table)
				return msg("Failed to create new table %s", cell);

			table->name = cell;

			if (next_cell)
				return msg("Unexpected cell after table %s name", table->name);

			parse_rows = 1;
		}
	}

	return 0;
}

static void
push(char *word)
{
	stack[stacki++] = word;
}

static char *
pop()
{
	return stacki ? stack[--stacki] : 0;
}

static char *
next()
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

static err_t
Unimplemented()
{
	return "Unimplemented word";
}

static err_t
Load()
{
	err_t why;
	char *path, *str;
	struct stat fs = {0};
	FILE *fp;
	size_t sz;

	if (!(path = pop()))
		return "Missing file path";

	if (stat(path, &fs) == -1)
		return "Failed to read file stats";

	str = store(0, fs.st_size +1);
	if (!str)
		return "Failed to allocate memory in storage";

	if (!(fp = fopen(path, "r")))
		return "Failed to load";

	sz = fread(str, 1, fs.st_size, fp);
	str[sz++] = 0;

	if (fclose(fp))
		return "Failed to close file";

	if (sz != (size_t)fs.st_size +1)
		return "Failed to read entire file";

	why = parse(str);
	if (why)
		return why;

	return 0;
}

static err_t
Select()
{
	struct table *t;
	struct row *r;
	unsigned i;

	if (!tables)
		return "No tables";

	for (t = tables; t; t = t->next) {
		printf("%s\n", t->name);

		for (r = t->rows; r; r = r->next) {
			for (i=0; r->cells[i]; i++)
				printf("%-*s  ",
				       width(t->width[i], r->cells[i]),
				       r->cells[i]);
			printf("\n");
		}
		printf("\n");
	}
	return 0;
}

static err_t
Null()
{
	static char *str = "---";
	push(str);
	return 0;
}

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
