import re
import csv

LOG_FILE = "baseline-2sdr/run.txt"
OUTPUT_FILE = "invalid_packets.csv"

with open(LOG_FILE, "r") as f: lines = f.readlines()

received = set() 
reading_received = False

for line in lines: 
    line = line.strip()

    if line.startswith("Received:"):
        reading_received = True

    # Handle packet numbers on the same line as Received:
        numbers = re.findall(r"\d+", line.replace("Received:", "", 1))
        received.update(map(int, numbers))
        continue

    if reading_received:
        # Stop when another section begins
        if line.startswith("RX breakdown:"):
            break

        numbers = re.findall(r"\d+", line)
        received.update(map(int, numbers))

print(f"Received packets extracted: {len(received)}")

#with open(LOG_FILE, "r") as f:
#    lines = f.readlines()

sequence = 0

with open(OUTPUT_FILE, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["Sequence Number", "Error Type", "Got"])

    for line in lines:
        line = line.strip()

        if not line.startswith("[Parser]:"):
            continue

        # Skip sequence numbers already received.
        while sequence in received:
            sequence += 1

        preamble = re.search(r"Got:\s*(0x[0-9a-fA-F]+)", line)
        length = re.search(r"Got:\s*([0-9a-fA-F]+)\s*$", line)

        if "invalid preamble" in line:
            error_type = "invalid preamble"
            got = preamble.group(1) if preamble else ""
        elif "payload length exceeds maximum" in line:
            error_type = "payload length exceeds maximum"
            got = length.group(1) if length else ""
        else:
            continue

        writer.writerow([sequence, error_type, got])
        sequence += 1

print(f"CSV written to {OUTPUT_FILE}")