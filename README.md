# KeyStats

KeyStats 是一款面向 Windows 的本地键盘与鼠标使用频率统计工具。它按分钟保存聚合计数，并提供时间范围、时段分布和全尺寸键盘热力图。

## 隐私原则

- 不记录输入内容；
- 不记录按键顺序；
- 不记录正在使用的应用或窗口；
- 不记录鼠标位置；
- 不需要账号，不连接服务器，不上传数据；
- 所有数据只保存在程序旁的 `Data` 目录。

## 主要功能

- Windows Raw Input 后台采集；
- 键盘按键和鼠标左右键分别统计；
- 左右 Ctrl、Shift、Alt 等物理键分开计数；
- 长按自动连发去重；
- 每分钟聚合，30 秒自动保存；
- 今天、最近 10 分钟、7 天、30 天、全部和自定义范围；
- 10 分钟时段分布和全尺寸键盘热力图；
- CSV 导出；
- 系统托盘后台运行、暂停、立即保存和完整退出；
- 单实例运行和可选的当前用户登录自启动；
- 绿色版数据目录，可直接随程序文件夹迁移。

## 系统要求

- Windows 10 或更高版本；
- x64 处理器；
- 源码构建需要 .NET 10 SDK；
- 轻量发行版需要 x64 `.NET 10 Desktop Runtime`；完整自包含版无需预装运行时。

## 从源码构建

```powershell
dotnet restore .\KeyStats.slnx
dotnet build .\KeyStats.slnx -c Release --no-restore
dotnet run --project .\tests\KeyStats.Core.Tests -c Release --no-build
```

生成完整自包含版和轻量版：

```powershell
.\scripts\publish.ps1
```

输出位于 `artifacts` 目录。

## 目录结构

```text
src/KeyStats.Core/          键位模型、归一化和计数逻辑
src/KeyStats.Storage/       SQLite、分钟聚合、设置与导出
src/KeyStats.App/           WPF 界面、Raw Input 和托盘生命周期
tests/KeyStats.Core.Tests/  无第三方测试框架的回归测试
docs/                       使用、构建、隐私和测试说明
scripts/                    发行包构建脚本
```

## 数据与迁移

数据保存在可执行文件旁的 `Data` 目录。迁移时完整复制程序文件夹即可。完整版和轻量版使用相同的数据格式。

数据库只保存每一分钟内各按键或按钮的累计次数，不保存事件顺序。更多说明见 [隐私与数据格式](docs/PRIVACY.md)。

## 游戏兼容性

普通桌面程序和大多数游戏可以使用 Raw Input 计数。如果游戏以管理员权限运行，请尝试以相同权限启动 KeyStats。部分采用独占输入或反作弊隔离的游戏可能不会向普通桌面程序提供输入事件，本项目不会尝试绕过反作弊机制。

## 参与贡献

提交问题或代码前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。安全相关问题请参考 [SECURITY.md](SECURITY.md)。

## 第三方组件

项目使用 Microsoft.Data.Sqlite 和 SQLitePCLRaw。详情见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 许可证

正式公开前需要由项目所有者选择并添加 `LICENSE` 文件。

