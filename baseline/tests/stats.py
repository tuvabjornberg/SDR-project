import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

EXPECTED_PREAMBLE = 0x6D5A96A5
EXPECTED_PAYLOAD_SIZE = 32  # bytes
MAX_PAYLOAD_SIZE = 512      # parser limit ("Expected <= 512")


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------
def parse_int(value):
    """Parse '0x6d5ad6b5' as hex and '4071' as decimal (as the parser logs them)."""
    s = str(value).strip()
    if s.lower().startswith("0x"):
        return int(s, 16)

    if any(c in "abcdefABCDEF" for c in s):
        return int(s, 16)

    return int(s, 10)

def hamming_distance(a, b):
    return bin(a ^ b).count("1")


# ----------------------------------------------------------------------
# Per-packet plots (ind_packets.csv)
# ----------------------------------------------------------------------
def plot_individual_packets(csv_path="tests/outlog/ind_packets.csv"):
    df = pd.read_csv(csv_path)
    received = df[df["received"] == 1]
    lost = df[df["received"] == 0]

    # Bit errors of the packets that were decoded
    plt.figure(figsize=(10, 4))
    plt.scatter(received["sequence"], received["bit_errors"])
    plt.xlabel("Sequence")
    plt.ylabel("Bit Errors")
    plt.ylim(-0.5, max(1, received["bit_errors"].max()) + 0.5)
    plt.title(f"Bit Errors per Decoded Packet "
              f"({len(received)} decoded, {int(received['bit_errors'].sum())} bit errors total)")
    plt.grid(True)
    plt.tight_layout()

    # Reception timeline
    print(len(received["sequence"]))
    print(len(lost["sequence"]))

    plt.figure(figsize=(12, 4))
    plt.scatter(received["sequence"], [1] * len(received), color="green", label="Received")
    plt.scatter(lost["sequence"], [0] * len(lost), color="red", alpha=0.6, label="Lost")
    plt.yticks([0, 1], ["Lost", "Received"])
    plt.xlabel("Packet Sequence")
    plt.title("Packet Reception Timeline")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()

    # Sliding PDR
    window = 20
    rolling_pdr = df["received"].rolling(window).mean()
    plt.figure(figsize=(12, 5))
    plt.plot(df["sequence"], rolling_pdr, linewidth=2)
    plt.xlabel("Sequence")
    plt.ylabel("PDR")
    plt.title(f"Sliding PDR (Window={window})")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.tight_layout()

    # Reception heatmap (pad with NaN so padding isn't drawn as "lost")
    status = df["received"].values.astype(float)
    width = 20
    padding = (width - len(status) % width) % width
    heatmap = np.pad(status, (0, padding), constant_values=np.nan).reshape(-1, width)
    plt.figure(figsize=(10, 4))
    plt.imshow(heatmap, cmap="RdYlGn", aspect="auto", vmin=0, vmax=1)
    plt.colorbar(label="Received")
    plt.title("Packet Reception Heatmap (row = block of 20 packets)")
    plt.xlabel("Packet Index within Block")
    plt.ylabel("Block")
    plt.tight_layout()

    # Loss burst lengths (losses are clustered, not uniform)
    runs, run = [], 0
    for r in df["received"].astype(int):
        if r == 0:
            run += 1
        elif run:
            runs.append(run)
            run = 0
    if run:
        runs.append(run)
    if runs:
        counts = pd.Series(runs).value_counts().sort_index()
        fig, ax = plt.subplots(figsize=(8, 4.5))
        bars = ax.bar(counts.index, counts.values, edgecolor="black")
        ax.bar_label(bars, padding=2)
        ax.set_xlabel("Consecutive packets lost")
        ax.set_ylabel("Number of bursts")
        ax.set_title("Loss Burst Lengths")
        ax.grid(axis="y", alpha=0.3)
        fig.tight_layout()

    # Latency (decoded packets only). NOTE: this is tx_time -> decode time and
    # includes RX backlog, so it is seconds-scale and drifts with sequence.
    latency_df = df[(df["received"] == 1) & (df["latency_ms"] >= 0)]
    if not latency_df.empty:
        plt.figure(figsize=(12, 5))
        plt.plot(latency_df["sequence"], latency_df["latency_ms"], marker="o")
        plt.xlabel("Sequence")
        plt.ylabel("Latency (ms)")
        plt.title("TX -> Decode Time (decoded packets only)")
        plt.grid(True)
        plt.tight_layout()

        plt.figure(figsize=(8, 5))
        plt.hist(latency_df["latency_ms"], bins=20, edgecolor="black")
        plt.xlabel("Latency (ms)")
        plt.ylabel("Count")
        plt.title("Latency Distribution")
        plt.grid(True)
        plt.tight_layout()

        latency = np.sort(latency_df["latency_ms"])
        cdf = np.arange(1, len(latency) + 1) / len(latency)
        plt.figure(figsize=(8, 5))
        plt.plot(latency, cdf)
        plt.xlabel("Latency (ms)")
        plt.ylabel("CDF")
        plt.title("Latency CDF")
        plt.grid(True)
        plt.tight_layout()

    plt.show()


# ----------------------------------------------------------------------
# Summary plots (tot_packets.csv)
# ----------------------------------------------------------------------
def plot_summary_metrics(csv_path="tests/outlog/tot_packets.csv"):
    df = pd.read_csv(csv_path)
    # The CSV has an empty column header -> "Unnamed: N"; drop it.
    df = df.loc[:, ~df.columns.astype(str).str.startswith("Unnamed")]
    row = df.iloc[0]

    tx = float(row["packets_tx"])
    detected = float(row["packets_detected"])
    decoded = float(row["packets_decoded"])
    parser_failures = detected - decoded
    missed = tx - detected

    # Compute from counts (the CSV pdr/per are rounded to 2 decimals)
    pdr = 100 * decoded / tx
    per = 100 - pdr
    detect_rate = 100 * detected / tx
    decode_given_detect = 100 * decoded / detected if detected else 0.0
    ber = float(row["ber"])

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    # Left: PDR / PER / detection / decode-given-detection (percent)
    labels = ["PDR", "PER", "Preamble\ndetection", "Decode | detected"]
    values = [pdr, per, detect_rate, decode_given_detect]
    bars = axes[0].bar(labels, values, color=["green", "red", "steelblue", "darkorange"])
    axes[0].bar_label(bars, labels=[f"{v:.1f}%" for v in values], padding=2)
    axes[0].set_ylim(0, 110)
    axes[0].set_ylabel("Percent")
    axes[0].set_title(f"Link Performance (BER of decoded packets = {ber:g})")
    axes[0].grid(axis="y", alpha=0.3)

    # Right: where packets are lost in the pipeline
    stages = ["TX", "Detected", "Decoded", "Parser\nfailures", "Not\ndetected"]
    counts = [tx, detected, decoded, parser_failures, missed]
    colors = ["gray", "steelblue", "green", "crimson", "black"]
    bars = axes[1].bar(stages, counts, color=colors)
    axes[1].bar_label(bars, labels=[str(int(c)) for c in counts], padding=2)
    axes[1].set_ylabel("Packets")
    axes[1].set_title("Packet Processing Pipeline")
    axes[1].grid(axis="y", alpha=0.3)

    fig.tight_layout()

    print(f"TX {tx:.0f} | detected {detected:.0f} ({detect_rate:.1f}%) | "
          f"decoded {decoded:.0f} ({pdr:.1f}%) | parser failures {parser_failures:.0f}")
    plt.show()


def plot_packet_comparison(csv_path="tests/outlog/tot_packets.csv"):
    """Bar chart comparing transmitted, detected, decoded and lost packets."""
    df = pd.read_csv(csv_path)
    df = df.loc[:, ~df.columns.astype(str).str.startswith("Unnamed")]
    row = df.iloc[0]

    tx = int(row["packets_tx"])
    detected = int(row["packets_detected"])
    decoded = int(row["packets_decoded"])
    lost = int(row["packets_lost"])  # = transmitted - decoded

    labels = ["Transmitted", "Detected", "Decoded", "Lost"]
    counts = [tx, detected, decoded, lost]
    colors = ["gray", "steelblue", "green", "crimson"]

    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar(labels, counts, color=colors, edgecolor="black")
    ax.bar_label(
        bars,
        labels=[f"{c}\n({100 * c / tx:.1f}%)" for c in counts],
        padding=3,
    )
    ax.set_ylabel("Packets")
    ax.set_ylim(0, tx * 1.18)
    ax.set_title("Packets Transmitted vs Detected vs Decoded vs Lost")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    plt.show()


# ----------------------------------------------------------------------
# Parser failures (invalid_packets.csv)
#
# NOTE: the "Sequence Number" in this file is parsed from a frame whose
# header is already corrupted/misaligned (values like 65535, 16377, ...),
# so it is NOT the real packet index. We plot against event order instead.
# ----------------------------------------------------------------------
def load_invalid(csv_path):
    df = pd.read_csv(csv_path)
    df["Event"] = np.arange(len(df))
    is_preamble = df["Error Type"].eq("invalid preamble")
    is_length = df["Error Type"].eq("payload length exceeds maximum")
    df["Got_int"] = df["Got"].map(parse_int)

    df["Preamble Bit Errors"] = np.nan
    df.loc[is_preamble, "Preamble Bit Errors"] = df.loc[is_preamble, "Got_int"].map(
        lambda v: hamming_distance(v, EXPECTED_PREAMBLE)
    )
    df["Length Bit Errors"] = np.nan
    df.loc[is_length, "Length Bit Errors"] = df.loc[is_length, "Got_int"].map(
        lambda v: hamming_distance(v, EXPECTED_PAYLOAD_SIZE)
    )
    return df, is_preamble, is_length


def plot_preamble_bit_errors(csv_path="tests/outlog/invalid_packets.csv"):
    df, is_preamble, is_length = load_invalid(csv_path)
    distances = df.loc[is_preamble, "Preamble Bit Errors"].dropna()

    # 1. Bit errors per failure event
    fig, ax = plt.subplots(figsize=(12, 4.5))
    ax.scatter(df.loc[is_preamble, "Event"], df.loc[is_preamble, "Preamble Bit Errors"],
               color="crimson", s=30, label="Invalid preamble")
    if is_length.any():
        ax.scatter(df.loc[is_length, "Event"], np.zeros(is_length.sum()),
                   color="darkorange", marker="x", s=70,
                   label="Preamble OK, payload length invalid")
    ax.set_xlabel("Parser failure event (in log order)")
    ax.set_ylabel("Preamble bit errors (of 32)")
    ax.set_title("Preamble Bit Errors per Parser Failure")
    ax.set_ylim(bottom=-0.5)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    # 2. Count per Hamming distance
    counts = distances.value_counts().sort_index()
    fig, ax = plt.subplots(figsize=(8, 4.5))
    bars = ax.bar(counts.index, counts.values, color="mediumpurple", edgecolor="black")
    ax.bar_label(bars, padding=2)
    ax.set_xlabel("Preamble bit errors")
    ax.set_ylabel("Number of events")
    ax.set_xticks(range(0, int(distances.max()) + 2))
    ax.set_title("Preamble Errors by Hamming Distance")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    # 3. Which preamble bit positions flip (0 = first/MSB bit of the 32)
    flips = np.zeros(32, dtype=int)
    for v in df.loc[is_preamble, "Got_int"]:
        diff = (v ^ EXPECTED_PREAMBLE) & 0xFFFFFFFF
        for b in range(32):
            if (diff >> (31 - b)) & 1:
                flips[b] += 1
    fig, ax = plt.subplots(figsize=(11, 4.5))
    ax.bar(range(32), flips, color="teal", edgecolor="black")
    ax.set_xticks(range(32))
    ax.set_xlabel("Preamble bit position (0 = MSB)")
    ax.set_ylabel("Times flipped")
    ax.set_title("Preamble Bit Flips by Position")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    print("Preamble bit-error counts:")
    print(counts.to_string())
    print(f"\nInvalid preambles: {len(distances)}   Payload-length errors: {int(is_length.sum())}")
    print(f"Mean bit errors: {distances.mean():.2f}   Max: {distances.max():.0f}")
    print("\nPackets that would pass with a preamble Hamming tolerance of k bits:")
    for k in range(0, 6):
        n = int((distances <= k).sum())
        print(f"  k={k}: +{n} of {len(distances)} invalid preambles")

    plt.show()


def plot_preamble_hamming(csv_path="tests/outlog/invalid_packets.csv"):
    """Hamming distance of the received preamble to the expected one, only for
    packets that failed with 'invalid preamble', plus how many would be
    recovered by a correlator that tolerates up to k wrong bits."""
    df, is_preamble, _ = load_invalid(csv_path)
    d = df.loc[is_preamble, "Preamble Bit Errors"].dropna().astype(int)
    n = len(d)
    if n == 0:
        print("No invalid-preamble events to plot.")
        return

    counts = d.value_counts().sort_index()
    ks = np.arange(0, int(d.max()) + 1)
    recovered = np.array([(d <= k).sum() for k in ks])

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    bars = axes[0].bar(counts.index, counts.values, color="crimson", edgecolor="black")
    axes[0].bar_label(
        bars, labels=[f"{c}\n({100 * c / n:.0f}%)" for c in counts.values], padding=2
    )
    axes[0].set_xticks(ks)
    axes[0].set_ylim(0, counts.max() * 1.2)
    axes[0].set_xlabel("Hamming distance to expected preamble (bits of 32)")
    axes[0].set_ylabel("Packets with invalid preamble")
    axes[0].set_title(f"Preamble Bit Errors (n={n}, mean={d.mean():.2f})")
    axes[0].grid(axis="y", alpha=0.3)

    axes[1].step(ks, 100 * recovered / n, where="post", color="green", linewidth=2)
    axes[1].scatter(ks, 100 * recovered / n, color="green", zorder=3)
    for k, r in zip(ks, recovered):
        axes[1].annotate(f"{r}", (k, 100 * r / n), textcoords="offset points",
                         xytext=(0, 8), ha="center")
    axes[1].set_xticks(ks)
    axes[1].set_ylim(0, 110)
    axes[1].set_xlabel("Preamble bit-error tolerance k")
    axes[1].set_ylabel("Invalid-preamble packets recovered (%)")
    axes[1].set_title("Recoverable with a k-bit Preamble Tolerance")
    axes[1].grid(True, alpha=0.3)

    fig.tight_layout()
    plt.show()


def plot_packet_bit_errors(csv_path="tests/outlog/invalid_packets.csv"):
    df, is_preamble, is_length = load_invalid(csv_path)
    if not is_length.any():
        print("No payload-length errors to plot.")
        return

    length_df = df.loc[is_length].copy()
    length_df["Reported Length"] = length_df["Got_int"]

    fig, axes = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

    axes[0].scatter(length_df["Event"], length_df["Reported Length"],
                    color="purple", marker="x", s=80)
    axes[0].axhline(EXPECTED_PAYLOAD_SIZE, color="green", linestyle="--",
                    label=f"Expected ({EXPECTED_PAYLOAD_SIZE})")
    axes[0].axhline(MAX_PAYLOAD_SIZE, color="red", linestyle="--",
                    label=f"Parser max ({MAX_PAYLOAD_SIZE})")
    axes[0].set_yscale("log")
    axes[0].set_ylabel("Reported length (bytes)")
    axes[0].set_title("Reported Payload Length")
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    axes[1].scatter(length_df["Event"], length_df["Length Bit Errors"],
                    color="purple", marker="x", s=80)
    axes[1].set_title(f"Payload Length Bit Errors vs Expected Value {EXPECTED_PAYLOAD_SIZE}")
    axes[1].set_xlabel("Parser failure event (in log order)")
    axes[1].set_ylabel("Differing bits")
    axes[1].grid(True, alpha=0.3)

    fig.tight_layout()

    length_df["Binary"] = length_df["Reported Length"].map(lambda v: format(int(v), "b"))
    print("Payload length error summary:")
    print(length_df[["Event", "Got", "Reported Length", "Binary", "Length Bit Errors"]]
          .to_string(index=False))

    plt.show()


if __name__ == "__main__":
    plot_individual_packets("baseline-2sdr/ind_packets.csv")
    plot_summary_metrics("baseline-2sdr/tot_packets.csv")
    plot_packet_comparison("baseline-2sdr/tot_packets.csv")
    plot_preamble_bit_errors("baseline-2sdr/invalid_packets.csv")
    # plot_preamble_hamming("baseline-2sdr/invalid_packets.csv")
    # plot_packet_bit_errors("baseline-2sdr/invalid_packets.csv")