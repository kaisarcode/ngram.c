# ngram - Descending Sliding-Window N-Gram Traversal

`ngram` is a small C library and CLI for token chunk traversal using descending sliding windows with optional span closing.

## What It Does

For an input token stream, `ngram`:

1. splits text into tokens using a separator byte set
2. emits windows from `max_tokens` down to `min_tokens`
3. walks each window left to right
4. allows a callback to close a span so future windows fully contained inside it are skipped

Separator configuration affects tokenization only. Emitted chunks always point to
the exact original byte range from the source input, including commas, tabs,
repeated spaces, or any other separators that appeared between the first and
last token in the span.

## Public API

The library is exposed through [ngram.h](./ngram.h).

Core types:

- `kc_ngram_options_t`
- `kc_ngram_chunk_t`
- `kc_ngram_visit_fn`

`kc_ngram_chunk_t` exposes the original source span:

- `input`: original input buffer
- `byte_start`: inclusive byte offset in `input`
- `byte_end`: exclusive byte offset in `input`
- `start`: inclusive token start index
- `end`: inclusive token end index
- `size`: token count in the span

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
The command is then executed for that chunk with the exact emitted source span on stdin.
`ngram` parses the command into arguments itself, so shell operators such
as `&&` need an explicit shell wrapper.

If the command produces stdout, the current span is closed and only future
windows fully contained inside it are skipped. Partially overlapping windows are
still emitted.

Examples:

```bash
ngram "one two three"
printf 'one two three' | ngram -max 2 -min 1
printf 'one two three' | ngram -cmd 'sh -c '\''grep -qx "one two" && echo cut'\'''
ngram -cmd 'cmd /C "findstr /X /C:\"one two\" && echo cut"' "one two three"
```

## Build

POSIX:

```bash
cc -O2 libngram.c ngram.c -o ngram
./test.sh
```

Windows:

```bash
cl /O2 /TC libngram.c ngram.c -o ngram
```

The source code is written to stay portable across Windows, macOS, iOS,
Linux, and Android. Final compiler flags and output names depend on the toolchain.

## Library Example

```c
#include <stdio.h>
#include "ngram.h"

static int handle_chunk(const kc_ngram_chunk_t *chunk, void *context) {
    (void)context;
    printf(
        "%d:%d %.*s\n",
        chunk->start,
        chunk->end,
        (int)(chunk->byte_end - chunk->byte_start),
        chunk->input + chunk->byte_start
    );
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
