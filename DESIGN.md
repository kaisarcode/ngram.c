# ngram.c Design

## Purpose

`ngram.c` enumerates contiguous token spans in descending window size and lets
a synchronous visitor close selected regions.

This supports composition with ordinary commands or caller-defined analysis
without embedding analysis policy into the library.

## Tokenization

Tokens are maximal non-empty byte ranges not containing any configured
separator byte. Defaults are spaces, tabs, carriage returns, and newlines.

The token table stores only start and end byte offsets. Input is neither copied
nor normalized. A chunk references the original input and spans from the first
token start to the final token end.

This is byte tokenization, not Unicode or linguistic segmentation.

## Traversal

Default options use a maximum of 10 tokens and minimum of 1. A maximum of zero
means the complete token count. A positive maximum below the minimum is invalid.

For each window size from effective maximum down to minimum, traversal visits
every left-to-right start position. Each chunk reports original byte offsets,
inclusive token indexes, and token count.

The return value is the number of emitted chunks, zero for no tokens, a generic
error for invalid input or visitor failure, or `KC_NGRAM_ESTOP` for cooperative
stop.

## Closed Spans

When the visitor returns `1`, the current token interval is inserted into a
sorted closed-span set. Later candidates fully contained by a closed interval
are skipped.

Insertion removes redundant contained spans and does not close partially
overlapping or unrelated windows.

Closure lets an external classifier accept a larger chunk and avoid examining
its smaller contained n-grams. The library does not define why a span closes.

## Callback Contract

The callback runs synchronously and receives borrowed pointers into the current
input. It may inspect or copy bytes during the call.

Return values are:

- `0` to continue without closing;
- `1` to close the current span;
- `-1` to abort as an error;
- `KC_NGRAM_ESTOP` to abort as a cooperative stop.

Traversal state is allocated per execution, making independent execute calls
reentrant.

## Context Role

The traversal function does not require a context. Contexts provide owned
default options, an optional borrowed mutable options pointer, stop state, and
signal callbacks for CLI and embedding lifecycle needs.

`kc_ngram_configure()` borrows the supplied options. Passing NULL restores the
context-owned defaults. The context does not copy or release caller options.

## CLI

The CLI accepts one positional input string or reads all stdin. It prints every
chunk as the exact original byte span followed by newline.

`--max`, `--min`, and `--sep` configure traversal. Environment overrides exist
for maximum and minimum values. CLI values take precedence.

The CLI is a one-input traversal process, not an EOT-resident filter.

## Command Execution

With `--cmd`, each printed chunk is also sent with a newline to one newly
started child process. The command string is parsed into argv with simple quote
and backslash handling.

POSIX uses `execvp`; Windows builds a quoted command line for `CreateProcessA`.
No shell evaluates the command.

The child stdout is drained but not forwarded. Any stdout byte returns visitor
decision `1`; no stdout returns `0`. Child exit status does not define closure.
Stderr remains attached to the parent diagnostic stream.

Execution is sequential and one process is started per visited chunk.

## Resource Model

Token offset storage grows linearly with token count. Closed-span storage grows
with non-redundant visitor closures. The number of candidate windows can be
quadratic in token count.

The CLI buffers complete stdin and dynamically parses command arguments. It
does not impose hidden truncation or launch commands concurrently.

There is no persistent state, index, model, cache, network dependency, or
background worker.

## Portability

The library is portable C11. Platform-specific code is confined to CLI process
creation, pipes, handle quoting, and signals.

Traversal order, offsets, callback decisions, and closure behavior must remain
identical across platforms.

## Non-Goals

`ngram.c` does not provide linguistic tokenization, Unicode segmentation,
stemming, language detection, semantic scoring, embeddings, search indexes,
parallel traversal, distributed execution, shell scripting, command queues,
remote APIs, persistent storage, telemetry, or a control plane.

These responsibilities belong to visitor code or separate composable tools.

## Change Criteria

A change should specify exact bytes, separators, token offsets, traversal order,
visitor results, and closed spans. It must preserve borrowed-input ownership,
synchronous callbacks, direct command execution without a shell, visible
resource costs, and platform-equivalent results.

Changes justified mainly by enterprise scale, generic NLP completeness, or
hypothetical extensibility should be rejected.

## Core Invariants

The project is defined by byte-set tokenization, descending contiguous windows,
synchronous borrowed chunks, visitor-controlled contained-span closure,
sequential optional command composition, no embedded analysis policy, and
portable inspectable C11 code.
