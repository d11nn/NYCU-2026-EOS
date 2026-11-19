#!/usr/bin/env python3
"""把 lab3-report.md（3.1）和 lab3-2-report.md（3.2）合成一份 lab3-report.pdf。

需要：pip install markdown-it-py pygments，以及 Chrome 或 Chromium（用無頭模式印成 PDF）。
用法：python3 lab3/report/build_report.py
"""
import html
import shutil
import subprocess
import sys
from pathlib import Path

from markdown_it import MarkdownIt
from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import TextLexer, get_lexer_by_name
from pygments.util import ClassNotFound

LAB3 = Path(__file__).resolve().parent.parent
PARTS = ['lab3-report.md', 'lab3-2-report.md']      # 3.1、3.2
OUT_PDF = LAB3 / 'lab3-report.pdf'
OUT_HTML = LAB3 / 'lab3-report.html'                # 中間檔，印完就刪

FORMATTER = HtmlFormatter(style='one-dark', nowrap=True)

CSS = """
@page { size: A4; margin: 16mm 15mm 16mm 15mm; }
body { font-family: "Noto Sans CJK TC", "Noto Sans TC", "PingFang TC", "Microsoft JhengHei", sans-serif;
       font-size: 10.5pt; line-height: 1.7; color: #1f2328; }
h1 { font-size: 20pt; margin: 0 0 10pt; }
h2 { font-size: 16pt; margin: 20pt 0 8pt; }
h3 { font-size: 12.5pt; margin: 16pt 0 6pt; }
h1, h2, h3 { break-after: avoid; }
section.part + section.part { break-before: page; }
p { margin: 7pt 0; }
strong { color: #e03131; }
img { max-width: 100%; display: block; margin: 8pt auto; }
hr { border: 0; border-top: 1px solid #d8dee4; margin: 16pt 0; }
code { font-family: "Noto Sans Mono CJK TC", "DejaVu Sans Mono", monospace; font-size: 9pt; color: #d9480f; }
pre { background: #282c34; color: #abb2bf; border-radius: 6px; padding: 9pt 11pt; margin: 9pt 0;
      font-size: 8pt; line-height: 1.5; white-space: pre-wrap; overflow-wrap: anywhere; break-inside: auto; }
pre code { color: inherit; font-size: inherit; }
blockquote { margin: 9pt 0; padding: 4pt 11pt; border-left: 4px solid #4c8bf5; background: #eef4ff; }
table { border-collapse: collapse; width: 100%; margin: 9pt 0; font-size: 9.5pt; break-inside: avoid; }
th, td { border: 1px solid #d8dee4; padding: 4pt 7pt; text-align: left; vertical-align: top; }
th { background: #f6f8fa; }
tr { break-inside: avoid; }
ul, ol { margin: 7pt 0; padding-left: 18pt; }
li { margin: 2pt 0; }
pre { orphans: 4; widows: 4; }
th { white-space: nowrap; }
th, td { padding: 3pt 7pt; }
section.part + section.part p:has(+ table), section.part + section.part p:has(+ pre) { break-after: avoid; }
"""


def highlight_code(code, lang, _attrs):
    try:
        lexer = get_lexer_by_name(lang) if lang else TextLexer()
    except ClassNotFound:
        lexer = TextLexer()
    return highlight(code, lexer, FORMATTER)


def find_chrome():
    for name in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'):
        path = shutil.which(name)
        if path:
            return path
    sys.exit('找不到 Chrome / Chromium')


def main():
    md = MarkdownIt('commonmark', {'highlight': highlight_code}).enable('table')
    parts = []
    for name in PARTS:
        parts.append('<section class="part">\n' + md.render((LAB3 / name).read_text(encoding='utf-8')) + '</section>')
    page = ('<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8"><title>eos-lab3</title><style>'
            + CSS + HtmlFormatter(style='one-dark').get_style_defs('pre') + '</style></head><body>'
            + '\n'.join(parts) + '</body></html>')
    OUT_HTML.write_text(page, encoding='utf-8')
    subprocess.run([find_chrome(), '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                    '--print-to-pdf=' + str(OUT_PDF), OUT_HTML.as_uri()], check=True)
    OUT_HTML.unlink()
    print('wrote', OUT_PDF)


if __name__ == '__main__':
    main()
