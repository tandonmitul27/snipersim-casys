#!/usr/bin/env python3
"""
dump_stats.py <resultsdir>

Reads sim.stats.sqlite3 from <resultsdir> and writes every metric value
for every snapshot to <resultsdir>/dumpstats.txt.
"""

import sys, os

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import sniper_stats

def main():
    if len(sys.argv) < 2:
        print("Usage: dump_stats.py <resultsdir>", file=sys.stderr)
        sys.exit(1)

    resultsdir = sys.argv[1]
    outfile = os.path.join(resultsdir, "dumpstats.txt")

    stats = sniper_stats.SniperStats(resultsdir=resultsdir)
    snapshots = stats.get_snapshots()
    names = stats.names  # {metric_id: (component, metric_name)}

    with open(outfile, "w") as f:
        for snap in snapshots:
            f.write(f"{'='*60}\n")
            f.write(f"SNAPSHOT: {snap}\n")
            f.write(f"{'='*60}\n")

            data = stats.read_snapshot(snap)  # {metric_id: {core: value}}

            # Group by component for readability
            rows = []
            for metric_id, core_vals in data.items():
                component, metric = names.get(metric_id, ("?", "?"))
                for core, value in sorted(core_vals.items()):
                    rows.append((component, core, metric, value))

            rows.sort()
            for component, core, metric, value in rows:
                f.write(f"  {component}[{core}].{metric} = {value}\n")

            f.write("\n")

    print(f"Stats dumped to: {outfile}")

if __name__ == "__main__":
    main()
