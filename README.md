<p align="right">
  <strong>简体中文</strong>
</p>

# CyberYAO

[![CI](https://github.com/XadillaX/CyberYAO/actions/workflows/ci.yml/badge.svg)](https://github.com/XadillaX/CyberYAO/actions/workflows/ci.yml)
[![Release](https://github.com/XadillaX/CyberYAO/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/XadillaX/CyberYAO/actions/workflows/build-firmware.yml)

CyberYAO 是为 FoloToy AI Passport 开发的六爻应用。三枚铜钱连续投掷六次，
从初爻向上生成本卦、动爻与之卦；完整收录六十四卦卦辞、三百八十四条爻辞，以及
乾卦用九和坤卦用六。起卦完成后，还可用手机扫描设备上的二维码，补充所问并生成
结构化 Prompt，交给 TraeWork 解卦并产出 HTML 报告。

<p align="center">
  <img src="docs/assets/screenshots/cyberyao-ready.png" width="220" alt="起卦界面">
  <img src="docs/assets/screenshots/cyberyao-cast.png" width="220" alt="铜钱翻滚">
  <img src="docs/assets/screenshots/cyberyao-reading.png" width="220" alt="完整解读">
</p>

## 特性

- 使用无线射频噪声增强的 `esp_fill_random()` 产生每次投掷结果，完全离线运行。
- 首次启动会开启带设备唯一后缀的 `CyberYAO-XXXXXX` 配网热点；手机打开
  `http://cyberyao`，选择 WiFi 并输入密码即可联网，成功后自动通过 NTP 校时。
- WiFi 无法重连或持续无互联网时，设备会重新打开配网页；长按下键也可手动进入。
- 三钱法严格采用「字面计二、背面计三」，六次结果从初爻向上排列。
- 伪三维龟壳与铜钱动画，轨迹、落点、旋转角度均由硬件随机数驱动。
- 支持本卦、动爻、之卦、完整卦辞与六爻爻辞。
- 按朱熹《易学启蒙》的动爻数量规则标记主读内容。
- 每个卦使用一个可滚动阅读页，不再把每一爻拆成独立页面。
- 在详细卦象页双击确认键可显示局域网二维码。手机与设备连接同一 WiFi 后扫码，
  可补充求测人、问卦内容、解读侧重与深度，一键复制结构化 Prompt 到 TraeWork；
  高级模式可要求安装六爻 Skill，并最终生成适合手机阅读的 HTML 解卦报告。
- 持续播放低声古朴环境音；起卦时自动切换为铜钱碰撞声。
- 30 秒无操作后进入待机，再待机 30 秒自动熄屏；白昼显示日晷，夜间显示刻漏，并展示时间、日期、电量、农历、干支、节气与宜忌。
- 固件与 macOS 模拟器共用同一套 C、LVGL、字体、图片和音频资源。

## 操作

| 按键 | 行为 |
| --- | --- |
| 确认短按 | 从亮屏待机进入摇卦页；在摇卦页投掷下一爻；完成六爻后进入详细解读 |
| 上短按 | 在摇卦页返回待机；在详细解读中向上滚动 |
| 上／下短按（待机页） | 只恢复亮度并留在待机页 |
| 下短按（解读页） | 向下滚动 |
| 确认短按（解读页） | 返回结果总览 |
| 确认双击（解读页） | 显示手机解卦二维码 |
| 确认长按 | 清空当前结果并回到第一爻，不自动投掷 |
| 下长按 | 打开 WiFi 配网页 |
| 上长按 | 重启并进入 Recovery |

屏幕完全熄灭时，上、下、确定任一功能键都只唤醒待机页并吞掉本次完整手势；独立
电源键不接入 MCU，短按不能唤醒背光。待机页亮起后再次短按确认，才进入摇卦页。

设备没有可用 WiFi 凭据时会自动开启配网热点；也可随时长按下键手动开启。按屏幕
提示连接 `CyberYAO-XXXXXX`，优先打开 `http://cyberyao`，无法打开时使用
`http://66.66.66.66`。页面会扫描附近网络，已成功使用的密码可在同一浏览器中作为
“旧契”再次选择或修改；设备只在完成认证并取得地址后保存凭据。

设备联网后自动校准时间。完成六爻后，在详细卦象页双击确认键即可显示手机解卦
二维码；手机必须与设备连接同一 WiFi，页面地址使用设备当前局域网 IP。

## 随机规则

每次投掷取得三个独立随机位，文字面计二，背面计三。

| 总数 | 爻 | 变化 |
| --- | --- | --- |
| 六 | 老阴 | 阴变阳 |
| 七 | 少阳 | 不变 |
| 八 | 少阴 | 不变 |
| 九 | 老阳 | 阳变阴 |

随机源失败时，本次投掷直接进入错误页，不使用伪随机或联网服务回退。

## 项目结构

```text
components/bsp/          AI Passport 板级驱动
components/yaogui_view/  六爻逻辑、LVGL 界面和资源声明
main/                    固件入口与硬件任务
simulator/               macOS LVGL + SDL2 模拟器
assets/                  实际使用的图片、音频与字体源文件
generated/               本机生成且不提交的 LVGL C 资源
tests/                   主机逻辑与固件验证
tools/                   资源生成和统一验证脚本
docs/                    开发、硬件与发布文档
```

## 构建

固件使用 ESP-IDF `v5.5.3`：

```bash
npm ci --ignore-scripts
python3 -m venv .venv
.venv/bin/pip install -r requirements-assets.txt
.venv/bin/python tools/generate_assets.py
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

macOS 模拟器需要 SDL2：

```bash
brew install sdl2 pkg-config
./simulator/build.sh
./simulator/run.sh
```

模拟器按键为方向键、`Enter`、`L`（长按确认）、`Esc`（复位）和 `Q`（退出）。

## 验证

```bash
npm ci --ignore-scripts
npm run lint:c
# 仅在需要修复格式时运行：
npm run format:c
./tools/validate.sh --generated
./tools/validate.sh --static
./tools/validate.sh --firmware
./simulator/run.sh --smoke-test
```

`lint:c` 使用锁定的 `clang-format` 1.6.0 与 `cpplint.js` 1.0.0，
检查仓库跟踪的自有手写 C/H 文件；不检查 `managed_components/`、
`generated/`、`build/` 和生成文件 `yaogui_text_data.c`。`--static`
与 GitHub CI 会执行同一检查。

主机测试覆盖三钱映射、六爻状态流转、六十四卦映射、朱熹主读规则、动画确定性和
完整阅读页。射频熵源与音频硬件仍需在 AI Passport 真机上验收。

## 上游与许可

项目基于 [FoloToy/ai-passport](https://github.com/folotoy/ai-passport) 开发，
保留原仓库的 BSP、构建链路和硬件文档。代码按仓库中的 [LICENSE](LICENSE) 发布。
