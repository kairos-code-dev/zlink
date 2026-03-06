# [API Correction Report] Poller service registration and SpotNode rollback

## Summary
- Date: 2026-03-06
- Target: `core`, `bindings`
- Release target: `v4.0.0`
- Type: breaking public API correction

이번 변경은 `Spot` pollable 버그 수정 후 잘못 public surface로 올라간
`SpotNode.open_*()` 방향을 바로잡는 작업입니다.

최종 정책은 다음으로 고정합니다.

```text
internal socket handle은 public으로 열지 않는다.
poller는 service instance를 직접 등록한다.
```

적용 대상 service는 다음 4개입니다.

- `spot_sub`
- `spot_pub`
- `gateway`
- `receiver`

`SpotNode`는 runtime/config owner로만 남고 poller 대상이 아닙니다.

## Why the previous direction was wrong
직전 수정에서는 다음 방향이 들어갔습니다.

- `zlink_spot_node_open_sub_socket(...)`
- `zlink_spot_node_open_pub_socket(...)`
- `zlink_gateway_open_router_socket(...)`
- `zlink_receiver_open_router_socket(...)`

이 방식은 순서를 숨기는 데는 도움이 되지만, 문제를 해결하는 public 개념이
여전히 `internal socket handle` 중심이었습니다.

사용자 입장에서는 다음이 여전히 애매했습니다.

- 왜 `Spot` 문제가 `SpotNode` API로 풀리는지
- poller를 쓰려면 왜 raw socket을 알아야 하는지
- service instance와 internal socket 중 어느 쪽이 실제 owner인지

또한 `gateway`, `spot_pub`, `spot_sub`는 runtime과 앱이 같은 내부 소켓을
접근할 수 있기 때문에, 단순 getter/open API를 public으로 늘리는 방향은
장기적으로 맞지 않습니다.

## Implemented direction
이번 수정에서는 `open_*()` public API를 제거하고, `Poller`가 service instance를
직접 받는 C API를 추가했습니다.

새 C API:

- `zlink_poller_add_spot_sub(...)`
- `zlink_poller_add_spot_pub(...)`
- `zlink_poller_add_gateway(...)`
- `zlink_poller_add_receiver(...)`
- `zlink_poller_modify_spot_sub(...)`
- `zlink_poller_modify_spot_pub(...)`
- `zlink_poller_modify_gateway(...)`
- `zlink_poller_modify_receiver(...)`
- `zlink_poller_remove_spot_sub(...)`
- `zlink_poller_remove_spot_pub(...)`
- `zlink_poller_remove_gateway(...)`
- `zlink_poller_remove_receiver(...)`

정책은 다음과 같습니다.

- poller는 service instance를 등록한다.
- readiness 이후에도 사용자는 기존 service API를 계속 사용한다.
- internal socket handle은 poller 내부에서만 참조한다.
- `SpotNode.open_*()` 같은 새 public helper는 제거한다.

## Internal behavior
service별 poller 연결 지점은 다음과 같습니다.

- `spot_sub`
  - raw SUB socket이 아니라 internal queue readiness를 poller에 노출합니다.
  - 이를 위해 `spot_sub` 내부 queue가 비어있지 않을 때 signaler fd를 깨웁니다.
  - poller는 이 fd를 감시하고, 사용자는 이후 `zlink_spot_sub_recv(...)`를 호출합니다.
- `spot_pub`
  - node가 소유한 existing PUB socket을 poller에 연결합니다.
- `gateway`
  - gateway가 소유한 existing ROUTER socket을 poller에 연결합니다.
- `receiver`
  - receiver가 소유한 existing ROUTER socket을 poller에 연결합니다.

중요한 점은 `open_*()`처럼 새 소켓을 추가로 만들지 않는다는 점입니다.
poller는 각 service가 원래 소유한 기존 role socket 또는 queue signaler를 사용합니다.

## Removed public helpers
이번 수정으로 다음 public helper를 제거했습니다.

- `zlink_spot_node_open_sub_socket(...)`
- `zlink_spot_node_open_pub_socket(...)`
- `zlink_gateway_open_router_socket(...)`
- `zlink_receiver_open_router_socket(...)`

bindings에서도 같은 helper를 제거했습니다.

- Java `SpotNode.openSubSocket(...)`, `openPubSocket(...)`
- .NET `SpotNode.OpenSubSocket(...)`, `OpenPubSocket(...)`
- Node `spotNode.openSubSocket(...)`, `openPubSocket(...)`
- Python `SpotNode.open_sub_socket(...)`, `open_pub_socket(...)`
- Python `Gateway.open_router_socket()`, `Receiver.open_router_socket(...)`
- C++ binding helper `open_*_handle(...)`

## Example
SPOT subscriber:

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
void *pub = zlink_spot_pub_new(node);
void *sub = zlink_spot_sub_new(node);

zlink_spot_node_connect_peer_pub(node, endpoint);
zlink_spot_sub_subscribe(sub, "bench");

void *poller = zlink_poller_new();
zlink_poller_add_spot_sub(poller, sub, NULL, ZLINK_POLLIN);

zlink_poller_event_t ev;
if (zlink_poller_wait(poller, &ev, 1000) == 1) {
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);
    zlink_spot_sub_recv(sub, &parts, &part_count, ZLINK_DONTWAIT,
                        topic, &topic_len);
}
```

Gateway:

```c
void *poller = zlink_poller_new();
zlink_poller_add_gateway(poller, gateway, NULL, ZLINK_POLLOUT);

zlink_poller_event_t ev;
if (zlink_poller_wait(poller, &ev, 1000) == 1) {
    zlink_gateway_send(gateway, "svc", &msg, 1, 0);
}
```

## Regression coverage
회귀 테스트는 새 public 경로 기준으로 보강했습니다.

- `core/tests/spot/test_spot_mode_split.cpp`
  - `test_spot_sub_can_be_polled_via_service_instance`
  - `test_spot_pub_can_be_polled_via_service_instance`
- `core/tests/discovery/test_gateway.cpp`
  - `test_gateway_can_be_polled_via_service_instance`
  - `test_receiver_can_be_polled_via_service_instance`

검증 포인트:

- poller가 service instance를 직접 받을 수 있어야 한다.
- `spot_sub`는 poller readiness 이후 `zlink_spot_sub_recv(...)`로 정상 수신돼야 한다.
- `spot_pub`, `gateway`, `receiver`는 poller 등록 이후에도 기존 send/recv 흐름이 유지돼야 한다.
- `SpotNode` helper 추가로 인해 생겼던 잘못된 public 방향이 다시 노출되지 않아야 한다.

## Validation
로컬 검증:

- `ctest --output-on-failure -R 'test_ancillaries|test_spot_mode_split|test_gateway$|test_gateway_handover'`
- Java `./gradlew compileJava`
- .NET `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj`
- Python `python3 -m py_compile bindings/python/src/zlink/_spot.py bindings/python/src/zlink/_discovery.py`
- Node `node -c bindings/node/src/index.js`

## Release plan
- version: `4.0.0`
- tag: `core/v4.0.0`
- GitHub Actions로 core release 생성
- release 완료 직후 `bindings/update_zlink_libs.sh`로 bindings native library 최신화

실행 명령:

```bash
bindings/update_zlink_libs.sh core/v4.0.0 --repo kairos-code-dev/zlink --expect-version 4.0.0
```
