# sz.plist - iOS PLIST Lua Module

iOS 上读写 plist 文件的 Lua C 模块，接口和 `sz.plist` 完全一致。

## 编译

GitHub Actions 自动编译，下载对应的 `.dylib`：

| 文件 | 架构 | 用途 |
|------|------|------|
| `sz-arm64.dylib` | arm64 | 真机 (A7+) |
| `sz-arm64e.dylib` | arm64e | 新款设备 |
| `sz-x86_64.dylib` | x86_64 | 模拟器 |

## 使用

```lua
local sz = require("sz")
local plist = sz.plist

-- 读取
local tmp = plist.read("/var/mobile/Library/Caches/com.apple.mobile.installation.plist")
dialog(tmp.Metadata.ProductBuildVersion, 0)

-- 写入
tmp.Metadata.ProductBuildVersion = "havonz"
plist.write("/path/to/file.plist", tmp)
```

## 注意事项

- nib 文件不支持（会返回 nil）
- 写入前建议备份重要 plist 文件
- 需要 Lua 5.1/5.2/5.3 环境