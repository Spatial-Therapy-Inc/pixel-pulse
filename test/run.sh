#!/usr/bin/env bash
# Host tests for Pixel Pulse's rules, pixel art and drawing (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -I../main -o "$OUT/test_pet" test_pet.c ../main/pet.c ../main/sprites.c ../main/render.c -lm
"$OUT/test_pet"
rm -rf "$OUT"
