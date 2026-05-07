# Connecting AI clients to a Curry MCP server

Curry MCP servers use **stdio transport**: the client spawns the `curry` binary as a
subprocess and communicates with it over stdin/stdout using newline-delimited JSON-RPC 2.0.
All clients below support this model except ChatGPT Desktop, which requires a remote HTTPS
endpoint.

## Quick start

Build Curry and pick an example server:

```bash
cmake -B build && cmake --build build -j$(nproc)

# Symbolic math tools
./build/curry examples/mcp_math.scm

# N-body physics in D dimensions
./build/curry examples/mcp_nbody.scm
```

Use **absolute paths** in every client config — clients typically launch with a minimal
`PATH` that does not include your build directory.

---

## Claude Code (CLI)

Claude Code reads server definitions from three scopes:

| Scope | File | Who sees it |
|-------|------|-------------|
| user | `~/.claude.json` | you, all projects |
| project | `.mcp.json` in project root | everyone (commit this) |
| local | `.claude/settings.local.json` | you, current project only |

Add a server with the CLI:

```bash
claude mcp add curry-math -- /home/you/curry/build/curry /home/you/curry/examples/mcp_math.scm
# --scope user|project|local  (default: local)
```

Or edit the JSON directly:

```json
{
  "mcpServers": {
    "curry-math": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"]
    },
    "curry-nbody": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_nbody.scm"]
    }
  }
}
```

Optional fields:

```json
{
  "mcpServers": {
    "curry-math": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"],
      "env":     { "CURRY_MODULE_PATH": "/home/you/curry/build/mods" }
    }
  }
}
```

Verify the server is visible: `claude mcp list`

---

## Claude Desktop (app)

Config file location:

| Platform | Path |
|----------|------|
| macOS | `~/Library/Application Support/Claude/claude_desktop_config.json` |
| Windows | `%APPDATA%\Claude\claude_desktop_config.json` |

Same JSON format as Claude Code:

```json
{
  "mcpServers": {
    "curry-math": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"]
    }
  }
}
```

**Restart Claude Desktop after editing** — it reads config only at startup.

Logs (useful when a server fails to start):

| Platform | Path |
|----------|------|
| macOS | `~/Library/Logs/Claude/mcp*.log` |
| Windows | `%APPDATA%\Claude\logs\mcp*.log` |

---

## ChatGPT Desktop

ChatGPT Desktop **does not support stdio MCP servers**. It only connects to remote servers
over HTTPS (Streamable HTTP or SSE transport).

To expose a Curry server to ChatGPT you need an HTTP bridge in front of it. One option is
the [`mcp-remote`](https://github.com/geelen/mcp-remote) proxy — it wraps a local stdio
server and exposes it as an SSE endpoint:

```bash
npx mcp-remote /home/you/curry/build/curry /home/you/curry/examples/mcp_math.scm
# Starts an HTTP server on localhost:3000 by default
```

Then add the resulting URL as a custom connector in ChatGPT Settings → Developer Mode → Apps.

---

## OpenAI Agents SDK (Python)

The `openai-agents` package provides `MCPServerStdio` for stdio servers.

```bash
pip install openai-agents
```

```python
import asyncio
from agents import Agent, Runner
from agents.mcp import MCPServerStdio

async def main():
    async with MCPServerStdio(
        name="curry-math",
        params={
            "command": "/home/you/curry/build/curry",
            "args":    ["/home/you/curry/examples/mcp_math.scm"],
        },
        cache_tools_list=True,   # avoids re-fetching the tool list every run
    ) as server:
        agent = Agent(
            name="Math Assistant",
            instructions="Use the curry-math tools to answer symbolic math questions.",
            mcp_servers=[server],
        )
        result = await Runner.run(agent, "Differentiate x³ + 2x with respect to x")
        print(result.final_output)

asyncio.run(main())
```

`MCPServerStdio` params:

| Field | Notes |
|-------|-------|
| `command` | Path to the `curry` binary |
| `args` | Script path and any extra arguments |
| `env` | Extra environment variables (dict) |
| `cwd` | Working directory for the subprocess |

---

## Cursor

Config files:

| Scope | File |
|-------|------|
| user | `~/.cursor/mcp.json` |
| project | `.cursor/mcp.json` in project root |

Same format as Claude Code:

```json
{
  "mcpServers": {
    "curry-math": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"]
    }
  }
}
```

Reload via Cursor Settings → MCP, or restart Cursor.

---

## Windsurf (Codeium)

Config file: `~/.codeium/windsurf/mcp_config.json`

Same format:

```json
{
  "mcpServers": {
    "curry-math": {
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"]
    }
  }
}
```

---

## VS Code (GitHub Copilot)

Create `.vscode/mcp.json` in your workspace. Note the **root key is `"servers"`**, not
`"mcpServers"`, and `"type": "stdio"` is required:

```json
{
  "servers": {
    "curry-math": {
      "type":    "stdio",
      "command": "/home/you/curry/build/curry",
      "args":    ["/home/you/curry/examples/mcp_math.scm"]
    }
  }
}
```

Enable via VS Code Settings → GitHub Copilot → MCP.

---

## Zed

Edit `~/.config/zed/settings.json` and add a `"context_servers"` section:

```json
{
  "context_servers": {
    "curry-math": {
      "command": {
        "path": "/home/you/curry/build/curry",
        "args": ["/home/you/curry/examples/mcp_math.scm"]
      }
    }
  }
}
```

---

## Client comparison

| Client | stdio | Config root key | Config file |
|--------|-------|-----------------|-------------|
| Claude Code | yes | `mcpServers` | `~/.claude.json` or `.mcp.json` |
| Claude Desktop | yes | `mcpServers` | `~/Library/Application Support/Claude/claude_desktop_config.json` |
| ChatGPT Desktop | **no** (HTTP only) | — | — |
| OpenAI Agents SDK | yes | code | `MCPServerStdio(...)` |
| Cursor | yes | `mcpServers` | `~/.cursor/mcp.json` |
| Windsurf | yes | `mcpServers` | `~/.codeium/windsurf/mcp_config.json` |
| VS Code Copilot | yes | `servers` | `.vscode/mcp.json` |
| Zed | yes | `context_servers` | `~/.config/zed/settings.json` |
