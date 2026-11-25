
import pandas as pd

# Load CSV
df = pd.read_csv("summary.csv")

# Timing columns
timing_columns = [
    "cfo_correction_ms",
    "rrc_filter_ms",
    "synchronization_ms",
    "demodulation_ms",
    "parsing_ms"
]

# Calculate percentage of total processing time
for col in timing_columns:
    df[col.replace("_ms", "_pct")] = (
        df[col] / df["total_ms"] * 100
    )

# Print results
for _, row in df.iterrows():
    print(f"\nRun: {row['label']} | {row['variant']}")
    print("-" * 50)

    for col in timing_columns:
        pct_col = col.replace("_ms", "_pct")
        print(f"{col.replace('_ms', ''):25s}: {row[pct_col]:6.2f}%")

    total_pct = sum(row[col.replace("_ms", "_pct")] for col in timing_columns)
    print(f"{'Other / overhead':25s}: {100 - total_pct:6.2f}%")
    print(f"{'Total':25s}: {100.0:6.2f}%")
