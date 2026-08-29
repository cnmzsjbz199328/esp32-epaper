# Claude Code 与 Codex 实时额度调查报告

**调查日期：** 2026-08-29  
**平台：** Windows，Australia/Adelaide  
**本机用户：** tj169  

## 1. 结论摘要

两者都没有一个可以长期依赖的“本地限流状态文件”。

- Claude Code 的计划额度和 reset 时间主要来自远程 Usage API；本地 credentials 文件只保存 OAuth 凭据及额度等级。
- Codex 的 rollout JSONL 会包含最近一次服务端返回的 rate_limits 快照，但它是会话日志中的事件，不是单独的状态文件。
- Codex 有更适合自动化的本地 app-server JSON-RPC：account/rateLimits/read 获取快照，account/rateLimits/updated 接收更新通知。
- Claude Code 当前版本可以直接通过 claude -p "/usage" --output-format json headless 查询，但 JSON 外壳中的 result 仍是面向人类的文本。

本机安装版本：

~~~text
Claude Code 2.1.250
Codex 0.150.1
~~~

## 2. Claude Code

### 2.1 本地路径和文件

Windows 下本机 home 为 C:\Users\tj169：

~~~text
C:\Users\tj169\.claude\.credentials.json
C:\Users\tj169\.claude\history.jsonl
C:\Users\tj169\.claude\projects\<encoded-cwd>\<session-id>.jsonl
~~~

.credentials.json 是 JSON，结构大致如下。真实 token 不应输出、提交或上传：

~~~json
{
  "claudeAiOauth": {
    "accessToken": "<secret>",
    "refreshToken": "<secret>",
    "expiresAt": 1787986148799,
    "refreshTokenExpiresAt": "<secret>",
    "scopes": ["user:inference", "user:profile"],
    "subscriptionType": "pro",
    "rateLimitTier": "default_claude_ai"
  },
  "organizationUuid": "<account-linked-id>"
}
~~~

rateLimitTier 只是账户/订阅等级，不包含当前使用百分比、剩余额度或 reset 时间。

项目会话日志是 JSONL。token 使用量通常在 message.usage：

~~~json
{
  "message": {
    "model": "<model>",
    "usage": {
      "input_tokens": 2,
      "cache_creation_input_tokens": 36309,
      "cache_read_input_tokens": 0,
      "output_tokens": 203,
      "output_tokens_details": {
        "thinking_tokens": 0
      },
      "server_tool_use": {
        "web_fetch_requests": 0,
        "web_search_requests": 0
      },
      "cache_creation": {
        "ephemeral_1h_input_tokens": 0,
        "ephemeral_5m_input_tokens": 0
      },
      "inference_geo": "<string>",
      "iterations": [],
      "speed": "<string>"
    }
  }
}
~~~

这些是 token/请求历史，不是账号限流状态。/usage 的贡献分析会利用本机 session 日志，因此是 approximate，不包括其他设备或 claude.ai 的活动。

### 2.2 Headless 查询

当前本机实测成功：

~~~powershell
claude -p "/usage" --output-format json --no-session-persistence
~~~

机器解析：

~~~powershell
$r = claude -p "/usage" --output-format json --no-session-persistence | ConvertFrom-Json
$r.subtype
$r.result
~~~

JSON envelope 包含 subtype、num_turns、total_cost_usd、duration_ms、result、session_id 等字段。result 是额度文本，不是稳定的数值 schema。

本次实测结果：

~~~text
Current session: 20% used · resets Aug 29, 1:19pm (Australia/Adelaide)
Current week (all models): 14% used · resets Sep 2, 9:29am (Australia/Adelaide)
~~~

本次查询返回 num_turns: 0、total_cost_usd: 0，未执行模型任务；但不应每秒轮询。

### 2.3 结构化 Usage API

社区通过 Claude Code 客户端逆向确认的接口为：

~~~text
GET https://api.anthropic.com/api/oauth/usage
Authorization: Bearer <claudeAiOauth.accessToken>
anthropic-beta: oauth-2025-04-20
~~~

常见格式：

~~~json
{
  "five_hour": {
    "utilization": 20,
    "resets_at": "2026-08-29T03:49:00Z"
  },
  "seven_day": {
    "utilization": 14,
    "resets_at": "2026-09-02T23:59:00Z"
  },
  "seven_day_sonnet": null,
  "seven_day_opus": null,
  "seven_day_oauth_apps": null,
  "extra_usage": null
}
~~~

utilization 是百分比，不是 token 数；剩余百分比通常是 100 - utilization。某些版本/账户可能返回新的 limits 数组，因此解析器应兼容版本变化。

该接口不是 Anthropic 面向第三方公开承诺的稳定 API。自动化优先使用官方 CLI；确实需要结构化字段时才直接调用，并加入缓存、退避和 token 脱敏。

## 3. Codex

### 3.1 本地路径和限流事件

本机 Codex home：

~~~text
C:\Users\tj169\.codex
~~~

没有发现独立的 rate_limits.json、quota.json 或同类状态文件。相关文件：

~~~text
C:\Users\tj169\.codex\auth.json
C:\Users\tj169\.codex\state_5.sqlite
C:\Users\tj169\.codex\sessions\YYYY\MM\DD\rollout-*.jsonl
~~~

state_5.sqlite 的 schema 主要是 threads、projects、thread_items 等本地应用状态，没有 rate_limits 表；auth.json 只保存认证信息。

限流快照实际位于：

~~~text
C:\Users\tj169\.codex\sessions\YYYY\MM\DD\rollout-*.jsonl
~~~

本机发现的具体样例：

~~~text
C:\Users\tj169\.codex\sessions\2026\08\29\rollout-2026-08-29T09-48-24-01a04ae1-c1e0-77b1-952b-0e7908112d5d.jsonl
~~~

因此“确切路径”应理解为路径模式，而不是固定单文件路径。

### 3.2 Codex rollout schema

限流信息位于 event_msg 行的 payload.rate_limits：

~~~json
{
  "type": "event_msg",
  "payload": {
    "type": "token_count",
    "rate_limits": {
      "limit_id": "codex",
      "limit_name": null,
      "primary": {
        "used_percent": 9,
        "window_minutes": 300,
        "resets_at": 1787977578
      },
      "secondary": {
        "used_percent": 2,
        "window_minutes": 10080,
        "resets_at": 1788504280
      },
      "credits": {
        "has_credits": false,
        "unlimited": false,
        "balance": "0"
      },
      "individual_limit": null,
      "spend_control_reached": false,
      "plan_type": "plus",
      "rate_limit_reached_type": null
    }
  }
}
~~~

字段含义：

| 字段 | 含义 |
| --- | --- |
| primary.used_percent | 当前主限流窗口使用百分比，通常是 5 小时窗口 |
| primary.window_minutes | 主窗口长度，常见为 300 |
| secondary.used_percent | 次限流窗口使用百分比，通常是周窗口 |
| secondary.window_minutes | 次窗口长度，常见为 10080 |
| resets_at | Unix 秒时间戳 |
| credits.balance | 额度余额字符串 |
| plan_type | 账户计划，例如 plus、pro |
| rate_limit_reached_type | 已触发限流时的后端分类 |

JSONL 使用 snake_case；app-server RPC 使用 camelCase，不能混用解析器。

### 3.3 TokenMeter 核对

TokenMeter 当前 Codex collector 扫描：

~~~text
~/.codex/sessions
~/.codex/archived_sessions
~~~

但其 codex.go 主要解析 event_msg 的 token_count、模型和 token 用量，用于本地 token/cost 统计；它不是官方限流状态的持久化实现。

参考：
https://github.com/tt-a1i/tokenmeter/blob/main/internal/collector/codex.go

### 3.4 Headless：app-server JSON-RPC

启动：

~~~text
codex app-server --listen stdio://
~~~

发送：

~~~json
{"method":"initialize","id":1,"params":{"clientInfo":{"name":"quota-probe","title":"Quota Probe","version":"0.1.0"}}}
{"method":"initialized","params":{}}
{"method":"account/rateLimits/read","id":2}
~~~

响应核心字段：

~~~json
{
  "id": 2,
  "result": {
    "rateLimits": {
      "limitId": "codex",
      "primary": {
        "usedPercent": 9,
        "windowDurationMins": 300,
        "resetsAt": 1787977578
      },
      "secondary": {
        "usedPercent": 2,
        "windowDurationMins": 10080,
        "resetsAt": 1788504280
      },
      "credits": {
        "hasCredits": false,
        "unlimited": false,
        "balance": "0"
      },
      "planType": "plus",
      "spendControlReached": false,
      "rateLimitReachedType": null
    },
    "rateLimitResetCredits": {
      "availableCount": 1
    }
  }
}
~~~

实际响应可能在 rateLimitsByLimitId 中包含额外 bucket，例如 base_model_inference。

长期 monitor 可监听：

~~~text
account/rateLimits/updated
~~~

该通知是 sparse update；客户端应与最近一次完整的 account/rateLimits/read 合并，或收到通知后重新 fetch 完整快照。

## 4. 本次实机实时结果

### Claude Code 2.1.250

~~~text
订阅：Pro
当前 session：20% used
当前 week（all models）：14% used
session reset：2026-08-29 13:19 Adelaide
week reset：2026-09-02 09:29 Adelaide

Last 24h：351 requests，8 sessions
Last 7d：3940 requests，54 sessions
~~~

贡献分析是本机本地 session 的近似统计，不等于账号全局精确账单。

### Codex 0.150.1 app-server

~~~text
计划：Plus
primary：9%，窗口 300 分钟
primary reset：2026-08-29 13:56:18 Adelaide
secondary：2%，窗口 10080 分钟
secondary reset：2026-09-04 16:14:40 Adelaide
credits：0
spend control：false
可用 full reset：1
~~~

另有一个 base_model_inference bucket：

~~~text
0%，窗口 10080 分钟
reset：2026-09-05 10:00:11 Adelaide
~~~

以上数字是调查时刻的快照，不应写死在程序或 dashboard 中。

## 5. 实现建议

1. Claude：使用 claude -p "/usage" --output-format json --no-session-persistence；对 result 做容错解析。必须有结构化字段时，再考虑 OAuth Usage API。
2. Codex：使用 app-server account/rateLimits/read；长期进程监听 account/rateLimits/updated。
3. 离线 fallback：扫描最近 Codex rollout JSONL，寻找最后一个非空的 payload.rate_limits；它可能滞后。
4. 不要使用 credentials.json、auth.json、state_5.sqlite 推断当前额度。

安全注意：

- 不要打印或上传 Claude accessToken/refreshToken、Codex auth.json 或完整请求头。
- OAuth Usage API 和其他私有接口可能发生 401/429；实现缓存、退避和 stale-cache fallback。
- 不要为了刷新额度而启动真实模型任务；codex exec --json 是任务流接口，不是 quota-only 接口。
- 字段、窗口和 bucket 可能随账户计划及客户端版本变化。

## 6. 参考资料

- Claude Code programmatic：https://code.claude.com/docs/en/headless
- Claude Code costs：https://code.claude.com/docs/en/costs
- Claude Code cheatsheet：https://support.claude.com/en/articles/14553413-claude-code-cheatsheet
- Codex ChatGPT plan usage：https://help.openai.com/en/articles/11369540/
- OpenAI Codex app-server：https://github.com/openai/codex/blob/main/codex-rs/app-server/README.md
- OpenAI Codex rate-limit parser：https://github.com/openai/codex/blob/main/codex-rs/codex-api/src/rate_limits.rs
- TokenMeter Codex collector：https://github.com/tt-a1i/tokenmeter/blob/main/internal/collector/codex.go
- Claude usage API 类型参考：https://git.jon-e.net/jonny/claude-code/src/commit/59a215da280cb4d2147f39f55c3bdb5779113ccc/src/services/api/usage.ts

