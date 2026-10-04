#!/usr/bin/env python3
"""Print the faulting-thread backtrace of the newest macOS crash report for ran_client.

macOS writes ~/Library/Logs/DiagnosticReports/ran_client-<date>.ips for every crash (also
SIGTRAP from clang's trap instructions). Usage: port/scripts/last_crash.py [report.ips]
"""
import glob
import json
import os
import sys


def main():
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        reports = glob.glob(os.path.expanduser("~/Library/Logs/DiagnosticReports/ran_client-*.ips"))
        if not reports:
            print("no ran_client crash reports")
            return 1
        path = max(reports, key=os.path.getmtime)
    with open(path) as f:
        _header, body = f.read().split("\n", 1)
    report = json.loads(body)
    print(path)
    exc = report.get("exception", {})
    print(f"{exc.get('type')} {exc.get('signal')} {exc.get('subtype', '')}".strip())
    images = report["usedImages"]
    thread = report["threads"][report.get("faultingThread", 0)]
    for frame in thread["frames"][:40]:
        image = images[frame["imageIndex"]].get("name", "?")
        symbol = frame.get("symbol", hex(frame["imageOffset"]))
        print(f"  {image:24} {symbol} +{frame.get('symbolLocation', 0)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
