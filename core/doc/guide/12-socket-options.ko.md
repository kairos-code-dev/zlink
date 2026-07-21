[English](12-socket-options.md) | [한국어](12-socket-options.ko.md)

<!-- zlink-nav:start -->
[← 스레드 안전성](11-thread-safety.ko.md)
<!-- zlink-nav:end -->

# 소켓 옵션 상세 가이드

이 문서는 `zlink_set_option()` / `zlink_get_option()`으로 설정하는 소켓 옵션
각각의 **동작**, **영향 범위**, **기본값**, **소켓 타입별 차이**를 상세히 설명한다.
API 시그니처만 다루는 [socket API 레퍼런스](../spec/core/socket/README.ko.md)와 달리,
이 가이드는 **옵션이 런타임에 무엇을 바꾸는지**에 초점을 둔다.

## 옵션 소유권 카테고리

내부적으로 옵션은 세 카테고리로 분류된다.

| 카테고리 | 설명 | 대표 옵션 |
|----------|------|-----------|
| **Core Socket** | 소켓 핵심 동작 | SNDHWM, RCVHWM, LINGER, SNDTIMEO, RCVTIMEO |
| **Transport/Network** | 네트워크/transport 정책 | SNDBUF, RCVBUF, TCP_*, TOS, CONNECT_TIMEOUT |
| **Protocol/Metadata** | 프로토콜 수준 메타데이터 | ZMP_METADATA |

---

## 피어 라우팅 ID 중복 정책

`ZLINK_OPT_RID_DUPLICATE_POLICY`는 같은 소켓에 동일한 피어 라우팅 ID가
다시 들어왔을 때 중복 연결을 거부할지, 하나의 연결로 수렴시킬지를 결정한다.

| 값 | 동작 |
|----|------|
| `ZLINK_RID_DUPLICATE_REJECT` | 기본값. 기존 연결을 유지하고 중복 연결은 등록하지 않는다. |
| `ZLINK_RID_DUPLICATE_HANDOVER` | 같은 방향에서 다시 연결하면 새 연결이 기존 연결을 인수한다. 양쪽이 서로 연결한 반대 방향 연결이 충돌하면 두 피어의 라우팅 ID를 비교해 양쪽이 같은 방향 하나를 선택한다. |

이 옵션은 `zlink_set_option()`으로 설정하는 공통 소켓 옵션이다.
중복 피어 ID 인수 여부를 바꾸는 공개 설정은 이 옵션 하나뿐이다.

STREAM은 서버가 연결별 4바이트 라우팅 ID를 직접 부여하므로 이 중복 정책의
대상이 아니다.

```c
int policy = ZLINK_RID_DUPLICATE_HANDOVER;
zlink_set_option(router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                 &policy, sizeof(policy));
```

---

## 1. 메시지 큐 — SNDHWM / RCVHWM

| 항목 | 설명 |
|------|------|
| **하는 일** | pipe의 최대 메시지 수를 제한 |
| **적용 위치** | `pipe_t::check_write()` |
| **기본값** | balanced profile을 쓰는 자동 HWM. context auto-HWM을 끄면 `1000` 사용 |
| **0** | 무제한 |
| **영향** | HWM 도달 시 block 또는 `ZLINK_SUBMIT_BACKPRESSURED` 반환. LWM 이하로 drain되면 복구 |

**LWM (Low Water Mark) 공식:** `(HWM + 1) / 2`

HWM=100이면 LWM=50. 큐가 100에서 block되고 50 이하로 drain되어야 재개된다.
이 간격이 writable/non-writable 진동을 방지하는 히스테리시스다.

**소켓 타입별 차이:** 의미는 같지만 자동 정책 클래스가 다르다.
`PAIR=control`, `DEALER=peer_queue`, `ROUTER=routed`, `STREAM=stream`,
`PUB/XPUB=fanout`, `SUB/XSUB=recv_ingress`다. SPOT 내부 토픽 퍼블리셔는
`spot_data`, peer/control 소켓은 `control`, SPOT 라우터는 `routed`로 계산한다.

컨텍스트 옵션 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`은 네 가지 profile 중 하나를 선택한다.
기본값은 `ZLINK_AUTO_HWM_PROFILE_BALANCED`이며 자동 HWM은 기본으로 켜져 있다.
컨텍스트에서 기존 고정 HWM 기본값 `1000`을 유지해야 할 때만
`ZLINK_CTX_OPT_AUTO_HWM_ENABLE`을 `0`으로 설정한다.

| 소켓 그룹 | `compact` | `low_latency` | `balanced` | `throughput` |
|---|---:|---:|---:|---:|
| non-STREAM data socket | 64 | 128 | 256 | 512 |
| STREAM | 8 | 16 | 64 | 256 |
| control | 8 | 16 | 16 | 32 |

일반 소켓의 계획기(planner)는 HWM을 연결 하나의 큐 깊이(queue depth)로 본다. 컨텍스트
메모리 예산을 연결 수로 나누지 않는다. 대신 profile의 바이트 범위(byte envelope)가 유지되도록
다음 공식을 적용한다.

```text
scaled_hwm = ceil(basis_hwm * basis_message_unit / effective_message_unit)
```

자동 HWM의 최소값은 `1`이고 결과는 profile별 메시지 수 cap으로 제한된다.

SPOT mesh 내부 소켓 중 `mesh-pub`, `mesh-xsub`, `routed-router`는 연결 수가 많을 때 별도의
connection bucket을 먼저 적용한다. bucket 값은 `4 KiB` 메시지 기준 HWM이며, 최종 HWM은 다음
순서로 계산한다.

```text
base_hwm_4k = min(profile_hwm_4k, bucket_hwm_4k)
unit_budget_bytes = base_hwm_4k * 4096
scaled_hwm = ceil(unit_budget_bytes / effective_message_unit)
```

bucket 경계에는 hysteresis가 있다. 현재 `1-64` bucket이면 peer 수가 `80` 이상일 때
`65-128` bucket으로 이동한다. 현재 `65-128` bucket이면 peer 수가 `48` 이하로 내려갈
때 `1-64` bucket으로 돌아간다. 이 여유 구간은 peer 수가 경계 근처에서 흔들릴 때 HWM이
반복해서 바뀌는 일을 막는다. profile 또는 메시지 단위를 바꾸면 같은 bucket에 머무를 수
있는 상황이라도 새 설정으로 다시 계산한다.

이 조정은 SPOT data-plane 내부 socket queue의 보조 상한이다. public publish와 routed send의
backpressure 의미는 `publish_ingress_queue`와 `routed_send_queue`의 admission 규칙이 계속
결정한다. local fanout, pub ingress, control socket, 일반 DEALER/PAIR/STREAM 소켓에는 이
connection bucket을 적용하지 않는다.

사용자가 `SNDHWM` / `RCVHWM`을 직접 설정하면 자동 HWM보다 그 값이 항상 우선한다.

```c
int sndhwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &sndhwm, sizeof(sndhwm));
int rcvhwm = 5000;
zlink_set_option(socket, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof(rcvhwm));
```

---

## 2. 자동 HWM 메시지 단위

`ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 context 안의 소켓에 대해 자동 HWM
정책이 큐 슬롯 1개를 몇 바이트로 볼지 정합니다. 이 값은 최대 메시지 크기
제한이 아닙니다. 인바운드 메시지 크기 제한은 `ZLINK_OPT_MAXMSGSIZE`가
담당합니다.

워크로드의 일반적인 payload 크기를 알고 있고 기본 계획 크기와 다를 때 이
context 옵션을 조정합니다. 기본값 `0`은 소켓 타입별 기본 메시지 단위를
쓰겠다는 뜻입니다. C 저수준 socket option인
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 한 raw socket만 context와 다른 메시지
단위를 써야 할 때 사용할 수 있습니다.

| 소켓 타입 | 기본 메시지 단위 |
|-----------|------------------|
| `STREAM` | `1024` bytes |
| 그 외 소켓 | `4096` bytes |

`zlink_ctx_get()`은 사용자가 설정한 context raw 값을 반환합니다. 반환값이
`0`이면 소켓 타입별 기본값을 쓴다는 뜻이고 실제 계산에 쓰인 값은 monitor
snapshot의 `auto_hwm_effective_message_bytes`에서 확인합니다.

```c
int msg_unit = 8192;
zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES, msg_unit);
```

음수는 `EINVAL`로 실패하며 기존 설정을 바꾸지 않습니다. `ZLINK_OPT_SNDHWM`
또는 `ZLINK_OPT_RCVHWM`을 직접 설정한 소켓에서는 그 수동 HWM이 계속
우선합니다. context option과 raw socket option을 모두 설정하면 해당 소켓에는
raw socket option이 우선 적용됩니다.

### 수동 재계산 트리거

런타임에 auto-HWM profile이나 메시지 단위를 변경한 후, context 내 모든
소켓에 즉시 재계산을 트리거하려면:

```c
zlink_ctx_auto_hwm_recalculate(ctx);
```

auto HWM이 비활성화(`ZLINK_CTX_OPT_AUTO_HWM_ENABLE = 0`)된 경우 no-op이다.
컨텍스트 레벨의 `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` 설정은 자동
백그라운드 재계산 빈도를 제어한다. `zlink_ctx_auto_hwm_recalculate()` 호출은
debounce를 우회하고 즉시 실행된다.

---

## 3. 종료 대기 — LINGER

| 항목 | 설명 |
|------|------|
| **하는 일** | 종료 시 미전송 메시지 대기 시간 결정 |
| **적용 위치** | `session_base_t::process_term()` |
| **기본값** | 컨텍스트 상속 (`BLOCKY=1`이면 `-1`) |
| **-1** | 무한 대기 |
| **0** | 즉시 종료 — 미전송 메시지 폐기 |
| **>0** | 지정 시간(ms)까지 대기 후 강제 종료 |

**실제 동작:** linger > 0이면 타이머를 설정하고 타이머 만료 시 파이프를
강제 종료한다. `pipe->terminate(linger != 0)`으로 delay 여부를 전달한다.

**소켓 타입별 차이:**
- `XSUB`, `SUB`: 생성 시 linger를 강제로 `0`으로 override한다 (구독 소켓은
  종료 시 대기할 필요 없음).

```c
/* Wait up to 1 second for pending messages on close */
int linger = 1000;
zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
```

---

## 4. 타임아웃 — SNDTIMEO / RCVTIMEO

| 항목 | 설명 |
|------|------|
| **하는 일** | send/recv 최대 대기 시간 설정 |
| **적용 위치** | `zlink_send()` / `zlink_recv()` blocking 경로 |
| **기본값** | `1000` ms |
| **0** | non-blocking과 동일 (즉시 반환) |
| **-1** | 명시적으로 설정한 경우 무한 대기 |
| **>0** | 지정 시간(ms)까지 대기 후 send는 `ZLINK_SUBMIT_BACKPRESSURED`, recv는 `ZLINK_RECV_NO_DATA` 반환 |

**서비스 적용:** SPOT에서 pub/sub 내부 소켓에 전파.

```c
/* Give up send/recv after 500 ms */
int sndtimeo = 500;
zlink_set_option(socket, ZLINK_OPT_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
int rcvtimeo = 500;
zlink_set_option(socket, ZLINK_OPT_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
```

---

## 5. 연결 타임아웃 — CONNECT_TIMEOUT

| 항목 | 설명 |
|------|------|
| **하는 일** | `connect()` 비동기 시도의 타임아웃 설정 |
| **적용 위치** | `asio_*_connecter` -- `add_connect_timer()` |
| **기본값** | `0` (비활성 -- OS TCP 타임아웃 의존) |
| **>0** | 지정 시간(ms) 내 미완료 시 reconnect |

**OS 타임아웃과의 관계:** TCP 스택 자체의 SYN 재전송 타임아웃(보통 ~2분)보다
짧게 설정하여 빠른 failover를 구현할 때 유용하다.

```c
/* Fail connect attempts after 3 seconds */
int timeout = 3000;
zlink_set_option(socket, ZLINK_OPT_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
```

---

## 6. 재연결 — RECONNECT_IVL / RECONNECT_IVL_MAX

| 항목 | 설명 |
|------|------|
| **하는 일** | 재연결 시도 간격 제어 |
| **적용 위치** | 모든 connecter -- `get_new_reconnect_ivl()` |
| **RECONNECT_IVL 기본값** | `100` ms |
| **RECONNECT_IVL_MAX 기본값** | `0` (비활성 — 고정 간격 사용) |

**재연결 알고리즘:**
- `MAX == 0`: 고정 간격 + 랜덤 jitter (0 ~ `IVL`)
- `MAX > 0`: **지수 백오프** -- 매 실패마다 2배 증가, MAX에서 캡
  (예: 100 -> 200 -> 400 -> ... -> max)

**음수:** `RECONNECT_IVL < 0`이면 자동 재연결을 비활성화한다.

```c
/* Exponential backoff: 200ms initial, cap at 30s */
int ivl = 200;
zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL, &ivl, sizeof(ivl));
int ivl_max = 30000;
zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL_MAX, &ivl_max, sizeof(ivl_max));
```

---

## 7. TCP Keepalive — TCP_KEEPALIVE / TCP_KEEPALIVE_CNT / TCP_KEEPALIVE_IDLE / TCP_KEEPALIVE_INTVL

| 옵션 | 하는 일 | 기본값 |
|------|---------|--------|
| `TCP_KEEPALIVE` | SO_KEEPALIVE 활성화/비활성화 | `-1` (OS 기본) |
| `TCP_KEEPALIVE_CNT` | keepalive probe 실패 횟수 제한 | `-1` (OS 기본) |
| `TCP_KEEPALIVE_IDLE` | 첫 probe까지 유휴 시간 (초) | `-1` (OS 기본) |
| `TCP_KEEPALIVE_INTVL` | probe 사이 간격 (초) | `-1` (OS 기본) |

**적용 위치:** `tcp.cpp`의 `tune_tcp_keepalives()` -- OS socket option 전달.

**`-1`의 의미:** "이 값을 변경하지 않는다" — OS 기본 킵얼라이브 설정을 유지.

**TCP에만 적용.** IPC, inproc, WebSocket에는 해당 없음.

**권장 설정 예:**
```c
// Probe after 60s idle, every 10s, 3 probes max, then disconnect
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE, &(int){1}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_IDLE, &(int){60}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_INTVL, &(int){10}, sizeof(int));
zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_CNT, &(int){3}, sizeof(int));
```

---

## 8. TCP 재전송 — TCP_MAXRT

| 항목 | 설명 |
|------|------|
| **하는 일** | TCP 재전송 최대 타임아웃 설정 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(TCP_USER_TIMEOUT)` |
| **기본값** | `0` (비활성 -- OS 기본값 사용) |
| **>0** | 지정 시간(ms) 이상 실패 시 연결 포기 |

`TCP_USER_TIMEOUT` 커널 옵션이 있는 시스템(Linux 2.6.37+)에서만 동작한다.
Keepalive보다 빠른 dead peer 감지가 필요할 때 사용.

---

## 9. Nagle 알고리즘 — TCP_NODELAY

| 항목 | 설명 |
|------|------|
| **하는 일** | Nagle 알고리즘 비활성화 (즉시 전송) |
| **적용 위치** | `tcp.cpp` — `setsockopt(TCP_NODELAY)` |
| **기본값** | `1` (TCP_NODELAY 활성 = Nagle 비활성) |

**`1` (기본, 권장):** 작은 메시지도 지연 없이 즉시 전송. 메시징 시스템에 적합.
**`0`:** Nagle 활성 — 작은 패킷을 모아서 전송. 대역폭 효율은 높지만 지연 증가.

---

## 10. 즉시 연결 — IMMEDIATE

| 항목 | 설명 |
|------|------|
| **하는 일** | pipe를 즉시 연결할지, 연결 완료 후 연결할지 결정 |
| **적용 위치** | `socket_base_endpoint.cpp` |
| **기본값** | `0` (즉시 연결) |

**`0` (기본):** connect() 호출 즉시 파이프를 소켓에 연결한다. 연결 완료 전에도 `send()`가
가능하고 메시지는 큐에 쌓인다.

**`1`:** 연결이 실제로 완료된 후에만 파이프가 소켓에 연결된다. 연결 전 `send()`는
차단되거나 `ZLINK_SUBMIT_BACKPRESSURED` 를 반환한다. 또한 일시적 연결 끊김(hiccup) 시 파이프가 즉시
제거된다.

---

## 11. 최신 값만 유지 — CONFLATE

| 항목 | 설명 |
|------|------|
| **하는 일** | 최근 메시지 1개만 유지, 이전 폐기 |
| **적용 위치** | `pipe.cpp` -- `ypipe_conflate_t` |
| **기본값** | `0` (비활성) |
| **유효 소켓** | `DEALER`, `PUB`, `SUB`에서만 동작 |

활성화 시 HWM 설정은 무시된다. 멀티파트 메시지는 합류(conflate) 모드에서 수신할 수
없다. 센서 데이터처럼 "최신 값만 의미 있는" 시나리오에 적합하다.

---

## 12. OS 소켓 버퍼 — SNDBUF / RCVBUF

| 항목 | 설명 |
|------|------|
| **하는 일** | 커널 소켓 송수신 버퍼 크기 설정 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(SO_SNDBUF/SO_RCVBUF)` |
| **기본값** | `-1` (OS 기본값 유지) |
| **0 이상** | 지정 크기(바이트)를 OS에 요청 |

HWM과 독립적이다. HWM은 zlink 파이프 수준의 메시지 수 제한이고
SNDBUF/RCVBUF는 OS 커널 소켓 버퍼의 바이트 크기다.

auto-HWM profile과 STREAM 기본값은 이 값을 자동으로 바꾸지 않는다. 대규모
mesh처럼 연결 수가 많고 메모리 상한이 중요하면 애플리케이션이나 운영 설정에서
작은 값을 명시할 수 있다.

---

## 13. IP 서비스 품질 — TOS

| 항목 | 설명 |
|------|------|
| **하는 일** | IP TOS (DSCP/ECN) 필드 설정 |
| **적용 위치** | TCP 소켓 설정 시 `setsockopt(IP_TOS)` |
| **기본값** | `0` (best-effort) |

QoS 정책이 있는 네트워크에서 트래픽 우선순위를 지정할 때 사용한다.

---

## 14. 연결 대기열 — BACKLOG

| 항목 | 설명 |
|------|------|
| **하는 일** | `listen()` accept 대기열 최대 길이 |
| **적용 위치** | `asio_tcp_listener.cpp` |
| **기본값** | `100` |

**소켓 타입별 차이:**
- `STREAM`: `65536`으로 자동 override (다수 외부 클라이언트 수용)

---

## 15. I/O 스레드 어피니티 — AFFINITY

| 항목 | 설명 |
|------|------|
| **하는 일** | 소켓을 특정 I/O 스레드에 할당 |
| **적용 위치** | `socket_base` -- `choose_io_thread()` |
| **기본값** | `0` (모든 I/O 스레드 사용 가능) |
| **타입** | `uint64_t` 비트마스크 |

비트 N이 1이면 I/O 스레드 N을 사용 가능하다. `0`은 모든 스레드를 허용한다.
I/O 스레드가 여러 개(`ZLINK_IO_THREADS > 1`)일 때 특정 소켓을 특정 스레드에
고정하여 CPU 친화성을 높일 수 있다.

---

## 16. 최대 메시지 크기 — MAXMSGSIZE

| 항목 | 설명 |
|------|------|
| **하는 일** | 수신 메시지 최대 크기 제한 |
| **적용 위치** | 세션/엔진 수준 메시지 크기 검증 |
| **기본값** | `-1` (무제한) |
| **>0** | 지정 크기(바이트) 초과 메시지 거부 |

신뢰할 수 없는 피어로부터의 메모리 고갈(OOM) 공격을 방지하는 데 유용하다. 기존의 큰 메시지
배포를 깨지 않기 위해 기본값은 무제한으로 남아 있으므로, 신뢰 경계 밖의 피어를 받는 애플리케이션은
양수 제한을 명시적으로 설정해야 한다.

listener 소켓에서는 새로 수락된 세션이 제한을 상속할 수 있도록 `bind` 전에 설정한다.

```c
int64_t max_msg_size = 1024 * 1024;  /* 1 MiB */
zlink_set_option(socket, ZLINK_OPT_MAXMSGSIZE,
                 &max_msg_size, sizeof(max_msg_size));
```

---

## 17. IPv6 — IPV6

| 항목 | 설명 |
|------|------|
| **하는 일** | IPv6 활성화 (IPv4와 동시 사용) |
| **적용 위치** | `asio_tcp_connecter`, `asio_tcp_listener` |
| **기본값** | `0` (IPv4 전용, 컨텍스트 상속) |

`1`로 설정하면 `IPV6_V6ONLY=0`인 dual-stack 소켓을 생성한다.

---

## 18. 멀티캐스트 — MULTICAST_HOPS / MULTICAST_MAXTPDU

| 옵션 | 하는 일 | 기본값 |
|------|---------|--------|
| `MULTICAST_HOPS` | 멀티캐스트 패킷 TTL | `1` |
| `MULTICAST_MAXTPDU` | 최대 transport 데이터 유닛 크기 (바이트) | `1500` |

PGM transport에서만 적용. 현재 PGM은 임시 비활성화 상태.

---

## 19. 구독 매칭 반전 — INVERT_MATCHING

| 항목 | 설명 |
|------|------|
| **하는 일** | 구독 필터 매칭 결과 반전 |
| **적용 위치** | `xsub.cpp` |
| **기본값** | `0` (정상 매칭) |

`1`로 설정하면 구독하지 않은 토픽의 메시지를 받고 구독한 토픽은 받지 않는다.

---

## 20. 네트워크 인터페이스 바인딩 — BINDTODEVICE

| 항목 | 설명 |
|------|------|
| **하는 일** | 특정 네트워크 인터페이스에 바인딩 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(SO_BINDTODEVICE)` |
| **기본값** | 빈 문자열 (바인딩 없음) |

Linux `SO_BINDTODEVICE` 지원 시스템에서만 동작한다. 멀티호밍 서버에서 특정 NIC에
트래픽을 제한할 때 사용한다.

---

## 21. 핸드셰이크 타임아웃 — HANDSHAKE_IVL

| 항목 | 설명 |
|------|------|
| **하는 일** | ZMP 핸드셰이크 최대 시간 설정 |
| **적용 위치** | ZMP 엔진 핸드셰이크 타이머 |
| **기본값** | `30000` ms (30초) |
| **0** | 핸드셰이크 타임아웃 비활성 |

핸드셰이크가 이 시간 안에 완료되지 않으면 연결이 닫힌다.

---

## 22. ZMP 메타데이터 — ZMP_METADATA

| 항목 | 설명 |
|------|------|
| **하는 일** | ZMP 프로토콜 메타데이터 속성 첨부 |
| **적용 위치** | `asio_zmp_engine` READY 프레임 |
| **기본값** | `0` (비활성) |
| **타입** | binary |

---

## 소켓 타입별 기본값 override

일부 소켓 타입은 생성 시 공통 기본값을 자체적으로 override한다.

| 소켓 타입 | override 옵션 | 값 | 이유 |
|-----------|---------------|-----|------|
| `SUB` / `XSUB` | `LINGER` | `0` | 구독 소켓은 종료 시 대기 불필요 |
| `ROUTER` | `ROUTER_MANDATORY` | `1` | 미연결 peer 대상 전송 실패를 surface |
| `PUB` / `XPUB` | `PUB_NODROP` | `1` | HWM 시 조용한 drop 대신 `BACKPRESSURED` surface |
| `STREAM` | `BACKLOG` | `65536` | 다수 외부 클라이언트 수용 |

> **기본값과 관찰 가능한 동작:**
>
> - `ROUTER_MANDATORY` 기본값은 `1` 이다. 옵션을 명시하지 않은 ROUTER 에서
>   `zlink_send_rid()` 로 미연결 peer 를 지정하면 조용히 넘어가지 않고
>   `ZLINK_SUBMIT_NOT_CONNECTED` 가 반환된다. writable / `ZLINK_POLLOUT`
>   관찰값도 실제로 쓸 수 있는 peer 가 있을 때만 surface 된다. 조용한 drop
>   이 필요하면 옵션을 `0` 으로 명시 설정한다.
> - `ZLINK_OPT_RID_DUPLICATE_POLICY` 기본값은
>   `ZLINK_RID_DUPLICATE_REJECT` 이다. duplicate peer identity 가 들어오면
>   기존 pipe 를 유지하고 새 연결은 등록하지 않는다. 새 연결이 기존 pipe 를
>   인수해야 하면 `ZLINK_RID_DUPLICATE_HANDOVER` 로 명시 설정한다.
> - `PUB_NODROP` 기본값은 `1` 이다. HWM 상황에서 `zlink_publish()` 가 조용히
>   drop 하지 않고 `ZLINK_SUBMIT_BACKPRESSURED` 를 반환한다. 진행률을 위해
>   drop 이 필요한 loss-tolerant workload 는 `0` 으로 명시 설정한다.
>
> 이 기본값은 **기본 profile** 에만 영향을 주며 옵션 상수 이름이나 on/off
> 의미는 그대로다.

## 소켓 타입별 전용 옵션

공통 옵션 외에 소켓 타입별 전용 옵션은 전용 API로 설정한다.

| 소켓 | API | 대표 옵션 |
|------|-----|-----------|
| ROUTER | `zlink_set_router_option()` | `MANDATORY` (기본 `1`), `PROBE`, `CONNECT_ROUTING_ID`, `REQUEST_TIMEOUT_MS`, `WEIGHT` (기본 `100`) |
| DEALER | `zlink_set_dealer_option()` | `PROBE`, `REQUEST_TIMEOUT_MS`, `WEIGHT` (기본 `100`) |
| PUB/XPUB | `zlink_set_pub_option()` | `VERBOSE`, `VERBOSER`, `NODROP` (기본 `1`), `MANUAL`, `WELCOME_MSG`, `APPROVE_SUBSCRIBE`, `REJECT_SUBSCRIBE` |
| SUB/XSUB | `zlink_set_sub_option()` (`TOPICS_COUNT`); 구독 필터는 `zlink_set_subscription()` / `zlink_unset_subscription()` | `TOPICS_COUNT` |
| STREAM | `zlink_set_stream_option()` | `NOTIFY` |

---

## 소켓 Channel 이름

임의의 소켓에 논리적 채널 이름을 지정한다. framework와 응용은 이 metadata를
사용해 transport endpoint에 channel을 섞어 넣지 않고 route-channel 설정을
명시적으로 유지할 수 있다.

```c
/* channel 이름 설정 */
zlink_socket_set_channel_name(socket, "price-feed");

/* 읽기 */
char buf[256];
size_t len = 0;
zlink_socket_get_channel_name(socket, buf, sizeof(buf), &len);
```

channel 이름은 metadata일 뿐이다. 이 API가 소켓을 연결하지 않는다.

---
<!-- zlink-nav:bottom:start -->
[← 스레드 안전성](11-thread-safety.ko.md)
<!-- zlink-nav:bottom:end -->
