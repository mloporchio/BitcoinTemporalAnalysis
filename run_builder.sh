#!/bin/bash

set -euo pipefail

FIRST_CHUNK=1
LAST_CHUNK=28

for ((i=FIRST_CHUNK; i<=LAST_CHUNK; i++)); do
    echo "Building Payment Graph for the first $i chunks..."
    ./builder.sh "$i"
done