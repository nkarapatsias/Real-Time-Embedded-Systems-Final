import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ---------------------------------------------------------
# 1. Loading data
# ---------------------------------------------------------
file_path = r"A:\Desktop\University\8th_semester\ESPX\Final_Submit\metrics_log.txt"

df = pd.read_csv(file_path, skipinitialspace=True, low_memory=False)
df.columns = df.columns.str.strip()

numeric_cols = [
    "Seconds",
    "Nanoseconds",
    "Commit_Count",
    "Identity_Count",
    "Account_Count",
    "Info_Count",
    "Buffer_Occupancy_Pct",
    "CPU_Pct",
]

for col in numeric_cols:
    if col in df.columns:
        df[col] = pd.to_numeric(df[col], errors="coerce")

# ---------------------------------------------------------
# Time and jitter calculations 
# ---------------------------------------------------------
df["Timestamp"] = df["Seconds"] + (df["Nanoseconds"] * 1e-9)
df = df.dropna(subset=["Timestamp"]).reset_index(drop=True)

df["Relative_Time_s"] = df["Timestamp"] - df["Timestamp"].iloc[0]
df["Relative_Time_h"] = df["Relative_Time_s"] / 3600.0

df["Period_s"] = df["Timestamp"].diff()
df["Jitter_ms"] = (df["Period_s"] - 1.0) * 1000.0

# ---------------------------------------------------------
# Cpu load calculation
# ---------------------------------------------------------
df["Total_Msgs_Hz"] = (
    df["Commit_Count"]
    + df["Identity_Count"]
    + df["Account_Count"]
    + df["Info_Count"]
)
df["CPU_Idle_Pct"] = 100.0 - df["CPU_Pct"]

df_metrics = df.dropna(
    subset=["Total_Msgs_Hz", "Buffer_Occupancy_Pct", "CPU_Pct"]
).copy()

plt.rcParams.update({"font.size": 11, "figure.autolayout": True})

# ---------------------------------------------------------
# jitter plot
# ---------------------------------------------------------
jitter_data = df.dropna(subset=["Jitter_ms"]).iloc[5:].copy()

y_min = jitter_data["Jitter_ms"].quantile(0.005)
y_max = jitter_data["Jitter_ms"].quantile(0.995)
margin = max(abs(y_min), abs(y_max), 1.0) * 1.2

fig1, (ax1_ts, ax1_hist) = plt.subplots(
    1, 2, figsize=(14, 5), gridspec_kw={"width_ratios": [3.5, 1]}
)

ax1_ts.plot(
    jitter_data["Relative_Time_h"],
    jitter_data["Jitter_ms"],
    color="#1f77b4",
    linewidth=0.8,
    alpha=0.8,
    label="Thread Jitter",
)
ax1_ts.axhline(0, color="red", linestyle="--", linewidth=1.2, label="Ideal (1s)")

ax1_ts.set_xlim(0, 24)
ax1_ts.set_xticks(np.arange(0, 25, 2))
ax1_ts.set_xlabel("Time (hours)")
ax1_ts.set_ylim(-margin, margin)
ax1_ts.set_ylabel("Deviation from 1s (ms)")
ax1_ts.set_title("Thread Execution Jitter Over 24 Hours", fontsize=12, fontweight="bold")
ax1_ts.grid(True, linestyle=":", alpha=0.6)
ax1_ts.legend(loc="upper right")

ax1_hist.hist(
    jitter_data["Jitter_ms"],
    bins=60,
    range=(-margin, margin),
    color="#1f77b4",
    orientation="horizontal",
    edgecolor="black",
    linewidth=0.5,
)
ax1_hist.axhline(0, color="red", linestyle="--", linewidth=1.2)
ax1_hist.set_ylim(-margin, margin)
ax1_hist.set_title("Distribution", fontsize=12, fontweight="bold")
ax1_hist.set_xlabel("Count")
ax1_hist.tick_params(left=False, labelleft=False)
ax1_hist.grid(True, linestyle=":", alpha=0.6)

# ---------------------------------------------------------
# Cpu load and buffer plot
# ---------------------------------------------------------
fig2, ax2_left = plt.subplots(figsize=(12, 5))
ax2_right = ax2_left.twinx()

line1 = ax2_left.plot(
    df_metrics["Relative_Time_h"],
    df_metrics["Total_Msgs_Hz"],
    color="#2ca02c",
    linewidth=1.0,
    alpha=0.85,
    label="Message Rate (Hz)",
)
ax2_left.set_ylabel("Message Rate (Hz / msgs per sec)", color="#2ca02c")
ax2_left.tick_params(axis="y", labelcolor="#2ca02c")

line2 = ax2_right.plot(
    df_metrics["Relative_Time_h"],
    df_metrics["Buffer_Occupancy_Pct"],
    color="#d62728",
    linewidth=1.2,
    alpha=0.9,
    linestyle="-",
    label="Buffer Occupancy (%)",
)
ax2_right.set_ylabel("Buffer Occupancy (%)", color="#d62728")
ax2_right.tick_params(axis="y", labelcolor="#d62728")

ax2_left.set_xlim(0, 24)
ax2_left.set_xticks(np.arange(0, 25, 2))
ax2_left.set_xlabel("Time (hours)")

lines = line1 + line2
labels = [l.get_label() for l in lines]
ax2_left.legend(lines, labels, loc="upper right")

ax2_left.set_title("Network Load & Circular Buffer Occupancy Over 24 Hours", fontsize=12, fontweight="bold")
ax2_left.grid(True, linestyle=":", alpha=0.6)

# ---------------------------------------------------------
# Message rate & cpu load plot
# ---------------------------------------------------------
fig3, (ax3_busy, ax3_idle) = plt.subplots(1, 2, figsize=(13, 5))

ax3_busy.scatter(
    df_metrics["Total_Msgs_Hz"],
    df_metrics["CPU_Pct"],
    color="#9467bd",
    alpha=0.5,
    edgecolors="none",
    s=20,
)
ax3_busy.set_title("Message Rate vs CPU Utilization", fontsize=12, fontweight="bold")
ax3_busy.set_xlabel("Incoming Message Rate (Hz)")
ax3_busy.set_ylabel("CPU Utilization (%)")
ax3_busy.grid(True, linestyle=":", alpha=0.6)

ax3_idle.scatter(
    df_metrics["Total_Msgs_Hz"],
    df_metrics["CPU_Idle_Pct"],
    color="#17becf",
    alpha=0.5,
    edgecolors="none",
    s=20,
)
ax3_idle.set_title("Message Rate vs CPU Idle", fontsize=12, fontweight="bold")
ax3_idle.set_xlabel("Incoming Message Rate (Hz)")
ax3_idle.set_ylabel("CPU Idle (%)")
ax3_idle.grid(True, linestyle=":", alpha=0.6)

plt.show()