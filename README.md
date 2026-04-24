# ngram - Descending N-gram Traversal

ngram is a descending sliding-window n-gram traversal library. It emits token spans from the largest possible window down to the smallest. It can optionally close spans when the callback returns 1, effectively "consuming" the tokens.

---

## Quick Start

### Build
Requires a C compiler and CMake 3.14+.
```bash
cmake -B build -DNGRAM_NATIVE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
*The `ngram` binary is generated directly in the root directory.*

### Usage
```bash
ngram [options] [text]
```

**Options:**
- `--max` / `-max <n>`: Maximum window size (default: 10).
- `--min` / `-min <n>`: Minimum window size (default: 1).
- `--sep` / `-sep <s>`: Token separators (default: " \t\n\r").
- `--cmd` / `-cmd <cmd>`: Shell command to execute for each n-gram.
- `--help` / `-h`: Show help.
- `--version` / `-v`: Show version.

---

## Public API

### Types
- `kc_ngram_options_t`: Configuration for traversal (max/min tokens, separators).
- `kc_ngram_chunk_t`: Represents an emitted span (pointer to input, byte offsets, token range).
- `kc_ngram_visit_fn`: Callback type for chunk processing.

### Functions
- `kc_ngram_options_default(kc_ngram_options_t *options)`: Initialize options with defaults.
- `kc_ngram_execute(const char *input, const kc_ngram_options_t *opts, kc_ngram_visit_fn visit, void *ctx)`: Run the traversal.

---

## Thread-safety / reentrancy

Thread-safety:
ngram has no global mutable library state.

kc_ngram_execute() allocates and owns all traversal state for the duration of the call. It is reentrant and may be called concurrently from multiple threads, as long as each caller provides its own input, options, callback context, and output handling.

The library does not create worker threads and does not use internal locking.

Callbacks are executed synchronously by the calling thread. If a callback writes to shared state, the caller is responsible for synchronizing that shared state.

The CLI --cmd option may spawn child processes, but that is CLI behavior, not library-level threading.

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
