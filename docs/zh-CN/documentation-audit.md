[English](../documentation-audit.md) | [简体中文](documentation-audit.md)

# 双语文档审计

审计日期：2026-11-14。本 V2.1 版本修改文档、文档工具及对应 CI job。协议、class、网络适配器、驱动行为测试和公共 C 接口均无变更。工作仅在本地 main 进行；CI job 已配置，但本版本尚未远端执行。

## 维护文档清单

| 英文主版本 | 完整简体中文版本 |
| --- | --- |
| [README](../../README.md) | [README](../../README.zh-CN.md) |
| [示例](../../examples/README.md) | [示例](../../examples/README.zh-CN.md) |
| [架构](../architecture.md) | [架构](architecture.md) |
| [兼容性](../compatibility.md) | [兼容性](compatibility.md) |
| [延迟探针](../latency-probe.md) | [延迟探针](latency-probe.md) |
| [移植](../porting.md) | [移植](porting.md) |
| [协议审计](../protocol-audit.md) | [协议审计](protocol-audit.md) |
| [来源](../sources.md) | [来源](sources.md) |
| [验证](../validation.md) | [验证](validation.md) |
| [本审计](../documentation-audit.md) | [本审计](documentation-audit.md) |

原有七篇技术文档保留技术范围和历史章节。英文正文整理为每段/列表项一个物理行，保留命令、日志和必要代码换行。README 扩充架构、协议、集成、生命周期、诊断、证据级别、限制和许可。原有九篇文档均新增完整翻译，本审计再增加一组。英文为主维护版本，中文版均有语言导航。

## 例外与来源

没有维护的 Markdown 文档获得豁免。纯文本 LICENSES 目录中的上游原始法律声明和根目录 LICENSE 保持原样、不翻译；它们不是项目维护的技术 Markdown。被忽略的构建输出和外部依赖检出目录属于生成/参考材料，不是正式文档。历史镜像路径、哈希、源码版本和外部 UART 观察继续作为历史证据保留，不代表当前版本已经烧录。

## 一致性审查

两种语言都保留各文档主要章节、公共 API 名称、命令、配置、测试状态、限制、警告及许可引用。审查保留 Apple NCM 仅 RX/原始 Ethernet TX、Legacy RX 前缀、首次 Bulk IN 就绪、Carrier 格式、外部 USB 配置选择、Interface Claim 所有权、取消失败隔离和持久 netif 生命周期。驱动就绪继续与 USBMUX/Lockdown 信任及 CarPlay 会话就绪分离。

检查器比较标题层级序列、fenced code/命令、行内技术字面量和外部 Markdown 引用 URL。它检查所有维护文档的语言配对、导航、命名、相对链接、标题锚点、主版本语言分离及明显硬换行。这些机械指标辅助人工翻译审查；字面量和标题数相同不能证明语义等价。不访问外部网站，因此 CI 不会因外网故障失败。裸写的历史文件系统路径和源码标识符按上下文审查，不要求它们存在于读者机器上。

## 本地验证

Windows Python 文档审计和 WSL Ubuntu 24.04 Python 审计均通过全部十组文档，Markdown 豁免数为零。检查器使用隔离临时 Git 仓库的故障注入测试，两平台均通过；覆盖缺失/孤立翻译、导航、文件/标题链接、硬换行、人工断行、命名、代码/字面量/引用漂移及语言分离。基于实际外部固定版本 CherryUSB 检出目录，CTest Debug 和 ASan/UBSan 分别通过全部三个驱动测试。这些是本地结果，独立于历史公开 CI。

复现文档检查：

```sh
python3 scripts/test-check-docs.py
python3 scripts/check-docs.py
```

从 Luban-Lite 子模块布局复现驱动检查：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCHERRYUSB_ROOT=../cherryusb
cmake --build build -j8
ctest --test-dir build --output-on-failure
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DIPHETH_SANITIZE=ON -DCHERRYUSB_ROOT=../cherryusb
cmake --build build-san -j8
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-san --output-on-failure
```

Host workflow 在独立 job 中运行文档故障注入测试和审计，保留原有普通/sanitizer 驱动矩阵。格式配置禁止 prose 自动折行，并关闭 markdownlint 行宽限制。当前版本硬件回归为 NOT_RUN；持续负载、物理重连、legacy 模式和更多设备证据仍按验证记录所述待完成。
