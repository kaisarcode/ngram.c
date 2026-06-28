# ngram.c - Sliding-window n-gram traversal

`ngram.c` is a minimalist C library and CLI for descending sliding-window n-gram traversal of text. It enables semantic analysis by emitting token spans and executing commands for each chunk, designed as a composable native primitive for the KaisarCode ecosystem.

---

## CLI

Traverse text and emit n-gram chunks based on token window constraints.

### Examples

Basic n-gram extraction (default 1-5 tokens):

```bash
./bin/x86_64/linux/ngram "The quick brown fox"
```

Extraction with custom window size and separators:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --max 3 --min 2 --sep " ,"
```

Execute a command for each chunk and close span on stdout:

```bash
./bin/x86_64/linux/ngram "The quick brown fox" --cmd "grep -q fox"
```

Standard input processing:

```bash
echo "The quick brown fox" | ./bin/x86_64/linux/ngram
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `--max, -max <n>` | Maximum tokens per block |
| `--min, -min <n>` | Minimum tokens per block |
| `--sep, -sep <s>` | Custom separator characters |
| `--cmd, -cmd <cmd>` | Execute command for each chunk |
| `--help, -h` | Show help and usage |
| `--version, -v` | Show version |

---

### Output

Chunks are printed to stdout, one per line:

```
The quick brown fox
The quick brown
quick brown fox
The quick
quick brown
brown fox
```

---

## Public API

```c
#include "ngram.h"

int my_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    printf("%.*s\n", (int)(chunk->byte_end - chunk->byte_start), chunk->input + chunk->byte_start);
    return 0; // 1 to close span, -1 to abort
}

kc_ngram_options_t options;
kc_ngram_options_default(&options);
options.max_tokens = 3;

kc_ngram_execute("The quick brown fox", &options, my_visitor, NULL);
```

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
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
```

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
