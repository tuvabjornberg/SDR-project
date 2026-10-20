import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from collections import Counter

def plot_individual_packets(csv_path="tests/outlog/ind_packets.csv"):
    df = pd.read_csv(csv_path)

    received = df[df["received"] == 1]
    lost = df[df["received"] == 0]

    #
    # Bit errors vs sequence
    #
    plt.figure(figsize=(10, 4))
    plt.scatter(received["sequence"], received["bit_errors"])
    plt.xlabel("Sequence")
    plt.ylabel("Bit Errors")
    plt.title("Bit Errors per Received Packet")
    plt.grid(True)
    plt.tight_layout()

    #
    # Received vs sequence
    #
    plt.figure(figsize=(10, 4))
    plt.plot(df["sequence"], df["received"].astype(int))
    plt.xlabel("Sequence")
    plt.ylabel("Received")
    plt.yticks([0, 1])
    plt.title("Packet Reception")
    plt.grid(True)
    plt.tight_layout()

    #
    # Reception timeline
    #
    plt.figure(figsize=(12, 4))

    plt.scatter(
        received["sequence"],
        [1] * len(received),
        color="green",
        label="Received"
    )

    plt.scatter(
        lost["sequence"],
        [0] * len(lost),
        color="red",
        alpha=0.6,
        label="Lost"
    )

    plt.yticks([0, 1], ["Lost", "Received"])
    plt.xlabel("Packet Sequence")
    plt.title("Packet Reception Timeline")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()

    #
    # Latency plots
    #
    latency_df = df[df["latency_ms"] >= 0]

    plt.figure(figsize=(12, 5))
    plt.plot(
        latency_df["sequence"],
        latency_df["latency_ms"],
        marker="o"
    )
    plt.xlabel("Sequence")
    plt.ylabel("Latency (ms)")
    plt.title("Packet Latency")
    plt.grid(True)
    plt.tight_layout()

    #
    # Latency histogram
    #
    plt.figure(figsize=(8, 5))
    plt.hist(
        latency_df["latency_ms"],
        bins=20,
        edgecolor="black"
    )
    plt.xlabel("Latency (ms)")
    plt.ylabel("Count")
    plt.title("Latency Distribution")
    plt.grid(True)
    plt.tight_layout()

    #
    # Latency CDF
    #
    latency = np.sort(latency_df["latency_ms"])

    cdf = np.arange(1, len(latency) + 1) / len(latency)

    plt.figure(figsize=(8, 5))
    plt.plot(latency, cdf)
    plt.xlabel("Latency (ms)")
    plt.ylabel("CDF")
    plt.title("Latency CDF")
    plt.grid(True)
    plt.tight_layout()

    #
    # Sliding PDR
    #
    window = 20

    rolling_pdr = (
        df["received"]
        .rolling(window)
        .mean()
    )

    plt.figure(figsize=(12, 5))
    plt.plot(
        df["sequence"],
        rolling_pdr,
        linewidth=2
    )

    plt.xlabel("Sequence")
    plt.ylabel("PDR")
    plt.title(f"Sliding PDR (Window={window})")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.tight_layout()

    #
    # Reception heatmap
    #
    status = df["received"].values

    width = 20
    padding = (width - len(status) % width) % width

    heatmap = np.pad(
        status,
        (0, padding)
    ).reshape(-1, width)

    plt.figure(figsize=(10, 4))
    plt.imshow(
        heatmap,
        cmap="RdYlGn",
        aspect="auto"
    )

    plt.colorbar(label="Received")
    plt.title("Packet Reception Heatmap")
    plt.xlabel("Packet Index")
    plt.ylabel("Block")
    plt.tight_layout()

    plt.show()


def plot_summary_metrics(csv_path="tests/outlog/tot_packets.csv"):
    df = pd.read_csv(csv_path)

    row = df.iloc[0]

    #
    # PDR / PER / BER
    #
    metrics = ["PDR", "PER", "BER"]
    values = [
        row["pdr"],
        row["per"],
        row["ber"]
    ]

    plt.figure(figsize=(7, 5))

    bars = plt.bar(
        metrics,
        values,
        color=["green", "red", "blue"]
    )

    for bar in bars:
        plt.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height(),
            f"{bar.get_height():.2f}",
            ha="center"
        )

    plt.title("Link Performance Summary")
    plt.ylabel("Value")
    plt.grid(axis="y")
    plt.tight_layout()

    #
    # Packet flow
    #
    stages = [
        "TX",
        "Received",
        "Detected",
        "Decoded"
    ]

    counts = [
        row["packets_tx"],
        row["packets_received"],
        row["packets_detected"],
        row["packets_decoded"]
    ]

    plt.figure(figsize=(8, 5))

    bars = plt.bar(
        stages,
        counts,
        color="steelblue"
    )

    for i, count in enumerate(counts):
        plt.text(
            i,
            count + 2,
            str(int(count)),
            ha="center"
        )

    plt.ylabel("Packets")
    plt.title("Packet Processing Pipeline")
    plt.grid(axis="y")
    plt.tight_layout()

    plt.show()

EXPECTED_PREAMBLE = 0x6d5a96a5

def hamming_distance(a, b):
    return bin(a ^ b).count("1")

def plot_preamble_bit_errors(
    csv_path="tests/outlog/invalid_packets.csv"
):
    df = pd.read_csv(csv_path)

    is_preamble = df["Error Type"].eq("invalid preamble")
    preambles = df.loc[is_preamble, "Got"].astype(str)

    df["Bit Errors"] = np.nan
    df.loc[is_preamble, "Bit Errors"] = preambles.map(
        lambda value: hamming_distance(
            int(value, 16),
            EXPECTED_PREAMBLE
        )
    )

    # --------------------------------------------------
    # 1. Bit errors vs packet sequence
    # --------------------------------------------------
    fig, ax = plt.subplots(figsize=(12, 4.5))

    ax.scatter(
        df.loc[is_preamble, "Sequence Number"],
        df.loc[is_preamble, "Bit Errors"],
        color="crimson",
        s=55,
        label="Invalid preamble"
    )

    # Mark payload-length errors separately.
    length_errors = df.loc[~is_preamble]

    if not length_errors.empty:
        ax.scatter(
            length_errors["Sequence Number"],
            np.zeros(len(length_errors)),
            color="darkorange",
            marker="x",
            s=90,
            label="Payload length error"
        )

    ax.set_xlabel("Packet Sequence Number")
    ax.set_ylabel("Preamble Bit Errors")
    ax.set_title("Preamble Bit Errors per Packet")
    ax.set_ylim(bottom=-0.5)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    # --------------------------------------------------
    # 2. Distribution of bit errors
    # --------------------------------------------------
    distances = df.loc[is_preamble, "Bit Errors"].dropna()

    fig, ax = plt.subplots(figsize=(8, 4.5))

    bins = np.arange(-0.5, 33.5, 1)

    ax.hist(
        distances,
        bins=bins,
        color="steelblue",
        edgecolor="black"
    )

    ax.set_xlabel("Number of Bit Errors")
    ax.set_ylabel("Number of Packets")
    ax.set_title("Distribution of Preamble Bit Errors")
    ax.set_xticks(range(0, 33, 2))
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    # --------------------------------------------------
    # 3. Count per bit-error level
    # --------------------------------------------------
    counts = distances.value_counts().sort_index()

    fig, ax = plt.subplots(figsize=(8, 4.5))

    bars = ax.bar(
        counts.index,
        counts.values,
        color="mediumpurple",
        edgecolor="black"
    )

    ax.bar_label(bars, padding=2)
    ax.set_xlabel("Number of Bit Errors")
    ax.set_ylabel("Number of Packets")
    ax.set_title("Preamble Error Counts by Hamming Distance")
    ax.set_xticks(range(0, 33, 2))
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()

    print("Preamble bit-error counts:")
    print(counts.to_string())
    print(f"\nTotal invalid preambles: {len(distances)}")
    print(f"Mean bit errors: {distances.mean():.2f}")
    print(f"Maximum bit errors: {distances.max():.0f}")

    plt.show()

EXPECTED_PAYLOAD_SIZE = 32  # bytes
MAX_PAYLOAD_SIZE = 200 

def plot_packet_bit_errors(
    csv_path="tests/outlog/invalid_packets.csv"
):
    df = pd.read_csv(csv_path)

    is_preamble = df["Error Type"].eq("invalid preamble")
    is_length = df["Error Type"].eq("payload length exceeds maximum")

    # Preamble bit errors (32-bit comparison)
    df["Preamble Bit Errors"] = np.nan

    df.loc[is_preamble, "Preamble Bit Errors"] = (
        df.loc[is_preamble, "Got"]
        .astype(str)
        .map(lambda x: hamming_distance(
            int(x, 16), EXPECTED_PREAMBLE
        ))
    )

    # Payload-length bit errors.
    # Compare at a common bit width to account for leading zeros.
    df["Payload Length Bit Errors"] = np.nan
    df["Reported Length"] = np.nan

    reported_lengths = df.loc[is_length, "Got"].astype(str).map(
        lambda x: int(x, 16)
    )

    df.loc[is_length, "Reported Length"] = reported_lengths

    for idx, value in reported_lengths.items():
        width = max(
            EXPECTED_PAYLOAD_SIZE.bit_length(),
            int(value).bit_length()
        )

        df.loc[idx, "Payload Length Bit Errors"] = (
            (int(value) ^ EXPECTED_PAYLOAD_SIZE).bit_count()
        )

    # -----------------------------------------------
    # Plot 1: preamble bit errors per packet
    # -----------------------------------------------
    fig, axes = plt.subplots(2, 1, figsize=(12, 10))

    axes[0].scatter(
        df.loc[is_preamble, "Sequence Number"],
        df.loc[is_preamble, "Preamble Bit Errors"],
        color="crimson"
    )
    axes[0].set_title("Preamble Bit Errors")
    axes[0].set_ylabel("Bit errors (out of 32)")
    axes[0].grid(True, alpha=0.3)


    # -----------------------------------------------
    # Plot 3: payload length bit errors
    # -----------------------------------------------
    axes[1].scatter(
        df.loc[is_length, "Sequence Number"],
        df.loc[is_length, "Payload Length Bit Errors"],
        color="purple",
        marker="x",
        s=80
    )
    axes[1].set_title("Payload Length Bit Errors vs Expected Value 32")
    axes[1].set_xlabel("Packet Sequence Number")
    axes[1].set_ylabel("Differing bits")
    axes[1].grid(True, alpha=0.3)


    for ax in axes:
        ax.set_xlim(0, 200)
        ax.set_xticks(np.arange(0, 201, 20))
    plt.tight_layout()
    plt.show()

    print("Payload length bit-error summary:")
    print(
        df.loc[is_length, [
            "Sequence Number",
            "Got",
            "Reported Length",
            "Payload Length Bit Errors"
        ]].to_string(index=False)
    )


if __name__ == "__main__":
    # plot_individual_packets("baseline-2sdr/ind_packets.csv")
    # plot_summary_metrics("baseline-2sdr/tot_packets.csv")
    plot_preamble_bit_errors("baseline-2sdr/invalid_packets.csv")
    plot_packet_bit_errors("baseline-2sdr/invalid_packets.csv")
    