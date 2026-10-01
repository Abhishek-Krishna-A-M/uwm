#!/bin/bash
set -e
echo "=== UWM comprehensive test suite ==="
echo "Low latency, high performance, smooth under pressure"
echo

PASS=0
FAIL=0
for f in tests/test_*.py; do
  echo "--- $f ---"
  if python3 "$f"; then
    PASS=$((PASS+1))
  else
    FAIL=$((FAIL+1))
    echo "FAIL in $f"
  fi
  echo
done

echo "=== Build verification ==="
echo "uwm..."
make -j$(nproc) > /tmp/build_uwm.log 2>&1 && echo "✓ uwm builds" && PASS=$((PASS+1)) || { FAIL=$((FAIL+1)); cat /tmp/build_uwm.log; echo "✗ uwm build failed"; }
echo "ulaunch..."
make -C tools/ulaunch > /tmp/build_ulaunch.log 2>&1 && echo "✓ ulaunch builds" && PASS=$((PASS+1)) || { FAIL=$((FAIL+1)); cat /tmp/build_ulaunch.log; echo "✗ ulaunch build failed"; }
echo "ubar..."
make -C tools/ubar > /tmp/build_ubar.log 2>&1 && echo "✓ ubar builds" && PASS=$((PASS+1)) || { FAIL=$((FAIL+1)); cat /tmp/build_ubar.log; echo "✗ ubar build failed"; }

echo
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ $FAIL -ne 0 ]; then
  exit 1
fi
echo "All high-performance checks passed!"
