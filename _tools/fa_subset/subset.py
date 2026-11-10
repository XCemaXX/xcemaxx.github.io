#!/usr/bin/env python3
"""Cut Font Awesome down to the icons the built site uses. See README.md."""
import pathlib
import re
import sys

from fontTools import subset

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = pathlib.Path(__file__).resolve().parent / "source"
CSS_OUT = REPO / "assets/css/fontawesome-all.min.css"
FONTS_OUT = REPO / "assets/webfonts"
FONTS = ["fa-solid-900.woff2", "fa-regular-400.woff2", "fa-brands-400.woff2"]

ICON_RULE = re.compile(r'((?:\.fa-[a-z0-9-]+,?)+)\{--fa:"((?:\\[0-9a-f]+)+)"\}')
FONT_FACE = re.compile(r"@font-face\{[^}]*\}")
TTF_SRC = re.compile(r',url\([^)]*\.ttf\) format\("truetype"\)')


def used_classes(site):
    classes = set()
    for path in site.rglob("*"):
        if path.suffix in (".html", ".js"):
            classes.update(re.findall(r"\bfa-[a-z0-9-]+", path.read_text(errors="ignore")))
    return classes


def main():
    site = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else REPO / "_site")
    if not (site / "index.html").exists():
        sys.exit(f"no built site in {site}: run `bundle exec jekyll build` first")
    used = used_classes(site)

    css = (SRC / "fontawesome-all.min.css").read_text()
    codepoints, icons = set(), set()

    def keep_icon(m):
        names = {s[1:] for s in m.group(1).split(",")}
        if not names & used:
            return ""
        icons.update(names & used)
        codepoints.update(int(h, 16) for h in m.group(2).split("\\")[1:])
        return m.group(0)

    css = ICON_RULE.sub(keep_icon, css)
    # Only woff2 is shipped; every browser that runs this site reads it.
    css = FONT_FACE.sub(
        lambda m: "" if "v4compatibility" in m.group(0) else TTF_SRC.sub("", m.group(0)), css
    )
    CSS_OUT.write_text(css)

    for name in FONTS:
        opts = subset.Options()
        opts.flavor = "woff2"
        opts.layout_features = ["*"]
        font = subset.load_font(SRC / name, opts)
        sub = subset.Subsetter(opts)
        sub.populate(unicodes=codepoints)
        sub.subset(font)
        subset.save_font(font, FONTS_OUT / name, opts)

    print(f"{len(icons)} icons: {' '.join(sorted(icons))}")
    for path in [CSS_OUT] + [FONTS_OUT / n for n in FONTS]:
        print(f"{path.relative_to(REPO)}: {path.stat().st_size} B")


if __name__ == "__main__":
    main()
