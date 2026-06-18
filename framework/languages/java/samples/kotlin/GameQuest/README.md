# GameQuest Kotlin

Kotlin version of the game quest progression sample.

The client opens stream sessions for multiple players. `GameApi` accepts game
events and owns player session push. `QuestMission` computes quest progress,
quest completion, idempotency, and projection rebuild behavior through ZLink
channel messages.

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

- `Shared/` contains request, response, and notification contracts.
- `Client/` contains the self-checking multi-player quest scenario.
- `Server/GameApi/` handles player sessions, game events, and client push.
- `Server/QuestMission/` owns quest progress state and rebuild behavior.
- `Server/Registry/` hosts the discovery registry used by the sample.
- `Server/Configuration/` contains endpoint, channel, and packet settings.

The successful run prints `gamequest=completed` from the client and
`gamequest-server-evidence=completed` after server evidence is checked.
