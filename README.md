# ngram - Descending N-gram Traversal

ngram emits token spans from the largest configured window down to the
smallest configured window. A callback may close a span, which suppresses later
windows fully contained inside that span.

## File Layout

```
ngram.c/
├── src/
│   ├── ngram.c        CLI entry point (main)
│   ├── libngram.c     Core library implementation
│   └── ngram.h        Public API header
├── bin/               Compiled artifacts (committed, Git LFS)
│   ├── x86_64/{linux,windows}
│   ├── i686/{linux,windows}
│   ├── aarch64/{linux,android}
│   ├── armv7/{linux,android}
│   ├── armv7hf/linux
│   ├── riscv64/linux
│   ├── powerpc64le/linux
│   ├── mips/linux  mipsel/linux  mips64el/linux
│   ├── s390x/linux
│   └── loongarch64/linux
├── CMakeLists.txt
├── Makefile
├── test.sh
└── README.md
```

## Build

```bash
make all              # all 16 targets
make x86_64/linux
make x86_64/windows
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
make clean
```

Each target produces under `bin/{arch}/{platform}/`:
- `libngram.a` — static library
- `libngram.so` / `libngram.dll` / `libngram.dll.a` — shared library and import lib
- `ngram` / `ngram.exe` — CLI executable

## CLI

```bash
./bin/x86_64/linux/ngram [options] [text]
```

Options:

- `--max`, `-max <n>`: Maximum tokens per span.
- `--min`, `-min <n>`: Minimum tokens per span.
- `--sep`, `-sep <s>`: Separator byte set.
- `--cmd`, `-cmd <cmd>`: Execute a command for each emitted span.
- `--help`, `-h`: Show help.
- `--version`, `-v`: Show version.

When `text` is omitted, the CLI reads standard input. Each span is printed
before `--cmd` is evaluated. If the command writes any stdout, the current span
is closed.

## Public API

`kc_ngram_options_t` configures traversal:

- `max_tokens`: Maximum window size.
- `min_tokens`: Minimum window size.
- `separators`: Separator byte set.

`kc_ngram_chunk_t` describes one emitted span. Its `input` pointer is the
caller-owned input buffer passed to `kc_ngram_execute()`. The pointer remains
valid only while the caller keeps that input buffer valid.

```c
int kc_ngram_options_default(kc_ngram_options_t *options);

int kc_ngram_execute(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *context
);
```

`kc_ngram_execute()` owns temporary traversal storage for the duration of the
call and releases it before returning. It does not take ownership of `input`,
`options`, `visit`, or `context`.

The callback returns:

- `1` to close the emitted span.
- `0` to keep traversal open.
- `-1` to abort traversal.

## Threading

The library has no global mutable state. `kc_ngram_execute()` is reentrant and
may be called concurrently from multiple caller-created threads when each call
uses independent input, options, callback context, and output handling.

Callbacks run synchronously in the calling thread because callback results alter
later traversal by closing spans. The CLI may spawn child processes for `--cmd`;
that behavior is outside the library API.

## Validation

```bash
make x86_64/linux
make test
kc kcs .
```

---

**Author:** KaisarCode

**Email:** kaisarcode@gmail.com

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
