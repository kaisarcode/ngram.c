/**
 * ngram.c
 * Summary: Thin CLI wrapper for libngram.
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "ngram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define KC_NGRAM_TEXT_CAP 65536
#define KC_NGRAM_VERSION "0.1.0"

typedef struct {
    const char *command;
} kc_ngram_cli_context_t;

/**
 * Reads text from standard input into the provided buffer.
 * @param buffer Destination buffer.
 * @param size Buffer size in bytes.
 * @return Pointer to buffer on success, or NULL on empty input.
 */
static const char *kc_ngram_read_stdin(char *buffer, size_t size) {
    size_t n;

    if (buffer == NULL || size < 2U) {
        return NULL;
    }

    n = fread(buffer, 1, size - 1U, stdin);
    if (n == 0U) {
        return NULL;
    }

    buffer[n] = '\0';
    return buffer;
}

/**
 * Prints the compact command help.
 * @return No return value.
 */
static void kc_ngram_help(void) {
    printf("Usage:\n");
    printf("  ngram [options] [text]\n\n");
    printf("Options:\n");
    printf("  --max, -max <n>     Maximum tokens per block\n");
    printf("  --min, -min <n>     Minimum tokens per block\n");
    printf("  --sep, -sep <s>     Custom separator characters\n");
    printf("  --cmd, -cmd <cmd>   Execute command for each chunk\n");
    printf("  --help, -h          Show help\n");
    printf("  --version, -v       Show version\n\n");
    printf("Examples:\n");
    printf("  ngram \"one two three\"\n");
    printf("  printf 'one two three' | ngram -max 2 -min 1\n");
}

/**
 * Prints the binary version.
 * @return No return value.
 */
static void kc_ngram_version(void) {
    printf("ngram %s\n", KC_NGRAM_VERSION);
}

/**
 * Prints one CLI error followed by help.
 * @param message Error text.
 * @return Process exit status.
 */
static int kc_ngram_fail_usage(const char *message) {
    fprintf(stderr, "Error: %s\n\n", message);
    kc_ngram_help();
    return 1;
}

/**
 * Writes one integer value into the process environment.
 * @param name Environment variable name.
 * @param value Integer value to store.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_set_env_int(const char *name, int value) {
    char buffer[32];

    if (snprintf(buffer, sizeof(buffer), "%d", value) < 0) {
        return -1;
    }

    return setenv(name, buffer, 1);
}

/**
 * Executes the configured shell command for one chunk.
 * @param command Shell command to run.
 * @param chunk Current chunk passed through stdin.
 * @return 1 when the command wrote any stdout, 0 otherwise, or -1 on failure.
 */
static int kc_ngram_run_command(const char *command, const kc_ngram_chunk_t *chunk) {
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    char output_buffer[2];
    ssize_t written;
    ssize_t read_count;
    int status;

    if (command == NULL || chunk == NULL) {
        return 0;
    }

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (kc_ngram_set_env_int("KC_NGR_START", chunk->start) != 0) {
            _exit(1);
        }
        if (kc_ngram_set_env_int("KC_NGR_END", chunk->end) != 0) {
            _exit(1);
        }
        if (kc_ngram_set_env_int("KC_NGR_SIZE", chunk->size) != 0) {
            _exit(1);
        }
        execl("/bin/sh", "sh", "-lc", command, (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    written = write(in_pipe[1], chunk->text, strlen(chunk->text));
    if (written < 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    written = write(in_pipe[1], "\n", 1U);
    close(in_pipe[1]);
    if (written < 0) {
        close(out_pipe[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    read_count = read(out_pipe[0], output_buffer, sizeof(output_buffer));
    close(out_pipe[0]);
    waitpid(pid, &status, 0);
    if (read_count < 0) {
        return -1;
    }

    return read_count > 0 ? 1 : 0;
}

/**
 * Emits one chunk and optionally closes its span using the shell command rule.
 * @param chunk Current emitted chunk.
 * @param context Opaque CLI context.
 * @return 1 to close the span, 0 to keep it open, or -1 on error.
 */
static int kc_ngram_cli_visit(const kc_ngram_chunk_t *chunk, void *context) {
    kc_ngram_cli_context_t *cli_context;

    if (chunk == NULL) {
        return -1;
    }

    printf("%s\n", chunk->text);
    cli_context = (kc_ngram_cli_context_t *)context;
    if (cli_context == NULL || cli_context->command == NULL || *cli_context->command == '\0') {
        return 0;
    }

    return kc_ngram_run_command(cli_context->command, chunk);
}

/**
 * Standalone entry point.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    char buffer[KC_NGRAM_TEXT_CAP];
    kc_ngram_options_t options;
    kc_ngram_cli_context_t context;
    const char *text;
    int i;

    if (kc_ngram_options_default(&options) != 0) {
        return 1;
    }

    context.command = NULL;
    text = NULL;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            kc_ngram_help();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            kc_ngram_version();
            return 0;
        }
        if ((strcmp(argv[i], "--max") == 0 || strcmp(argv[i], "-max") == 0)
            && i + 1 < argc) {
            options.max_tokens = atoi(argv[++i]);
            continue;
        }
        if ((strcmp(argv[i], "--min") == 0 || strcmp(argv[i], "-min") == 0)
            && i + 1 < argc) {
            options.min_tokens = atoi(argv[++i]);
            continue;
        }
        if ((strcmp(argv[i], "--sep") == 0 || strcmp(argv[i], "-sep") == 0)
            && i + 1 < argc) {
            options.separators = argv[++i];
            continue;
        }
        if ((strcmp(argv[i], "--cmd") == 0 || strcmp(argv[i], "-cmd") == 0)
            && i + 1 < argc) {
            context.command = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--max") == 0 || strcmp(argv[i], "-max") == 0) {
            return kc_ngram_fail_usage("Missing value for --max.");
        }
        if (strcmp(argv[i], "--min") == 0 || strcmp(argv[i], "-min") == 0) {
            return kc_ngram_fail_usage("Missing value for --min.");
        }
        if (strcmp(argv[i], "--sep") == 0 || strcmp(argv[i], "-sep") == 0) {
            return kc_ngram_fail_usage("Missing value for --sep.");
        }
        if (strcmp(argv[i], "--cmd") == 0 || strcmp(argv[i], "-cmd") == 0) {
            return kc_ngram_fail_usage("Missing value for --cmd.");
        }
        if (argv[i][0] == '-') {
            return kc_ngram_fail_usage("Unknown argument.");
        }
        if (text != NULL) {
            return kc_ngram_fail_usage("Too many positional arguments.");
        }
        text = argv[i];
    }

    if (text == NULL) {
        text = kc_ngram_read_stdin(buffer, sizeof(buffer));
    }

    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (kc_ngram_execute(text, &options, kc_ngram_cli_visit, &context) < 0) {
        return 1;
    }

    return 0;
}
