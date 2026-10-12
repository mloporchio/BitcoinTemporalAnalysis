"""

Author: Matteo Loporchio
"""

import argparse
import time
import polars as pl


def main():
    parser = argparse.ArgumentParser(
        description="Convert a CSV/TSV file to Parquet using Polars."
    )

    parser.add_argument("input_file", help="Input CSV/TSV file")
    parser.add_argument("output_file", help="Output Parquet file")

    parser.add_argument(
        "--separator",
        default="\t",
        help=r"Field separator (default: tab, '\t')",
    )
    parser.add_argument(
        "--has_header",
        action="store_true",
        help="Specify this flag if the input file has a header",
    )

    args = parser.parse_args()

    # Scan lazily to avoid loading the entire input into memory.
    q = pl.scan_csv(
        args.input_file,
        separator=args.separator,
        has_header=args.has_header,
    )

    print(
        f"Sinking input dataframe {args.input_file} "
        f"to parquet: {args.output_file}..."
    )

    start_time = time.time()
    q.sink_parquet(args.output_file)
    elapsed = time.time() - start_time

    print(f"Done in {elapsed:.3f} seconds.")


if __name__ == "__main__":
    main()