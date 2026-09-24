#!/bin/bash
#
#   This script fits a power law function to the in- and out-degree distributions of each graph.
#   The script requires the plfit tool to be installed (see: https://github.com/ntamas/plfit).
#
#   Author: Matteo Loporchio
#

set -euo pipefail

# compute_comp_size() {
#     python3 - <<END
# import polars as pl
# df = pl.read_csv('${1}/${2}.tsv', separator="\t")
# data = df['${2}_id'].value_counts(name="comp_size")
# data.select("comp_size").write_csv('${3}', include_header=False)
# END
# }

START_CHUNK=1
END_CHUNK=28
METRICS=("indegree" "outdegree")
PLFIT_EXEC="~/plfit-1.0.1/build/src/plfit"
TEMP_DIR="tmp"
RESULT_DIR="results"

mkdir -p $RESULT_DIR $TEMP_DIR

for METRIC in "${METRICS[@]}"; do
    # Prepare the output file for the current metric.
    OUTPUT_FILE="${RESULT_DIR}/fit_${METRIC}.tsv"
    printf "chunk_id\talpha\tx_min\tL\tD\tp_value\telapsed_time\n" > $OUTPUT_FILE
    # Determine the appropriate input file extension based on the metric.
    if [ "$METRIC" == "indegree" ]; then
        EXTENSION="indegrees"
    elif [ "$METRIC" == "outdegree" ]; then
        EXTENSION="outdegrees"
    else
        echo "Error: unsupported metric $METRIC."
        rm -rf $TEMP_DIR
        exit 1
    fi
    # Loop through each graph chunk and fit the power law distribution.
    for ((i=START_CHUNK; i<=END_CHUNK; i++)); do
        FILENAME="results/pg_${i}.${EXTENSION}"
        # Check if the file exists before proceeding.
        if [ ! -f "$FILENAME" ]; then
            echo "Error: file $FILENAME does not exist. Please run run_degree.sh first."
            rm -rf $TEMP_DIR
            exit 1
        fi
        # Fit the power law distribution using plfit.
        echo "Fitting power law for graph $i and metric $METRIC..."
        START_TIME=$EPOCHSECONDS
        if PLFIT_OUT=$((eval ${PLFIT_EXEC} -p approximate -b ${FILENAME}) 2>/dev/null); then
            echo "Done in $((EPOCHSECONDS - START_TIME)) seconds."
            echo "Result: ${PLFIT_OUT}"
            # Fitted exponent, minimum X value, log-likelihood (L), Kolmogorov-Smirnov statistic (D) and p-value (p)
            ELAPSED_TIME=$((EPOCHSECONDS - START_TIME))
            ALPHA=$(echo $PLFIT_OUT | cut -d' ' -f3)
            X_MIN=$(echo $PLFIT_OUT | cut -d' ' -f4)
            LL=$(echo $PLFIT_OUT | cut -d' ' -f5)
            KS=$(echo $PLFIT_OUT | cut -d' ' -f6)
            P_VALUE=$(echo $PLFIT_OUT | cut -d' ' -f7)
            printf "%d\t%s\t%s\t%s\t%s\t%s\t%s\n" "$i" "$ALPHA" "$X_MIN" "$LL" "$KS" "$P_VALUE" "$ELAPSED_TIME" >> $OUTPUT_FILE
        else
            echo "Error: fitting failure for graph $i and metric $METRIC."
            rm -rf $TEMP_DIR
            exit 1
        fi
    done
done

exit 0
