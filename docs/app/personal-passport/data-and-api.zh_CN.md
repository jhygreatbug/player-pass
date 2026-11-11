<p align="right">
  <strong>简体中文</strong> · <a href="data-and-api.md">English</a>
</p>

# Player Pass 数据与 API

本文定义设备端存储格式与管理热点提供的全部接口。Web 管理后台只是这些 API 的客户端，不需要其他服务。

## LittleFS 目录

```text
/lfs/
  data/
    profile.json        个人资料与联系方式文字
    games.json          游戏列表，按“喜欢 → 正在玩”归一化
    nfc.json            NFC 标题、类型与内容
  img/
    avatar.bin          88 × 88
    qr_steam.bin        180 × 180
    qr_switch.bin       180 × 180
    qr_psn.bin          180 × 180
    qr_wechat.bin       180 × 180
    qr_wechat_pay.bin   180 × 180
    games/<game-id>.bin 72 × 96
```

图片是否存在由文件系统决定。JSON 不保存可能失效的路径或布尔标记；Web API 在响应中附加 `images` / `cover` 标记只是方便前端使用。

## 图片容器（PPIM）

所有图片由 20 字节文件头 + 原始 RGB565 小端像素组成。设备端不解码 JPEG/PNG；浏览器直接生成 UI 需要的精确尺寸。

| 偏移 | 大小 | 字段 |
| ---: | ---: | --- |
| 0 | 4 | magic `50 50 49 4D`（`PPIM`） |
| 4 | 1 | 版本 `1` |
| 5 | 1 | 格式 `1` = RGB565 小端 |
| 6 | 1 | flags `0` |
| 7 | 1 | reserved `0` |
| 8 | 2 | 宽度，小端 u16 |
| 10 | 2 | 高度，小端 u16 |
| 12 | 4 | 像素字节数，小端 u32（`width * height * 2`） |
| 16 | 4 | 像素数据的 CRC32（IEEE） |
| 20 | N | 像素字节 |

| 槽位类型 | 尺寸 | 完整文件字节数 |
| --- | --- | ---: |
| 头像 | 88 × 88 | 15,508 |
| 游戏封面 | 72 × 96 | 13,844 |
| 二维码 | 180 × 180 | 64,820 |

上传接口要求与该槽位完全一致的字节数，校验文件头与尺寸，把内容流式写入 `<name>.tmp`，计算 CRC，成功后才替换旧文件。上传失败会保留旧图片。

### 原子替换与恢复

`pp_storage_write_atomic()` 写入 `<final>.tmp`，fsync，把当前 `<final>` 改名为 `<final>.old`，把临时文件改名为 `<final>`，最后删除旧副本。启动时清理残留 `.tmp`；若只剩 `.old` 且最终文件缺失则恢复，若最终文件存在则删除 `.old`。这样掉电不会留下半写文件，也不会保留用户可见的历史版本。

## JSON 结构

### profile.json

```json
{
  "version": 1,
  "name": "Player One",
  "tagline": "Plays a little every day.",
  "platforms": {
    "steam": { "id": "76561198000000000" },
    "switch": { "id": "SW-0000-0000-0000" },
    "playstation": { "id": "player_one" }
  },
  "contact": {
    "weibo": { "handle": "@player_one", "url": "https://weibo.com/u/0" },
    "wechat": { "name": "Player", "id": "player_one" }
  }
}
```

长度按码点计算：姓名 24，一句话介绍 60（允许换行），三个平台 ID 各 40，微博账号 40，微信昵称 24，微信号 40。URL 必须以 `http://` 或 `https://` 开头，不含空格/控制字符，且不超过 480 字节。

### games.json

```json
{
  "version": 1,
  "games": [
    {
      "id": "g-a1b2c3",
      "title": "A Game",
      "group": "favorite",
      "note": "Short note",
      "link": "https://www.xiaoheihe.cn/app/..."
    }
  ]
}
```

`id` 为 4–24 个小写字母、数字或 `-`。名称必填（40 码点），介绍允许换行（120 码点），链接可选（480 字节）。`group` 为 `favorite` 或 `playing`。加载与保存时数组会归一化：先“喜欢”，后“正在玩”，组内保持相对顺序。API 以 200 条作为输入/资源保护上限；产品预期约 10 款。

### nfc.json

```json
{
  "version": 1,
  "enabled": true,
  "label": "Homepage",
  "type": "url",
  "content": "https://example.com/"
}
```

`label` 16 码点，`content` 160 码点。`type` 为 `url` 或 `text`。启用时内容必填；URL 类型必须是合法 HTTP(S) 地址。

## HTTP API

基础地址：`http://192.168.4.1`。除图片与静态后台外，响应均为 `application/json`。修改类接口必须带 claim 返回的请求头 `X-PP-Token: <token>`。JSON 请求体必须带 `Content-Length`；缺失或 chunked 会被拒绝。

| 方法 | 路径 | 鉴权 | 用途 |
| --- | --- | --- | --- |
| GET | `/` | 否 | 管理后台（gzip HTML） |
| GET | `/style.css`、`/app.js` | 否 | 后台资源（gzip） |
| GET | `/api/status?client=<id>` | 可选 token | 设备、Wi-Fi、会话、存储状态 |
| POST | `/api/session/claim` | 否 | 取得唯一编辑租约 |
| POST | `/api/session/heartbeat` | token | 刷新编辑租约 |
| POST | `/api/session/release` | token | 立即释放租约 |
| GET | `/api/profile` | 否 | 个人资料与图片标记 |
| PUT | `/api/profile` | token | 校验、写入并返回个人资料 |
| GET | `/api/games` | 否 | 游戏列表与封面标记 |
| PUT | `/api/games` | token | 替换列表并清理无主封面 |
| GET | `/api/nfc` | 否 | NFC 记录 |
| PUT | `/api/nfc` | token | 校验并写入 NFC 记录 |
| GET | `/api/images/<slot>` | 否 | 流式读取 PPIM 文件 |
| PUT | `/api/images/<slot>` | token | 流式上传 PPIM 文件 |
| DELETE | `/api/images/<slot>` | token | 删除图片 |
| POST | `/api/validate-text` | 否 | 检查最多 8 段文字的字体覆盖 |

槽位：`avatar`、`qr_steam`、`qr_switch`、`qr_psn`、`qr_wechat`、`qr_wechat_pay`、`games/<game-id>`。

### Claim

请求：

```json
{ "client_id": "uuid", "client_name": "iPhone browser" }
```

成功（200）：

```json
{
  "ok": true,
  "token": "0123456789abcdef",
  "lease_seconds": 30,
  "grace_seconds": 120,
  "idle_seconds": 0
}
```

被占用（409）：

```json
{
  "error": "occupied",
  "message": "The device is being edited by another browser",
  "client_name": "iPhone browser",
  "grace_remaining_s": 0,
  "reserved_for_me": false
}
```

失去租约的浏览器在宽限期内可用同一个 `client_id` 重新 claim。仍持有租约的同一浏览器刷新页面时会获得新 token，旧 token 立即失效。

### 租约规则

| 参数 | 数值 |
| --- | ---: |
| 无心跳的租约超时 | 30 秒 |
| 租约过期后的重连宽限 | 120 秒 |
| 管理模式空闲退出 | 关闭（0）；长按 OK 退出 |
| 长操作暂停 | 上传期间 |

第一个 claim 的浏览器获胜，只有它持有 token。第二个浏览器可以读取状态和数据，并看到带有占用者名称的 `occupied`。错过心跳后，租约给同一客户端保留两分钟，其他人仍被拒绝。上传期间租约会自动延长，避免大文件传输把编辑器踢掉。本产品的管理模式以 0 空闲超时启动：不会自动退出，设备端长按 OK 可立即退出。

### 状态

```json
{
  "firmware": "0.1.0",
  "uptime_ms": 123456,
  "session": {
    "running": true,
    "occupied": true,
    "editor": true,
    "reserved_for_me": false,
    "client_name": "iPhone browser",
    "grace_remaining_s": 0,
    "idle_remaining_s": 0
  },
  "storage": {
    "used": 1234567,
    "total": 5177344,
    "free": 3942777,
    "percent": 23,
    "level": 0,
    "warn_percent": 80,
    "critical_percent": 90
  },
  "wifi": { "ssid": "PlayerPass-A1B2", "ip": "192.168.4.1", "clients": 1 }
}
```

`storage.level`：0 正常，1 到 80% 提醒，2 到 90% 警告。

### 文字校验

请求：

```json
{ "texts": ["Name", "A sentence"] }
```

响应：

```json
{
  "ok": false,
  "unsupported": [
    { "index": 1, "codepoint": "U+9F98" }
  ]
}
```

服务端在每次写入个人资料、游戏或 NFC 时也会执行同一检查，并在第一个缺字处拒绝请求。设备端 sanitizer 会把不支持的字符替换为 `?` 并记录 `U+XXXX`。

## 图片上传流程

1. 浏览器校验 JPG/PNG（拒绝 WebP/GIF），处理方向并裁剪：头像方形、封面 3:4 竖版、二维码按比例缩放并补白。
2. 浏览器绘制到 88 × 88、72 × 96 或 180 × 180，转换成 RGB565 小端（照片用有序抖动，二维码用亮度阈值），计算 CRC32，以 `application/octet-stream` 和精确长度发送。
3. 服务端校验槽位、token、类型、长度、剩余空间，然后读取文件头，把像素流式写入临时文件并同步计算 CRC。
4. 成功后替换文件、刷新图片标记并返回新的存储百分比；任何失败都会删除临时文件，旧图片继续有效。

旧文件仍存在时，剩余空间必须能容纳新文件外加 4 KiB 余量；该检查在传输开始前完成。

## 手动备份格式

备份页完全在浏览器端实现，没有服务端备份接口。它通过普通 GET 接口读取个人资料、游戏和 NFC，通过 `GET /api/images/<slot>` 读取所有引用的图片，然后下载一个 JSON 文件：

```json
{
  "format": "playerpass-backup",
  "version": 1,
  "exported_at": "2026-11-11T12:00:00.000Z",
  "profile": { "version": 1, "name": "Player One" },
  "games": { "version": 1, "games": [] },
  "nfc": { "version": 1, "enabled": false },
  "images": {
    "avatar": "<base64 PPIM bytes>",
    "games/g-a1b2c3": "<base64 PPIM bytes>"
  }
}
```

- 文件是普通 JSON，不是 ZIP，不包含热点密码。
- 导出只读，不需要编辑租约。
- 导入需要编辑 token。它校验格式与槽位，通过 PUT 写入个人资料、游戏和 NFC，然后恢复固定资料图片与游戏封面。备份中缺失的槽位会被删除，使设备状态与备份一致。
- 浏览器最多接受 16 MB 的文件。导入在第一个错误处停止，不自动回滚；建议导入前先导出当前状态。
- 槽位名与图片 API 完全一致。

## 错误响应

| 状态码 | 含义 |
| --- | --- |
| 400 | JSON、字段、尺寸、CRC 或槽位无效 |
| 401 | 缺少或过期的编辑 token |
| 404 | 接口不存在或图片缺失 |
| 409 | 编辑租约被占用 |
| 411 | 缺少 `Content-Length` |
| 413 | JSON 请求体超过该接口上限 |
| 415 | 上传不是 `application/octet-stream` |
| 500 | 存储或内存分配失败 |
| 507 | 上传空间不足 |

错误体为 `{ "error": "slug", "message": "人类可读文本" }`。

## 安全模型

热点使用 WPA2-PSK，密码是编译进固件的固定产品密码。后台按设计没有登录密码；能加入热点的人都能读取数据，租约只防止同时编辑。热点不桥接互联网，后台不发起外部请求。设备不会通过 HTTP 暴露热点密码，密码只显示在设备屏幕上。需要更换密码时重新编译固件；NVS 只保存 Wi-Fi 校准数据。

## 手机网络行为

热点没有上游互联网。iOS 通常在连接无互联网热点时保留移动数据；部分 Android 版本会把全部流量路由到热点，直到断开热点才能上网。后台完全离线可用，但从小黑盒链接跳转可能需要先断开热点。在用户的实际手机上验证前，该行为标记为未验证。
