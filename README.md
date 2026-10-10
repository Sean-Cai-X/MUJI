# MUJI — Mother-machine for Unified Semantics

**Commit `f484c41`** (S12, 2026-10-05)

显式、可溯源、可校准的语义基准机床。五层架构，每层一个原生模块 + 一个胶水适配器 + 一个 ImGui 面板。原生入口完整保留，胶水层零侵入。

---

## 1. 架构一览

```
┌─────────────────────────────────────────────────────────────────────┐
│                    ImGui 6-Panel Workspace                          │
│    main.cpp — one-shot GLFW/OpenGL3 render, no while-loop deadlock  │
│    L0 cppjieba │ L1 WordNet │ L2 GoldenDict │ L2b StarDict         │
│    L3 Kiwix ZIM│ L4 Vector  │ Pipeline (L0→L4 auto-run)             │
├─────────────────────────────────────────────────────────────────────┤
│            SemanticBus + AbstractAdapter + SemanticTypes.h          │
│            (胶水契约: 统一注册, 统一状态, 统一数据结构)                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  原生模块              胶水适配器              原生 CLI              │
│  ─────────────────────────────────────────────────────────────     │
│  cppjieba (header)    JiebaAdapter      →  build/bin/jieba_cli      │
│  libwordnet.so        WordNetAdapter    →  build/bin/wn_cli         │
│  dictd (RFC 2229 TCP) GoldenDictAdapter →  build/bin/dict_cli        │
│  StarDict (.ifo/.idx) StarDictAdapter   →  build/bin/stardict_cli    │
│  libzim 9.x           KiwixAdapter      →  build/bin/kiwix_cli       │
│  (stub, 纯 C++)       VectorAdapter     →  (GUI 内嵌, 无独立 CLI)    │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│  SemanticContext — 跨层数据流 bag                                    │
│  tokens → synset_candidates → definitions → zim_articles → vectors  │
│  每个元素都带 SourceTag (JiebaToken / WordNetSynset / StarDict / …)  │
└─────────────────────────────────────────────────────────────────────┘
```

### 关键设计原则

| 原则 | 实现 |
|------|------|
| 原生入口不接管 | 6 个独立 CLI，零胶水层依赖可编译运行 |
| 胶水层只做协议转换 | 不重写原生逻辑，只翻译数据格式 |
| One-shot GLX 兼容 | `glfwSwapInterval(0)` + 单帧 `glfwSwapBuffers`，Live ISO 不死锁 |
| SourceTag 全链路溯源 | 每个 DictEntry / Synset / ZimArticle / Vector 维度都带来源标签 |
| 可解释相似度 | TraceableVector 每一维带来源，相似度可拆解为多个贡献项 |

---

## 2. Live ISO 运行指南

假设 Live ISO 已启动，`/H` 挂载到你的持久化磁盘分区，Trae 也跑在同一个 Live ISO 里。

### 2.1 首次启动 — 安装依赖 + 构建

```bash
# 进入项目根目录 (Live ISO 里 /H 是挂载点)
cd /H/aisrc/MUJI

# 安装系统依赖 (只需要做一次 — apt 缓存自动存到 /H/usr/aisrc/apt/)
sudo apt update
sudo apt install -y \
    cmake build-essential pkg-config \
    libglfw3-dev libgl1-mesa-dev \
    libwordnet-dev wordnet \
    dictd dict-gcide dict-wn stardict-tools \
    libzim-dev libkiwix-dev

# 3rdparty 已 gitignore 但需要手动拉取 (vendored 不进 git 大仓库)
# — cppjieba 中文分词头文件库
git clone --depth 1 https://github.com/yanyiwu/cppjieba.git 3rdparty/cppjieba
rm -rf 3rdparty/cppjieba/.git
# — ImGui docking 分支
git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git 3rdparty/imgui-docking
rm -rf 3rdparty/imgui-docking/.git

# 解压缩 StarDict 37 个词典 + 预解压 .dict.dz
for f in dic/stardict-*.tar.bz2; do tar xjf "$f" -C dic/extracted/; done
find dic/extracted -name "*.dict.dz" | xargs -I{} dictunzip {}

# CMake configure + build
cmake -S . -B build
cmake --build build -j$(nproc)
```

> 注：Live ISO 里 `git clone github.com` 可能不通（HTTP2 framing / connect timeout）。
> 如果 3rdparty 拉不到，从本地 bare repo 复制：
> `cp -a /path/to/cppjieba /path/to/imgui-docking /H/aisrc/MUJI/3rdparty/`
> 或者用 `https://deb.debian.org/debian/pool/` 镜像下载。

### 2.2 启动 dictd（GoldenDict 数据源）

```bash
sudo service dictd restart
build/bin/dict_cli list      # 应显示 gcide + wn 两个 DB
build/bin/dict_cli define memory -d wn
```

### 2.3 启动 6-Panel ImGui 工作台 + 自动截图

```bash
# DISPLAY 必需（Live ISO 默认有 Xorg）
# MUJI_SCREENSHOT 设了就把 PPM 写到指定路径然后直接退出（one-shot 模式）
cd /H/aisrc/MUJI
DISPLAY=:0.0 MUJI_SCREENSHOT=/H/usr/aisrc/out/s13.ppm build/muji

# 转换 PPM → PNG
convert /H/usr/aisrc/out/s13.ppm /H/usr/aisrc/out/s13.png

# 查看 stderr 日志
cat /tmp/muji_stderr.log
# 应看到: StarDict ready (37 dicts), Kiwix: no .zim files (可忽略), Vector ready
```

### 2.4 所有原生 CLI 独立运行

```bash
cd /H/aisrc/MUJI

# L0 — 中文分词
build/bin/jieba_cli -m ex "NUMA架构下，跨节点内存访问延迟高于本地内存"

# L1 — WordNet
build/bin/wn_cli memory -n -s

# L2 — GoldenDict (dictd TCP)
build/bin/dict_cli define memory -d wn

# L2b — StarDict 37 词典
build/bin/stardict_cli list -D dic/extracted
build/bin/stardict_cli define memory -D dic/extracted

# L3 — Kiwix ZIM (等 dic/zim/ 里放了 .zim 文件就能用)
build/bin/kiwix_cli list -D dic/zim
build/bin/kiwix_cli title NUMA -D dic/zim
```

### 2.5 日常开发循环（Live ISO + Trae）

```bash
# Live ISO 重启后，/H 自动挂载，项目代码全部在 /H 上持久化
# 不需要重新 git clone — 代码就在那儿
cd /H/aisrc/MUJI

# 如果 dictd 挂了
sudo service dictd restart

# 如果想从头重建 (通常不需要，cmake --build 够了)
rm -rf build && cmake -S . -B build && cmake --build build -j$(nproc)

# 提交到本地 bare repo (NO GitHub push — 这是规则)
git add -A && git commit -m "your message" && git push origin master
# origin → /H/aisrc/MUJI.git (本地 bare repo)
```

### 2.6 添加新的 ZIM 离线百科

```bash
# 下载一个小的（从 library.kiwix.org，需要能访问外网）
# 或者从别人那里拿 .zim 文件
cp /path/to/wikipedia_en_nopic_mini_*.zim /H/aisrc/MUJI/dic/zim/

# 验证
build/bin/kiwix_cli list -D dic/zim
build/bin/kiwix_cli title NUMA -D dic/zim

# 重启 muji — Pipeline auto-run 会自动把 ZIM 结果灌进 SemanticContext.zim_articles
DISPLAY=:0.0 MUJI_SCREENSHOT=/H/usr/aisrc/out/s14.ppm build/muji
```

---

## 3. Trae 里继续开发的工作流

Trae 直接打开 `/H/aisrc/MUJI` 作为 workspace。所有代码在 `/H` 上持久化，Live ISO 重启后还在。

### 3.1 Trae 需要知道的项目事实

| 事实 | 值 |
|------|------|
| Git root | `/H/aisrc/MUJI` |
| 本地 bare repo | `/H/aisrc/MUJI.git`（push destination） |
| **永远不要** push | GitHub / Gitea / 任何外网仓库 |
| apt 缓存自动存 | `/H/usr/aisrc/apt/` |
| 运行产物输出 | `/H/usr/aisrc/out/` |
| CMake 构建目录 | `/H/aisrc/MUJI/build/` |
| dict 源文件 | `/H/aisrc/MUJI/dic/*.tar.bz2`（git 跟踪，已解压的在 `dic/extracted/` 被 gitignore） |
| 3rdparty vendored | `3rdparty/cppjieba/`, `3rdparty/imgui-docking/`（gitignore） |

### 3.2 Trae Agent 编码规则

1. **Adapter 类** 必须继承 `muji::AbstractAdapter`，构造时传 name 给基类：
   ```cpp
   class FooAdapter : public AbstractAdapter {
   public:
       FooAdapter() : AbstractAdapter("Foo") {}
       bool init() override;   // 设置 set_status(Ready)
       bool shutdown() override;
   };
   ```
2. **注册到 SemanticBus**：
   ```cpp
   auto foo = muji::make_and_register<muji::FooAdapter>();
   foo->init();
   ```
3. **Pipeline auto-run 里加新层**：在 main.cpp 的 `AUTO-RUN` 块里 `#ifdef MUJI_HAS_FOO` 包起来，按 adapter 是否 Ready 保护。
4. **SemanticContext** 里加新字段，先在 `src/data/SemanticTypes.h` 里加，带 SourceTag。
5. **新 SourceTag** 加在 `SourceTag` enum + `to_cstr()` switch 里。

### 3.3 Trae 常用编码循环

```bash
# 改完代码 → 构建 → 截图验证
cd /H/aisrc/MUJI
cmake --build build -j$(nproc) 2>&1 | tail -20
DISPLAY=:0.0 MUJI_SCREENSHOT=/H/usr/aisrc/out/iter.ppm build/muji
convert /H/usr/aisrc/out/iter.ppm /H/usr/aisrc/out/iter.png
# Read tool 打开 /H/usr/aisrc/out/iter.png 验证 GUI
```

### 3.4 Git 工作流（Trae Agent 执行时用）

```bash
# Agent 发现改动后：
git add -A
git commit -m "S13: <简洁描述，列出新功能>"
git push origin master      # 只推本地 bare
# 注意：commit message 里不要写 GitHub / Gitea URL — 这是规则
```

### 3.5 常见坑 & 解法

| 问题 | 解法 |
|------|------|
| `muji_exit=124` / `SwapBuffers` 死锁 | 必须 `glfwSwapInterval(0)` + one-shot 单帧，Live ISO GLX 不能 vsync |
| `dict_cli` connect() 挂死 | Linux `connect()` 不吃 `SO_SNDTIMEO`，必须 `fcntl(O_NONBLOCK)` + `select()` 超时 |
| `stardict_cli list` 返回 0 | `.dict.dz` 必须先 `dictunzip` 解压成 `.dict` |
| `kiwix` adapter 编译不过 `Entry` 不能默认构造 | libzim 9.x：`Entry e = arch.getEntryByTitle()` 直接赋值，不要 `Entry e;` |
| GitHub clone 不通 | 用 `https://deb.debian.org/debian/pool/` 下载源码包，或本地 bare repo 复制 |
| Trae Agent 误删大量文件（之前发生过） | `git checkout -- src/` 一键恢复 HEAD；**不要**让 Agent 做 `rm -rf` 类操作 |
| cppjieba `FATAL exp: [ifs.is_open()]` | 必须从项目根目录运行 `build/bin/jieba_cli`，它用相对路径找 `3rdparty/cppjieba/dict/` |

---

## 4. 目录结构

```
/H/aisrc/MUJI/
├── CMakeLists.txt              # 构建系统 (auto-detect 各层可选依赖)
├── .gitignore                  # build/, dic/extracted/, imgui.ini, kiwix-desktop-qt/
├── AGENTS.md                   # Trae Agent 行为规范
├── README.md                   # 本文档
│
├── 3rdparty/                   # vendored, gitignore
│   ├── cppjieba/               # header-only 中文分词
│   └── imgui-docking/          # docking branch ImGui
│
├── src/
│   ├── main.cpp                # 6-panel ImGui 工作台 + Pipeline auto-run
│   ├── data/
│   │   └── SemanticTypes.h     # glue 契约: SemanticContext, DictEntry, TraceableVector, SourceTag
│   ├── glue/
│   │   ├── AbstractAdapter.h   # adapter 基类 (status/init/shutdown)
│   │   └── SemanticBus.h       # 单例 registry + active_context + recent_logs
│   ├── adapters/
│   │   ├── JiebaAdapter.h/cpp       # L0 cppjieba 封装
│   │   ├── WordNetAdapter.h/cpp     # L1 libwordnet.so C API 封装
│   │   ├── GoldenDictAdapter.h/cpp  # L2 dictd RFC 2229 TCP 封装
│   │   ├── StarDictAdapter.h/cpp    # L2b StarDict .ifo/.idx/.dict 封装
│   │   ├── KiwixAdapter.h/cpp       # L3 libzim 9.x .zim 封装
│   │   └── VectorAdapter.h/cpp      # L4 TraceableVector stub
│   └── native/                 # 原生独立 CLI (无 ImGui / 无胶水)
│       ├── jieba_cli.cpp
│       ├── wn_cli.cpp
│       ├── dict_cli.cpp
│       ├── stardict_cli.cpp
│       └── kiwix_cli.cpp
│
├── dic/                        # 词典 + ZIM 源文件 (git 跟踪 .tar.bz2)
│   ├── stardict-*.tar.bz2      # 37 个 StarDict 词典 (中英/法/德/日/韩/俄/维/马/越/医学)
│   ├── *.mdx / *.mdd           # 牛津/柯林斯/金山词霸 (.mdx 格式, 未解析)
│   ├── wordnet.dwm             # 旧格式 WordNet
│   ├── zim/                    # 放 .zim 离线百科 (目录已建, 等文件)
│   └── extracted/              # (gitignore) 解压后的 37 个 StarDict 词典, 596MB
│
└── build/                      # (gitignore) CMake 产物
    └── bin/
        ├── muji                # 6-panel ImGui 工作台
        ├── jieba_cli           # 5 个原生独立 CLI
        ├── wn_cli
        ├── dict_cli
        ├── stardict_cli
        └── kiwix_cli

/H/aisrc/MUJI.git               # 本地 bare repo (push destination, gitignore 不覆盖)
/H/usr/aisrc/                   # 所有外部工作输出 (AGENTS.md 里规定)
├── apt/                        # apt 包镜像缓存
├── out/                        # screenshot .ppm/.png (s11.png, s12.png, ...)
├── tasks/                      # 任务列表
├── logs/                       # 日志
└── ...
```

---

## 5. CMake 检测矩阵

CMake 启动时自动探测各层依赖：

| 层 | 检测方式 | 编译宏 | 没装会怎样 |
|----|---------|--------|-----------|
| L0 cppjieba | 3rdparty vendored (必须存在) | — | FATAL_ERROR，build 中止 |
| L1 WordNet | `find_library(WORDNET_LIB wordnet)` | `MUJI_HAS_WORDNET` | adapter 跳过编译，panel 不显示 |
| L2 GoldenDict | `find_program(DICTD_BIN dictd)` | — | 编过但 init 会失败，panel 显示 "GoldenDict not ready" |
| L2b StarDict | 纯 C++，无外部 deps | `MUJI_HAS_STARDICT=ON` | 永远编过，只是 dic/extracted 为空时无词典 |
| L3 Kiwix | `find_library(ZIM_LIB zim)` | `MUJI_HAS_KIWIX` | adapter 跳过编译，panel 不显示 |
| L4 Vector | 纯 C++，无外部 deps | — | 永远编过（stub） |

---

## 6. 已知限制 & 待办

| 项 | 状态 | 说明 |
|----|------|------|
| **ZIM 离线百科** | 架构就绪，缺数据 | 外网下载被墙，`library.kiwix.org` 不通；`dic/zim/` 放了文件即启 |
| **.mdx/.mdd 词典** | dic/ 里有 6 个 | MDX 是 proprietary 二进制，需 vendor C++ parser；当前未解析 |
| **L4 Vector 真实实现** | stub | 现在用 def 长度 / synset 数构造 4 维，后续接 Eigen 做线性代数 |
| **L3 Entity engine** | SemanticContext 有 `entities` 字段，未填实 | 用 libkiwix 或 Wikidata dump 填 |
| **Pipeline 手动触发** | 已有 "Run Full Pipeline" 按钮 | auto-run 是为了 one-shot 截图验证；真实交互式用按钮 |
| **kiwix-desktop Qt → ImGui 移植** | `kiwix-desktop-qt/` 作参考 | 2.6MB 纯 Qt 源码，gitignore；后续按需移植 |

---

## 7. 快速检查表（Live ISO 启动后 30 秒内完成）

```bash
# [ ] 1. /H 已挂载
ls /H/aisrc/MUJI/README.md

# [ ] 2. dictd 运行
sudo service dictd restart; build/bin/dict_cli list

# [ ] 3. 构建产物在
ls build/bin/muji

# [ ] 4. 跑起来
DISPLAY=:0.0 MUJI_SCREENSHOT=/H/usr/aisrc/out/check.ppm build/muji

# [ ] 5. 所有原生 CLI
build/bin/jieba_cli -m ex "测试" && build/bin/wn_cli memory -n -s && build/bin/stardict_cli list -D dic/extracted | head -3

# [ ] 6. git push 目标正确
cd /H/aisrc/MUJI && git remote get-url origin   # → /H/aisrc/MUJI.git
```
