/**
 * libngram.c
 * Summary: Descending sliding-window n-gram traversal library.
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "ngram.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t length;
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

typedef struct {
    char *data;
    size_t cap;
} kc_ngram_text_buffer_t;

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
 * Releases all token strings and their container.
 * @param tokens Token list to release.
 * @return No return value.
 */
static void kc_ngram_free_tokens(kc_ngram_token_list_t *tokens) {
    int i;

    if (tokens == NULL) {
        return;
    }

    for (i = 0; i < tokens->count; i++) {
        free(tokens->items[i].text);
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
 * Appends one token copy into the token list.
 * @param tokens Destination token list.
 * @param start Start pointer of the token slice.
 * @param length Token length in bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_token(
    kc_ngram_token_list_t *tokens,
    const char *start,
    size_t length
) {
    char *copy;

    if (tokens == NULL || start == NULL || length == 0U) {
        return -1;
    }

    if (kc_ngram_reserve_token_slot(tokens) != 0) {
        return -1;
    }

    copy = (char *)malloc(length + 1U);
    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, start, length);
    copy[length] = '\0';

    tokens->items[tokens->count].text = copy;
    tokens->items[tokens->count].length = length;
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

    if (input == NULL || tokens == NULL) {
        return -1;
    }

    tokens->items = NULL;
    tokens->count = 0;
    tokens->cap = 0;
    cursor = input;

    while (*cursor != '\0') {
        const char *token_start;
        size_t token_length;

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

        token_length = (size_t)(cursor - token_start);
        if (token_length == 0U) {
            continue;
        }

        if (kc_ngram_push_token(tokens, token_start, token_length) != 0) {
            kc_ngram_free_tokens(tokens);
            return -1;
        }
    }

    return 0;
}

/**
 * Returns whether one span is fully contained inside a closed span.
 * @param start Inclusive candidate start index.
 * @param end Inclusive candidate end index.
 * @param closed_spans Closed span array.
 * @param closed_count Number of closed spans.
 * @return 1 when the span is closed, or 0 otherwise.
 */
static int kc_ngram_span_is_closed(
    int start,
    int end,
    const kc_ngram_span_t *closed_spans,
    int closed_count
) {
    int i;

    for (i = 0; i < closed_count; i++) {
        if (start >= closed_spans[i].start && end <= closed_spans[i].end) {
            return 1;
        }
    }

    return 0;
}

/**
 * Ensures the shared joined-text buffer can hold the given size.
 * @param buffer Shared text buffer.
 * @param size Required size including trailing NUL.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_text_buffer_reserve(
    kc_ngram_text_buffer_t *buffer,
    size_t size
) {
    char *next_data;
    size_t next_cap;

    if (buffer == NULL) {
        return -1;
    }

    if (size <= buffer->cap) {
        return 0;
    }

    next_cap = buffer->cap > 0U ? buffer->cap : 64U;
    while (next_cap < size) {
        next_cap *= 2U;
    }

    next_data = (char *)realloc(buffer->data, next_cap);
    if (next_data == NULL) {
        return -1;
    }

    buffer->data = next_data;
    buffer->cap = next_cap;
    return 0;
}

/**
 * Joins one token window into a reusable space-delimited buffer.
 * @param tokens Source token list.
 * @param start Inclusive token start index.
 * @param size Number of tokens in the window.
 * @param buffer Shared reusable text buffer.
 * @param out_text Destination for the joined chunk pointer.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_join_tokens(
    const kc_ngram_token_list_t *tokens,
    int start,
    int size,
    kc_ngram_text_buffer_t *buffer,
    const char **out_text
) {
    int i;
    size_t length;
    size_t offset;

    if (
        tokens == NULL ||
        buffer == NULL ||
        out_text == NULL ||
        start < 0 ||
        size < 1
    ) {
        return -1;
    }

    length = 1U;
    for (i = 0; i < size; i++) {
        length += tokens->items[start + i].length;
        if (i + 1 < size) {
            length++;
        }
    }

    if (kc_ngram_text_buffer_reserve(buffer, length) != 0) {
        return -1;
    }

    offset = 0U;
    for (i = 0; i < size; i++) {
        size_t token_length;

        token_length = tokens->items[start + i].length;
        memcpy(
            buffer->data + offset,
            tokens->items[start + i].text,
            token_length
        );
        offset += token_length;

        if (i + 1 < size) {
            buffer->data[offset++] = ' ';
        }
    }

    buffer->data[offset] = '\0';
    *out_text = buffer->data;
    return 0;
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
    kc_ngram_text_buffer_t text_buffer;
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

    if (options->min_tokens < 1 || options->max_tokens < options->min_tokens) {
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
    text_buffer.data = NULL;
    text_buffer.cap = 0U;

    loop_max = options->max_tokens;
    if (tokens.count < loop_max) {
        loop_max = tokens.count;
    }

    emitted = 0;

    for (window_size = loop_max; window_size >= options->min_tokens; window_size--) {
        for (start = 0; start <= tokens.count - window_size; start++) {
            kc_ngram_chunk_t chunk;
            const char *text;
            int end;
            int decision;

            end = start + window_size - 1;
            if (kc_ngram_span_is_closed(start, end, closed_spans, closed_count)) {
                continue;
            }

            if (
                kc_ngram_join_tokens(
                    &tokens,
                    start,
                    window_size,
                    &text_buffer,
                    &text
                ) != 0
            ) {
                free(text_buffer.data);
                free(closed_spans);
                kc_ngram_free_tokens(&tokens);
                return -1;
            }

            chunk.text = text;
            chunk.start = start;
            chunk.end = end;
            chunk.size = window_size;

            decision = visit(&chunk, context);
            if (decision < 0) {
                free(text_buffer.data);
                free(closed_spans);
                kc_ngram_free_tokens(&tokens);
                return -1;
            }

            emitted++;

            if (decision == 1) {
                if (
                    kc_ngram_reserve_span_slot(
                        &closed_spans,
                        closed_count,
                        &closed_cap
                    ) != 0
                ) {
                    free(text_buffer.data);
                    free(closed_spans);
                    kc_ngram_free_tokens(&tokens);
                    return -1;
                }

                closed_spans[closed_count].start = start;
                closed_spans[closed_count].end = end;
                closed_count++;
            }
        }
    }

    free(text_buffer.data);
    free(closed_spans);
    kc_ngram_free_tokens(&tokens);
    return emitted;
}
