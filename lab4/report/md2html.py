"""把本報告用到的 markdown 子集（標題、段落、清單、表格、程式碼區塊、行內 code/粗體/連結）轉成 HTML。"""
import html, re, sys

CSS = """
@page { size: A4; margin: 18mm 16mm; }
body { font-family: "PingFang TC", "Heiti TC", sans-serif; font-size: 11pt; line-height: 1.65; color: #111; }
h1 { font-size: 20pt; border-bottom: 2px solid #333; padding-bottom: 6px; }
h2 { font-size: 15pt; margin-top: 26px; border-bottom: 1px solid #bbb; padding-bottom: 3px; }
h3 { font-size: 12.5pt; margin-top: 18px; }
code { font-family: Menlo, "PingFang TC", monospace; font-size: 9.5pt; background: #f2f2f2; padding: 1px 4px; border-radius: 3px; }
pre { background: #f6f6f6; border: 1px solid #ddd; border-radius: 4px; padding: 8px 10px; white-space: pre-wrap; word-break: break-all; page-break-inside: avoid; }
pre code { background: none; padding: 0; font-size: 9pt; line-height: 1.45; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 10pt; page-break-inside: avoid; }
th, td { border: 1px solid #bbb; padding: 4px 7px; text-align: left; vertical-align: top; }
th { background: #eee; }
"""


def inline(s):
    parts = re.split(r"(`[^`]+`)", s)
    out = []
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
        else:
            p = html.escape(p)
            p = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", p)
            p = re.sub(r"\*(.+?)\*", r"<em>\1</em>", p)
            p = re.sub(r"(https?://[^\s<）)]+)", r'<a href="\1">\1</a>', p)
            out.append(p)
    return "".join(out)


def convert(md):
    lines = md.split("\n")
    out, i = [], 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            i += 1
            buf = []
            while i < len(lines) and not lines[i].startswith("```"):
                buf.append(lines[i]); i += 1
            out.append("<pre><code>" + html.escape("\n".join(buf)) + "</code></pre>")
            i += 1
        elif line.startswith("<!--"):
            while i < len(lines) and "-->" not in lines[i]:
                i += 1
            i += 1
        elif m := re.match(r"(#{1,6}) (.*)", line):
            n = len(m.group(1))
            out.append(f"<h{n}>{inline(m.group(2))}</h{n}>"); i += 1
        elif line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")]); i += 1
            t = ["<table><tr>" + "".join(f"<th>{inline(c)}</th>" for c in rows[0]) + "</tr>"]
            for r in rows[2:]:
                t.append("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>")
            out.append("".join(t) + "</table>")
        elif re.match(r"(- |\d+\. )", line):
            tag = "ul" if line.startswith("- ") else "ol"
            items = []
            while i < len(lines) and re.match(r"(- |\d+\. )", lines[i]):
                items.append("<li>" + inline(re.sub(r"^(- |\d+\. )", "", lines[i])) + "</li>"); i += 1
            out.append(f"<{tag}>" + "".join(items) + f"</{tag}>")
        elif line.strip() == "":
            i += 1
        else:
            buf = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(#|```|\||- |\d+\. |<!--)", lines[i]):
                buf.append(lines[i]); i += 1
            out.append("<p>" + inline(" ".join(buf)) + "</p>")
    return "\n".join(out)


src, dst = sys.argv[1], sys.argv[2]
body = convert(open(src, encoding="utf-8").read())
with open(dst, "w", encoding="utf-8") as f:
    f.write(f'<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8"><title>Lab 4 Report</title><style>{CSS}</style></head><body>\n{body}\n</body></html>')
