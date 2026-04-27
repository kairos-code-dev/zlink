# zlink Go Binding API Reference

The Go binding is implemented in the `zlink` module under `bindings/go`.
Documentation is generated directly from source comments and exported symbols.

## Generate

Local package documentation:

```bash
cd bindings/go
go doc ./...
```

Browsable HTML using pkgsite:

```bash
cd bindings/go
go install golang.org/x/pkgsite/cmd/pkgsite@latest
pkgsite -http=:6060
# Open http://localhost:6060
```

## Public Surface Summary

The exported Go package reflects the shared bindings policy in
`bindings/README.md`.

- multipart-only public send/receive APIs
- blocking methods use direct names such as `Send`, `Recv`, `Publish`,
  `Subscribe`
- non-blocking methods use `Try*`
- non-blocking submit returns `(false, nil)` only for temporary backpressure
- non-blocking receive returns `(value, ok, error)`
- message diagnostics expose `GetProperty` and `RefCount`
- context options are exposed via `Context.Options()` and `ContextOptions`
- typed domain objects are used for `Message`, `RoutingID`, `Received`,
  `TopicMessage`, `SubscriptionEvent`, and `MonitorEvent`
- raw option bags and raw flags are not exposed publicly
- socket-specific capabilities are exposed only on concrete socket types
- service-layer observability uses snapshot/query APIs
- monitor open APIs take typed masks and default to `ALL` when omitted
- callback delivery hops off native callback threads onto Go-managed
  dispatcher goroutines before user handlers run

## Verification Entry Points

- `go test ./...`
- `./tests/run_tests.sh`
- `./samples/run_samples.sh`

## Scope

- exported symbols in package `zlink`
- package-level documentation in `doc.go`
- source comments on exported types and methods
