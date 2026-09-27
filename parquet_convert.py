"""

Author: Matteo Loporchio
"""

import polars as pl
import time
import sys

INPUT_FILE = sys.argv[1]
OUTPUT_FILE = sys.argv[2]

# Scan the TSV lazily (keeps it out of memory)
q = pl.scan_csv(INPUT_FILE, separator="\t", has_header=False)

# Sink directly to parquet
print(f"Sinking input dataframe {INPUT_FILE} to parquet: {OUTPUT_FILE}...")
start_time = time.time()
q.sink_parquet(OUTPUT_FILE)
end_time = time.time()
elapsed = end_time - start_time
print(f"Done in {elapsed:.3f} seconds.")
