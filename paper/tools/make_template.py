"""Write build/template_zh.docx: the document-PC skill's template with Chinese fonts.

    py paper/tools/make_template.py

The skill's template (ACM style) names Latin faces for East Asian text, so
Word picks its own fallback for every Chinese character. This copy sets
SimSun for body styles and SimHei for the styles set in the sans face
(title, headings, captions), marks the text language as zh-CN and sets the
page margins to one inch on every side (the ACM page keeps almost two inches
free at the foot, which reads as a blank band on every page of a draft).
The copy is a build product (build/ is not tracked); the styles and their
names are untouched.
"""
import re
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAPER = HERE.parent
SKILL = Path.home() / ".claude" / "skills" / "document-PC"
SOURCE = SKILL / "assets" / "template.docx"
OUT = PAPER / "build" / "template_zh.docx"
BODY, SANS = "宋体", "黑体"
MARGINS = ('<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" '
           'w:header="706" w:footer="706" w:gutter="0"/>')


def east_asian(match):
    tag = match.group(0)
    face = SANS if "Biolinum" in tag else BODY
    if "w:eastAsia=" in tag:
        return re.sub(r'w:eastAsia="[^"]*"', 'w:eastAsia="%s"' % face, tag)
    return tag.replace("<w:rFonts", '<w:rFonts w:eastAsia="%s"' % face, 1)


def main():
    if not SOURCE.exists():
        sys.exit("no template at %s" % SOURCE)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(SOURCE) as src, zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            data = src.read(item.filename)
            if item.filename == "word/styles.xml":
                text = data.decode("utf-8")
                text, n = re.subn(r"<w:rFonts\b[^>]*>", east_asian, text)
                text = re.sub(r'(<w:lang\b[^>]*w:eastAsia=")[^"]*(")', r"\1zh-CN\2", text)
                data = text.encode("utf-8")
                print("styles.xml: %d font declarations set" % n)
            elif item.filename == "word/document.xml":
                text = data.decode("utf-8")
                text, m = re.subn(r'<w:pgMar [^>]*/>', MARGINS, text)
                data = text.encode("utf-8")
                print("document.xml: margins set in %d section(s)" % m)
            dst.writestr(item, data)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
