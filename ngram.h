/**
 * ngram.h
 * Summary: Public API for descending sliding-window n-gram traversal.
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef NGRAM_H
#define NGRAM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One emitted chunk.
 * @param text Joined token text for the current span.
 * @param start Inclusive token start index.
 * @param end Inclusive token end index.
 * @param size Number of tokens in the span.
 * @return No return value.
 */
typedef struct {
    const char *text;
    int start;
    int end;
    int size;
} kc_ngram_chunk_t;

/**
 * Traversal options.
 * @param max_tokens Maximum window size.
 * @param min_tokens Minimum window size.
 * @param separators Byte set treated as token separators.
 * @return No return value.
 */
typedef struct {
    int max_tokens;
    int min_tokens;
    const char *separators;
} kc_ngram_options_t;

/**
 * Callback invoked for each emitted chunk.
 * @param chunk Current emitted chunk.
 * @param context Caller-provided opaque context.
 * @return 1 to close the span, 0 to keep it open, or -1 to abort traversal.
 */
typedef int (*kc_ngram_visit_fn)(const kc_ngram_chunk_t *chunk, void *context);

/**
 * Fills one options structure with default values.
 * @param options Destination options structure.
 * @return 0 on success, or -1 on invalid input.
 */
int kc_ngram_options_default(kc_ngram_options_t *options);

/**
 * Executes descending sliding-window traversal for the given input text.
 * @param input Input text to tokenize and traverse.
 * @param options Traversal options, or NULL to use defaults.
 * @param visit Callback invoked for each chunk.
 * @param context Caller-provided opaque context.
 * @return Number of emitted chunks, or -1 on failure.
 */
int kc_ngram_execute(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *context
);

#ifdef __cplusplus
}
#endif

#endif
