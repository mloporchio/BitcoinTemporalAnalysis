#!/bin/bash
#
#   This script builds the Bitcoin Payment Graph starting from multiple transaction lists.
#   Author: Matteo Loporchio
#

set -euo pipefail

OUTPUT_PREFIX=$1 # Prefix for the output files of the WebGraph representation.
shift 1
INPUT_FILES=("$@") # Paths of the transaction list files.

BIN_DIR="bin" # Directory where the binary executables are stored.
LOG_DIR="logs" # Directory where logs will be stored.
TMP_DIR="tmp" # Temporary directory for intermediate files.
EL_TMP_FILE="${TMP_DIR}/el_tmp.bin"
EL_FINAL_FILE="${TMP_DIR}/el_final.bin"
EL_BUILDER="${BIN_DIR}/pg_el_builder"
EL_BUILDER_LOG="${LOG_DIR}/pg_el_builder.log"
EL_BUILDER_ERR="${LOG_DIR}/pg_el_builder.err"
EL_SORTER="${BIN_DIR}/edge_sorter"
MEMORY_LIMIT="150000" # Memory limit for the edge sorter.
WG_BUILDER="./target/release/wg_native"
WG_BUILDER_LOG="${LOG_DIR}/webgraph_builder.log"
WG_BUILDER_ERR="${LOG_DIR}/webgraph_builder.err"

# Create the output directories if they do not exist.
mkdir -p ${LOG_DIR} ${TMP_DIR}

# Parse the input files and create the temporary edge list.
echo "Building the edge list..."
START_TIME=$(date +%s)
"${EL_BUILDER}" "${INPUT_FILES[@]}" "${EL_TMP_FILE}" 1>${EL_BUILDER_LOG} 2>${EL_BUILDER_ERR}
NUM_NODES=$(grep -e "Nodes:" ${EL_BUILDER_LOG} | awk '{print $2}')
END_TIME=$(date +%s)
echo "Number of nodes: ${NUM_NODES}"
echo "Done in $(($END_TIME - $START_TIME)) seconds."

# Sort the temporary edge list and store the result in the final edge list file.
echo "Sorting the edge list and removing duplicates..."
START_TIME=$(date +%s)
${EL_SORTER} ${EL_TMP_FILE} ${EL_FINAL_FILE} -m ${MEMORY_LIMIT}
END_TIME=$(date +%s)
echo "Done in $(($END_TIME - $START_TIME)) seconds."

# Remove the temporary edge list.
rm -f ${EL_TMP_FILE}

# Transform the TSV edge list into the WebGraph representation.
echo "Building the WebGraph representation..."
START_TIME=$(date +%s)
${WG_BUILDER} ${EL_FINAL_FILE} ${OUTPUT_PREFIX} ${NUM_NODES} 1>${WG_BUILDER_LOG} 2>${WG_BUILDER_ERR}
END_TIME=$(date +%s)
echo "Done in $(($END_TIME - $START_TIME)) seconds."