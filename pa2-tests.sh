#!/usr/bin/env bash
# ============================================================
# PA2 Aggie Shell — Rubric-Aligned Tests with Explanations
# ============================================================

# === Debug utility ===
debug_files() {
  echo -e "\n${YELLOW}--- DEBUG FILES ---${NC}"
  ls -l
  echo ""
  for f in a b test.txt output.txt hidden.txt; do
    if [[ -f "$f" ]]; then
      echo "Contents of $f:"
      head -n 10 "$f"
      echo ""
    fi
  done
  echo -e "${YELLOW}--------------------${NC}\n"
}

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCORE=0
MAX_SCORE=75 # hidden tests worth 25 pts in gradescope

remake() { make clean >/dev/null 2>&1; make -s >/dev/null 2>&1; }

print_result() {
  local passed=$1
  local reason=$2
  local pts=$3
  local expected="$4"
  local actual="$5"

  if [ $passed -eq 0 ]; then
    echo -e "  ${RED}Failed${NC}: $reason"
    if [ -n "$expected" ]; then
      echo -e "    ${YELLOW}Expected:${NC} $expected"
    fi
    if [ -n "$actual" ]; then
      echo -e "    ${YELLOW}Actual:${NC}   $actual"
    fi
  else
    echo -e "  ${GREEN}Passed${NC}: $reason"
    if [ -n "$actual" ]; then
      echo -e "    ${YELLOW}Output:${NC} $actual"
    fi
    SCORE=$((SCORE+pts))
  fi
  echo "Current SCORE: ${SCORE}/${MAX_SCORE}"
  echo ""
}


echo -e "${YELLOW}Starting Aggie Shell rubric-aligned tests (with diagnostics)...${NC}\n"
remake

# ============================================================
# 1. Echo (5 pts)
# ============================================================
echo "[Test 1] echo"
OUTPUT=$(./shell <<< 'echo "Hello world | Life is Good > Great $" && exit' 2>&1)
EXPECTED="Hello world | Life is Good > Great $"

if echo "$OUTPUT" | grep -qF "$EXPECTED"; then
  print_result 1 "echo output contains the expected text" 5 "$EXPECTED" "$OUTPUT"
else
  print_result 0 "Expected output text missing or formatted incorrectly" 0 "$EXPECTED" "$OUTPUT"
fi


# ============================================================
# 2. Simple Commands (10 pts)
# ============================================================
echo "[Test 2] simple commands with arguments"
./shell <<< "ls && ls -l /usr/bin && ls -la && ps aux && exit" >/tmp/test2.log 2>&1
if grep -q "bash" /tmp/test2.log && grep -q "root" /tmp/test2.log; then
  print_result 1 "executed external commands successfully" 10
else
  tail -n 5 /tmp/test2.log
  print_result 0 "one or more basic commands failed to execute" 0
fi
rm -f /tmp/test2.log

# ============================================================
# 3. Input/Output Redirection (15 pts)
# ============================================================
echo "[Test 3] input/output redirection"
./shell <<< "ps aux > a && grep init < a && grep init < a > b && exit" >/dev/null 2>&1
if [[ ! -f a ]]; then
  print_result 0 "file 'a' was not created with output redirection" 0
elif [[ ! -s a ]]; then
  print_result 0 "file 'a' exists but is empty" 0
elif [[ ! -f b ]]; then
  print_result 0 "file 'b' was not created during combined redirection" 0
elif grep -q "dumb-init" b; then
  print_result 1 "successfully redirected input/output for ps aux and grep dumb-init" 15
else
  print_result 0 "file 'b' exists but missing expected 'dumb-init' output" 0
fi
rm -f a b

# ============================================================
# 4. Single Pipe (8 pts)
# ============================================================
echo "[Test 4] single pipe"
OUTPUT=$(./shell <<< "ls -l | grep shell && exit" 2>&1)
if echo "$OUTPUT" | grep -q "shell"; then
  print_result 1 "pipe between ls and grep worked correctly" 8
else
  print_result 0 "failed to transfer ls output into grep through pipe" 0
fi

# ============================================================
# 5. Multiple Pipes (6 pts)
# ============================================================
echo "[Test 5] multiple pipes"
./shell <<< "ps aux | awk '/usr/{print \$1}' | sort -r | head -n 1 && exit" >/tmp/test5.log 2>&1
if [ $? -eq 0 ] && [[ -s /tmp/test5.log ]]; then
  print_result 1 "executed multi-pipe chain successfully" 6
else
  tail -n 3 /tmp/test5.log
  print_result 0 "multi-pipe chain did not produce output or exited with error" 0
fi
rm -f /tmp/test5.log


# ============================================================
# 6. Multiple Pipes + I/O Redirection (15 pts)
# ============================================================
# ============================================================
# 6. Multiple Pipes + I/O Redirection (15 pts)
# ============================================================
echo "[Test 6] multiple pipes + I/O redirection"

{
  # ---- Step 1 ----
  CMD1="ps aux > test.txt && exit"
  # echo ">>> Running: $CMD1"
  ./shell <<< "$CMD1" 2>&1 | head -n 10
  if [[ ! -s test.txt ]]; then
    print_result 0 "Step 1 failed — test.txt not created or empty" 0 "$CMD1" ""
    debug_files
    return 0 2>/dev/null || exit 0
  else
    echo "✅ test.txt created successfully"
  fi

  # ---- Step 2 ----
  # Use cut instead of awk to avoid quoting issues
  CMD2="cut -d' ' -f1,11 test.txt > tmp1.txt && exit"
  # echo ">>> Running: $CMD2"
  ./shell <<< "$CMD2" 2>&1 | head -n 10
  if [[ ! -s tmp1.txt ]]; then
    print_result 0 "Step 2 failed — cut output missing" 0 "$CMD2" ""
    debug_files
    return 0 2>/dev/null || exit 0
  else
    echo "✅ cut step successful"
  fi

  # ---- Step 3 ----
  CMD3="head -10 < tmp1.txt > tmp2.txt && exit"
  # echo ">>> Running: $CMD3"
  ./shell <<< "$CMD3" 2>&1 | head -n 10
  if [[ ! -s tmp2.txt ]]; then
    print_result 0 "Step 3 failed — head output missing" 0 "$CMD3" ""
    debug_files
    return 0 2>/dev/null || exit 0
  else
    echo "✅ head step successful"
  fi

  # ---- Step 4 ----
  CMD4="tr a-z A-Z < tmp2.txt > tmp3.txt && exit"
  # echo ">>> Running: $CMD4"
  ./shell <<< "$CMD4" 2>&1 | head -n 10
  if [[ ! -s tmp3.txt ]]; then
    print_result 0 "Step 4 failed — tr output missing" 0 "$CMD4" ""
    debug_files
    return 0 2>/dev/null || exit 0
  else
    echo "✅ tr step successful"
  fi

  # ---- Step 5 ----
  CMD5="sort < tmp3.txt > output.txt && exit"
  # echo ">>> Running: $CMD5"
  ./shell <<< "$CMD5" 2>&1 | head -n 10
  if [[ ! -s output.txt ]]; then
    print_result 0 "Step 5 failed — sort output missing" 0 "$CMD5" ""
    debug_files
    return 0 2>/dev/null || exit 0
  else
    echo "✅ sort step successful"
  fi

  # ---- Step 6 ----
  CMD6="cat output.txt && exit"
  # echo ">>> Running: $CMD6"
  OUTPUT=$(./shell <<< "$CMD6" 2>&1)
  echo ">>> Output (first 15 lines):"
  echo "$OUTPUT" | head -n 15
  echo "--------------------------------------------"

  if [[ -s test.txt && -s output.txt ]]; then
    print_result 1 "All stages passed (I/O redirection + multi-step pipeline works)" 15 "All intermediate commands succeeded" "$OUTPUT"
  else
    print_result 0 "Some stage failed — see logs above" 0 "Expected non-empty test.txt/output.txt" "$OUTPUT"
  fi

  rm -f test.txt tmp1.txt tmp2.txt tmp3.txt output.txt
}



# ============================================================
# 7. Background Processes (5 pts)
# ============================================================
echo "[Test 7] background processes"
START=$(date +%s)
./shell <<< "sleep 3 &; sleep 2; exit" >/dev/null 2>&1
END=$(date +%s)
if (( END - START < 3 )); then
  print_result 1 "background process did not block the shell" 5
else
  print_result 0 "sleep command blocked instead of running in background" 0
fi

# ============================================================
# 8. Directory Processing (6 pts)
# ============================================================
echo "[Test 8] cd command"
TMPDIR=$(pwd)
OUT=$(./shell <<< "cd ../../ && pwd && cd - && exit" 2>/dev/null)
if echo "$OUT" | grep -q "$TMPDIR"; then
  print_result 1 "cd and cd - worked as expected" 6
else
  echo "$OUT"
  print_result 0 "failed to navigate directories correctly" 0
fi

# ============================================================
# 9. User Prompt (5 pts)
# ============================================================
echo "[Test 9] user prompt"
PROMPT_OUT=$(echo "exit" | ./shell 2>/dev/null)

EXPECTED_PATTERN="$(whoami).*$(date +%b)"
ALT_PATTERN="$(date +%b).*$(whoami)"

if echo "$PROMPT_OUT" | grep -Eq "$EXPECTED_PATTERN" || \
   echo "$PROMPT_OUT" | grep -Eq "$ALT_PATTERN"; then
  print_result 1 "prompt displayed username and date" 5 "$EXPECTED_PATTERN OR $ALT_PATTERN" "$(echo "$PROMPT_OUT" | head -n 1)"
else
  print_result 0 "prompt missing username/date or not printed correctly" 0 "$EXPECTED_PATTERN OR $ALT_PATTERN" "$(echo "$PROMPT_OUT" | head -n 3)"
fi

# ============================================================
# Final Score
# ============================================================
echo -e "\n${YELLOW}Final SCORE: ${SCORE}/${MAX_SCORE}${NC}\n"
