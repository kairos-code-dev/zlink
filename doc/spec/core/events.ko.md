[English](events.md) | [한국어](events.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 이벤트 카탈로그

이 문서는 raw socket monitor 이벤트의 canonical catalog입니다.

사용 기준:
- [monitoring.ko.md](monitoring.ko.md): monitor API와 peer inspection API
- 이 문서: 이벤트 의미, payload 필드, 권장 gate
- [socket-family-monitor-contract-spec.ko.md](../../plan/direct-callback-recv/socket-family-monitor-contract-spec.ko.md):
  패밀리별 제어 가능 범위와 회귀 테스트 기준

## semantic level

- `CONNECTION_READY`: raw socket 전용 저비용 ready edge
  - raw socket: send/recv ready edge
- queue 이벤트: 로컬 backpressure 관찰

권장 perf gate:
- raw socket perf: `ZLINK_EVENT_CONNECTION_READY`를 expected client 수만큼
  센다
- SPOT perf: 별도 readiness 스트림을 사용하지 않고 explicit `READY/START`
  barrier protocol을 사용한다
- delivery-ready 또는 aggregate-ready monitor event를 perf gate로 사용하지 않음

## Raw Socket Monitor 이벤트

| 상수 | 의미 |
|---|---|
| `ZLINK_EVENT_CONNECTED` | outbound 연결 수립 |
| `ZLINK_EVENT_CONNECT_DELAYED` | 동기 connect 실패 후 재시도 예약 |
| `ZLINK_EVENT_CONNECT_RETRIED` | 비동기 재시도 진행 중 |
| `ZLINK_EVENT_LISTENING` | bind/listen 활성 |
| `ZLINK_EVENT_BIND_FAILED` | bind 실패 |
| `ZLINK_EVENT_ACCEPTED` | inbound 연결 수락 |
| `ZLINK_EVENT_ACCEPT_FAILED` | accept 실패 |
| `ZLINK_EVENT_CLOSED` | 정상 close |
| `ZLINK_EVENT_CLOSE_FAILED` | close 실패 |
| `ZLINK_EVENT_DISCONNECTED` | 세션 연결 해제 |
| `ZLINK_EVENT_MONITOR_STOPPED` | socket monitor 종료 |
| `ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL` | 상세 정보 없는 handshake 실패 |
| `ZLINK_EVENT_CONNECTION_READY` | transport handshake 이후 ready edge / first usable send path |
| `ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL` | protocol handshake 오류 |
| `ZLINK_EVENT_HANDSHAKE_FAILED_AUTH` | auth handshake 오류 |
| `ZLINK_EVENT_PEER_WEIGHT_CHANGED` | 연결된 raw peer의 가중치가 바뀜. `routing_id`가 그 peer를 식별하고, `value`에 새 `0..100` 가중치가 들어간다. `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED`의 별칭이다. |

disconnect reason:
- `ZLINK_DISCONNECT_UNKNOWN`
- `ZLINK_DISCONNECT_HANDSHAKE_FAILED`
- `ZLINK_DISCONNECT_TRANSPORT_ERROR`
- `ZLINK_DISCONNECT_CTX_TERM`

## 예시

raw perf gate:

```c
if (event->event == ZLINK_EVENT_CONNECTION_READY) {
    ++ready_clients;
}
```

SPOT perf gate:

```c
/* client가 control topic으로 READY 송신 */
/* server가 READY == expected_clients를 기다림 */
/* server가 START broadcast */
```
