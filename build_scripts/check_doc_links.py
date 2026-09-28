"""Check that every backtick-quoted `.md` path in the docs exists.

Scans docs/**/*.md plus the root README.md, AGENTS.md and CLAUDE.md for
references written as `path/to/file.md` (optionally with a trailing #anchor)
and resolves each one relative to the referencing file's directory, then to
docs/, then to the repository root. Prints every unresolved reference and
exits 1 if there is any.

Run from anywhere:  py build_scripts\\check_doc_links.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
REF = re.compile(r"`([^`\s<>|*]+?\.md)(?:#[^`]*)?`")


def files_to_scan():
    yield from sorted(DOCS.rglob("*.md"))
    for name in ("README.md", "AGENTS.md", "CLAUDE.md"):
        path = ROOT / name
        if path.exists():
            yield path


def resolve(ref, base_dir):
    candidates = [base_dir / ref, DOCS / ref, ROOT / ref]
    return any(candidate.exists() for candidate in candidates)


def main():
    missing = []
    total = 0
    for path in files_to_scan():
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            for match in REF.finditer(line):
                ref = match.group(1)
                # Placeholders such as <工具>.md are documentation, not paths.
                if "<" in ref or "…" in ref:
                    continue
                total += 1
                if not resolve(ref, path.parent):
                    missing.append((path.relative_to(ROOT), lineno, ref))
    for rel, lineno, ref in missing:
        print(f"MISSING  {rel}:{lineno}  `{ref}`")
    print(f"checked {total} references, {len(missing)} missing")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
