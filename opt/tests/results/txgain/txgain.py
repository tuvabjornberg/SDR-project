
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

df = pd.read_csv("outlog/summary.csv")
df = df.sort_values("tx_gain")

modes = ["RX0 only", "RX1 only", "RX0+RX1 (MRC)"]

colors = {
    "RX0 only": "#2878B5",
    "RX1 only": "#E87524",
    "RX0+RX1 (MRC)": "#269B62"
}

markers = {
    "RX0 only": "o",
    "RX1 only": "s",
    "RX0+RX1 (MRC)": "^"
}

plt.rcParams.update({
    "font.size": 12,
    "figure.figsize": (9, 5.5),
    "axes.grid": True,
    "grid.alpha": 0.3
})

df["pdr_percent"] = df["pdr_exact"] * 100

def plot_metric(column, ylabel, title, filename,
                ylim=None, log=False):

    fig, ax = plt.subplots()

    for mode in modes:
        subset = df[df["variant"] == mode].sort_values("tx_gain")

        y = subset[column].copy()

        if log:
            y = y.replace(0, np.nan)

        ax.plot(
            subset["tx_gain"],
            y,
            marker=markers[mode],
            color=colors[mode],
            linewidth=2,
            markersize=7,
            label=mode
        )

    ax.set_xlabel("TX Gain")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.set_xticks(sorted(df["tx_gain"].unique()))

    if ylim:
        ax.set_ylim(*ylim)

    if log:
        ax.set_yscale("log")

    ax.legend()
    ax.grid(True, which="both", alpha=0.3)

    plt.tight_layout()
    plt.savefig(filename, dpi=300, bbox_inches="tight")
    plt.show()

plot_metric(
    "pdr_percent",
    "Exact PDR (%)",
    "Packet Delivery Ratio vs TX Gain",
    "pdr_vs_tx_gain.png",
    ylim=(0, 105)
)

plot_metric(
    "ber",
    "Bit Error Rate",
    "BER vs TX Gain",
    "ber_vs_tx_gain.png",
    log=True
)

plot_metric(
    "exact",
    "Successfully Decoded Packets",
    "Exact Packets Decoded vs TX Gain",
    "decoded_packets_vs_tx_gain.png",
    ylim=(0, 52)
)

plot_metric(
    "bit_errors",
    "Bit Errors",
    "Bit Errors vs TX Gain",
    "bit_errors_vs_tx_gain.png"
)

print("All plots generated!")
