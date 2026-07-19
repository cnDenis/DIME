# DIME 预编译二进制词库格式（DIME Binary Dictionary, `.bin`）

> 状态：设计文档（对应二进制格式 version = 2）
> 适用范围：DIME（迪弥五笔输入法）五笔主词库与拼音词库
> 关联代码：`BinaryDictFormat.h`（运行时与构建工具共用）、`BinaryDictionaryEngine.*`（运行时读取器）、`build_bindict.cpp`（离线构建工具）、`TableDictionaryEngine.*`、`FileMapping.*`、`CompositionProcessorEngine.*`（加载与探测）

---

## 1. 动机

当前词库是 UTF-16 LE 的纯文本 `"编码"="词条"`（`wubi98.txt`，约 2.5 MB / 98,205 行）。每次 IME 在某个进程内激活，`CDictionaryIndex::Build` 都会：

1. 逐行解析全部 98,205 行；
2. 把每条词条的**值复制成堆上的 `std::wstring`**，存进 `unordered_map<wstring, vector<wstring>>` / `unordered_map<wstring, wstring>` / `vector<wstring>` 三套结构；
3. 排序 `_sortedKeys`。

同时原始 2.4 MB 的内存映射（mmap）缓冲区在会话期间一直保留（`_pDictionaryFile` 被引擎持有）。结果是：

- **内存 ≈ 文本的两倍**：mmap（2.4 MB）+ 复制出的词条堆字符串 + map/vector 节点开销，每进程约 4~5 MB；
- **启动开销**：每进程激活时都要扫描 + 分配 98k 次，约几十到一两百毫秒；
- 词条字符串被无谓地复制了一份。

目标：设计一种**自包含、可内存映射、运行时零解析**的二进制词库。加载时只 `MapViewOfFile` 一次，直接以指针读结构，**不产生任何词条字符串拷贝、不扫描全表**，内存降到接近原始文本大小。

---

## 2. 总体方案

- **单文件、自描述、可直接 mmap**：所有索引与字符串都落在同一个 `.bin` 文件里。
- **字符串池 + 偏移引用**：词条与编码字符串集中存放在“字符串池”，其它结构只保存 `(offset, length)` 指向池内。运行时返回的候选条目（`CStringRange`）直接指向 mmap 缓冲区，**零拷贝**。
- **有序数组 + 二分查找**：编码按字典序存为有序数组，精确/前缀/通配查找统一用 `lower_bound` + 线性扫描；反查（词→码）用另一份按词排序的数组。避免引入哈希表，格式更简单、对 mmap 友好、可校验。
- **小端序（LE）固定布局**：Windows 仅 LE，不做跨端兼容；所有多字节字段按 LE 存储，结构体按 4 字节对齐。
- **向后兼容**：加载器优先尝试 `<名>.bin`；缺失或校验失败（magic/版本/字节序/边界）则调用随包发布的 `build_bindict` 由 `.txt` 现场编译出 `.bin` 再加载，**不做文本索引（`CDictionaryIndex`）回退**。`.bin` 一旦通过校验即作为权威词库**直接加载**，运行时不再做“是否过期”的比对。

---

## 3. 文件布局

```
+-----------------------------------------------------------+
| Header (64 bytes, 固定)                                    |
+-----------------------------------------------------------+
| Config 块 (变长, UTF-16LE 文本, NUL 补齐到 4 字节对齐)     |
|   形如 "KEY:value\n" 的多行文本, 无 BOM; 可为空            |
+-----------------------------------------------------------+
| CodeEntry 数组 (有序, 每项 16 字节, 共 codeCount 项)        |
|   按编码字符串升序（大写、序数/ordinal 比较）               |
+-----------------------------------------------------------+
| WordRef 数组 (每项 8 字节, 共 wordPairCount 项)            |
|   与 CodeEntry.firstWordRef 配合定位某编码的全部词条        |
+-----------------------------------------------------------+
| ReverseEntry 数组 (有序, 每项 16 字节, 共 reverseCount 项)  |
|   按词条字符串升序，词条 -> 最短编码                        |
+-----------------------------------------------------------+
| StringPool (字符串池, 变长, 2 字节对齐)                    |
|   连续存放的 UTF-16LE 字符串, 去重, 无长度前缀/无终止符    |
+-----------------------------------------------------------+
```

- 所有 `offset` 字段均为**相对文件起始的绝对偏移（字节）**，运行时 `ptr = base + offset`。
- `codeEntryOffset` 同时充当 Config 块的结束边界：`Config 字节数 = codeEntryOffset - 64`。**旧版加载器（v1，无 Config 概念）只把 `codeEntryOffset` 当作数组起点，因此天然跳过这一间隙、不会误读配置区**——这是向后兼容的关键。
- 每个数组前部都是 4 字节对齐（Header=64、各 stride 为 16/8/16 的倍数，Config 块补齐到 `kConfigAlign=4`），StringPool 因此天然 2 字节对齐，满足 `WCHAR` 访问。

---

## 4. Header 字段（64 字节）

| 偏移 | 类型     | 字段                | 说明                                              |
|------|----------|---------------------|---------------------------------------------------|
| 0    | uint32   | `magic`             | `0x44494342`（`'D','I','C','B'` 的小端表示）      |
| 4    | uint32   | `version`           | 格式版本，本版 = `2`                              |
| 8    | uint32   | `flags`             | bit0 = 1 表示小端序（当前固定为 1）               |
| 12   | uint32   | `sourceSize`        | 源 txt 字节数的低 32 位（构建溯源元数据，运行时不再比对，见 §8）|
| 16   | uint32   | `codeCount`         | CodeEntry 数量（唯一编码数）                      |
| 20   | uint32   | `wordPairCount`     | WordRef 总数（所有编码的词条对数之和）            |
| 24   | uint32   | `reverseCount`      | ReverseEntry 数量（唯一词条数）                   |
| 28   | uint32   | `sourceMtimeLow`    | 源 txt 最后写入时间 FILETIME 低 32 位（构建溯源元数据）|
| 32   | uint32   | `codeEntryOffset`   | CodeEntry 数组起始偏移                            |
| 36   | uint32   | `codeEntryStride`   | = `16`                                           |
| 40   | uint32   | `wordRefOffset`     | WordRef 数组起始偏移                              |
| 44   | uint32   | `wordRefStride`     | = `8`                                            |
| 48   | uint32   | `reverseEntryOffset`| ReverseEntry 数组起始偏移                         |
| 52   | uint32   | `reverseEntryStride`| = `16`                                           |
| 56   | uint32   | `stringPoolOffset`  | StringPool 起始偏移                               |
| 60   | uint32   | `sourceMtimeHigh`   | 源 txt 最后写入时间 FILETIME 高 32 位（构建溯源元数据）|

> `sourceSize` / `sourceMtimeLow` / `sourceMtimeHigh` 由构建工具写入，作为**构建溯源元数据**保留在 Header 中。运行时加载器**不再读取或比对这些字段**来判断词库是否过期（以保持零解析）；它们可用于离线 `--check` 或人工排查。

---

## 5. 结构与字符串定义

字段顺序经过排布，确保**无隐式填充**，在 x86/x64 下 `sizeof` 恒定（见头文件中的 `static_assert`）。

### 5.1 字符串（StringPool 内）

StringPool 是**连续存放、去重后的 UTF-16LE 字符串**，既无长度前缀也无 `\0` 终止符。每个字符串的长度由引用它的 `*Len` 字段给出，`*Offset` **直接指向首个 `WCHAR`**，因此运行时可零拷贝地构造 `CStringRange(base + offset, len)`。

### 5.2 CodeEntry（16 字节，有序）

```c
struct CodeEntry {          // 共 16 字节
    uint32 codeOffset;      // 指向 StringPool 中该编码字符串首字符
    uint32 firstWordRef;    // 在 WordRef 数组中的起始下标
    uint16 codeLen;         // 编码长度（UTF-16 单元数）
    uint16 wordCount;       // 该编码对应的词条数
    uint32 reserved;        // bit0 = 该码至少有一个“仅常用字”过滤后仍保留的词条
                           //       （词组，或属于 GB2312 的单字）
};
```

数组按编码字符串（大写、序数比较）升序排列。

### 5.3 WordRef（8 字节）

```c
struct WordRef {            // 共 8 字节
    uint32 wordOffset;      // 指向 StringPool 中词条字符串首字符
    uint16 wordLen;         // 词条长度（UTF-16 单元数）
    uint16 reserved;        // 低 2 位 = 该单字的 GB2312 等级（见 §5.5）
                           //   0 = 不在 GB2312（生僻字或词组），1 = 二级，2 = 一级
};
```

第 `i` 个编码的词条为 `WordRef[codeEntry[i].firstWordRef .. +wordCount)`。词条顺序保留源文件中的出现顺序。

### 5.4 ReverseEntry（16 字节，有序）

```c
struct ReverseEntry {       // 共 16 字节
    uint32 wordOffset;      // 指向 StringPool 中词条字符串首字符
    uint32 codeOffset;      // 指向 StringPool 中“最短编码”字符串首字符
    uint16 wordLen;         // 词条长度
    uint16 codeLen;         // 编码长度
    uint32 reserved;        // 保留，置 0
};
```

数组按词条字符串（序数比较）升序排列。存“最短编码”与原 `CDictionaryIndex::_reverseMap`（取最短码，该类已移除，仅作历史对照）语义一致。

### 5.5 单字 GB2312 等级（用于“仅常用字”开关）

`WordRef.reserved` 低 2 位存放该**单字**的 GB2312 等级，构建期由 `build_bindict` 用
`WideCharToMultiByte(CP_GB2312=20936, WC_NO_BEST_FIT_CHARS, …)` 判定：

- `0` = 不在 GB2312（生僻/扩展汉字，或长度 > 1 的词组——词组按长度走“照常显示”规则，等级无关）；
- `1` = GB2312 二级汉字（区位码区 56–87，次常用）；
- `2` = GB2312 一级汉字（区位码区 16–55，最常用）。

运行时“仅常用字”过滤规则（`CBinaryDictionaryEngine::_IsWordKept`）：开启时，
**词组（wordLen > 1）一律保留；单字仅在等级 ≥ 1（属于 GB2312）时保留**，生僻单字被隐藏。
`CodeEntry.reserved` 的 bit0 缓存“该码是否含任一保留词条”，供通配符/前缀扫描整码跳过，
避免无谓展开。该过滤为 O(1) 位测试，不产生额外索引或二次扫描。

### 5.6 Config 块（Header 与 CodeEntry 之间的变长文本区）

`[kHeaderSize, codeEntryOffset)` 是一段 **UTF-16LE 纯文本（无 BOM）**，由零或多行
`KEY:value\n` 组成，承载词库自带的可读元数据（如显示名）。构建工具把它写到这里，
运行时不解析词条、只读这一段短文本，因此仍是“零解析”之外的极低成本开销。

- **编码**：UTF-16LE，无 BOM；整段以 `kConfigAlign(=4)` 字节为界用 `\0` 补齐，
  补齐字节被运行时 `ParseConfig` 当作行分隔符（与 `\r`/`\n` 同等处理）。
- **行格式**：`KEY:value`，`KEY`/`value` 两侧的空白（空格、Tab）由构建工具在写入时去除；
  未知 `KEY` 全部保留进 `DictConfig::raw`，运行时按需取用，便于后续扩展。
- **NAME 字段**：`NAME` 为词库显示名，运行时经 `CBinaryDictionaryEngine::GetName()`
  暴露。构建工具若未在源 txt 头部见到 `#@NAME:` 指令，会自动以**源文件名（去扩展名）**
  兜底，保证每个 `.bin` 都至少带一个可读名字。
- **向后兼容**：该段位于 `codeEntryOffset` 之前；旧版加载器（v1）只把
  `codeEntryOffset` 当数组起点，自然跳过这段间隙，不会误读配置。新加载器用
  `codeEntryOffset - kHeaderSize` 反推配置区长度，旧 `.bin`（该差为 0）则无配置。

---

### 5.7 源 txt 头部的 Config 指令（`#@KEY:Value`）

`build_bindict` 在解析词条前，先扫一遍文件**头部**直到首个非注释行，收集形如
`#@KEY:Value`（也兼容 `#@KEY=Value`）的指令行：

- 行首 `#` 之后若紧跟（可含空格）`@`，即视为 Config 指令，去掉 `#@` 前缀后写进 Config 块；
- `#` 开头但下一个非空字符不是 `@` 的行，视为普通注释，忽略；
- 空行（含纯空白行）忽略；
- 遇到**首个非 `#` 行**（即真正的 `"编码"="词条"` 词条）即停止扫描，其后出现的 `#@`
  指令不会被收集——配置只来自文件头部。

示例（`Dictionary/wubi98.txt` 第 1–2 行）：

```
# 这是五笔98海峰词库
# @NAME: 五笔98
```

运行时主解析循环同样跳过空行与 `#` 注释行，因此这些指令行不会被误当成词条。

---

## 6. 查找算法（全部零分配，返回指向池的 `CStringRange`）

设 `base` 为 mmap 基址，则：
- `codeEntries = (CodeEntry*)(base + codeEntryOffset)`
- `wordRefs    = (WordRef*)(base + wordRefOffset)`
- `revEntries  = (ReverseEntry*)(base + reverseEntryOffset)`
- `pool        = base + stringPoolOffset`

字符串比较统一用**大写、不区分大小写**（`towupper`），与现有 `_ToUpperKey` 行为对齐。

### 6.1 精确查找 `LookupExact(code)`
1. 在 `codeEntries[0..codeCount)` 上二分查找等于 `code`（大写）的 `CodeEntry`；
2. 命中后，取 `WordRef[firstWordRef .. +wordCount)`，为每条构造 `CCandidateListItem`：
   - `_ItemString.Set(pool + wordOffset, wordLen)`
   - `_FindKeyCode.Set(pool + codeOffset, codeLen)`
3. 未命中返回空。

### 6.2 前缀查找 `LookupPrefix(prefix, maxCount, &hasMore)`
1. `lower_bound(codeEntries, prefix)` 找到首个 ≥ `prefix` 的项；
2. 从该处向后遍历，凡 `code` 以 `prefix` 开头者，收集其词条；
3. 达到 `maxCount` 仍有多余则置 `hasMore = TRUE` 并停止；
4. 越界或前缀不匹配即停止。

### 6.3 通配符查找 `LookupWildcard(pattern, maxCount, &hasMore)`
1. 找到 `pattern` 中首个通配符（`Z` / `?`，大小写不敏感）；
2. 以通配符前的子串为 `prefix`，执行 §6.2 的 `lower_bound` + 前缀遍历；
3. 对每个候选 `code`，用 `_MatchesWildcardPattern`（逐字符比较，通配符位匹配任意）过滤；
4. 同样按 `maxCount` / `hasMore` 截断。

### 6.4 反查 `LookupCodeByWord(word)`
1. 在 `revEntries[0..reverseCount)` 上二分查找等于 `word` 的 `ReverseEntry`；
2. 命中则返回其 `codeOffset/codeLen`（指向池的 `CStringRange`）；未命中返回 FALSE。

> 以上四种查询与原 `CDictionaryIndex`（已移除）的公开方法签名一致，可作为其零解析替代。

---

## 7. 运行时加载器（接口规范）

新增 `CBinaryDictionaryEngine : public CBaseDictionaryEngine`，复用 `CBaseDictionaryEngine` 已有的
`CollectWord` / `CollectWordByPrefix` / `CollectWordForWildcard` / `FindCodeByWord` 虚接口，内部用 §6 算法实现。

加载流程（`CFileMapping` 已负责 mmap）：

```
LoadBinary(path.bin):
    hMap = CreateFileMapping(READONLY)
    base = MapViewOfFile()
    hdr  = (Header*)base
    if !ValidateHeader(hdr, fileSize)     -> 失败, 调用 build_bindict 由 .txt 现场编译后加载（不做文本索引回退）
        // 校验 magic / version(=2) / 字节序标志 / stride / 各段边界
    保存 base / 各数组指针 / 计数, _isBuilt = TRUE
    // 注意：不比对 sourceSize / sourceMtime，`.bin` 视为权威词库直接加载
```

- mmap 缓冲区在引擎生命周期内保持映射（指针才有效），与现有 txt 路径一致；
- 不分配任何词条字符串，候选条目直接引用池内数据；
- `CompositionProcessorEngine::_LoadDictionary` 改造为：**先尝试 `<名>.bin`，`ValidateHeader` 通过则构造 `CBinaryDictionaryEngine` 并直接使用**；`.bin` 缺失或 `ValidateHeader` 失败时，调用随包发布的 `build_bindict` 由 `.txt` 现场编译出 `.bin` 再加载，**不做文本索引（`CDictionaryIndex`）回退**。转换器缺失或执行失败则记日志、该词库本会话不可用。两路对外接口不变。

---

## 8. 版本与构建溯源（无运行时失效判定）

- `version` 字段用于格式演进；加载器校验 `version == kVersion(2)`，不符则拒绝并调用 build_bindict 由 `.txt` 重建。
- Header 中的 `sourceSize` / `sourceMtimeLow` / `sourceMtimeHigh` 由构建工具写入，记录生成该 `.bin` 的源 txt 大小与最后写入时间，属于**构建溯源元数据**。
- **运行时不再做“词库是否过期”的比对**：`.bin` 一旦通过 `ValidateHeader` 即作为权威词库直接加载，不会因源 txt 变化而自动回退。理由：避免加载时读取/哈希源文件，保持“零解析”收益；且 `.bin` 与 `.txt` 都来自同一份受版本控制的词库，过期风险低。
- 由此带来的运维约定：若源词库 `txt` 更新，**必须显式重新运行 `build_bindict` 生成新的 `.bin`**（部署脚本 `deploy_test.bat` 会在注册前自动完成）。若某个 `.bin` 已损坏/过期，直接删除它、重新构建即可；引擎不会替你“悄悄”回退到可能同样过期的 txt。
- `.bin` 被视为可重建产物（建议 gitignore，由构建/部署脚本生成）。

---

## 9. 构建工具（离线）

采用 **C++**（而非脚本语言）实现，理由：零新运行时依赖（复用现有 VS2022 工具链）、与运行时读取器共用同一份 `BinaryDictFormat.h`（单一事实来源，杜绝布局漂移）、无 GC/哈希遍历随机性、字节级可复现。

- 头文件：`BinaryDictFormat.h`（结构体、常量、`static_assert`，运行时与工具共用）。
- 工具：`src/build_bindict.cpp`，编译为独立控制台 exe（CMake target `build_bindict`）。

CLI：

```
build_bindict.exe <in.txt> <out.bin> [--max-code N] [--verify]
build_bindict.exe                              (无参数：编译 Dictionary\ 下全部 *.txt，拼音自动 --max-code 24)
```

- `--max-code N`：仅收录 key 长度 ≤ N 的行；省略则不限制长度（wubi 表本身 ≤4，limit 与否等价；拼音建议 24）。
- `--verify`：写完后重新 mmap 该 `.bin`，以零解析方式回读并比对 code→words / word→code，确认落盘内容与源 txt 一致；不一致则非零退出。
- 无参数模式：扫描 `Dictionary\*.txt`，为每个 `<名>.txt` 生成同级 `<名>.bin`（拼音自动 `--max-code 24`）。

算法：
1. 读入文本并**自动识别编码**：依 BOM 判定 UTF-16LE / UTF-16BE / UTF-8，无 BOM 时先按 UTF-8 严格解码、失败再回退系统 ANSI（中文机即 GBK），统一转成 UTF-16LE 后逐行解析（对齐 `CDictionaryParser::ParseLine`：`=` 分隔、去首尾空白、去成对引号、跳过空 key/空值/畸形行；key 统一大写以对齐 `_ToUpperKey`）；**主解析循环同时跳过空行与 `#` 注释行**，避免把头部 Config 指令误当词条；
2. 解析词条前先扫描文件头部（到首个非 `#` 行止），收集 `#@KEY:Value`（兼容 `=`）指令，写入 Config 块（见 §5.6 / §5.7）；未显式给 `#@NAME:` 时以源文件名兜底；
3. 构建 `code -> [words]`（保留文件内出现顺序）与 `word -> 最短 code`（对齐 `DictionaryIndex::Build`：仅当出现严格更短的 code 时替换）；
4. 收集全部编码串与词条串，去重后写入 StringPool（连续 UTF-16LE，无前缀/无终止符），记录各自 offset；
5. 编码按序数升序，生成 `CodeEntry[]`（记录 `firstWordRef` 与 `wordCount`）与对应 `WordRef[]`；
6. 词条按序数升序，生成 `ReverseEntry[]`；
7. 读取源 txt 的 size 与 `ftLastWriteTime`，填入 Header 的 `sourceSize` / `sourceMtimeLow/High`（仅作构建溯源元数据，运行时并不比对）；
8. 写出 `Header + Config块 + CodeEntry[] + WordRef[] + ReverseEntry[] + StringPool`；其中 `codeEntryOffset = kHeaderSize + |Config块|`（Config 块按 `kConfigAlign=4` 对齐），其余段偏移顺延。

将构建步骤接入 `scripts/deploy_test.bat`：注册 DLL 前先运行 `build_bindict.exe` 生成 `.bin`，保证部署即最新。

---

## 10. 收益与代价

| 维度           | 现状（txt + 内存索引）         | 二进制词库                        |
|----------------|--------------------------------|-----------------------------------|
| 启动（每进程） | 扫描 99k 行 + 建哈希/排序       | 一次 mmap + 头部校验（≈ 0 解析）  |
| 运行时内存     | ≈ 文本 2 倍（mmap + 堆拷贝）    | ≈ 文本大小（仅 mmap + 少量指针）  |
| 词条拷贝       | 每条都 `new wstring`            | 零拷贝，指针引用池                |
| 精确查找       | `unordered_map` O(1)           | 有序数组二分 O(log n)（n≈数万）   |
| 前缀/通配      | `lower_bound` O(log n)         | 同左，算法不变                    |
| 兼容性         | —                              | `.bin` 缺失/损坏时运行时调用 build_bindict 由 `.txt` 重建并加载（无自动过期检测；转换器缺失则记日志）|

代价：增加一个离线构建步骤；`.bin` 需随 txt 变更而显式重建（无自动过期检测）。对 98k 规模，二分查找的常数极小，性能与原 `unordered_map` 无感知差异。

---

## 11. 后续可扩展（非 v1 必需）

- **哈希索引段**：在 Header 增加 `hashBucketOffset`，为精确查找提供 O(1) 直查（v1 用二分已足够）。
- **双数组 Trie / DAWG**：把 `CodeEntry` 有序数组替换为 trie，进一步压缩并加速前缀/通配，但实现复杂。
- **多词库合并**：Header 增加段计数，支持把五笔 + 拼音 + 用户词库拼成单个 `.bin`。
- **增量用户词库**：运行时用户词单独存小 `.bin`/文本，热加载时与主词库合并视图。

---

## 12. 验收检查清单

- [ ] `build_bindict.exe` 能由 `wubi98.txt` 生成 `wubi98.bin`，且 `magic/version/flags` 正确；
- [ ] 加载器对 `.bin` 校验 `magic/version/flags/stride/边界`（经 `ValidateHeader`），不比对 sourceSize/sourceMtime；
- [ ] 四种查询结果与现有 `CDictionaryIndex` 逐条一致（可用脚本对拍）；
- [ ] 删除/损坏 `.bin` 时，运行时自动调用随包发布的 `build_bindict` 由 `.txt` 重建并加载；转换器缺失/失败则记日志、该词库本会话不可用；更新 `txt` 后（删除旧 `.bin`）会自动重建；
- [ ] 内存与启动时间对比：每进程内存下降约一半，首激活无建索引延迟。
