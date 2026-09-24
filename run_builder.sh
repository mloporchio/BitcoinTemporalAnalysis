#!/bin/bash
#
#   This script builds the Payment Graphs for all chunks of the dataset.
#
#   For each chunk number i, it invokes the builder.sh script to construct the corresponding graph.
#   The i-th graph is built using the first i chunks of the dataset, 
#   and is stored (in WebGraph format) in the "graph" directory, with the "pg_i" prefix.
#
#   Author: Matteo Loporchio
#

set -euo pipefail

FIRST_CHUNK=1
LAST_CHUNK=28

for ((i=FIRST_CHUNK; i<=LAST_CHUNK; i++)); do
    echo "Building Payment Graph for the first $i chunks..."
    ./builder.sh "$i"
done