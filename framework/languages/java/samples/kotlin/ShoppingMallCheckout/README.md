# ShoppingMallCheckout Kotlin

Kotlin version of the shopping mall checkout workflow sample.

`CommerceApi` receives checkout requests and sends workflow messages over
ZLink. `OrderWorkflow` advances the order state and exposes the evidence used by
the sample client to check success, failure, rebuild, consistency, and scale-out
paths.

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

- `Shared/` contains checkout request, response, and order state contracts.
- `Client/` contains the self-checking checkout scenario.
- `Server/CommerceApi/` receives checkout requests and queries order results.
- `Server/OrderWorkflow/` owns order state transitions and projection rebuild.
- `Server/Registry/` hosts the discovery registry used by the sample.
- `Server/Configuration/` contains endpoint, channel, and packet settings.

The successful run prints `shoppingmall=completed` from the client and
`shoppingmall-server-evidence=completed` after server evidence is checked.
