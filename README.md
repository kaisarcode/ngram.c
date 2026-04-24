# ngram - N-gram Text Processing

A minimalist C library for N-gram text analysis and processing. Specialized for efficient sequence extraction and counting from large text corpora.

---

## Quick Start

### Build
Requires a C compiler and CMake 3.14+.
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
```
*The `ngram` binary will be generated directly in the root directory.*

### Usage
```bash
./ngram -n 3 "The quick brown fox"
```

---

## Features
- **Efficient Extraction**: Fast tokenization and N-gram generation.
- **Unified Build**: Single CMake workflow for all platforms.
- **Minimalist**: Focused, autonomous implementation.
- **Native Performance**: Optimized for standard CPU architectures.

---

## Public API
```c
#include "ngram.h"

// Initialize context
kc_ngram_t *ctx = kc_ngram_open(n);

// Process input
kc_ngram_exec(ctx, "Input text data");

// Clean up
kc_ngram_close(ctx);
```

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
