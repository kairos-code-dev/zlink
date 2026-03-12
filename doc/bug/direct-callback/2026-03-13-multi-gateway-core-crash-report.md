# MULTI_GATEWAY direct-callback core crash report

## Summary

- 증상: `core/perf` multi smoke에서 `GATEWAY` 패턴만 server 측 crash로 실패한다.
- 상태: `DEALER_DEALER`, `PUBSUB`, `SPOT`는 smoke 통과 상태이고 `GATEWAY`만 blocker다.
- 성격: perf harness 불안정이 아니라 gateway direct-callback receive path의 core message lifetime/ownership 버그로 판단된다.

## Impact

- 막힌 게이트: multi smoke 완료, 산출물/메트릭 최종 검증, full single/multi 실행
- 최신 wrapper 결과: `partial`
- 최신 결과 파일: `core/perf/results/multi/report/perf_linux_20260313_043015.txt`
- wrapper failure label: `GATEWAY current tcp 1024B: non_zero_exit_1_size_1024`

## Reproduction

### 1. Wrapper reproduction

```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
PERF_DEBUG=1 \
PERF_ALLOW_MULTI=1 \
PERF_POLICY=1 \
./core/perf/run_benchmarks_multi.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern GATEWAY \
  --transports tcp \
  --msg-sizes 1024 \
  --runs 1 \
  --warmup 1 \
  --duration 2 \
  --clients 16
```

기대 결과:

- `complete`
- server/client exit code 0

실제 결과:

- `partial`
- server가 중간에 `SIGBUS` 또는 `SIGSEGV`로 종료되며 wrapper가 `non_zero_exit_1_size_1024`로 정리한다.

### 2. Direct split reproduction

서버:

```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
tail -f /dev/null | \
env \
  PERF_DEBUG=1 \
  PERF_MULTI_SERVER_BIND_PORT=40191 \
  PERF_PATTERN=GATEWAY \
  PERF_CLIENTS=16 \
  PERF_WARMUP_SECONDS=1 \
  PERF_DURATION_SECONDS=2 \
  PERF_SETTLE_MS=500 \
  core/build/bin/comp_src_gateway_server current tcp
```

클라이언트:

```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
PERF_DEBUG=1 \
PERF_MULTI_SERVER_BIND_PORT=40191 \
PERF_PATTERN=GATEWAY \
PERF_CLIENTS=16 \
PERF_WARMUP_SECONDS=1 \
PERF_DURATION_SECONDS=2 \
PERF_SETTLE_MS=500 \
core/build/bin/comp_src_gateway_client current tcp 1024 \
  --endpoint tcp://127.0.0.1:40191
```

관측:

- sequential slot priming을 적용한 상태에서도 slot 0..7 정도까지는 왕복이 성립한다.
- 그 뒤 server가 crash한다.
- representative stderr:

```text
[multi-gateway-server] recv size=1024 part_count=1 pending=0
[multi-gateway-server] send ok size=1024 part_count=1
...
Bus error (core dumped)
```

## gdb result

다음 형태로 재현 가능했다.

```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
gdb -q -batch \
  -ex 'handle SIGPIPE nostop noprint pass' \
  -ex run \
  -ex bt \
  --args core/build/bin/comp_src_gateway_server current tcp < <(tail -f /dev/null)
```

그 다음 client를 server가 출력한 endpoint로 접속시키면 server가 IO thread에서 죽는다.

대표 backtrace:

```text
Thread 5 "ZLINKbg/IO/1" received signal SIGBUS, Bus error.
0x... in zlink::msg_t::close()
#1  (anonymous namespace)::gateway_server_handler(
        zlink_routing_id_t const*, zlink_msg_t*, unsigned long)
#2  zlink::socket_base_t::invoke_socket_msg_handler(...)
#3  zlink::router_t::xsocket_msg_dispatch(...)
#4  zlink::socket_base_t::socket_msg_dispatch_from_io(...)
#5  zlink::session_base_t::push_msg(...)
#6  zlink::asio_zmp_engine_t::decode_and_push(...)
#7  zlink::asio_engine_t::process_input()
#8  zlink::asio_engine_t::on_read_complete(...)
#9  boost::asio::detail::reactive_socket_recv_op...
#10 zlink::asio_poller_t::loop()
#11 thread_routine
```

## Why this looks like a core bug

- crash 지점이 perf 코드 바깥인 `zlink::msg_t::close()`다.
- stack상 gateway callback에서 받은 `parts`를 정리하는 시점에 죽는다.
- `router_t::xsocket_msg_dispatch()`는 `_dispatch_parts`를 채운 뒤 handler를 호출하고, handler 반환 뒤에는 `_dispatch_parts.clear()`만 수행한다.
- `socket_base_t::invoke_socket_msg_handler()`도 current socket TLS 설정 후 handler를 호출할 뿐 message close를 대신하지 않는다.
- 즉 callback delivery 경로의 message ownership/lifetime contract가 깨졌거나 gateway 서비스가 그 contract를 잘못 사용하고 있을 가능성이 높다.

## Suspect area

- `core/src/services/gateway/gateway.cpp`
- `core/src/sockets/router.cpp`
- `core/src/sockets/socket_base.cpp`

특히 확인이 필요한 축:

- gateway callback에서 받은 `parts`의 close 책임이 현재 contract와 실제 구현에서 일치하는지
- router dispatch vector가 callback 중/후에 aliasing 또는 double-close 상황을 만들지 않는지
- source routing id와 reply path가 callback 이후에도 안전한 lifetime을 보장하는지

## Perf-layer changes already tried

아래는 이미 반영했지만 crash를 제거하지 못했다. core 담당자는 이 범위부터 다시 의심할 필요는 낮다.

- client payload ownership 수정
  - `zlink_msg_init_data()` 대신 owned buffer(`zlink_msg_init_size` + `memcpy`) 사용
- server reply payload도 owned copy로 전환
- client sequential slot priming 적용
  - 모든 소켓을 한 번에 밀어넣지 않고 slot별로 handshake를 정렬
- server에서 multipart noise 필터링 추가
  - `part_count != 1` 요청은 echo send 경로에서 제외
- server에서 `source_rid`를 local copy로 사용

결과:

- 크래시가 더 늦게 보이거나 패턴이 약간 바뀌기는 했지만 최종적으로는 같은 종류의 crash가 계속 발생했다.

## Expected behavior

- multi gateway perf server는 client 수가 늘어나도 callback 처리 중 crash 없이 echo를 계속 반환해야 한다.
- callback에 전달된 receive message part를 handler가 close하는 방식이라면 그 contract가 router/gateway/service 경로에서 일관되어야 한다.
- callback에 전달된 receive message part를 handler가 close하면 안 되는 구조라면 gateway service handler가 그 규칙을 어기고 있는 것이다.

## Suggested next debugging steps

- `gateway_server_handler()`에서 close하는 `parts[i]`가 dispatch-owned buffer를 잘못 닫는지 먼저 확인
- `router_t::_dispatch_parts`에 대해 callback 전후 close state를 추적
- ASan/UBSan 또는 debug poison을 켠 상태에서 direct split reproduction으로 double-close/use-after-free 여부 확인
- gateway service가 raw socket callback contract와 동일한 close 규칙을 따르는지 문서와 구현을 대조

## Related perf files

- `core/perf/multi/src/perf_multi_gateway_client.cpp`
- `core/perf/multi/src/perf_multi_gateway_server.cpp`

## Current gate status

- thread-safe contract 단계는 별도 진행 중
- perf multi smoke는 `GATEWAY` blocker 때문에 아직 closed gate가 아니다
- 이 버그가 해결되어야 multi smoke 완료 판정과 full run으로 넘어갈 수 있다
