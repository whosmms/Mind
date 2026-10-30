# 乙评审：丙 `feature/c/M5-deepseek-client`（3f9a4ea + 0137a94）——退回待修

日期：2026-10-30；评审人：乙LH（AI协助）；被评审：丙。**结论：暂不合入 main，退回分支修改。**

方向认可：接口按 `IAiProvider` 实现、错误码按 §10.3 映射、密钥只进 `Authorization` 头且不落盘、
本机桩服务器不访问外网。以下问题不是风格问题，是行为与规范问题。

合入动作说明：评审期间曾做过一次 `git merge`，发现下述问题后已 `git merge --abort` 回退，
main 上没有任何本分支的痕迹（HEAD 为 `6420473` 合入 R13 之后）。

---

## 一、阻断项（必须改，共 5 条）

### P1-1 取消被当成错误，与 `FakeAiProvider`/`AiService` 的既有契约冲突

`DeepSeekClient.cpp:63-68`（cancel 只 abort）+ `:88-95`（把 `OperationCanceledError` 映射成
`failed({code:"CANCELED"})`）。

既有契约是相反的：`FakeAiProvider.cpp:32-37` 的 cancel 是**静默**的，注释写明"canceled 终态语义由
AiService 解释"；`AiService::cancel` 自己 `setState(Canceled)` + `emit canceled`。
现在多出来的这条 `failed(CANCELED)` 会走 `AiService::onProviderFailed` → `setState(Error)` → 再 `emit failed`：
用户点"取消"，界面先显示已取消，再翻成错误提示。

时序更糟的一种：若 `abort()` 同步发 `finished`，`failed` 先到，`onProviderFailed` 清空了
`m_currentRequestId`，回到 `AiService::cancel` 里 `requestId == m_currentRequestId` 已不成立，
`canceled` 根本不发，最终只剩 Error。

修法：`cancel()` 只 abort，不发任何信号；超时仍发 `TIMEOUT`（那是真异常）。
用例：把 `cancelEmitsCanceled` 改成断言 `failed.count() == 0`；另加一条联动用例，把
`DeepSeekClient` 塞进 `AiService`，取消后断言 `state() == Canceled`。

### P1-2 析构里 abort，正在遍历的 `QHash` 会被增删；且在析构中发信号

`DeepSeekClient.cpp:23-27`：

```cpp
for (QNetworkReply* reply : m_active) { reply->abort(); }
```

`QNetworkReply::abort()` 文档明确"finished() 也会被发出"，是否同线程同步并不承诺。
`onFinished` 里有 `m_active.remove(requestId)`——一旦同步回调，就是在 range-for 遍历中删除元素，
迭代器失效，属未定义行为，不是理论风险。即使异步，析构中 emit `failed` 也会让 `AiService`
在客户端已销毁的过程中翻状态、回调 UI。

修法：先 `const auto keys = m_active.keys();`，再逐个 `take` + `abort`；加 `m_shuttingDown` 标志，
`onFinished` 见到就直接 `deleteLater` 返回，不发信号。

### P1-3 取消后同一 requestId 立刻重试会被静默吞掉

`cancel()`（`:63-68`）只 abort、不 `m_active.remove`；`start()`（`:39-42`）用 `m_active.contains` 判重。
取消后若 `finished` 尚未派发，第二次 `start` 同一 id 会被当成重复请求直接 return——
用户点"重试"没反应，也没有任何错误提示。

用例：`cancel(req)` 后立即 `start(req)`，断言桩服务器收到 2 次请求。

### P1-4 §10.7 的有界重试完全没做

规范：429/500/503 或可安全重发的连接前失败**自动重试一次**，等待 2s + 小抖动；
整个生成最多 2 次网络请求、总预算 180s；`Retry-After` 有效但超出预算则直接告知稍后重试。
Provider 负责"限流重试"是 §10.3 的职责划分。

现状：`mapError` 只置 `retryable = true` 就返回，而 `AiService` 里没有任何重试逻辑
（`src/ai/AiService.cpp` 全文无 retry 分支）。等于"标了可重试，但没有任何一层会重试"，
429 直接砸到用户脸上，规范表格 T21 那一行的预期达不到。

修法：`start` 时把 body 与 `attempt` 存在内存（不落盘），429/500/503 且 `attempt < 1` 时
`QTimer` 2s + 随机抖动重发；解析 `Retry-After`，若大于剩余预算直接
`failed(RATE_LIMITED, "请求过于频繁，请稍后重试")`；总预算用 `QDeadlineTimer` 卡。

用例：429 一次后 200 → 桩收到 2 次、最终 `completed`；连续 429 → 只发 2 次就 `failed`；
`Retry-After: 600` → 不发第二次。

### P1-5 超时 60s，规范是 90s

`DeepSeekClient.h:35` 与 `DeepSeekClient.cpp:15` 均为 60000；§10.7 写的是"单次请求 90 秒截止"，
配套 180s 总预算。改成 90000，并与 P1-4 的总预算对齐。

---

## 二、用例缺口（与上面几条一起补）

| 编号 | 覆盖点 | 现状 |
|---|---|---|
| T-1 | 超时：`setTimeoutMs(50)` + 桩 `delayMs=2000` → `TIMEOUT`、`retryable`、只发 1 次 | 完全没测（最关键的分支） |
| T-2 | 断网：指向未监听端口 → `NETWORK_ERROR` | 没测 |
| T-3 | 200 但 body 不是 JSON 对象（代理返回 HTML） | 没测。现在会落到 `mapError`：status=200 不进任何 case → `UPSTREAM_ERROR`，message 取 `reply->errorString()` = "Unknown error"，中文提示丢了 |
| T-4 | 402 → `BALANCE_LOW` | 映射表里唯一没覆盖的码 |
| T-5 | 取消后可重发（P1-3） | 没测 |
| T-6 | 桩服务器计数会重复 | `tst_deepseekclient.cpp:28-48`：完整请求头收到后再来一次 `readyRead`（body 分包）会二次 `++requestCount`，`duplicateRequestIdIsIgnored` 的 `== 1` 断言会假失败；用 per-socket `responded` 标志挡住。另：socket 从不 delete，`cleanup()` 只 `close()`，顺手 `deleteLater` |

---

## 三、不用你改（乙合入时处理）

- `CMakeLists.txt`：`Qt6::Network` 挂到 `lingya_appcore` PUBLIC，可以，甲直接能用。
- `docs/third-party.md` 的 Network 模块登记、章节号与 DEV 编号、交接文档里的集成表述，我来补。

---

## 四、交付完整性（不用改代码，但要定下来）

目前 `src/` 下没有任何地方 `new DeepSeekClient`，也没有 `new AiService`（我 grep 过，装配点为零）。
分支只交了一个"谁都不认识的类"，M5 的"两次真实联网演示"依然做不了。二选一：

- **a)** 补一个 AI 装配入口：按 `SecretStore` 有无密钥返回 `DeepSeekClient` 或 `FakeAiProvider`，
  交接文档写清甲怎么拿、参数从哪来；
- **b)** 不写代码，但在交接文档（H-027）里明确写：接线由甲做、provider 从哪里构造、
  baseUrl/超时/密钥如何传入。

不选的话这条会一直挂在"未打通"。

---

## 五、评审依据

- 被评审对象：`origin/feature/c/M5-deepseek-client`（`3f9a4ea` 实现、`0137a94` 文档）。
- 对照物：`src/ai/IAiProvider.h`（契约）、`src/ai/FakeAiProvider.cpp:32-37`（取消语义）、
  `src/ai/AiService.cpp:199-206/240-250`（取消与失败的状态机）、
  `docs/设计与实施规范_AI版.md` §10.3（职责划分）、§10.7（超时/重试/错误映射）。
- 本轮未做：真实 API 联网验证（无可用密钥，且分支本身未装配，无法触发）。
