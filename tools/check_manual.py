#!/usr/bin/env python3
"""
check_manual.py - check that the manual's code figures still match the source.

    python tools/check_manual.py          list any figure whose code has changed
    python tools/check_manual.py --fix    quote those figures again from the source

docs/manual/manual.html quotes source files by line number, and nothing updates
those quotes when a file changes. This reads every figure, takes the lines it
claims to quote from the file as it is now (or, for a figure marked "at tag ...",
from that git tag), and compares them. The exit status is 1 if any differ.

--fix looks for each stale figure's code in the current file, by its first and
last lines, and quotes it again with the right line numbers. It prints what moved.
Read the diff afterwards: a figure whose code was rewritten can't be found again
automatically, so it is reported and left alone.
"""
import argparse
import difflib
import html
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANUAL = os.path.join(ROOT, "docs", "manual", "manual.html")

FIGURE = re.compile(
    r'(?P<head><figcaption><span class="file">(?P<file>[^<]+)</span>'
    r'(?:<span class="tag">at tag (?P<tag>[^<]*)</span>)?'
    r'(?:<span class="what">[^<]*</span>)?'
    r'<span class="lines">lines )(?P<first>\d+)&ndash;(?P<last>\d+)'
    r'(?P<mid></span></figcaption><pre>)(?P<body>.*?)(?P<tail></pre>)', re.S)
LINE = re.compile(r'<span class="l" data-n="(\d+)">(.*?)</span>(?=<span class="l" |$)', re.S)


# ---- reading the source -------------------------------------------------------

def source_lines(name, tag):
    """The lines of a file, now or at a git tag, with tabs shown as the manual shows them."""
    if tag:
        text = subprocess.run(["git", "show", f"{tag}:{name}"], cwd=ROOT,
                              capture_output=True, text=True, encoding="utf-8").stdout
    else:
        with open(os.path.join(ROOT, name), encoding="utf-8", newline="") as f:
            text = f.read()
    return [line.expandtabs(8) for line in text.split("\n")]


def quoted_lines(body):
    """The figure's lines as plain text: [(line number, text), ...]."""
    return [(int(n), html.unescape(re.sub(r"<[^>]*>", "", fragment)))
            for n, fragment in LINE.findall(body)]


# ---- writing a figure: the same markup the manual uses ------------------------

def escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def comment(before, text):
    return escape(before) + f'<span class="c">{escape(text)}</span>'


def mark_hash(line):
    """CMake and Python: a comment starts at a # that isn't inside quotes."""
    quote = None
    for i, ch in enumerate(line):
        if quote:
            quote = None if ch == quote else quote
        elif ch in "'\"":
            quote = ch
        elif ch == "#":
            return comment(line[:i], line[i:])
    return escape(line)


def mark_c(line, in_comment):
    """C, assembly and the linker script: /* ... */ and // comments.
    Returns the marked-up line and whether a /* comment is still open."""
    if in_comment:
        end = line.find("*/")
        if end == -1:
            return comment("", line), True
        return comment("", line[:end + 2]) + escape(line[end + 2:]), False
    starts = [i for i in (line.find("/*"), line.find("//")) if i != -1]
    if not starts:
        return escape(line), False
    at = min(starts)
    if line.startswith("//", at):
        return comment(line[:at], line[at:]), False
    end = line.find("*/", at + 2)
    if end == -1:
        return comment(line[:at], line[at:]), True
    return comment(line[:at], line[at:end + 2]) + escape(line[end + 2:]), False


def render(name, lines, first, last):
    """The <pre> contents quoting lines first..last."""
    hash_comments = os.path.basename(name) == "CMakeLists.txt" or name.endswith((".cmake", ".py"))
    in_comment = False
    out = []
    for n, line in enumerate(lines[:last], start=1):
        if hash_comments:
            marked = mark_hash(line)
        else:
            marked, in_comment = mark_c(line, in_comment)
        if n >= first:
            out.append(f'<span class="l" data-n="{n}">{marked}</span>')
    return "".join(out)


# ---- finding a figure's code again ---------------------------------------------

def find_again(quoted, lines):
    """Where the quoted code is now, as (first, last), or None.

    The first line must be unique. The last line is often just "}", so take the end
    that makes the block most like the figure, allowing for lines added or edited."""
    starts = [i for i, line in enumerate(lines) if line == quoted[0]]
    if len(starts) != 1:
        return None
    first = starts[0] + 1
    best, last = 0.0, None
    for end in range(first + len(quoted) - 1, min(first + len(quoted) + 80, len(lines) + 1)):
        if lines[end - 1] == quoted[-1]:
            score = difflib.SequenceMatcher(None, quoted, lines[first - 1:end]).ratio()
            if score > best:
                best, last = score, end
    return (first, last) if last and best > 0.6 else None


def main():
    parser = argparse.ArgumentParser(description="Check the manual's code figures against the source.")
    parser.add_argument("--fix", action="store_true", help="quote stale figures again from the source")
    args = parser.parse_args()

    with open(MANUAL, encoding="utf-8", newline="") as f:
        manual = f.read()

    checked, stale, edits = 0, 0, []
    for m in FIGURE.finditer(manual):
        name, tag = m["file"], m["tag"]
        first, last = int(m["first"]), int(m["last"])
        lines = source_lines(name, tag)
        quoted = quoted_lines(m["body"])
        checked += 1
        if [text for _, text in quoted] == lines[first - 1:last]:
            continue

        stale += 1
        where = f"{name}{' at ' + tag if tag else ''}, lines {first}-{last}"
        place = None if tag else find_again([text for _, text in quoted], lines)
        if args.fix and place:
            a, b = place
            edits.append((m.start(), m.end(),
                          f'{m["head"]}{a}&ndash;{b}{m["mid"]}{render(name, lines, a, b)}{m["tail"]}'))
            print(f"fixed     {where}  ->  lines {a}-{b}")
            continue
        print(f"CHANGED   {where}")
        for n, text in quoted:
            now = lines[n - 1] if n <= len(lines) else "(past the end of the file)"
            if text != now:
                print(f"          line {n} in the manual: {text!r}")
                print(f"          line {n} in the file:   {now!r}")
                break
        if args.fix:
            print("          can't find this code in the file any more: quote it again by hand")

    if edits:
        for start, end, new in reversed(edits):
            manual = manual[:start] + new + manual[end:]
        with open(MANUAL, "w", encoding="utf-8", newline="") as f:
            f.write(manual)

    left = stale - len(edits)
    print(f"\n{checked} figures checked: {checked - stale} match, "
          f"{len(edits)} fixed, {left} {'need' if left != 1 else 'needs'} attention")
    sys.exit(1 if left else 0)


if __name__ == "__main__":
    main()
