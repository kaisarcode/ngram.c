/**
 * ngram.c
 * Summary: Portable CLI wrapper for libngram (POSIX + Windows).
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "ngram.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define KC_NGRAM_TEXT_CAP 65536
#define KC_NGRAM_VERSION "0.1.0"

typedef struct {
    const char *command;
} kc_ngram_cli_context_t;

typedef struct {
    char **items;
    int count;
} kc_ngram_arg_list_t;

/**
 * Duplicates one string into heap memory.
 * @param text Source string.
 * @return Heap copy, or NULL on failure.
 */
static char *kc_ngram_strdup(const char *text) {
    size_t size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, size);
    return copy;
}

/**
 * Releases all parsed command arguments.
 * @param args Argument list.
 * @return No return value.
 */
static void kc_ngram_free_args(kc_ngram_arg_list_t *args) {
    int i;

    if (args == NULL) {
        return;
    }

    for (i = 0; i < args->count; i++) {
        free(args->items[i]);
    }

    free(args->items);
    args->items = NULL;
    args->count = 0;
}

/**
 * Appends one byte into a dynamic token buffer.
 * @param buffer Destination token buffer.
 * @param length Current used length.
 * @param capacity Current allocated capacity.
 * @param ch Byte to append.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_char(
    char **buffer,
    size_t *length,
    size_t *capacity,
    char ch
) {
    char *next_buffer;
    size_t next_capacity;

    if (buffer == NULL || length == NULL || capacity == NULL) {
        return -1;
    }

    if (*length + 1U >= *capacity) {
        next_capacity = *capacity ? (*capacity * 2U) : 32U;
        next_buffer = (char *)realloc(*buffer, next_capacity);
        if (next_buffer == NULL) {
            return -1;
        }

        *buffer = next_buffer;
        *capacity = next_capacity;
    }

    (*buffer)[*length] = ch;
    (*length)++;
    return 0;
}

/**
 * Appends one parsed argument into the command list.
 * @param args Destination argument list.
 * @param text Argument text.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_arg(kc_ngram_arg_list_t *args, const char *text) {
    char **next_items;
    char *copy;

    if (args == NULL || text == NULL || *text == '\0') {
        return -1;
    }

    next_items = (char **)realloc(
        args->items,
        (size_t)(args->count + 1) * sizeof(char *)
    );
    if (next_items == NULL) {
        return -1;
    }

    copy = kc_ngram_strdup(text);
    if (copy == NULL) {
        return -1;
    }

    args->items = next_items;
    args->items[args->count] = copy;
    args->count++;
    return 0;
}

/**
 * Finalizes one parsed token into the command list.
 * @param args Destination argument list.
 * @param token Mutable token buffer.
 * @param length Token length in bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_finish_token(
    kc_ngram_arg_list_t *args,
    char **token,
    size_t *length
) {
    int rc;

    if (args == NULL || token == NULL || length == NULL) {
        return -1;
    }

    if (*length == 0U) {
        return 0;
    }

    if (kc_ngram_push_char(token, length, &(size_t){*length + 1U}, '\0') != 0) {
        return -1;
    }

    (*length)--;
    rc = kc_ngram_push_arg(args, *token);
    *length = 0U;
    return rc;
}

/**
 * Parses one command string into executable arguments.
 * @param command Raw command string.
 * @param args Destination argument list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_parse_command(
    const char *command,
    kc_ngram_arg_list_t *args
) {
    char *token;
    size_t length;
    size_t capacity;
    int in_single;
    int in_double;
    int had_text;
    const char *cursor;

    if (command == NULL || args == NULL) {
        return -1;
    }

    args->items = NULL;
    args->count = 0;

    token = NULL;
    length = 0U;
    capacity = 0U;
    in_single = 0;
    in_double = 0;
    had_text = 0;
    cursor = command;

    while (*cursor != '\0') {
        char ch;

        ch = *cursor;

        if (!in_single && ch == '\\') {
            cursor++;
            if (*cursor == '\0') {
                free(token);
                kc_ngram_free_args(args);
                return -1;
            }

            if (kc_ngram_push_char(&token, &length, &capacity, *cursor) != 0) {
                free(token);
                kc_ngram_free_args(args);
                return -1;
            }

            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_double && ch == '\'') {
            in_single = !in_single;
            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_single && ch == '"') {
            in_double = !in_double;
            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_single && !in_double) {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                if (had_text) {
                    if (kc_ngram_push_char(&token, &length, &capacity, '\0') != 0) {
                        free(token);
                        kc_ngram_free_args(args);
                        return -1;
                    }

                    length--;
                    if (kc_ngram_push_arg(args, token) != 0) {
                        free(token);
                        kc_ngram_free_args(args);
                        return -1;
                    }

                    length = 0U;
                    had_text = 0;
                }

                cursor++;
                continue;
            }
        }

        if (kc_ngram_push_char(&token, &length, &capacity, ch) != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }

        had_text = 1;
        cursor++;
    }

    if (in_single || in_double) {
        free(token);
        kc_ngram_free_args(args);
        return -1;
    }

    if (had_text) {
        if (kc_ngram_push_char(&token, &length, &capacity, '\0') != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }

        length--;
        if (kc_ngram_push_arg(args, token) != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }
    }

    free(token);

    if (args->count == 0) {
        kc_ngram_free_args(args);
        return -1;
    }

    return 0;
}

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
 * Parses one integer CLI value.
 * @param text Input text.
 * @param out Output integer pointer.
 * @return 1 on success, or 0 on failure.
 */
static int kc_ngram_parse_int(const char *text, int *out) {
    char *end;
    long value;

    if (text == NULL || out == NULL) {
        return 0;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    if (value < -2147483647L - 1L || value > 2147483647L) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

/**
 * Prints compact command help.
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
    printf("Notes:\n");
    printf("  Each chunk is printed before --cmd is evaluated.\n");
    printf("  A span closes when the command produces stdout.\n");
}

/**
 * Prints binary version.
 * @return No return value.
 */
static void kc_ngram_version(void) {
    printf("ngram %s\n", KC_NGRAM_VERSION);
}

/**
 * Prints usage error.
 * @param message Error text.
 * @return Exit code.
 */
static int kc_ngram_fail_usage(const char *message) {
    fprintf(stderr, "Error: %s\n\n", message);
    kc_ngram_help();
    return 1;
}

#ifndef _WIN32

/**
 * Writes all bytes to one file descriptor.
 * @param fd Destination descriptor.
 * @param text Source buffer.
 * @param size Number of bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(int fd, const char *text, size_t size) {
    while (size > 0U) {
        ssize_t written;

        written = write(fd, text, size);

        if (written <= 0) {
            return -1;
        }

        text += (size_t)written;
        size -= (size_t)written;
    }

    return 0;
}

/**
 * Executes one command on POSIX systems.
 * @param command Command string.
 * @param chunk Current chunk.
 * @return 1 to close span, 0 to keep open, -1 on error.
 */
static int kc_ngram_run_command(
    const char *command,
    const kc_ngram_chunk_t *chunk
) {
    int stdin_pipe[2];
    int stdout_pipe[2];
    pid_t pid;
    int status;
    kc_ngram_arg_list_t args;
    char output_buffer[256];
    ssize_t read_count;
    int has_stdout;
    int i;

    if (command == NULL || chunk == NULL) {
        return 0;
    }

    if (kc_ngram_parse_command(command, &args) != 0) {
        return -1;
    }

    if (pipe(stdin_pipe) != 0) {
        kc_ngram_free_args(&args);
        return -1;
    }

    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (pid == 0) {
        char **argv;

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        argv = (char **)calloc((size_t)args.count + 1U, sizeof(char *));
        if (argv == NULL) {
            _exit(127);
        }

        for (i = 0; i < args.count; i++) {
            argv[i] = args.items[i];
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    if (kc_ngram_write_all(stdin_pipe[1], chunk->text, strlen(chunk->text)) != 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        waitpid(pid, NULL, 0);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (kc_ngram_write_all(stdin_pipe[1], "\n", 1U) != 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        waitpid(pid, NULL, 0);
        kc_ngram_free_args(&args);
        return -1;
    }

    close(stdin_pipe[1]);

    has_stdout = 0;
    while ((read_count = read(stdout_pipe[0], output_buffer, sizeof(output_buffer))) > 0) {
        has_stdout = 1;
    }

    close(stdout_pipe[0]);

    if (waitpid(pid, &status, 0) < 0) {
        kc_ngram_free_args(&args);
        return -1;
    }

    kc_ngram_free_args(&args);

    if (read_count < 0) {
        return -1;
    }

    if (has_stdout) {
        return 1;
    }

    return 0;
}

#else

/**
 * Writes all bytes to one Windows handle.
 * @param handle Destination handle.
 * @param text Source buffer.
 * @param size Number of bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(
    HANDLE handle,
    const char *text,
    size_t size
) {
    while (size > 0U) {
        DWORD written;
        DWORD part;

        part = size > 0x7fffffffU ? 0x7fffffffU : (DWORD)size;

        if (!WriteFile(handle, text, part, &written, NULL)) {
            return -1;
        }

        if (written == 0U) {
            return -1;
        }

        text += written;
        size -= written;
    }

    return 0;
}

/**
 * Executes one command on Windows systems.
 * @param command Command string.
 * @param chunk Current chunk.
 * @return 1 to close span, 0 to keep open, -1 on error.
 */
static int kc_ngram_run_command(
    const char *command,
    const kc_ngram_chunk_t *chunk
) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_pipe;
    HANDLE write_pipe;
    DWORD exit_code;

    if (command == NULL || chunk == NULL) {
        return 0;
    }

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return -1;
    }

    SetHandleInformation(write_pipe, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = read_pipe;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (
        !CreateProcessA(
            NULL,
            (LPSTR)command,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        )
    ) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return -1;
    }

    CloseHandle(read_pipe);

    if (kc_ngram_write_all(write_pipe, chunk->text, strlen(chunk->text)) != 0) {
        CloseHandle(write_pipe);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    if (kc_ngram_write_all(write_pipe, "\n", 1U) != 0) {
        CloseHandle(write_pipe);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    CloseHandle(write_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);

    if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return exit_code == 0U ? 1 : 0;
}

#endif

/**
 * Emits one chunk and optionally closes its span.
 * @param chunk Current chunk.
 * @param context Opaque CLI context.
 * @return 1 to close span, 0 to keep open, or -1 on error.
 */
static int kc_ngram_cli_visit(
    const kc_ngram_chunk_t *chunk,
    void *context
) {
    kc_ngram_cli_context_t *cli_context;

    if (chunk == NULL) {
        return -1;
    }

    printf("%s\n", chunk->text);

    cli_context = (kc_ngram_cli_context_t *)context;

    if (
        cli_context == NULL ||
        cli_context->command == NULL ||
        *cli_context->command == '\0'
    ) {
        return 0;
    }

    return kc_ngram_run_command(cli_context->command, chunk);
}

/**
 * Standalone entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
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
        if (
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0
        ) {
            kc_ngram_help();
            return 0;
        }

        if (
            strcmp(argv[i], "--version") == 0 ||
            strcmp(argv[i], "-v") == 0
        ) {
            kc_ngram_version();
            return 0;
        }

        if (
            strcmp(argv[i], "--max") == 0 ||
            strcmp(argv[i], "-max") == 0
        ) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --max.");
            }

            if (!kc_ngram_parse_int(argv[i + 1], &options.max_tokens)) {
                return kc_ngram_fail_usage("Invalid value for --max.");
            }

            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--min") == 0 ||
            strcmp(argv[i], "-min") == 0
        ) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --min.");
            }

            if (!kc_ngram_parse_int(argv[i + 1], &options.min_tokens)) {
                return kc_ngram_fail_usage("Invalid value for --min.");
            }

            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--sep") == 0 ||
            strcmp(argv[i], "-sep") == 0
        ) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --sep.");
            }

            options.separators = argv[i + 1];
            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--cmd") == 0 ||
            strcmp(argv[i], "-cmd") == 0
        ) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --cmd.");
            }

            context.command = argv[i + 1];
            i++;
            continue;
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

    if (
        kc_ngram_execute(
            text,
            &options,
            kc_ngram_cli_visit,
            &context
        ) < 0
    ) {
        return 1;
    }

    return 0;
}
