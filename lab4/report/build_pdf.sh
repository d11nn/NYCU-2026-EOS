#!/bin/bash
# 把 lab4_report.md 轉成 lab4_report.pdf（markdown → HTML → Chrome 列印成 PDF）
set -e
cd "$(dirname "$0")"
python3 md2html.py lab4_report.md lab4_report.html
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --no-pdf-header-footer --print-to-pdf="$PWD/lab4_report.pdf" "file://$PWD/lab4_report.html" 2>/dev/null
echo "done: $PWD/lab4_report.pdf"
