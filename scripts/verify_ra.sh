#!/usr/bin/env bash

set -u

usage() {
    cat <<'USAGE'
Usage: scripts/verify_ra.sh --scope SCOPE [options]

Scopes:
  ui       Menu, overlay, text conversion, and input-adjacent tests
  disk     D88, Drive 1/2, Library launch, and media policy tests
  state    Session, state, frame callback, and service tests
  auth     Credentials, HTTP contract, user agent, and service tests
  full     All tests in the RA-enabled build

Options:
  --ra-build-dir DIR      RA-enabled build directory (default: build-ra)
  --jobs N                Parallel build jobs (default: 4)
  --report FILE           Markdown result report path
  --base-ref REF          Diff base (default: origin/main, main, then HEAD)
  --dry-run               Print the planned commands without running them
  -h, --help              Show this help
USAGE
}

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
scope=""
ra_build_dir="build-ra"
jobs="4"
report_path=""
base_ref=""
dry_run=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --scope)
            [ "$#" -ge 2 ] || { echo "Error: --scope requires a value" >&2; exit 2; }
            scope="$2"
            shift 2
            ;;
        --ra-build-dir)
            [ "$#" -ge 2 ] || { echo "Error: --ra-build-dir requires a value" >&2; exit 2; }
            ra_build_dir="$2"
            shift 2
            ;;
        --jobs)
            [ "$#" -ge 2 ] || { echo "Error: --jobs requires a value" >&2; exit 2; }
            jobs="$2"
            shift 2
            ;;
        --report)
            [ "$#" -ge 2 ] || { echo "Error: --report requires a value" >&2; exit 2; }
            report_path="$2"
            shift 2
            ;;
        --base-ref)
            [ "$#" -ge 2 ] || { echo "Error: --base-ref requires a value" >&2; exit 2; }
            base_ref="$2"
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$scope" in
    ui|disk|state|auth|full) ;;
    "")
        echo "Error: --scope is required" >&2
        usage >&2
        exit 2
        ;;
    *)
        echo "Error: unsupported scope: $scope" >&2
        usage >&2
        exit 2
        ;;
esac

case "$jobs" in
    ''|*[!0-9]*)
        echo "Error: --jobs must be a positive integer" >&2
        exit 2
        ;;
    *)
        if [ "$jobs" -eq 0 ]; then
            echo "Error: --jobs must be a positive integer" >&2
            exit 2
        fi
        ;;
esac

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$repo_root" "$1" ;;
    esac
}

ra_build_dir="$(absolute_path "$ra_build_dir")"

if [ -z "$base_ref" ]; then
    if git -C "$repo_root" rev-parse --verify --quiet refs/remotes/origin/main >/dev/null; then
        base_ref="origin/main"
    elif git -C "$repo_root" rev-parse --verify --quiet refs/heads/main >/dev/null; then
        base_ref="main"
    else
        base_ref="HEAD"
    fi
fi
if ! git -C "$repo_root" rev-parse --verify --quiet "${base_ref}^{commit}" >/dev/null; then
    echo "Error: base ref does not resolve to a commit: $base_ref" >&2
    exit 2
fi
merge_base="$(git -C "$repo_root" merge-base HEAD "$base_ref" 2>/dev/null || true)"
if [ -z "$merge_base" ]; then
    echo "Error: cannot determine merge-base for $base_ref" >&2
    exit 2
fi

if [ -z "$report_path" ]; then
    report_path="/tmp/xm8-validation-${scope}-$(date +%Y%m%d-%H%M%S).md"
else
    report_path="$(absolute_path "$report_path")"
fi

if [ "$dry_run" -eq 0 ]; then
    command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is not installed" >&2; exit 2; }
    command -v ctest >/dev/null 2>&1 || { echo "Error: ctest is not installed" >&2; exit 2; }
    command -v git >/dev/null 2>&1 || { echo "Error: git is not installed" >&2; exit 2; }
fi

ra_targets=(xm8)
test_regex=""

case "$scope" in
    ui)
        ra_targets+=(menu_file_routing_test ra_menu_status_test ra_overlay_test ra_text_converter_test)
        test_regex='^(menu_file_routing_test|ra_menu_status_test|ra_overlay_test|ra_text_converter_test|converter_mac_test)$'
        if [ "$(uname -s)" = "Darwin" ]; then
            ra_targets+=(converter_mac_test)
        fi
        ;;
    disk)
        ra_targets+=(clidisk_test menu_file_routing_test d88probe_test d88fixture_test d88write_test fileio_error_test
            ra_media_probe_test ra_media_change_policy_test ra_disk_transaction_test
	    ra_media_request_integration_test ra_app_media_integration_test ra_drop_session_scenario_test ra_media_state_matrix_test
            ra_library_launch_policy_test
            ra_auxiliary_mount_commit_policy_test ra_multi_image_policy_test ra_media_eject_policy_test
            ra_library_store_test ra_seed_library_fixture_test ra_working_media_identity_test)
	    test_regex='^(clidisk_test|menu_file_routing_test|d88probe_test|d88fixture_test|d88write_test|fileio_error_test|ra_media_probe_test|ra_media_change_policy_test|ra_disk_transaction_test|ra_media_request_integration_test|ra_app_media_integration_test|ra_drop_session_scenario_test|ra_media_state_matrix_test|verify_ra_script_test|ra_library_launch_policy_test|ra_auxiliary_mount_commit_policy_test|ra_multi_image_policy_test|ra_media_eject_policy_test|ra_library_store_test|ra_seed_library_fixture_test|ra_working_media_identity_test)$'
        ;;
    state)
        ra_targets+=(host_frame_callback_test event_host_frame_integration_test fileio_error_test
            ra_callback_lifetime_test ra_session_state_test ra_session_policy_test ra_state_store_test ra_service_test)
        test_regex='^(host_frame_callback_test|event_host_frame_integration_test|fileio_error_test|ra_callback_lifetime_test|ra_session_state_test|ra_session_policy_test|ra_state_store_test|ra_service_test)$'
        ;;
    auth)
        ra_targets+=(ra_dependency_test ra_user_agent_test ra_credentials_http_test ra_service_test)
        test_regex='^(ra_dependency_test|ra_user_agent_test|ra_credentials_http_test|ra_service_test)$'
        ;;
    full)
        ;;
esac

mkdir_report_parent() {
    report_parent="$(dirname "$report_path")"
    if [ "$dry_run" -eq 0 ] && [ ! -d "$report_parent" ]; then
        mkdir -p "$report_parent"
    fi
}

mkdir_report_parent

if [ "$dry_run" -eq 0 ]; then
    commit="$(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || printf 'unknown')"
    {
        echo "# XM8 validation report"
        echo
        echo "- Scope: \`$scope\`"
        echo "- Commit: \`$commit\`"
        echo "- Base ref: \`$base_ref\`"
        echo "- Merge-base: \`$merge_base\`"
        echo "- RA-enabled build: \`$ra_build_dir\`"
        echo "- Started: \`$(date '+%Y-%m-%d %H:%M:%S %Z')\`"
        echo
        echo "## Automated checks"
        echo
    } > "$report_path"
fi

failures=0

print_command() {
    printf '  '
    printf '%q ' "$@"
    printf '\n'
}

record_result() {
    step_name="$1"
    step_result="$2"
    if [ "$dry_run" -eq 0 ]; then
        printf -- '- %s: **%s**\n' "$step_name" "$step_result" >> "$report_path"
    fi
}

run_step() {
    step_name="$1"
    shift
    echo "==> $step_name"
    print_command "$@"
    if [ "$dry_run" -eq 1 ]; then
        return 0
    fi
    if "$@"; then
        record_result "$step_name" "PASS"
        return 0
    fi
    record_result "$step_name" "FAIL"
    failures=$((failures + 1))
    return 1
}

cache_value() {
    cache_file="$1"
    cache_key="$2"
    sed -n "s/^${cache_key}:[^=]*=//p" "$cache_file" | tail -n 1
}

configure_build() {
    build_dir="$1"
    cache_file="$build_dir/CMakeCache.txt"

    if [ -f "$cache_file" ]; then
        actual_ra="$(cache_value "$cache_file" XM8_ENABLE_RETROACHIEVEMENTS)"
        actual_testing="$(cache_value "$cache_file" BUILD_TESTING)"
        if [ "$actual_ra" != "ON" ] || [ "$actual_testing" != "ON" ]; then
            echo "Error: incompatible existing build directory: $build_dir" >&2
            echo "  expected RA=ON BUILD_TESTING=ON" >&2
            echo "  actual   RA=${actual_ra:-unknown} BUILD_TESTING=${actual_testing:-unknown}" >&2
            failures=$((failures + 1))
            record_result "Configure $(basename "$build_dir")" "FAIL"
            return 1
        fi
        echo "==> Reuse configured $(basename "$build_dir") (RA=ON, tests=ON)"
        record_result "Configure $(basename "$build_dir")" "PASS (reused)"
        return 0
    fi

    configure_args=(cmake -S "$repo_root" -B "$build_dir"
        -DCMAKE_BUILD_TYPE=Release
        -DXM8_ENABLE_RETROACHIEVEMENTS=ON
        -DBUILD_TESTING=ON)

    for sdl_source in "$repo_root/build-ra/_deps/sdl2-src" "$repo_root/build/_deps/sdl2-src"; do
        if [ -f "$sdl_source/CMakeLists.txt" ]; then
            configure_args+=("-DFETCHCONTENT_SOURCE_DIR_SDL2=$sdl_source")
            break
        fi
    done

    run_step "Configure $(basename "$build_dir")" "${configure_args[@]}"
}

build_scope() {
    build_dir="$1"
    build_name="$2"
    shift 2
    if [ "$scope" = "full" ]; then
        run_step "Build $build_name" cmake --build "$build_dir" --parallel "$jobs"
    else
        run_step "Build $build_name" cmake --build "$build_dir" --parallel "$jobs" --target "$@"
    fi
}

test_scope() {
    build_dir="$1"
    test_name="$2"
    regex="$3"
    if [ "$scope" = "full" ]; then
        run_step "Test $test_name" ctest --test-dir "$build_dir" --output-on-failure
    elif [ "$regex" = '^$' ]; then
        echo "==> Test $test_name: no tests selected for scope $scope"
        record_result "Test $test_name" "PASS (not applicable)"
    else
        run_step "Test $test_name" ctest --test-dir "$build_dir" -R "$regex" --output-on-failure
    fi
}

cd "$repo_root" || exit 2

ra_configured=0
if configure_build "$ra_build_dir"; then
    ra_configured=1
fi

if [ "$ra_configured" -eq 1 ]; then
    build_scope "$ra_build_dir" "RA-enabled" "${ra_targets[@]}"
    test_scope "$ra_build_dir" "RA-enabled" "$test_regex"
fi

run_step "Branch whitespace check" git -C "$repo_root" \
    -c core.whitespace=cr-at-eol diff --check "$merge_base" --

if [ "$dry_run" -eq 1 ]; then
    echo
    echo "Dry run complete. No build, test, or report was written."
    exit 0
fi

{
    echo
    echo "## Result"
    echo
    if [ "$failures" -eq 0 ]; then
        echo "**PASS**"
    else
        echo "**FAIL** ($failures failed step(s))"
    fi
    echo
    echo "Completed: \`$(date '+%Y-%m-%d %H:%M:%S %Z')\`"
    echo
    echo "GUI and user-manual checks are not included. Continue with the cases selected from"
    echo "\`Documents/RetroAchievements/46_RA統一媒体動作確認運用手順.md\`."
} >> "$report_path"

echo
echo "Report: $report_path"
if [ "$failures" -ne 0 ]; then
    exit 1
fi
