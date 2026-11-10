# Font Awesome subset

The site ships only the Font Awesome icons it uses.
This script cuts the full Font Awesome down to those icons.

| Path | Content |
|---|---|
| `source/` | full Font Awesome Free 6.7.2: CSS and woff2 fonts. Do not edit. |
| `assets/css/fontawesome-all.min.css` | generated: only the used icon rules |
| `assets/webfonts/*.woff2` | generated: only the used glyphs |

Jekyll ignores folders that start with `_`, so `_tools/` does not get into the site.

## When to run

Run the script after you add an icon anywhere:
- `_config.yml` (author or footer links);
- `_data/navigation.yml`;
- `_includes/`, `_layouts/`, `assets/js/_main.js`;
- a post.

If you do not run it, the new icon shows as an empty square.

## How to run

1. Install the tools once:
   ```
   python3 -m venv ~/.venvs/fonttools
   ~/.venvs/fonttools/bin/pip install fonttools brotli
   ```
2. Build the site:
   ```
   bundle exec jekyll build
   ```
3. Run the script. It reads `_site/` by default:
   ```
   ~/.venvs/fonttools/bin/python _tools/fa_subset/subset.py
   ```
4. Check the list of icons that the script prints.
5. Commit the changed files in `assets/css/` and `assets/webfonts/`.

## Limits

- The script finds icons by the `fa-<name>` classes in the built HTML and JS.
  An icon that a template shows only under a condition (for example, `far fa-clock` in `page__meta.html`
  when `show_date` is on) gets into the subset only when the build renders it.
  After you change such a setting, run the script again.
- To update Font Awesome, replace the files in `source/` and run the script.
