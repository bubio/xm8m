#!/usr/bin/env bash

set -eu

verify_script="$1"
test_root="$(mktemp -d /tmp/xm8-verify-ra-test.XXXXXX)"
trap 'rm -rf "$test_root"' EXIT

make_tools() {
    tools_dir="$1"
    mkdir -p "$tools_dir"
    printf '#!/bin/sh\nexit 0\n' > "$tools_dir/cmake"
    printf '#!/bin/sh\nexit 0\n' > "$tools_dir/ctest"
    chmod +x "$tools_dir/cmake" "$tools_dir/ctest"
}

new_repo() {
    repo="$1"
    mkdir -p "$repo/scripts"
    cp "$verify_script" "$repo/scripts/verify_ra.sh"
    chmod +x "$repo/scripts/verify_ra.sh"
    git -C "$repo" init -q -b main
    git -C "$repo" config user.email test@example.invalid
    git -C "$repo" config user.name "XM8 Test"
    printf 'clean\n' > "$repo/sample.txt"
    git -C "$repo" add .
    git -C "$repo" commit -qm base
}

tools_dir="$test_root/tools"
make_tools "$tools_dir"

committed_repo="$test_root/committed"
new_repo "$committed_repo"
git -C "$committed_repo" switch -qc feature
printf 'trailing space \n' >> "$committed_repo/sample.txt"
git -C "$committed_repo" add sample.txt
git -C "$committed_repo" commit -qm violation
if PATH="$tools_dir:$PATH" "$committed_repo/scripts/verify_ra.sh" \
    --scope full --base-ref main --report "$test_root/committed.md" >/dev/null 2>&1; then
    echo "FAIL: committed whitespace violation was not detected" >&2
    exit 1
fi

working_repo="$test_root/working"
new_repo "$working_repo"
printf 'working violation \n' >> "$working_repo/sample.txt"
if PATH="$tools_dir:$PATH" "$working_repo/scripts/verify_ra.sh" \
    --scope full --base-ref main --report "$test_root/working.md" >/dev/null 2>&1; then
    echo "FAIL: working-tree whitespace violation was not detected" >&2
    exit 1
fi

crlf_repo="$test_root/crlf"
new_repo "$crlf_repo"
printf 'first\r\nsecond\r\n' > "$crlf_repo/crlf.txt"
if ! PATH="$tools_dir:$PATH" "$crlf_repo/scripts/verify_ra.sh" \
    --scope full --base-ref main --report "$test_root/crlf.md" >/dev/null 2>&1; then
    echo "FAIL: CRLF line endings were treated as trailing whitespace" >&2
    exit 1
fi

grep -q 'Base ref: `main`' "$test_root/crlf.md"
grep -q 'Merge-base:' "$test_root/crlf.md"
