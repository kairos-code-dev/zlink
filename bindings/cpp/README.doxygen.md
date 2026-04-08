# zlink C++ Binding API Reference

This reference is generated from the header-only C++ wrapper in `bindings/cpp/include/zlink`.

## Generate

```bash
cd bindings/cpp
doxygen Doxyfile
```

Generated HTML entrypoint:

```text
bindings/cpp/doxygen/html/index.html
```

## Scope

- Public C++ wrapper headers in `include/zlink/`
- Service wrappers in `include/zlink/services/`
- Runtime/helper wrappers (`context_t`, `socket_t`, `message_t`, `poller_t`, etc.)
- `context_t::options()` exposes the typed `context_options_t` facade
- `message_t` diagnostics use `get_property()` and `ref_count()`
- `service_monitor_handle_t` is discovery-only; `monitor_handle_t` stays socket-only
