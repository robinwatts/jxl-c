#!/usr/bin/env bash
# Pinned single-threaded C vs Rust decode benchmark.
#
# Rebuilds both harnesses, then for each fixture runs C and Rust back-to-back
# (sync between runs) with the same iteration count and reports medians.
#
# Usage:
#   scripts/bench_st_compare.sh [options]
#
# Options:
#   --iters N       Iterations per timed run (default: 30)
#   --rounds N      Back-to-back C/Rust pairs per fixture (default: 3)
#   --no-rebuild    Skip configure/build (use existing binaries)
#   --fixtures LIST Space-separated fixture basenames (default: all benchmark-data)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUST_ROOT="$("$ROOT/scripts/rust_root.sh")"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$RUST_ROOT/target}"
C_BUILD="${JXL_BENCH_C_BUILD:-$ROOT/build-bench-st}"
RUST_BIN="${JXL_BENCH_RUST_BIN:-$CARGO_TARGET_DIR/release/bench_decode_st}"
FIXTURES_DIR="$RUST_ROOT/crates/jxl-oxide-tests/decode/benchmark-data"
ITERS=30
ROUNDS=3
REBUILD=1
FIXTURES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iters)
            ITERS="$2"
            shift 2
            ;;
        --rounds)
            ROUNDS="$2"
            shift 2
            ;;
        --no-rebuild)
            REBUILD=0
            shift
            ;;
        --fixtures)
            read -r -a FIXTURES <<<"$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,12p' "$0"
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [[ ${#FIXTURES[@]} -eq 0 ]]; then
    mapfile -t FIXTURES < <(find "$FIXTURES_DIR" -maxdepth 1 -name '*.jxl' -printf '%f\n' | sed 's/\.jxl$//' | sort)
fi

median() {
    awk '
        { a[NR] = $1 }
        END {
            if (NR == 0) { print "nan"; exit }
            asort(a)
            if (NR % 2) print a[(NR + 1) / 2]
            else print (a[NR / 2] + a[NR / 2 + 1]) / 2
        }'
}

try_quiet_cpu() {
    if [[ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        local prev
        prev="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
        echo "$prev" > /tmp/jxl-bench-governor-prev 2>/dev/null || true
        echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
    fi
}

restore_cpu() {
    if [[ -f /tmp/jxl-bench-governor-prev ]] &&
        [[ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        cat /tmp/jxl-bench-governor-prev > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true
        rm -f /tmp/jxl-bench-governor-prev
    fi
}

parse_mpix() {
    sed -n 's/.*, \([0-9.]*\) Mpix\/s/\1/p'
}

build_all() {
    echo "==> Configure/build C (Release) -> $C_BUILD"
    cmake -S "$ROOT" -B "$C_BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DJXL_OXIDE_C_BUILD_TOOLS=ON \
        -DJXL_OXIDE_C_BUILD_TESTS=OFF >/dev/null
    cmake --build "$C_BUILD" --target bench_decode -j"$(nproc)" >/dev/null

    echo "==> Build Rust bench_decode_st (release, no default features) -> $CARGO_TARGET_DIR"
    cargo build --manifest-path "$RUST_ROOT/Cargo.toml" -p jxl-oxide-tests --release --no-default-features --bin bench_decode_st >/dev/null
    if [[ ! -x "$RUST_BIN" ]]; then
        resolved="$(cargo build --manifest-path "$RUST_ROOT/Cargo.toml" -p jxl-oxide-tests --release --no-default-features --bin bench_decode_st \
            --message-format=json 2>/dev/null |
            awk -F'"' '/"executable":/ { exe=$4 } END { print exe }')"
        if [[ -n "$resolved" && -x "$resolved" ]]; then
            RUST_BIN="$resolved"
        fi
    fi
}

run_c() {
    local fixture="$1"
    sync
    "$C_BUILD/bench_decode" "$FIXTURES_DIR/$fixture.jxl" "$ITERS" 2>&1
}

run_rust() {
    local fixture="$1"
    sync
    "$RUST_BIN" "$FIXTURES_DIR/$fixture.jxl" "$ITERS" 2>&1
}

trap restore_cpu EXIT
try_quiet_cpu

if [[ "$REBUILD" -eq 1 ]]; then
    build_all
fi

if [[ ! -x "$C_BUILD/bench_decode" ]]; then
    echo "missing $C_BUILD/bench_decode (run without --no-rebuild)" >&2
    exit 1
fi
if [[ ! -x "$RUST_BIN" ]]; then
    echo "missing $RUST_BIN (run without --no-rebuild)" >&2
    exit 1
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="$ROOT/build-bench-st/results-$STAMP.txt"
mkdir -p "$(dirname "$OUT")"

{
    echo "jxl-oxide ST decode benchmark"
    echo "timestamp: $STAMP"
    echo "iters: $ITERS  rounds: $ROUNDS"
    echo "c_binary: $C_BUILD/bench_decode"
    echo "rust_binary: $RUST_BIN"
    echo
    printf "%-32s %10s %10s %10s %10s %8s\n" "fixture" "C med" "Rust med" "C/Rust" "C rounds" "Rust rnd"
} | tee "$OUT"

for fixture in "${FIXTURES[@]}"; do
    c_vals=()
    r_vals=()
  for ((round = 1; round <= ROUNDS; ++round)); do
        c_out="$(run_c "$fixture")"
        if ! echo "$c_out" | grep -q 'Mpix/s'; then
            echo "$fixture: C failed (round $round)" >&2
            echo "$c_out" >&2
            exit 1
        fi
        c_vals+=("$(echo "$c_out" | parse_mpix)")

        r_out="$(run_rust "$fixture")"
        if ! echo "$r_out" | grep -q 'Mpix/s'; then
            echo "$fixture: Rust failed (round $round)" >&2
            echo "$r_out" >&2
            exit 1
        fi
        r_vals+=("$(echo "$r_out" | parse_mpix)")

        {
            echo "[$fixture round $round]"
            echo "$c_out"
            echo "$r_out"
        } >>"$OUT"
    done

    c_med="$(printf '%s\n' "${c_vals[@]}" | median)"
    r_med="$(printf '%s\n' "${r_vals[@]}" | median)"
    ratio="$(awk -v c="$c_med" -v r="$r_med" 'BEGIN { if (r > 0) printf "%.2f", c / r; else print "nan" }')"
    c_rounds="$(printf '%s ' "${c_vals[@]}")"
    r_rounds="$(printf '%s ' "${r_vals[@]}")"

    printf "%-32s %10.2f %10.2f %10s %s / %s\n" \
        "$fixture" "$c_med" "$r_med" "${ratio}x" "$c_rounds" "$r_rounds" | tee -a "$OUT"
done

echo | tee -a "$OUT"
echo "full log: $OUT" | tee -a "$OUT"
