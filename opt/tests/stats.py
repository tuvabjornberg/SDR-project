import argparse
import re

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

import os

from matplotlib.colors import LogNorm
from matplotlib.lines import Line2D

PLOT_DIR = "plots"
os.makedirs(PLOT_DIR, exist_ok=True)

PREAMBLE, SEQ, LENGTH, CHECKSUM = 32, 16, 16, 32
KEYS = {"run": str, "label": str, "mode": str, "variant": str}
NON_PAYLOAD_BITS = PREAMBLE + SEQ + LENGTH + CHECKSUM
STATUSES = ["ok", "valid_errors", "invalid", "lost"]
STATUS_COLORS = {"ok": "green", "valid_errors": "darkorange", "invalid": "crimson", "lost": "black"}
FIELDS = ["preamble", "seq", "length", "payload", "checksum"]

COLORS = {
    "RX0 only": "#1f77b4",
    "RX1 only": "#ff7f0e",
    "RX0+RX1 (MRC)": "#2ca02c"
}

OUTPUT_DIR = "plots"

SUMMARY_FILE = "results/two_comp/summary.csv"
PACKETS_FILE = "results/two_comp/packets.csv"
BIT_ERRORS_FILE = "results/two_comp/bit_errors.csv"

def order_labels(labels):
    """Sorted labels, plus {label: distance in cm} if every label looks like a distance."""
    dist = {l: distance_cm(l) for l in labels}
    if all(v is not None for v in dist.values()):
        return sorted(labels, key=lambda l: dist[l]), dist
    return sorted(labels), None

def wilson(k, n, z=1.96):
    """95% Wilson confidence interval for a proportion k/n."""
    if n == 0:
        return np.nan, np.nan
    p = k / n
    d = 1 + z ** 2 / n
    centre = (p + z ** 2 / (2 * n)) / d
    half = z * np.sqrt(p * (1 - p) / n + z ** 2 / (4 * n ** 2)) / d
    return max(0.0, centre - half), min(1.0, centre + half)

def default_variant(df):
    variants = list(df.variant.unique())
    mrc = [v for v in variants if "MRC" in v]
    return mrc[0] if mrc else variants[-1]

def save_fig(fig, name):
    fig.savefig(
        os.path.join(PLOT_DIR, f"{name}.png"),
        dpi=300,
        bbox_inches="tight"
    )

def load(out_dir):
    read = lambda name: pd.read_csv(f"{out_dir}/{name}.csv", dtype=KEYS)
    return read("summary"), read("packets"), read("bit_errors")


def pick(df, label, run):
    if label:
        df = df[df.label == label]
    if run is not None:
        df = df[df.run == run]
    return df

def distance_cm(label):
    m = re.match(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(cm|m)?", label.lower())
    if not m:
        return None
    value = float(m.group(1))
    return value * 100 if m.group(2) == "m" else value


def order_labels(labels):
    """Sorted labels, plus {label: distance in cm} if every label looks like a distance."""
    dist = {l: distance_cm(l) for l in labels}
    if all(v is not None for v in dist.values()):
        return sorted(labels, key=lambda l: dist[l]), dist
    return sorted(labels), None


def wilson(k, n, z=1.96):
    """95% Wilson confidence interval for a proportion k/n."""
    if n == 0:
        return np.nan, np.nan
    p = k / n
    d = 1 + z ** 2 / n
    centre = (p + z ** 2 / (2 * n)) / d
    half = z * np.sqrt(p * (1 - p) / n + z ** 2 / (4 * n ** 2)) / d
    return max(0.0, centre - half), min(1.0, centre + half)


def failure_bursts(g):
    """Lengths of runs of consecutive packets that were not received bit-exact."""
    runs, run = [], 0
    for fail in (g.sort_values("sequence").status != "ok"):
        if fail:
            run += 1
        elif run:
            runs.append(run)
            run = 0
    if run:
        runs.append(run)
    return runs


def aggregate(p, keep_misattributed=False):
    """One row per (label, series). A series is a variant (RX0 only / RX1 only / MRC)."""
    p = p.copy()
    p["series"] = (p["mode"] + " / " + p["variant"]) if p["mode"].nunique() > 1 else p["variant"]

    rows = []
    for (label, series), g in p.groupby(["label", "series"]):
        n = len(g)
        counts = g.status.value_counts()
        got = g[g.status != "lost"]
        # > 25% wrong bits almost always means a corrupted sequence number that was matched to
        # the wrong transmitted packet, not real bit errors.
        mis = got.bit_errors > 0.25 * got.packet_bits
        used = got if keep_misattributed else got[~mis]
        k = len(used)

        r = {"label": label, "series": series, "mode": g["mode"].iloc[0],
             "variant": g["variant"].iloc[0], "n": n}
        r.update({s: int(counts.get(s, 0)) for s in STATUSES})
        r["pdr_exact"] = r["ok"] / n
        r["pdr_valid"] = (r["ok"] + r["valid_errors"]) / n
        r["ci_lo"], r["ci_hi"] = wilson(r["ok"], n)

        r["bits"] = int(used.packet_bits.sum())
        r["errs"] = int(used.bit_errors.sum())
        r["ber"] = r["errs"] / r["bits"] if r["bits"] else np.nan
        r["misattributed"] = int(mis.sum())

        for name, width, col in [("preamble", PREAMBLE, "preamble_errors"),
                         ("seq", SEQ, "seq_errors"),
                         ("length", LENGTH, "length_errors"),
                         ("checksum", CHECKSUM, "checksum_errors")]:
            r["errors_" + name] = used[col].sum()
            r["ber_" + name] = used[col].sum() / (width * k) if k else np.nan
        r["errors_payload"] = used["payload_errors"].sum()
        payload_bits = (used.packet_bits - NON_PAYLOAD_BITS).sum()
        r["ber_payload"] = used.payload_errors.sum() / payload_bits if payload_bits else np.nan

        lens = []
        for _, gr in g.groupby("run"):
            lens += failure_bursts(gr)
        r["burst_count"] = len(lens)
        r["burst_mean"] = float(np.mean(lens)) if lens else 0.0
        r["burst_max"] = max(lens) if lens else 0
        rows.append(r)

    return pd.DataFrame(rows)


def default_variant(df):
    variants = list(df.variant.unique())
    mrc = [v for v in variants if "MRC" in v]
    return mrc[0] if mrc else variants[-1]


def set_label_axis(ax, labels, pos, numeric):
    ax.set_xticks([pos[l] for l in labels])
    ax.set_xticklabels(labels)
    ax.set_xlabel("Distance (cm)" if numeric else "Label")

def plot_pdr_ber(df, labels, pos, numeric):
    fig, ax = plt.subplots(1, 2, figsize=(13, 5))

    for series, g in df.groupby("series"):
        g = g.set_index("label")
        present = [l for l in labels if l in g.index]
        g = g.loc[present]
        x = [pos[l] for l in present]

        yerr = np.vstack([100 * (g.pdr_exact - g.ci_lo), 100 * (g.ci_hi - g.pdr_exact)])
        ax[0].errorbar(x, 100 * g.pdr_exact, yerr=np.clip(yerr, 0, None), marker="o", capsize=3,
                       label=series)

        line, = ax[1].plot(x, 100 * g.ber.where(g.errs > 0), marker="o", label=series)
        zero = g[(g.errs == 0) & (g.bits > 0)]
        if len(zero):  # no errors seen: show the 95% upper bound (rule of three)
            ax[1].scatter([pos[l] for l in zero.index], 100 * 3 / zero.bits, marker="v",
                          color=line.get_color(), zorder=3)

    ax[0].set_ylabel("PDR decoded (%)")
    ax[0].set_ylim(0, 105)
    ax[0].set_title("Packet delivery ratio (95% Wilson interval)")
    ax[1].set_ylabel("BER of decoded frames (%)")
    ax[1].set_yscale("log")
    ax[1].set_title("Bit error rate  (triangle = no errors seen, 95% upper bound)")
    for a in ax:
        set_label_axis(a, labels, pos, numeric)
        a.grid(True, alpha=0.3)
        a.legend()
    fig.tight_layout()
    save_fig(fig, "pdr_ber")


def plot_status_breakdown(df, labels, series):
    d = df[df.series == series].set_index("label")
    present = [l for l in labels if l in d.index]
    x = np.arange(len(present))
    bottom = np.zeros(len(present))

    fig, ax = plt.subplots(figsize=(9, 5))
    for st in STATUSES:
        vals = np.array([100 * d.loc[l, st] / d.loc[l, "n"] for l in present])
        bars = ax.bar(x, vals, bottom=bottom, color=STATUS_COLORS[st], edgecolor="black", label=st)
        ax.bar_label(bars, labels=[f"{v:.0f}%" if v >= 4 else "" for v in vals], label_type="center",
                     color="white")
        bottom += vals
    ax.set_xticks(x)
    ax.set_xticklabels([f"{l}\n(n={d.loc[l, 'n']})" for l in present])
    ax.set_ylabel("Share of transmitted packets (%)")
    ax.set_title(f"What happens to the packets - {series}")
    ax.legend(loc="lower left")
    fig.tight_layout()
    save_fig(fig, "status_breakdown")


def plot_field_ber(df, labels, series):
    d = df[df.series == series].set_index("label")
    present = [l for l in labels if l in d.index]

    x = np.arange(len(FIELDS))
    w = 0.8 / max(1, len(present))

    fig, ax = plt.subplots(figsize=(10, 5))

    for i, l in enumerate(present):

        vals = np.array([
            d.loc[l, "errors_" + f] for f in FIELDS
        ], dtype=float)

        bars = ax.bar(
            x + (i - (len(present) - 1) / 2) * w,
            vals,
            w,
            label=l,
            edgecolor="black"
        )

        ax.bar_label(bars, padding=2, fmt="%d")

    ax.set_xticks(x)
    ax.set_xticklabels(FIELDS)

    ax.set_ylabel("Number of Bit Errors")
    ax.set_title(f"Number of bit errors per packet field - {series}")

    ax.grid(axis="y", alpha=0.3)
    ax.legend(title="Distance")

    fig.tight_layout()
    save_fig(fig, "field_bit_errors")


def plot_bursts(df, labels, series):
    d = df[df.series == series].set_index("label")
    present = [l for l in labels if l in d.index]
    x = np.arange(len(present))
    w = 0.38

    fig, ax = plt.subplots(figsize=(9, 4.5))
    b1 = ax.bar(x - w / 2, [d.loc[l, "burst_mean"] for l in present], w, label="mean",
                edgecolor="black")
    b2 = ax.bar(x + w / 2, [d.loc[l, "burst_max"] for l in present], w, label="max",
                edgecolor="black")
    ax.bar_label(b1, fmt="%.1f", padding=2)
    ax.bar_label(b2, fmt="%d", padding=2)
    ax.set_xticks(x)
    ax.set_xticklabels([f"{l}\n({d.loc[l, 'burst_count']} bursts)" for l in present])
    ax.set_ylabel("Consecutive packets not received bit-exact")
    ax.set_title(f"Failure burst length - {series}")
    ax.grid(axis="y", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    save_fig(fig, "bursts")

def bit_error_bursts(bit_errors):

    if bit_errors.empty:
        return []

    errors = bit_errors.sort_values(["sequence", "bit_index"])

    bursts = []
    current_burst = 0

    previous_sequence = None
    previous_bit = None

    for row in errors.itertuples():

        sequence = row.sequence
        bit = row.bit_index

        if (sequence == previous_sequence and
                bit == previous_bit + 1):

            current_burst += 1

        else:
            if current_burst > 0:
                bursts.append(current_burst)

            current_burst = 1

        previous_sequence = sequence
        previous_bit = bit

    if current_burst > 0:
        bursts.append(current_burst)

    return bursts

def aggregate_bit_bursts(bit_errors):

    rows = []

    for (run, distance, variant), group in bit_errors.groupby(
        ["run", "distance", "variant"]
    ):

        bursts = bit_error_bursts(group)

        for length, count in pd.Series(bursts).value_counts().items():
            rows.append({
                "run": run,
                "distance": distance,
                "variant": variant,
                "burst_length": length,
                "occurrences": count
            })

    return pd.DataFrame(rows)

def plot_bit_error_bursts_all(bit_errors):

    df = aggregate_bit_bursts(bit_errors)

    if df.empty:
        print("No bit errors found.")
        return

    variants = ["RX0 only", "RX1 only", "RX0+RX1 (MRC)"]
    distances = sorted(df["distance"].unique())

    max_length = int(df["burst_length"].max())
    lengths = np.arange(1, max_length + 1)

    fig, axes = plt.subplots(
        len(distances), 1,
        figsize=(11, 4 * len(distances)),
        sharex=True
    )

    if len(distances) == 1:
        axes = [axes]

    width = 0.25

    for ax, distance in zip(axes, distances):

        d = df[df.distance == distance]

        for i, variant in enumerate(variants):

            counts = (
                d[d.variant == variant]
                .groupby("burst_length")["occurrences"]
                .sum()
                .reindex(lengths, fill_value=0)
            )

            offset = (i - 1) * width

            bars = ax.bar(
                lengths + offset,
                counts.values,
                width,
                label=variant,
                color=COLORS[variant],
                edgecolor="black"
            )

        ax.set_ylabel("Occurrences")
        ax.set_title(f"Distance: {distance:g} cm")
        ax.grid(axis="y", alpha=0.3)
        ax.legend()

    axes[-1].set_xlabel("Consecutive bit errors in a burst")

    axes[-1].set_xticks(lengths)

    fig.suptitle(
        "Bit Error Burst Length Distribution",
        fontweight="bold",
        y=1.002
    )

    fig.tight_layout()
    save_fig(fig, "bit_error_burst_distribution_all")

def plot_bit_error_bursts(bit_errors):

    df = aggregate_bit_bursts(bit_errors)

    if df.empty:
        print("No bit errors found.")
        return

    # Only MRC
    df = df[df.variant == "RX0+RX1 (MRC)"]

    if df.empty:
        print("No MRC bit errors found.")
        return

    # Combine all distances and runs
    df = (
        df.groupby("burst_length")["occurrences"]
        .sum()
    )

    lengths = np.arange(1, int(df.index.max()) + 1)
    counts = df.reindex(lengths, fill_value=0)

    fig, ax = plt.subplots(figsize=(11, 5))

    bars = ax.bar(
        lengths,
        counts.values,
        color=COLORS["RX0+RX1 (MRC)"],
        edgecolor="black"
    )

    ax.set_xlabel("Consecutive wrong bits in a burst")
    ax.set_ylabel("Number of occurrences")
    ax.set_title("Bit Error Burst Length Distribution - RX0+RX1 (MRC)")

    ax.set_xticks(lengths)
    ax.grid(axis="y", alpha=0.3)

    fig.tight_layout()
    save_fig(fig, "bit_error_burst_distribution_mrc")


def print_table(df, labels):
    d = df.copy()
    d["order"] = d.label.map({l: i for i, l in enumerate(labels)})
    d = d.sort_values(["series", "order"])
    d["PDR exact %"] = [f"{100 * r.pdr_exact:5.1f} [{100 * r.ci_lo:4.1f}-{100 * r.ci_hi:5.1f}]"
                        for r in d.itertuples()]
    d["PDR csum-ok %"] = (100 * d.pdr_valid).round(1)
    d["BER %"] = (100 * d.ber).round(3)
    cols = ["series", "label", "n", "PDR exact %", "PDR csum-ok %", "BER %", "ok", "valid_errors",
            "invalid", "lost", "misattributed", "burst_count", "burst_mean", "burst_max"]
    print(d[cols].to_string(index=False, float_format=lambda v: f"{v:.2f}"))


def plot_distance_comparison(packets, variant=None, keep_misattributed=False):
    if packets.empty:
        raise SystemExit("No rows in packets.csv for this selection.")

    labels, dist = order_labels(sorted(packets.label.unique()))
    numeric = dist is not None
    pos = dist if numeric else {l: i for i, l in enumerate(labels)}

    df = aggregate(packets, keep_misattributed)
    print_table(df, labels)
    if not keep_misattributed and df.misattributed.sum():
        print(f"\nExcluded {int(df.misattributed.sum())} frame(s) with >25% wrong bits from the BER "
              f"numbers (likely a corrupted sequence number matched to the wrong packet). "
              f"Use --keep-misattributed to include them.")

    plot_pdr_ber(df, labels, pos, numeric)

def plot_summary(s):
    x = np.arange(len(s))
    w = 0.38
    fig, ax = plt.subplots(1, 2, figsize=(12, 4.5))

    b1 = ax[0].bar(x - w / 2, 100 * s.pdr_valid, w, label="PDR (checksum ok)", color="green")
    b2 = ax[0].bar(x + w / 2, 100 * s.pdr_exact, w, label="PDR (bit-exact)", color="steelblue")
    ax[0].bar_label(b1, fmt="%.1f%%", padding=2)
    ax[0].bar_label(b2, fmt="%.1f%%", padding=2)
    ax[0].set_xticks(x, s.variant)
    ax[0].set_ylim(0, 112)
    ax[0].set_ylabel("Percent of transmitted packets")
    ax[0].set_title("Packet delivery")
    ax[0].legend()
    ax[0].grid(axis="y", alpha=0.3)

    b3 = ax[1].bar(x, 100 * s.ber, color="mediumpurple")
    ax[1].bar_label(b3, fmt="%.3f%%", padding=2)
    ax[1].set_xticks(x, s.variant)
    ax[1].set_ylabel("BER (%)  of decoded frames")
    ax[1].set_title("Bit error rate")
    ax[1].grid(axis="y", alpha=0.3)

    fig.tight_layout()
    save_fig(fig, "summary")

    cols = ["variant", "num_packets", "detected", "present", "decoded_valid", "exact", "lost",
            "pdr_valid", "pdr_exact", "bit_errors", "ber"]
    print(s[cols].to_string(index=False))

def plot_error_positions(p, be, title):
    d = p[p.status != "lost"]
    if d.empty:
        print("No decoded frames, skipping error-position plots.")
        return

    nbits = int(d.packet_bits.max())
    payload_end = nbits - CHECKSUM
    fields = [
        ("preamble", 0, PREAMBLE, "tab:blue"),
        ("seq", PREAMBLE, PREAMBLE + SEQ, "tab:orange"),
        ("length", PREAMBLE + SEQ, PREAMBLE + SEQ + LENGTH, "tab:green"),
        ("payload", PREAMBLE + SEQ + LENGTH, payload_end, "tab:gray"),
        ("checksum", payload_end, nbits, "tab:red"),
    ]

    denom = np.maximum(np.array([(d.packet_bits > i).sum() for i in range(nbits)]), 1)
    counts = np.bincount(be.bit_index.astype(int), minlength=nbits)[:nbits]

    n_err = d.bit_errors
    print(f"\n{title}: {int((n_err > 0).sum())} of {len(d)} decoded frames contain errors; "
          f"median errors in those = {n_err[n_err > 0].median() if (n_err > 0).any() else 0}")
    if (d.bit_errors > 0.25 * d.packet_bits).any():
        n = int((d.bit_errors > 0.25 * d.packet_bits).sum())
        print(f"  note: {n} frame(s) have >25% wrong bits - probably a corrupted sequence number "
              f"matched to the wrong transmitted packet, not real bit errors.")

def plot_preamble_hamming(p, title):
    d = p[p.status != "lost"]
    bad = d[d.preamble_errors > 0]
    if bad.empty:
        print("No frames with preamble errors.")
        return
    counts = bad.preamble_errors.value_counts().sort_index()
    ks = np.arange(0, int(bad.preamble_errors.max()) + 1)
    recovered = np.array([(bad.preamble_errors <= k).sum() for k in ks])

    fig, ax = plt.subplots(1, 2, figsize=(12, 4.5))
    bars = ax[0].bar(counts.index, counts.values, color="crimson", edgecolor="black")
    ax[0].bar_label(bars, padding=2)
    ax[0].set_xlabel("Preamble bit errors (of 32)")
    ax[0].set_ylabel("Frames")
    ax[0].set_title(f"Preamble errors - {title}")
    ax[0].grid(axis="y", alpha=0.3)

    ax[1].step(ks, 100 * recovered / len(bad), where="post", color="green", linewidth=2)
    ax[1].set_xlabel("Preamble tolerance k (bits)")
    ax[1].set_ylabel("Frames with a bad preamble accepted (%)")
    ax[1].set_title("Accepted with a k-bit preamble tolerance")
    ax[1].grid(True, alpha=0.3)
    fig.tight_layout()
    save_fig(fig, "preamble_bit_err")

def plot_sweep(s, x):
    fig, ax = plt.subplots(1, 2, figsize=(12, 4.5))
    for variant, g in s.groupby("variant"):
        g = g.groupby(x, as_index=False)[["pdr_valid", "pdr_exact", "ber"]].mean().sort_values(x)
        ax[0].plot(g[x], 100 * g.pdr_exact, marker="o", label=variant)
        ax[1].plot(g[x], 100 * g.ber, marker="o", label=variant)
    ax[0].set_xlabel(x)
    ax[0].set_ylabel("PDR, bit-exact (%)")
    ax[0].set_ylim(0, 105)
    ax[0].set_title(f"PDR vs {x}")
    ax[1].set_xlabel(x)
    ax[1].set_ylabel("BER (%)")
    ax[1].set_title(f"BER vs {x}")
    for a in ax:
        a.grid(True, alpha=0.3)
        a.legend()
    fig.tight_layout()
    save_fig(fig, "ber_pdr")

def test():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", nargs="?", default="results/two_comp")
    ap.add_argument("--variant", help="variant for the per-label breakdown plots (default: MRC)")
    ap.add_argument("--mode", help="only rows with this mode (siso / simo / mimo ...)")
    ap.add_argument("--keep-misattributed", action="store_true")
    args = ap.parse_args()

    p = pd.read_csv(f"{args.out_dir}/packets.csv", dtype=KEYS)
    if args.mode:
        p = p[p["mode"] == args.mode]

    plot_distance_comparison(p, args.variant, args.keep_misattributed)
    plt.show()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", nargs="?", default="results/two_comp")
    ap.add_argument("--label", help="only rows with this label")
    ap.add_argument("--run", default="latest", help="run id, or 'latest' (default)")
    ap.add_argument("--variant", help="variant for the per-packet plots")
    ap.add_argument("--sweep", help="column to sweep over across runs")
    args = ap.parse_args()

    summary, packets, bits = load(args.out_dir)

    summary = pick(summary, args.label, None)

    if summary.empty:
        raise SystemExit("No rows found (check --label).")

    if args.sweep:
        plot_sweep(summary, args.sweep)

    run = str(summary.run.astype(int).max()) if args.run == "latest" else args.run

    s = pick(summary, args.label, run)
    p = pick(packets, args.label, run)

    if s.empty:
        raise SystemExit(f"Run {run} not found.")

    plot_summary(s)

    variant = args.variant or s.variant.iloc[-1]

    pv = p[p.variant == variant].sort_values("sequence")
    title = f"{variant} (run {run})"

    bv = pick(bits, args.label, run)
    bv = bv[bv.variant == variant]

    plot_error_positions(pv, bv, title)
    plot_preamble_hamming(pv, title)

    bv_all = pick(bits, args.label, None)
    bv_all = bv_all.copy()
    bv_all["distance"] = (
        bv_all["label"]
        .str.replace("cm", "", regex=False)
        .astype(float)
    )

    plot_bit_error_bursts(bv_all)
    plot_bit_error_bursts_all(bv_all)

    plt.show()

def finish(fig, name, save_dir):
    fig.tight_layout()
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        fig.savefig(os.path.join(save_dir, name + ".png"), dpi=200)


def add_series(df, multi_mode):
    df = df.copy()
    df["series"] = (df["mode"] + " / " + df["variant"]) if multi_mode else df["variant"]
    return df


def pool_summary(s):
    """One row per (label, mode, variant): add up the counts of all runs, recompute the rates."""
    cols = ["num_packets", "detected", "present", "decoded_valid", "exact", "lost",
            "parse_failures", "invalid_seq", "bits_compared", "bit_errors"]
    g = s.groupby(["label", "mode", "variant"], as_index=False)[cols].sum()
    g["pdr_valid"] = g.decoded_valid / g.num_packets
    g["pdr_exact"] = g.exact / g.num_packets
    g["ber"] = g.bit_errors / g.bits_compared.where(g.bits_compared > 0)
    ci = [wilson(int(k), int(n)) for k, n in zip(g.exact, g.num_packets)]
    g["ci_lo"] = [c[0] for c in ci]
    g["ci_hi"] = [c[1] for c in ci]
    return g


def plot_performance(df, labels, save_dir):
    """PDR and BER per label, one bar per variant."""
    series = list(df.series.unique())
    x = np.arange(len(labels))
    w = 0.8 / len(series)
    fig, ax = plt.subplots(1, 2, figsize=(14, 5))

    for i, s in enumerate(series):
        d = df[df.series == s].set_index("label")
        off = x + (i - (len(series) - 1) / 2) * w

        pdr = np.array([100 * d.pdr_exact.get(l, np.nan) for l in labels])
        bars = ax[0].bar(off, pdr, w, label=s, edgecolor="black")
        ax[0].bar_label(bars, fmt="%.1f", rotation=90, padding=2, fontsize=7)

        ber = np.array([100 * d.ber.get(l, np.nan) for l in labels])
        errs = np.array([d.bit_errors.get(l, np.nan) for l in labels])
        bits = np.array([d.bits_compared.get(l, np.nan) for l in labels])
        b = ax[1].bar(off, np.where(ber > 0, ber, np.nan), w, label=s, edgecolor="black")
        zero = (errs == 0) & (bits > 0)
        if zero.any():  # no errors seen: show the 95% upper bound (rule of three)
            ax[1].scatter(off[zero], 100 * 3 / bits[zero], marker="v",
                          color=b.patches[0].get_facecolor(), edgecolor="black", zorder=3)

    n_per_label = [int(df[df.label == l].num_packets.max()) for l in labels]
    ticks = [f"{l}\n(n={n})" for l, n in zip(labels, n_per_label)]
    for a in ax:
        a.set_xticks(x)
        a.set_xticklabels(ticks)
        a.grid(axis="y", alpha=0.3)
        a.legend()

    lo = np.nanmin(100 * df.pdr_exact)
    ax[0].set_ylim(max(0, np.floor(lo) - 2), 101.5)
    ax[0].set_ylabel("PDR, bit-exact (%)")
    ax[0].set_title("Packet delivery ratio  (y axis zoomed)")
    ax[1].set_yscale("log")
    ax[1].set_ylabel("BER of decoded frames (%)")
    ax[1].set_title("Bit error rate  (triangle = no errors seen, 95% upper bound)")
    finish(fig, "pdr_ber_per_label", save_dir)
    

def plot_metric(data, metric, ylabel, title, filename,
                log_scale=False, y_max=None):

    fig, ax = plt.subplots(figsize=(11, 6))

    for variant in data["variant"].unique():

        subset = data[data["variant"] == variant]
        subset = subset.sort_values("distance")

        ax.plot(
            subset["distance"],
            subset[metric],
            marker="o",
            markersize=8,
            linewidth=2.5,
            label=variant,
            color=COLORS.get(variant)
        )

    ax.set_xlabel("Distance (cm)")
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontweight="bold")

    ax.set_xticks(sorted(data["distance"].unique()))

    if log_scale:
        ax.set_yscale("log")

    if y_max is not None:
        ax.set_ylim(top=y_max)

    ax.legend(title="Receiver configuration")
    ax.grid(True, which="both", linestyle="--", alpha=0.5)

    plt.tight_layout()

    plt.savefig(
        f"{OUTPUT_DIR}/{filename}",
        dpi=300,
        bbox_inches="tight"
    )

    plt.show()


def plot_additional_summary_plots():

    summary = pd.read_csv(SUMMARY_FILE)
    packets = pd.read_csv(PACKETS_FILE)
    bit_errors = pd.read_csv(BIT_ERRORS_FILE)

    # Convert labels into numerical distances

    summary["distance"] = summary["label"].str.replace(
        "cm", "", regex=False
    ).astype(float)

    packets["distance"] = packets["label"].str.replace(
        "cm", "", regex=False
    ).astype(float)

    bit_errors["distance"] = bit_errors["label"].str.replace(
        "cm", "", regex=False
    ).astype(float)

    # Sort distances

    summary = summary.sort_values("distance")
    packets = packets.sort_values("distance")
    bit_errors = bit_errors.sort_values("distance")

    # if summary.empty:
    #     print("No summary data available for additional plots.")
    #     return

    # summary = summary.copy()

    # # -------------------------------------------------
    # # Prepare distance values and sorting
    # # -------------------------------------------------

    # summary["distance"] = summary["label"].map(distance_cm)

    # if summary["distance"].isna().any():
    #     print("Warning: Some labels are not distances.")

    # summary = summary.sort_values("distance")

    plot_metric(
        summary,
        "bit_errors",
        "Number of Bit Errors",
        "Total Bit Errors vs Distance",
        "bit_errors_vs_distance",
    )

    plot_metric(
        summary,
        "bit_errors",
        "Number of Bit Errors",
        "Total Bit Errors vs Distance (Maximum)",
        "bit_errors_vs_distance_max",
        y_max=summary["bits_compared"].max()
    )

    summary["calculated_delivery"] = (
        summary["num_packets"] - summary["decoded_valid"]
    )
    plot_metric(
        summary,
        "calculated_delivery",
        "Number of Lost Packets",
        "Packet Loss vs Distance",
        "packet_loss_vs_distance.png"
    )

    plot_metric(
        summary,
        "calculated_delivery",
        "Number of Lost Packets",
        "Packet Loss vs Distance",
        "packet_loss_vs_distance_max.png",
        y_max=1000
    )

if __name__ == "__main__":
    main()
    test()
    plot_additional_summary_plots()