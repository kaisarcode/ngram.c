/**
 * ngram.c
 * Summary: Portable CLI wrapper for libngram.
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "ngram.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define KC_NGRAM_TEXT_CAP 65536
#define KC_NGRAM_VERSION "0.1.0"

typedef struct {
    char **items;
    int count;
} kc_ngram_arg_list_t;

typedef struct {
    const char *command;
    kc_ngram_arg_list_t args;
} kc_ngram_cli_context_t;

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
 * Appends one argument into the parsed command list.
 * @param args Destination argument list.
 * @param text Argument text.
 * @return 0 on success or -1 on failure.
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
 * Finalizes one parsed token into the argument list.
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
 * Parses one command string into executable arguments without shell expansion.
 * Supports whitespace splitting, single quotes, double quotes,
 * and backslash escapes.
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
    printf("  --cmd, -cmd <cmd>   Execute program for each chunk\n");
    printf("  --help, -h          Show help\n");
    printf("  --version, -v       Show version\n\n");
    printf("Notes:\n");
    printf("  Each chunk is printed before --cmd is evaluated.\n");
    printf("  The command is parsed by ngram itself, not by a shell.\n");
    printf("  A span closes when the command produces stdout.\n\n");
    printf("Examples:\n");
    printf("  ngram \"one two three\"\n");
    printf("  printf 'one two three' | ngram -max 2 -min 1\n");
    printf("  printf 'one two three' | ngram -cmd 'grep -qx \"one two\"'\n");
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

#ifndef _WIN32

/**
 * Writes the full buffer into one file descriptor.
 * @param fd Destination file descriptor.
 * @param buffer Source buffer.
 * @param size Number of bytes to write.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(int fd, const char *buffer, size_t size) {
    while (size > 0U) {
        ssize_t written;

        written = write(fd, buffer, size);
        if (written < 0) {
            return -1;
        }

        buffer += (size_t)written;
        size -= (size_t)written;
    }

    return 0;
}

/**
 * Writes one integer value into the child process environment.
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
 * Executes the configured program for one chunk on POSIX systems.
 * @param args Parsed executable arguments.
 * @param chunk Current chunk passed through stdin.
 * @return 1 when the command wrote any stdout, 0 otherwise, or -1 on failure.
 */
static int kc_ngram_run_command_posix(
    const kc_ngram_arg_list_t *args,
    const kc_ngram_chunk_t *chunk
) {
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid;
    char output_buffer[2];
    ssize_t read_count;
    int status;
    char **argv;
    int i;

    if (args == NULL || chunk == NULL || args->count < 1) {
        return -1;
    }

    if (pipe(in_pipe) != 0) {
        return -1;
    }

    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    argv = (char **)calloc((size_t)args->count + 1U, sizeof(char *));
    if (argv == NULL) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    for (i = 0; i < args->count; i++) {
        argv[i] = args->items[i];
    }

    pid = fork();
    if (pid < 0) {
        free(argv);
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

        execvp(argv[0], argv);
        _exit(127);
    }

    free(argv);

    close(in_pipe[0]);
    close(out_pipe[1]);

    if (kc_ngram_write_all(in_pipe[1], chunk->text, strlen(chunk->text)) != 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    if (kc_ngram_write_all(in_pipe[1], "\n", 1U) != 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        waitpid(pid, NULL, 0);
        return -1;
    }

    close(in_pipe[1]);

    read_count = read(out_pipe[0], output_buffer, sizeof(output_buffer));
    close(out_pipe[0]);

    waitpid(pid, &status, 0);

    if (read_count < 0) {
        return -1;
    }

    return read_count > 0 ? 1 : 0;
}

#else

/**
 * Writes the full buffer into one Windows handle.
 * @param handle Destination handle.
 * @param buffer Source buffer.
 * @param size Number of bytes to write.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(HANDLE handle, const char *buffer, size_t size) {
    while (size > 0U) {
        DWORD written;
        DWORD chunk_size;

        chunk_size = size > (size_t)0x7fffffffU ? 0x7fffffffU : (DWORD)size;
        if (!WriteFile(handle, buffer, chunk_size, &written, NULL)) {
            return -1;
        }

        if (written == 0U) {
            return -1;
        }

        buffer += written;
        size -= written;
    }

    return 0;
}

/**
 * Appends one string into the Windows environment block buffer.
 * @param buffer Destination environment block.
 * @param offset Current used size.
 * @param capacity Current allocated size.
 * @param text Text to append including trailing NUL.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_env_append(
    char **buffer,
    size_t *offset,
    size_t *capacity,
    const char *text
) {
    size_t length;
    char *next_buffer;
    size_t needed;

    if (buffer == NULL || offset == NULL || capacity == NULL || text == NULL) {
        return -1;
    }

    length = strlen(text) + 1U;
    needed = *offset + length + 1U;

    if (needed > *capacity) {
        size_t next_capacity;

        next_capacity = *capacity ? *capacity : 256U;
        while (needed > next_capacity) {
            next_capacity *= 2U;
        }

        next_buffer = (char *)realloc(*buffer, next_capacity);
        if (next_buffer == NULL) {
            return -1;
        }

        *buffer = next_buffer;
        *capacity = next_capacity;
    }

    memcpy(*buffer + *offset, text, length);
    *offset += length;
    (*buffer)[*offset] = '\0';
    return 0;
}

/**
 * Builds one Windows environment block with chunk metadata variables.
 * @param chunk Current chunk metadata.
 * @return Heap environment block, or NULL on failure.
 */
static char *kc_ngram_build_env_block(const kc_ngram_chunk_t *chunk) {
    LPCH env_strings;
    LPCH cursor;
    char *block;
    size_t offset;
    size_t capacity;
    char entry[64];

    if (chunk == NULL) {
        return NULL;
    }

    block = NULL;
    offset = 0U;
    capacity = 0U;

    env_strings = GetEnvironmentStringsA();
    if (env_strings == NULL) {
        return NULL;
    }

    cursor = env_strings;
    while (*cursor != '\0') {
        if (kc_ngram_env_append(&block, &offset, &capacity, cursor) != 0) {
            FreeEnvironmentStringsA(env_strings);
            free(block);
            return NULL;
        }

        cursor += strlen(cursor) + 1U;
    }

    if (snprintf(entry, sizeof(entry), "KC_NGR_START=%d", chunk->start) < 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    if (kc_ngram_env_append(&block, &offset, &capacity, entry) != 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    if (snprintf(entry, sizeof(entry), "KC_NGR_END=%d", chunk->end) < 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    if (kc_ngram_env_append(&block, &offset, &capacity, entry) != 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    if (snprintf(entry, sizeof(entry), "KC_NGR_SIZE=%d", chunk->size) < 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    if (kc_ngram_env_append(&block, &offset, &capacity, entry) != 0) {
        FreeEnvironmentStringsA(env_strings);
        free(block);
        return NULL;
    }

    FreeEnvironmentStringsA(env_strings);

    if (block == NULL) {
        return NULL;
    }

    block[offset] = '\0';
    return block;
}

/**
 * Returns whether one command argument needs quoting on Windows.
 * @param text Argument text.
 * @return 1 when quoting is required, or 0 otherwise.
 */
static int kc_ngram_win_needs_quotes(const char *text) {
    const char *cursor;

    if (text == NULL || *text == '\0') {
        return 1;
    }

    for (cursor = text; *cursor != '\0'; cursor++) {
        if (
            *cursor == ' ' ||
            *cursor == '\t' ||
            *cursor == '\n' ||
            *cursor == '\v' ||
            *cursor == '"'
        ) {
            return 1;
        }
    }

    return 0;
}

/**
 * Appends one byte into the Windows command-line buffer.
 * @param buffer Destination buffer.
 * @param length Current used size.
 * @param capacity Current allocated size.
 * @param ch Byte to append.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_cmd_push_char(
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
        next_capacity = *capacity ? (*capacity * 2U) : 128U;
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
 * Appends one command argument with Windows CreateProcess quoting.
 * @param buffer Destination command-line buffer.
 * @param length Current used size.
 * @param capacity Current allocated size.
 * @param arg Argument text.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_win_append_arg(
    char **buffer,
    size_t *length,
    size_t *capacity,
    const char *arg
) {
    const char *cursor;
    int quoted;
    int backslashes;

    quoted = kc_ngram_win_needs_quotes(arg);

    if (!quoted) {
        for (cursor = arg; *cursor != '\0'; cursor++) {
            if (kc_ngram_cmd_push_char(buffer, length, capacity, *cursor) != 0) {
                return -1;
            }
        }

        return 0;
    }

    if (kc_ngram_cmd_push_char(buffer, length, capacity, '"') != 0) {
        return -1;
    }

    cursor = arg;
    backslashes = 0;

    while (*cursor != '\0') {
        if (*cursor == '\\') {
            backslashes++;
            cursor++;
            continue;
        }

        if (*cursor == '"') {
            while (backslashes-- >= 0) {
                if (kc_ngram_cmd_push_char(buffer, length, capacity, '\\') != 0) {
                    return -1;
                }
            }

            if (kc_ngram_cmd_push_char(buffer, length, capacity, '"') != 0) {
                return -1;
            }

            backslashes = 0;
            cursor++;
            continue;
        }

        while (backslashes-- > 0) {
            if (kc_ngram_cmd_push_char(buffer, length, capacity, '\\') != 0) {
                return -1;
            }
        }

        backslashes = 0;

        if (kc_ngram_cmd_push_char(buffer, length, capacity, *cursor) != 0) {
            return -1;
        }

        cursor++;
    }

    while (backslashes-- > 0) {
        if (kc_ngram_cmd_push_char(buffer, length, capacity, '\\') != 0) {
            return -1;
        }

        if (kc_ngram_cmd_push_char(buffer, length, capacity, '\\') != 0) {
            return -1;
        }
    }

    if (kc_ngram_cmd_push_char(buffer, length, capacity, '"') != 0) {
        return -1;
    }

    return 0;
}

/**
 * Builds one CreateProcess command line from parsed arguments.
 * @param args Parsed executable arguments.
 * @return Heap command line string, or NULL on failure.
 */
static char *kc_ngram_build_command_line(const kc_ngram_arg_list_t *args) {
    char *buffer;
    size_t length;
    size_t capacity;
    int i;

    if (args == NULL || args->count < 1) {
        return NULL;
    }

    buffer = NULL;
    length = 0U;
    capacity = 0U;

    for (i = 0; i < args->count; i++) {
        if (i > 0) {
            if (kc_ngram_cmd_push_char(&buffer, &length, &capacity, ' ') != 0) {
                free(buffer);
                return NULL;
            }
        }

        if (
            kc_ngram_win_append_arg(
                &buffer,
                &length,
                &capacity,
                args->items[i]
            ) != 0
        ) {
            free(buffer);
            return NULL;
        }
    }

    if (kc_ngram_cmd_push_char(&buffer, &length, &capacity, '\0') != 0) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

/**
 * Executes the configured program for one chunk on Windows systems.
 * @param args Parsed executable arguments.
 * @param chunk Current chunk passed through stdin.
 * @return 1 when the command wrote any stdout, 0 otherwise, or -1 on failure.
 */
static int kc_ngram_run_command_windows(
    const kc_ngram_arg_list_t *args,
    const kc_ngram_chunk_t *chunk
) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE child_stdin_read;
    HANDLE child_stdin_write;
    HANDLE child_stdout_read;
    HANDLE child_stdout_write;
    HANDLE stderr_handle;
    char *command_line;
    char *env_block;
    char output_buffer[2];
    DWORD read_count;
    int result;

    if (args == NULL || chunk == NULL || args->count < 1) {
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    child_stdin_read = NULL;
    child_stdin_write = NULL;
    child_stdout_read = NULL;
    child_stdout_write = NULL;

    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
        return -1;
    }

    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return -1;
    }

    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    command_line = kc_ngram_build_command_line(args);
    if (command_line == NULL) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return -1;
    }

    env_block = kc_ngram_build_env_block(chunk);
    if (env_block == NULL) {
        free(command_line);
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return -1;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;

    stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdError = stderr_handle != INVALID_HANDLE_VALUE ? stderr_handle : child_stdout_write;

    if (
        !CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            TRUE,
            0,
            env_block,
            NULL,
            &si,
            &pi
        )
    ) {
        free(command_line);
        free(env_block);
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return -1;
    }

    free(command_line);
    free(env_block);

    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);

    if (
        kc_ngram_write_all(
            child_stdin_write,
            chunk->text,
            strlen(chunk->text)
        ) != 0
    ) {
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    if (kc_ngram_write_all(child_stdin_write, "\n", 1U) != 0) {
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    CloseHandle(child_stdin_write);

    if (!ReadFile(child_stdout_read, output_buffer, sizeof(output_buffer), &read_count, NULL)) {
        CloseHandle(child_stdout_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return -1;
    }

    CloseHandle(child_stdout_read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result = read_count > 0U ? 1 : 0;
    return result;
}

#endif

/**
 * Executes the configured external program for one chunk.
 * @param args Parsed executable arguments.
 * @param chunk Current chunk passed through stdin.
 * @return 1 when the command wrote any stdout, 0 otherwise, or -1 on failure.
 */
static int kc_ngram_run_command(
    const kc_ngram_arg_list_t *args,
    const kc_ngram_chunk_t *chunk
) {
#ifdef _WIN32
    return kc_ngram_run_command_windows(args, chunk);
#else
    return kc_ngram_run_command_posix(args, chunk);
#endif
}

/**
 * Emits one chunk and optionally closes its span using the command rule.
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
    if (
        cli_context == NULL ||
        cli_context->command == NULL ||
        *cli_context->command == '\0'
    ) {
        return 0;
    }

    return kc_ngram_run_command(&cli_context->args, chunk);
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
    int rc;

    if (kc_ngram_options_default(&options) != 0) {
        return 1;
    }

    context.command = NULL;
    context.args.items = NULL;
    context.args.count = 0;
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

        if (strcmp(argv[i], "--max") == 0 || strcmp(argv[i], "-max") == 0) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --max.");
            }

            if (!kc_ngram_parse_int(argv[i + 1], &options.max_tokens)) {
                return kc_ngram_fail_usage("Invalid value for --max.");
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--min") == 0 || strcmp(argv[i], "-min") == 0) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --min.");
            }

            if (!kc_ngram_parse_int(argv[i + 1], &options.min_tokens)) {
                return kc_ngram_fail_usage("Invalid value for --min.");
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--sep") == 0 || strcmp(argv[i], "-sep") == 0) {
            if (i + 1 >= argc) {
                return kc_ngram_fail_usage("Missing value for --sep.");
            }

            options.separators = argv[i + 1];
            i++;
            continue;
        }

        if (strcmp(argv[i], "--cmd") == 0 || strcmp(argv[i], "-cmd") == 0) {
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

    if (context.command != NULL) {
        if (kc_ngram_parse_command(context.command, &context.args) != 0) {
            return kc_ngram_fail_usage("Invalid value for --cmd.");
        }
    }

    if (text == NULL) {
        text = kc_ngram_read_stdin(buffer, sizeof(buffer));
    }

    if (text == NULL || *text == '\0') {
        kc_ngram_free_args(&context.args);
        return 0;
    }

    rc = kc_ngram_execute(text, &options, kc_ngram_cli_visit, &context);
    kc_ngram_free_args(&context.args);

    if (rc < 0) {
        return 1;
    }

    return 0;
}
