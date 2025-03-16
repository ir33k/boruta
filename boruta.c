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

struct row {
	char *cells[CMAX];
	struct row *next;
};

struct table {
	char *name;
	int cn, rn;	/* Number of columns and rows */
	char *cols[CMAX];
	int width[CMAX];	/* Each column max width */
	struct row *rows, *last;
	struct table *next;
};

struct query {
	boruta_cb_t cb;
	void *ctx;
	char *stack[128];
	int si;
	char *tname;		/* Selected table name */
	struct table *table;	/* Selected table if exist */
	char *eq[CMAX], *neq[CMAX];
	int skip, limit;
};

struct word {
	char *name;
	char *(*cb)(struct query*);
};

static char *msg(const char *fmt, ...);
static int utf8len(char *str);
static int width(int w, char *cell);
static char *store(char *str, size_t len);
static struct table *table_get(char *name);
static struct table *table_new();
static void table_drop(struct table *t);
static struct row *row_new(struct table *t);
static void row_del(struct table *t, struct row *r);
static int column_indexof(struct table *t, char *name);
static char *skip_whitespaces(char *str);
static char *each_line(char *str);
static char *each_cell(char *str);
static char *parse(char *str);
static void push(struct query *query, char *word);
static char *pop(struct query *query);
static char *next(char **cp);
static int filter(struct query *query, struct row *r);

/* Words */
static char *Info(struct query*);
static char *Load(struct query*);
static char *Write(struct query*);
static char *Table(struct query*);
static char *Eq(struct query*);
static char *Neq(struct query*);
static char *Skip(struct query*);
static char *Limit(struct query*);
static char *Select(struct query*);
static char *Create(struct query*);
static char *Insert(struct query*);
static char *Set(struct query*);
static char *Del(struct query*);
static char *Drop(struct query*);
static char *Null(struct query*);
static char *Now(struct query*);

static struct table *tables = 0;
static struct word words[] = {
	"INFO", Info,
	"LOAD", Load,
	"WRITE", Write,
	"TABLE", Table,
	"EQ", Eq,
	"NEQ", Neq,
	"SKIP", Skip,
	"LIMIT", Limit,
	"SELECT", Select,
	"CREATE", Create,
	"INSERT", Insert,
	"SET", Set,
	"DEL", Del,
	"DROP", Drop,
	"NULL", Null,
	"NOW", Now,
	0
};

static char *
msg(const char *fmt, ...)
{
	static char buf[4096];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	return buf;
}

static int
utf8len(char *str)
{
	int i, n;

	for (i=0, n=0; str[i]; i++)
		if ((str[i] & 0xC0) != 0x80)
			n++;
	return n;
}

static int
width(int w, char *cell)
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

static void
table_drop(struct table *t)
{
	struct table *parent;

	parent = 0;

	if (t == tables)
		tables = t->next;
	else
		for (parent = tables; parent; parent = parent->next)
			if (parent->next == t)
				break;

	if (parent)
		parent->next = t->next;

	while (t->rows)
		row_del(t, t->rows);

	free(t);
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
	t->rn++;

	return new;
}

static void
row_del(struct table *t, struct row *r)
{
	struct row *parent;

	parent = 0;

	if (r == t->rows)
		t->rows = r->next;
	else
		for (parent = t->rows; parent; parent = parent->next)
			if (parent->next == r)
				break;

	if (parent)
		parent->next = r->next;

	if (r == t->last)
		t->last = parent;

	free(r);
	t->rn--;
}

static int
column_indexof(struct table *t, char *name)
{
	int i;

	for (i=0; i < t->cn; i++)
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

static char *
parse(char *str)
{
	struct table *t;
	struct row *r;
	int i, n, state;
	char *line, *next_line, *cell, *next_cell;

	t = 0;
	r = 0;
	r = 0;

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

			if (i > CMAX)
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
				t->cn++;
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
push(struct query *query, char *word)
{
	query->stack[query->si++] = word;
}

static char *
pop(struct query *query)
{
	return query->si ? query->stack[--(query->si)] : 0;
}

static char *
next(char **cp)
{
	char terminate, *word;

	*cp = skip_whitespaces(*cp);
	if (!**cp)
		return 0;

	switch (**cp) {
	case '"': case '\'':	/* Explicite strings */
		terminate = **cp;
		(*cp)++;
		break;
	default:
		terminate = ' ';
	}

	word = *cp;

	while (**cp && **cp != '\n' && **cp != terminate)
		(*cp)++;

	**cp = 0;
	(*cp)++;
	return word;
}

static int
filter(struct query *query, struct row *r)
{
	int i;
	char *str;

	for (i=0; i < query->table->cn; i++) {
		str = query->eq[i];
		if (str && strcmp(str, r->cells[i]))
			return 1;

		str = query->neq[i];
		if (str && !strcmp(str, r->cells[i]))
			return 1;
	}

	return 0;
}

static char *
Info(struct query *query)
{
	int i;
	char buf0[32], buf1[32], buf2[32], *cols[4], *row[4];
	struct table *t;

	if (query->tname && !query->table)
		return msg("No table named %s", query->tname);

	cols[0] = "index";
	row[0] = buf0;

	if (query->table) {	/* List table columns */
		if (query->table->cn == 0)
			return msg("Table %s has no columns", query->tname);

		cols[1] = "column";

		for (i=0; i < query->table->cn; i++) {
			snprintf(buf0, sizeof buf0, "%d", i);
			row[1] = query->table->cols[i];
			(*query->cb)(query->ctx, 0, 2, cols, row);
		}
	} else {		/* List tables */
		if (!tables)
			return "No tables";

		cols[1] = "columns";
		cols[2] = "rows";
		cols[3] = "table";
		row[1] = buf1;
		row[2] = buf2;

		for (i=0, t = tables; t; t = t->next, i++) {
			snprintf(buf0, sizeof buf0, "%d", i);
			snprintf(buf1, sizeof buf1, "%d", t->cn);
			snprintf(buf2, sizeof buf2, "%d", t->rn);
			row[3] = t->name;
			(*query->cb)(query->ctx, 0, 4, cols, row);
		}
	}

	return 0;
}

static char *
Load(struct query *query)
{
	char *why, *str;
	struct stat fs = {0};
	FILE *fp;
	size_t sz;

	str = pop(query);
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

static char *
Write(struct query *query)
{
	char *str;
	FILE *fp;
	struct table *t;
	struct row *r;
	int i;

	str = pop(query);
	if (!str)
		return "Missing file path";

	if (!tables)
		return "Nothing to write";

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

static char *
Table(struct query *query)
{
	query->tname = pop(query);
	query->table = table_get(query->tname);
	return 0;
}

static char *
Eq(struct query *query)
{
	char *column, *value;
	int i;

	if (!query->table)
		return "Undefined table";

	while (1) {
		column = pop(query);
		value = pop(query);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(query->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		query->eq[i] = value;
	}

	return 0;
}

static char *
Neq(struct query *query)
{
	char *column, *value;
	int i;

	if (!query->table)
		return "Undefined table";

	while (1) {
		column = pop(query);
		value = pop(query);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(query->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		query->neq[i] = value;
	}

	return 0;
}

static char *
Skip(struct query *query)
{
	char *str;

	str = pop(query);
	if (!str)
		return "Missing SKIP argument";

	query->skip = atoi(str);
	return 0;
}

static char *
Limit(struct query *query)
{
	char *str;

	str = pop(query);
	if (!str)
		return "Missing LIMIT argument";

	query->limit = atoi(str);
	return 0;
}

static char *
Select(struct query *query)
{
	struct row *r;
	char *str, *cols[CMAX], *row[CMAX];
	int i, j, coli[CMAX], cn;

	if (!query->table)
		return "Undefined table";

	cn = 0;

	for (i=CMAX; i>0;) {
		str = pop(query);
		if (!str)
			break;

		if (!strcmp(str, "*")) {
			for (j = query->table->cn; j > 0;)
				coli[--i] = --j;
			continue;
		}

		j = column_indexof(query->table, str);
		if (j == -1)
			return msg("Unknown column %s", str);

		coli[--i] = j;
	}
	cn = CMAX - i;
	for (j=0; j<cn; j++)
		coli[j] = coli[i++];

	for (i=0; i<cn; i++)
		cols[i] = query->table->cols[coli[i]];

	for (r = query->table->rows; r; r = r->next) {
		if (filter(query, r))
			continue;

		if (query->skip) {
			query->skip--;
			continue;
		}

		for (i=0; i<cn; i++)
			row[i] = r->cells[coli[i]];

		(*query->cb)(query->ctx, 0, cn, cols, row);

		if (query->limit && !(--query->limit))
			return 0;
	}

	return 0;
}

static char *
Create(struct query *query)
{
	char *cell, *cells[CMAX] = {0};
	int i, j;

	if (!query->tname)
		return "Missing table name";

	if (query->table)
		return "Table already exists";

	query->table = table_new();
	if (!query->table)
		return "Failed to create new table";

	query->table->name = store(query->tname, -1);

	for (i=0; i < CMAX-1 && (cell = pop(query)); i++)
		cells[i] = store(cell, -1);

	/* NOTE(irek): Decrement to preserve columns order. */
	for (j=0; i--; j++) {
		cell = cells[i];
		query->table->cols[j] = cell;
		query->table->width[j] = utf8len(cell);
	}

	return 0;
}

static char *
Insert(struct query *query)
{
	struct row *r;
	char *column, *value, *cells[CMAX]={0};
	int i, w;

	if (!query->table)
		return "Undefined table";

	while (1) {
		column = pop(query);
		value = pop(query);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(query->table, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		cells[i] = value;
	}

	r = row_new(query->table);
	if (!r)
		return msg("Failed to create row for table %s", query->table->name);

	for (i=0; i < query->table->cn; i++) {
		r->cells[i] = cells[i] ? store(cells[i], -1) : EMPTY;
		w = utf8len(r->cells[i]);
		if (w > query->table->width[i])
			query->table->width[i] = w;
	}

	return 0;
}

static char *
Set(struct query *query)
{
	struct table *t;
	struct row *r;
	char *column, *value, *new[CMAX]={0};
	int i;

	t = query->table;
	if (!t)
		return "Undefined table";

	while (1) {
		column = pop(query);
		value = pop(query);

		if (!column)
			break;	/* End, nothing more on stack */

		if (!value)
			return msg("Missing value for column %s", column);

		i = column_indexof(t, column);
		if (i == -1)
			return msg("Column %s don't exist", column);

		new[i] = value;
	}

	for (r = t->rows; r; r = r->next) {
		if (filter(query, r))
			continue;

		for (i=0; i < t->cn; i++)
			if (new[i])
				r->cells[i] = store(new[i], -1);
	}

	return 0;
}

static char *
Del(struct query *query)
{
	struct table *t;
	struct row *r;

	t = query->table;
	if (!t)
		return "Undefined table";

	for (r = t->rows; r; r = r->next) {
		if (filter(query, r))
			continue;

		row_del(t, r);
	}

	return 0;
}

static char *
Drop(struct query *query)
{
	if (query->tname) {
		if (!query->table)
			return msg("No table named %s", query->tname);

		table_drop(query->table);
	} else {
		while (tables)
			table_drop(tables);
	}

	return 0;
}

static char *
Null(struct query *query)
{
	push(query, EMPTY);
	return 0;
}

static char *
Now(struct query *query)
{
	time_t now;
	struct tm *tm;
	char buf[64], *str;
	unsigned sz;

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
	push(query, str);
	return 0;
}

void
boruta(boruta_cb_t cb, void *ctx, char *fmt, ...)
{
	struct query query = {0};
	char *why, *str, cmd[4096], *cp;
	va_list ap;
	unsigned n;
	struct word *w;

	why = 0;
	query.cb = cb;
	query.ctx = ctx;

	va_start(ap, fmt);
	n = vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);

	if (n >= sizeof cmd)
		why = msg("Command max length %d exceeded", sizeof cmd);

	cp = cmd;
	while (1) {
		if (why) {
			(*cb)(ctx, why, 0, 0, 0);
			break;
		}

		str = next(&cp);
		if (!str)
			break;

		for (w=words; w->name; w++)
			if (!strcmp(w->name, str))
				break;

		if (w->name)
			why = (*w->cb)(&query);
		else
			push(&query, str);
	}
}
