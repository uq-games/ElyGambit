# 第三方来源与权利边界

本文件说明已知来源与许可边界，不是完整法律意见，也不构成全部素材公开分发权已经核实的声明。

## 源码展示

- 项目自有代码暂未指定统一开源许可证。公开展示并不授予对全部文件的无限制复制、修改、商业利用或再分发许可；也不改变 GitHub 平台条款及适用法律下的权利。
- **KawaiiPhysics**：Copyright (c) 2019–2026 pafuhana1213，采用 [MIT 许可证](<../Plugins/KawaiiPhysics/LICENSE>)；[上游项目](<https://github.com/pafuhana1213/KawaiiPhysics>)。保留现有文件中的版权说明。本地源码快照可能含项目适配，不宣称与上游版本完全一致。
- 部分 UE 工程模板保留 Epic Games 版权头；Unreal Engine 自身不随源码快照提供，仍受其适用许可约束。
- 原完整工程根 `Shaders` 中有标明 HoyoToon / GPL-3.0 的适配，也有其他外部来源实现。该根目录未纳入本代码展示快照。**这不代表已消除成品中可能涉及的许可义务**；实际烘焙使用范围、GPL 义务及与其他许可的相容性仍需另行核实。保留在渲染插件里的两个 Shader 是另一组文件，不应与上述根目录混为一谈。

## 游戏成品中的原作及外部素材

ElyGambit 是以《崩坏3》为题材的非官方同人项目，不代表原作权利人的认可、合作或背书。角色、名称、模型、动作、美术、原声、音乐、影像等权利归相应权利人；官网/CDN、原客户端或公开视频的可访问性不等于重新分发许可。

已知涉及：原作角色模型与动作、角色原画、中文配音及音效、影片片段、HOYO-MiX 音乐，以及社区制作/描摹内容。部分第三方特效和动捕素材的分发范围也尚未核实。

- [结局媒体来源说明](<ThirdPartyNotices/Endings20261110/README.txt>)保留了已知原作影像、配音和音乐记录。这份说明只覆盖部分媒体，**不是全游戏资产授权清单**。
- 公开资源许可尚有未完成核验的部分；发布版本和原有声明不应被理解为“全部官方授权”“全部原创”或“可任意抽取复用”。
- 源码展示与 Release 成品是不同交付物。源码未包含资产，不代表成品中的资产权利随之解决；免责声明也不能替代必要授权。

## 已附第三方许可与署名

### 字体

游戏使用 Noto Sans SC、Noto Serif CJK SC、LXGW WenKai、Cinzel。来源记录与四份 SIL Open Font License 1.1 正文见：

- [字体来源](<ThirdPartyNotices/Fonts/FontSources.md>)
- [Noto Sans SC](<ThirdPartyNotices/Fonts/OFL-NotoSansSC.txt>)
- [Noto Serif CJK](<ThirdPartyNotices/Fonts/OFL-NotoSerifCJK.txt>)
- [LXGW WenKai](<ThirdPartyNotices/Fonts/OFL-LXGWWenKai.txt>)
- [Cinzel](<ThirdPartyNotices/Fonts/OFL-Cinzel.txt>)

本源码快照只提供声明，不提供字体二进制。字体各自的版权与保留字体名要求继续适用。

### 屏障扫描线素材

**Hologram & Glitch FX Free** by **heyheythere**，采用 [CC BY 4.0](<https://creativecommons.org/licenses/by/4.0/>)。

- [作者来源](<https://heyheythere.itch.io/hologram-glitch-free>)
- [署名与改动说明](<ThirdPartyNotices/BossWall/Attribution.txt>)
- [随项目保存的许可说明](<ThirdPartyNotices/BossWall/HologramGlitchFree-LICENSE.txt>)

项目使用其中扫描线 atlas，并进行透明度采样、蓝紫色调和时钟驱动动画适配；作者不为此适配背书。

### UI 音效来源记录

项目制作记录还列有 Kenney Interface Sounds 与 Cleyton Kauffman Menu Screen Slides（CC0）。这里保留来源署名，不将 CC0 的建议署名误写为强制要求，也不据此推断游戏中所有音效都是 CC0。

## 权利联系

如你是相关权利人，或发现署名、许可覆盖及内容分发问题，请通过 [GitHub Issues](<https://github.com/uq-games/ElyGambit/issues>) 联系作者并指明具体内容。不要在公开 Issue 中提交个人敏感材料。
