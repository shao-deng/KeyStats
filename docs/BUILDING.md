# 构建与测试

项目运行时依赖 Windows 10 或更高版本、C++20 编译器。SQLite3 的头文件和
MSVC 库已经放在 `third_party/sqlite` 中。

## 配置

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

如果使用 MinGW，也可以把生成器替换为本机可用的 CMake 生成器。

## 构建

也可以让批处理脚本自动检测本机 MSVC：

```powershell
scripts\build.bat
```

首次切换生成器或需要彻底重建时使用：

```powershell
scripts\build.bat /clean /test
```

手动构建方式如下：

```powershell
cmake --build build --config Release
```

生成的程序为 `build/Release/KeyStats.exe`，测试程序为
`build/Release/KeyStatsTests.exe`。

## 测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

CMake 默认使用项目内置的 SQLite3。如果要替换为系统版本，也可以使用 vcpkg：

```powershell
vcpkg install sqlite3:x64-windows
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

也可以从 SQLite 官方发行包获取开发文件，然后指定安装目录：

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 `
  -DSQLite3_ROOT=C:/path/to/sqlite3
```

该目录应包含 `include/sqlite3.h` 和 `lib/sqlite3.lib`。
