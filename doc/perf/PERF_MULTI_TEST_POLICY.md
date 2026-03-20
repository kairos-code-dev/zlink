# zlink Multi Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — multi-client 벤치마크
> **Policy Version**: 1.7
> **Date**: 2026-03-20
> **Scope**: zlink multi-client 성능 테스트 정책
>
> 본 정책은 `perf/multi`의 C++ 벤치마크뿐 아니라 모든 바인딩 라이브러리(`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`, `bindings/python`)의 multi-client 성능 테스트에도 동일하게 적용된다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md) — 공통 디렉터리 구조, 통합 실행, 비교 스크립트
> **관련 문서**: [PERF_SINGLE_TEST_POLICY.md](PERF_SINGLE_TEST_POLICY.md) — single-client 성능 테스트

---

## 1. 측정 기준

| 항목 | 기준 |
|------|------|
| 측정 모델 | time-based, 패턴별 phase: warmup → settle(optional) → active(throughput+latency 동시) |
| throughput | `recv_count / duration_seconds` — echo 패턴: `ops/s`, one-way 패턴: `msg/s` |
| latency | active phase에서 수신된 메시지의 내장 timestamp(header)로 전수 집계 |
| 대표값 | median (runs > 1) |
| 기본 runs | 1 |
| 결과 출력 | RESULT line |

### 1.1 Multi 핵심 정책

- 목적
  - 벤치 코드가 병목이 되지 않게 유지하면서, 선택된 I/O 모델의 성능을 측정한다.
- 두 가지 I/O 모델 지원 (`--recv` 옵션)
  - **recv 모델** (기본, `--recv recv`):
    - recv: poller `POLLIN` readiness 감지 → `zlink_recv()` / `zlink_msg_recv()`
      비동기 drain 루프 (react 방식). poller가 readable을 알려주면 수신 가능한
      만큼 drain한다.
    - send: `send(..., DONTWAIT)` nonblocking send 사용.
    - send backpressure: poller `POLLOUT` readiness 감지 → writable 상태에서만
      send 수행. `EAGAIN` 발생 시 `POLLOUT`을 대기하고, writable이 되면 재개한다.
    - app thread가 poller event loop를 직접 구동하며, `POLLIN` / `POLLOUT`
      이벤트에 따라 recv drain과 send 재개를 처리한다.
    - `send_ready_handler`는 사용하지 않는다.
  - **callback 모델** (`--recv callback`):
    - multi suite에서 callback 모델은 `MULTI_SPOT`, `MULTI_STREAM`에만
      허용된다.
    - recv: pattern별 callback API 등록 → 메시지 도착 시 I/O thread에서 콜백
      호출. `zlink_recv()` / `zlink_msg_recv()` 동기 수신 API는 측정 경로에
      사용하지 않는다.
    - callback hot path는 메시지에서 metric header와 timestamp 등 필요한 최소
      메타데이터만 추출해 bounded queue로 enqueue한다. `zlink_msg_t` handle,
      payload pointer, multipart parts 소유권은 callback 밖으로 넘기지 않는다.
    - send: protocol상 즉시 echo/ack가 필요한 경우에만 recv callback 안에서
      `send(DONTWAIT)`를 호출할 수 있다. callback 중 same-handle send는
      thread-safe socket plan에 의해 공식 허용된다.
    - send backpressure: `zlink_send_ready_handler()` 등록 → writable
      transition 시 callback으로 통지. `EAGAIN` 시 역할별 전략을 적용한다 (아래 참조).
    - throughput/latency 집계, phase window 판정, 결과 출력용 통계 계산은
      callback 안에서 직접 수행하지 않고 metric worker가 queue를 drain하며
      처리한다.
    - app thread는 setup/phase 제어/teardown을 담당하고, 측정 구간에서는
      I/O thread callback과 metric worker가 역할을 분리한다.
    - poller는 사용하지 않는다.
  - 한 측정 구간에서 두 모델의 recv/send 메커니즘을 섞지 않는다.
  - 지원하지 않는 multi pattern에서 `--recv callback`을 주면 즉시 실패한다.
- `while (send 실패)` 식의 즉시 재시도는 양쪽 모델 모두 금지한다.
- backpressure 전략 (역할별, 양쪽 모델 공통 — 통지 메커니즘만 다름)
  - **echo 서버** (소켓 1개 × 클라이언트 N개):
    - `EAGAIN` 시 per-socket pending deque에 메시지를 저장한다.
    - pending이 있는 동안 새 send는 pending deque에 추가만 한다.
    - recv 모델: poller `POLLOUT` readiness에서 pending deque를 `EAGAIN`까지 drain한다.
    - callback 모델: send-ready callback에서 pending deque를 `EAGAIN`까지 drain한다.
    - 소켓 1개로 N개 클라이언트를 처리하므로, EAGAIN 중에도 다른 클라이언트의 메시지가 도착할 수 있어 deque가 필요하다.
  - **echo 클라이언트** (per-socket, inflight 1):
    - `EAGAIN` 시 `bool send_pending` 플래그만 설정한다.
    - recv 모델: poller `POLLOUT` readiness에서 플래그 확인 후 재전송한다.
    - callback 모델: send-ready callback에서 플래그 확인 후 재전송한다.
    - 응답 수신 → 다음 전송의 1:1 대응이므로 deque 불필요하다.
  - **one-way sender** (단일 흐름):
    - `EAGAIN` 시 `bool send_pending` 플래그만 설정한다.
    - recv 모델: poller `POLLOUT` readiness에서 플래그 확인 후 재전송한다.
    - callback 모델: send-ready callback에서 플래그 확인 후 재전송한다.
  - **one-way receiver**: send 없음, backpressure 불필요.
- 동시성
  - callback 모델: recv callback과 send-ready callback은 동일 소켓에 대해 직렬화된다
    (같은 I/O thread). 이 직렬화는 same-socket send/pending 보호에는 충분하지만,
    callback과 metric worker 사이의 handoff까지 없애주지는 않으므로 metric event
    전달에는 bounded queue를 사용한다.
  - recv 모델: app thread가 poller event loop를 단일 스레드로 구동하므로
    동일하게 직렬화된다.
- 성능 참고
  - perf 환경(HWM 100, inflight 1/peer)에서 EAGAIN은 사실상 발생하지 않는다.
  - deque/플래그는 정확성을 위한 safety net이며, hot path에서는 `empty()` / `bool` 체크만 수행된다.
- 한 줄 요약
  - recv 모델: `multi = poller POLLIN drain + POLLOUT backpressure`
  - callback 모델: `multi = callback recv + bounded queue + metric worker + send_ready_handler backpressure`
  - backpressure 전략: `echo 서버: deque, echo 클라이언트/one-way sender: bool 플래그`
- 연결 준비와 benchmark start gate는 socket/service monitoring의
  **delivery-ready event**만 사용한다.
- multi perf는 delivery-ready event 확인 이전에 측정을 시작하지 않는다.
- multi perf start gate 구현에서 아래를 금지한다.
  - ad-hoc sleep/retry handshake loop
  - monitor snapshot polling
- 패턴별 ready gate 이벤트는
  [`../guide/06-monitoring.ko.md`](../guide/06-monitoring.ko.md)의
  "메시징 시작 전 준비 확인" 절을 단일 기준으로 따른다.

### 1.2 프로세스 모델

Multi 벤치마크는 **server/client 별도 프로세스**로 동작한다.

| 역할 | 바이너리 | 책임 |
|------|----------|------|
| server | `comp_src_<pattern>_server(.exe)` | bind, relay/echo, server-side 리소스 보고 |
| client | `comp_src_<pattern>_client(.exe)` | connect, 패턴별 phase 정책에 따라 throughput/latency 측정, client-side 리소스 보고 |

```text
┌─ server process ─────────────────────┐    ┌─ client process ──────────────────────┐
│  bind(endpoint)                      │    │  connect(endpoint) × N clients        │
│  relay/echo received messages        │◄──►│  phase별 throughput/latency 측정         │
│  RESULT: server_cpu_pct, server_mem_mb│    │  RESULT: throughput, latency, p95/p99, │
│  READY stdout / stdin STOP 제어       │    │         client_cpu_pct, client_mem_mb  │
└──────────────────────────────────────┘    └───────────────────────────────────────┘
                        ▲                                      ▲
                        └────── 스크립트가 양쪽 프로세스를 관리 ──┘
```

#### 프로세스 간 조정 프로토콜

| 단계 | 동작 |
|------|------|
| 1. server 시작 | 스크립트가 server 바이너리를 spawn |
| 2. server READY | server가 bind 완료 후 stdout에 `READY,<endpoint>` 출력 |
| 3. client 시작 | 스크립트가 READY를 읽은 후 client 바이너리를 spawn (`--endpoint <endpoint>`) |
| 4. 측정 수행 | client가 active phase에서 throughput/latency를 동시 측정 |
| 5. client 종료 | client가 phase 완료 후 종료 (exit code 0) |
| 6. server 종료 | 스크립트가 server stdin에 `STOP` 메시지 송신 → graceful shutdown 대기 → timeout 시 SIGTERM (Linux) / TerminateProcess (Windows) → 재 timeout 시 SIGKILL (Linux). server/client가 출력한 RESULT line을 합산 |

> **server 종료 순서**: ① stdin `STOP\n` 송신 + stdin close ② shutdown timeout 대기 ③ `terminate()` (SIGTERM) ④ 2차 timeout 대기 ⑤ `kill()` (SIGKILL). server는 stdin에서 `STOP` 또는 `QUIT` 수신 시 graceful shutdown을 수행한다.

- server는 client 종료까지 상시 대기하며 relay/echo를 수행한다.
- phase 전환은 패턴별로 제어한다: echo는 client가 phase를 제어하고 server는 relay/echo 대기, one-way는 sender/receiver가 동일 순서의 phase를 수행한다. throughput/latency는 모두 active phase 한 구간에서 계산한다.
- 스크립트는 양쪽 프로세스의 stdout을 수집하고, 종료 코드를 확인하여 결과를 합산한다.

#### 소스 파일 구조

```text
perf/multi/
├── common/
│   ├── perf_common.hpp                # 공통 (settings, result, utilities)
│   └── perf_common_multi.hpp          # multi 설정
├── src/
│   ├── perf_multi_<pattern>_server.cpp    # server (server)
│   ├── perf_multi_<pattern>_client.cpp    # client (client)
│   └── ...
```

- 모든 패턴은 `_server.cpp` / `_client.cpp` **별도 소스 파일 / 별도 바이너리**로 작성한다.
- 공통 로직(settings 해석, RESULT 출력, TLS 설정 등)은 `perf_common_multi.hpp`에 유지한다.

---

## 2. 옵션 우선순위

실행 옵션이 여러 경로로 지정될 수 있는 경우 아래 우선순위를 따른다 (높은 순).

| 옵션 | CLI 인자 | 환경 변수 | 기본값 |
|------|----------|-----------|--------|
| runs | `--runs N` | — | 1 |
| msg sizes | `--msg-sizes` | `PERF_MSG_SIZES` | 표준 6종 |
| transports | `--transports` | `PERF_TRANSPORTS` | 패턴별 기본값 (§11.3 참조) |
| clients | `--clients` | `PERF_MULTI_CLIENTS` | 100 (stream=10000), 메모리 가드에 의해 자동 하향 가능 |

- **CLI 인자 > 환경 변수 > 기본값** 순으로 적용한다.
- CLI/환경 변수로 clients를 명시하지 않은 경우, shell wrapper의 메모리 가드(`PERF_MULTI_MEMORY_BUDGET_PCT` 기반)가 가용 메모리에 따라 기본값을 자동 하향(capping)할 수 있다. `PERF_SKIP_MEMORY_CHECK=1`로 비활성화 가능.
- `--runs`를 생략하면 기본값 1을 사용한다.

---

## 3. 테스트 유효성 기준

### 3.1 결과 상태 분류

| 상태 | 조건 | 집계 |
|------|------|------|
| success | RESULT line 정상 출력 | 유효 결과 |
| unsupported | 패턴-transport 조합 미지원 | 결과 제외, fail 아님 |
| skip | 환경 미충족 (OS, 아키텍처, nofile limit 등) | 결과 제외, fail 아님 |
| fail | timeout / no_data / non-zero exit | 무효 처리 |

#### 상태 판정 토큰

스크립트는 바이너리의 stdout과 종료 코드를 조합하여 상태를 판정한다.

| 상태 | 판정 기준 |
|------|-----------|
| success | exit code 0 + RESULT line 존재 |
| unsupported | stdout에 `UNSUPPORTED` 토큰 출력 + exit code 0, 또는 stderr에 `protocol not supported` 포함 |
| skip | stdout에 `SKIP` 토큰 출력 + exit code 0 |
| fail | exit code ≠ 0, 또는 timeout, 또는 RESULT line 미출력 (exit 0이나 데이터 없음 = no_data) |

- `UNSUPPORTED` 토큰 형식: `UNSUPPORTED,<lib>,<pattern>,<transport>`
- `SKIP` 토큰 형식: `SKIP,<lib>,<pattern>,<transport>,<reason>`
- **stderr 기반 unsupported 판정**: 바이너리 stderr에 `protocol not supported` 문자열이 포함되면 실행 엔진이 해당 조합을 `unsupported`로 자동 분류한다. 이는 런타임에서 지원되지 않는 transport를 감지하는 메커니즘이다.
- 동일 조합에서 RESULT line과 UNSUPPORTED/SKIP 토큰이 동시에 출력되면 **RESULT line을 우선**한다.
- MULTI_STREAM에서 테스트 모델 위반(예: non-STREAM server 사용, zlink STREAM
  client `connect()` 경로 사용)은 `UNSUPPORTED`/`SKIP` 대상이 아니다.
- 해당 구현 경로는 코드에서 삭제하고, `zlink STREAM server(bind-only) + raw client(connect)` 모델로 재구현해야 한다.

### 3.2 유효성 규칙

1. 모든 `pattern/transport/size` 조합에서 RESULT line이 출력되어야 한다.
2. `unsupported`는 fail 집계에서 제외한다.
3. `skip`은 fail 집계에서 제외한다. 단, 결과 테이블에서 skip 조합의 행은 `fail`로 표시된다 (내부적으로는 skip으로 분류되어 완료 판정에서 제외).
4. runs > 1인 경우 대표값은 **median**을 사용한다.
5. 동일 `pattern/transport/size/metric` 조합의 RESULT line이 **중복** 출력되면 **마지막 값**을 사용한다. 중복 자체는 에러가 아니며 warning을 출력한다.
6. RESULT line의 필드 수가 7개가 아니면 해당 라인을 무시하고 warning을 출력한다.

### 3.3 실행 순서

스크립트 1회 실행으로 요청된 모든 패턴/transport를 순차 측정한다.

```text
for pattern in [MULTI_DEALER_DEALER, MULTI_PUBSUB, ...]:
    for transport in pattern_transports:   # non-service: tcp,tls,ws,wss,ipc / service+stream: tcp,tls,ws,wss
        for run in 1..N:
            for size in msg_sizes:
                spawn server(pattern, transport)     # server 프로세스 시작
                wait READY                           # server stdout에서 READY,<endpoint> 대기
                spawn client(pattern, transport, size, endpoint)  # client 프로세스 시작 (size 1개)
                wait client exit                     # client 종료 대기, RESULT line 수집
                stop server                          # server 종료, server RESULT line 수집
            run_cooldown                             # 3s (PERF_MULTI_RUN_COOLDOWN_MS)
        transport_transition_cooldown                # 3s (PERF_MULTI_TRANSPORT_TRANSITION_MS)
    pattern_transition_cooldown                      # 3s (PERF_MULTI_PATTERN_TRANSITION_MS)
```

| 전환 구간 | 기본값 | 환경 변수 | 이유 |
|-----------|--------|-----------|------|
| run 간 cooldown | 3000ms | `PERF_MULTI_RUN_COOLDOWN_MS` | 동일 조합 반복 run 사이 안정화 |
| transport 전환 | 3000ms | `PERF_MULTI_TRANSPORT_TRANSITION_MS` | 이전 transport 소켓 정리 (TIME_WAIT 해소) |
| pattern 전환 | 3000ms | `PERF_MULTI_PATTERN_TRANSITION_MS` | 이전 패턴의 전체 클라이언트 소켓 정리 |

- Multi 벤치마크는 대량의 클라이언트 소켓(1000~10000)을 사용하므로, transport/pattern 전환 시 OS 소켓 리소스 해제를 위한 충분한 대기가 필요하다.
- 전환 cooldown은 이전 server/client 프로세스 종료 후 다음 server 실행 전에 **스크립트 레벨**에서 `sleep`으로 수행한다.

### 3.4 종료 코드

| 종료 코드 | 의미 | 상황 |
|-----------|------|------|
| 0 | 성공 | 모든 조합 complete |
| 1 | 실행 오류 | 빌드 실패, 바이너리 미존재, partial 결과 |

- partial 상태(일부 조합 실패)는 종료 코드 1이다.
- 여러 오류 조건이 동시에 발생하면 가장 높은 종료 코드를 반환한다.

### 3.5 실패 처리: Retry 금지

실패한 조합을 자동으로 재시도하지 않는다. 상세 정책은 [PERF_POLICY.md § 8](PERF_POLICY.md)을 참조한다.

> STREAM 서버도 동일 정책을 따른다. `EAGAIN`은 pending 상태로 기록하고 `send_ready_handler` 콜백에서 재개할 수 있으나, 동일 호출 흐름에서의 즉시 retry loop는 두지 않는다. 스크립트/조합 레벨의 재시도(retry)와는 다르다.

### 3.6 코어 로직 인라인 원칙

각 벤치마크 소스 파일은 해당 패턴의 zlink API 사용법을 명시적으로 보여주는 샘플 역할을 해야 한다. 상세 규칙은 [PERF_POLICY.md § 8.5](PERF_POLICY.md)를 참조한다.

- **server 바이너리**: 소켓 생성, bind, recv callback/send-ready handler 등록, phase 제어가 각 파일에 인라인으로 존재해야 한다.
- **client 바이너리**: 소켓 생성, connect, monitor-ready gate, phase 진행, send/recv 흐름, routing/backpressure 처리가 각 파일에 인라인으로 존재해야 한다. 설정/출력/cleanup/metric 유틸리티는 공통화할 수 있지만, `run_multi_echo_client_benchmark` / `run_multi_oneway_client_benchmark` 같은 skeleton helper로 핵심 측정 흐름 전체를 위임해서는 안 된다.
- **동일 파일 내 extract method(의미 단위 함수 분리)** 는 허용/권장한다.

예외: STREAM client(`core/perf/common/streamclient/`)는 검증 인프라 코드로
분류하며 공통 모듈화를 허용한다. 단, multi 실행 경로/phase 정책 준수는
`multi` suite에서 보장해야 한다.

---

## 4. 결과 산출물

### 4.1 결과 파일 형식

결과는 `report/`에 사람이 읽을 수 있는 형식으로 저장한다.

```text
## Effective Options (start)
- runs: 1
- patterns: MULTI_DEALER_DEALER
- transports: tcp
- msg_sizes: 64, 256
- recv_mode: recv
- clients: 100
- pin_cpu: off
- warmup_seconds: 2
- duration_seconds: 5

===============================================================================

## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
|----------|------------------|------------|---------------|---------------|---------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |   9.6 MB/s |      0.05 ms  |      0.06 ms  |      0.08 ms  |  35.1  |   64.2   |
| 256B     |   135.00 Kmsg/s  |  34.6 MB/s |      0.05 ms  |      0.07 ms  |      0.09 ms  |  38.5  |   66.8   |
```

- **실행 옵션 헤더 + TABLE**을 저장한다.
- `## Effective Options (start)` / `## Effective Options (result)` 섹션은 실행 시 사용된 옵션을 불릿 목록으로 출력한다. report/ 파일과 stdout 모두에 포함해야 한다.
- `recv_mode` 항목은 필수이며, 실제 실행에 사용된 `--recv` 값(`recv` 또는 `callback`)을 그대로 기록해야 한다.

### 4.2 RESULT line 형식

```text
RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>
```

- RESULT line은 stdout에 출력되며, 스크립트가 이를 파싱하여 테이블을 구성한다.

### 4.3 저장 구조

```text
perf/results/
└── multi/
    └── report/
        ├── perf_linux_recv_20260224_091530.txt
        ├── perf_linux_callback_20260224_143000_release.txt
        └── ...
```

파일명 형식: `perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt`

- **시간대**: `YYYYMMDD_HHMMSS`는 **로컬 시간** 기준이다. 결과 파일은 로컬 머신에만 저장되므로 로컬 시간이 직관적이다.
- **태그 포함 정렬**: 태그는 타임스탬프 뒤에 위치하므로 사전순 정렬 시 동일 시각의 파일 간 순서만 영향받는다. 동일 시각+다른 태그는 태그 사전순으로 정렬된다.
- **저장 단위**: 스크립트(`run_benchmarks_multi.sh` / `.ps1`) 1회 실행 = 1개 결과 파일. 실행에서 측정된 모든 패턴/transport/size 조합의 결과가 하나의 파일에 기록된다.
- 날짜별 하위 디렉터리를 만들지 않는다. 파일명에 날짜/시간이 포함되어 있으므로 `ls -t`로 시간순 확인이 가능하다.
- `<platform>`: `linux`, `windows`, `macos`
- `<recv_mode>`: `recv`, `callback`
- `<tag>`: `--results-tag` 옵션으로 지정 (선택)

| 동작 | 저장 위치 | 저장 형식 | 조건 |
|------|-----------|-----------|------|
| 결과 저장 | `report/` | 실행 옵션 헤더 + TABLE | 항상 저장 (complete/partial 무관) |

### 4.4 결과 저장 흐름

```text
실행 완료
    → results/multi/report/ 에 실행 옵션 헤더 + TABLE 저장 (complete/partial 무관)
```

- 결과는 항상 `report/`에 저장된다.
- `status=partial`인 경우에도 저장한다. 실패한 조합의 결과가 누락된 채로 저장되며, 실패 요약(§ 6.4)이 포함된다.
- **예외**: preflight 검사(nofile/memory)로 **모든 패턴**이 skip된 경우, 결과 파일을 생성하지 않고 `exit 0`으로 종료한다. skip 사유는 콘솔 `## Skips` 섹션에 출력된다.

### 4.5 보존 정책

| 디렉터리 | 최대 파일 수 | 초과 시 처리 |
|-----------|-------------|-------------|
| `report/` | `PERF_RESULTS_MAX_FILES` (기본 100) | 파일명 사전순 기준 오래된 파일 삭제 |

---

## 5. 실행 방법

### 5.1 스크립트 실행

```bash
# core (Linux)
core/perf/run_benchmarks_multi.sh [options]

# core (Windows PowerShell)
core/perf/run_benchmarks_multi.ps1 [options]

# bindings (Linux, 예: python)
perf/multi/run_benchmarks.sh [options]
```

> **정책 준수 실행기**: core는 `run_benchmarks_multi.sh` / `.ps1`, bindings는 `multi/run_benchmarks.sh` / `.ps1`이 multi suite의 유일한 정책 준수 실행기이다 ([PERF_POLICY.md § 3.1](PERF_POLICY.md) 참조). 환경 변수로 실행 스크립트를 우회하는 것은 허용하지 않는다.

#### 실행기 체인

```text
run_benchmarks_multi.sh / .ps1                             # 진입점: 옵션 파싱, 빌드, multi 환경 설정
    → run_benchmarks.sh (PERF_ALLOW_MULTI=1)               # 공통 빌드/실행 래퍼
        → run_comparison.py                                # Python 비교/실행 엔진
            → comp_src_*_server(.exe)                  # server 프로세스
            → comp_src_*_client(.exe)                  # client 프로세스
            → perf_stream_client (STREAM 계열)             # STREAM 공유 client
```

- `run_benchmarks_multi.sh`는 multi 옵션을 정규화한 뒤 `PERF_ALLOW_MULTI=1` 환경으로 `run_benchmarks.sh`를 호출한다.
- `run_benchmarks.sh`는 `PERF_ALLOW_MULTI=1`일 때 root `run_comparison.py`를 Python 엔진으로 사용한다.
- 스크립트는 각 pattern/transport 조합별로 **server → READY 대기 → client** 순서로 두 프로세스를 관리한다.
- server/client 양쪽 바이너리가 RESULT line을 stdout에 출력하고, 스크립트가 이를 합산 수집한다.

### 5.2 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분 가능). `MULTI_` 접두어 생략 가능 | 전체 MULTI_* 패턴 |
| `--build-dir PATH` | 빌드 디렉터리 경로 | 자동 탐색 |
| `--runs N` | 패턴/transport/size 조합당 반복 횟수 | Linux: 1, Windows PS1: 3 |
| `--reuse-build` | 기존 빌드 디렉터리 재사용 (configure/build 생략) | off |
| `--clean-build` | 빌드 디렉터리 삭제 후 클린 configure/build 수행 | off (기본은 증분 빌드) |
| `--pin-cpu` | CPU 고정 (Linux: taskset, Windows: processor affinity) | off |
| `--io-threads N` | 서버/클라이언트 io threads 동시 설정 (레거시 별칭) | — |
| `--server-io-threads N` | 서버 io threads (Linux sh만 지원, Windows PS1은 `--io-threads`로 통합) | non-stream=2, stream=4 |
| `--client-io-threads N` | 클라이언트 io threads (Linux sh만 지원, Windows PS1은 `--io-threads`로 통합) | non-stream=2, stream=4 |
| `--msg-sizes LIST` | 메시지 크기 목록 (쉼표 구분). STREAM 계열은 § 11.2 참조 | `64,256,1024,65536,131072,262144` (STREAM: `64,256,1024,65536`) |
| `--transports LIST` | transport 목록 (쉼표 구분) | `tcp,tls,ws,wss` |
| `--recv MODE` | recv 모델 선택: `recv` (기본) 또는 `callback` | `recv` |
| `--output PATH` | 결과를 파일에 동시 출력 (tee) | stdout만 |
| `--results-dir PATH` | 결과 저장 루트 디렉터리 override (`PATH/multi/` 하위 사용) | `perf/results` |
| `--results-tag NAME` | 결과 파일명에 태그 추가 | 없음 |
| `--warmup N` | warmup 시간(초) | 2 |
| `--duration N` | 측정 시간(초) | 5 |
| `--clients N` | 클라이언트 소켓 수 | 100 (stream=10000) |
| `--hwm N` | 소켓 HWM | 100 (stream=10) |
| `--send-hwm N` | 소켓 송신 HWM | `--hwm` fallback |
| `--recv-hwm N` | 소켓 수신 HWM | `--hwm` fallback |
| `--sndtimeo N` / `--send-timeout-ms N` | 송신 타임아웃(ms) | 200 |
| `--rcvtimeo N` / `--recv-timeout-ms N` | 수신 타임아웃(ms) | 200 |
| `--connect-concurrency N` | 동시 연결 수 | auto (clients≥10000: 1024, 기타: 128) |
| `--transport-transition-ms N` | transport 전환 cooldown(ms) | 3000 |
| `--pattern-transition-ms N` | pattern 전환 cooldown(ms) | 3000 |
| `--server-ready-timeout-ms N` | server READY 대기 타임아웃(ms) | 10000 |
| `--connect-ready-timeout-ms N` | 연결 준비 대기 타임아웃(ms) | 5000 |
| `--monitor-hwm N` | 모니터 소켓 HWM | 1000 |
| `--server-shutdown-timeout-ms N` | server 종료 대기 타임아웃(ms) | 5000 |
| `--server-bind-port N` | server 바인드 포트 (0=자동 할당) | 0 |

#### 옵션 관계

- `--output`: 임의 경로에 stdout을 tee. 저장 구조와 무관.
- 결과는 항상 `results/multi/report/`에 저장된다.

#### 빌드 모드 동작

| 항목 | 기본 (증분 빌드) | `--reuse-build` | `--clean-build` |
|------|------------------|-----------------|-----------------|
| 빌드 디렉터리 삭제 | 생략 | 생략 | 실행 |
| CMake configure | 실행 | 생략 | 실행 |
| CMake build | 실행 | 생략 | 실행 |
| 빌드 디렉터리 미존재 시 | 생성 후 진행 | 에러 후 중단 | 생성 후 진행 |

- 기본 모드는 증분 빌드다.
- `--reuse-build`는 이미 빌드된 결과를 그대로 재사용할 때 지정한다.
- `--clean-build`는 빌드 디렉터리를 초기화한 뒤 완전 재빌드가 필요할 때 지정한다.

### 5.3 실행 예시

```bash
# 전체 멀티 패턴 실행 (stdout만)
core/perf/run_benchmarks_multi.sh

# 특정 패턴만 실행
core/perf/run_benchmarks_multi.sh --pattern MULTI_STREAM

# 여러 패턴
core/perf/run_benchmarks_multi.sh --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB

# callback 모델로 실행 가능한 multi 패턴
core/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback

# 클라이언트 수/메시지 크기 제한
core/perf/run_benchmarks_multi.sh --clients 1000 --msg-sizes 64,1024

# 태그 추가
core/perf/run_benchmarks_multi.sh --results-tag debug1

# 5회 반복, CPU 고정
core/perf/run_benchmarks_multi.sh --runs 5 --pin-cpu

# 측정 시간 조정
core/perf/run_benchmarks_multi.sh --warmup 5 --duration 10
```

### 5.4 바이너리 직접 실행

개별 벤치마크 바이너리를 직접 실행할 수 있다. server/client를 별도 프로세스로 실행해야 한다.

```bash
# server 먼저 실행 (bind 후 READY,<endpoint> 출력)
<server_binary> <lib_name> <transport>

# client 실행 (server의 READY endpoint를 전달)
<client_binary> <lib_name> <transport> <size> --endpoint <endpoint>
```

```bash
# 예시: MULTI_DEALER_DEALER
# 터미널 1 (server)
./core/build/linux-x64/bin/comp_src_dealer_dealer_server current tcp
# stdout: READY,tcp://0.0.0.0:15557

# 터미널 2 (client)
./core/build/linux-x64/bin/comp_src_dealer_dealer_client current tcp 1024 --endpoint tcp://127.0.0.1:15557

# 예시: MULTI_STREAM (recv mode)
./core/build/linux-x64/bin/comp_src_stream_server current tcp
./core/build/linux-x64/bin/perf_stream_client current tcp 1024 --endpoint tcp://127.0.0.1:15557

# 예시: MULTI_STREAM (callback mode)
./core/build/linux-x64/bin/comp_src_stream_server current tcp --recv callback
./core/build/linux-x64/bin/perf_stream_client current tcp 1024 --endpoint tcp://127.0.0.1:15557
```

| 인자 | 대상 | 설명 |
|------|------|------|
| `lib_name` | server/client | 라이브러리 식별자 (`current`) |
| `transport` | server/client | `tcp`, `tls`, `ws`, `wss`, `ipc` (패턴별, §11.3 참조) |
| `size` | client만 | 메시지 크기(bytes) |
| `--endpoint` | client만 | server가 READY로 출력한 endpoint 주소 |

---

## 6. 출력 형식

### 6.1 바이너리 RESULT line

각 바이너리는 `pattern/transport/size` 조합마다 아래 RESULT line을 stdout에 출력한다.

```text
# client 프로세스가 출력 (throughput, bandwidth, latency, client 리소스)
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,throughput,150000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,bandwidth,153.60
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency,45.23
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency_p95,61.40
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency_p99,79.85
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,client_cpu_pct,48.20
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,client_mem_mb,128.40

# server 프로세스가 출력 (server 리소스)
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,server_cpu_pct,35.10
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,server_mem_mb,64.20
```

| 필드 | 설명 |
|------|------|
| `lib` | 라이브러리 식별자 (`current`) |
| `pattern` | `MULTI_DEALER_DEALER`, `MULTI_STREAM` 등 |
| `transport` | `tcp`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput`, `bandwidth`, `latency`, `latency_p95`, `latency_p99`, `client_cpu_pct`, `client_mem_mb`, `server_cpu_pct`, `server_mem_mb`, `server_snd_pending_max`, `server_rcv_pending_max`, `server_rcv_pending_end` |
| `value` | 수치 값 (소수점 2자리) |

| metric | 출력 프로세스 | 설명 | 필수 |
|--------|-------------|------|------|
| `throughput` | client | echo 패턴: 왕복 완료 수 (`ops/s`), one-way 패턴: 단방향 수신 수 (`msg/s`) — 섹션 8.1 참조 | MUST |
| `bandwidth` | client | 네트워크 전송량 (MB/s) — 섹션 8.3 참조 | MUST |
| `latency` | client | 레이턴시 (us) | MUST |
| `latency_p95` | client | 95th percentile 레이턴시 (us) | MUST |
| `latency_p99` | client | 99th percentile 레이턴시 (us) | MUST |
| `client_cpu_pct` | client | client 프로세스 CPU 사용률 (%) | Linux/Windows |
| `client_mem_mb` | client | client 프로세스 메모리 (MB, RSS/WorkingSet 기준) | Linux/Windows |
| `server_cpu_pct` | server | server 프로세스 CPU 사용률 (%) | Linux/Windows |
| `server_mem_mb` | server | server 프로세스 메모리 (MB, RSS/WorkingSet 기준) | Linux/Windows |
| `server_snd_pending_max` | server | server 송신 큐 최대 대기 수 | informational |
| `server_rcv_pending_max` | server | server 수신 큐 최대 대기 수 | informational |
| `server_rcv_pending_end` | server | server 수신 큐 종료 시점 대기 수 | informational |

- 리소스 메트릭은 server/client 프로세스별로 **독립 측정**한다.
- `client_cpu_pct`, `client_mem_mb`는 client 프로세스가 자체 PID를 대상으로 측정하여 출력한다.
- `server_cpu_pct`, `server_mem_mb`는 server 프로세스가 자체 PID를 대상으로 측정하여 출력한다.
- 리소스 메트릭이 누락되어도 완료 판정(`expected`/`actual`)에 영향을 주지 않는다.

### 6.2 스크립트 결과 테이블

> **구현 필수**: 스크립트는 RESULT line 파싱 외에 아래 형식의 사람이 읽을 수 있는 테이블을 **반드시 stdout에 출력하고, 결과 파일에도 기록**해야 한다. RESULT line만 출력하고 테이블을 생략하면 안 된다.

`run_benchmarks_multi.sh` 실행 시 패턴/transport별로 markdown table이 stdout에 출력되고, 결과 파일(`report/`)에도 실행 옵션 헤더 + TABLE로 기록된다.

```text
## PATTERN: MULTI_DEALER_DEALER (one-way)

### Transport: tcp
| Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
|----------|------------------|------------|---------------|---------------|---------------|--------|----------|
| 64B      |   150.00 Kmsg/s  |   9.6 MB/s |      0.05 ms  |      0.06 ms  |      0.08 ms  |  35.1  |   64.2   |
| 1024B    |   120.30 Kmsg/s  | 123.2 MB/s |      0.05 ms  |      0.07 ms  |      0.09 ms  |  38.5  |   66.8   |
| 65536B   |    35.50 Kmsg/s  |2326.5 MB/s |      0.18 ms  |      0.25 ms  |      0.31 ms  |  42.0  |   72.1   |


===============================================================================

## PATTERN: MULTI_STREAM (echo)

### Transport: tcp
| Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
|----------|------------------|------------|---------------|---------------|---------------|--------|----------|
| 64B      |   320.00 Kops/s  |  41.0 MB/s |      0.03 ms  |      0.05 ms  |      0.06 ms  |  30.8  |   58.4   |
| 1024B    |   280.50 Kops/s  | 574.5 MB/s |      0.04 ms  |      0.05 ms  |      0.07 ms  |  33.2  |   60.1   |
```

- **패턴 간 구분선**: 패턴이 바뀔 때 `===============================================================================` 구분선을 출력한다 (첫 번째 패턴 앞에는 출력하지 않음).
- throughput 단위: echo 패턴 `Kops/s` (ops/sec / 1000), one-way 패턴 `Kmsg/s` (msg/sec / 1000) — 섹션 8.1 참조
- bandwidth 단위: `MB/s` (메가바이트/초) — 섹션 8.3 참조
- latency 단위: `ms` (밀리초, mean/p95/p99) — RESULT line 값(us)을 1000으로 나누어 변환
- S.CPU% / S.Mem MB: server 프로세스 CPU 사용률 / 메모리
- transport 미지원 시: `N/A`
- 수집 실패 시: 리소스 컬럼은 `N/A`

### 6.3 진행 로그

실행 중 **사이즈별 결과 테이블 행을 즉시 출력**하여 진행 상황과 측정 데이터를 동시에 제공한다. 공통 규칙은 [PERF_POLICY.md § 5.2](PERF_POLICY.md)를 참조한다.

#### runs=1 출력 형식

`run N/M:` 및 `median:` 레이블 없이 테이블만 출력한다. 각 행은 client가 해당 size 측정을 완료한 즉시 출력된다.

```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,256B,1024B,65536B,131072B,262144B:
      | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
      |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
      | 64B      |    121.98 Kops/s |    15.61 MB/s |      0.81 ms  |      1.01 ms  |      1.26 ms  |    N/A |      N/A |
      | 256B     |    234.56 Kops/s |    60.05 MB/s |      0.75 ms  |      0.92 ms  |      1.19 ms  |    N/A |      N/A |
      | 1024B    |    ...
      | 65536B   |    ...
      | 131072B  |    ...
      | 262144B  |    ...
    Testing tcp: Done
    [transport cooldown 3000ms]
    Testing tls | 64B,256B,1024B,65536B,131072B,262144B:
      | Size     |       Throughput |    Bandwidth |     Lat.Mean |      Lat.P95 |      Lat.P99 | S.CPU% | S.Mem MB |
      |----------|------------------|--------------|--------------|--------------|--------------|--------|----------|
      | 64B      |    ...
```

#### runs > 1 출력 형식

각 run마다 테이블을 출력하고, 마지막에 `median:` 테이블을 출력한다.

```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,256B,1024B:
      run 1/3:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    121.98 Kops/s |    15.61 MB/s |      0.81 ms  |      1.01 ms  |      1.26 ms  |    N/A |      N/A |
        | 256B     |    ...
        | 1024B    |    ...
      [cooldown 3000ms]
      run 2/3:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
      [cooldown 3000ms]
      run 3/3:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
      median:
        | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) | S.CPU% | S.Mem MB |
        |----------|------------------|--------------|---------------|---------------|---------------|--------|----------|
        | 64B      |    ...
        | 256B     |    ...
        | 1024B    |    ...
    Testing tcp: Done
    [transport cooldown 3000ms]
```

- run 간 `[cooldown Nms]`, transport 간 `[transport cooldown Nms]` 표시
- 실패 발생 시: `(failures=N) Done`
- transport 미지원 시: `unsupported Done`

### 6.4 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- MULTI_STREAM current wss 65536B: timeout
```

### 6.5 결과 파일 저장

사용된 옵션에 따라 결과 파일이 아래 경로에 저장된다. 파일 형식은 섹션 4.1을 참조한다.

| 저장 경로 |
|-----------|
| `perf/results/multi/report/perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt` |

### 6.6 리소스 메트릭 수집

Multi 벤치마크는 server/client 별도 프로세스로 동작하므로, 각 프로세스가 **자체 PID를 대상으로** 리소스를 측정한다.

| 메트릭 | 출력 프로세스 | Linux 수집 소스 | Windows 수집 소스 | 계산 방식 |
|--------|-------------|----------------|-------------------|-----------|
| `client_cpu_pct` | client | `/proc/[pid]/stat` | `GetProcessTimes()` | `(user₂+kernel₂ - user₁-kernel₁) / (elapsed × nproc) × 100` |
| `client_mem_mb` | client | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 측정 종료 시점 1회 읽기, MB 변환 |
| `server_cpu_pct` | server | `/proc/[pid]/stat` | `GetProcessTimes()` | 동일 공식 (server 자체 PID) |
| `server_mem_mb` | server | `/proc/[pid]/status` VmRSS | `GetProcessMemoryInfo()` WorkingSetSize | 동일 (server 자체 PID) |

```text
client 프로세스 내부:
[warmup] -> [settle] -> [active(throughput+latency)]
                          ^                         ^
                      샘플₁ 수집                  샘플₂ 수집 → client_cpu_pct, client_mem_mb

server 프로세스:
[bind] -> [READY] -> [relay 대기] -> ... -> [종료 신호] -> server_cpu_pct, server_mem_mb 출력
                                                           (size 1개 실행 구간 기준)
```

- **client**: active phase 시작/종료 시점에 자체 PID를 대상으로 2회 샘플링하여 `client_cpu_pct`, `client_mem_mb`를 출력한다.
- **server**: 종료 신호 수신 시 bind~종료 구간의 CPU/메모리를 측정하여 `server_cpu_pct`, `server_mem_mb`를 출력한다(size 1개 실행 기준).
- 리소스 메트릭은 정보성(informational)이므로 누락 시 완료 판정에 영향을 주지 않는다.

#### size별 귀속 규칙

스크립트는 size마다 server/client를 별도 실행한다. 리소스 메트릭(`client_cpu_pct`, `client_mem_mb`, `server_cpu_pct`, `server_mem_mb`)은 **size별로 독립 측정**한다.

| 항목 | 규칙 |
|------|------|
| `client_cpu_pct` | 각 size의 active phase 시작/종료 시점에 개별 샘플링 |
| `client_mem_mb` | 각 size의 active phase 종료 시점에 개별 읽기 |
| `server_cpu_pct` | 각 size 케이스의 server 프로세스 생애주기에서 개별 측정 |
| `server_mem_mb` | 각 size 케이스의 server 종료 시점에 개별 읽기 |

- size별 측정값이 아닌 바이너리 1회 실행 전체의 단일 측정값을 복제하는 것은 허용하지 않는다.
- server/client 리소스는 size별 RESULT line에 해당 size 케이스 값으로 귀속되어야 한다.

---

## 7. Test Phase

### 7.0 전체 실행 구조

```text
┌─ pattern loop ──────────────────────────────────────────────────────────────┐
│  ┌─ transport loop ──────────────────────────────────────────────────────┐  │
│  │  ┌─ run loop ──────────────────────────────────────────────────────┐  │  │
│  │  │  [size loop]                                                    │  │  │
│  │  │    [1] spawn server(pattern, transport)                         │  │  │
│  │  │    [2] wait READY,<endpoint>                                    │  │  │
│  │  │    [3] spawn client(pattern, transport, size, endpoint)         │  │  │
│  │  │    [4] client 종료, RESULT line 수집                            │  │  │
│  │  │    [5] server 종료, server RESULT line 수집                     │  │  │
│  │  │  → run_cooldown (3s)                                            │  │  │
│  │  └────────────────────────────────────────────────────────────────┘  │  │
│  │  → transport_transition_cooldown (3s)                                │  │
│  └──────────────────────────────────────────────────────────────────────┘  │
│  → pattern_transition_cooldown (3s)                                        │
└────────────────────────────────────────────────────────────────────────────┘
```

### 7.1 client 프로세스 내부 Phase (size 1개 기준)

```text
[warmup] -> [settle] -> [active(throughput+latency)]
```

> echo는 client가 phase를 제어하며 server는 relay/echo 대기한다. one-way는 sender/receiver가 동일 순서의 phase를 수행한다.

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | time-based | 2s | `PERF_MULTI_WARMUP_SECONDS` |
| settle | time-based | 500ms | `PERF_MULTI_SETTLE_MS` |
| active | time-based | 5s | `PERF_MULTI_DURATION_SECONDS` |

> 참고: phase 레벨 settle(500ms) 외에, 소켓 설정 완료 후 코드 레벨 settle(`SETTLE_TIME_MS = 300ms`, `perf_common.hpp`)이 별도로 존재한다. 이 값은 환경 변수로 변경할 수 없다.

### 7.2 스크립트 레벨 전환 cooldown

| 전환 구간 | 기본값 | 환경 변수 | 설명 |
|-----------|--------|-----------|------|
| run 간 cooldown | 3000ms | `PERF_MULTI_RUN_COOLDOWN_MS` | 동일 transport에서 반복 run 사이 안정화 |
| transport 전환 | 3000ms | `PERF_MULTI_TRANSPORT_TRANSITION_MS` | transport 변경 시 소켓 정리 대기 |
| pattern 전환 | 3000ms | `PERF_MULTI_PATTERN_TRANSITION_MS` | 패턴 변경 시 전체 클라이언트 소켓 정리 대기 |

- 전환 cooldown은 이전 run의 server/client 프로세스 양쪽 종료 후 다음 run의 server 실행 전에 스크립트에서 `sleep`으로 수행한다.
- Multi는 대량의 클라이언트 소켓(1000~10000)을 사용하므로 OS의 TIME_WAIT 소켓 해제를 위해 전환 cooldown이 필수적이다.
- 마지막 transport/pattern 이후에는 전환 cooldown을 수행하지 않는다 (불필요).

### 7.3 Size 전환 정책

- Multi는 run 내부에서 size loop를 수행하며, size마다 server/client를 별도 실행한다.
- size 사이의 drain sleep은 사용하지 않는다.

### 7.4 warmup 모드

| 모드 | 설명 | 환경 변수 |
|------|------|-----------|
| passive (기본) | 송신 비활성 상태에서 시간 대기 | `PERF_MULTI_ACTIVE_WARMUP=0` |
| active | 송신/수신 활성 상태로 워밍업 | `PERF_MULTI_ACTIVE_WARMUP=1` |

active warmup은 active phase 시작 전 추가 송수신 워밍업을 수행하며, 별도 drain 구간은 사용하지 않는다.

---

## 8. Throughput 측정

### 8.1 패턴 방향 분류

각 패턴은 메시지 흐름 방향에 따라 **echo(왕복)** 또는 **one-way(단방향)**으로 분류되며, throughput 단위가 다르다.

| 방향 | 단위 | 의미 | 측정 지점 | 패턴 |
|------|------|------|-----------|------|
| echo | `ops/s` | 왕복 완료 수/초 | client 측 recv | MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_GATEWAY, MULTI_STREAM |
| one-way | `msg/s` | 단방향 수신 수/초 | receiver 측 recv | MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT |

- echo 패턴: client가 send → server echo → client recv. 1 rtt = 2 message hops. client가 echo를 수신한 횟수를 카운트한다.
- one-way 패턴: sender가 송신한 메시지를 receiver가 수신한다(서버 relay 또는 server push 포함). 1 msg = 1 message hop으로 보고, receiver 수신 수를 카운트한다.
- 동일 단위의 패턴 간에만 throughput을 직접 비교할 수 있다.

### 8.2 계산

1. duration 구간의 수신량으로 계산한다.
2. `throughput = recv_count / duration_seconds`
3. warmup/settle 구간의 데이터는 계산에서 제외한다.

### 8.3 Bandwidth (네트워크 전송량)

throughput과 메시지 크기로부터 실제 네트워크 전송량(MB/s)을 계산한다. 패턴 방향에 따라 계산이 다르다.

| 방향 | 계산식 | 의미 |
|------|--------|------|
| echo (`ops/s`) | `throughput × msg_size × 2 / 1,000,000` | 양방향 총 전송량 (send + recv) |
| one-way (`msg/s`) | `throughput × msg_size / 1,000,000` | 단방향 전송량 |

- 단위: `MB/s` (1 MB = 1,000,000 bytes, SI 기준)
- echo 패턴은 send/recv 양방향 데이터가 이동하므로 `×2`를 적용한다.
- bandwidth는 throughput 단위(ops/s vs msg/s)가 다른 패턴 간에도 **실제 데이터 처리량**으로 직접 비교할 수 있는 공통 지표이다.

---

## 9. Latency 측정

latency는 패턴 유형에 따라 측정 방식을 분리한다.

### 9.0 phase 순서

각 size는 아래 순서로 측정한다.

1. echo 패턴: warmup → settle(optional) → active phase
2. one-way 패턴: warmup → settle(optional) → active phase

- echo/one-way 모두 active phase 단일 실행에서 throughput/latency를 동시에 산출한다.

### 9.1 패턴별 divisor 규칙

| 유형 | divisor | 적용 패턴 |
|------|---------|-----------|
| 양방향 RTT | `2` | MULTI_DEALER_ROUTER, MULTI_ROUTER_*, MULTI_GATEWAY, MULTI_STREAM |
| 단방향 | `received_count` | MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT |

### 9.2 계산식

- mean: active phase에서 수집한 샘플의 산술 평균
- p95: 샘플의 95th percentile
- p99: 샘플의 99th percentile

- RTT 샘플(echo): `sample_us = (recv_ts_us - sent_ts_us) / 2`
- 단방향 샘플(one-way): 수신 메시지에 포함된 송신 타임스탬프 기준 `now_us - sent_us`
- warmup/settle 구간의 데이터는 계산에서 제외한다.

### 9.3 one-way latency 집계 규칙

one-way 패턴 latency는 패턴의 실제 receiver 측에서 측정한다.

- `MULTI_DEALER_DEALER`: server(receiver) 기준으로 latency 측정
- `MULTI_PUBSUB`, `MULTI_SPOT`: client(receiver) 기준으로 latency 측정
- active phase 구간에서 수신한 메시지는 **전수 집계**한다(메시지 단위 샘플 누락 금지).
- mean은 `lat_sum / lat_count`로 계산하고, p95/p99는 동일 샘플 집합에서 계산한다.

### 9.4 Header 기반 필터 규칙

- 측정 메시지 payload 선두에는 공통 metric header를 포함한다: `magic`, `run_id`, `phase`, `msg_size`, `seq`, `sent_ts_us`.
- receiver는 header를 decode하여 `phase == active`, `msg_size == expected_size` (필요 시 `run_id` 일치) 조건을 만족하는 샘플만 집계한다.
- ROUTER 계열 multipart 수신은 routing frame이 앞에 올 수 있으므로 capture buffer에서 header magic을 스캔해 payload header를 탐지한다.
- header 불일치(다른 size/phase, stale 메시지)는 수신 드레인만 수행하고 메트릭 집계에서 제외한다.

---

## 10. Metric Tiers

### 10.1 Tier 1: 필수 (RESULT line 출력, 완료 판정 대상)

| 메트릭 | RESULT key | 단위 | 계산 방식 |
|--------|-----------|------|-----------|
| throughput | `throughput` | echo: `ops/s`, one-way: `msg/s` | `recv_count / duration_seconds` — 섹션 8.1 참조 |
| bandwidth | `bandwidth` | MB/s | 섹션 8.3 참조 |
| latency | `latency` | us | 패턴별 divisor 규칙 적용 (섹션 9.1) |
| latency p95 | `latency_p95` | us | 측정 샘플 95th percentile (echo/one-way 모두 active phase) |
| latency p99 | `latency_p99` | us | 측정 샘플 99th percentile (echo/one-way 모두 active phase) |

- Tier 1 메트릭이 누락되면 해당 조합은 fail로 처리한다.
- `expected`/`actual` 완료 판정은 Tier 1 RESULT line만 카운트한다 (조합당 5줄: throughput + bandwidth + latency + latency_p95 + latency_p99).

### 10.2 Tier 2: 권장 (RESULT line 미출력, 향후 확장 예약)

| 메트릭 | 단위 | 설명 |
|--------|------|------|
| `connect_ms` | ms | 전체 클라이언트 연결 완료 시간 |
| `ready_ms` | ms | 연결 후 준비 완료 대기 시간 |

- Tier 2 메트릭은 현재 RESULT line에 출력하지 않는다. 향후 구현 시 RESULT line에 추가할 수 있다.
- 누락 시 완료 판정에 영향 없음.

### 10.3 Tier 3: 정보성 (RESULT line 출력, 완료 판정 제외)

| 메트릭 | RESULT key | 출력 프로세스 | 단위 | 수집 방식 | 플랫폼 |
|--------|-----------|-------------|------|-----------|--------|
| client CPU | `client_cpu_pct` | client | % | Linux: `/proc/[pid]/stat`, Windows: `GetProcessTimes()` | Linux/Windows |
| client 메모리 | `client_mem_mb` | client | MB | Linux: `/proc/[pid]/status` VmRSS, Windows: `GetProcessMemoryInfo()` WorkingSetSize | Linux/Windows |
| server CPU | `server_cpu_pct` | server | % | 동일 (server 자체 PID) | Linux/Windows |
| server 메모리 | `server_mem_mb` | server | MB | 동일 (server 자체 PID) | Linux/Windows |

- server/client 별도 프로세스이므로 각 프로세스가 자체 PID를 대상으로 리소스를 측정한다.
- server/client 리소스는 모두 size별 독립 측정 (섹션 6.6 참조).
- **client 리소스 메트릭(`client_cpu_pct`, `client_mem_mb`)은 바이너리가 RESULT line으로 출력하지만, 최종 집계/결과 테이블에는 반영되지 않는다.** 결과 테이블에는 server 리소스(`S.CPU%`, `S.Mem MB`)만 표시된다.
- 누락 시 완료 판정에 영향 없음.
- 수집 실패 시 테이블에 `N/A`로 표시.

---

## 11. Pattern & Transport Matrix

### 11.1 지원 패턴

MULTI_DEALER_DEALER, MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_PUBSUB, MULTI_GATEWAY, MULTI_SPOT, MULTI_STREAM

#### 바인딩 소스 파일 명명 규칙

모든 벤치마크 소스 파일은 **`perf_`** 접두어를 사용한다. multi는 server/client 역할 분리를 필수로 하며, 소스 파일 분리는 권장이다. 소스 위치는 [PERF_POLICY.md § 2.0.2](PERF_POLICY.md)를 참조한다.

| 언어 | server 파일 | client 파일 | 예시 |
|------|-----------|-----------|------|
| Core (C++) | `perf_multi_<pattern>_server.cpp` | `perf_multi_<pattern>_client.cpp` | `perf_multi_stream_server.cpp` |
| C++ binding | `perf_multi_<pattern>_server.cpp` 또는 `perf_main.cpp --multi-server` | `perf_multi_<pattern>_client.cpp` 또는 `perf_main.cpp --multi-client` | `perf_multi_stream_server.cpp` |
| .NET | `PerfMulti<Pattern>Server.cs` 또는 `PerfMain --multi-server` | `PerfMulti<Pattern>Client.cs` 또는 `PerfMain --multi-client` | `PerfMultiStreamServer.cs` |
| Java | `PerfMulti<Pattern>Server.java` 또는 `PerfMain --multi-server` | `PerfMulti<Pattern>Client.java` 또는 `PerfMain --multi-client` | `PerfMultiStreamServer.java` |
| Node | `perf_multi_<pattern>_server.js` | `perf_multi_<pattern>_client.js` | `perf_multi_stream_server.js` |
| Python | `perf_multi_<pattern>_server.py` | `perf_multi_<pattern>_client.py` | `perf_multi_stream_server.py` |

- STREAM 계열은 public pattern 이름을 `stream` 하나만 사용하고, recv mode는
  `--recv`로만 선택한다
- 공통 유틸리티 파일도 `perf_` 접두어: `perf_common.hpp`, `PerfCommon.cs`, `PerfUtil.java`, `perf_common.py` 등
- 실행 스크립트: bindings는 `multi/run_benchmarks.sh` / `.ps1`, core는 `run_benchmarks_multi.sh` / `.ps1` ([PERF_POLICY.md § 3.1](PERF_POLICY.md) 참조)
- 파일 분리 대신 단일 runner를 사용하는 경우에도 실행 시점에서는 반드시 server/client 별도 프로세스로 동작해야 하며 READY/RESULT 프로토콜은 동일하게 준수한다.

#### 패턴별 소스 파일 / 바이너리 매핑 (Core)

server/client 분리 패턴은 **별도 소스 파일 / 별도 바이너리**로 작성하는 것을 원칙으로 한다. 기본 소스 경로: `perf/multi/src/`

| 패턴 | server 소스 | server 바이너리 | client 소스 | client 바이너리 |
|------|------------|----------------|------------|----------------|
| MULTI_DEALER_DEALER | `*_dealer_dealer_server.cpp` | `comp_src_dealer_dealer_server` | `*_dealer_dealer_client.cpp` | `comp_src_dealer_dealer_client` |
| MULTI_DEALER_ROUTER | `*_dealer_router_server.cpp` | `comp_src_dealer_router_server` | `*_dealer_router_client.cpp` | `comp_src_dealer_router_client` |
| MULTI_ROUTER_ROUTER | `*_router_router_server.cpp` | `comp_src_router_router_server` | `*_router_router_client.cpp` | `comp_src_router_router_client` |
| MULTI_PUBSUB | `*_pubsub_server.cpp` | `comp_src_pubsub_server` | `*_pubsub_client.cpp` | `comp_src_pubsub_client` |
| MULTI_GATEWAY | `*_gateway_server.cpp` | `comp_src_gateway_server` | `*_gateway_client.cpp` | `comp_src_gateway_client` |
| MULTI_SPOT | `*_spot_server.cpp` | `comp_src_spot_server` | `*_spot_client.cpp` | `comp_src_spot_client` |
| MULTI_STREAM | `*_stream_server.cpp` | `comp_src_stream_server` | `perf/common/streamclient/perf_stream_client.cpp` (shared) | `perf_stream_client` (shared) |

> 위 표의 `*`는 `perf_multi`를 축약한 것이다 (예: `*_stream_server.cpp` = `perf_multi_stream_server.cpp`).
> STREAM client 예외(core): `MULTI_STREAM` client는 [PERF_POLICY.md § 8.5](PERF_POLICY.md)의 STREAM client 예외에 따라 `perf/common/streamclient/` 공용 구현을 사용한다. server는 pattern 이름과 별개로 recv mode별 내부 entrypoint를 가질 수 있지만, public pattern은 `MULTI_STREAM` 하나만 유지한다.

#### MULTI_STREAM 계열 패턴

> **STREAM 소켓은 multi suite에서만 테스트한다.** single suite에서는 STREAM 테스트를 수행하지 않는다. STREAM의 성능 특성은 대량 동시 연결 환경(multi)에서 평가하는 것이 의미 있으므로, 모든 STREAM 벤치마크는 multi suite에 집중한다.

| 패턴 | `--recv` | server 수신 방식 | 설명 |
|------|----------|-----------------|------|
| MULTI_STREAM | `recv` | recv loop | 기존 소켓 recv API로 수신 |
| MULTI_STREAM | `callback` | callback dispatch | stream dispatch callback API로 수신 |

- STREAM 계열은 동일한 transport, size, clients 설정을 공유하며, recv/callback
  두 mode를 모두 지원해야 한다.
- **Wire protocol**: client는 `[4B length (big-endian)][payload]` (len32be framing)으로 통일한다. server 수신 방식만 패턴별로 다르다. 상세는 [PERF_POLICY.md § 2.0.3 Wire Protocol](PERF_POLICY.md)을 참조한다.
- 수신 방식만 다르므로 throughput/latency 차이를 직접 비교할 수 있다.
- `MULTI_STREAM_LEN32BE`는 삭제되었다. 문서, 스크립트, 빌드 설정, 코드에 잔존 구현이 있으면 모두 삭제해야 하며, 삭제된 패턴을 alias/legacy path로 유지하지 않는다.
- MULTI_STREAM의 server 프로세스는 recv/callback mode와 무관하게 반드시 zlink
  STREAM 소켓으로 `bind`해야 하며, DEALER/ROUTER/PUBSUB 등 non-STREAM 소켓으로
  대체할 수 없다.
- client 프로세스는 raw transport(`tcp`,`tls`,`ws`,`wss`)로 `connect`해야 하며, zlink STREAM 소켓의 client `connect()` 경로를 사용하지 않는다.
- 각 size 측정에서 `connect_ok`는 `target clients`와 동일해야 한다(100%). 하나라도 미달하면 해당 조합은 `fail`이다.
- 위 모델을 위반한 구현은 정책 위반이므로 해당 코드를 삭제하고 정책 모델로 다시 구현해야 한다.
- 위반 구현에서 나온 실행 결과는 정책 산출물로 인정하지 않는다.

### 11.2 표준 메시지 크기

| 패턴군 | 크기 |
|--------|------|
| MULTI_DEALER / MULTI_ROUTER / MULTI_PUBSUB | `[64, 256, 1024, 65536, 131072, 262144]` |
| MULTI_STREAM | `[64, 256, 1024, 65536]` |
| MULTI_GATEWAY / MULTI_SPOT | `[64, 256, 1024, 65536, 131072, 262144]` |

- STREAM 계열은 대량 동시 연결 환경에서 테스트하므로 65536B까지만 측정한다.

### 11.3 transport

| 패턴군 | transport |
|--------|-----------|
| MULTI_DEALER_DEALER, MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_PUBSUB | tcp, tls, ws, wss (Python 엔진 기본값에 ipc 포함, 단 shell wrapper 기본값은 tcp,tls,ws,wss; Windows: ipc 제외) |
| MULTI_GATEWAY, MULTI_SPOT | tcp, tls, ws, wss |
| MULTI_STREAM | tcp, tls, ws, wss |

---

## 12. Environment Variables

### 12.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 0 |
| `PERF_MSG_SIZES` | 테스트 size 목록 (러너가 size별 케이스로 분할 실행) | `64,256,1024,65536,131072,262144` |
| `PERF_TRANSPORTS` | 테스트 transport 목록 | 패턴별 기본값 (§11.3 참조) |
| `PERF_TASKSET` | CPU pinning (`1`로 활성화, Linux: taskset, Windows: processor affinity) | 0 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`로 활성화) | 0 |
| `PERF_DISABLE_RESOURCE_METRICS` | 리소스 메트릭(CPU/메모리) 수집 비활성화 (`1`로 활성화) | 0 |
| `PERF_CAPTURE_MAX_BYTES` | 프로세스 stdout 캡처 최대 바이트 | 4194304 (4MB) |

### 12.2 Phase 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_WARMUP_SECONDS` | warmup 시간(초) | 2 |
| `PERF_MULTI_DURATION_SECONDS` | 측정 시간(초) | 5 |
| `PERF_MULTI_SETTLE_MS` | settle 대기(ms) | 500 |
| `PERF_MULTI_TRANSPORT_TRANSITION_MS` | transport 전환 cooldown(ms) | 3000 |
| `PERF_MULTI_PATTERN_TRANSITION_MS` | pattern 전환 cooldown(ms) | 3000 |
| `PERF_MULTI_ACTIVE_WARMUP` | active warmup 활성화 | 0 |

### 12.3 클라이언트 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_CLIENTS` | 클라이언트 소켓 수 | 100 (stream=10000) |
| `PERF_MULTI_STREAM_MSG_SIZES` | STREAM 계열 전용 size 목록 (러너가 size별 케이스로 분할 실행). 미설정 시 `PERF_MSG_SIZES`가 설정되어 있으면 그 값을 사용하고, 둘 다 미설정이면 기본값 사용 | `64,256,1024,65536` |
| `PERF_MULTI_HWM` | 소켓 HWM | 100 (stream=10) |
| `PERF_MULTI_SNDHWM` | 소켓 송신 HWM | `PERF_MULTI_HWM` |
| `PERF_MULTI_RCVHWM` | 소켓 수신 HWM | `PERF_MULTI_HWM` |
| `PERF_MULTI_CONNECT_CONCURRENCY` | 동시 연결 수 | auto (clients≥10000: 1024, 기타: 128) |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | 연결 준비 타임아웃(ms) | 5000 |
| `PERF_MULTI_SERVICE_CLIENTS` | 서비스 클라이언트 수 상한 (0=제한 없음) | 0 |
| `PERF_MULTI_LATENCY_SAMPLE_CAP` | 레이턴시 샘플 최대 수 | 200000 |

### 12.4 송수신 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_SNDTIMEO_MS` | 송신 타임아웃(ms) | 200 |
| `PERF_MULTI_RCVTIMEO_MS` | 수신 타임아웃(ms) | 200 |
| `PERF_MULTI_MONITOR_HWM` | 모니터 소켓 HWM | 1000 |
| `PERF_MULTI_PUBSUB_XPUB_NODROP` | PUBSUB 서버의 `ZLINK_XPUB_NODROP` 기본값 | 1 |
| `PERF_MULTI_SPOT_XPUB_NODROP` | SPOT 서버의 `ZLINK_XPUB_NODROP` 기본값 | 1 |

- `PERF_MULTI_CLIENT_POLL_TIMEOUT_MS`, `PERF_MULTI_CLIENT_IDLE_SLEEP_US`, `PERF_MULTI_SEND_BACKOFF_US`, `PERF_MULTI_BLOCKING_SEND`는 삭제됐다. callback 모델에서는 poller를 사용하지 않고, recv 모델에서는 poller를 사용하지만 이 변수들은 필요하지 않다.
- `PERF_MULTI_RECV_BATCH`, `PERF_MULTI_SEND_WORKERS`, `PERF_SERVER_RECV_THREADS`는 삭제됐다. callback 모델에서는 콜백으로 자동 처리되며, recv 모델에서도 단일 recv 루프를 사용한다.

### 12.5 프로세스 조정

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MULTI_TIMEOUT_SECONDS` | client 실행 timeout(초) | auto (`duration`/`size` 기반) |
| `PERF_MULTI_SERVER_READY_TIMEOUT_MS` | server READY 대기 타임아웃(ms) | 10000 |
| `PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS` | server 종료 대기 타임아웃(ms) | 5000 |
| `PERF_MULTI_SERVER_BIND_PORT` | server bind 포트 (0=자동 할당) | 0 |

- server READY 타임아웃 초과 시 해당 run을 실패 처리하고 server 프로세스를 강제 종료한다.
- server 종료 시퀀스: stdin `STOP\n` 송신 → shutdown timeout 대기 → `terminate()` (SIGTERM) → 2차 timeout 대기 → `kill()` (SIGKILL). 각 단계에서 프로세스가 종료되면 이후 단계를 건너뛴다.
- `PERF_MULTI_SERVER_BIND_PORT=0`이면 OS가 사용 가능한 포트를 자동 할당한다. server는 실제 bind된 포트를 `READY,<endpoint>`에 포함하여 출력한다.

### 12.6 기타

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_MAX_SOCKETS` | context max sockets | auto |
| `PERF_MULTI_RUN_COOLDOWN_MS` | run 간 cooldown(ms) | 3000 |
| `PERF_MULTI_SERVER_IO_THREADS` | 서버 I/O threads (non-stream) | 2 |
| `PERF_MULTI_CLIENT_IO_THREADS` | 클라이언트 I/O threads (non-stream) | 2 |
| `PERF_MULTI_STREAM_SERVER_IO_THREADS` | 서버 I/O threads (stream) | 4 |
| `PERF_MULTI_STREAM_CLIENT_IO_THREADS` | 클라이언트 I/O threads (stream) | 4 |
| `PERF_MULTI_DEFAULT_IO_THREADS` | I/O threads 공통 기본값 | 2 |
| `PERF_SKIP_NOFILE_CHECK` | nofile limit 검사 생략 | 0 |
| `PERF_SKIP_MEMORY_CHECK` | 메모리 가드 검사 생략 | 0 |
| `PERF_MULTI_MEMORY_BUDGET_PCT` | MemAvailable 대비 예산 비율(%) | 70 |
| `PERF_MULTI_MEMORY_BASE_MB` | 기본 메모리 예약(MB) | 512 |
| `PERF_MULTI_MEMORY_PER_CLIENT_KB` | 클라이언트당 예상 메모리(KB) | 1024 |
| `PERF_RESULTS_MAX_FILES` | report/ 디렉터리 최대 파일 수 | 100 |

> **삭제된 환경 변수**: `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS` 및 레거시 `PERF_MULTI_ATTEMPTS`, `PERF_MULTI_STREAM_ATTEMPTS`는 삭제 대상이다. 구현에 존재하면 제거해야 한다. Retry 금지 정책은 [PERF_POLICY.md § 8](PERF_POLICY.md)을 참조한다.

---

## 13. 구현 제약

### 13.0 Public C API 전용

- bench 코드는 `doc/guide` 및 `doc/api` 문서에 기술된 public C API만
  사용한다. 내부 헤더나 내부 함수를 직접 호출하지 않는다.
- public C API 동작에 문제가 있으면 bench 코드에서 우회하지 않고 버그로
  레포팅한다. core 수정 후 bench 작업을 계속한다.

### 13.1 측정 경로(hot path) lock 사용 금지

벤치마크 바이너리의 **측정 구간(active phase: throughput/latency)에서 실행되는 hot path**에 `std::mutex`, `std::condition_variable` 등 blocking synchronization primitive를 사용하지 않는다.

| 구분 | 허용 | 금지 |
|------|------|------|
| hot path (send/recv/callback 루프) | `std::atomic`, lock-free queue, SPSC ring buffer | `std::mutex`, `std::condition_variable`, `std::shared_mutex` |
| cold path (setup/teardown/결과 출력) | 제한 없음 | — |

- **이유**: lock contention이 throughput/latency 측정값에 포함되어 벤치마크 대상(라이브러리 성능)이 아닌 동기화 오버헤드를 측정하게 된다.
- **callback recv 모델**: callback 지원 패턴(`MULTI_SPOT`, `MULTI_STREAM`)에서만
  recv는 I/O thread의 콜백으로 처리된다. recv callback은 metric header decode,
  timestamp 취득, protocol상 즉시 필요한 send, bounded queue enqueue까지만
  수행한다. throughput/latency 집계와 phase 판정은 callback 밖
  worker/app thread가 맡는다.
- **queue payload 규칙**: queue에는 작은 POD metric event만 넣는다. `zlink_msg_t`, payload pointer, multipart parts 포인터, 소유권 있는 버퍼를 callback 밖으로 넘기지 않는다.
- **callback/send-ready 직렬화**: 동일 소켓의 콜백은 직렬화되므로 same-socket pending deque나 send_pending 플래그는 lock 없이 유지할 수 있다. 다만 metric worker와의 handoff는 별도 실행 컨텍스트이므로 bounded SPSC 또는 lock-free queue를 사용한다.
- **app thread와 I/O thread(callback) 간 동기화**: callback hot path와 active phase worker 집계 경로에서는 `std::atomic`과 bounded lock-free/SPSC queue만 사용한다. blocking lock은 사용하지 않는다. 결과 출력과 최종 정리만 cold path로 분리한다.

### 13.2 불필요한 메모리 할당/복사 금지

벤치마크는 **라이브러리 자체의 성능만 온전히 측정**해야 한다. 측정 구간(active phase: throughput/latency)에서 벤치마크 코드가 유발하는 불필요한 메모리 할당·복사는 측정 결과를 왜곡하므로 금지한다.

| 구분 | 권장 | 금지 |
|------|------|------|
| 송신 버퍼 | warmup 전 사전 할당, duration 내 재사용 | 매 send마다 `std::vector` 생성/resize |
| 수신 버퍼 | 고정 크기 버퍼 또는 pool | 매 recv마다 동적 할당 |
| 수신 데이터 | 필요한 metric header만 추출 후 경량 event로 전달 | 수신 payload 전체를 별도 컨테이너에 복사 |
| dispatch callback | timestamp/phase 추출 후 bounded queue에 POD event enqueue | `zlink_msg_t` handle, payload pointer, 대형 버퍼를 queue에 push |
| routing_id | 필요 시 고정 버퍼에 1회 저장 | 매 메시지마다 `std::vector<unsigned char>` 할당 |
| 카운터/통계 | `std::atomic<int64_t>`, bounded SPSC queue의 경량 event | active 구간마다 heap 할당이 필요한 동적 컨테이너 push |

- **원칙**: active phase(throughput/latency)에서 벤치마크 인프라 코드는 동적
  메모리 할당 없이 미리 준비된 버퍼와 bounded queue를 재사용해야 한다. 측정
  결과에 라이브러리 외 오버헤드가 포함되면 같은 pattern의 recv/callback mode
  비교(예: `MULTI_STREAM --recv recv` vs `MULTI_STREAM --recv callback`)가
  공정하지 않다.
- warmup phase 이전(setup/connect)과 active 이후(결과 출력/정리)에서는 할당/복사에 제한이 없다.
- `zlink_msg_data()` 반환 포인터를 직접 참조하여 불필요한 복사를 피한다. 내용 검증이 필요 없는 throughput 측정에서는 payload를 읽지 않는다.
- Multi의 대량 클라이언트(1000~10000) 환경에서는 per-client 버퍼도 setup 시 사전 할당하고, duration 내에서 재사용한다.

### 13.3 연결 준비 확인: MONITOR delivery-ready event 전용

client 프로세스가 server에 대한 benchmark start gate를 확인할 때는 반드시
**MONITOR delivery-ready event**를 사용한다. Sleep, handshake barrier
(첫 메시지 전송 성공 대기), monitor snapshot polling 등 우회적 방법을 연결
확인 수단으로 사용하지 않는다.

| 항목 | 규칙 |
|------|------|
| 연결 확인 API | `zlink_socket_monitor_open(..., handler)` — monitor callback으로 이벤트 수신 |
| 감시 이벤트 | 패턴별 delivery-ready event를 사용하며, 기준 표는 [`../guide/06-monitoring.ko.md`](../guide/06-monitoring.ko.md) §11을 따른다 |
| 대기 방식 | monitor callback에서 atomic counter 증가 + app thread에서 타임아웃 기반 대기 — busy-wait/sleep 금지 |
| 타임아웃 | `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` (기본 5000ms) 초과 시 run 실패 처리 |
| Monitor HWM | `PERF_MULTI_MONITOR_HWM` (기본 1,000) — monitor event queue 상한 |

- **이유**: perf start gate는 실제 delivery 가능 시점만 써야 한다. Sleep은 환경에 따라 불충분하거나 과다하고, handshake barrier와 snapshot polling은 측정 인프라 오버헤드를 늘리거나 잘못된 ready 판정을 만들 수 있다.
- 대기 함수 구현 시 monitor callback에서 `atomic_fetch_add`로 connection ready 카운트를 증가시키고, app thread에서 `wait_connect_ready_count(expected_count, timeout_ms)` 형태로 atomic 카운터가 목표에 도달할 때까지 대기한다.
- `CONNECTED`, `ACCEPTED`, `LISTENING`은 progress/debug 용도로만 사용한다. perf 시작 gate로 승격하지 않는다.
- server 측에서도 동일하게 guide §11에 정의된 delivery-ready event를 기준으로 준비를 판정한다.

---

## Appendix: 계산 레퍼런스

```python
import statistics

def aggregate_runs(values):
    """runs > 1인 경우 대표값 산출"""
    if not values:
        return 0.0
    return statistics.median(values)

def throughput_per_sec(recv_count, duration_seconds):
    return recv_count / max(1, duration_seconds)

def bandwidth_mbps(throughput, msg_size, is_echo):
    """echo 패턴: 양방향(×2), one-way 패턴: 단방향"""
    multiplier = 2 if is_echo else 1
    return throughput * msg_size * multiplier / 1_000_000

def latency_rtt_us(elapsed_us, roundtrip_count):
    """MULTI_DEALER_ROUTER, MULTI_ROUTER_*, MULTI_GATEWAY, MULTI_STREAM"""
    return elapsed_us / max(1, roundtrip_count * 2)

def latency_oneway_us(elapsed_us, count):
    """MULTI_DEALER_DEALER, MULTI_PUBSUB, MULTI_SPOT: count=received_count"""
    return elapsed_us / max(1, count)
```
