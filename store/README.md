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

Every former built-in game and tool, now an installable app, plus the
DLNA music player:

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

## Building these

Sources live in [`../sdk/dynapp/apps/`](../sdk/dynapp/apps). Rebuild one with:

```bash
cd ../sdk/dynapp
RISCV_TOOLCHAIN=~/.platformio/packages/toolchain-riscv32-esp/bin \
  ./build-eapp.sh apps/<slug>          # → out/<slug>.eapp, verified
```

Copy `out/<slug>.eapp` here and update `catalog.json` (name/version/bytes/url).
See [../docs/engineering/dynapp.md](../docs/engineering/dynapp.md) for the ABI
and the C3 loader internals.
