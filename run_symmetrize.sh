#!/bin/bash
#
#   This script symmetrizes the graphs built by run_builder.sh.
#   The symmetrized graphs are stored in a different directory.
#   
#   Author: Matteo Loporchio
#

set -euo pipefail

START_CHUNK=1
END_CHUNK=28
INPUT_DIR="graph"
OUTPUT_DIR="graph_s"
WG_EXEC="../webgraph-rs/target/release/webgraph"

mkdir -p $OUTPUT_DIR

# Compute the Elias-Fano representation.
# for ((i=START_CHUNK; i<=END_CHUNK; i++)); do
#     echo "Building Elias-Fano representation for Payment Graph $i..."
#     "$WG_EXEC" build ef "$INPUT_DIR/pg_$i"
# done

for ((i=START_CHUNK; i<=END_CHUNK; i++)); do
    echo "Symmetrizing Payment Graph $i..."
    "${WG_EXEC}" transform symmetrize "${INPUT_DIR}/pg_${i}" "${OUTPUT_DIR}/pg_${i}_s"
done