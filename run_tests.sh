#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0

pass() { echo -e "${GREEN}[PASS]${NC} $1"; ((PASS++)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; ((FAIL++)); }

# -----------------------------------------------------------------------
# Sanity checks
# -----------------------------------------------------------------------

if [ ! -f "./generate" ]; then
    echo -e "${RED}Error: ./generate not found. Run 'make' first.${NC}"
    exit 1
fi

if ! command -v gcc &> /dev/null; then
    echo -e "${RED}Error: gcc not found. Required to compile rexec.c.${NC}"
    exit 1
fi

mkdir -p failed_tests

# -----------------------------------------------------------------------
# PHASE 1 -- Semantic accept/reject tests (same as before)
# -----------------------------------------------------------------------

echo "=============================="
echo "  PHASE 1: SEMANTIC CHECKS    "
echo "=============================="

echo ""
echo "--- Should ACCEPT ---"
for file in tests/accepts/*.txt; do
    [ -f "$file" ] || continue
    ./generate "$file" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        pass "$file"
    else
        fail "$file (expected semantic accept, but failed)"
    fi
done

echo ""
echo "--- Should FAIL ---"
for file in tests/fails/*.txt; do
    [ -f "$file" ] || continue
    OUTPUT=$(./generate "$file" 2>&1)
    EXIT_CODE=$?

    if [ $EXIT_CODE -eq 0 ]; then
        fail "$file (expected semantic failure, but accepted)"
        continue
    fi

    EXPECTED_ERR=""
    case "$file" in
        *Sem_HiddenPoison.txt|*unicodeOutofBounds.txt)
            EXPECTED_ERR="out of bounds" ;;
        *Sem_Redefine.txt|*duplicateConstant.txt)
            EXPECTED_ERR="already defined" ;;
        *Sem_PhantomSub.txt|*unboundVariable.txt)
            EXPECTED_ERR="Unbound variable" ;;
        *)
            EXPECTED_ERR="Syntax Error" ;;
    esac

    if echo "$OUTPUT" | grep -q "$EXPECTED_ERR"; then
        pass "$file (caught: $EXPECTED_ERR)"
    else
        fail "$file"
        echo "       Expected error containing: '$EXPECTED_ERR'"
        echo "       Actual output: $OUTPUT"
    fi
done

# -----------------------------------------------------------------------
# PHASE 2 -- Code generation + rexec tests
#
# Directory structure expected:
#
#   tests/accepts/myregex.txt        <- the regex source file
#   tests/accepts/myregex/           <- test inputs for that regex
#     accept/test1.txt               <- rexec should print ACCEPTS
#     accept/test2.txt
#     reject/test1.txt               <- rexec should print REJECTS
#     reject/test2.txt
#
# For each regex file that has a matching subdirectory, the script will:
#   1. Run ./semantics on it to generate output/rexec.c
#   2. Compile output/rexec.c -> output/rexec
#   3. Run output/rexec on each accept/* and reject/* input file
# -----------------------------------------------------------------------

echo ""
echo "=============================="
echo "  PHASE 2: GENERATED REXEC    "
echo "=============================="

for regex_file in tests/accepts/*.txt; do
    [ -f "$regex_file" ] || continue

    # Derive the test input directory from the regex filename
    # e.g. tests/accepts/myregex.txt -> tests/accepts/myregex/
    base=$(basename "$regex_file" .txt)
    test_dir="tests/accepts/${base}"

    # Skip if no test input directory exists for this regex
    if [ ! -d "$test_dir" ]; then
        continue
    fi

    echo ""
    echo "--- Regex: $regex_file ---"

    # rexec.c is written to the same directory as the regex source file
    regex_dir=$(dirname "$regex_file")
    rexec_c="${regex_dir}/rexec.c"
    rexec_bin="${regex_dir}/rexec"

    # Step 1: generate rexec.c
    ./generate "$regex_file" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        fail "$regex_file (semantic check failed during codegen phase)"
        continue
    fi

    if [ ! -f "$rexec_c" ]; then
        fail "$regex_file ($rexec_c was not created)"
        continue
    fi

    # Step 2: compile rexec.c
    gcc -o "$rexec_bin" "$rexec_c" 2>/dev/null
    if [ $? -ne 0 ]; then
        fail "$regex_file (failed to compile $rexec_c)"
        continue
    fi

    # Step 3a: run accept inputs -- expect ACCEPTS
    regex_failed=0
    if [ -d "$test_dir/accept" ]; then
        for input_file in "$test_dir/accept"/*.txt; do
            [ -f "$input_file" ] || continue
            OUTPUT=$("$rexec_bin" "$input_file" 2>&1)
            if echo "$OUTPUT" | grep -q "^ACCEPTS$"; then
                pass "$input_file (ACCEPTS as expected)"
            else
                fail "$input_file (expected ACCEPTS, got: $OUTPUT)"
                regex_failed=1
            fi
        done
    fi

    # Step 3b: run reject inputs -- expect REJECTS
    if [ -d "$test_dir/reject" ]; then
        for input_file in "$test_dir/reject"/*.txt; do
            [ -f "$input_file" ] || continue
            OUTPUT=$("$rexec_bin" "$input_file" 2>&1)
            if echo "$OUTPUT" | grep -q "^REJECTS$"; then
                pass "$input_file (REJECTS as expected)"
            else
                fail "$input_file (expected REJECTS, got: $OUTPUT)"
                regex_failed=1
            fi
        done
    fi

    # If any test for this regex failed, save the generated C file for inspection
    if [ $regex_failed -eq 1 ]; then
        mv "$rexec_c" "failed_tests/${base}_rexec.c"
    fi

done

# -----------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------

echo ""
echo "=============================="
TOTAL=$((PASS + FAIL))
echo "  RESULTS: $PASS/$TOTAL passed"
if [ $FAIL -gt 0 ]; then
    echo -e "  ${RED}$FAIL test(s) failed${NC}"
else
    echo -e "  ${GREEN}All tests passed!${NC}"
fi
echo "=============================="
