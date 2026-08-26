# CrossMux app store

Prebuilt `.eapp` images and the install catalog served to the on-device App
Manager (应用管理). This directory is the default catalog source
(`catalog.json`), fetched over Wi-Fi from
`raw.githubusercontent.com/<owner>/crossmux/<branch>/store/`.

## Installing

- **On device:** Apps → 应用管理 → 在线应用目录 → pick an app. Or drop any
  `.eapp` into the SD card's `/apps/` folder (via the File Transfer app's
  web uploader, WebDAV, or a card reader) and it appears under 已安装应用.
- Data lives in `/apps/data/<slug>/` and survives updates and firmware
  reflashes.

## What's here

Every former built-in game and tool, now an installable app, plus the DLNA
music player and the five AI apps:

| slug | 应用 | slug | 应用 |
|---|---|---|---|
| g2048 | 2048 | sanguo | 三国霸业 |
| minesweeper | 扫雷 | xiangqi | 中国象棋 |
| sudoku | 数独 | vocab | 单词卡 |
| gomoku | 五子棋 | pomodoro | 番茄钟 |
| sokoban | 推箱子 | calculator | 计算器 |
| klotski | 华容道 | woodfish | 电子木鱼 |
| avatar | 头像生成器 | buddy | 抽卡伙伴 |
| weather | 天气 | poem | 每日诗词 |
| rss | RSS 速览 | exchange | 汇率 |
| sysmon | 系统监视器 | life | 生命游戏 |
| music | 音乐播放器 | | |

### AI 应用

These call an OpenAI-compatible `/chat/completions` endpoint that **you**
configure — DeepSeek, 智谱, 月之暗面, 通义千问, OpenAI, or a llama.cpp /
Ollama server on your own LAN all work. Set it up once in 「AI 助手」 →
设置; the other four read the same shared configuration.

| slug | 应用 | 需要打字 | 做什么 |
|---|---|---|---|
| aichat | AI 助手 | 一行，或完全不用 | 配置中心 + 一触即问模板，可带上下文追问 |
| aidict | AI 词典 | 一个词 | 固定格式词卡，自动存卡，离线可复习 |
| ailesson | 每日一课 | 不用 | 每天一张卡，带上已学过的标题避免重复；缓存后离线可读 |
| aibook | 读书伴侣 | 不用 | 翻卡上的 txt/md，随时就「这一段」提问 |
| ainote | 灵感整理 | 一句短话 | 随手记，攒够了让 AI 理成提纲 / 待办 / 主线 |

**请注意**：密钥以明文保存在 SD 卡的 `/apps/data/_shared/ai_key.txt`，能读卡的人
就能读到它，请用可以随时吊销的密钥。发出去的内容（词、书里的段落、你的笔记）会
到达你配置的服务商，受其数据政策约束。传输走 HTTPS，但**不校验服务器证书**——本机
的 wolfSSL 通道没有内置根证书库，这和设备上其它所有联网请求（OPDS、微信读书、应用
目录、OTA）是同一套；能防旁听，不能防中间人劫持。所以请用可以随时吊销的密钥，并尽
量在可信网络下使用。每次调用都按服务商计费。

## Building these

Sources live in [`../sdk/dynapp/apps/`](../sdk/dynapp/apps). Rebuild one with:

```bash
cd ../sdk/dynapp
RISCV_TOOLCHAIN=~/.platformio/packages/toolchain-riscv32-esp/bin \
  ./build-eapp.sh apps/<slug>          # → out/<slug>.eapp, verified
```

Or build everything and regenerate the catalog in one go:

```bash
./sdk/dynapp/build-store.sh              # all apps → store/ + catalog.json
./sdk/dynapp/build-store.sh aichat       # just one, catalog refreshed
```

`catalog.json` is **generated — do not hand-edit it.** Each app's name,
version and note live in its own `app.meta`, and the byte count is read off
the built file, so a stale size cannot ship. `CROSSMUX_STORE_BASE` overrides
the download URL prefix.

See [../docs/engineering/dynapp.md](../docs/engineering/dynapp.md) for the ABI
and the C3 loader internals.
