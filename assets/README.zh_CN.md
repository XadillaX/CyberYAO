<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

`fonts/` 仅保留界面实际使用的四套源字体：
`fusion-pixel-12px-proportional-zh_hans.otf`、`xique-guzidian.ttf` 和
`xique-zaishanlin.ttf`，以及只补齐七个古文字形的
`source-han-sans-sc-normal.otf`。

Fusion Pixel 使用 SIL Open Font License 1.1，许可证与上游组成字体许可保存在
`fonts/licenses/fusion-pixel/`；思源黑体许可证保存在
`fonts/licenses/source-han-sans-sc/`。两套喜鹊字体为项目提供的原始设计素材，二次
分发前仍需自行确认对应授权范围。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

`images/` 仅存放可复用源图；生成后的显示资源放在顶层且被忽略的 `generated/`。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（audio）

可复用的音乐与音效源码放在 `audio/`。当前包含持续循环的古朴卜卦环境音，以及
龟壳摇动、铜钱翻滚和落地音效。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 当前 BSP 音频路径使用 32 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 当前资源

| 源文件 | 用途 | 固件资源 |
| --- | --- | --- |
| `images/bagua_table_user.png` | 用户选定的八卦桌原图 | `yaogui_table_image.c` |
| `images/coin_front.png`、`coin_back.png` | 铜钱正反面 | `yaogui_coin_images.c` |
| `images/yaogui_shell_faces.jpg` | 龟壳源图 | `yaogui_shell_images.c` |
| `audio/coin_ritual.wav` | 摇动、翻滚与落地声 | `yaogui_coin_sound.c` |
| `audio/ambient_divination.wav` | 六秒古朴环境循环 | `yaogui_ambient_sound.c` |
| `fonts/fusion-pixel-12px-proportional-zh_hans.otf` | 正文字体 | `yaogui_font_14.c` |
| `fonts/source-han-sans-sc-normal.otf` | 正文七个古文字形回退 | `yaogui_font_14.c` |
| `fonts/xique-guzidian.ttf` | 古典标题 | `yaogui_classic_14.c` |
| `fonts/xique-zaishanlin.ttf` | 卦名结果 | `yaogui_mifu_18.c` |

先安装 `package-lock.json` 与 `requirements-assets.txt` 锁定的版本，再执行
`python3 tools/generate_assets.py`。生成的 C 数组写入被忽略的 `generated/`，直接编入
固件和模拟器；执行 `./tools/validate.sh --generated` 可验证生成过程可复现。
