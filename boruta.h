typedef void (*boruta_cb_t)(void *ctx, const char *why,
                            int ri, int cn, char **cols, char **rows);

void boruta(boruta_cb_t cb, void *ctx, char *fmt, ...);
