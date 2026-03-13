[English](receiver.md) | [한국어](receiver.ko.md)

# 수신자 (제거됨)

> **참고**: `zlink_receiver_*` public API는 제거되었습니다. receiver 역할은
> Gateway로 통합되었습니다. 서버 측 동작은 `zlink_gateway_new()`와
> `zlink_gateway_bind()`를 사용하세요.
>
> 현재 API는 [gateway.ko.md](gateway.ko.md)를 참고하세요.

## 마이그레이션 요약

| 기존 Receiver API | 대체 API |
|---|---|
| `zlink_receiver_new` | `zlink_gateway_new` |
| `zlink_receiver_bind` | `zlink_gateway_bind` |
| `zlink_receiver_connect_registry` | `zlink_gateway_attach_discovery` |
| `zlink_receiver_register` | discovery attachment으로 자동 처리 |
| `zlink_receiver_update_weight` | `zlink_gateway_update_peer_weight` |
| `zlink_receiver_set_tls_server` | `zlink_gateway_set_tls_server` |
| `zlink_receiver_last_endpoint` | `zlink_gateway_last_endpoint` |
| `zlink_receiver_set_option` | `zlink_gateway_set_option` |
| `zlink_receiver_set_routing_id` | `zlink_gateway_set_routing_id` |
| `zlink_receiver_routing_id` | `zlink_gateway_routing_id` |
| `zlink_receiver_recv` | `zlink_gateway_new` 핸들러 콜백 |
| `zlink_receiver_destroy` | `zlink_gateway_destroy` |
| `zlink_receiver_monitor_open` | `zlink_gateway_monitor_open` |
