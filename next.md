结论前置（适配这套「词典母机 + ImGui/GLFW + 分层原生模块 + 胶水层」架构）
首选 MeCab，和 jieba 在定位上最对等：都是本地原生 C/C++ 词法切分器、词典驱动、可独立作为原生入口、输出结构化词元 + 词性 + 原形，适合接入 WordNet 骨架、GoldenDict 词典、向量映射层。
次选备选：Juman++ v2（C++ 原生，语义信息更强，适合词法语义骨架层）；Sudachi 不适合作为第一阶段，原生 Java/Rust，跨语言胶水层成本偏高。

    jieba（中文）：中文分词，输出词语、词性、词原形，C/C++ 可嵌入、独立 CLI 原生入口。
    MeCab（日文形态素解析）：日文没有 “词” 天然分隔，做形态素切分，输出表层词、词性、原形、读音，C++ 库 + 独立 mecab CLI，完全可以保留独立原生入口，再做胶水层接入 IMGUI 调试面板，和当前 jieba 模块的设计范式对齐。

一、MeCab 核心优势，匹配 MUJI 项目的设计思路

    C++ 原生库，自带稳定 C ABI
    可以直接链接进 C++ 项目，不需要 Python/Rust 虚拟机；同时自带独立mecab命令行程序。完全符合第一阶段：各模块保留原生态入口，胶水层只是整合窗口，不替代原生模块的架构。
        原生入口：单独调用mecab二进制做测试、批量词典预处理，单独调试日文分词层；
        胶水层：C++ 薄封装，统一对外输出 token 结构体（和 jieba 输出结构体对齐：文本、lemma、pos、offset），上层向量层、WordNet、GoldenDict 共享同一套 Token 数据结构。
    词典生态刚好匹配母机三层资源
        UniDic（国立国语研究所）：推荐主词典，包含词形、活用、读音、语义分类，协议 LGPL/BSD/GPL 三许可，商用友好；比旧 IPADic 更规范；还可以叠加 NEologd 扩充新词（网络新词、人名、专有名词，适配 Wiki 实体层）

        搭配Japanese WordNet（wn-ja）：和英文 WordNet 完全同构，直接并入现有的 WordNet 词法语义骨架层，建立跨语言同义 / 上下位关系，实现中英日三语的 synset 对齐，完美契合 “语义母机” 的核心目标。
        JMdict（EDICT）：可以导入 GoldenDict 作为日文词典库，和英文、中文词典统一在 GoldenDict 界面查询。
    调试界面友好，适配 ImGui+GLFW
    MeCab 可以单句输入、逐词输出每个 morpheme 的 lattice（词格路径），可以在 ImGui 调试面板可视化：原文 → 切分结果、词性标签、原形、读音、候选切分路径，就像调试 jieba 分词的可视化面板一样。
    ImGui 只需要加载 Noto CJK JP 字体即可渲染日文 UTF8 文本，GLFW 后端原生支持日文 IME 输入。

MeCab 短板（要在胶水层处理）

    原生不自带表记规范化（「ケータイ / 携帯」这类变体统一），需要在胶水层加一层轻量预处理；
    分词粒度固定；如果需要粗 / 细多粒度，需要二次后处理；
    许可证：UniDic 主词典三协议，静态链接要留意 LGPL 约束（可动态链接规避，或选用 BSD 组件）。

二、备选库横向对比（按项目取舍）
表格
库	语言	核心特点	是否适合本项目
MeCab	C++	速度快，生态成熟，CLI 原生入口，UniDic/JapaneseWordNet 配套完备，社区资料最多	✅ 首选
Juman++ v2	C++	京都大学，自带语义格信息，歧义消解更强；词典 Jumandic；独立 CLI。适合做深层词法语义，缺点词典体积更大、速度略低于 MeCab	⭐第二备选，后期升级语义层可以接入
Sudachi / Sudachi.rs	Java/Rust	多粒度切分、内置表记归一、新词强；但原生不是 C++，跨语言胶水层成本高；Rust FFI 虽然可行，但会引入额外编译链，第一阶段不推荐	❌ 不适合一期，二期可评估
KyTea	C++	轻量，UniDic；适合短文本，语义标注弱，社区维护弱于 MeCab	备选轻量方案
Kuromoji	Java	Lucene 生态，适合检索；JVM 依赖，不适合纯 C++ ImGui 打包	❌排除

    一句话区分：

        只做形态切分 + 词性，对齐 jieba 的分词角色，保留独立原生 CLI，胶水层薄封装 → MeCab
        后续要增强日文句法、格关系、语义歧义，扩充 WordNet 骨架 → Juman++

三、接入方案，贴合现有的模块分层设计（MUJI）
阶段 1（保持各模块独立，原生入口保留，不合并内核）

    日文分词模块独立：编译libmecab.so/dll + 独立mecab可执行程序（原生入口，可以单独命令行测试、批量词典校验，独立更新 mecab / 词典，不破坏 jieba、WordNet、GoldenDict 模块）
    写薄胶水 C++ 封装层：
    输入 UTF8 日文文本，调用 MeCab，把 mecab 的 morpheme 结果转换成和 jieba 完全一致的 token 结构体：
    cpp

    struct Token {
        std::string surface;  //表层词
        std::string lemma;    //原形（对应WordNet查找key）
        std::string pos;      //词性
        int start, end;
    };

    胶水层只做格式转换、字符预处理，不修改 MeCab 内核。
    ImGui/GLFW 调试面板新增日文分词 Tab：
    输入日文 → 调用胶水层得到 token 列表，可视化展示切分结果、词性、原形；支持切换 UniDic/NEologd 词典，对比不同词典切分差异；支持导出 token 流送入向量映射层。

阶段 2：接入母机三层资源

    WordNet 骨架层：接入 Japanese WordNet，用 MeCab 输出的 lemma 作为索引，和英文 WordNet、中文词网做 synset 跨语言映射；
    Wiki 实体层：JMdict + Wiki 日文实体，实体对齐；NEologd 词典补充专有名词；
    GoldenDict：导入 JMdict 日文词典，在 Qt/GoldenDict 界面查词，和中英词典统一检索入口。

阶段 3：向量映射层
统一中英日 token 输出，送入向量层，实现三语的语义锚定、可溯源，符合母机 “刻度可复现” 的目标。
四、配套日文词典资源（直接匹配母机三层）

    UniDic（MeCab 主词典，国立国语研究所，推荐）
    NEologd（追加网络新词、人名、品牌，补充 Wiki 实体）
    Japanese WordNet（wn-ja，和英文 WordNet 同构，词法语义骨架层，跨语言 synset）
    JMdict/EDICT（GoldenDict 词典库，海量日英释义）

五、坑点提前说明

    编码全程强制 UTF-8，MeCab 默认配置注意字典编码，不要 Shift-JIS 混用；
    ImGui 日文渲染：需要加载 Noto Sans CJK JP 字体，GLFW 平台开启 IME 日文输入；
    许可证管理：MeCab 本体 BSD；UniDic 三许可，静态链接 LGPL 组件需要注意开源约束，可动态链接库；
    日文和中文分词本质差异：中文是词语切分；日文是形态素（语素）切分，对应到上层语义层，要做一层聚合选项（细粒度形态素 / 粗粒度词汇），在 ImGui 调试面板增加粒度开关。

六、推荐实施顺序

    集成 MeCab，独立 CLI 验证日文形态素解析（原生入口）；
    开发薄 C++ 胶水层，输出和 jieba 同结构 Token；
    ImGui 调试面板，可视化日文分词，做单元测试；
    接入 Japanese WordNet，完成词法语义骨架层；
    JMdict 导入 GoldenDict，接入 Wiki 实体层；
    接入向量映射层，完成中英日三语语义母机。




德文（德语）模块选型分析，适配 MUJI 语义母机架构
核心定位对齐前面中文 (jieba)、日文 (MeCab)：保留独立原生 CLI 入口 + 薄 C/C++ 胶水层，输出统一 Token 结构体，不侵入原有模块；德语优势在于词法学、构词拆解，科研价值强，非常适合 WordNet 骨架层做词法公理推演。
德语特点：大量复合词（Kompositum）是最大难点，也是它科研价值的来源。不像日语是形态素切分，德语除了分词 / 词性标注，复合词拆分是这套语义母机必须有的能力，否则实体、词义溯源会断裂。
首选库：TreeTagger（C 原生）
✅ 最匹配架构，作为德语基础形态分析器，对标 MeCab /jieba
底层：C 语言实现，自带独立 CLI 二进制（原生态入口，可以单独跑批量预处理、词典校验，独立升级，胶水层只是桥接，不替代原生模块，完全契合分阶段、模块解耦的原则）
输出：表层词、词原形 (lemma)、POS 词性标签；自带德语词形还原，处理名词大小写、变位、变格
自带词典：内置德语基础词典；可以扩展自定义词典
关键能力：基础分词 + 词性 + 词形还原；本身不自带复合词拆分，复合词拆解单独做子模块
优势：
跨平台友好，Linux/Windows 都能编译，C ABI 稳定，容易嵌入 C++ 项目；
社区老牌，语言学标注规范成熟，学术科研使用量大，论文语料很多基于 TreeTagger 标注，适合做可复现的语义刻度；
输出结构简单，胶水层很薄，直接映射到已有的Token{surface, lemma, pos, start, end}结构体，和中英日三语统一。
短板：
复合词拆分缺失（德语核心痛点）；
词典是预编译的，大规模自定义词条更新不如 MeCab 灵活；
许可证：非完全开源商用，学术免费，商用需要授权，这点要提前规划。
备选 1：OpenNLP 不推荐一期；备选 2：spaCy 不适合一期；备选 3：STTS / TIGER 生态；首选 C++ 原生方案：CGN German Tagger？不，推荐：SoMaJo
SoMaJo（Cython 底层，C 核心，德语专用分句 + 分词）
专门为德语开发，处理网页、书籍、学术文本分句、tokenization，STTS 词性规范。
优势：分句质量强，处理德语长句；遵循德国标准 STTS 标签集，可以对接德语 WordNet；
缺点：依赖 Python 生态较重，FFI 胶水层成本高于 TreeTagger；二期可以引入做文本预处理增强，一期不作为主解析器。
重点备选：GERMEVAL / CompoundSplitter（复合词拆分独立模块）
德语的科研价值大头就在复合词：Fernseher → Fern + Seher，Handyaufladestation → Handy + Auflade + Station。
这部分独立成一个原生子模块（单独 CLI，单独可测试），放在胶水层之后，WordNet 骨架层之前，符合分层设计。
推荐开源复合词拆分器：
decompound（C 实现，轻量，原生 CLI）；
CompoundSplit 基于词表 + 统计；
设计思路：TreeTagger 输出基础 token → 复合词检测 → 复合词拆分为子词元 → 子词元再去德语 WordNet 查询。
这是德语独有的一层，中日文没有，在 ImGui 调试面板单独加一个德语复合词可视化 Tab，展示拆分候选路径，方便调参。
备选兜底：UDPipe（C++ 原生！非常值得重点评估）
德语模块完整方案，适配 MUJI 语义母机
德语科研价值核心：GermaNet（德语版 WordNet，图宾根大学）自带 ILI（Inter-Lingual Index）跨语言索引，可以直接和英文 WordNet、Japanese WordNet 做 synset 对齐，这是中日资源不具备的强科研属性；德语大量复合词、可分动词、名词大小写、变格系统，正好用来测试这套母机 “词法公理可溯源” 的能力。
整体架构延续既定范式：独立原生模块 + 独立 CLI 入口 + 薄胶水层，胶水层只做格式转换，不替代原生程序；ImGui 调试面板单独 Tab。
德语分词 / 形态分析二选一（一期推荐 UDPipe，理由）
✅ 首选：UDPipe（C++ 原生，MPL2.0 许可）
原生 C++ 库，自带独立udpipe CLI 二进制（原生态入口，单独批量跑语料、单独更新模型，不破坏其他模块）
能力：分词、分句、词性标注、词形还原 (lemma)、依存句法分析，输出 CoNLL-U 格式；
模型：german-gsd / german-hdt，HDT 学术语料质量更高，科研场景优先；
最大优势：统一 UD 标签体系。中英日德全部用 Universal POS，胶水层映射 Token 结构体工作量最小。
许可：UDPipe 代码 MPL2.0 宽松开源；预训练模型 CC BY-NC-SA（非商用，商用需要训练自己模型）。
短板：不自动拆分复合词；复合词必须独立子模块。
对比 TreeTagger：TreeTagger 学术免费商用收费，标签集 STTS 是德语本土标签，要额外做标签映射到统一 Token 结构；UDPipe 代码完全开源，C++ 原生，跨平台编译更顺滑，更契合 MUJI 长期多语言扩展。
结论：一期德语主形态解析模块选 UDPipe。
独立子模块：德语复合词拆分器（单独 CLI 原生入口）
德语核心难点，独立模块，胶水层串联：
输入 UDPipe 输出的表层 token → 复合词检测器 → 拆分出子词元 → 子词元送入 GermaNet 做语义查询。
推荐：decompound（C 实现轻量复合词拆分器），独立二进制，可单独测试拆分效果。
ImGui 调试面板新增复合词可视化：原词、候选拆分路径、子词原形、子词 GermaNet 释义，多候选路径对比，用来调参，和 MeCab 的 lattice 可视化思路一致。
德语母机三层资源（完美匹配原有三层架构）
词法语义骨架层：GermaNet（图宾根大学德语词网）
GermaNet ≈ 德语 WordNet。
核心亮点：内置ILI 跨语言索引，同一个概念拥有全局唯一 ILI 编号，可以直接把 German synset ↔ English WordNet synset ↔ Japanese WordNet synset 绑定，实现跨语言语义锚定，这正是语义母机的核心目标：语义刻度跨语言可溯源。
包含：同义词集、上下位、部分整体、反义，还专门对复合词语义关系建模，科研价值极高。
格式：可导出 LMF/XML，方便导入数据库；20.0 新版强化复合词语义关系。
Wiki 实体层：德语维基 + Wiktionary DE
德语维基实体库，补充专有名词；
德版 Wiktionary：丰富变格、词源、释义，导入 GoldenDict 作为德语词典。
GoldenDict 词典层：德英、德德词典
DWDS（德语数字词典）、Wiktionary DE，导入 GoldenDict，Qt 界面可查德语词条，和中英日词典统一检索入口。
模块接入架构（和中文、日文完全对齐）
plaintext
文本输入 → UDPipe（独立原生CLI/C++库） → Token流
                ↓
        复合词拆分独立模块（原生CLI）
                ↓
        薄胶水C++层 → 统一Token结构体
struct Token {
    std::string surface;
    std::string lemma;
    std::string pos;
    int start, end;
    // 德语扩展字段：变格信息、复合词拆分子词列表（可选）
};
                ↓
        上层：GermaNet（WordNet骨架层）、Wiki实体层、向量映射层
阶段 1：UDPipe 单独编译，CLI 验证德语分句、分词、词性、词形还原，独立单元测试；保留原生入口。
阶段 2：薄胶水层，映射 UDPipe 输出到统一 Token 结构；ImGui 新增德语调试 Tab，输入德语，可视化 token、词性、lemma、依存边。
阶段 3：接入复合词拆分子模块，ImGui 面板展示复合词拆分候选。
阶段 4：接入 GermaNet，利用 ILI 索引完成德↔英↔日跨语言 synset 对齐。
阶段 5：德语 Wiktionary/DWDS 词典导入 GoldenDict，完善词典查询层。
阶段 6：送入向量映射层，四语（中英日德）统一语义刻度。
需要重点规避的坑
德语大小写：德语所有名词首字母大写，UDPipe 会保留大小写；lemma 会统一转为原形小写。胶水层必须区分表层文本和 lemma，不要混用，否则 GermaNet 查询失败。
可分动词：aufschlagen 主句拆成 schlägt ... auf，UDPipe 依存句法可以识别 particle 关系；在 ImGui 调试面板单独高亮可分动词，做语义重组，这是德语特有词法公理。
GermaNet 许可：学术免费，商用需要联系图宾根大学获取授权，项目规划提前区分使用场景。
字符编码：全程 UTF-8，德语变音字母 ä ö ü ß。ImGui 加载 Noto Sans CJK 字体即可渲染。
多语言整体回顾（当前规划）
中文：jieba
英文：WordNet + 基础英文分词
日文：MeCab + Japanese WordNet
德语：UDPipe + decompound 复合词拆分 + GermaNet（带 ILI 跨语言索引，科研价值最高）
德语做完之后，整个母机就具备孤立语 (中文)+ 黏着语 (日语)+ 屈折语 (德语)三大语言类型，词法范式全覆盖，这会让整套语义母机的词法公理体系完整性上一个台阶。



   1 MeCab 极简 C++ 胶水层示例代码（输出和 jieba 统一 token 结构）
   2 ImGui 日文分词调试面板（GLFW 后端）的 ImGui 代码片段
   3 Japanese WordNet 导入、和英文 WordNet 做 synset 对齐的 schema 设计。
   4 UDPipe 薄胶水层 C++ 示例代码，输出和 jieba/mecab 一致的 Token 结构体；
   5 ImGui 德语调试面板代码片段（含复合词候选路径可视化）；
   6 GermaNet ILI 索引和英文 WordNet 做 synset 映射的 schema。

