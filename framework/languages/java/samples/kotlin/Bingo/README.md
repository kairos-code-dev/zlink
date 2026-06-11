# Bingo Kotlin

Kotlin version of the Bingo matching room sample.

The Kotlin Bingo client and server roles use Protobuf payloads for STREAM
messages. Shared contains only the message contracts used by those roles.
The client verification flow lives in `BingoClientScenario`.

Run the complete sample scenario on Linux or WSL:

```bash
./run_sample.sh
```

On Windows:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```
