#!/bin/bash

# 集成测试脚本 - C 文件服务器
# 自动启动服务端 → 运行客户端测试 → 关闭服务端

set -uo pipefail

SERVER="./bin/release/server"
CLIENT="./bin/release/client"
TEST_PORT=18888
TEST_ROOT="./test_storage"
TMPDIR="./test_tmp"
LOGFILE="./test_result.log"

PASS=0
FAIL=0

green() { printf '\033[32m%s\033[0m\n' "$1"; }
red() { printf '\033[31m%s\033[0m\n' "$1"; }

pass() { PASS=$((PASS + 1)); green "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); red "  FAIL: $1"; }

# 清理函数
cleanup() {
  set +e
  if [[ -n ${SERVER_PID:-} ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
  fi
  rm -rf "$TEST_ROOT" "$TMPDIR"
}
trap cleanup EXIT

# 启动服务端
start_server() {
  rm -rf "$TEST_ROOT"
  mkdir -p "$TEST_ROOT"
  "$SERVER" -p "$TEST_PORT" -r "$TEST_ROOT" > /dev/null 2>&1 &
  SERVER_PID=$!
  sleep 0.5
}

# 准备测试文件
prepare_files() {
  mkdir -p "$TMPDIR"
  dd if=/dev/urandom of="$TMPDIR/small.bin" bs=1K count=1 2>/dev/null
  dd if=/dev/urandom of="$TMPDIR/medium.bin" bs=1M count=2 2>/dev/null
  echo "Hello, World!" > "$TMPDIR/text.txt"
}

echo "========================================"
echo "  File Server Integration Test"
echo "========================================"
echo ""

start_server
prepare_files

# ===== 测试 1: mkdir =====
echo "[1/10] mkdir"
if "$CLIENT" -p "$TEST_PORT" mkdir testdir > /dev/null 2>&1; then
  if [ -d "$TEST_ROOT/testdir" ]; then
    pass "mkdir creates directory"
  else
    fail "mkdir directory not found"
  fi
else
  fail "mkdir command failed"
fi

# ===== 测试 2: upload single file =====
echo "[2/10] upload single file"
if "$CLIENT" -p "$TEST_PORT" upload "$TMPDIR/small.bin" small.bin > /dev/null 2>&1; then
  if [ -f "$TEST_ROOT/small.bin" ]; then
    if cmp -s "$TMPDIR/small.bin" "$TEST_ROOT/small.bin"; then
      pass "upload single file (content matches)"
    else
      fail "upload single file (content mismatch)"
    fi
  else
    fail "upload single file (not found)"
  fi
else
  fail "upload single file (command failed)"
fi

# ===== 测试 3: upload multiple files =====
echo "[3/10] upload multiple files"
if "$CLIENT" -p "$TEST_PORT" upload "$TMPDIR/text.txt" text.txt "$TMPDIR/small.bin" small2.bin > /dev/null 2>&1; then
  ok=1
  for f in text.txt small2.bin; do
    [ -f "$TEST_ROOT/$f" ] || ok=0
  done
  if [ "$ok" -eq 1 ]; then
    pass "upload multiple files"
  else
    fail "upload multiple files (not all found)"
  fi
else
  fail "upload multiple files (command failed)"
fi

# ===== 测试 4: download file =====
echo "[4/10] download file"
if "$CLIENT" -p "$TEST_PORT" download small.bin "$TMPDIR/downloaded.bin" > /dev/null 2>&1; then
  if cmp -s "$TMPDIR/small.bin" "$TMPDIR/downloaded.bin"; then
    pass "download file (content matches)"
  else
    fail "download file (content mismatch)"
  fi
else
  fail "download file (command failed)"
fi

# ===== 测试 5: list directory =====
echo "[5/10] list directory"
list_output=$("$CLIENT" -p "$TEST_PORT" list 2>/dev/null)
if echo "$list_output" | grep -q "small.bin" && echo "$list_output" | grep -q "text.txt"; then
  pass "list root directory"
else
  fail "list root directory (expected files not found)"
fi

# ===== 测试 6: list subdirectory =====
echo "[6/10] list subdirectory"
"$CLIENT" -p "$TEST_PORT" mkdir subdir > /dev/null 2>&1
"$CLIENT" -p "$TEST_PORT" upload "$TMPDIR/text.txt" subdir/nested.txt > /dev/null 2>&1
list_output=$("$CLIENT" -p "$TEST_PORT" list subdir 2>/dev/null)
if echo "$list_output" | grep -q "nested.txt"; then
  pass "list subdirectory"
else
  fail "list subdirectory (nested.txt not found)"
fi

# ===== 测试 7: rename file =====
echo "[7/10] rename file"
if "$CLIENT" -p "$TEST_PORT" rename text.txt renamed.txt > /dev/null 2>&1; then
  if [ -f "$TEST_ROOT/renamed.txt" ] && [ ! -f "$TEST_ROOT/text.txt" ]; then
    pass "rename file"
  else
    fail "rename file (expected file not found or old still exists)"
  fi
else
  fail "rename file (command failed)"
fi

# ===== 测试 8: delete file and directory =====
echo "[8/10] delete file and directory"
ok=1
"$CLIENT" -p "$TEST_PORT" delete small2.bin > /dev/null 2>&1 || ok=0
"$CLIENT" -p "$TEST_PORT" delete subdir/nested.txt > /dev/null 2>&1 || ok=0
"$CLIENT" -p "$TEST_PORT" delete subdir > /dev/null 2>&1 || ok=0
if [ "$ok" -eq 1 ] && [ ! -f "$TEST_ROOT/small2.bin" ] && [ ! -d "$TEST_ROOT/subdir" ]; then
  pass "delete file and directory"
else
  fail "delete file and directory (files still exist)"
fi

# ===== 测试 9: path traversal =====
echo "[9/10] path traversal protection"
result=$("$CLIENT" -p "$TEST_PORT" list "../.." 2>&1)
if echo "$result" | grep -qi "INVALID_PATH"; then
  pass "path traversal rejected"
else
  fail "path traversal not rejected (got: $result)"
fi

# ===== 测试 10: large file upload/download =====
echo "[10/10] large file transfer (2MB)"
if "$CLIENT" -p "$TEST_PORT" upload "$TMPDIR/medium.bin" medium.bin > /dev/null 2>&1; then
  if "$CLIENT" -p "$TEST_PORT" download medium.bin "$TMPDIR/medium_dl.bin" > /dev/null 2>&1; then
    if cmp -s "$TMPDIR/medium.bin" "$TMPDIR/medium_dl.bin"; then
      pass "large file transfer (2MB, content matches)"
    else
      fail "large file transfer (content mismatch)"
    fi
  else
    fail "large file transfer (download failed)"
  fi
else
  fail "large file transfer (upload failed)"
fi

# 统计结果
echo ""
echo "========================================"
echo "  Test Results: $PASS passed, $FAIL failed"
echo "========================================"

if [ "$FAIL" -eq 0 ]; then
  green "  All tests passed!"
  exit 0
else
  red "  Some tests failed!"
  exit 1
fi
