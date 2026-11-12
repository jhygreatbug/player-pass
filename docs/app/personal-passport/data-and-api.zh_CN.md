<p align="right">
  <strong>简体中文</strong> · <a href="data-and-api.md">English</a>
</p>

# Player Pass 数据与 API

本文定义设备端存储格式与管理热点提供的全部接口。Web 管理后台只是这些 API 的客户端，不需要其他服务。

## LittleFS 目录

```text
/lfs/
  data/
    profile.json        个人资料与通用联系方式列表
    games.json          游戏列表，按保存顺序
  img/
    avatar.bin          88 × 88
    games/<game-id>.bin 216 × 101
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
| 游戏封面 | 216 × 101 | 43,652 |

上传接口要求与该槽位完全一致的字节数，校验文件头与尺寸，把内容流式写入 `<name>.tmp`，计算 CRC，成功后才替换旧文件。上传失败会保留旧图片。

### 原子替换与恢复

`pp_storage_write_atomic()` 写入 `<final>.tmp`，fsync，把当前 `<final>` 改名为 `<final>.old`，把临时文件改名为 `<final>`，最后删除旧副本。启动时清理残留 `.tmp`；若只剩 `.old` 且最终文件缺失则恢复，若最终文件存在则删除 `.old`。这样掉电不会留下半写文件，也不会保留用户可见的历史版本。

## JSON 结构

### profile.json

```json
{
  "version": 2,
  "name": "Player One",
  "tagline": "Plays a little every day.",
  "platforms": {
    "steam": { "id": "76561198000000000" },
    "switch": { "id": "SW-0000-0000-0000" },
    "playstation": { "id": "player_one" }
  },
  "contacts": [
    { "platform": "微博", "account": "@player_one", "link": "https://weibo.com/u/0" },
    { "platform": "微信", "account": "player_one", "link": "" }
  ]
}
```

联系方式是通用列表：每条由任意平台名称、该平台下的账号和可选链接组成。设备用链接生成二维码；没有填写链接的条目不显示二维码。长度按码点计算：姓名 24，一句话介绍 60（允许换行），三个平台 ID 各 40，最多 8 条联系方式，每条平台 16、账号 40。链接必须以 `http://` 或 `https://` 开头，不含空格/控制字符，且不超过 480 字节。

版本 1 文件保存固定的 `contact.weibo` / `contact.wechat` 对象；加载时会转换成通用联系方式，下一次保存会写成版本 2。旧固件上传的二维码图片不再读取或展示。

`platforms.steam.id` 是展示字段。管理后台可选的“最近游玩 / 游玩时长”列表接受数字好友码（1–10 位）或 17 位 SteamID64；好友码由游戏资料服务自行换算。

### games.json

```json
{
  "version": 1,
  "games": [
    {
      "id": "g-a1b2c3",
      "title": "A Game",
      "note": "Short note",
      "link": "https://example.com/game"
    }
  ]
}
```

`id` 为 4–24 个小写字母、数字或 `-`。名称必填（40 码点），介绍允许换行（120 码点），链接可选（480 字节）。数组保持保存顺序；旧文件里的 `group` 字段会被忽略。API 以 200 条作为输入/资源保护上限；产品预期约 10 款。

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
| GET | `/api/images/<slot>` | 否 | 流式读取 PPIM 文件 |
| PUT | `/api/images/<slot>` | token | 流式上传 PPIM 文件 |
| DELETE | `/api/images/<slot>` | token | 删除图片 |
| POST | `/api/validate-text` | 否 | 检查最多 8 段文字的字体覆盖 |

槽位：`avatar` 和 `games/<game-id>`。联系方式二维码由设备根据链接生成，不是可上传的图片。

### 管理台保存模型

管理页在打开时加载一次各份数据，页面存续期间以浏览器中的状态为准。Wi-Fi 断开后重新连接只会重新取得编辑租约，不会重载已经加载过的文档，因此未保存的编辑不会丢失。点击一次“保存更改”会先校验有改动的文字，再依次写入 `profile.json`、`games.json`，上传待传图片并执行待删除的图片操作；没有改动的分区不会写入。各部分相互独立：失败的部分保留本地状态，由下一次保存补写。接口本身仍然按文档写入，没有合并写入接口。

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

服务端在每次写入个人资料或游戏时也会执行同一检查，并在第一个缺字处拒绝请求。设备端 sanitizer 会把不支持的字符替换为 `?` 并记录 `U+XXXX`。

## 图片上传流程

1. 浏览器校验 JPG/PNG（拒绝 WebP/GIF），处理方向并裁剪：头像方形、封面 460:215（Steam 横幅图）。
2. 浏览器绘制到 88 × 88 或 216 × 101，转换成 RGB565 小端（有序抖动），计算 CRC32，以 `application/octet-stream` 和精确长度发送。
3. 服务端校验槽位、token、类型、长度、剩余空间，然后读取文件头，把像素流式写入临时文件并同步计算 CRC。
4. 成功后替换文件、刷新图片标记并返回新的存储百分比；任何失败都会删除临时文件，旧图片继续有效。

旧文件仍存在时，剩余空间必须能容纳新文件外加 4 KiB 余量；该检查在传输开始前完成。

## 手动备份格式

备份页完全在浏览器端实现，没有服务端备份接口。它通过普通 GET 接口读取个人资料和游戏，通过 `GET /api/images/<slot>` 读取所有引用的图片，然后下载一个 JSON 文件：

```json
{
  "format": "playerpass-backup",
  "version": 1,
  "exported_at": "2026-11-11T12:00:00.000Z",
  "profile": {
    "version": 2,
    "name": "Player One",
    "contacts": [
      { "platform": "微博", "account": "@player_one", "link": "https://weibo.com/u/0" }
    ]
  },
  "games": { "version": 1, "games": [] },
  "images": {
    "avatar": "<base64 PPIM bytes>",
    "games/g-a1b2c3": "<base64 PPIM bytes>"
  }
}
```

- 文件是普通 JSON，不是 ZIP，不包含热点密码。
- 导出只读，不需要编辑租约。
- 导入需要编辑 token。它校验格式与槽位，通过 PUT 写入个人资料和游戏，然后恢复固定资料图片与游戏封面。备份中缺失的槽位会被删除，使设备状态与备份一致。
- 旧固件导出的备份可能仍带有 `nfc` 对象；导入时会忽略它。
- 浏览器最多接受 16 MB 的文件。导入在第一个错误处停止，不自动回滚；建议导入前先导出当前状态。
- 槽位名与图片 API 完全一致。旧备份中的 `qr_steam`、`qr_switch`、`qr_psn`、`qr_wechat`、`qr_wechat_pay` 槽位会被接受，但导入时忽略。其他尺寸的游戏封面也会跳过，因为设备只接受 216 × 101 封面；备份的其余内容仍会导入。

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

热点使用 WPA2-PSK，密码是编译进固件的固定产品密码。后台按设计没有登录密码；能加入热点的人都能读取数据，租约只防止同时编辑。热点不桥接互联网，设备本身不发起任何外部请求。后台可选的 Steam 游戏搜索与玩家游玩列表由浏览器直接请求游戏资料服务，只发送搜索关键词或个人资料里的 Steam 好友码，不需要账号。设备不会通过 HTTP 暴露热点密码，密码只显示在设备屏幕上。需要更换密码时重新编译固件；NVS 只保存 Wi-Fi 校准数据。

## 手机网络行为

热点没有上游互联网。iOS 通常在连接无互联网热点时保留移动数据；部分 Android 版本会把全部流量路由到热点，直到断开热点才能上网。后台编辑功能完全离线可用；Steam 搜索、候选列表与自动下载封面还需要手机本身能访问互联网。跳转外部链接可能需要先断开热点。在用户的实际手机上验证前，该行为标记为未验证。
