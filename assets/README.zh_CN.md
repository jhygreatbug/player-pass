<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 设备字体子集（Player Pass）

个人名片固件为其 240 × 320 中文界面编译固定子集。

| 项目 | 内容 |
| --- | --- |
| 字族 | Fusion Pixel 12px proportional，简体中文字形集 |
| 发布版本 | 2026.10.25 |
| 来源 | https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.10.25 |
| 源文件 | `fusion-pixel-12px-proportional-zh_hans.ttf`（未提交） |
| SHA-256 | `58a0ac8784bfb0ed70b788a9cf815d00fcad9daa9e0517b8d1c244b4f912e3d6` |
| 许可 | SIL Open Font License 1.1 |
| 转换工具 | `lv_font_conv` 1.5.3（固定版本） |
| 字符清单 | 7,393 个码点：可打印 ASCII、GB2312 符号区与源字体实际包含的一级/二级汉字，以及 `main/` 中出现的全部非 ASCII 字符 |
| 字号 | 12 px 与 24 px，1 bpp，不压缩，行高 16 / 28 px |
| 生成文件 | `pp_font_12.c`、`pp_font_24.c`、`pp_font_charset.c`、`pp_font_charset.txt` |
| 校验 | `tools/check_fonts.py`，由 `tools/validate.sh --static` 执行 |

重新生成：

```bash
python3 tools/gen_fonts.py \
  --font /path/to/fusion-pixel-12px-proportional-zh_hans.ttf \
  --tool /path/to/node_modules/.bin/lv_font_conv
```

源 TTF 不提交。生成需要 `fonttools`，校验不需要。生成脚本排除了未使用的全角货币符号区 U+FFE0..U+FFE5。密集的 CJK 区段会使用 LVGL 9.5 支持的 FORMAT0_FULL cmap（LVGL 自带 CJK 字体也使用该布局）；`tools/check_fonts.py` 同时解析 FULL 与 TINY/SPARSE_TINY。生成后的行高会改写为适配布局的 16 / 28 px。实测 Flash 占用：12 px 字体 187,267 字节、24 px 字体 505,351 字节、覆盖表 28,700 字节，合计 721,318 字节。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
