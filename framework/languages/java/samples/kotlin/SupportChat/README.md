# SupportChat Kotlin

Kotlin version of the support chat session sample.

The client connects through the Session stream endpoint. Server roles use ZLink
messaging to open a support conversation, join the support side, exchange chat
messages, and push notifications to the connected sessions.

## Run

Run the complete sample scenario on Linux or WSL:

```bash
./run_sample.sh
```

On Windows:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```

## Layout

- `Shared/` contains conversation, message, and notification contracts.
- `Client/` contains the self-checking support chat scenario.
- `Probe/` checks registry and stream readiness before the client runs.
- `Server/Api/` accepts support chat API requests.
- `Server/Session/` owns stream sessions and client push.
- `Server/Support/` manages conversation state and participant actions.
- `Server/Registry/` hosts the discovery registry used by the sample.

The successful run prints `supportchat=completed` from the client and
`supportchat-server-evidence=completed` after server evidence is checked.
