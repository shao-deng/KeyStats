# 构建与发布

## 环境

- Windows x64；
- .NET 10 SDK；
- PowerShell。

仓库的 `global.json` 指定 .NET SDK 10.0.400，并允许使用同一功能带的更新补丁版本。

## 构建

```powershell
dotnet restore .\KeyStats.slnx
dotnet build .\KeyStats.slnx -c Release --no-restore
```

## 回归测试

测试项目是一个无第三方测试框架的控制台程序：

```powershell
dotnet run --project .\tests\KeyStats.Core.Tests -c Release --no-build
```

进程返回码为 0 表示全部通过。

## 发行包

```powershell
.\scripts\publish.ps1
```

脚本会生成：

- `KeyStats-V1.0-full-win-x64.zip`：包含 .NET 桌面运行时；
- `KeyStats-V1.0-lite-win-x64.zip`：依赖 x64 .NET 10 Desktop Runtime；
- `SHA256SUMS.txt`：两个 ZIP 的 SHA-256。

