#!/bin/bash
#
#   This script computes the weakly connected components of the graphs built by run_builder.sh.
#   The results are stored in a different directory.
#   
#   Author: Matteo Loporchio
#

set -euo pipefail

START_CHUNK=1
END_CHUNK=28
INPUT_DIR="graph"
TMP_DIR="tmp"
OUTPUT_DIR="results"
WG_WCC_EXEC="./target/release/wg_wcc"
WCC_DIST_SCRIPT="wcc_dist.py"
PARQUET_CONVERT_SCRIPT="parquet_convert.py"

mkdir -p $TMP_DIR $OUTPUT_DIR

for ((i=START_CHUNK; i<=END_CHUNK; i++)); do
    echo "Computing the weakly connected components for Payment Graph $i..."
    TEMP_FILE="${TMP_DIR}/pg_wcc_${i}.txt"
    "${WG_WCC_EXEC}" "${INPUT_DIR}/pg_${i}" "${TEMP_FILE}"
    echo "Converting results to Parquet format..."
    OUTPUT_FILE="${OUTPUT_DIR}/pg_wcc_${i}.parquet"
    python3 $PARQUET_CONVERT_SCRIPT $TEMP_FILE $OUTPUT_FILE
    # Throw away the text file. 
    rm $TEMP_FILE
    # Compute the WCC size distribution for the graph.
    echo "Computing the WCC size distribution..."
    DIST_FILE="${OUTPUT_DIR}/pg_wcc_dist_${i}.parquet"
    python3 "${WCC_DIST_SCRIPT}" "${OUTPUT_FILE}" "${DIST_FILE}"
done