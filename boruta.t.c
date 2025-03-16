#include "boruta.c"
#include "walter.h"

struct ctx {
	int count;
	char *why;
};

static void
cb(void *_ctx, const char *why, int cn, char **cols, char **row)
{
	struct ctx *ctx = _ctx;

	(void)cn;
	(void)cols;
	(void)row;

	ctx->count++;
	ctx->why = (char*)why;
}

TEST("Create tables")
{
	struct ctx ctx = {0};

	boruta(cb, &ctx, "INFO");
	OK(ctx.count == 1);
	SAME(ctx.why, "No tables", -1);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "aaa TABLE col1 col2 col3 CREATE");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "aaa TABLE INFO");
	OK(ctx.count == 3);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "bbb TABLE col1 col2 col3 CREATE");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "bbb TABLE INFO");
	OK(ctx.count == 3);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "ccc TABLE col1 col2 col3 CREATE");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "ccc TABLE INFO");
	OK(ctx.count == 3);
	OK(ctx.why == 0);

	memset(&ctx, 0, sizeof ctx);
	boruta(cb, &ctx, "INFO");
	OK(ctx.count == 3);
	OK(ctx.why == 0);
}

TEST("Insert data")
{
	struct ctx ctx = {0};

	boruta(cb, &ctx, "aaa TABLE a1 col1 a2 col2 a3 col3 INSERT");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	boruta(cb, &ctx, "aaa TABLE b1 col1 b2 col2 b3 col3 INSERT");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	boruta(cb, &ctx, "aaa TABLE c1 col1 c2 col2 c3 col3 INSERT");
	OK(ctx.count == 0);
	OK(ctx.why == 0);

	boruta(cb, &ctx, "aaa TABLE * SELECT");
	OK(ctx.count == 3);
	OK(ctx.why == 0);
}
