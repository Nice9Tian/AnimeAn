"""Build the paper: figures (optional), the Chinese-font template, then the document-PC pipeline.

    py paper/build.py              # template + .docx + check copy + Markdown copy
    py paper/build.py --figures    # redraw assets/src/ first
    py paper/build.py --pages      # also ask Word for the page count and a PDF

The pipeline is the document-PC skill (~/.claude/skills/document-PC); this file only
chains the project's own two steps in front of it.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SKILL = Path.home() / ".claude" / "skills" / "document-PC" / "scripts"


def run(*args):
    print("$", " ".join(str(a) for a in args[1:]))
    code = subprocess.run([str(a) for a in args], cwd=HERE).returncode
    if code:
        sys.exit(code)


def main():
    flags = set(sys.argv[1:])
    if "--figures" in flags:
        run(sys.executable, HERE / "figures" / "make_figures.py")
    run(sys.executable, HERE / "tools" / "make_template.py")
    run(sys.executable, SKILL / "build.py", "--config", HERE / "docbuild.json")
    if "--pages" in flags:
        run("powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            SKILL / "word_pages.ps1", "-Docx", HERE / "output" / "animean_paper.docx",
            "-Pdf", HERE / "output" / "animean_paper.pdf")


if __name__ == "__main__":
    main()
