[English](receiver.md) | [한국어](receiver.ko.md)

# Receiver (Removed)

> **Note**: The `zlink_receiver_*` public API has been removed. The receiver
> role is now unified into the Gateway. Use `zlink_gateway_new()` with
> `zlink_gateway_bind()` for server-side operation.
>
> See [gateway.md](gateway.md) for the current API.

## Migration Summary

| Old Receiver API | Replacement |
|---|---|
| `zlink_receiver_new` | `zlink_gateway_new` |
| `zlink_receiver_bind` | `zlink_gateway_bind` |
| `zlink_receiver_connect_registry` | `zlink_gateway_attach_discovery` |
| `zlink_receiver_register` | Automatic via discovery attachment |
| `zlink_receiver_update_weight` | `zlink_gateway_update_peer_weight` |
| `zlink_receiver_set_tls_server` | `zlink_gateway_set_tls_server` |
| `zlink_receiver_last_endpoint` | `zlink_gateway_last_endpoint` |
| `zlink_receiver_set_option` | `zlink_gateway_set_option` |
| `zlink_receiver_set_routing_id` | `zlink_gateway_set_routing_id` |
| `zlink_receiver_routing_id` | `zlink_gateway_routing_id` |
| `zlink_receiver_recv` | Handler callback via `zlink_gateway_new` |
| `zlink_receiver_destroy` | `zlink_gateway_destroy` |
| `zlink_receiver_monitor_open` | `zlink_gateway_monitor_open` |
