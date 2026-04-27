#!/bin/bash
# test.sh
# Summary: Validation suite for ngram CLI traversal behavior.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

# Prints one failure line using the shared KCS color style.
# @param 1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    printf "\033[31m[FAIL]\033[0m %s\n" "$1"
    return 1
}

# Prints one success line using the shared KCS color style.
# @param 1 Success message.
# @return 0 on success.
kc_test_pass() {
    printf "\033[32m[PASS]\033[0m %s\n" "$1"
}

# Detects the artifact architecture for the current machine.
# @return Architecture name on stdout.
kc_test_arch() {
    case "$(uname -m)" in
        x86_64 | amd64)
            printf '%s\n' "x86_64"
            ;;
        aarch64 | arm64)
            printf '%s\n' "aarch64"
            ;;
        armv7l | armv7)
            printf '%s\n' "armv7"
            ;;
        i386 | i486 | i586 | i686)
            printf '%s\n' "i686"
            ;;
        ppc64le | powerpc64le)
            printf '%s\n' "powerpc64le"
            ;;
        *)
            uname -m
            ;;
    esac
}

# Detects the artifact platform for the current machine.
# @return Platform name on stdout.
kc_test_platform() {
    case "$(uname -s)" in
        Linux)
            printf '%s\n' "linux"
            ;;
        *)
            uname -s | tr '[:upper:]' '[:lower:]'
            ;;
    esac
}

# Returns the CLI path for the current architecture and platform.
# @return CLI path on stdout.
kc_test_binary_path() {
    printf './bin/%s/%s/ngram\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Checks whether the ngram binary exists in the artifact directory.
# @return 0 on success, 1 on failure.
kc_test_check_binary() {
    if [ ! -x "$BIN" ]; then
        return 1
    fi

    return 0
}

# Runs one CLI test case and compares full stdout with the expected value.
# @param 1 Case label.
# @param 2 Expected stdout.
# @param 3... Command and arguments to execute.
# @return 0 on success, 1 on failure.
kc_test_run_case() {
    local label="$1"
    local expected="$2"
    shift 2

    local actual

    actual="$("$@")"
    if [ "$actual" = "$expected" ]; then
        kc_test_pass "$label"
        return 0
    fi

    kc_test_fail "$label"
    return 1
}

# Runs one stdin-driven CLI test case and compares full stdout.
# @param 1 Case label.
# @param 2 Input text.
# @param 3 Expected stdout.
# @param 4... Command and arguments to execute.
# @return 0 on success, 1 on failure.
kc_test_run_pipe_case() {
    local label="$1"
    local input="$2"
    local expected="$3"
    shift 3

    local actual

    actual="$(printf '%s' "$input" | "$@")"
    if [ "$actual" = "$expected" ]; then
        kc_test_pass "$label"
        return 0
    fi

    kc_test_fail "$label"
    echo "Expected:"
    printf '%s\n' "$expected"
    echo "Actual:"
    printf '%s\n' "$actual"
    return 1
}

# Runs the full validation suite for ngram traversal semantics.
# @return 0 when all tests pass, 1 otherwise.
kc_test_main() {
    local failed=0
    local expected

    BIN=$(kc_test_binary_path)

    kc_test_check_binary || exit 1

    expected="$(cat <<'EOF'
one two three
one two
two three
one
two
three
EOF
)"
    kc_test_run_case "default traversal" "$expected" "$BIN" "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two
two three
one
two
three
EOF
)"
    kc_test_run_case "bounded max window" "$expected" "$BIN" -max 2 -min 1 "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two
two three
EOF
)"
    kc_test_run_case "bounded min window" "$expected" "$BIN" -max 2 -min 2 "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
red,blue;green
red,blue
blue;green
red
blue
green
EOF
)"
    kc_test_run_case "custom separator set" "$expected" "$BIN" -sep ",;" "red,blue;green" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
red,,blue;green
red,,blue
blue;green
red
blue
green
EOF
)"
    kc_test_run_case "custom separators preserve original bytes" "$expected" "$BIN" -sep ",;" "red,,blue;green" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one  two   three
one  two
two   three
one
two
three
EOF
)"
    kc_test_run_case "default separators preserve repeated spaces" "$expected" "$BIN" "one  two   three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two three
EOF
)"
    kc_test_run_case "span closing command" "$expected" "$BIN" -cmd 'sh -c '\''cat >/dev/null; echo cut'\''' "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two
two three
three
EOF
)"
    kc_test_run_case "closed span pruning keeps partial overlaps" "$expected" "$BIN" -max 2 -min 1 -cmd "sh -c 'grep -qx \"one two\" && echo cut'" "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
a b c
b c d
c d e
c d
d e
EOF
)"
    kc_test_run_case "earlier wide close still suppresses later contained windows" "$expected" "$BIN" -max 3 -min 1 -cmd "sh -c 'grep -qx \"a b c\\|d e\" && echo cut'" "a b c d e" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
a b c d
b c d e
EOF
)"
    kc_test_run_case "closing all multi token spans keeps maximal windows only" "$expected" "$BIN" -max 4 -min 1 -cmd "sh -c 'grep -q \" \" && echo cut'" "a b c d e" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
a b
b c
c d
d e
e f
c
d
EOF
)"
    kc_test_run_case "multiple disjoint closed spans preserve gaps and overlaps" "$expected" "$BIN" -max 2 -min 1 -cmd "sh -c 'grep -qx \"a b\\|e f\" && echo cut'" "a b c d e f" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two three
two three four
EOF
)"
    kc_test_run_case "overlapping closed spans keep the same output" "$expected" "$BIN" -max 3 -min 1 -cmd "sh -c 'grep -q \" \" && echo cut'" "one two three four" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
t0 t1
t1 t2
t2 t3
t3 t4
t4 t5
t5 t6
t6 t7
EOF
)"
    kc_test_run_case "stress many span closures" "$expected" "$BIN" -max 2 -min 1 -cmd "sh -c 'grep -q \" \" && echo cut'" "t0 t1 t2 t3 t4 t5 t6 t7" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
t0 t1 t2
t1 t2 t3
t2 t3 t4
t3 t4 t5
t4 t5 t6
EOF
)"
    kc_test_run_case "stress overlapping trigram closures" "$expected" "$BIN" -max 3 -min 1 -cmd "sh -c 'grep -Eq \"^[^ ]+ [^ ]+ [^ ]+$\" && echo cut'" "t0 t1 t2 t3 t4 t5 t6" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two three
EOF
)"
    kc_test_run_pipe_case "stdin input" "one two three" "$expected" "$BIN" -max 3 -min 3 || failed=$((failed + 1))

    kc_test_run_case "large stdin beyond fixed cap" "70001" bash -lc "awk 'BEGIN { for (i = 0; i < 70000; i++) printf \"a\"; }' | \"$BIN\" | wc -c | tr -d '[:space:]'" || failed=$((failed + 1))

    if [ "$failed" -eq 0 ]; then
        return 0
    fi

    return 1
}

kc_test_main
