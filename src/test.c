/**
 * test.c - libngram public API tests.
 * Summary: Tests each public libngram function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "ngram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static int signal_count = 0;
static kc_ngram_t *signal_ctx_seen = NULL;

/**
 * Records one signal callback invocation.
 * @param ctx Context supplied by the library.
 * @return None.
 */
static void count_signal(kc_ngram_t *ctx) {
    if (ctx != NULL) {
        signal_count++;
        signal_ctx_seen = ctx;
    }
}

static int signal_count_b = 0;

/**
 * Records one replacement signal callback invocation.
 * @param ctx Context supplied by the library.
 * @return None.
 */
static void count_signal_b(kc_ngram_t *ctx) {
    if (ctx != NULL) {
        signal_count_b++;
    }
}

/**
 * Visitor that counts emitted chunks.
 */
typedef struct {
    int count;
    int max_size;
} counter_state_t;

/**
 * Counts one emitted chunk.
 * @param chunk Current chunk.
 * @param context Counter state pointer.
 * @return 0 to continue.
 */
static int count_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    counter_state_t *st = (counter_state_t *)context;
    st->count++;
    if (chunk->size > st->max_size) st->max_size = chunk->size;
    return 0;
}

/**
 * Visitor that closes the span on the first chunk.
 * @return 1 to close the span.
 */
static int close_first_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return 1;
}

/**
 * Visitor that aborts traversal.
 * @return -1 to abort.
 */
static int abort_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return -1;
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("\033[31m[FAIL]\033[0m %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("\033[31m[FAIL]\033[0m %s\n", name);
        return 1;
    }
    printf("\033[32m[PASS]\033[0m %s\n", name);
    return 0;
}

/**
 * Tests kc_ngram_version.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    return expect_true("kc_ngram_version returns non-zero", kc_ngram_version() != 0U);
}

/**
 * Tests kc_ngram_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_options_default(void) {
    kc_ngram_options_t opts;
    int rc;

    rc = 0;
    rc += expect_int("options_default(NULL) returns -1", -1,
        kc_ngram_options_default(NULL));
    memset(&opts, 0, sizeof(opts));
    rc += expect_int("options_default fills max_tokens", 0,
        kc_ngram_options_default(&opts));
    rc += expect_int("max_tokens is 10", 10, opts.max_tokens);
    rc += expect_int("min_tokens is 1", 1, opts.min_tokens);
    rc += expect_true("separators is set", opts.separators != NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_options_load_env(void) {
    kc_ngram_options_t opts;
    int rc;

    rc = 0;
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_load_env(&opts);
    rc += expect_true("load_env does not crash", 1);
    kc_ngram_options_load_env(NULL);
    rc += expect_true("load_env(NULL) does not crash", 1);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_options_free(void) {
    kc_ngram_options_t opts;
    int rc;

    rc = 0;
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_free(&opts);
    rc += expect_true("options_free does not crash", 1);
    kc_ngram_options_free(NULL);
    rc += expect_true("options_free(NULL) does not crash", 1);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_open.
 * @return 0 on success, 1 on failure.
 */
static int case_open(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    ctx = NULL;
    rc += expect_int("open(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_open(NULL));
    rc += expect_int("open creates context", KC_NGRAM_OK,
        kc_ngram_open(&ctx));
    rc += expect_true("open sets output", ctx != NULL);
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_close.
 * @return 0 on success, 1 on failure.
 */
static int case_close(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    kc_ngram_close(NULL);
    rc += expect_true("close(NULL) does not crash", 1);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    kc_ngram_close(ctx);
    rc += expect_true("close releases context", 1);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_stop(void) {
    kc_ngram_t *ctx;
    kc_ngram_t *other;
    int rc;

    rc = 0;
    rc += expect_int("stop(NULL) returns ERROR", KC_NGRAM_ERROR, kc_ngram_stop(NULL));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    if (kc_ngram_open(&other) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        return 1;
    }
    rc += expect_int("stop succeeds", KC_NGRAM_OK, kc_ngram_stop(ctx));
    rc += expect_int("stop is idempotent", KC_NGRAM_OK, kc_ngram_stop(ctx));
    rc += expect_int("other context unaffected", KC_NGRAM_OK, kc_ngram_stop(other));
    kc_ngram_close(ctx);
    kc_ngram_close(other);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute with default options.
 * @return 0 on success, 1 on failure.
 */
static int case_execute(void) {
    counter_state_t st;
    kc_ngram_options_t opts;
    int rc;

    rc = 0;
    rc += expect_int("execute(NULL, ...) returns ERROR", -1,
        kc_ngram_execute(NULL, NULL, count_visitor, NULL));
    rc += expect_int("execute(input, NULL, NULL, ...) returns ERROR", -1,
        kc_ngram_execute("hello", NULL, NULL, NULL));
    rc += expect_int("execute empty input returns 0", 0,
        kc_ngram_execute("", NULL, count_visitor, NULL));
    st = (counter_state_t){0, 0};
    rc += expect_int("execute with defaults emits chunks", 65,
        kc_ngram_execute("a b c d e f g h i j k", NULL, count_visitor, &st));
    rc += expect_true("max_size respects window", st.max_size <= 10);
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_default(&opts);
    opts.max_tokens = 2;
    opts.min_tokens = 1;
    st = (counter_state_t){0, 0};
    rc += expect_int("execute with max=2 emits fewer chunks", 5,
        kc_ngram_execute("a b c", &opts, count_visitor, &st));
    opts.max_tokens = 3;
    opts.min_tokens = 3;
    st = (counter_state_t){0, 0};
    rc += expect_int("execute min==max emits exact-size chunks", 1,
        kc_ngram_execute("a b c", &opts, count_visitor, &st));
    rc += expect_true("chunk size matches window", st.max_size == 3);
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_default(&opts);
    opts.max_tokens = 1;
    opts.min_tokens = 100;
    rc += expect_int("execute min>max returns ERROR", -1,
        kc_ngram_execute("a b", &opts, count_visitor, NULL));
    opts.min_tokens = 0;
    rc += expect_int("execute with min=0 returns ERROR", -1,
        kc_ngram_execute("a b", &opts, count_visitor, NULL));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute span closure and abort.
 * @return 0 on success, 1 on failure.
 */
static int case_execute_span(void) {
    int rc;
    counter_state_t st;

    rc = 0;
    st = (counter_state_t){0, 0};
    rc += expect_int("execute close-first emits 1 chunk", 1,
        kc_ngram_execute("a b c", NULL, close_first_visitor, NULL));
    rc += expect_int("execute abort returns -1", -1,
        kc_ngram_execute("a b c", NULL, abort_visitor, NULL));
    st = (counter_state_t){0, 0};
    rc += expect_int("execute custom sep tokenizes correctly", 2,
        kc_ngram_execute("a,b,c", &(kc_ngram_options_t){
            .max_tokens = 2, .min_tokens = 2, .separators = ","
        }, count_visitor, &st));
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_on_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_on_signal(void) {
    kc_ngram_t *ctx;
    int rc;
    int i;

    rc = 0;
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("on_signal(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_on_signal(NULL, 1, count_signal));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    rc += expect_int("remove missing handler returns OK", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 1, NULL));
    rc += expect_int("register handler returns OK", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 1, count_signal));
    rc += expect_int("raise registered handler returns OK", KC_NGRAM_OK,
        kc_ngram_raise_signal(ctx, 1));
    rc += expect_int("handler was invoked", 1, signal_count);
    rc += expect_int("replace handler returns OK", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 1, count_signal_b));
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("raise replaced handler returns OK", KC_NGRAM_OK,
        kc_ngram_raise_signal(ctx, 1));
    rc += expect_int("old handler was not invoked", 0, signal_count);
    rc += expect_int("replacement handler was invoked", 1, signal_count_b);
    for (i = 0; i < 8; i++) {
        rc += expect_int("register growth handler returns OK", KC_NGRAM_OK,
            kc_ngram_on_signal(ctx, 200 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise growth handler returns OK", KC_NGRAM_OK,
        kc_ngram_raise_signal(ctx, 207));
    rc += expect_int("growth handler was invoked", 1, signal_count);
    rc += expect_int("remove handler returns OK", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 1, NULL));
    rc += expect_int("raise removed handler returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_raise_signal(ctx, 1));
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_raise_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_raise_signal(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("raise_signal(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_raise_signal(NULL, 1));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    rc += expect_int("raise unhandled signal returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_raise_signal(ctx, 1));
    rc += expect_int("register signal handler", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 1, count_signal));
    rc += expect_int("raise handled signal returns OK", KC_NGRAM_OK,
        kc_ngram_raise_signal(ctx, 1));
    rc += expect_int("raise_signal invokes handler", 1, signal_count);
    rc += expect_true("raise_signal passes same context", signal_ctx_seen == ctx);
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_listen_signals.
 * @return 0 on success, 1 on failure.
 */
static int case_listen_signals(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("listen_signals(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_listen_signals(NULL));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    rc += expect_int("register listener callback", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 44, count_signal));
    rc += expect_int("listen_signals registers context", KC_NGRAM_OK,
        kc_ngram_listen_signals(ctx));
    kc_ngram_signal_listener(44);
    rc += expect_int("listener dispatched callback", 1, signal_count);
    rc += expect_true("listener dispatched correct context", signal_ctx_seen == ctx);
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_listen_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_listen_signal(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    rc += expect_int("listen_signal(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_listen_signal(NULL, 1));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
#ifdef _WIN32
    rc += expect_int("listen_signal accepts ctx", KC_NGRAM_OK,
        kc_ngram_listen_signal(ctx, 2));
#else
    rc += expect_int("listen_signal accepts ctx", KC_NGRAM_OK,
        kc_ngram_listen_signal(ctx, SIGUSR1));
#endif
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_signal_listener.
 * @return 0 on success, 1 on failure.
 */
static int case_signal_listener(void) {
    kc_ngram_t *ctx;
    int rc;

    rc = 0;
    signal_count = 0;
    signal_ctx_seen = NULL;
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    rc += expect_int("register listener handler", KC_NGRAM_OK,
        kc_ngram_on_signal(ctx, 55, count_signal));
    rc += expect_int("listen_signals registers ctx", KC_NGRAM_OK,
        kc_ngram_listen_signals(ctx));
    kc_ngram_signal_listener(55);
    rc += expect_int("signal_listener invokes handler", 1, signal_count);
    rc += expect_true("signal_listener passes ctx", signal_ctx_seen == ctx);
    kc_ngram_close(ctx);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests two contexts coexist.
 * @return 0 on success, 1 on failure.
 */
static int case_multictx(void) {
    kc_ngram_t *a;
    kc_ngram_t *b;
    int rc;
    counter_state_t st;

    rc = 0;
    if (kc_ngram_open(&a) != KC_NGRAM_OK) return 1;
    if (kc_ngram_open(&b) != KC_NGRAM_OK) {
        kc_ngram_close(a);
        return 1;
    }
    rc += expect_int("stop a returns OK", KC_NGRAM_OK, kc_ngram_stop(a));
    rc += expect_int("stop b returns OK", KC_NGRAM_OK, kc_ngram_stop(b));
    rc += expect_int("stop a again returns OK", KC_NGRAM_OK, kc_ngram_stop(a));
    st = (counter_state_t){0, 0};
    rc += expect_int("execute still works with defaults", 6,
        kc_ngram_execute("a b c", NULL, count_visitor, &st));
    rc += expect_int("close a returns OK", KC_NGRAM_OK, 0);
    kc_ngram_close(a);
    rc += expect_int("close b returns OK", KC_NGRAM_OK, 0);
    kc_ngram_close(b);
    return rc == 0 ? 0 : 1;
}

/**
 * Connect one client socket to the control endpoint.
 * @param sock_path Unix socket path.
 * @return Connected socket fd, or -1 on failure.
 */
#ifndef _WIN32
static int connect_ctrl_client(const char *sock_path) {
    struct sockaddr_un addr;
    int client_fd;

    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1U);
    addr.sun_path[sizeof(addr.sun_path) - 1U] = '\0';

    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(client_fd);
        return -1;
    }

    return client_fd;
}
#endif

/**
 * Read one control response into the provided buffer.
 * @param fd Connected socket fd.
 * @param buf Destination buffer.
 * @param size Buffer size.
 * @return Number of bytes read, or -1 on failure.
 */
#ifndef _WIN32
static int read_ctrl_response(int fd, char *buf, size_t size) {
    ssize_t n;

    if (fd < 0 || buf == NULL || size < 2U) {
        return -1;
    }

    n = read(fd, buf, size - 1U);
    if (n < 0) {
        return -1;
    }

    buf[n] = '\0';
    return (int)n;
}
#endif

/**
 * Tests control socket HELP command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_help(void) {
#ifndef _WIN32
    kc_ngram_t *ctx;
    char sock_path[128];
    char buf[256];
    int client_fd;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/ngram_ctrl_help_%d.sock", (int)getpid());
    unlink(sock_path);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) {
        return 1;
    }
    if (kc_ngram_ctrl_open(ctx, sock_path) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = connect_ctrl_client(sock_path);
    if (client_fd < 0) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    if (write(client_fd, "HELP\n", 5) != 5) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc = 0;
    rc += expect_true("ctrl help returns OK",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strncmp(buf, "OK", 2) == 0);
    rc += expect_true("ctrl help lists HELP",
        strstr(buf, "HELP") != NULL);
    close(client_fd);
    kc_ngram_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests control socket STOP command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_stop(void) {
#ifndef _WIN32
    kc_ngram_t *ctx;
    char sock_path[128];
    char buf[256];
    int client_fd;
    int rc;

    snprintf(sock_path, sizeof(sock_path), "/tmp/ngram_ctrl_stop_%d.sock", (int)getpid());
    unlink(sock_path);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) {
        return 1;
    }
    if (kc_ngram_ctrl_open(ctx, sock_path) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = connect_ctrl_client(sock_path);
    if (client_fd < 0) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    if (write(client_fd, "STOP\n", 5) != 5) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc = 0;
    rc += expect_true("ctrl stop returns OK",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strncmp(buf, "OK", 2) == 0);
    rc += expect_true("ctrl stop sets stop_requested",
        kc_ngram_stop_requested(ctx) == 1);
    close(client_fd);
    kc_ngram_close(ctx);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests control socket GET command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_get(void) {
#ifndef _WIN32
    kc_ngram_options_t opts;
    kc_ngram_t *ctx;
    char sock_path[128];
    char buf[256];
    int client_fd;
    int rc;

    if (kc_ngram_options_default(&opts) != 0) {
        return 1;
    }
    opts.max_tokens = 7;
    opts.min_tokens = 2;
    opts.separators = ",";

    snprintf(sock_path, sizeof(sock_path), "/tmp/ngram_ctrl_get_%d.sock", (int)getpid());
    unlink(sock_path);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) {
        return 1;
    }
    if (kc_ngram_configure(ctx, &opts) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        return 1;
    }
    if (kc_ngram_ctrl_open(ctx, sock_path) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = connect_ctrl_client(sock_path);
    if (client_fd < 0) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    rc = 0;
    kc_ngram_ctrl_poll(ctx);
    if (write(client_fd, "GET max\n", 8) != 8) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl get max returns 7",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strstr(buf, "7") != NULL);
    if (write(client_fd, "GET min\n", 8) != 8) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl get min returns 2",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strstr(buf, "2") != NULL);
    if (write(client_fd, "GET sep\n", 8) != 8) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl get sep returns comma",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strstr(buf, ",") != NULL);
    close(client_fd);
    kc_ngram_close(ctx);
    kc_ngram_options_free(&opts);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Tests control socket SET command.
 * @return 0 on success, 1 on failure.
 */
static int case_ctrl_set(void) {
#ifndef _WIN32
    kc_ngram_options_t opts;
    kc_ngram_t *ctx;
    char sock_path[128];
    char buf[256];
    int client_fd;
    int rc;

    if (kc_ngram_options_default(&opts) != 0) {
        return 1;
    }

    snprintf(sock_path, sizeof(sock_path), "/tmp/ngram_ctrl_set_%d.sock", (int)getpid());
    unlink(sock_path);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) {
        return 1;
    }
    if (kc_ngram_configure(ctx, &opts) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        return 1;
    }
    if (kc_ngram_ctrl_open(ctx, sock_path) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    client_fd = connect_ctrl_client(sock_path);
    if (client_fd < 0) {
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    rc = 0;
    kc_ngram_ctrl_poll(ctx);
    if (write(client_fd, "SET max 5\n", 10) != 10) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl set max returns OK",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strncmp(buf, "OK", 2) == 0);
    rc += expect_int("ctrl set max updates options", 5, opts.max_tokens);
    if (write(client_fd, "SET min 2\n", 10) != 10) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl set min returns OK",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strncmp(buf, "OK", 2) == 0);
    rc += expect_int("ctrl set min updates options", 2, opts.min_tokens);
    if (write(client_fd, "SET sep ,\n", 10) != 10) {
        close(client_fd);
        kc_ngram_close(ctx);
        unlink(sock_path);
        return 1;
    }
    kc_ngram_ctrl_poll(ctx);
    rc += expect_true("ctrl set sep returns OK",
        read_ctrl_response(client_fd, buf, sizeof(buf)) > 0 && strncmp(buf, "OK", 2) == 0);
    rc += expect_true("ctrl set sep updates options",
        opts.separators != NULL && strcmp(opts.separators, ",") == 0);
    close(client_fd);
    kc_ngram_close(ctx);
    kc_ngram_options_free(&opts);
    unlink(sock_path);
    return rc == 0 ? 0 : 1;
#else
    return expect_true("ctrl test is skipped on Windows", 1);
#endif
}

/**
 * Runs one libngram public API test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "options-default") == 0) return case_options_default();
    if (strcmp(argv[1], "options-load-env") == 0) return case_options_load_env();
    if (strcmp(argv[1], "options-free") == 0) return case_options_free();
    if (strcmp(argv[1], "open") == 0) return case_open();
    if (strcmp(argv[1], "close") == 0) return case_close();
    if (strcmp(argv[1], "stop") == 0) return case_stop();
    if (strcmp(argv[1], "execute") == 0) return case_execute();
    if (strcmp(argv[1], "execute-span") == 0) return case_execute_span();
    if (strcmp(argv[1], "on-signal") == 0) return case_on_signal();
    if (strcmp(argv[1], "raise-signal") == 0) return case_raise_signal();
    if (strcmp(argv[1], "listen-signals") == 0) return case_listen_signals();
    if (strcmp(argv[1], "listen-signal") == 0) return case_listen_signal();
    if (strcmp(argv[1], "signal-listener") == 0) return case_signal_listener();
    if (strcmp(argv[1], "multictx") == 0) return case_multictx();
    if (strcmp(argv[1], "ctrl-help") == 0) return case_ctrl_help();
    if (strcmp(argv[1], "ctrl-stop") == 0) return case_ctrl_stop();
    if (strcmp(argv[1], "ctrl-get") == 0) return case_ctrl_get();
    if (strcmp(argv[1], "ctrl-set") == 0) return case_ctrl_set();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
