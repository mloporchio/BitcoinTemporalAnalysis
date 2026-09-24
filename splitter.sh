#!/bin/bash
#
#   This script splits the original transaction list into chunks based on 6-month time intervals.
#   Chunks are created taking January 1st, 2009 as the starting point and December 31st, 2022 as the ending point.
#   The chunks are stored in the "chunks" directory.
#   
#   Run the script as follows:
#       ./splitter.sh <path_to_transaction_list>
#   where <path_to_transaction_list> is the path to the original Bitcoin transaction list file.
#

set -euo pipefail

INPUT=$1 # Path of the original transaction list file.
OUTPUT_DIR="chunks" # Name of the output directory where the chunks will be stored.

# Creates the output directory if it does not exist.
mkdir -p "$OUTPUT_DIR"

# ------------------------------------------------------------
# Generate chunk boundaries.
#
# Chunks:
#
#   01: 2009-01-01 <= t < 2009-07-01
#   02: 2009-07-01 <= t < 2010-01-01
#   03: 2010-01-01 <= t < 2010-07-01
#   ...
#   28: 2022-07-01 <= t < 2023-01-01
#
# ------------------------------------------------------------

BOUNDARIES_FILE=$(mktemp)
trap 'rm -f "$BOUNDARIES_FILE"' EXIT

YEAR=2009
MONTH=1
CHUNK=1

while (( YEAR < 2023 )); do

    # Six months after the current start date
    NEXT_MONTH=$((MONTH + 6))
    NEXT_YEAR=$YEAR

    if (( NEXT_MONTH > 12 )); then
        NEXT_MONTH=$((NEXT_MONTH - 12))
        NEXT_YEAR=$((NEXT_YEAR + 1))
    fi

    # Convert boundary date to Unix timestamp.
    BOUNDARY=$(date -d "$NEXT_YEAR-$(printf '%02d' "$NEXT_MONTH")-01 00:00:00" +%s)

    echo "$CHUNK $BOUNDARY" >> "$BOUNDARIES_FILE"

    YEAR=$NEXT_YEAR
    MONTH=$NEXT_MONTH
    CHUNK=$((CHUNK + 1))
done

NUM_CHUNKS=$((CHUNK - 1))

# Timestamp range
START_TS=$(date -d "2009-01-01 00:00:00" +%s)
END_TS=$(date -d "2023-01-01 00:00:00" +%s)

echo "Splitting $INPUT into $NUM_CHUNKS chunks..."

# ------------------------------------------------------------
# Process the input ONCE.
#
# The input is chronologically sorted, so current_chunk only
# moves forward.
# ------------------------------------------------------------

awk -F',' \
    -v boundaries_file="$BOUNDARIES_FILE" \
    -v output_dir="$OUTPUT_DIR" \
    -v start_ts="$START_TS" \
    -v end_ts="$END_TS" '

BEGIN {
    n = 0

    # Read chunk boundaries.
    while ((getline line < boundaries_file) > 0) {
        split(line, f, " ")

        chunk_number = f[1]
        boundary[++n] = f[2]

        output[n] = sprintf("%s/chunk_%02d.txt",
                            output_dir,
                            chunk_number)
    }

    close(boundaries_file)

    current = 1
}

{
    timestamp = $1

    # Skip transactions before 2009-01-01.
    if (timestamp < start_ts)
        next

    # Stop after 2022-12-31.
    if (timestamp >= end_ts)
        exit

    # Because transactions are sorted chronologically,
    # current can only increase.
    while (current < n && timestamp >= boundary[current]) {
        close(output[current])
        current++
    }

    print $0 >> output[current]
}

END {
    for (i = 1; i <= n; i++)
        close(output[i])
}
' "$INPUT"

echo "Done."
echo "Created $NUM_CHUNKS chunks in '$OUTPUT_DIR/'."