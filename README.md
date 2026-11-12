# ElyGambit · 往世之弈

一款使用 **Unreal Engine 5.8** 制作、以《崩坏3》往世乐土与逐火英桀为题材的同人自走棋游戏。

在 7×7 棋盘上招募、合成与部署英桀，通过商店经营、刻印选择和阵容搭配挑战七个关卡，迎战侵蚀之律者。

🎬 **[B站游戏演示 · 往世的涟漪](<https://www.bilibili.com/video/BV1h4W26yEKQ>)**

🎮 **[下载 Windows 版](<https://github.com/uq-games/ElyGambit/releases/tag/v1.0.0-rc1>)**

## 游戏特色

- **英桀阵容**：招募熟悉的角色，通过合成与升变打造自己的队伍。
- **棋盘部署**：在 7×7 棋盘上安排站位，搭配角色技能与召唤物展开自动战斗。
- **刻印构筑**：经营局内资源，选择刻印，在七关挑战中逐步完善阵容。
- **终局演出**：不同终局构筑路线、人之律者双形态战斗与剧情结局。

## 下载与游玩

当前版本：**1.0.0-rc1 · Windows 64 位**，下载大小约 **890 MiB**。

1. 前往 [Releases](<https://github.com/uq-games/ElyGambit/releases/tag/v1.0.0-rc1>)，选择 **ElyGambit往世之弈.zip**（实际下载文件名为 `ElyGambit.zip`）。
2. 完整解压，进入 `ElyGambit` 文件夹。
3. 双击 **`启动游戏.exe`**，无需安装虚幻引擎。

如提示缺少 VCRUNTIME/MSVCP 运行库，安装包内的 `Engine/Extras/Redist/en-us/vc_redist.x64.exe` 后重试。

## 基本操作

- 在商店购买角色，将手牌拖到己方棋盘进行部署。
- 点击战场单位查看属性和技能。
- 按 `Esc` 或点击右上角菜单进行设置、返回主菜单或退出。
- 点击推进剧情对白，影片可使用画面中的跳过按钮。

## 关于代码

项目主要使用 **C++、Gameplay Ability System、UMG/Slate 和 Niagara**，包含战斗技能、局内成长、棋盘交互、界面与角色表现等实现。

本仓库分享项目源码，游戏资源不包含在内；直接游玩请下载 Release。

- [游戏源码](<Source/ElyGambit/>) · [编辑器工具](<Source/ElyGambitEditor/>)
- [开发说明](<Docs/DEVELOPMENT.md>) · [第三方声明](<Docs/THIRD_PARTY.md>)

## 作者与反馈

作者：**往世的涟漪** / [uq-games](<https://github.com/uq-games>)

欢迎在 [B站视频](<https://www.bilibili.com/video/BV1h4W26yEKQ>)下交流，或通过 [Issues](<https://github.com/uq-games/ElyGambit/issues>)反馈问题。
