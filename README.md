# TermHarbor for HarmonyOS PC

TermHarbor 是基于 Tera Term 社区鸿蒙迁移工程完善的 ArkUI/C++ 桌面终端工具，提供 TCP、Telnet、SSH 会话及终端显示、连接配置和日志操作。

本轮改进网络分包条件下的 UTF-8 解码，按会话保留待续字节；依据 Unicode 16 EastAsianWidth W/F 数据处理中文与 emoji 的双列终端占位，并修复清除、插入、删除和窗口列数变化时的宽字符配对。SSH 成功连接时显示连接状态与主机密钥处理结果。Help 面板提供隐私说明和完整开源许可。

## 来源与许可

基于 [OpenHarmonyPCDeveloper/ohso_teraterm](https://atomgit.com/OpenHarmonyPCDeveloper/ohso_teraterm)，固定提交 `4d50343a767fd20fab5cbf1e0cd13aa4fde6f803`。已有 ArkUI 界面、终端解析、TCP/Telnet/串口、SSH、配置和宏脚本工作归原作者。本轮新增工作为流式 Unicode 与宽字符修复、对应回归验证、连接状态文案、发行工程整理及隐私和许可入口。

Tera Term 及相关修改按 BSD 条款提供，见 LICENSES/TeraTerm-BSD.txt。SSH 使用 libssh2 和 Mbed TLS，许可材料保存在 LICENSES/ssh-runtime。Unicode 数据及许可位于 third_party/unicode-16。发行应用内包含完整许可文本。

## 构建

用 DevEco Studio 打开 `ports/ohos/app`，安装工程 OHPM 依赖，使用本机 HarmonyOS SDK、DevEco 自带 Hvigor 插件和自己的签名配置。

原生模块启用 libssh2。将与目标 ABI 匹配的 libssh2/Mbed TLS 头文件和静态库放入独立工具目录，并设置 `TT_OHOS_LIBSSH2_ROOT`；目录应含 `include/libssh2.h` 和 `lib/`。具体所需库见 ports/ohos/native/CMakeLists.txt。签名材料、构建缓存和运行库二进制由构建者在本机提供。

## 验证

2026-10-16：新增流式 UTF-8 主机回归24项、宽字符回归12项通过，原有终端核心、配置解析和 known_hosts 三个主机测试程序通过。

USB 鸿蒙 PC 上的开发签名正式模式版本完成一次绑定运行 r4：TCP 逐字节分片接收中文与 emoji，TCP 与 SSH 真实认证终端收发、宽字符显示、常规与最大化窗口、隐私和许可入口均已核对。测试会话及本应用进程已释放。独立原生测试程序的直接执行曾被系统拒绝126，保留在验证记录中。

发行 HAP/APP 已通过官方验签，发行 Profile 无设备绑定。发行包与真机验收包的8项应用运行载荷逐项同字节，APP 内嵌 HAP 载荷也一致。对应回执在 evidence/；开发者中心的提审、批准和发布状态以平台回执为准。
