#!/usr/bin/env python3
"""Writes the online manual: docs/wiki as GitHub wiki pages, into a clone of the project's wiki repository.

    tools/publish-wiki.py <docs/wiki of the public copy> <wiki clone>

docs/wiki stays the one source (edit there; the app's Help reads the same pages). Every page but the tutorials, which
ship with the app, becomes <file>.md in the clone, with:
  - [[Title]], [[Title#Heading]], [[Title#Heading|label]] and [[#Heading]] as Markdown links (a tutorial's to its file
    in the repository, since the wiki does not hold them);
  - example: and target: links (projects the app opens) to the project file in the repository;
  - the Category and Order lines and the page's own "# Title" dropped (the wiki shows the page name);
  - the pictures of those pages (images/<page>/) copied beside them;
and Home.md (the front page, vats.md), _Sidebar.md (the contents by category) and _Footer.md written. Pages the clone
holds that docs/wiki no longer has are removed. Nothing is committed or pushed: review the clone, then push it.
"""
import os
import re
import shutil
import sys

REPO = "https://github.com/squeedledorf/viewport-avatar-toolset/blob/main/docs/wiki"
ORDER = ["Getting started", "Interface", "Animating", "Motion", "Import and export", "Second Life", "Viewer",
         "Reference", "Troubleshooting"]


def anchor(heading):
    out = ""
    for c in heading.strip().lower():
        if c.isalnum() or c in "-_" or ord(c) >= 0x80:
            out += c
        elif c == " ":
            out += "-"
    return out


def skipped(stem):
    return stem in ("STYLE", "README") or stem.startswith("tutorial") or re.match(r"hex.*-import$", stem)


def main(src, dst):
    pages = {}  # stem -> (title, category, order, text)
    for name in sorted(os.listdir(src)):
        if not name.endswith(".md"):
            continue
        stem = name[:-3]
        if stem in ("STYLE", "README"):
            continue
        text = open(os.path.join(src, name), encoding="utf-8").read()
        title = next((l[2:].strip() for l in text.splitlines() if l.startswith("# ")), stem)
        cat = next((l[10:].strip() for l in text.splitlines() if l.startswith("Category: ")), "")
        order = next((int(l[7:]) for l in text.splitlines() if re.match(r"Order: \d+$", l)), 0)
        pages[stem] = (title, cat, order, text)
    by_title = {t.lower(): s for s, (t, *_ ) in pages.items()}

    def target(name):
        name = name.strip()
        stem = name if name in pages else by_title.get(name.lower())
        if stem is None:
            return None
        if stem == "vats":
            return "Home"
        return f"{REPO}/{stem}.md" if skipped(stem) else stem

    def wiki_link_text(inner):
        inner = " ".join(inner.split())  # a link wrapped over two lines
        page, _, label = inner.partition("|")
        page, _, head = page.partition("#")
        if not page:  # [[#Heading]]: on this page
            return f"[{label or head}](#{anchor(head)})"
        to = target(page)
        text = label or (page if not head else f"{page}: {head}")
        if to is None:
            return text
        return f"[{text}]({to}{'#' + anchor(head) if head else ''})"

    def convert(stem, text):
        lines = []
        dropped_title = False
        for line in text.splitlines():
            if not dropped_title and line.startswith("# "):
                dropped_title = True
                continue
            if re.match(r"(Category: .*|Order: \d+)$", line):
                continue
            lines.append(line)
        # Links only outside code blocks (a JSON sample's [[0, 1]] is not one); a link may wrap onto the next line.
        parts = re.split(r"(^```.*?^```[^\n]*$)", "\n".join(lines), flags=re.S | re.M)
        for i in range(0, len(parts), 2):
            parts[i] = re.sub(r"\[\[([^\]\n]+(?:\n[^\]\n]+)?)\]\]", lambda m: wiki_link_text(m.group(1)), parts[i])
            parts[i] = re.sub(r"\]\((?:example|target):([^)\s]+)\)", lambda m: f"]({REPO}/examples/{m.group(1)})", parts[i])
        return "".join(parts).strip() + "\n"

    os.makedirs(dst, exist_ok=True)
    written = set()
    for stem, (title, cat, order, text) in pages.items():
        if skipped(stem):
            continue
        name = "Home.md" if stem == "vats" else f"{stem}.md"
        open(os.path.join(dst, name), "w", encoding="utf-8").write(convert(stem, text))
        written.add(name)
        pics = os.path.join(src, "images", stem)
        if os.path.isdir(pics):
            shutil.copytree(pics, os.path.join(dst, "images", stem), dirs_exist_ok=True)
    # The contents, by category in the style guide's order.
    cats = {}
    for stem, (title, cat, order, _) in pages.items():
        if not skipped(stem) and stem != "vats":
            cats.setdefault(cat or "Other", []).append((order or 1000, title, stem))
    side = ["**[Viewport Avatar Toolset](Home)**", ""]
    for cat in ORDER + sorted(c for c in cats if c not in ORDER):
        if cat in cats:
            side.append(f"**{cat}**")
            side += [f"- [{t}]({s})" for _, t, s in sorted(cats[cat])]
            side.append("")
    side.append("The tutorials are in VATs itself: **Help → Tutorials**.")
    open(os.path.join(dst, "_Sidebar.md"), "w", encoding="utf-8").write("\n".join(side) + "\n")
    open(os.path.join(dst, "_Footer.md"), "w", encoding="utf-8").write(
        "This manual is made from [docs/wiki](https://github.com/squeedledorf/viewport-avatar-toolset/tree/main/docs/wiki) "
        "in the repository; changes go there. VATs can download it for offline reading: **Help → Download the Manual "
        "for Offline Reading**.\n")
    written |= {"_Sidebar.md", "_Footer.md"}
    for name in os.listdir(dst):  # pages docs/wiki no longer has
        if name.endswith(".md") and name not in written:
            os.remove(os.path.join(dst, name))
    print(f"wrote {len(written) - 2} pages, the sidebar and the footer into {dst}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
