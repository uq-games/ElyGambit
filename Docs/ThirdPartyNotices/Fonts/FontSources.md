# UI 字体来源与许可 / 2026-11-06

以下保留 2026-11-06 UI 改版的字体来源与历史验证记录。四套字体采用 SIL Open Font License 1.1（OFL）；历史记录说明原始字节未修改、未重绘、未改名，游戏内通过 UE 导入为 FontFace + Runtime UFont 私有副本使用。

**2026-11-12 公开快照说明：**本目录只提供来源记录和四份同层 `OFL-*.txt` 许可正文，不包含字体二进制、制作脚本或导入报告。下文原始文件名、制作路径和测试结果均为历史来源信息，不代表这些文件随源码仓库提供，也不代表本次重新执行过字体导入或视觉验收。

## 字体清单

| 角色 | 文件 | 字体内部版本 | 来源（固定 URL） | SHA-256 |
|---|---|---|---|---|
| Body 正文 | `NotoSansSC-Regular.otf` | Version 2.002（`2.002;GOOG;NotoSansSC-Regular;ADOBE`） | google/fonts 静态字体快照 commit `beff1d8226e598dfaf06f9f8312c01021f70e9d2`（2021-07-29，变量字体迁移之前的静态 OTF）<br>https://raw.githubusercontent.com/google/fonts/beff1d8226e598dfaf06f9f8312c01021f70e9d2/ofl/notosanssc/NotoSansSC-Regular.otf | `04da2dac7dc3960fad1046d75b9a61ee30ecf3de8dd3b71a7860fbb99492ac21` |
| Heading 标题 | `NotoSerifCJKsc-Regular.otf` | Version 2.003（`2.003;GOOG;NotoSerifCJKsc-Regular;ADOBE`） | 复用 `SourceArt/UI/StigmataSelectionC20261105/Fonts/NotoSerifCJKsc-Regular.otf`（2026-11-05 圣痕界面导入所用同一字节拷贝，原文件未改动）；上游 noto-cjk Serif：https://github.com/notofonts/noto-cjk | `2a2eae2628df83556c54018c41e20fa532c1b862c5256ae8b3f23feb918d12ca` |
| Narrative 叙事 | `LXGWWenKai-Regular.ttf` | Version 1.530（2026-10-18） | lxgw/LxgwWenKai Release `v1.530`<br>https://github.com/lxgw/LxgwWenKai/releases/download/v1.530/LXGWWenKai-Regular.ttf | `2d73eea94ad808999d90c0000c239a84a33b09d4bc6ab6c5b8bbf1eda8a7fc8b` |
| Numerals 数字 | `Cinzel-Regular.ttf` | Version 2.000（静态 Regular） | google/fonts commit `45071f07c63e863a539442ef3562b71ab1f147a6`（2020-07-22，“cinzel: v2.000 added”），静态目录 `ofl/cinzel/static/`；上游 https://github.com/NDISCOVER/Cinzel<br>https://raw.githubusercontent.com/google/fonts/45071f07c63e863a539442ef3562b71ab1f147a6/ofl/cinzel/static/Cinzel-Regular.ttf | `af0031129f27dc752e8629a80b793d27abea94027faa27cc660c3fc33f607a1f` |

Cinzel 使用固定提交中的**静态 TTF**（`ofl/cinzel/static/Cinzel-Regular.ttf`），不是变量字体 `Cinzel[wght].ttf`，因此无需再找替代字体。若以后需要重新获取，必须保留同一 URL 或记录新的 SHA-256。

## 许可

| 许可文件 | 对应字体 | SHA-256 | 来源 |
|---|---|---|---|
| `OFL-NotoSansSC.txt` | Noto Sans SC | `5e0da210fb04058a8c0087985d2d456b931c2579811a49655721d3cf0c36b6d6` | google/fonts 同一静态快照 |
| `OFL-NotoSerifCJK.txt` | Noto Serif CJK SC | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` | noto-cjk commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c` 的 `Serif/LICENSE` |
| `OFL-LXGWWenKai.txt` | LXGW WenKai | `1a25e35da1031c6c3436fde545bb9cb5aca954e9873afe510c834b8b79bd21a0` | lxgw/LxgwWenKai `v1.530` 的 `OFL.txt` |
| `OFL-Cinzel.txt` | Cinzel | `f2b3029aba64c378bf0963b62945eee15e564fe4330b934c8f2eb058282b5e83` | google/fonts 同一 Cinzel 提交的 `OFL.txt` |

OFL 1.1 允许把字体随软件捆绑、嵌入和再分发；四份许可正文保存在本目录，字体二进制不随公开源码快照提供。项目未出售字体本身，也未把字体作为独立产品分发。

### 保留字体名（Reserved Font Name）注意

- LXGW WenKai 的 OFL 声明保留字体名 `霞鹜`/`霞鶩`/`落霞孤鹜`/`落霞孤鶩`/`LXGW`，并附有“未修改源码或仅作 Web 转换时可用保留名”的附加许可。本轮**不修改字体二进制、不做子集化、不改 internal name**，直接把原始 TTF 字节导入 UE 作为游戏内嵌资源（OFL 允许的捆绑使用），因此不产生“Modified Version”，不触发保留名冲突。
- 导入后的**资产名** `FF_EG_LXGWWenKai` / `F_EG_LXGWWenKai` 仅为 UE 工程内资产标签，不是字体家族名，也没有作为字体产品对外分发。
- 若以后要子集化、改名或制作派生字体，必须重新按 OFL 保留名条款处理，并在本 README 记录修改内容与新的字体名。

## 字形覆盖（`inspect_fonts.py` 实测）

| 字体 | CJK 样本（界面实际用字） | ASCII 数字/百分号/小数点 | 结论 |
|---|---|---|---|
| Noto Sans SC | 全部覆盖 | 全部覆盖 | 正文/按钮/HUD 常规文本 |
| Noto Serif CJK SC | 全部覆盖 | 全部覆盖 | 标题/角色名/阶段/圣痕名 |
| LXGW WenKai | 全部覆盖 | 全部覆盖 | 长文本叙事、刻印/圣痕描述 |
| Cinzel | **不含 CJK** | 全部覆盖 | 仅用于纯数字、百分比、价格、金币/回合数值 |

**Cinzel 没有中文字形**，因此数字角色的自动套用必须避开含中文的文本：`EGApplyTypography` 在把控件判为 Numeric 前会检查文本是否含 CJK，含中文则降级为 Heading（Noto Serif CJK SC），避免缺字方框；数字控件自身只放数字/符号。

## 状态与验证边界

- 已完成：四套原始文件与许可、SHA-256、版本、字形覆盖核对；`fetch_fonts.ps1` 可重复获取。
- 已完成：UE 编辑器内导入为 FontFace + Runtime UFont（见导入报告），既有 `F_UI_CJK`、`FF_StigmaC_Serif` 未覆盖。
- 未完成：C++ 编译（由 Lead 统一构建）、PIE/真机视觉验收、打包/Cook 检查。导入成功不等于视觉通过。
