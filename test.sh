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

# Checks whether the ngram binary exists in the current directory.
# @return 0 on success, 1 on failure.
kc_test_check_binary() {
    if [ ! -f "./ngram" ]; then
        echo "[ERROR] ngram binary not found. Please compile first."
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
    echo "Expected:"
    printf '%s\n' "$expected"
    echo "Actual:"
    printf '%s\n' "$actual"
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

    kc_test_check_binary || exit 1

    echo "Starting ngram validation suite..."
    echo "---------------------------------"

    expected="$(cat <<'EOF'
one two three
one two
two three
one
two
three
EOF
)"
    kc_test_run_case "default traversal" "$expected" ./ngram "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two
two three
one
two
three
EOF
)"
    kc_test_run_case "bounded max window" "$expected" ./ngram -max 2 -min 1 "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two
two three
EOF
)"
    kc_test_run_case "bounded min window" "$expected" ./ngram -max 2 -min 2 "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
red blue green
red blue
blue green
red
blue
green
EOF
)"
    kc_test_run_case "custom separator set" "$expected" ./ngram -sep ",;" "red,blue;green" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two three
EOF
)"
    kc_test_run_case "span closing command" "$expected" ./ngram -cmd 'cat >/dev/null; echo cut' "one two three" || failed=$((failed + 1))

    expected="$(cat <<'EOF'
one two three
EOF
)"
    kc_test_run_pipe_case "stdin input" "one two three" "$expected" ./ngram -max 3 -min 3 || failed=$((failed + 1))

    echo "---------------------------------"
    if [ "$failed" -eq 0 ]; then
        echo "[SUCCESS] All ngram tests passed!"
        return 0
    fi

    echo "[FAILURE] $failed tests failed."
    return 1
}

kc_test_main
