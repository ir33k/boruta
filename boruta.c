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

struct state {
	boruta_cb_t cb;
	void *ctx;
	char cmd[4096], *cp;
	char *stack[128];
	unsigned si;
	char *tname;		/* Selected table name */
	struct table *table;	/* Selected table by name if exist */
	char *eq[CMAX];
	char *neq[CMAX];
};

struct word {
	char *name;
	err_t (*cb)(struct state*);
};

static err_t msg(const char *fmt, ...);
static unsigned utf8len(char *str);
static unsigned width(unsigned w, char *cell);
static char *store(char *str, size_t len);
static struct table *table_get(char *name);
static struct table *table_new();
static struct row *row_new(struct table *t);
static int column_indexof(struct table *t, char *name);
static char *skip_whitespaces(char *str);
static char *each_line(char *str);
static char *each_cell(char *str);
static err_t parse(char *str);
static void push(struct state *state, char *word);
static char *pop(struct state *state);
static char *next(struct state *state);

/* Words */
static err_t Load(struct state*);
static err_t Save(struct state*);
static err_t Table(struct state*);
static err_t Eq(struct state*);
static err_t Neq(struct state*);
static err_t Select(struct state*);
static err_t Create(struct state*);
static err_t Insert(struct state*);
static err_t Set(struct state*);
static err_t Del(struct state*);
static err_t Drop(struct state*);
static err_t Null(struct state*);
static err_t Now(struct state*);

static struct table *tables = 0;
static struct word words[] = {
	"LOAD",	Load,
	"SAVE",	Save,
	"TABLE",	Table,
	"EQ",	Eq,
	"NEQ",	Neq,
	"SELECT",	Select,
	"CREATE",	Create,
	"INSERT",	Insert,
	"SET",	Set,
	"DEL",	Del,
	"DROP",	Drop,
	"NULL",	Null,
	"NOW",	Now,
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
	unsigned i=0, n;
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
				n = utf8len(cell);
				if (n > t->width[i])
					t->width[i] = n;
				break;
			}
		}
	}

	return 0;
}

static void
push(struct state *state, char *word)
{
	state->stack[state->si++] = word;
}

static char *
pop(struct state *state)
{
	return state->si ? state->stack[--(state->si)] : 0;
}

static char *
next(struct state *state)
{
	char terminate, *word;

	state->cp = skip_whitespaces(state->cp);
	if (!*state->cp)
		return 0;

	switch (*state->cp) {
	case '"': case '\'':	/* Explicite strings */
		terminate = *state->cp;
		(*state->cp)++;
		break;
	default:
		terminate = ' ';
	}

	word = state->cp;

	while (*state->cp && *state->cp != '\n' && *state->cp != terminate)
		state->cp++;

	*state->cp = 0;
	state->cp++;
	return word;
}

static err_t
Load(struct state *state)
{
	err_t why;
	char *str;
	struct stat fs = {0};
	FILE *fp;
	size_t sz;

	str = pop(state);
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
Save(struct state *state)
{
	char *str;
	FILE *fp;
	struct table *t;
	struct row *r;
	unsigned i;

	str = pop(state);
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
Table(struct state *state)
{
	state->tname = pop(state);
	state->table = table_get(state->tname);
	return 0;
}

static err_t
Eq(struct state *state)
{
	char *column, *value;
	int i;

	if (!state->table)
		return "Undefined table";

	while (1) {
		column = pop(state);
		value = pop(state);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(state->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		state->eq[i] = value;
	}

	return 0;
}

static err_t
Neq(struct state *state)
{
	char *column, *value;
	int i;

	if (!state->table)
		return "Undefined table";

	while (1) {
		column = pop(state);
		value = pop(state);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(state->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		state->neq[i] = value;
	}

	return 0;
}

static err_t
Select(struct state *state)
{
	struct row *r;
	char *str, *cols[CMAX], *rows[CMAX];
	int i,j, coli[CMAX], ri, cn;

	if (!state->table)
		return "Undefined table";

	ri = 0;
	cn = 0;

	for (i=CMAX; i>0;) {
		str = pop(state);
		if (!str)
			break;

		if (!strcmp(str, "*")) {
			for (j=0; i>0 && state->table->cols[j]; j++)
				coli[--i] = j;
			continue;
		}

		j = column_indexof(state->table, str);
		if (j == -1)
			return msg("Unknown column %s", str);

		coli[--i] = j;
	}
	cn = CMAX - i;
	for (j=0; j<cn; j++)
		coli[j] = coli[i++];

	for (i=0; i<cn; i++)
		cols[i] = state->table->cols[coli[i]];

	for (r = state->table->rows; r; r = r->next, ri++) {
		for (i=0; state->table->cols[i]; i++) {
			str = state->eq[i];
			if (str && strcmp(str, r->cells[i]))
				goto skip;

			str = state->neq[i];
			if (str && !strcmp(str, r->cells[i]))
				goto skip;
		}

		for (i=0; i<cn; i++)
			rows[i] = r->cells[coli[i]];

		(*state->cb)(state->ctx, 0, ri, cn, cols, rows);
	skip:
		continue;
	}

	return 0;
}

static err_t
Create(struct state *state)
{
	char *cell, *cells[CMAX] = {0};
	unsigned i, j;

	if (!state->tname)
		return "Missing table name";

	if (state->table)
		return "Table already exists";

	state->table = table_new();
	if (!state->table)
		return "Failed to create new table";

	state->table->name = store(state->tname, -1);

	for (i=0; i < CMAX-1 && (cell = pop(state)); i++)
		cells[i] = store(cell, -1);

	/* NOTE(irek): Decrement to preserve columns order. */
	for (j=0; i--; j++) {
		cell = cells[i];
		state->table->cols[j] = cell;
		state->table->width[j] = utf8len(cell);
	}

	return 0;
}

static err_t
Insert(struct state *state)
{
	struct row *r;
	char *column, *value;
	int i;
	unsigned w;
	char *cells[CMAX] = {0};

	if (!state->table)
		return "Undefined table";

	while (1) {
		column = pop(state);
		value = pop(state);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(state->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		cells[i] = value;
	}

	r = row_new(state->table);
	if (!r)
		return msg("Failed to create row for table %s", state->table->name);

	for (i=0; state->table->cols[i]; i++) {
		r->cells[i] = cells[i] ? store(cells[i], -1) : EMPTY;
		w = utf8len(r->cells[i]);
		if (w > state->table->width[i])
			state->table->width[i] = w;
	}

	return 0;
}

static err_t
Set(struct state *state)
{
	(void)state;
	return "Not implemented";
}

static err_t
Del(struct state *state)
{
	(void)state;
	return "Not implemented";
}

static err_t
Drop(struct state *state)
{
	(void)state;
	return "Not implemented";
}

static err_t
Null(struct state *state)
{
	push(state, EMPTY);
	return 0;
}

static err_t
Now(struct state *state)
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
	push(state, str);
	return 0;
}

void
boruta(boruta_cb_t cb, void *ctx, char *fmt, ...)
{
	static struct state state = {0};
	err_t why = 0;
	char *str;
	va_list ap;
	unsigned n;
	struct word *w;

	memset(&state, 0, sizeof state);

	state.cb = cb;
	state.ctx = ctx;

	va_start(ap, fmt);
	n = vsnprintf(state.cmd, sizeof state.cmd, fmt, ap);
	va_end(ap);

	state.cp = state.cmd;
	if (n >= sizeof state.cmd)
		why = msg("Command length exceeded (%d)", sizeof state.cmd);

	while (1) {
		if (why) {
			(*cb)(ctx, why, 0, 0, 0, 0);
			break;
		}

		str = next(&state);
		if (!str)
			break;

		for (w=words; w->name; w++)
			if (!strcmp(w->name, str))
				break;

		if (w->name)
			why = (*w->cb)(&state);
		else
			push(&state, str);
	}
}
