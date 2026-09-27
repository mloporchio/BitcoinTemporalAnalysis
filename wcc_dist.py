"""

Author: Matteo Loporchio
"""

import polars as pl
import sys
import time

INPUT_FILE=sys.argv[1]
OUTPUT_FILE=sys.argv[2]

print(f"Computing WCC size distribution...")
start_time = time.time()
(
    pl.scan_parquet(INPUT_FILE)
        .rename({"column_1": "comp_id"})
        .group_by("comp_id").len(name="size")
        .group_by("size").len("num_comp")
        .sink_parquet(OUTPUT_FILE)
)
end_time = time.time()
elapsed = end_time - start_time
print(f"Done in {elapsed:.3f} seconds.")
