# 论文草稿（paper）

把 AnimeAn 组织成一篇论文的草稿。正文只写在 `paper_src.md`，Word 文件由它生成，不在 Word 里改。

## 文件

| 文件 | 内容 |
|---|---|
| `paper_src.md` | 正文的唯一来源。公式写在美元符号之间，待填和待核的地方用黄色高亮标出（搜索「〔待」） |
| `output/animean_paper.docx` | 生成的 Word 文件 |
| `rendered/paper.md` | 生成的 Markdown 副本，可以直接在 GitHub 上读 |
| `check/paper_check.md` | 生成的对照副本，用来看两次修改之间的差异 |
| `paper.md`、`equations/paper.json` | 生成的中间文件，不要手改 |
| `figures/make_figures.py`、`figures/labels.json` | 画图脚本和图里的中文标注 |
| `assets/base/` | 没有文字的底图，取自 2026-10-06 的 zemi 幻灯片 |
| `assets/paper/` | Word 里实际嵌入的图。画图脚本先画到 `assets/src/`（不入库），生成时再拷到这里 |
| `verify/verify_claims.py`、`verify/verify_results.json` | 复算正文里引用的数字，和复算的结果 |
| `verify/sources.md` | 背景与关联研究的出处核实记录 |
| `tools/make_template.py` | 生成带中文字体的 Word 模板（生成物在 `build/`，不入库） |

## 重新生成

```
py paper/build.py --figures    # 重画图，再生成 Word、对照副本、Markdown 副本（新克隆的仓库第一次用这条）
py paper/build.py              # 只改了正文时用：不重画图
py paper/build.py --pages      # 另外让 Word 报页数并导出 PDF
py paper/verify/verify_claims.py   # 重新复算数字
```

生成用的是 document-PC 技能（`~/.claude/skills/document-PC`）。

## 写作时的约定

- 方法一节以仓库为准：`docs/auto_mapping/` 的规格和 `pyfile/auto_mapping.py`。正文引用的数字都由 `verify/verify_claims.py` 在当时的提交上重新算出，不照抄旧文档里的数字。
- 背景和关联研究里来自幻灯片的说法，逐条核对过出处，结果记在 `verify/sources.md`；核对不上的没有写进正文。
- 还没有做的实验不写结果，留黄色高亮的占位。
- 草稿用中文写，投稿的会议和语言定下来之后再翻译。这个生成流程的「一句一行」对照只认英文句号，中文稿的对照副本是一段一行。
