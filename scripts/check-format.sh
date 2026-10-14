#!/bin/bash
set -e

# Format checking script for all languages (C and Rust)
# Used by local development (make format-check)
# For CI pipeline, use check-format-c.sh and check-format-rust.sh separately
# Exit code 0 = all formatted correctly, 1 = formatting issues found

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================"
echo "  Code Format Checker (All Languages)"
echo "========================================"
echo ""

FAILED=0

# ============================================================================
# C Code Formatting Check (delegates to check-format-c.sh)
# ============================================================================
if ! bash "$SCRIPT_DIR/check-format-c.sh"; then
    FAILED=1
fi

# ============================================================================
# Rust Code Formatting Check (delegates to check-format-rust.sh)
# ============================================================================
if ! bash "$SCRIPT_DIR/check-format-rust.sh"; then
    FAILED=1
fi

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 1 ]; then
    echo "========================================"
    echo "  ✗ Format Check FAILED"
    echo "========================================"
    exit 1
else
    echo "========================================"
    echo "  ✓ Format Check PASSED"
    echo "========================================"
    exit 0
fi
