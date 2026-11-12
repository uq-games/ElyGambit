# ElyGambit · 往世之弈

**以《崩坏3》往世乐土与逐火英桀为题材的 Unreal Engine 5 同人自走棋游戏。**

在 7×7 棋盘上招募、合成与部署英桀，通过商店经营、刻印选择和阵容搭配挑战七个关卡，迎战侵蚀之律者。

🎬 **[B站游戏演示 · 往世的涟漪](<https://www.bilibili.com/video/BV1h4W26yEKQ>)**  
🎮 **[下载 Windows 游戏（GitHub Release）](<https://github.com/uq-games/ElyGambit/releases/tag/v1.0.0-rc1>)**

> **想玩游戏：请到 Release 下载 `ElyGambit往世之弈.zip`，不要下载 GitHub 自动生成的 “Source code” 压缩包。**  
> 本仓库是代码展示快照，不含完整游戏资源，不能仅凭克隆仓库直接运行游戏。

## 游戏内容

- **棋盘与阵容**：在准备阶段购买、合成和部署角色，搭配不同英桀与升变形态。
- **自动战斗**：角色技能、召唤物、状态效果与不同敌人共同构成战斗。
- **七关流程**：经营局内资源，选择刻印并逐步构筑阵容。
- **终局路线**：包含不同终局构筑、人之律者双形态战斗与剧情结局演出。

## 下载与运行

当前公开包为 **`1.0.0-rc1` / Win64 Shipping**，游戏构建批次为 **`radiantvow20261111`**。这是预发行候选版本，不是已完成全平台兼容验收的正式版。

1. 打开 [Release 页面](<https://github.com/uq-games/ElyGambit/releases/tag/v1.0.0-rc1>)，下载 **`ElyGambit往世之弈.zip`**。
2. **完整解压**到普通文件夹，不要在压缩软件内直接运行，也不要只解压 EXE。
3. 进入解压后的 `ElyGambit` 文件夹，双击 **`启动游戏.exe`**。游玩无需安装 Unreal Engine。
4. 若提示缺少 VCRUNTIME/MSVCP 等运行库，运行包内 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`，安装或修复后重试。
5. 首次启动和进入战斗可能需要等待资源加载；如明显卡顿，可先降低画质。

游戏采用 Windows 64 位与 DirectX 12 渲染路径。尚未完成多设备兼容测试，不在此声称具体最低硬件配置或稳定帧率。

**下载校验：**压缩包为 `933,159,674` 字节（约 890 MiB）。SHA-256：

```text
9321572869bc1240265bc1a7d01151ac346a9f5dc22698f159be0822c68fec3e
```

Release 同时提供 `SHA256SUMS.txt`。Windows PowerShell 可运行：

```powershell
Get-FileHash -LiteralPath '.\ElyGambit往世之弈.zip' -Algorithm SHA256
```

> 本次仅修正下载附件名中的“弈”，没有修改或重新打包游戏内容。包内旧运行说明仍可能写 `ElyGambit.exe`，请以实际的 **`启动游戏.exe`** 为准。包内旧文件校验清单可能仍使用改名前的启动文件名；上述 ZIP 校验值对应本次实际发布的完整下载文件。

## 基本操作

- 点击开场的开始按钮进入游戏。
- 在商店购买角色，将手牌拖到己方棋盘部署；点击战场单位查看属性和技能。
- 右上角“菜单”或 `Esc` 打开局内菜单，可继续、设置、返回主菜单或退出。
- `Esc` 会优先取消拖拽或关闭详情；短结算、入场及转阶段演出中暂不打开暂停菜单。
- 剧情对白点击补全字幕，再点击推进；影片可使用画面中的跳过按钮。

## 代码导览

| 目录 | 内容 |
|---|---|
| [Source/ElyGambit](<Source/ElyGambit/>) | 运行时游戏模块：战斗、技能、局内流程、棋盘交互、UI 与表现 |
| [Source/ElyGambitEditor](<Source/ElyGambitEditor/>) | 编辑器制作工具及编辑器侧测试 |
| [Plugins/EGNativeRenderer](<Plugins/EGNativeRenderer/>) | 原生渲染通道与相关 Shader 实现 |
| [Plugins/KawaiiPhysics](<Plugins/KawaiiPhysics/>) | 项目使用的 KawaiiPhysics 源码快照与原许可证 |
| [Config](<Config/>) | 去除本地工具设置后的项目配置 |
| [Docs/DEVELOPMENT.md](<Docs/DEVELOPMENT.md>) | 开发环境、源码阅读入口与缺失资源说明 |
| [Docs/THIRD_PARTY.md](<Docs/THIRD_PARTY.md>) | 第三方来源、许可与权利边界 |

### 为什么不是完整 UE 工程？

公开仓库只保留代码展示所需的文件，**不包含**地图、蓝图、数据资产、角色模型、动作、材质、贴图、音频、影片、字体二进制及制作原件，也不包含原工程的私有工具配置、缓存与历史提交。根目录中部分外部派生 Shader 的分发条件尚待核对，未纳入本快照。

源码和配置仍保留对正式游戏资产的引用，便于理解实际实现；这些路径并不代表资源已在仓库中提供。自动化测试中也有依赖真实地图和资产的集成测试。**没有宣称此精简仓库已独立编译、通过测试或完成打包。**

开发基线是 **Unreal Engine 5.8**，使用 C++、Gameplay Ability System、Enhanced Input、Niagara、UMG/Slate、Media 与 Level Sequence 等模块。原生渲染插件依赖 UE 私有接口，不能保证其他 UE5 版本兼容。详见[开发说明](<Docs/DEVELOPMENT.md>)。

## 版本与验证范围

- 既有构建记录：本批 Win64 Shipping 构建、Cook、打包成功，并验证了启动与进入准备棋盘。
- 本次发布检查：公开文件白名单、配置净化、常见敏感信息模式扫描；下载 ZIP 内 47 个文件逐项解压读取及哈希与现有解压副本比对通过。
- **未完成/未重做**：本次未重新编译或启动游戏；没有以本次发布检查证明三条路线自然完整通关、完整音频验收、满盘性能或多设备兼容。
- 源码快照整理于 2026-11-12；游戏包构建于 2026-11-11。公开源码经过范围裁剪和工具配置调整，不是游戏包的完整可复现构建输入。

## 权利与许可

这是非官方同人项目，不代表《崩坏3》权利人的官方作品、合作或背书。原作人物、美术、模型、动作、音频、影片等素材属于各自权利人；部分素材的公开分发许可仍未完成核验，发布行为不构成授权证明。

**公开可查看不等于全部代码或资源按 MIT 等许可证开源。** 本仓库暂未为项目自有代码指定统一开源许可证；第三方代码、字体及素材仍分别适用其原许可证，不能用一个项目许可证覆盖。详见[权利与第三方声明](<Docs/THIRD_PARTY.md>)。请勿将这里的素材来源记录视为允许自行抽取或再分发游戏素材的授权。

## 作者与反馈

- B站：**往世的涟漪** · [游戏演示视频](<https://www.bilibili.com/video/BV1h4W26yEKQ>)
- GitHub：[uq-games](<https://github.com/uq-games>)
- 问题反馈、版权联系：[Issues](<https://github.com/uq-games/ElyGambit/issues>)。报告问题时请附游戏版本、系统/显卡信息、复现步骤；发送日志或截图前请遮盖个人信息。
