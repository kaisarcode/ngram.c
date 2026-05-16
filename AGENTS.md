# ngram.c — N-gram Traversal

## Overview
Descending sliding-window n-gram traversal library and CLI for text. Tokenizes input by a configurable separator byte set, then emits all token spans from `max_tokens` down to `min_tokens` via a callback. Supports span closing (when a callback or external command signals that a span is "complete", sub-spans nested inside it are pruned from traversal). Optional per-chunk command execution via `--cmd`: the chunk text is piped to a child process; if the child produces stdout, the span is closed. Designed as a composable native primitive for semantic chunking, text analysis, and pipeline processing.

## Architecture
Three-file split: `ngram.h` exposes `kc_ngram_chunk_t`, `kc_ngram_options_t`, `kc_ngram_visit_fn`, and two functions. `libngram.c` implements the tokenizer, the descending-window traversal loop, and a closed-span tracking system with sorted insertion and binary-search containment checks. `ngram.c` is the CLI — reads text from argv or stdin, prints each chunk to stdout, and conditionally executes `--cmd` via platform-specific process creation (`fork`/`execvp` on POSIX, `CreateProcess` on Windows). The library is reentrant (no global state).

### Sliding-Window Traversal Algorithm
The traversal emits n-gram chunks in descending window order. First, input text is split into tokens by scanning for separator bytes (`strchr`-based `kc_ngram_is_separator`). Consecutive separators are skipped; empty tokens are not emitted. The outer loop iterates `window_size` from `max_tokens` (or `token_count`, whichever is smaller) down to `min_tokens`. The inner loop iterates `start` from 0 to `token_count - window_size`. For each span `[start, start + window_size - 1]`, if it is fully contained within a previously closed span (checked via binary search over the sorted closed-spans array), it is skipped. Otherwise, the span is emitted as a `kc_ngram_chunk_t` (with byte offsets, token indices, and count). If the visit callback returns 1 (close), the span is inserted into the closed-spans array (sorted by start index; any existing spans fully contained in the new span are removed). If the callback returns -1, traversal is aborted.

### Optional Command Execution
When `--cmd` is provided, each chunk is printed to stdout, then the command string is parsed into `argv`-style arguments (supporting single/double quotes and backslash escaping). On POSIX, a `fork()` creates a child process; stdin/stdout pipes connect the parent to the child. The chunk text plus a trailing newline is written to the child's stdin. The child's stdout is read (up to 256-byte buffer); if any bytes are read, the span is closed (return 1). On Windows, `CreateProcessA` is used with analogous pipe handling via `CreatePipe` and `ReadFile`. Empty output keeps the span open; pipe/exec errors abort traversal.

## Directory Layout
| Path | Contents |
|------|----------|
| `src/ngram.h` | Public API — chunk, options, callback typedefs; function declarations |
| `src/libngram.c` | Library implementation — tokenizer, traversal loop, closed-span tracking |
| `src/ngram.c` | CLI entry point — argv parsing, stdin read, chunk output, command execution |
| `Makefile` | Cross-compilation builder (17 targets) via CMake + Ninja |
| `CMakeLists.txt` | CMake project definition |
| `test.sh` | Shell test suite — traversal semantics, span closing, stdin |
| `README.md` | Project documentation and usage examples |
| `LICENSE` | GPL v3.0 |
| `.kcsignore` | KCS exclusion list |

## Data Model
### Internal Structures
| Symbol | Type | Role |
|--------|------|------|
| `kc_ngram_chunk_t` | `struct` | Emitted chunk: input pointer, byte start/end, token start/end, size |
| `kc_ngram_options_t` | `struct` | Traversal config: max_tokens, min_tokens, separators string |
| `kc_ngram_visit_fn` | `typedef` | Callback: `int (*)(const kc_ngram_chunk_t *, void *)` |
| `kc_ngram_token_t` | `struct` | Internal: byte_start, byte_end |
| `kc_ngram_token_list_t` | `struct` | Internal: items array, count, cap |
| `kc_ngram_span_t` | `struct` | Internal: start token index, end token index |
| `kc_ngram_cli_context_t` | `struct` | CLI: command string pointer |
| `kc_ngram_arg_list_t` | `struct` | CLI: parsed argv items array, count |
| `kc_ngram_string_t` | `struct` | CLI: data, length, capacity |

### Hard Limits
| Limit | Value | Symbol |
|-------|-------|--------|
| Default max_tokens | 10 | `options->max_tokens` |
| Default min_tokens | 1 | `options->min_tokens` |
| Default separators | `" \t\r\n"` | `options->separators` |
| Initial token capacity | 16 | `next_cap` in `kc_ngram_reserve_token_slot` |
| Token capacity growth | doubles | `cap * 2` |
| Initial closed-span capacity | 16 | `next_cap` in `kc_ngram_reserve_span_slot` |
| Closed-span capacity growth | doubles | `cap * 2` |
| Stdin read chunk | 4096 bytes | `chunk` in `kc_ngram_read_stdin` |
| Stdin initial capacity | 4096 bytes | `kc_ngram_string_reserve` |
| CLI output buffer | 256 bytes | `output_buffer` in `kc_ngram_run_command` |
| Initial token buffer | 32 bytes | `kc_ngram_push_char` |
| Token buffer growth | doubles | `capacity * 2U` |

## Public API
| Function | Returns | Description |
|----------|---------|-------------|
| `kc_ngram_options_default(kc_ngram_options_t *options)` | `int` | Fill options with defaults (max=10, min=1, separators=whitespace) |
| `kc_ngram_execute(input, options, visit, context)` | `int` | Execute descending sliding-window traversal; returns emitted count or -1 |

## CLI
| Argument | Description |
|----------|-------------|
| `<text>` | Input text (positional; stdin used if absent) |
| `--max`, `-max <n>` | Maximum tokens per block (default: 10) |
| `--min`, `-min <n>` | Minimum tokens per block (default: 1) |
| `--sep`, `-sep <s>` | Custom separator characters (default: whitespace) |
| `--cmd`, `-cmd <cmd>` | Execute command for each chunk; close span if command produces stdout |
| `-h`, `--help` | Show help and exit 0 |
| `-v`, `--version` | Show version and exit 0 |

Output: one chunk text per line to stdout.

### Exit Codes
| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Error (invalid args, stdin failure, traversal abort, command execution failure) |

## Build
| Target | Description |
|--------|-------------|
| `make` (default: `native`) | Build for host arch/platform |
| `make all` | Build full 17-target cross-compilation matrix |
| `make <arch>/<platform>` | Build specific target |
| `make test` | Run `sh test.sh` |
| `make clean` | Remove `.build/` |

## Error Handling
| Condition | Stderr | Exit Code |
|-----------|--------|-----------|
| Missing value for -max/-min/-sep/-cmd | `"Error: Missing value for <flag>."` + usage | 1 |
| Invalid integer for -max/-min | `"Error: Invalid value for <flag>."` + usage | 1 |
| Unknown argument | `"Error: Unknown argument."` + usage | 1 |
| Too many positional arguments | `"Error: Too many positional arguments."` + usage | 1 |
| Stdin read failure | (none, returns 1) | 1 |
| Traversal aborted by callback | (none, returns 1) | 1 |
| Command parsing/execution failure | (none, returns 1) | 1 |
| Invalid options (min < 1, max < min) | (none, returns -1 from API) | N/A |
| NULL input or visit to execute | (none, returns -1 from API) | N/A |

## Constraints
- Reads entire stdin into memory before processing — no streaming mode.
- Token separators are single-byte characters; multi-byte separators not supported.
- Closed-span containment is strict: a span is pruned only when its token range is fully inside a closed span (start >= closed.start && end <= closed.end).
- Command execution is synchronous — traversal waits for the child process to finish before continuing.
- POSIX command execution uses `fork()`/`execvp()`; Windows uses `CreateProcessA()`.
- The command string parser supports single quotes, double quotes, and backslash escaping.
- Library is reentrant (no global/static state); CLI has a single fixed stdout output path before command execution.
- Max chunk output is the entire input; no internal limit on emitted chunk count beyond memory.
