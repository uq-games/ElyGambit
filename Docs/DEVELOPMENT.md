# 开发与源码阅读

## 公开范围

本仓库是 ElyGambit 当前工作树的精简代码展示快照，不携带原开发仓库的提交历史。游戏成品在 GitHub Release 单独提供。

保留：运行时与编辑器 C++ 源码及构建规则、必要插件源码、插件自身的两个原生渲染 Shader、必要项目配置、第三方文本声明。

不提供：`Content` 运行资产、`SourceArt` 制作原件、根 `Shaders` 外部派生实现、缓存和编译输出、素材提取工具、个人编辑器设置、MCP 配置、AI 会话附件、原项目内部历史文档。

## 环境与依赖

- 工程关联：**Unreal Engine 5.8**。
- 目标规则使用 `BuildSettingsVersion.V7`、`EngineIncludeOrderVersion.Unreal5_8`。
- 需要与该引擎版本兼容的 Windows C++ 工具链与 SDK；本次发布没有验证公开副本的具体工具链组合。
- 游戏模块依赖 GameplayAbilities/Tags/Tasks、EnhancedInput、UMG/Slate、Niagara、AIModule、NavigationSystem、MediaAssets、MoviePlayer、LevelSequence、MovieScene、AudioMixer、RHI/RenderCore 等引擎模块。
- [KawaiiPhysics](<../Plugins/KawaiiPhysics/>)：按本地当前源码快照提供，插件描述版本为 1.21.0；保留 [MIT 许可证](<../Plugins/KawaiiPhysics/LICENSE>)，不宣称与上游发布包逐字节相同。本快照不包含插件的完整编辑器 Python、资源和本地化目录。
- [EGNativeRenderer](<../Plugins/EGNativeRenderer/>)：依赖 `Renderer/Private`、`Renderer/Internal` 和引擎私有 Shader。需要合法安装的匹配引擎提供相关实现，不随本仓库分发 Unreal Engine 源码，也不保证跨引擎版本兼容。

## 公开副本做了什么调整

- 从 `DefaultEngine.ini` 删除 AndroidFileServer 和 PythonScriptPlugin 的本地工具配置段。
- 不包含三份编辑器个人/工具偏好配置。
- 在 [工程描述](<../ElyGambit.uproject>) 中禁用 `ModelContextProtocol`、`AllToolsets`。它们是本地编辑器辅助工具，不作为源码阅读的必要依赖。
- 未修改玩法源码和原开发工程；未复制原仓库的历史提交。

## 阅读入口

| 范围 | 入口 |
|---|---|
| 战斗与技能 | [AbilitySystem](<../Source/ElyGambit/Private/AbilitySystem/>)、[Skills](<../Source/ElyGambit/Private/Skills/>)、[Components](<../Source/ElyGambit/Private/Components/>) |
| 局内流程 | [Game](<../Source/ElyGambit/Private/Game/>)、[Data](<../Source/ElyGambit/Private/Data/>) |
| 棋盘部署 | [Placement](<../Source/ElyGambit/Private/Placement/>) |
| UI 与交互 | [UI](<../Source/ElyGambit/Private/UI/>) |
| 运行表现 | [Presentation](<../Source/ElyGambit/Private/Presentation/>) |
| 自动化 | [运行时测试](<../Source/ElyGambit/Private/Tests/>)、[编辑器测试](<../Source/ElyGambitEditor/Private/Tests/>) |

## 构建限制

工程描述、配置和代码仍保留正式资产路径，例如主菜单地图和关卡数据。这些引用用于展示真实实现，不是已经提供的依赖。

即使安装 UE 5.8 并生成工程文件，也**不保证当前公开快照能够完成编译、打开地图、运行自动化或打包**。部分测试通过真实地图加载 GameMode、Run 和敌人资产，不是独立的纯逻辑测试。这里没有通过删测试或换占位资产来制造“全部通过”的结果。

恢复完整开发工程还需要另行取得并合法使用相应资源及其引用关系；本仓库不提供第三方游戏资源的抽取或补齐下载流程。若仅想游玩，请使用 [Windows Release](<https://github.com/uq-games/ElyGambit/releases/tag/v1.0.0-rc1>)。

## 已知验证边界

发布准备只做文件清单、配置/常见凭证模式扫描、JSON 结构、相对文档链接和下载包完整性检查。本次没有执行 UHT、编译、编辑器、PIE、Cook、自然战斗或设备兼容测试，不能用旧完整工程的历史结果替代公开快照验证。
