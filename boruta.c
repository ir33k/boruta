#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "boruta.h"

#define EMPTY "---"	/* String used for NULL cell values */
#define CMAX 32	/* Max number of columns */

enum { TABLE, COLS, ROWS };	/* Parser state */

typedef const char* err_t;

struct row {
	char *cells[CMAX];
	struct row *next;
};

struct table {
	char *name;
	char *cols[CMAX];
	unsigned width[CMAX];	/* Each column max width */
	struct row *rows, *last;
	struct table *next;
};

typedef struct {	/* Currently running command context */
	char cmd[4096], *cp;
	char *stack[128];
	unsigned si;
	char *tname;		/* Selected table name */
	struct table *table;	/* Selected table by name if exist */
	char *eq[CMAX];
	char *neq[CMAX];
} ctx_t;

/* Util */
static err_t msg(const char *fmt, ...);
static unsigned utf8len(char *str);
static unsigned width(unsigned w, char *cell);

/* Memory */
static char *store(char *str, size_t len);

/* Table */
static struct table *table_get(char *name);
static struct table *table_new();
static struct row *row_new(struct table *t);
static int column_indexof(struct table *t, char *name);

/* Parsing */
static char *skip_whitespaces(char *str);
static char *each_line(char *str);
static char *each_cell(char *str);
static err_t parse(char *str);

/* Stack */
static void push(ctx_t *ctx, char *word);
static char *pop(ctx_t *ctx);

/* TODO(irek): I don't like this function */
static char *next(ctx_t *ctx);

/* Words */
static err_t Unimplemented(ctx_t*);
static err_t Load(ctx_t*);
static err_t Save(ctx_t*);
static err_t From(ctx_t*);
static err_t Eq(ctx_t*);
static err_t Neq(ctx_t*);
static err_t Describe(ctx_t*);
static err_t Select(ctx_t*);
static err_t Create(ctx_t*);
static err_t Insert(ctx_t*);
static err_t Null(ctx_t*);
static err_t Now(ctx_t*);

/* External */
const char *boruta_err;
const char **boruta_cols;
const char **boruta_row;

/* Internal */
static struct table *tables = 0;
static struct {
	char *name;
	err_t (*cb)(ctx_t*);
} words[] = {
	"LOAD",		Load,
	"SAVE",		Save,
	"FROM",		From,
	"EQ",		Eq,
	"NEQ",		Neq,
	"DESCRIBE",	Describe,
	"SELECT",	Select,
	"CREATE",	Create,
	"INSERT",	Insert,
	"UPDATE",	Unimplemented,
	"DELETE",	Unimplemented,
	"DROP",		Unimplemented,
	"NULL",		Null,
	"NOW",		Now,
	0
};

static err_t
msg(const char *fmt, ...)
{
	static char buf[4096];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	return (err_t)buf;
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
	char *pt;

	if (str && len == (size_t)-1)
		len = strlen(str) +1;

	pt = malloc(len);
	if (!pt)
		return 0;

	if (str)
		memcpy(pt, str, len);

	return pt;
}

static struct table *
table_get(char *name)
{
	struct table *node;

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
row_new(struct table *t)
{
	struct row *new;

	new = malloc(sizeof *new);
	if (!new)
		return 0;

	memset(new, 0, sizeof *new);

	if (!t->rows)
		t->rows = new;

	if (t->last)
		t->last->next = new;

	t->last = new;
	return new;
}

static int
column_indexof(struct table *t, char *name)
{
	int i;

	for (i=0; t->cols[i]; i++)
		if (!strcmp(t->cols[i], name))
			return i;

	return -1;
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

	while (*str == ' ') {
		*str = 0;
		str++;
	}

	if (*str == 0 || *str == '\n')
		return 0;

	return str;
}

static err_t
parse(char *str)
{
	struct table *t=0;
	struct row *r=0;
	unsigned i=0, w;
	char *line, *next_line, *cell, *next_cell;
	int state;

	state = TABLE;
	for (line = str; line; line = next_line) {
		line = skip_whitespaces(line);
		next_line = each_line(line);

		if (*line == 0) {	/* Empty line */
			state = TABLE;
			continue;
		}

		switch (state) {
		case ROWS:
			r = row_new(t);
			if (!r)
				return msg("Failed creating row for table %s", t->name);
			break;
		}

		for (i=0, cell = line; cell; i++, cell = next_cell) {
			cell = skip_whitespaces(cell);
			next_cell = each_cell(cell);

			if (i >= CMAX)
				return msg("Cells count (%d) exceeded in table %s", CMAX, t->name);

			switch (state) {
			case TABLE:
				t = table_get(cell);
				if (t)
					return msg("Table %s already exist", cell);

				t = table_new();
				if (!t)
					return msg("Failed to create new table %s", cell);

				t->name = cell;

				if (next_cell)
					return msg("Unexpected cell after table %s name", t->name);

				state = COLS;
				break;
			case COLS:
				t->cols[i] = cell;
				t->width[i] = utf8len(cell);
				if (!next_cell)
					state = ROWS;
				break;
			case ROWS:
				r->cells[i] = cell;
				w = utf8len(cell);
				if (w > t->width[i])
					t->width[i] = w;
				break;
			}
		}
	}

	return 0;
}

static void
push(ctx_t *ctx, char *word)
{
	ctx->stack[ctx->si++] = word;
}

static char *
pop(ctx_t *ctx)
{
	return ctx->si ? ctx->stack[--(ctx->si)] : 0;
}

static char *
next(ctx_t *ctx)
{
	char terminate, *word;

	ctx->cp = skip_whitespaces(ctx->cp);
	if (!*ctx->cp)
		return 0;

	switch (*ctx->cp) {
	case '"': case '\'':	/* Explicite strings */
		terminate = *ctx->cp;
		(*ctx->cp)++;
		break;
	default:
		terminate = ' ';
	}

	word = ctx->cp;

	while (*ctx->cp && *ctx->cp != '\n' && *ctx->cp != terminate)
		ctx->cp++;

	*ctx->cp = 0;
	ctx->cp++;
	return word;
}

static err_t
Unimplemented(ctx_t *ctx)
{
	(void)ctx;
	return "Unimplemented word";
}

static err_t
Load(ctx_t *ctx)
{
	err_t why;
	char *str;
	struct stat fs = {0};
	FILE *fp;
	size_t sz;

	str = pop(ctx);
	if (!str)
		return "Missing file path";

	if (stat(str, &fs) == -1)
		return "Failed to read file stats";

	str = store(str, fs.st_size +1);
	if (!str)
		return "Failed to allocate memory in storage";

	if (!(fp = fopen(str, "r")))
		return msg("Failed to open file '%s'", str);

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
Save(ctx_t *ctx)
{
	char *str;
	FILE *fp;
	struct table *t;
	struct row *r;
	unsigned i;

	str = pop(ctx);
	if (!str)
		return "Missing file path";

	if (!tables)
		return "Nothing to save";

	if (!(fp = fopen(str, "w")))
		return msg("Failed to open file '%s'", str);

	for (t = tables; t; t = t->next) {
		fprintf(fp, "%s\n", t->name);

		for (i=0; t->cols[i]; i++)
			fprintf(fp, "%-*s  ",
				width(t->width[i], t->cols[i]),
				t->cols[i]);
		fprintf(fp, "\n");

		for (r = t->rows; r; r = r->next) {
			for (i=0; r->cells[i]; i++)
				fprintf(fp, "%-*s  ",
					width(t->width[i], r->cells[i]),
					r->cells[i]);
			fprintf(fp,"\n");
		}
		fprintf(fp, "\n");
	}

	if (fclose(fp))
		return "Failed to close file";

	return 0;
}

static err_t
From(ctx_t *ctx)
{
	ctx->tname = pop(ctx);
	ctx->table = table_get(ctx->tname);
	return 0;
}

static err_t
Eq(ctx_t *ctx)
{
	char *column, *value;
	int i;

	if (!ctx->table)
		return "Undefined table";

	while (1) {
		column = pop(ctx);
		value = pop(ctx);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(ctx->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		ctx->eq[i] = value;
	}

	return 0;
}

static err_t
Neq(ctx_t *ctx)
{
	char *column, *value;
	int i;

	if (!ctx->table)
		return "Undefined table";

	while (1) {
		column = pop(ctx);
		value = pop(ctx);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(ctx->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		ctx->neq[i] = value;
	}

	return 0;
}

/* TODO(irek): Printing directly to STDOUT is not an option if I want
 * to use this as library. */
static err_t
Describe(ctx_t *ctx)
{
	struct table *t;
	unsigned i;

	if (!tables)
		return "No tables";

	for (t = tables; t; t = t->next) {
		if (ctx->table && t != ctx->table)
			continue;

		printf("%s: ", t->name);

		for (i=0; t->cols[i]; i++)
			printf("%s, ", t->cols[i]);

		printf("\b\b\n");
	}
	return 0;
}

static err_t
Select(ctx_t *ctx)
{
	struct row *r;
	int i, cols[CMAX], cn;
	char *str;

	if (!ctx->table)
		return "Undefined table";

	cn = 0;
	while (cn < CMAX && (str = pop(ctx))) {
		if (!strcmp(str, "*")) {
			for (i=0; ctx->table->cols[i]; i++)
				cols[cn++] = i;
			continue;
		}
		i = column_indexof(ctx->table, str);
		if (i == -1)
			return msg("Unknown column %s", str);
		cols[cn++] = i;
	}

	/* TODO(irek): Consider storing number of columns. */
	for (i=cn-1; i >= 0; i--) {
		str = ctx->table->cols[cols[i]];
		printf("%-*s  ",
		       width(ctx->table->width[cols[i]], str),
		       str);
	}
	printf("\n");

	for (r = ctx->table->rows; r; r = r->next) {
		for (i=0; ctx->table->cols[i]; i++) {
			str = ctx->eq[i];
			if (str && strcmp(str, r->cells[i]))
				goto skip;

			str = ctx->neq[i];
			if (str && !strcmp(str, r->cells[i]))
				goto skip;
		}

		for (i=cn-1; i >= 0; i--) {
			str = r->cells[cols[i]];
			printf("%-*s  ",
			       width(ctx->table->width[cols[i]], str),
			       str);
		}

		printf("\n");
	skip:
		continue;
	}

	return 0;
}

static err_t
Create(ctx_t *ctx)
{
	char *cell, *cells[CMAX] = {0};
	unsigned i, j;

	if (!ctx->tname)
		return "Missing table name";

	if (ctx->table)
		return "Table already exists";

	ctx->table = table_new();
	if (!ctx->table)
		return "Failed to create new table";

	ctx->table->name = store(ctx->tname, -1);

	for (i=0; i < CMAX-1 && (cell = pop(ctx)); i++)
		cells[i] = store(cell, -1);

	/* NOTE(irek): Deicrement to preserve columns order. */
	for (j=0; i--; j++) {
		cell = cells[i];
		ctx->table->cols[j] = cell;
		ctx->table->width[j] = utf8len(cell);
	}

	return 0;
}

static err_t
Insert(ctx_t *ctx)
{
	struct row *r;
	char *column, *value;
	int i;
	unsigned w;
	char *cells[CMAX] = {0};

	if (!ctx->table)
		return "Undefined table";

	while (1) {
		column = pop(ctx);
		value = pop(ctx);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(ctx->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		cells[i] = value;
	}

	r = row_new(ctx->table);
	if (!r)
		return msg("Failed to create row for table %s", ctx->table->name);

	for (i=0; ctx->table->cols[i]; i++) {
		r->cells[i] = cells[i] ? store(cells[i], -1) : EMPTY;
		w = utf8len(r->cells[i]);
		if (w > ctx->table->width[i])
			ctx->table->width[i] = w;
	}

	return 0;
}

static err_t
Null(ctx_t *ctx)
{
	push(ctx, EMPTY);
	return 0;
}

static err_t
Now(ctx_t *ctx)
{
	time_t now;
	struct tm *tm;
	char buf[64], *str;
	size_t sz;

	now = time(0);
	tm = localtime(&now);
	sz = strftime(buf, sizeof buf, "%Y-%M-%D", tm);

	/* TODO(irek): This is not great.  Value like Today might be
	 * created only for purpose of filtering SELECT output, which
	 * means that lifetime of this store memory is most likely
	 * only for time of running single command, and not enrite
	 * program like most of the values.  The main storage is more
	 * suited for storing table cell values. */
	str = store(buf, sz);
	push(ctx, str);

	return 0;
}

int
boruta(char *fmt, ...)
{
	static ctx_t ctx = {0};
	char *str;
	va_list ap;
	unsigned i, n;

	boruta_err = 0;
	boruta_cols = 0;
	boruta_row = 0;

	/* New command */
	if (fmt) {
		memset(&ctx, 0, sizeof ctx);
		va_start(ap, fmt);
		n = vsnprintf(ctx.cmd, sizeof ctx.cmd, fmt, ap);
		va_end(ap);
		ctx.cp = ctx.cmd;
		if (n >= sizeof ctx.cmd)
			boruta_err = msg("Command length exceeded (%d)", sizeof ctx.cmd);
		return 0;
	}

	str = next(&ctx);
	if (!str)
		return 0;	/* End */

	for (i=0; words[i].name; i++)
		if (!strcmp(words[i].name, str))
			break;

	if (words[i].name)
		boruta_err = (*words[i].cb)(&ctx);
	else
		push(&ctx, str);

	return 1;
}
