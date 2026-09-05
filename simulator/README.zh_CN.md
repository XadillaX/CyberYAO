# 摇龟 Mac LVGL + SDL2 模拟器

[English](README.md)

模拟器直接编译仓库内 `managed_components/lvgl__lvgl`，并与固件共用
`components/yaogui_view` 中的像素 UI、中文字体配置、龟壳与铜钱图像、音频素材、
`yaogui_logic`、界面创建与渲染代码，不包含 ESP-IDF 射频实现。

## 构建和运行

```sh
brew install sdl2 pkg-config
./simulator/build.sh
./simulator/run.sh
```

默认以 2 倍显示 240x320 逻辑画布。可用
`./simulator/run.sh --scale 1.5` 指定 1～4 倍初始缩放。

## 键盘

- `↑`／`↓`：在详细解读中滚动，到达边界后切换本卦与之卦。
- `Enter` / `Space`：确认并起卦。
- `L`：模拟长按确认并开始新卦。
- `+` / `-`：缩放窗口。
- `Esc`：复位。
- `Q`：退出。

Mock 会延迟 0.9～1.8 秒返回随机结果。模拟器持续播放与真机相同的背景音，起卦时
清空背景队列并播放铜钱声，铜钱声结束后恢复循环。

## 边界

模拟器验证真实 LVGL 渲染、动画和键盘交互；不模拟 ESP32-C3 Wi-Fi 射频、按键 ADC
或真实屏幕色彩。真机固件使用射频增强的 `esp_fill_random`，生命周期失败时直接
报错，不使用回退源。
