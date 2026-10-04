<p align="right">
  <strong>简体中文</strong> · <a href="multi-wifi-provisioning.md">English</a>
</p>

# 多 WiFi 凭据与配网体验升级 · 设计文档

> 状态：**待评审（实现前请先过目）**
> 范围：`main/yaogui_time_sync.c`、`main/portal.html`、`main/yaogui_network_policy.{c,h}`、`tests/test_yaogui_network_policy.c`
> 不改动：分区表（`partitions.csv`）、NVS 偏移（`0x9000`，24KB）、按键映射

## 1. 背景与目标

当前设备只能保存**一组** WiFi 凭据，断线后只会反复重连同一个 AP，重试到上限才开热点。换个场所（家 → 公司）就得重新配网，旧凭据被覆盖。

本次升级目标：

1. **多组凭据**：NVS 从单组 `ssid`/`password` 升级为凭据数组，最多保存 N 组（建议 N=5）。
2. **智能择优重连**：断线后扫描周边，在「已保存 ∩ 当前可见」中按信号最强优先逐个尝试，全部失败才开热点。
3. **配网页信息更丰富**：弹出的配网页里，除说明外，直接列出「已保存且当前有信号」的网络（可删除 / 可调优先级），以及「附近新网络」。
4. **双向实时反馈**：手机侧轮询连接进度（现有机制保留），设备侧待机页 / 状态栏实时显示「配网中 / 已连 XXX」。

## 2. 现状与硬约束（必须遵守）

### 2.1 现状关键事实（已核对源码）

- NVS 命名空间 `yaogui_net`，键 `ssid` / `password`，单组字符串（`load_credentials` L559、`save_credentials` L575）。
- `esp_wifi_set_storage(WIFI_STORAGE_RAM)` + `init.nvs_enable = 0`（L816、L826）：协议栈不落盘，凭据完全由固件自管 NVS，升级存储格式不受协议栈约束。
- 断线处理 `handle_wifi_failure()`（L669）：当前仅 `esp_wifi_connect()` 重连同一 AP；连续失败达 `YAOGUI_WIFI_FAILURE_LIMIT`（=3，见 `yaogui_network_policy.h`）才 `open_portal()`。
- 扫描接口 `GET /networks`（L388）已返回 `{ssid, rssi, open}`，**信号强度现成可用**。
- HTTP 路由 7 个（`max_uri_handlers = 7`，L484）：`/connect`、`/networks`、`/status`、`/api/reading`、`/guaxiang.html`、`/`、`/*`。新增接口需同步调大 `max_uri_handlers` 与 `handlers[]` 数组。
- Portal 前端 `portal.html` 已有 `localStorage`（键 `cyberyao.wifi.v2`）缓存手机侧填过的密码，用于回填。

### 2.2 硬约束（违反即回退）

- **配网入口只能「连不上自动开热点」**。双击确定键严禁直接弹配网窗口；仅在「完全离线且无任何连接尝试」时提示联网。本次升级不新增任何手动弹配网的入口。
- **`GET /saved` 等任何接口严禁回传明文密码**，只回 SSID（及是否加密、优先级）。
- **英文 CHANGELOG 不得含中文字符**（曾因「暗动」触发 CI `check_repo.py` 英文散文校验失败）。
- **逐字字体校验机制保留**，构建时缺 UI 所需字符必须报错中断。
- **问卦页严禁 `localStorage` 缓存用户问卦信息**（问题 / 姓名 / 选项）。注意：这里 `portal.html` 缓存的是「配网密码回填」，与问卦页约束是两件事，不冲突。

## 3. 存储层设计（NVS 单组 → 数组 blob）

### 3.1 数据结构

在 `yaogui_time_sync.c` 内定义紧凑的持久化结构（固定长度，便于 blob 读写与 CRC 校验）：

```c
#define WIFI_CRED_MAX 5
#define NVS_CRED_BLOB "creds"      // 新键，blob
#define NVS_CRED_VERSION 2

typedef struct {
  char ssid[WIFI_SSID_TEXT_SIZE];       // 33
  char password[WIFI_PASSWORD_TEXT_SIZE]; // 65
} wifi_cred_t;

typedef struct {
  uint8_t version;                 // = NVS_CRED_VERSION
  uint8_t count;                   // 0..WIFI_CRED_MAX
  wifi_cred_t items[WIFI_CRED_MAX]; // items[0] = 最高优先级
} wifi_cred_store_t;               // 约 5 * 98 + 2 ≈ 492 字节
```

- **优先级即数组顺序**：`items[0]` 最优先。`POST /reorder` 重排数组，`POST /connect` 成功后把该 SSID 提到 `items[0]`（LRU-success，最近成功的最优先）。
- 24KB NVS 分区远够（单 blob < 512 字节），**不动分区表**。

### 3.2 读写与迁移

- `cred_store_load(store)`：读 blob；读不到或 `version != 2` 时，尝试读旧键 `ssid`/`password`，若有则迁移为 `count=1` 的新结构并写回，再删除旧键。保证老设备 OTA 后不丢配网。
- `cred_store_save(store)`：`nvs_set_blob` + `nvs_commit`。
- `cred_store_add(store, ssid, pwd)`：同名 SSID 覆盖密码并提到首位；新 SSID 插入首位；超过 `WIFI_CRED_MAX` 时丢弃末位（最久未成功）。**追加不覆盖全表**。
- `cred_store_forget(store, ssid)`：删除指定 SSID。

## 4. 连接状态机设计（择优重连）

改造 `handle_wifi_failure()` 与启动流程：

1. **启动**：`cred_store_load`。若 `count==0` → `open_portal()`；否则进入「候选择优」连接流程。
2. **候选择优**（新函数 `connect_best_candidate()`）：
   - `esp_wifi_scan_start`（复用现有扫描能力）得到周边 AP + RSSI。
   - 构造候选集 = `已保存 ∩ 周边可见`，按 **RSSI 降序**排序（同信号时按存储优先级）。
   - 依次 `connect_station()` 尝试；单个失败（`WIFI_EVENT_STA_DISCONNECTED` 或 30s 超时）则试下一个。
   - **候选全部耗尽**才 `open_portal()`。这是唯一的开热点路径，满足「连不上才开热点」硬约束。
3. **成功**：`handle_got_ip()` 里把当前 SSID 提到 `items[0]` 并 `cred_store_save`。
4. `yaogui_network_policy` 复用：`consecutive_wifi_failures` 的语义从「同一 AP 失败次数」变为「一轮候选全败次数」，阈值逻辑不变，单测相应补充。

> 注意：扫描在 STA 模式下会短暂打断连接，需确保只在断线 / 启动 / 候选切换时扫描，避免在线态频繁扫描。

## 5. HTTP API 设计

| 方法 | 路径 | 作用 | 返回 |
|---|---|---|---|
| GET | `/networks` | 附近 AP（已有） | `[{ssid,rssi,open}]` |
| GET | `/saved` | **新增**，已保存列表 | `[{ssid,order}]`，**绝不含密码** |
| POST | `/connect` | 连接 + 追加保存（改为追加不覆盖） | `{accepted:true}` |
| POST | `/forget` | **新增**，删除一组凭据 | `{ok:true}` |
| POST | `/reorder` | **新增**，重排优先级 | `{ok:true}` |
| GET | `/status` | 连接进度（已有，拟扩展） | `{state, ssid?}` |

- `max_uri_handlers` 7 → 10，`handlers[]` 同步加 3 项。
- `/forget`、`/reorder` body 复用 `yaogui_form_value` 解析（`ssid=`，`/reorder` 用 `order=0,2,1` 之类的序号串）。
- `/status` 增加当前目标/已连 `ssid` 字段，供手机侧显示「正在连 XXX」。

## 6. Portal 前端设计（`portal.html`）

页面在现有「附近 WiFi」列表基础上，分两区呈现：

- **已保存（有信号）**：`/saved` ∩ `/networks` 的交集，带信号格、「删除」按钮、拖动/按钮调序。点击可直接重连（已存密码无需重输）。
- **附近网络（新增）**：周边有信号但未保存的 AP，点击进入输入密码表单。

交互：

- 删除 → `POST /forget` → 刷新两区。
- 调序 → `POST /reorder` → 刷新。
- 连接新网络 → `POST /connect`（追加）→ 轮询 `/status`。
- 保持纯静态、无框架、Gzip 存储（与现有 `portal.html` 一致，构建期 minify+gzip）。
- 继续用 `localStorage` 回填密码（配网便利，非问卦信息，不违反约束）。

## 7. 双向实时反馈

- **手机侧**：`/status` 轮询已存在，扩展 `ssid` 字段后显示「正在连接 XXX / 已连 XXX」。
- **设备侧**：`yaogui_app.c` 已通过 `wifi_status()` 读取 `connecting / local / online`。本次把「目标 SSID」透出到 view 层，待机页 / 状态栏可显示「配网中」或「已连 XXX」。状态变化才 `invalidate` 重绘，避免无谓刷新（与充电图标优化同一思路）。

## 8. 测试计划

扩展 `tests/test_yaogui_network_policy.c`（纯 host 单测，无需硬件）：

- 凭据数组：add 去重 / 覆盖提首位 / 超限淘汰末位 / forget / reorder。
- 旧格式迁移：单组 `ssid`+`password` → `count=1` 新结构。
- 候选排序：已保存 ∩ 周边，按 RSSI 降序。
- `/saved` 序列化不含密码（可加轻量断言或在 handler 层评审把关）。
- 保留现有 5 个用例全绿。

## 9. 风险与未决事项

1. **一轮候选全败的判定边界**：需明确「扫描失败」与「候选全败」是否都计入 `consecutive_wifi_failures`，避免扫描抖动误开热点。
2. **扫描耗时**：APSTA 下扫描会抖动连接，建议扫描超时与节流参数评审时定（如最短 10s 间隔）。
3. **`WIFI_CRED_MAX` 取值**：建议 5，评审确认。
4. **`/reorder` 的 body 格式**：序号串 vs SSID 串，二选一，建议序号串（更短、避免中文 SSID 编码问题）。
5. **烧录建议**：仅烧 `0x10000` app 分区保留 NVS；但本次改了 NVS 键结构，首启会触发一次迁移（旧 `ssid`/`password` → `creds` blob），属预期。

## 10. 实施顺序（评审通过后）

1. 存储层：`wifi_cred_store_t` + load/save/add/forget/reorder + 旧格式迁移。
2. 状态机：`connect_best_candidate()` 改造 `handle_wifi_failure()` 与启动流程。
3. HTTP：`/saved`、`/forget`、`/reorder`，调大 `max_uri_handlers`，扩展 `/status`。
4. 前端：`portal.html` 两区重构。
5. 设备侧实时显示目标 SSID。
6. 单测补齐 + `idf.py build` + 真机烧录验证。
