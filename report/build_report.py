#!/usr/bin/env python3
"""Render report/report.md to report/Report_106123015.pdf with reportlab.

Supports the Markdown subset used in report.md: headings (#, ##, ###), paragraphs,
bullet / numbered lists, pipe tables, fenced code blocks, images (![caption](path)),
inline **bold**, *italic* and `code`, and a horizontal rule (---) as a page break.
Usage: python3 report/build_report.py [report.md] [out.pdf]
"""
import os
import re
import sys
import html

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.platypus import (Image, ListFlowable, ListItem, PageBreak, Paragraph, Preformatted,
                                SimpleDocTemplate, Spacer, Table, TableStyle, KeepTogether)

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "report.md")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "Report_106123015.pdf")
ROOT = os.path.dirname(HERE)

ss = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=ss["Normal"], fontName="Helvetica", fontSize=10.5, leading=14.5,
                      alignment=TA_JUSTIFY, spaceAfter=6)
H1 = ParagraphStyle("h1", parent=ss["Heading1"], fontName="Helvetica-Bold", fontSize=16, leading=20,
                    spaceBefore=14, spaceAfter=8, textColor=colors.HexColor("#1a1a1a"))
H2 = ParagraphStyle("h2", parent=ss["Heading2"], fontName="Helvetica-Bold", fontSize=13, leading=17,
                    spaceBefore=10, spaceAfter=5, textColor=colors.HexColor("#1a1a1a"))
H3 = ParagraphStyle("h3", parent=ss["Heading3"], fontName="Helvetica-Bold", fontSize=11, leading=14,
                    spaceBefore=8, spaceAfter=4)
CODE = ParagraphStyle("code", parent=ss["Code"], fontName="Courier", fontSize=8.2, leading=10.2,
                      backColor=colors.HexColor("#f4f4f4"), borderPadding=5, leftIndent=4, spaceAfter=8)
CAPTION = ParagraphStyle("cap", parent=BODY, fontSize=9, leading=12, alignment=TA_CENTER,
                         textColor=colors.HexColor("#444444"), spaceAfter=10)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=8.8, leading=11, alignment=0, spaceAfter=0)
CELLH = ParagraphStyle("cellh", parent=CELL, fontName="Helvetica-Bold")
TITLE = ParagraphStyle("title", parent=H1, fontSize=20, leading=26, alignment=TA_CENTER, spaceAfter=4)
SUB = ParagraphStyle("sub", parent=BODY, fontSize=12, leading=16, alignment=TA_CENTER, spaceAfter=2)


def inline(text):
    """Markdown inline -> reportlab mini-HTML."""
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r'<font face="Courier" size="9">\1</font>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<![\w*])\*([^*]+)\*(?![\w*])", r"<i>\1</i>", text)
    return text


def table_flowable(rows):
    data = []
    for i, r in enumerate(rows):
        data.append([Paragraph(inline(c.strip()), CELLH if i == 0 else CELL) for c in r])
    ncols = max(len(r) for r in data)
    for r in data:
        while len(r) < ncols:
            r.append(Paragraph("", CELL))
    avail = A4[0] - 4 * cm
    t = Table(data, colWidths=[avail / ncols] * ncols, repeatRows=1, hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e8e8e8")),
        ("LINEBELOW", (0, 0), (-1, 0), 0.8, colors.HexColor("#555555")),
        ("LINEBELOW", (0, 1), (-1, -1), 0.3, colors.HexColor("#cccccc")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
    ]))
    return t


def build(lines):
    flow = []
    i = 0
    para = []
    title_block = []

    def flush_para():
        nonlocal para
        if para:
            flow.append(Paragraph(inline(" ".join(para)), BODY))
            para = []

    while i < len(lines):
        line = lines[i].rstrip("\n")
        s = line.strip()
        if s.startswith("```"):
            flush_para()
            i += 1
            buf = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                buf.append(lines[i].rstrip("\n"))
                i += 1
            i += 1
            flow.append(Preformatted("\n".join(buf), CODE))
            continue
        if s.startswith("%%title "):
            flow.append(Paragraph(inline(s[8:]), TITLE)); i += 1; continue
        if s.startswith("%%sub "):
            flow.append(Paragraph(inline(s[6:]), SUB)); i += 1; continue
        if s == "%%pagebreak" or s == "---":
            flush_para(); flow.append(PageBreak()); i += 1; continue
        if s.startswith("%%space"):
            flush_para(); flow.append(Spacer(1, float(s.split()[1]) * cm if len(s.split()) > 1 else 0.5 * cm)); i += 1; continue
        m = re.match(r"^(#{1,3})\s+(.*)$", s)
        if m:
            flush_para()
            style = {1: H1, 2: H2, 3: H3}[len(m.group(1))]
            flow.append(Paragraph(inline(m.group(2)), style))
            i += 1
            continue
        m = re.match(r"^!\[(.*?)\]\((.*?)\)$", s)
        if m:
            flush_para()
            path = os.path.join(ROOT, m.group(2))
            if os.path.exists(path):
                img = Image(path)
                maxw = A4[0] - 4 * cm
                scale = min(1.0, maxw / img.imageWidth)
                img.drawWidth = img.imageWidth * scale
                img.drawHeight = img.imageHeight * scale
                flow.append(KeepTogether([img, Paragraph(inline(m.group(1)), CAPTION)]))
            else:
                flow.append(Paragraph("[missing image: %s]" % m.group(2), CAPTION))
            i += 1
            continue
        if s.startswith("|"):
            flush_para()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                r = [c.replace("\x00", "|") for c in lines[i].strip().replace("\\|", "\x00").strip("|").split("|")]
                if not all(re.match(r"^\s*:?-+:?\s*$", c) for c in r):
                    rows.append(r)
                i += 1
            flow.append(table_flowable(rows))
            flow.append(Spacer(1, 0.25 * cm))
            continue
        m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", line)
        if m:
            flush_para()
            items = []
            numbered = m.group(2)[0].isdigit()
            while i < len(lines):
                m2 = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", lines[i].rstrip("\n"))
                if not m2:
                    # continuation line of the same item
                    if i < len(lines) and lines[i].startswith("  ") and lines[i].strip() and items:
                        items[-1] = items[-1] + " " + lines[i].strip()
                        i += 1
                        continue
                    break
                items.append(m2.group(3))
                i += 1
            flow.append(ListFlowable([ListItem(Paragraph(inline(t), BODY), leftIndent=12) for t in items],
                                     bulletType="1" if numbered else "bullet", start="1" if numbered else None,
                                     leftIndent=14, bulletFontSize=9))
            continue
        if s == "":
            flush_para()
            i += 1
            continue
        para.append(s)
        i += 1
    flush_para()
    return flow


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.HexColor("#666666"))
    canvas.drawString(2 * cm, 1.2 * cm, "CSPE71 Advanced Cryptography - Mini Project - 106123015 ASIL KHALIFA")
    canvas.drawRightString(A4[0] - 2 * cm, 1.2 * cm, "Page %d" % doc.page)
    canvas.restoreState()


def main():
    with open(SRC, encoding="utf-8") as f:
        lines = f.readlines()
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=2 * cm, rightMargin=2 * cm, topMargin=2 * cm,
                            bottomMargin=2 * cm, title="Optimized 256-level Sparse Merkle Tree",
                            author="ASIL KHALIFA (106123015)")
    doc.build(build(lines), onFirstPage=footer, onLaterPages=footer)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
