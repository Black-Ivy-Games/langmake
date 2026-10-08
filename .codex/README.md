# Langmake Codex delegation

Project-local setup, September 22, 2026. This configures external Codex development;
it does not change Langmake Studio, its providers, the runtime or exported games.

| Role | Model / effort | Scope |
| --- | --- | --- |
| Main agent | Human-selected settings, unchanged | Requirements, architecture, difficult implementation, integration and final review |
| `planner` | `gpt-5.6-luna` / `medium` | Read-only bounded planning and verification checklist |
| `grunt` | `gpt-5.5` / `low` | Lookups, evidence summaries and explicitly assigned mechanical edits |

The repository's AGENTS.md requests selective delegation. There is no need to paste
the article's orchestrator prompt for every feature. Small or tightly coupled tasks
stay with the main agent. At most two children should be active, with no recursive
delegation. The main agent retains difficult coding work as well as final decisions.

Give a child only its objective, relevant context/files, constraints, owned files
if editing, acceptance criteria and expected short output. Use a fresh context
instead of copying the entire chat when the client supports it. Never pay two
agents to repeat the same investigation. Preserve all required guides and checks.

Custom roles are standalone files in `agents/`. The config uses `max_threads`, the
documented legacy alias for the child concurrency limit, for client compatibility.
The main model, main reasoning effort, provider, approvals, plugins and global config
are intentionally not set here. Agent permission defaults can be overridden by the
parent session; behavioral read-only instructions are not an OS security guarantee.

## Loading and fallback

Open a new Codex task in this trusted repository after configuration changes. If
the roles are not visible, restart Codex. Existing task settings and tool surfaces
may not reload role files mid-session. Do not claim named-role loading solely from
valid TOML or a successful manually configured child.

When a client exposes model/effort overrides but no named-role selector, the main
agent can read the relevant role file and pass its model, effort and instructions
explicitly with a small task brief. Do not silently inherit Astra for a cheap task.
If that client cannot select the configured model, report the limitation and handle
the task in the main agent instead of retrying spawns or silently substituting one.

GPT-5.5 retires from Codex with ChatGPT sign-in on October 14, 2026. Before using
`grunt` after that date, select an available replacement with the human and update
its file. API access has separate availability. Model availability and lower usage
must not be inferred from a model name alone.

Subagents still consume tokens and may increase total usage. This setup aims to
reduce expensive main-agent exploration and context overhead; it does not promise
a percentage saving or unchanged quality. Review actual results on representative
tasks, escalate difficult work, and keep the main agent's verification responsibility.

References: [official subagent configuration](https://learn.chatgpt.com/docs/agent-configuration/subagents),
[model availability and retirement](https://learn.chatgpt.com/docs/models).
