# ngram - Descending Sliding-Window N-Gram Traversal

`ngram` is a small C library and CLI for token chunk traversal using descending sliding windows with optional span closing.

## What It Does

For an input token stream, `ngram`:

1. splits text into tokens using a separator byte set
2. emits windows from `max_tokens` down to `min_tokens`
3. walks each window left to right
4. allows a callback to close a span so nested windows inside it are skipped

## Public API

The library is exposed through [ngram.h](./ngram.h).

Core types:

- `kc_ngram_options_t`
- `kc_ngram_chunk_t`
- `kc_ngram_visit_fn`

Core functions:

- `kc_ngram_options_default()`
- `kc_ngram_execute()`

The callback returns:

- `0` to keep traversal open
- `1` to close the current span
- `-1` to abort execution

## CLI Usage

```bash
ngram [options] [text]
```

Options:

- `--max`, `-max <n>`
- `--min`, `-min <n>`
- `--sep`, `-sep <s>`
- `--cmd`, `-cmd <cmd>`
- `--help`, `-h`
- `--version`, `-v`

When `--cmd` is provided, each chunk is still printed normally.
The command is then executed for that chunk with the chunk text on stdin.

If the command produces stdout, the current span is closed and nested windows inside it are skipped.

Examples:

```bash
ngram "one two three"
printf 'one two three' | ngram -max 2 -min 1
printf 'one two three' | ngram -cmd 'grep -qx "one two" && echo cut'
```

## Build

POSIX:

```bash
cc -O2 libngram.c ngram.c -o ngram
```

Windows:

```bash
cl /O2 /TC libngram.c ngram.c -o ngram
```

## Library Example

```c
#include "ngram.h"

static int handle_chunk(const kc_ngram_chunk_t *chunk, void *context) {
    (void)context;
    printf("%d:%d %s\n", chunk->start, chunk->end, chunk->text);
    return 0;
}

int main(void) {
    kc_ngram_options_t options;

    kc_ngram_options_default(&options);
    options.max_tokens = 3;
    options.min_tokens = 1;

    return kc_ngram_execute("one two three", &options, handle_chunk, NULL) < 0;
}
```

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
