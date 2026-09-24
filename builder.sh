#!/bin/bash
#
#   This script builds the Bitcoin Payment Graph corresponding to the first N temporal chunks 
#   of the original transaction list.
#   It assumes that the chunks have already been created using the splitter.sh script.
#
#   The script:
#   1)  concatenates the first N chunks, creating a temporary cumulative transaction list;
#   2)  invokes the build_pg.sh script to generate the edge list, node mapping, and final WebGraph representation
#       of the first N chunks.
#
#   Run the script as follows:
#       ./builder.sh N
#   where N is a positive integer representing the number of chunks to concatenate and build the graph from.
#

set -euo pipefail

# ------------------------------------------------------------
# Check command line arguments
# ------------------------------------------------------------

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 N"
    echo "Example: $0 5"
    exit 1
fi

N="$1" # Number of chunks to concatenate and build the graph from.
BUILD_SCRIPT="./build_pg.sh"
CHUNK_DIR="chunks" # Directory where the chunks are stored.
GRAPH_DIR="graph" # Directory where the WebGraph representation will be stored.

if ! [[ "$N" =~ ^[0-9]+$ ]] || [ "$N" -lt 1 ]; then
    echo "Error: N must be a positive integer."
    exit 1
fi

if [ ! -x "$BUILD_SCRIPT" ]; then
    echo "Error: $BUILD_SCRIPT not found or not executable."
    exit 1
fi

# ------------------------------------------------------------
# Create chunk list
# ------------------------------------------------------------

CHUNK_LIST=()
for ((i = 1; i <= N; i++)); do
    CHUNK=$(printf "%s/chunk_%02d.txt" "$CHUNK_DIR" "$i")
    if [ ! -f "$CHUNK" ]; then
        echo "Error: chunk file not found: $CHUNK"
        exit 1
    fi
    CHUNK_LIST+=("$CHUNK")
done

# ------------------------------------------------------------
# Run the build script
# ------------------------------------------------------------

mkdir -p "$GRAPH_DIR"
OUTPUT_PREFIX="${GRAPH_DIR}/pg_${N}" # Prefix for the output files of the WebGraph representation of the first N chunks.

# Run the build script.
echo "Running $BUILD_SCRIPT..."
START_TIME=$(date +%s)
"$BUILD_SCRIPT" "$OUTPUT_PREFIX" "${CHUNK_LIST[@]}"
END_TIME=$(date +%s)
echo "Build completed successfully in $(($END_TIME - $START_TIME)) seconds."
