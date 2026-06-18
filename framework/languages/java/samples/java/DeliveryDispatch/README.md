# DeliveryDispatch

Java version of the delivery assignment and tracking sample.

The sample keeps the external client flow separate from internal ZLink
messaging. The client creates deliveries and watches status updates through the
Session stream endpoint. Server roles use channels, fanout, spots, and stream
sessions to assign a courier, handle reassignment after timeout, and push
delivery status changes back to the subscribed customer session.

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

- `Shared/` contains the message contracts shared by client and server roles.
- `Client/` contains the self-checking delivery scenario.
- `Probe/` checks registry and stream readiness before the client runs.
- `Server/DispatchApi/` accepts delivery creation requests.
- `Server/DispatchCenter/` assigns couriers and performs reassignment.
- `Server/Courier/` receives delivery offers and returns courier decisions.
- `Server/Tracking/` records status changes and publishes session notifications.
- `Server/Session/` owns the stream session endpoint.
- `Server/Registry/` hosts the discovery registry used by the sample.

The successful run prints `deliverydispatch=completed` from the client and
`deliverydispatch-server-evidence=completed` after server evidence is checked.
