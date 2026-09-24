#!/bin/bash

FIRST_CHUNK=21
LAST_CHUNK=28
GRAPH_DIR="graph"
RESULTS_DIR="results_dist"
WG_EXEC="../webgraph-rs/target/release/webgraph" # Path to the WebGraph executable.

mkdir -p "${RESULTS_DIR}"

for ((i=FIRST_CHUNK; i<=LAST_CHUNK; i++)); do
    GRAPH_PREFIX="${GRAPH_DIR}/pg_${i}"
    if [ ! -f "${GRAPH_PREFIX}.graph" ]; then
        echo "Payment Graph ${i} does not exist. Please run run_builder.sh first."
        exit 1
    fi
    OUTPUT_PREFIX="${RESULTS_DIR}/pg_${i}"
    echo "Analyzing degree distribution for Payment Graph ${i}..."
    START_TIME=$(date +%s)
    "${WG_EXEC}" analyze stats ${GRAPH_PREFIX} ${OUTPUT_PREFIX}
    #rm "${OUTPUT_PREFIX}.indegree" "${OUTPUT_PREFIX}.outdegree"
    END_TIME=$(date +%s)
    echo "Done in $(($END_TIME - $START_TIME)) seconds."
done