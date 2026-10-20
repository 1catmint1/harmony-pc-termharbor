# 泊舟连接台 for HarmonyOS PC

泊舟连接台是基于 Tera Term 社区鸿蒙工程完善的 ArkUI/C++ 桌面终端工具，提供 TCP、Telnet、SSH 会话、终端显示、连接配置、日志和文件传输操作。

1.0.2 新增纯色背景图标、输入框与终端输出的 Ctrl+A 全选，以及无需外部账号的本机连接体验。体验入口建立真实 TCP 与 SSH 会话，支持中文收发和内存文件读写。常规连接仍可使用用户选择的服务器。文件传输保留原工程的 SCP 优先与 SFTP 回退。

1.0.3 将应用展示名称更新为“泊舟连接台”，采用原创纸舟与连接节点图标。首页直接说明体验条件，并提供 TCP、SSH、文件传输及自有服务器四个入口。文件传输体验自动连接本机 SSH 并填写账户与路径。

## 来源与许可

基于 [OpenHarmonyPCDeveloper/ohso_teraterm](https://atomgit.com/OpenHarmonyPCDeveloper/ohso_teraterm)，固定提交 `4d50343a767fd20fab5cbf1e0cd13aa4fde6f803`。已有 ArkUI 界面、终端解析、网络与串口、SSH、文件传输及 SFTP 回退、配置和宏脚本工作归原作者。

本适配层先前完善了网络分包下的 UTF-8 与 Unicode 16 宽字符处理。本轮新增键盘全选、图标、本机内存体验服务与 SCP 文件末尾状态确认，补充对应源码、构建步骤与回归证据。Tera Term 及相关修改按 BSD 条款提供；libssh 0.12.2 为 LGPL-2.1-or-later，libssh2 1.11.1 为 BSD-3-Clause，Mbed TLS 3.6.7 选择 Apache-2.0。完整原始许可与作者说明见 `LICENSES/ssh-runtime`。

## 构建和重新链接

`third_party/runtime-sources` 提供完整原始依赖源码压缩包与 SHA256；`tools/build-local-runtime.ps1` 在 Windows 上以两个工作线程构建同一套启用 pthread 的 Mbed TLS、libssh 和 libssh2。默认构建目录在 F 盘，运行库在 D 盘，日志在 E 盘，可通过参数指定。

将 `TT_OHOS_LIBSSH2_ROOT` 和 `TT_OHOS_REVIEW_RUNTIME_ROOT` 都设置为生成的 arm64-v8a 运行库前缀。用 DevEco Studio 打开 `ports/ohos/app`，安装 OHPM 依赖，使用自己的 SDK 和签名。完整应用源码与对应运行库源码均可修改、重新编译和重新链接。

## 使用本机体验

点击首页“TCP 回声体验”或“SSH 终端体验”，再点 Send 查看终端收发。“文件传输体验”会自动连接本机 SSH 并展开已填好路径的 File 工具区。SSH 体验自动填入公开账号 local-demo/local-demo，示例文件为 /hello.txt。File 提供文件传输控件，Setup 提供转发控件。体验服务仅监听回环地址，命令仅作用于内存工作区；停止体验或退出应用后释放连接与内存文件。

## 验证

2026-10-20：10项生产 ArkTS 入口行为主机检查通过；USB r16 完成启动引导、TCP/SSH 中文往返、SCP/SFTP各自双向111字节传输、自有服务器配置、停止和重连、帮助版号及许可访问。正式包与r16运行载荷一致，详情见 `evidence/native-production-acceptance-r16.json` 和 `evidence/final-release-payload-binding-r16.json`。


2026-10-19：生产 ArkTS 键盘方法15项、生产 SCP 完成确认头文件8项主机回归通过。USB r14 完成实际连接和文件传输、键盘操作及应用说明检查；验收范围和结果见 `evidence/native-production-acceptance-r14.json`。

正式 HAP 和 APP 通过官方验签，发行 Profile 无设备绑定；正式包与真机验收包的运行载荷逐项同字节，APP 内嵌 HAP 也一致。对应回执保存在 evidence。平台审核与上架状态以开发者中心回执为准。
