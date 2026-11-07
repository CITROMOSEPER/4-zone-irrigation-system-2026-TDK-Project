#!/bin/sh
# Browser tests of the web UI (needs: pip install playwright && playwright install chromium)
cd "$(dirname "$0")/../.." && python3 tools/build_pages.py >/dev/null && python3 test/ui/run.py
