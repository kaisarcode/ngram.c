/**
 * libngram.c
 * Summary: Descending sliding-window n-gram traversal library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "libngram.h"

#if !defined(KC_NGRAM_BUILD_VERSION) || KC_NGRAM_BUILD_VERSION + 0 == 0
#undef KC_NGRAM_BUILD_VERSION
#define KC_NGRAM_BUILD_VERSION 0ULL
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <signal.h>

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_STR,
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_NGRAM_MAX", offsetof(kc_ngram_options_t, max_tokens), KC_ENV_TYPE_INT },
    { "KC_NGRAM_MIN", offsetof(kc_ngram_options_t, min_tokens), KC_ENV_TYPE_INT },
    { "KC_NGRAM_CTRL", offsetof(kc_ngram_options_t, ctrl_path), KC_ENV_TYPE_STR },
};
static const int env_config_table_n =
    sizeof(env_config_table) / sizeof(env_config_table[0]);

typedef struct {
    int sig;
    kc_ngram_signal_callback_t cb;
} kc_ngram_signal_entry_t;

typedef struct {
    char *cmd;
    kc_ngram_ctrl_callback_t cb;
} kc_ngram_ctrl_entry_t;

typedef struct {
    int fd;
    char *buf;
    size_t used;
    size_t cap;
} kc_ngram_ctrl_conn_t;

static kc_ngram_t **g_signal_ctx_list = NULL;
static int g_signal_ctx_cap = 0;
static int g_signal_ctx_count = 0;

struct kc_ngram {
    kc_ngram_options_t owned_options;
    kc_ngram_options_t *options;
    kc_ngram_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    volatile sig_atomic_t stop_requested;
    int ctrl_fd;
    char *ctrl_path;
    kc_ngram_ctrl_entry_t *ctrl_handlers;
    int n_ctrl_handlers;
    int ctrl_handlers_cap;
    kc_ngram_ctrl_conn_t *ctrl_conns;
    int n_ctrl_conns;
    int ctrl_conns_cap;
};

typedef struct {
    size_t byte_start;
    size_t byte_end;
} kc_ngram_token_t;

typedef struct {
    kc_ngram_token_t *items;
    int count;
    int cap;
} kc_ngram_token_list_t;

typedef struct {
    int start;
    int end;
} kc_ngram_span_t;

/**
 * Duplicate a string into owned heap storage.
 * @param text Source string.
 * @return Owned copy, or NULL on allocation failure.
 */
static char *kc_ngram_strdup(const char *text) {
    size_t size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, size);
    return copy;
}

/**
 * Returns the active mutable options for one context.
 * @param ctx Context pointer.
 * @return Active options pointer, or NULL when unavailable.
 */
#ifndef _WIN32
static kc_ngram_options_t *kc_ngram_runtime_options(kc_ngram_t *ctx) {
    if (ctx == NULL) {
        return NULL;
    }

    if (ctx->options != NULL) {
        return ctx->options;
    }

    return &ctx->owned_options;
}

/**
 * Replace the owned separators backing store.
 * @param opts Options to update.
 * @param value Replacement separators.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_options_set_separators(
    kc_ngram_options_t *opts,
    const char *value
) {
    char *copy;

    if (opts == NULL || value == NULL) {
        return KC_NGRAM_ERROR;
    }

    copy = kc_ngram_strdup(value);
    if (copy == NULL) {
        return KC_NGRAM_ERROR;
    }

    free(opts->separators_storage);
    opts->separators_storage = copy;
    opts->separators = copy;
    return KC_NGRAM_OK;
}
#endif

/**
 * Replace the owned control socket path.
 * @param opts Options to update.
 * @param value Replacement control path.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_options_set_ctrl_path(
    kc_ngram_options_t *opts,
    const char *value
) {
    char *copy;

    if (opts == NULL) {
        return KC_NGRAM_ERROR;
    }

    if (value == NULL) {
        free(opts->ctrl_path);
        opts->ctrl_path = NULL;
        return KC_NGRAM_OK;
    }

    copy = kc_ngram_strdup(value);
    if (copy == NULL) {
        return KC_NGRAM_ERROR;
    }

    free(opts->ctrl_path);
    opts->ctrl_path = copy;
    return KC_NGRAM_OK;
}

/**
 * Returns whether one byte belongs to the configured separator set.
 * @param ch Byte to inspect.
 * @param separators Separator byte set.
 * @return 1 when the byte is a separator, or 0 otherwise.
 */
static int kc_ngram_is_separator(char ch, const char *separators) {
    if (separators == NULL || *separators == '\0') {
        return 0;
    }

    return strchr(separators, (unsigned char)ch) != NULL;
}

/**
 * Releases token storage.
 * @param tokens Token list to release.
 * @return No return value.
 */
static void kc_ngram_free_tokens(kc_ngram_token_list_t *tokens) {
    if (tokens == NULL) {
        return;
    }

    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->cap = 0;
}

/**
 * Ensures token storage capacity for at least one more element.
 * @param tokens Destination token list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_reserve_token_slot(kc_ngram_token_list_t *tokens) {
    kc_ngram_token_t *next_items;
    int next_cap;

    if (tokens == NULL) {
        return -1;
    }

    if (tokens->count < tokens->cap) {
        return 0;
    }

    next_cap = tokens->cap > 0 ? tokens->cap * 2 : 16;

    next_items = (kc_ngram_token_t *)realloc(
        tokens->items,
        (size_t)next_cap * sizeof(kc_ngram_token_t)
    );
    if (next_items == NULL) {
        return -1;
    }

    tokens->items = next_items;
    tokens->cap = next_cap;
    return 0;
}

/**
 * Appends one token span into the token list.
 * @param tokens Destination token list.
 * @param byte_start Inclusive token byte start.
 * @param byte_end Exclusive token byte end.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_token(
    kc_ngram_token_list_t *tokens,
    size_t byte_start,
    size_t byte_end
) {
    if (tokens == NULL || byte_end <= byte_start) {
        return -1;
    }

    if (kc_ngram_reserve_token_slot(tokens) != 0) {
        return -1;
    }

    tokens->items[tokens->count].byte_start = byte_start;
    tokens->items[tokens->count].byte_end = byte_end;
    tokens->count++;
    return 0;
}

/**
 * Splits input text into tokens using the configured separators.
 * @param input Input text to tokenize.
 * @param separators Separator byte set.
 * @param tokens Destination token list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_split_tokens(
    const char *input,
    const char *separators,
    kc_ngram_token_list_t *tokens
) {
    const char *cursor;
    const char *base;

    if (input == NULL || tokens == NULL) {
        return -1;
    }

    tokens->items = NULL;
    tokens->count = 0;
    tokens->cap = 0;
    cursor = input;
    base = input;

    while (*cursor != '\0') {
        const char *token_start;

        while (*cursor != '\0' && kc_ngram_is_separator(*cursor, separators)) {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        token_start = cursor;
        while (*cursor != '\0' && !kc_ngram_is_separator(*cursor, separators)) {
            cursor++;
        }

        if (cursor == token_start) {
            continue;
        }

        if (
            kc_ngram_push_token(
                tokens,
                (size_t)(token_start - base),
                (size_t)(cursor - base)
            ) != 0
        ) {
            kc_ngram_free_tokens(tokens);
            return -1;
        }
    }

    return 0;
}

/**
 * Finds where one span should be inserted by start index.
 * @param spans Sorted span array.
 * @param count Number of spans.
 * @param start Inclusive candidate start index.
 * @return Insertion index.
 */
static int kc_ngram_find_span_insert_index(
    const kc_ngram_span_t *spans,
    int count,
    int start
) {
    int left;
    int right;

    left = 0;
    right = count;

    while (left < right) {
        int mid;

        mid = left + (right - left) / 2;
        if (spans[mid].start < start) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

/**
 * Returns whether one span is fully contained inside a closed span.
 * @param start Inclusive candidate start index.
 * @param end Inclusive candidate end index.
 * @param closed_spans Closed span array sorted by start index.
 * @param closed_count Number of closed spans.
 * @return 1 when the span is closed, or 0 otherwise.
 */
static int kc_ngram_span_is_closed(
    int start,
    int end,
    const kc_ngram_span_t *closed_spans,
    int closed_count
) {
    int left;
    int right;
    int index;

    if (closed_spans == NULL || closed_count < 1) {
        return 0;
    }

    left = 0;
    right = closed_count;

    while (left < right) {
        int mid;

        mid = left + (right - left) / 2;
        if (closed_spans[mid].start <= start) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    index = left - 1;
    if (index < 0) {
        return 0;
    }

    return start >= closed_spans[index].start && end <= closed_spans[index].end;
}

/**
 * Ensures span storage capacity for at least one more element.
 * @param spans Span array pointer.
 * @param count Current number of spans.
 * @param cap Current allocated capacity.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_reserve_span_slot(
    kc_ngram_span_t **spans,
    int count,
    int *cap
) {
    kc_ngram_span_t *next_spans;
    int next_cap;

    if (spans == NULL || cap == NULL) {
        return -1;
    }

    if (count < *cap) {
        return 0;
    }

    next_cap = *cap > 0 ? (*cap * 2) : 16;

    next_spans = (kc_ngram_span_t *)realloc(
        *spans,
        (size_t)next_cap * sizeof(kc_ngram_span_t)
    );
    if (next_spans == NULL) {
        return -1;
    }

    *spans = next_spans;
    *cap = next_cap;
    return 0;
}

/**
 * Inserts one closed span while pruning redundant contained spans.
 * @param spans Sorted span array pointer.
 * @param count Current number of spans.
 * @param cap Current allocated capacity.
 * @param start Inclusive token start index.
 * @param end Inclusive token end index.
 * @return 0 on success, or -1 on allocation failure.
 */
static int kc_ngram_add_closed_span(
    kc_ngram_span_t **spans,
    int *count,
    int *cap,
    int start,
    int end
) {
    int insert_at;
    int remove_end;
    int tail_count;

    if (spans == NULL || count == NULL || cap == NULL) {
        return -1;
    }

    insert_at = kc_ngram_find_span_insert_index(*spans, *count, start);

    if (
        insert_at > 0 &&
        start >= (*spans)[insert_at - 1].start &&
        end <= (*spans)[insert_at - 1].end
    ) {
        return 0;
    }

    if (
        insert_at < *count &&
        (*spans)[insert_at].start == start &&
        (*spans)[insert_at].end >= end
    ) {
        return 0;
    }

    remove_end = insert_at;
    while (remove_end < *count && (*spans)[remove_end].end <= end) {
        remove_end++;
    }

    if (remove_end > insert_at) {
        memmove(
            *spans + insert_at,
            *spans + remove_end,
            (size_t)(*count - remove_end) * sizeof(kc_ngram_span_t)
        );
        *count -= remove_end - insert_at;
    }

    if (
        insert_at < *count &&
        start >= (*spans)[insert_at].start &&
        end <= (*spans)[insert_at].end
    ) {
        return 0;
    }

    if (kc_ngram_reserve_span_slot(spans, *count, cap) != 0) {
        return -1;
    }

    tail_count = *count - insert_at;
    if (tail_count > 0) {
        memmove(
            *spans + insert_at + 1,
            *spans + insert_at,
            (size_t)tail_count * sizeof(kc_ngram_span_t)
        );
    }

    (*spans)[insert_at].start = start;
    (*spans)[insert_at].end = end;
    (*count)++;
    return 0;
}

/**
 * Loads environment variables into an options structure.
 * Reads KC_NGRAM_MAX and KC_NGRAM_MIN from the environment.
 * @param opts Destination options structure.
 * @return No return value.
 */
void kc_ngram_options_load_env(kc_ngram_options_t *opts) {
    int i;
    if (!opts) return;
    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;
        if (!val) continue;
        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                if (env_config_table[i].offset == offsetof(kc_ngram_options_t, ctrl_path)) {
                    kc_ngram_options_set_ctrl_path(opts, val);
                }
                break;
            }
        }
    }
}

/**
 * Frees resources held by an options structure.
 * Currently a no-op placeholder for API symmetry.
 * @param opts Options structure to release.
 * @return No return value.
 */
void kc_ngram_options_free(kc_ngram_options_t *opts) {
    const char *owned_separators;

    if (opts == NULL) {
        return;
    }

    free(opts->ctrl_path);
    opts->ctrl_path = NULL;
    owned_separators = opts->separators_storage;
    free(opts->separators_storage);
    opts->separators_storage = NULL;
    if (opts->separators == NULL || opts->separators == owned_separators) {
        opts->separators = NULL;
    }
}

/**
 * Initialize a new ngram context.
 * @param out Pointer to receive the context pointer.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_open(kc_ngram_t **out) {
    kc_ngram_t *ctx;
    if (!out) return KC_NGRAM_ERROR;
    ctx = (kc_ngram_t *)calloc(1, sizeof(kc_ngram_t));
    if (!ctx) return KC_NGRAM_ERROR;
    if (kc_ngram_options_default(&ctx->owned_options) != 0) {
        free(ctx);
        return KC_NGRAM_ERROR;
    }
    ctx->options = &ctx->owned_options;
    ctx->ctrl_fd = -1;
    *out = ctx;
    return KC_NGRAM_OK;
}

/**
 * Release an ngram context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_ngram_close(kc_ngram_t *ctx) {
    int i;
    if (!ctx) return;
    kc_ngram_ctrl_close(ctx);
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] == ctx) {
            g_signal_ctx_list[i] = g_signal_ctx_list[--g_signal_ctx_count];
            break;
        }
    }
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        free(ctx->ctrl_handlers[i].cmd);
    }
    free(ctx->ctrl_handlers);
    free(ctx->ctrl_conns);
    free(ctx->signal_handlers);
    kc_ngram_options_free(&ctx->owned_options);
    free(ctx);
}

/**
 * Request stop for a specific ngram context.
 * @param ctx Context pointer.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_stop(kc_ngram_t *ctx) {
    if (!ctx) return KC_NGRAM_ERROR;
    ctx->stop_requested = 1;
    return KC_NGRAM_OK;
}

/**
 * Checks whether a stop request has been raised on the context.
 * @param ctx Context pointer.
 * @return 1 when stop was requested, or 0 otherwise.
 */
int kc_ngram_stop_requested(kc_ngram_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    return ctx->stop_requested ? 1 : 0;
}

/**
 * Attach one mutable options struct to the context runtime.
 * @param ctx Context pointer.
 * @param options Runtime options, or NULL to restore internal defaults.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_configure(kc_ngram_t *ctx, kc_ngram_options_t *options) {
    if (ctx == NULL) {
        return KC_NGRAM_ERROR;
    }

    if (options == NULL) {
        ctx->options = &ctx->owned_options;
        return KC_NGRAM_OK;
    }

    ctx->options = options;
    return KC_NGRAM_OK;
}

/**
 * Register a handler for a library-level signal number.
 * @param ctx ngram context.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_on_signal(kc_ngram_t *ctx, int sig, kc_ngram_signal_callback_t cb) {
    int i;
    if (!ctx) return KC_NGRAM_ERROR;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            if (cb) {
                ctx->signal_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&ctx->signal_handlers[i], &ctx->signal_handlers[i + 1],
                            (size_t)tail * sizeof(kc_ngram_signal_entry_t));
                }
                ctx->n_signal_handlers--;
            }
            return KC_NGRAM_OK;
        }
    }
    if (!cb) return KC_NGRAM_OK;
    if (ctx->n_signal_handlers >= ctx->signal_handlers_capacity) {
        int new_cap = ctx->signal_handlers_capacity ? ctx->signal_handlers_capacity * 2 : 4;
        kc_ngram_signal_entry_t *p = (kc_ngram_signal_entry_t *)realloc(ctx->signal_handlers,
            (size_t)new_cap * sizeof(kc_ngram_signal_entry_t));
        if (!p) return KC_NGRAM_ERROR;
        ctx->signal_handlers = p;
        ctx->signal_handlers_capacity = new_cap;
    }
    ctx->signal_handlers[ctx->n_signal_handlers].sig = sig;
    ctx->signal_handlers[ctx->n_signal_handlers].cb = cb;
    ctx->n_signal_handlers++;
    return KC_NGRAM_OK;
}

/**
 * Raise a library-level signal.
 * @param ctx ngram context.
 * @param sig Signal number to raise.
 * @return KC_NGRAM_OK if handled, or KC_NGRAM_ERROR if no handler.
 */
int kc_ngram_raise_signal(kc_ngram_t *ctx, int sig) {
    int i;
    if (!ctx) return KC_NGRAM_ERROR;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            ctx->signal_handlers[i].cb(ctx);
            return KC_NGRAM_OK;
        }
    }
    return KC_NGRAM_ERROR;
}

/**
 * Set the internal signal-listener context.
 * @param ctx ngram context.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR if ctx is NULL.
 */
int kc_ngram_listen_signals(kc_ngram_t *ctx) {
    if (!ctx) return KC_NGRAM_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_ngram_t **new_list = (kc_ngram_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_ngram_t *));
        if (!new_list) return KC_NGRAM_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
    return KC_NGRAM_OK;
}

/**
 * Generic signal-listener compatible with signal() / sigaction().
 * @param sig OS signal number.
 * @return None.
 */
void kc_ngram_signal_listener(int sig) {
    int i;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] &&
            kc_ngram_raise_signal(g_signal_ctx_list[i], sig) == 0)
            return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Wire an OS signal to the library signal listener.
 * @param ctx ngram context.
 * @param sig_id OS signal number.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_listen_signal(kc_ngram_t *ctx, int sig_id) {
    if (!ctx) return KC_NGRAM_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_ngram_t **new_list = (kc_ngram_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_ngram_t *));
        if (!new_list) return KC_NGRAM_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
#ifdef _WIN32
    (void)sig_id;
#else
    signal(sig_id, kc_ngram_signal_listener);
#endif
    return KC_NGRAM_OK;
}

/**
 * Fills one options structure with default traversal values.
 * @param options Destination options structure.
 * @return 0 on success, or -1 on invalid input.
 */
int kc_ngram_options_default(kc_ngram_options_t *options) {
    if (options == NULL) {
        return -1;
    }

    options->max_tokens = 10;
    options->min_tokens = 1;
    options->separators = " \t\r\n";
    options->separators_storage = NULL;
    options->ctrl_path = NULL;
    return 0;
}

/**
 * Executes descending sliding-window traversal for the input text.
 * @param input Input text to tokenize and traverse.
 * @param options Traversal options, or NULL to use defaults.
 * @param visit Callback invoked for each emitted chunk.
 * @param context Caller-provided opaque context.
 * @return Number of emitted chunks, or -1 on failure.
 */
int kc_ngram_execute(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *context
) {
    kc_ngram_options_t local_options;
    kc_ngram_token_list_t tokens;
    kc_ngram_span_t *closed_spans;
    int closed_count;
    int closed_cap;
    int loop_max;
    int window_size;
    int start;
    int emitted;

    if (input == NULL || visit == NULL) {
        return -1;
    }

    if (options == NULL) {
        if (kc_ngram_options_default(&local_options) != 0) {
            return -1;
        }

        options = &local_options;
    }

    if (options->min_tokens < 1 || (options->max_tokens > 0 && options->max_tokens < options->min_tokens)) {
        return -1;
    }

    if (kc_ngram_split_tokens(input, options->separators, &tokens) != 0) {
        return -1;
    }

    if (tokens.count == 0) {
        kc_ngram_free_tokens(&tokens);
        return 0;
    }

    closed_spans = NULL;
    closed_count = 0;
    closed_cap = 0;

    loop_max = options->max_tokens;
    if (loop_max == 0 || tokens.count < loop_max) {
        loop_max = tokens.count;
    }

    emitted = 0;

    for (window_size = loop_max; window_size >= options->min_tokens; window_size--) {
        for (start = 0; start <= tokens.count - window_size; start++) {
            kc_ngram_chunk_t chunk;
            int end;
            int decision;

            end = start + window_size - 1;
            if (kc_ngram_span_is_closed(start, end, closed_spans, closed_count)) {
                continue;
            }

            chunk.input = input;
            chunk.byte_start = tokens.items[start].byte_start;
            chunk.byte_end = tokens.items[end].byte_end;
            chunk.start = start;
            chunk.end = end;
            chunk.size = window_size;

            decision = visit(&chunk, context);
            if (decision == KC_NGRAM_ESTOP) {
                free(closed_spans);
                kc_ngram_free_tokens(&tokens);
                return KC_NGRAM_ESTOP;
            }
            if (decision < 0) {
                free(closed_spans);
                kc_ngram_free_tokens(&tokens);
                return -1;
            }

            emitted++;

            if (decision == 1) {
                if (
                    kc_ngram_add_closed_span(
                        &closed_spans,
                        &closed_count,
                        &closed_cap,
                        start,
                        end
                    ) != 0
                ) {
                    free(closed_spans);
                    kc_ngram_free_tokens(&tokens);
                    return -1;
                }
            }
        }
    }

    free(closed_spans);
    kc_ngram_free_tokens(&tokens);
    return emitted;
}

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_ngram_version(void) {
    return (uint64_t)KC_NGRAM_BUILD_VERSION;
}

#ifndef _WIN32

/**
 * Send one response string through the control connection.
 * @param fd Connected control file descriptor.
 * @param msg Response text.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_send(int fd, const char *msg) {
    size_t len;

    if (msg == NULL) {
        return KC_NGRAM_ERROR;
    }

    len = strlen(msg);
    return (size_t)write(fd, msg, len) == len ? KC_NGRAM_OK : KC_NGRAM_ERROR;
}

/**
 * Send the default HELP response.
 * @param ctx Context pointer.
 * @param fd Connected control file descriptor.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_default_help(
    kc_ngram_t *ctx,
    int fd,
    int argc,
    char **argv
) {
    char tmp[4096];
    size_t pos;
    int i;

    (void)argc;
    (void)argv;

    pos = 0U;
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        size_t len;

        len = strlen(ctx->ctrl_handlers[i].cmd);
        if (pos + len + 2U > sizeof(tmp)) {
            break;
        }
        if (pos > 0U) {
            tmp[pos] = ' ';
            pos++;
        }
        memcpy(tmp + pos, ctx->ctrl_handlers[i].cmd, len);
        pos += len;
    }

    if (pos + 1U > sizeof(tmp)) {
        pos = sizeof(tmp) - 1U;
    }
    tmp[pos] = '\n';
    kc_ngram_ctrl_send(fd, "OK ");
    write(fd, tmp, pos + 1U);
    return KC_NGRAM_OK;
}

/**
 * Send the default STOP response.
 * @param ctx Context pointer.
 * @param fd Connected control file descriptor.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_default_stop(
    kc_ngram_t *ctx,
    int fd,
    int argc,
    char **argv
) {
    (void)argc;
    (void)argv;

    if (kc_ngram_stop(ctx) == KC_NGRAM_OK) {
        kc_ngram_ctrl_send(fd, "OK\n");
    } else {
        kc_ngram_ctrl_send(fd, "ERR\n");
    }
    return KC_NGRAM_OK;
}

/**
 * Send the default GET response.
 * @param ctx Context pointer.
 * @param fd Connected control file descriptor.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_default_get(
    kc_ngram_t *ctx,
    int fd,
    int argc,
    char **argv
) {
    kc_ngram_options_t *options;
    char tmp[256];

    if (argc < 2) {
        return KC_NGRAM_ERROR;
    }

    options = kc_ngram_runtime_options(ctx);
    if (options == NULL) {
        return KC_NGRAM_ERROR;
    }

    if (strcmp(argv[1], "max") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %d\n", options->max_tokens);
    } else if (strcmp(argv[1], "min") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %d\n", options->min_tokens);
    } else if (strcmp(argv[1], "sep") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %s\n", options->separators ? options->separators : "");
    } else {
        return KC_NGRAM_ERROR;
    }

    write(fd, tmp, strlen(tmp));
    return KC_NGRAM_OK;
}

/**
 * Send the default SET response.
 * @param ctx Context pointer.
 * @param fd Connected control file descriptor.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_default_set(
    kc_ngram_t *ctx,
    int fd,
    int argc,
    char **argv
) {
    kc_ngram_options_t *options;

    if (argc < 3) {
        kc_ngram_ctrl_send(fd, "ERR missing value\n");
        return KC_NGRAM_OK;
    }

    options = kc_ngram_runtime_options(ctx);
    if (options == NULL) {
        kc_ngram_ctrl_send(fd, "ERR\n");
        return KC_NGRAM_OK;
    }

    if (strcmp(argv[1], "max") == 0) {
        char *end;
        long value;

        value = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || value < 0L || value > 2147483647L) {
            kc_ngram_ctrl_send(fd, "ERR invalid value\n");
            return KC_NGRAM_OK;
        }
        if (value != 0L && value < (long)options->min_tokens) {
            kc_ngram_ctrl_send(fd, "ERR invalid value\n");
            return KC_NGRAM_OK;
        }
        options->max_tokens = (int)value;
        kc_ngram_ctrl_send(fd, "OK\n");
        return KC_NGRAM_OK;
    }

    if (strcmp(argv[1], "min") == 0) {
        char *end;
        long value;

        value = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || value < 1L || value > 2147483647L) {
            kc_ngram_ctrl_send(fd, "ERR invalid value\n");
            return KC_NGRAM_OK;
        }
        if (options->max_tokens > 0 && value > (long)options->max_tokens) {
            kc_ngram_ctrl_send(fd, "ERR invalid value\n");
            return KC_NGRAM_OK;
        }
        options->min_tokens = (int)value;
        kc_ngram_ctrl_send(fd, "OK\n");
        return KC_NGRAM_OK;
    }

    if (strcmp(argv[1], "sep") == 0) {
        if (kc_ngram_options_set_separators(options, argv[2]) != KC_NGRAM_OK) {
            kc_ngram_ctrl_send(fd, "ERR out of memory\n");
            return KC_NGRAM_OK;
        }
        kc_ngram_ctrl_send(fd, "OK\n");
        return KC_NGRAM_OK;
    }

    kc_ngram_ctrl_send(fd, "ERR unknown key\n");
    return KC_NGRAM_OK;
}

/**
 * Dispatch one complete control command line.
 * @param ctx Context pointer.
 * @param fd Connected control file descriptor.
 * @param line Full command line.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
static int kc_ngram_ctrl_dispatch(kc_ngram_t *ctx, int fd, const char *line) {
    char *copy;
    char *argv[64];
    int argc;
    int i;

    copy = kc_ngram_strdup(line);
    if (copy == NULL) {
        return KC_NGRAM_ERROR;
    }

    argc = 0;
    argv[argc] = strtok(copy, " \t\r\n");
    if (argv[argc] != NULL) {
        argc++;
        while (argc < 64 && (argv[argc] = strtok(NULL, " \t\r\n")) != NULL) {
            argc++;
        }
    }

    if (argc == 0) {
        free(copy);
        return KC_NGRAM_OK;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, argv[0]) == 0) {
            ctx->ctrl_handlers[i].cb(ctx, fd, argc, argv);
            free(copy);
            return KC_NGRAM_OK;
        }
    }

    kc_ngram_ctrl_send(fd, "ERR unknown command\n");
    free(copy);
    return KC_NGRAM_OK;
}

#endif

/**
 * Register a control command handler.
 * @param ctx Context pointer.
 * @param cmd Control command name.
 * @param cb Callback function, or NULL to remove.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_ctrl_on(kc_ngram_t *ctx, const char *cmd, kc_ngram_ctrl_callback_t cb) {
    int i;

    if (ctx == NULL || cmd == NULL) {
        return KC_NGRAM_ERROR;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, cmd) == 0) {
            if (cb != NULL) {
                ctx->ctrl_handlers[i].cb = cb;
            } else {
                int tail;

                free(ctx->ctrl_handlers[i].cmd);
                tail = ctx->n_ctrl_handlers - i - 1;
                if (tail > 0) {
                    memmove(
                        &ctx->ctrl_handlers[i],
                        &ctx->ctrl_handlers[i + 1],
                        (size_t)tail * sizeof(kc_ngram_ctrl_entry_t)
                    );
                }
                ctx->n_ctrl_handlers--;
            }
            return KC_NGRAM_OK;
        }
    }

    if (cb == NULL) {
        return KC_NGRAM_OK;
    }

    if (ctx->n_ctrl_handlers >= ctx->ctrl_handlers_cap) {
        kc_ngram_ctrl_entry_t *next_handlers;
        int next_cap;

        next_cap = ctx->ctrl_handlers_cap > 0 ? ctx->ctrl_handlers_cap * 2 : 4;
        next_handlers = (kc_ngram_ctrl_entry_t *)realloc(
            ctx->ctrl_handlers,
            (size_t)next_cap * sizeof(kc_ngram_ctrl_entry_t)
        );
        if (next_handlers == NULL) {
            return KC_NGRAM_ERROR;
        }
        ctx->ctrl_handlers = next_handlers;
        ctx->ctrl_handlers_cap = next_cap;
    }

    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd = kc_ngram_strdup(cmd);
    if (ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd == NULL) {
        return KC_NGRAM_ERROR;
    }
    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cb = cb;
    ctx->n_ctrl_handlers++;
    return KC_NGRAM_OK;
}

/**
 * Remove a control command handler.
 * @param ctx Context pointer.
 * @param cmd Control command name.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_ctrl_off(kc_ngram_t *ctx, const char *cmd) {
    return kc_ngram_ctrl_on(ctx, cmd, NULL);
}

/**
 * Open a Unix domain socket for control commands.
 * @param ctx Context pointer.
 * @param path Socket path.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_ctrl_open(kc_ngram_t *ctx, const char *path) {
#ifndef _WIN32
    struct sockaddr_un addr;
    int fd;
    int flags;

    if (ctx == NULL || path == NULL) {
        return KC_NGRAM_ERROR;
    }

    if (ctx->ctrl_fd >= 0) {
        return KC_NGRAM_ERROR;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return KC_NGRAM_ERROR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1U);
    addr.sun_path[sizeof(addr.sun_path) - 1U] = '\0';

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return KC_NGRAM_ERROR;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(path);
        return KC_NGRAM_ERROR;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    ctx->ctrl_path = kc_ngram_strdup(path);
    if (ctx->ctrl_path == NULL) {
        close(fd);
        unlink(path);
        return KC_NGRAM_ERROR;
    }

    ctx->ctrl_fd = fd;
    if (
        kc_ngram_ctrl_on(ctx, "HELP", kc_ngram_ctrl_default_help) != KC_NGRAM_OK ||
        kc_ngram_ctrl_on(ctx, "STOP", kc_ngram_ctrl_default_stop) != KC_NGRAM_OK ||
        kc_ngram_ctrl_on(ctx, "GET", kc_ngram_ctrl_default_get) != KC_NGRAM_OK ||
        kc_ngram_ctrl_on(ctx, "SET", kc_ngram_ctrl_default_set) != KC_NGRAM_OK
    ) {
        kc_ngram_ctrl_close(ctx);
        return KC_NGRAM_ERROR;
    }
    return KC_NGRAM_OK;
#else
    (void)ctx;
    (void)path;
    return KC_NGRAM_ERROR;
#endif
}

/**
 * Close the control socket and active control connections.
 * @param ctx Context pointer.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_ctrl_close(kc_ngram_t *ctx) {
#ifndef _WIN32
    int i;

    if (ctx == NULL) {
        return KC_NGRAM_OK;
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            close(ctx->ctrl_conns[i].fd);
        }
        free(ctx->ctrl_conns[i].buf);
        ctx->ctrl_conns[i].buf = NULL;
        ctx->ctrl_conns[i].used = 0U;
        ctx->ctrl_conns[i].cap = 0U;
    }
    ctx->n_ctrl_conns = 0;

    if (ctx->ctrl_fd >= 0) {
        close(ctx->ctrl_fd);
        ctx->ctrl_fd = -1;
    }

    if (ctx->ctrl_path != NULL) {
        unlink(ctx->ctrl_path);
        free(ctx->ctrl_path);
        ctx->ctrl_path = NULL;
    }

    return KC_NGRAM_OK;
#else
    (void)ctx;
    return KC_NGRAM_OK;
#endif
}

/**
 * Poll the control socket and dispatch pending control commands.
 * @param ctx Context pointer.
 * @return Number of handled commands, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_ctrl_poll(kc_ngram_t *ctx) {
#ifndef _WIN32
    struct pollfd pfds[64];
    int handled;
    int i;
    int nfds;

    if (ctx == NULL || ctx->ctrl_fd < 0) {
        return 0;
    }

    handled = 0;
    nfds = 0;
    pfds[nfds].fd = ctx->ctrl_fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;

    for (i = 0; i < ctx->n_ctrl_conns && nfds < 64; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            pfds[nfds].fd = ctx->ctrl_conns[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
    }

    if (poll(pfds, (nfds_t)nfds, 0) < 0) {
        return KC_NGRAM_ERROR;
    }

    if (pfds[0].revents & POLLIN) {
        int conn_fd;

        conn_fd = accept(ctx->ctrl_fd, NULL, NULL);
        if (conn_fd >= 0) {
            int flags;

            flags = fcntl(conn_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);
            }

            if (ctx->n_ctrl_conns >= ctx->ctrl_conns_cap) {
                kc_ngram_ctrl_conn_t *next_conns;
                int next_cap;

                next_cap = ctx->ctrl_conns_cap > 0 ? ctx->ctrl_conns_cap * 2 : 4;
                next_conns = (kc_ngram_ctrl_conn_t *)realloc(
                    ctx->ctrl_conns,
                    (size_t)next_cap * sizeof(kc_ngram_ctrl_conn_t)
                );
                if (next_conns != NULL) {
                    ctx->ctrl_conns = next_conns;
                    ctx->ctrl_conns_cap = next_cap;
                }
            }

            if (ctx->n_ctrl_conns < ctx->ctrl_conns_cap) {
                ctx->ctrl_conns[ctx->n_ctrl_conns].fd = conn_fd;
                ctx->ctrl_conns[ctx->n_ctrl_conns].buf = NULL;
                ctx->ctrl_conns[ctx->n_ctrl_conns].used = 0U;
                ctx->ctrl_conns[ctx->n_ctrl_conns].cap = 0U;
                ctx->n_ctrl_conns++;
            } else {
                close(conn_fd);
            }
        }
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        char chunk[256];
        int j;
        int pidx;

        pidx = -1;
        for (j = 1; j < nfds; j++) {
            if (pfds[j].fd == ctx->ctrl_conns[i].fd) {
                pidx = j;
                break;
            }
        }
        if (pidx < 0 || !(pfds[pidx].revents & POLLIN)) {
            continue;
        }

        for (;;) {
            ssize_t n;

            n = read(ctx->ctrl_conns[i].fd, chunk, sizeof(chunk));
            if (n < 0) {
                break;
            }
            if (n == 0) {
                close(ctx->ctrl_conns[i].fd);
                ctx->ctrl_conns[i].fd = -1;
                free(ctx->ctrl_conns[i].buf);
                ctx->ctrl_conns[i].buf = NULL;
                ctx->ctrl_conns[i].used = 0U;
                ctx->ctrl_conns[i].cap = 0U;
                break;
            }

            {
                size_t offset;

                offset = 0U;
                while ((size_t)n > offset) {
                    char *newline;

                    newline = (char *)memchr(chunk + offset, '\n', (size_t)n - offset);
                    if (newline == NULL) {
                        size_t avail;
                        char *next_buf;
                        size_t next_cap;

                        avail = (size_t)n - offset;
                        if (ctx->ctrl_conns[i].used + avail + 1U > ctx->ctrl_conns[i].cap) {
                            next_cap = ctx->ctrl_conns[i].cap > 0U ? ctx->ctrl_conns[i].cap * 2U : 256U;
                            while (next_cap < ctx->ctrl_conns[i].used + avail + 1U) {
                                next_cap *= 2U;
                            }
                            next_buf = (char *)realloc(ctx->ctrl_conns[i].buf, next_cap);
                            if (next_buf == NULL) {
                                break;
                            }
                            ctx->ctrl_conns[i].buf = next_buf;
                            ctx->ctrl_conns[i].cap = next_cap;
                        }
                        memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, avail);
                        ctx->ctrl_conns[i].used += avail;
                        ctx->ctrl_conns[i].buf[ctx->ctrl_conns[i].used] = '\0';
                        offset = (size_t)n;
                    } else {
                        char *next_buf;
                        size_t line_len;
                        size_t next_cap;
                        size_t total;

                        line_len = (size_t)(newline - (chunk + offset));
                        total = ctx->ctrl_conns[i].used + line_len;
                        if (total + 1U > ctx->ctrl_conns[i].cap) {
                            next_cap = ctx->ctrl_conns[i].cap > 0U ? ctx->ctrl_conns[i].cap * 2U : 256U;
                            while (next_cap < total + 1U) {
                                next_cap *= 2U;
                            }
                            next_buf = (char *)realloc(ctx->ctrl_conns[i].buf, next_cap);
                            if (next_buf == NULL) {
                                break;
                            }
                            ctx->ctrl_conns[i].buf = next_buf;
                            ctx->ctrl_conns[i].cap = next_cap;
                        }
                        if (line_len > 0U) {
                            memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, line_len);
                        }
                        ctx->ctrl_conns[i].buf[total] = '\0';
                        kc_ngram_ctrl_dispatch(ctx, ctx->ctrl_conns[i].fd, ctx->ctrl_conns[i].buf);
                        handled++;
                        ctx->ctrl_conns[i].used = 0U;
                        offset += line_len + 1U;
                    }
                }
            }
        }
    }

    {
        int write_idx;

        write_idx = 0;
        for (i = 0; i < ctx->n_ctrl_conns; i++) {
            if (ctx->ctrl_conns[i].fd >= 0) {
                if (write_idx != i) {
                    ctx->ctrl_conns[write_idx] = ctx->ctrl_conns[i];
                }
                write_idx++;
            }
        }
        ctx->n_ctrl_conns = write_idx;
    }

    return handled;
#else
    (void)ctx;
    return 0;
#endif
}
