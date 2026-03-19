# Typed Socket Option API Plan

> 상태: 계획 초안.
> 선행 문서: 이 문서는
> [`subscribe-surface-renaming-plan.ko.md`](subscribe-surface-renaming-plan.ko.md)의
> subscribe 관련 naming 재편을 **대체**한다. 해당 문서에서 도입 예정이던
> `zlink_set_subscribe`/`zlink_unset_subscribe` 전용 함수는 이 문서의
> `zlink_set_subscription`/`zlink_unset_subscription` 경로로 흡수된다.
> 범위: `core/include/zlink.h` public C API에 타입별 옵션 함수/enum 추가,
> gateway/spot 기존 옵션 API 통합, subscription 함수 경로 통합을 다룬다.
> 수정 범위: `core/`만 포함한다. bindings/외부 래퍼는 이번 범위에서 제외한다.

## 1. 목적

**이 계획은 major API break 전제이다.**
기존 옵션 관련 public 함수/enum을 삭제하고 새 API로 대체한다.
bindings 및 외부 사용자는 같은 릴리스에서 기존 API가 깨지며,
별도 이관이 필요하다.

현재 소켓 옵션 public C API는 단일 함수 + flat enum 구조다:

```c
zlink_setsockopt(socket, ZLINK_SOCKOPT_???, &val, sizeof(val));
```

현재 문제:

- `zlink_socket_option_t` enum에 48+개 옵션이 카테고리 구분 없이 나열되어 있다.
  ROUTER 전용 옵션, XPUB 전용 옵션, 공통 옵션이 같은 enum에 섞여 있어서
  소켓 타입별 유효한 옵션이 무엇인지 직관적으로 알 수 없다.
- 잘못된 조합(예: SUB 소켓에 `ZLINK_SOCKOPT_ROUTER_MANDATORY`)은
  런타임에 `EINVAL`을 받고서야 알 수 있다.
- gateway/spot은 이미 타입별 옵션 함수+enum 패턴을 사용 중이나
  (`zlink_gateway_set_option`, `zlink_spot_set_pub_option`, `zlink_spot_set_sub_option`),
  raw 소켓과 서로 다른 함수 체계를 사용하고 있다.
- ROUTING_ID는 `setsockopt` 경로와 서비스별 전용 함수가 공존하며,
  getter의 데이터 계약(raw bytes vs `zlink_routing_id_t`)이 불일치한다.
- LAST_ENDPOINT는 `getsockopt` 경로와 서비스 전용 함수가 공존한다.
- spot/spot_node는 내부적으로 pub/sub side를 가지지만, 그 구조를 public
  surface에 과도하게 노출하면 사용자가 알아야 할 개념 수가 늘고
  change amplification이 커진다.

이 문서는 raw 소켓에 타입별 옵션 함수+enum을 도입하고,
gateway/discovery도 같은 함수 체계로 통합하되
**spot/spot_node public surface는 unified handle로 유지**하는 계획을
정리한다.

핵심 목표:

- 공통 소켓 옵션 함수 `zlink_set_option` + `zlink_option_t` enum을 도입한다.
- 소켓 타입별 전용 옵션 함수+enum을 도입한다:
  `zlink_set_router_option` / `zlink_get_router_option`,
  `zlink_set_dealer_option`,
  `zlink_set_stream_option` / `zlink_get_stream_option`,
  `zlink_set_pub_option` / `zlink_get_pub_option`,
  `zlink_set_sub_option` / `zlink_get_sub_option`.
- subscription 명령/조회는 option getter에 넣지 않고 별도 함수로 분리한다:
  `zlink_set_subscription`,
  `zlink_unset_subscription`,
  `zlink_subscription_at`.
- ROUTING_ID는 `zlink_set_routing_id` / `zlink_get_routing_id` 전용 함수로 제공한다.
- TLS는 `zlink_set_tls_server` / `zlink_set_tls_client` 전용 함수로 제공한다.
- LAST_ENDPOINT는 `zlink_get_option(handle, ZLINK_OPT_LAST_ENDPOINT, ...)` 경로로 통합한다.
- unified `spot` / `spot_node` handle은 public surface로 유지한다.
  내부 구현에는 side 개념을 둘 수 있지만, public C API는 기존처럼
  unified handle을 중심으로 유지한다.
- discovery는 managed socket set에 대한 fan-out setter로 정의한다.
  단일 source-of-truth가 없으므로 getter는 routing_id만 제공한다.
- 기존 옵션 관련 함수/enum을 public header에서 **삭제**한다 (하위 호환 없음).

비목표:

- 내부 옵션 dispatch 구조(`xsetsockopt`/`options.setsockopt` 2-tier)는
  변경하지 않는다.
- PAIR 소켓 전용 함수는 도입하지 않는다 (PAIR는 공통 옵션만 사용).

## 1.1 새 컨텍스트에서 바로 작업하기 위한 전제

이 문서만 보고 작업을 시작하는 사람을 위해 현재 기준점을 명시한다.

- **현재 mainline 기준 구현은 unified surface 모델이다.**
  즉 현재 코드베이스에는 `zlink_spot_*`, `zlink_spot_node_*`,
  `zlink_gateway_set_tls_*`, `zlink_discovery_set_tls_client`,
  `zlink_registry_setsockopt(...)` 같은 기존 API가 존재한다.
  이 문서는 그 상태에서 typed option 체계로 재편하되 unified service
  surface는 유지하는 계획이다.
- **문서의 목표 상태와 현재 코드는 일부 다르다.**
  문서에 `spot-pub`, `spot-sub`, `spotnode-pub`, `spotnode-sub`가
  등장하더라도, 이는 public API의 주 표면이 아니라 내부 구현이나
  제한적 보조 handle 관점 설명일 수 있다.
- **작업 기준 파일은 아래 순서로 읽는다.**
  1. `core/include/zlink.h`
  2. `core/src/api/zlink.cpp`
  3. `core/src/services/gateway/gateway.cpp`
  4. `core/src/services/discovery/discovery.cpp`
  5. `core/src/services/spot/spot_pub.cpp`
  6. `core/src/services/spot/spot_sub.cpp`
  7. `core/src/services/spot/spot_node.cpp`
- **빌드/검증 디렉터리는 `core/build/` 하나만 사용한다.**
  새 컨텍스트에서 작업할 때도 configure/build/test는 모두 `core/build/` 기준이다.
- **bindings는 이번 범위 밖이다.**
  따라서 public C API를 먼저 깨끗하게 재편하고, bindings 반영은 후속 범위로 본다.

### 1.1.1 용어 맵

이 문서에서 쓰는 용어는 아래 의미로 고정한다.

- **raw socket**: `PUB`, `SUB`, `XPUB`, `XSUB`, `ROUTER`, `DEALER`, `STREAM`
  등 기존 libzlink socket handle
- **gateway**: gateway service handle
- **discovery**: 단일 socket이 아니라 managed socket set을 가진 service handle
- **spot-pub**: publish/send-ready/TLS server/client/peer-connect 의미를 갖는
  pub side public handle
- **spot-sub**: recv/subscribe/unsubscribe/TLS client 의미를 갖는
  sub side public handle
- **spotnode-pub**: node-owned live pub socket을 직접 대표하는 public handle
- **spotnode-sub**: node-owned live sub socket을 직접 대표하는 public handle
- **unified spot / unified spot_node**: public API의 주 표면으로 유지되는
  기존 service handle

### 1.1.2 작업 순서 요약

새 컨텍스트에서는 아래 순서로 진행하면 문서와 코드의 불일치가 가장 적다.

1. §3.1, §3.2로 새 public surface와 enum을 먼저 고정한다.
2. §3.5, §5로 삭제/대체되는 기존 unified API 범위를 먼저 확인한다.
3. §4와 §7을 따라 내부 dispatch/helper 추가 지점을 잡는다.
4. §8의 phase 순서대로 구현한다. 임의로 순서를 바꾸지 않는다.
5. 마지막에 §11 검증 계획으로 build/test/symbol 확인을 한다.

### 1.1.3 구현 중 반드시 유지할 불변조건

- unified `spot` / `spot_node` public handle은 최종 상태에서도 유지된다.
- `spot_node`와 child `spot`은 옵션 관점에서 서로 독립적이다.
  child 생성 시 option/filter/routing_id를 복제하지 않는다.
- TLS는 option enum에 다시 넣지 않는다.
- `ONLY_FIRST_SUBSCRIBE`는 public typed enum에 다시 넣지 않는다.
- discovery는 `set_option` fan-out만 제공하고 `get_option`은 제공하지 않는다.
- public API는 unified `spot` / `spot_node`를 우선으로 유지한다.
  side handle은 필요 시 내부 구현 또는 제한적 보조 surface로만 둔다.

## 2. 설계 근거

### 2.1 기존 패턴 확장

gateway와 기존 spot 계열이 이미 사용 중인 패턴을 raw 소켓에도 적용한다:

| 기존 패턴 | 새 패턴 |
| --- | --- |
| `zlink_gateway_set_option(gw, ZLINK_GATEWAY_OPT_SNDHWM, ...)` | `zlink_set_option(gw, ZLINK_OPT_SNDHWM, ...)` |
| `zlink_spot_pub_set_option(spot_pub, ZLINK_SPOT_PUB_OPT_NODROP, ...)` | `zlink_set_pub_option(spot_pub, ZLINK_PUB_OPT_NODROP, ...)` |
| `zlink_spot_sub_set_option(spot_sub, ZLINK_SPOT_SUB_OPT_RCVHWM, ...)` | `zlink_set_option(spot_sub, ZLINK_OPT_RCVHWM, ...)` |
| `zlink_setsockopt(router, ZLINK_SOCKOPT_ROUTER_MANDATORY, ...)` | `zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, ...)` |
| `zlink_gateway_set_routing_id(gw, data, size)` | `zlink_set_routing_id(gw, data, size)` |
| `zlink_gateway_last_endpoint(gw, buf, &size)` | `zlink_get_option(gw, ZLINK_OPT_LAST_ENDPOINT, buf, &size)` |

### 2.2 POSD 관점

- **deep module**: 각 함수가 내부적으로 핸들 타입 감지 + 옵션 매핑 + 유효성 검증을
  처리하되, 사용자에게는 간결한 인터페이스만 노출.
- **information hiding**: 사용자는 소켓 내부 구조를 몰라도
  해당 타입의 enum만 보면 유효 옵션을 즉시 알 수 있다.
- **change amplification 제거**: gateway/spot 계열/raw socket에서 같은 함수 체계를
  사용하므로, 옵션 추가 시 하나의 enum만 수정하면 된다.

### 2.3 설계 원칙

- **기존 validation/apply 경로 최대 재사용**: 서비스 내부의 `set_option()`
  switch와 live socket apply 경로를
  그대로 활용한다. 우회하지 않는다.
- **부족한 곳은 최소 shadow state 추가**: 기존 코드에 getter가 없거나
  subscription filter persistence가 없는 경우, public 계약 충족에 필요한
  최소 state만 추가한다.
- **값 타입별 함수 분리는 하지 않는다**: 그룹별 함수 + runtime validation을
  사용한다. 각 옵션의 값 타입(int, string, blob)은 API reference 문서에서
  명세하되, 이 계획 문서에서는 특수 케이스만 명시한다.

### 2.4 ROUTING_ID 전용 함수 분리 근거

ROUTING_ID는 다른 소켓 옵션과 달리 타입 의미가 특수하다:

- getter는 `zlink_routing_id_t` 구조체(size + data[255])를 반환해야 자연스럽다.
  공통 raw-bytes getter(`zlink_get_option`)에 억지로 끼우면 데이터 계약이 모호해진다.
- STREAM 소켓에서는 사용 불가(TCP 연결 주소를 routing identity로 사용).
- socket, gateway, discovery, unified `spot` / `spot_node`에서 동작하며,
  각각 내부 경로가 다르다.

전용 함수 2개로 분리하면 set/get 데이터 계약이 명확하고,
STREAM 제약도 함수 내부에서 일관되게 처리된다.

unified public surface에서는 publish side routing_id를 대표 값으로 둔다:
- `zlink_set_routing_id(spot, ...)` → unified `spot`의 publish side routing_id 설정
- `zlink_set_routing_id(node, ...)` → unified `spot_node`의 publish side routing_id 설정
- 제한적 보조 side handle이 있는 경우에는 각 side own routing_id를 직접 가질 수 있다.

### 2.4.1 TLS 전용 함수 분리 근거

TLS는 `linger`, `sndhwm`, `heartbeat` 같은 일반 socket option과 성격이 다르다.

- TLS는 transport tuning이 아니라 보안 정책이다.
- gateway, discovery, spot 계열, registry 같은 서비스에도 같은 의미로
  적용될 수 있다.
- 현재 구현도 이미 `*_set_tls_server()`, `*_set_tls_client()` 별도 함수 패턴을
  사용하고 있다.

따라서 TLS는 `zlink_option_t`에 넣지 않고 공통 전용 함수로 분리한다:

- `zlink_set_tls_server(handle, cert, key, require_client_cert)`
- `zlink_set_tls_client(handle, ca_cert, hostname, trust_system)`

이 함수들은 typed option matrix와 별도 계약을 가지며, 지원 대상 handle은
gateway, discovery, unified `spot` / `spot_node`, registry를 포함한다.

### 2.5 spot/spot_node unified public surface 유지 결정

spot/spot_node는 내부적으로 pub/sub가 서로 다른 socket 의미를 가지지만,
그 내부 구조를 public API의 주 표면으로 끌어올리면 사용자가 알아야 할
개념 수가 늘고 문서, 테스트, 호출부가 side 중심으로 증폭된다.

따라서 이 계획은 unified `spot` / `spot_node` public surface를 유지한다.
typed option 도입의 목적은 public surface를 쪼개는 것이 아니라,
공통 옵션과 pub/sub 특화 옵션의 경계를 명확히 하는 것이다.

원칙:

- 공통 옵션은 `zlink_set_option()` / `zlink_get_option()`으로 설정한다.
- pub 특화 옵션은 `zlink_set_pub_option()` / `zlink_get_pub_option()`으로 설정한다.
- sub 특화 옵션은 `zlink_set_sub_option()` / `zlink_get_sub_option()`으로 설정한다.
- subscription은 `zlink_set_subscription()` / `zlink_unset_subscription()`으로 설정한다.
- unified `spot` / `spot_node`는 위 generic typed option 함수를 직접 사용한다.
- `zlink_spot_set_pub_option()` 같은 service-specific option wrapper는 유지하지 않는다.
- side handle은 필요 시 내부 구현 또는 제한적 보조 surface로만 둔다.

**spot_node ↔ child spot 옵션 독립 규칙**:

spot_node는 자체 pub/sub socket을 가진 **live socket owner**이다.
child spot 생성 시 node의 현재 pub/sub socket option과 subscription filter를
복제하지 않는다. spot_node와 child spot은 옵션 관점에서 서로 **독립적**이다.

독립 규칙:

| 분류 | 옵션 | child spot 생성 시 처리 |
| --- | --- | --- |
| 정수형 socket option | SNDHWM, SNDTIMEO, LINGER, SNDBUF, RCVBUF, RCVHWM, RCVTIMEO, NODROP 등 | ✗ 비복제 — child spot에서 별도 설정 필요 |
| subscription filter | SUBSCRIBE로 설정된 필터 | ✗ 비복제 — child spot에서 별도 subscribe 필요 |
| routing_id | publish side own routing_id | ✗ 비복제 — child spot은 자체 ID를 별도 설정해야 함 |
| runtime/read-only | TOPICS_COUNT, FD, EVENTS, TYPE, LAST_ENDPOINT | ✗ 비복제 (socket별 고유 runtime 값) |

이 모델로 인해:
- 사용자는 기존 unified lifecycle과 send/recv surface를 유지할 수 있다.
- 공통 옵션과 pub/sub 특화 옵션의 경계만 배우면 된다.
- wrapper API 수를 줄여 change amplification을 억제할 수 있다.
- 내부 side 구조는 감추고 필요한 경우에만 제한적으로 직접 접근하게 할 수 있다.

### 2.6 대안 평가

| 대안 | 판단 |
| --- | --- |
| 문서/주석만 추가 | 코드 도움 없음, 드리프트 위험 → 보조 수단으로만 |
| `zlink_sockopt_is_valid()` 조회 함수 | API 표면에 메타 함수가 추가되지만 사용자 코딩 패턴 변경 없음 |
| 옵션 값 범위 재배치 | 기존 enum을 삭제하므로 ABI 논점 소멸, 그러나 불필요한 복잡성 |
| struct+enum discriminator 통합 | 피드백 위반: 불필요한 추상화, 보일러플레이트 증가 |
| service-specific wrapper 유지 (`zlink_spot_set_pub_option` 등) | API 수만 늘고 generic typed option과 중복 → 기각 |
| **타입별 함수+enum 분리 + unified spot 유지** | 기존 mental model 유지, 옵션 체계만 정리 → **채택** |

## 3. 새 API 명세

### 3.0 spot/discovery 계약 정리

- unified `spot` / `spot_node` handle은 새 public API에서도 유지한다.
- 공통 옵션은 unified `spot` / `spot_node`에도
  `zlink_set_option(handle, ...)` / `zlink_get_option(handle, ...)`로 설정한다.
- pub/sub 전용 옵션은 unified `spot` / `spot_node`에도
  `zlink_set_pub_option(handle, ...)` / `zlink_set_sub_option(handle, ...)`로 설정한다.
- subscription은 unified `spot` / `spot_node`에도
  `zlink_set_subscription(handle, ...)` / `zlink_unset_subscription(handle, ...)`로 설정한다.
- `zlink_spot_set_pub_option`, `zlink_spot_set_sub_option`,
  `zlink_spot_node_set_pub_option`, `zlink_spot_node_set_sub_option` 같은
  service-specific wrapper는 삭제한다.
- TLS는 option enum이 아니라 `zlink_set_tls_server` /
  `zlink_set_tls_client` 전용 함수로 제공한다.
- `discovery`는 unified socket이 아니라 managed socket set(SUB + 복수 DEALER)을 가진다.
  따라서 `zlink_set_option(discovery, ...)`은 "단일 socket setter"가 아니라
  **managed socket set에 fan-out 적용**으로 해석한다.
  단일 source-of-truth가 없으므로 `zlink_get_option(discovery, ...)`는 제공하지 않는다.

### 3.1 함수 선언

```c
/* ── 공통 옵션 (모든 소켓 타입 + gateway + discovery(fan-out)) ── */
ZLINK_EXPORT int zlink_set_option (void *handle_,
                                   zlink_option_t option_,
                                   const void *optval_,
                                   size_t optvallen_);
ZLINK_EXPORT int zlink_get_option (void *handle_,
                                   zlink_option_t option_,
                                   void *optval_,
                                   size_t *optvallen_);

/* ── Routing ID
 * supported handles:
 *   PAIR, PUB, SUB, XPUB, XSUB, DEALER, ROUTER,
 *   gateway, discovery, unified spot, unified spot_node
 * unsupported:
 *   STREAM
 * ── */
ZLINK_EXPORT int zlink_set_routing_id (void *handle_,
                                       const void *data_,
                                       size_t size_);
ZLINK_EXPORT int zlink_get_routing_id (void *handle_,
                                       zlink_routing_id_t *out_);

/* ── TLS
 * supported handles:
 *   gateway, discovery, unified spot, unified spot_node, registry
 * ── */
ZLINK_EXPORT int zlink_set_tls_server (void *handle_,
                                       const char *cert_,
                                       const char *key_,
                                       int require_client_cert_);
ZLINK_EXPORT int zlink_set_tls_client (void *handle_,
                                       const char *ca_cert_,
                                       const char *hostname_,
                                       int trust_system_);

/* ── Router 전용
 * supported handles:
 *   ROUTER, gateway
 * partial:
 *   DEALER는 PROBE만 허용
 * ── */
ZLINK_EXPORT int zlink_set_router_option (void *handle_,
                                          zlink_router_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);
ZLINK_EXPORT int zlink_get_router_option (void *handle_,
                                          zlink_router_option_t option_,
                                          void *optval_,
                                          size_t *optvallen_);

/* ── Dealer 전용
 * supported handles:
 *   DEALER
 * ── */
ZLINK_EXPORT int zlink_set_dealer_option (void *handle_,
                                          zlink_dealer_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);

/* ── Stream 전용
 * supported handles:
 *   STREAM
 * ── */
ZLINK_EXPORT int zlink_set_stream_option (void *handle_,
                                          zlink_stream_option_t option_,
                                          const void *optval_,
                                          size_t optvallen_);
ZLINK_EXPORT int zlink_get_stream_option (void *handle_,
                                          zlink_stream_option_t option_,
                                          void *optval_,
                                          size_t *optvallen_);

/* ── Pub 옵션
 * supported handles:
 *   PUB, XPUB, unified spot, unified spot_node
 * implementation may also route limited side handles internally
 * ──
 * option_: zlink_pub_option_t (pub 전용만 허용)
 * 공통 옵션은 zlink_set_option()/zlink_get_option() 사용 */
ZLINK_EXPORT int zlink_set_pub_option (void *handle_,
                                       zlink_pub_option_t option_,
                                       const void *optval_,
                                       size_t optvallen_);
ZLINK_EXPORT int zlink_get_pub_option (void *handle_,
                                       zlink_pub_option_t option_,
                                       void *optval_,
                                       size_t *optvallen_);

/* ── Sub 옵션
 * supported handles:
 *   SUB, XSUB, unified spot, unified spot_node
 * implementation may also route limited side handles internally
 * ──
 * option_: zlink_sub_option_t (sub 전용만 허용)
 * 공통 옵션은 zlink_set_option()/zlink_get_option() 사용 */
ZLINK_EXPORT int zlink_set_sub_option (void *handle_,
                                       zlink_sub_option_t option_,
                                       const void *optval_,
                                       size_t optvallen_);
ZLINK_EXPORT int zlink_get_sub_option (void *handle_,
                                       zlink_sub_option_t option_,
                                       void *optval_,
                                       size_t *optvallen_);

/* ── Subscription commands / query
 * supported handles:
 *   SUB, XSUB, unified spot, unified spot_node
 * ── */
ZLINK_EXPORT int zlink_set_subscription (void *handle_,
                                         const char *filter_);
ZLINK_EXPORT int zlink_unset_subscription (void *handle_,
                                           const char *filter_);
ZLINK_EXPORT int zlink_subscription_at (void *handle_,
                                        size_t index_,
                                        char *filter_out_,
                                        size_t *filter_len_inout_,
                                        int *is_pattern_out_);

/* ── Send-ready callback
 * supported handles:
 *   PAIR, PUB, XPUB, DEALER, ROUTER, gateway, unified spot, unified spot_node
 * unsupported handles:
 *   SUB, XSUB, STREAM
 * ── */
ZLINK_EXPORT int zlink_send_ready_handler (void *handle_,
                                           zlink_send_ready_handler_fn handler_,
                                           void *userdata_);
```

총 18개 함수 (set 10 + get 5 + subscription 3).

### 3.2 Enum 정의

#### 3.2.1 `zlink_option_t` — 공통 (모든 소켓 타입 + gateway + discovery)

```c
typedef enum zlink_option_t
{
    /* ── Transport / Buffer ── */
    ZLINK_OPT_AFFINITY          = 0x3001,
    ZLINK_OPT_RATE              = 0x3003,
    ZLINK_OPT_RECOVERY_IVL      = 0x3004,
    ZLINK_OPT_SNDBUF            = 0x3005,
    ZLINK_OPT_RCVBUF            = 0x3006,
    ZLINK_OPT_SNDHWM            = 0x300F,
    ZLINK_OPT_RCVHWM            = 0x3010,
    ZLINK_OPT_MAXMSGSIZE        = 0x300E,

    /* ── Timing ── */
    ZLINK_OPT_LINGER            = 0x300A,
    ZLINK_OPT_RCVTIMEO          = 0x3012,
    ZLINK_OPT_SNDTIMEO          = 0x3013,
    ZLINK_OPT_CONNECT_TIMEOUT   = 0x3024,
    ZLINK_OPT_RECONNECT_IVL     = 0x300B,
    ZLINK_OPT_RECONNECT_IVL_MAX = 0x300D,
    ZLINK_OPT_HANDSHAKE_IVL     = 0x301D,

    /* ── TCP ── */
    ZLINK_OPT_TCP_KEEPALIVE      = 0x3015,
    ZLINK_OPT_TCP_KEEPALIVE_CNT  = 0x3016,
    ZLINK_OPT_TCP_KEEPALIVE_IDLE = 0x3017,
    ZLINK_OPT_TCP_KEEPALIVE_INTVL= 0x3018,
    ZLINK_OPT_TCP_MAXRT          = 0x3025,
    ZLINK_OPT_TCP_NODELAY        = 0x3031,

    /* ── Heartbeat ── */
    ZLINK_OPT_HEARTBEAT_IVL     = 0x3021,
    ZLINK_OPT_HEARTBEAT_TTL     = 0x3022,
    ZLINK_OPT_HEARTBEAT_TIMEOUT = 0x3023,

    /* ── Network ── */
    ZLINK_OPT_IPV6              = 0x301A,
    ZLINK_OPT_TOS               = 0x301C,
    ZLINK_OPT_MULTICAST_HOPS    = 0x3011,
    ZLINK_OPT_MULTICAST_MAXTPDU = 0x3026,
    ZLINK_OPT_BINDTODEVICE      = 0x3027,
    ZLINK_OPT_BACKLOG           = 0x300C,

    /* ── Behavior ── */
    ZLINK_OPT_IMMEDIATE         = 0x3019,
    ZLINK_OPT_CONFLATE          = 0x301B,
    ZLINK_OPT_BLOCKY            = 0x301E,
    ZLINK_OPT_INVERT_MATCHING   = 0x3020,
    ZLINK_OPT_ZMP_METADATA      = 0x3030,

    /* ── Read-only (get_option only) ── */
    ZLINK_OPT_FD                = 0x3007,
    ZLINK_OPT_EVENTS            = 0x3008,
    ZLINK_OPT_TYPE              = 0x3009,
    ZLINK_OPT_LAST_ENDPOINT     = 0x3014
} zlink_option_t;
```

**ROUTING_ID는 이 enum에 포함하지 않는다** — `zlink_set_routing_id` /
`zlink_get_routing_id` 전용 함수로 분리 (§2.4 참조).

**특수 값 계약**:

| 옵션 | 값 타입 | set/get | 비고 |
| --- | --- | --- | --- |
| `ZLINK_OPT_LAST_ENDPOINT` | `char*` + `size_t*` | get-only | set 시 EINVAL |
| `ZLINK_OPT_BINDTODEVICE` | `const char*` + `size_t` | set/get | NUL-terminated string |
| `ZLINK_OPT_ZMP_METADATA` | `const char*` + `size_t` | set/get | key=value 형식 |
| 나머지 | `int` + `sizeof(int)` | set/get | 표준 정수 옵션 |

#### 3.2.2 `zlink_router_option_t` — Router 전용

```c
typedef enum zlink_router_option_t
{
    ZLINK_ROUTER_OPT_MANDATORY          = 0x3101,
    ZLINK_ROUTER_OPT_HANDOVER           = 0x3102,
    ZLINK_ROUTER_OPT_PROBE              = 0x3103,
    ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID = 0x3104
} zlink_router_option_t;
```

| 옵션 | ROUTER | DEALER | Gateway |
| --- | --- | --- | --- |
| MANDATORY | set/get | - | set/get |
| HANDOVER | set/get | - | set/get |
| PROBE | set | set | - |
| CONNECT_ROUTING_ID | set | - | set |

#### 3.2.3 `zlink_dealer_option_t` — Dealer 전용

```c
typedef enum zlink_dealer_option_t
{
    ZLINK_DEALER_OPT_PROBE = 0x3201
} zlink_dealer_option_t;
```

#### 3.2.4 `zlink_stream_option_t` — Stream 전용

```c
typedef enum zlink_stream_option_t
{
    ZLINK_STREAM_OPT_NOTIFY = 0x3501
} zlink_stream_option_t;
```

set/get 대칭을 유지한다.

#### 3.2.5 `zlink_pub_option_t` — Pub 전용 옵션

```c
typedef enum zlink_pub_option_t
{
    ZLINK_PUB_OPT_VERBOSE              = 0x3301,
    ZLINK_PUB_OPT_VERBOSER             = 0x3302,
    ZLINK_PUB_OPT_MANUAL               = 0x3303,
    ZLINK_PUB_OPT_MANUAL_LAST_VALUE    = 0x3304,
    ZLINK_PUB_OPT_NODROP               = 0x3305,
    ZLINK_PUB_OPT_WELCOME_MSG          = 0x3306,
    ZLINK_PUB_OPT_TOPICS_COUNT         = 0x3307,  /* get-only */

    /* XPUB manual mode — subscription 승인/거부 */
    ZLINK_PUB_OPT_APPROVE_SUBSCRIBE    = 0x3308,
    ZLINK_PUB_OPT_REJECT_SUBSCRIBE     = 0x3309
} zlink_pub_option_t;
```

`zlink_set_pub_option` / `zlink_get_pub_option`은
`zlink_pub_option_t`만 수용한다.
unified `spot` / `spot_node`를 포함한 pub-capable handle의 공통 옵션(LINGER, SNDHWM 등)은
`zlink_set_option(handle, ZLINK_OPT_..., ...)` /
`zlink_get_option(handle, ZLINK_OPT_..., ...)`로 설정/조회한다.

#### 3.2.6 `zlink_sub_option_t` — Sub 전용 옵션

```c
typedef enum zlink_sub_option_t
{
    ZLINK_SUB_OPT_TOPICS_COUNT         = 0x3400   /* get-only */
} zlink_sub_option_t;
```

pub과 동일하게 `zlink_sub_option_t`만 수용한다.
unified `spot` / `spot_node`를 포함한 sub-capable handle의 공통 옵션은
`zlink_set_option(handle, ZLINK_OPT_..., ...)` /
`zlink_get_option(handle, ZLINK_OPT_..., ...)`로 설정/조회한다.

`ONLY_FIRST_SUBSCRIBE`는 raw `XSUB`/`XPUB`의 low-level multipart parsing 옵션이므로
typed public enum에는 포함하지 않는다. 필요 시 legacy raw/internal 경로에서만
다룬다.

### 3.3 핸들 타입별 지원 매트릭스

#### 3.3.1 옵션 함수 지원

| 함수 | PAIR | PUB/XPUB | SUB/XSUB | DEALER | ROUTER | STREAM | Gateway | Discovery | Spot | SpotNode |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `zlink_set_option` | set | set | set | set | set | set | set† | set⁂ | set | set |
| `zlink_get_option` | get | get | get | get | get | get | get† | - | get | get |
| `zlink_set_routing_id` | set | set | set | set | set | EINVAL§ | set | set | set | set |
| `zlink_get_routing_id` | get | get | get | get | get | EINVAL§ | get | get | get | get |
| `zlink_set_router_option` | - | - | - | set‡ | set | - | set | - | - | - | - | - |
| `zlink_get_router_option` | - | - | - | get‡ | get | - | get | - | - | - | - | - |
| `zlink_set_dealer_option` | - | - | - | set | - | - | - | - | - | - | - | - |
| `zlink_set_stream_option` | - | - | - | - | - | set | - | - | - | - | - | - |
| `zlink_get_stream_option` | - | - | - | - | - | get | - | - | - | - | - | - |
| `zlink_set_pub_option` | - | set | - | - | - | - | - | - | set | set |
| `zlink_get_pub_option` | - | get | - | - | - | - | - | - | get | get |
| `zlink_set_sub_option` | - | - | set | - | - | - | - | - | set | set |
| `zlink_get_sub_option` | - | - | get | - | - | - | - | - | get | get |
| `zlink_set_subscription` | - | - | set | - | - | - | - | - | set | set |
| `zlink_unset_subscription` | - | - | set | - | - | - | - | - | set | set |
| `zlink_subscription_at` | - | - | get | - | - | - | - | - | get | get |

- †: gateway 지원 옵션만 (SNDHWM, RCVHWM, SNDTIMEO, LINGER, SNDBUF, RCVBUF, LAST_ENDPOINT(get))
- ‡: DEALER는 PROBE만 허용, 나머지는 EINVAL
- §: STREAM은 TCP 연결 주소를 routing identity로 사용하므로 사용자 지정 불가
- ⁂: discovery는 managed socket set에 fan-out 적용. getter 없음 (단일 source-of-truth 없음)
- Spot: unified `spot` handle
- SpotNode: unified `spot_node` handle

#### 3.3.2 제약

| 옵션 | 제약 |
| --- | --- |
| `ZLINK_OPT_LAST_ENDPOINT` | get-only. set 시 EINVAL |

#### 3.3.2.1 TLS 전용 함수 지원

TLS는 option matrix와 별도 계약을 가진다.

| 함수 | Gateway | Discovery | SpotPub | SpotSub | SpotNodePub | SpotNodeSub | Registry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `zlink_set_tls_server` | set | - | set | - | set | - | set |
| `zlink_set_tls_client` | set | set | set | set | set | set | set |

- `zlink_set_tls_server`는 server 역할 transport에만 허용한다.
- `zlink_set_tls_client`는 client 역할 transport에 허용한다.
- `Registry`도 이번 범위에 포함하며, 기존 내부 socket role 설정을 TLS 전용 함수로 흡수한다.

#### 3.3.3 getter source-of-truth

| 핸들 | 함수 | get 가능 옵션 | 값 원천 |
| --- | --- | --- | --- |
| raw socket | `zlink_get_option` | 모든 공통 옵션 | live socket `getsockopt()` |
| raw socket | `zlink_get_pub_option` | 해당 raw PUB/XPUB 소켓에 유효한 공통 옵션 + `TOPICS_COUNT` | live socket `getsockopt()` |
| raw socket | `zlink_get_sub_option` | 해당 raw SUB/XSUB 소켓에 유효한 공통 옵션 + `TOPICS_COUNT` | live socket `getsockopt()` |
| gateway | `zlink_get_option` | SNDHWM, RCVHWM, SNDTIMEO, LINGER, SNDBUF, RCVBUF, LAST_ENDPOINT | `gateway_t::get_socket_option()` (신규) / `last_endpoint()` |
| gateway | `zlink_get_routing_id` | routing_id | `gateway_t::routing_id()` |
| gateway | `zlink_get_router_option` | MANDATORY, HANDOVER | `gateway_t::get_socket_option()` |
| discovery | `zlink_get_routing_id` | routing_id | `discovery_t::routing_id()` |
| unified spot | `zlink_get_option` | 공통 옵션 | 해당 live side socket |
| unified spot | `zlink_get_pub_option` | pub 전용 옵션 + `TOPICS_COUNT` | live pub socket |
| unified spot | `zlink_get_sub_option` | sub 전용 옵션 + `TOPICS_COUNT` | live sub socket |
| unified spot | `zlink_subscription_at` | 등록된 subscribe/pattern 필터 집합 | subscription shadow state |
| unified spot | `zlink_get_routing_id` | routing_id | publish side routing_id |
| unified spot_node | `zlink_get_option` | 공통 옵션 | 해당 live side socket |
| unified spot_node | `zlink_get_pub_option` | pub 전용 옵션 + `TOPICS_COUNT` | live pub socket |
| unified spot_node | `zlink_get_sub_option` | sub 전용 옵션 + `TOPICS_COUNT` | live sub socket |
| unified spot_node | `zlink_subscription_at` | 등록된 subscribe/pattern 필터 집합 | subscription shadow state |
| unified spot_node | `zlink_get_routing_id` | routing_id | publish side routing_id |

#### 3.3.4 lifecycle 계약

| 핸들 상태 | set 동작 | get 동작 |
| --- | --- | --- |
| **unified spot** + `zlink_set_option(spot, 공통옵션)` | 의미에 맞는 live side socket에 직접 적용 | 해당 live side socket에서 읽음 |
| **unified spot** + `zlink_set_pub_option(spot, pub 전용)` | live pub socket에 직접 적용 | live pub socket에서 읽음 |
| **unified spot** + `zlink_set_sub_option(spot, sub 전용)` | live sub socket에 직접 적용 | live sub socket에서 읽음 |
| **unified spot_node** + `zlink_set_option(node, 공통옵션)` | 의미에 맞는 live side socket에 직접 적용 | 해당 live side socket에서 읽음 |
| **모든 sub-capable handle** + `zlink_set_subscription` / `zlink_unset_subscription` | live sub socket에 직접 적용 | `zlink_subscription_at`로 별도 조회 |
| **gateway bind 전** + `zlink_get_option(gw, LAST_ENDPOINT)` | N/A | ENOTSUP (router socket 미생성) |
| **gateway bind 전** + `zlink_set_routing_id(gw, ...)` | 저장, bind 시 적용 (기존 동작) | 저장된 값 반환 |

#### 3.3.5 spot/spot_node 허용 옵션 목록

unified `spot` / `spot_node`에 대한 typed option은
`zlink_option_t` 전체를 무차별 허용하지 않고, 내부 pub/sub 구현이 실제로
지원하는 옵션만 허용한다. 미지원 옵션은 EINVAL을 반환한다.

공통 옵션은 `zlink_set_option()` / `zlink_get_option()`으로 설정한다.
pub/sub 전용 옵션만 `zlink_set_pub_option()` / `zlink_set_sub_option()`으로 설정한다.

**pub side 허용 공통 옵션** (`zlink_set_option(handle, ...)`):

| 공통 옵션 | 비고 |
| --- | --- |
| `ZLINK_OPT_SNDHWM` | |
| `ZLINK_OPT_SNDTIMEO` | |
| `ZLINK_OPT_LINGER` | |
| `ZLINK_OPT_SNDBUF` | |
| `ZLINK_OPT_RCVBUF` | |

**pub side 허용 전용 옵션**:

| pub 전용 옵션 | 비고 |
| --- | --- |
| `ZLINK_PUB_OPT_NODROP` | |
| `ZLINK_PUB_OPT_VERBOSE` | raw XPUB만 |
| `ZLINK_PUB_OPT_VERBOSER` | raw XPUB만 |
| `ZLINK_PUB_OPT_MANUAL` | raw XPUB만 |
| `ZLINK_PUB_OPT_TOPICS_COUNT` | get-only |

**sub side 허용 공통 옵션** (`zlink_set_option(handle, ...)`):

| 공통 옵션 | 비고 |
| --- | --- |
| `ZLINK_OPT_RCVHWM` | |
| `ZLINK_OPT_RCVTIMEO` | |
| `ZLINK_OPT_LINGER` | |
| `ZLINK_OPT_SNDBUF` | |
| `ZLINK_OPT_RCVBUF` | |

**sub side 허용 전용 옵션**:

| sub 전용 옵션 | 비고 |
| --- | --- |
| `ZLINK_SUB_OPT_TOPICS_COUNT` | get-only |

등록된 구독 필터 문자열 목록은 `zlink_get_sub_option()`으로 조회하지 않는다.
subscription 명령/조회는 아래 별도 함수로 제공한다.

- `zlink_set_subscription(handle, filter)`
- `zlink_unset_subscription(handle, filter)`
- `zlink_subscription_at(handle, index, buf, &len, &is_pattern)`

subscription query 계약:

- 조회 대상은 `SUB`, `XSUB`, unified `spot`, unified `spot_node`를 포함한다.
- 반환 순서는 사전순 정렬을 사용한다. 내부 삽입 순서를 public 계약으로 노출하지 않는다.
- pattern은 원문 형식으로 반환한다. 예를 들어 prefix `"market."` 패턴은
  조회 시 `"market.*"`로 반환한다.
- 중복 필터는 collapse된다. 동일 filter를 여러 번 subscribe해도 조회 결과는
  하나만 노출한다.
- `zlink_subscription_at()`에서 `index_`가 범위를 벗어나면 `-1`과
  `errno=ENOENT`를 반환한다.
- `zlink_subscription_at()`에서 버퍼가 부족하면 `-1`과 `errno=EINVAL`을 반환하고,
  필요한 길이를 `filter_len_inout_`에 기록한다.

참조: `spot_pub_t::set_option()` (`spot_pub.cpp:269`),
`spot_sub_t::set_option()` (`spot_sub.cpp:490`).

### 3.4 사용 예시

```c
/* ── 공통 옵션 (raw socket) ── */
int sndhwm = 5000;
zlink_set_option (router, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
zlink_set_option (gateway, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));

/* ── 공통 옵션 (discovery — fan-out) ── */
int hb_ivl = 2000;
zlink_set_option (discovery, ZLINK_OPT_HEARTBEAT_IVL,
                  &hb_ivl, sizeof (hb_ivl));

/* ── LAST_ENDPOINT (get-only) ── */
char endpoint[256];
size_t ep_len = sizeof (endpoint);
zlink_get_option (router,  ZLINK_OPT_LAST_ENDPOINT, endpoint, &ep_len);
zlink_get_option (gateway, ZLINK_OPT_LAST_ENDPOINT, endpoint, &ep_len);

/* ── Routing ID ── */
const char *rid = "node-01";
zlink_set_routing_id (router,    rid, strlen (rid));
zlink_set_routing_id (gateway,   rid, strlen (rid));
zlink_set_routing_id (discovery, rid, strlen (rid));

/* ── Routing ID (unified spot) ── */
zlink_set_routing_id (spot, "spot-01", 7);
zlink_routing_id_t out;
zlink_get_routing_id (spot, &out);

/* ── Router 전용 (raw socket + gateway) ── */
int mandatory = 1;
zlink_set_router_option (router,  ZLINK_ROUTER_OPT_MANDATORY,
                         &mandatory, sizeof (mandatory));
zlink_set_router_option (gateway, ZLINK_ROUTER_OPT_MANDATORY,
                         &mandatory, sizeof (mandatory));

/* ── Pub 옵션 (raw socket) ── */
int verbose = 1;
zlink_set_pub_option (xpub, ZLINK_PUB_OPT_VERBOSE,
                      &verbose, sizeof (verbose));

/* ── unified spot: 공통 pub-side 옵션 ── */
int spot_sndhwm = 2000;
zlink_set_option (spot, ZLINK_OPT_SNDHWM,
                  &spot_sndhwm, sizeof (spot_sndhwm));
int spot_linger = 500;
zlink_set_option (spot, ZLINK_OPT_LINGER,
                  &spot_linger, sizeof (spot_linger));

/* ── unified spot: 공통 sub-side 옵션 ── */
int spot_rcvhwm = 3000;
zlink_set_option (spot, ZLINK_OPT_RCVHWM,
                  &spot_rcvhwm, sizeof (spot_rcvhwm));

/* ── unified spot: 구독 필터 ── */
zlink_set_subscription (spot, "market.AAPL");

/* ── unified spot_node: 자체 pub socket 공통 옵션 ── */
int node_sndhwm = 4000;
zlink_set_option (node, ZLINK_OPT_SNDHWM,
                  &node_sndhwm, sizeof (node_sndhwm));
```

### 3.5 unified spot / spot_node 유지

이번 방향에서는 기존 unified `spot` / `spot_node` public API를 유지한다.
typed option 도입은 surface 분리가 아니라 옵션 체계 정리에 집중한다.

#### 3.5.1 기존 unified API inventory

| 기존 API | 현재 의미 | 새 모델에서의 처리 |
| --- | --- | --- |
| `zlink_spot_new()` | unified spot 생성 | **유지** |
| `zlink_spot_destroy()` | unified spot 파괴 | **유지** |
| `zlink_socket_send_ready_handler()` | raw socket send-ready 설정 | **delete** → `zlink_send_ready_handler(socket, ...)` |
| `zlink_gateway_send_ready_handler()` | gateway send-ready 설정 | **delete** → `zlink_send_ready_handler(gateway, ...)` |
| `zlink_spot_publish()` | unified spot에서 pub 송신 | **delete** → `zlink_publish(spot, ...)` |
| `zlink_spot_sub_recv()` | unified spot에서 sub 수신 | **delete** → `zlink_subscribe(spot, ...)` |
| `zlink_spot_subscribe()` | unified spot에서 subscribe | **delete** → `zlink_set_subscription(spot, filter)` |
| `zlink_spot_subscribe_pattern()` | unified spot에서 pattern subscribe | **delete** → `zlink_set_subscription(spot, pattern)` |
| `zlink_spot_unsubscribe()` | unified spot에서 unsubscribe | **delete** → `zlink_unset_subscription(spot, filter)` |
| `zlink_spot_send_ready_handler()` | unified spot에서 pub send-ready | **delete** → `zlink_send_ready_handler(spot, ...)` |
| `zlink_spot_monitor_open(role)` | role 인자로 side 선택 | **유지** |
| `zlink_spot_node_new()` | unified spot_node 생성 | **유지** |
| `zlink_spot_node_destroy()` | unified spot_node 파괴 | **유지** |
| `zlink_spot_node_bind()` | unified node bind | **유지** |
| `zlink_spot_node_publish()` | unified node에서 pub 송신 | **delete** → `zlink_publish(node, ...)` |
| `zlink_spot_node_recv()` | unified node에서 sub 수신 | **delete** → `zlink_subscribe(node, ...)` |
| `zlink_spot_node_subscribe()` | unified node에서 subscribe | **delete** → `zlink_set_subscription(node, filter)` |
| `zlink_spot_node_subscribe_pattern()` | unified node에서 pattern subscribe | **delete** → `zlink_set_subscription(node, pattern)` |
| `zlink_spot_node_unsubscribe()` | unified node에서 unsubscribe | **delete** → `zlink_unset_subscription(node, filter)` |
| `zlink_spot_node_send_ready_handler()` | unified node에서 pub send-ready | **delete** → `zlink_send_ready_handler(node, ...)` |
| `zlink_spot_node_monitor_open(role)` | role 인자로 side 선택 | **유지** |
| `zlink_spot_node_connect_peer()` | unified node peer pub 연결 | **유지** |
| `zlink_spot_node_disconnect_peer()` | unified node peer pub 해제 | **유지** |
| `zlink_spot_node_attach_discovery()` | unified node에 discovery attach | **유지** |
| `zlink_spot_node_set_tls_server()` | unified node TLS server 설정 | **replace** → `zlink_set_tls_server(handle, ...)` |
| `zlink_spot_node_set_tls_client()` | unified node TLS client 설정 | **replace** → `zlink_set_tls_client(handle, ...)` |

send-ready는 socket/service별 전용 entry를 없애고 `zlink_send_ready_handler()` 하나로 통합한다.

#### 3.5.2 role 기반 API 유지

`zlink_spot_role_t`, `zlink_spot_monitor_open()`, `zlink_spot_node_monitor_open()`
같이 role 인자로 side를 선택하는 API는 unified public surface를 유지하는 한
유효하다. role 인자를 제거하지 않는다.

#### 3.5.3 옵션 API 삭제 대상

unified surface 유지 기준에서도 아래 service-specific option wrapper는
public API에서 삭제한다:

| 삭제 대상 (옵션 API) | 대체 |
| --- | --- |
| `zlink_spot_node_set_pub_option(node, opt, ...)` | `zlink_set_pub_option(node, opt, ...)` |
| `zlink_spot_node_set_sub_option(node, opt, ...)` | `zlink_set_sub_option(node, opt, ...)` |
| `zlink_spot_set_pub_option(spot, opt, ...)` | `zlink_set_pub_option(spot, opt, ...)` |
| `zlink_spot_set_sub_option(spot, opt, ...)` | `zlink_set_sub_option(spot, opt, ...)` |
| `zlink_spot_subscribe(spot, filter)` | `zlink_set_subscription(spot, filter)` |
| `zlink_spot_subscribe_pattern(spot, pattern)` | `zlink_set_subscription(spot, pattern)` |
| `zlink_spot_unsubscribe(spot, filter)` | `zlink_unset_subscription(spot, filter)` |
| `zlink_spot_publish(spot, ...)` | `zlink_publish(spot, ...)` |
| `zlink_spot_sub_recv(spot, ...)` | `zlink_subscribe(spot, ...)` |
| `zlink_spot_node_subscribe(node, filter)` | `zlink_set_subscription(node, filter)` |
| `zlink_spot_node_subscribe_pattern(node, pattern)` | `zlink_set_subscription(node, pattern)` |
| `zlink_spot_node_unsubscribe(node, filter)` | `zlink_unset_subscription(node, filter)` |
| `zlink_spot_node_publish(node, ...)` | `zlink_publish(node, ...)` |
| `zlink_spot_node_recv(node, ...)` | `zlink_subscribe(node, ...)` |

## 4. Internal Design

### 4.1 핸들 타입 감지

기존 tag 기반 `check_tag()` 패턴을 활용한다.

| 타입 | Tag 값 | 위치 |
| --- | --- | --- |
| `socket_base_t` | `0xbaddecaf` | `core/src/sockets/socket_base.cpp:150` |
| `gateway_t` | `0x1e6700d7` | `core/src/services/gateway/gateway.cpp:32` |
| `discovery_t` | `0x1e6700d6` | `core/src/services/discovery/discovery.cpp:38` |
| `spot_handle_t` / `spot_node_t` 감지 경로 | 기존 확장 | `core/src/api/zlink.cpp`, `core/src/api/zlink_option.cpp` |

```
zlink_set_option(handle, option, val, len)
  ├─ as_socket_handle(handle) → socket->setsockopt(map_common_option(option), ...)
  ├─ as_gateway_service(handle) → gateway->set_socket_option(...)
  ├─ as_discovery_service(handle) → managed socket set에 fan-out 적용
  ├─ as_spot_handle(handle) → 의미에 맞는 live pub/sub socket에 적용
  ├─ as_spot_node(handle) → 의미에 맞는 live pub/sub socket에 적용
  └─ 그 외 → EFAULT

zlink_set_pub_option(handle, option, val, len)
  ├─ pub 전용 enum(zlink_pub_option_t) 검증
  ├─ as_socket_handle(handle) → PUB/XPUB 타입 검증 → setsockopt
  ├─ as_spot_handle(handle) → live pub socket에 직접 적용
  ├─ as_spot_node(handle) → live pub socket에 직접 적용
  └─ 그 외 → EFAULT

zlink_set_routing_id(handle, data, size)
  ├─ as_socket_handle(handle) → STREAM이면 EINVAL → setsockopt
  ├─ as_gateway_service(handle) → gateway->set_routing_id(...)
  ├─ as_discovery_service(handle) → discovery->set_routing_id(...)
  ├─ as_spot_handle(handle) → unified spot publish side에 직접 설정
  ├─ as_spot_node(handle) → unified spot_node publish side에 직접 설정
  └─ 그 외 → EFAULT
```

### 4.2 옵션 값 매핑

새 enum 값(0x3xxx)을 내부 legacy 옵션 값으로 변환하는 매핑 함수를 추가한다.

```c
static int map_common_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:  return ZLINK_SNDHWM;
        case ZLINK_OPT_RCVHWM:  return ZLINK_RCVHWM;
        case ZLINK_OPT_LINGER:  return ZLINK_LINGER;
        /* ... */
        default: errno = EINVAL; return -1;
    }
}

static int map_router_option (zlink_router_option_t option_)
{
    switch (option_) {
        case ZLINK_ROUTER_OPT_MANDATORY: return ZLINK_ROUTER_MANDATORY;
        case ZLINK_ROUTER_OPT_HANDOVER:  return ZLINK_ROUTER_HANDOVER;
        case ZLINK_ROUTER_OPT_PROBE:     return ZLINK_PROBE_ROUTER;
        case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
            return ZLINK_CONNECT_ROUTING_ID;
        default: errno = EINVAL; return -1;
    }
}
```

pub/sub 함수는 공통 옵션을 같이 받지 않는다.

- `zlink_set_option()` / `zlink_get_option()`은 `zlink_option_t`만 수용한다.
- `zlink_set_pub_option()` / `zlink_get_pub_option()`은 `zlink_pub_option_t`만 수용한다.
- `zlink_set_sub_option()` / `zlink_get_sub_option()`은 `zlink_sub_option_t`만 수용한다.

### 4.3 소켓 타입 검증

타입별 함수는 소켓 타입을 검증한다.

```c
int zlink_set_pub_option (void *handle_, zlink_pub_option_t option_, ...)
{
    int legacy = map_pub_option (option_);
    if (legacy < 0) return -1;

    /* 1. raw socket */
    socket_handle_t sh = as_socket_handle (handle_);
    if (sh.socket) {
        int type = sh.socket->get_socket_type ();
        if (type != ZLINK_PUB && type != ZLINK_XPUB) {
            errno = EINVAL;
            return -1;
        }
        return sh.socket->setsockopt (legacy, optval_, optvallen_);
    }

    /* 2. unified spot / spot_node */
    if (as_spot_handle (handle_))
        return apply_spot_pub_option (handle_, option_, optval_, optvallen_);
    if (as_spot_node (handle_))
        return apply_spot_node_pub_option (handle_, option_, optval_, optvallen_);

    errno = EFAULT;
    return -1;
}
```

### 4.4 gateway/spot/spot_node 내부 라우팅

**핵심 원칙: 새 public API는 각 서비스의 기존 내부 메서드로 위임한다.**

| 핸들 | 위임 대상 | 비고 |
| --- | --- | --- |
| gateway set (공통 옵션) | `gateway_t::set_socket_option()` | 기존 |
| gateway get (공통 옵션) | `gateway_t::get_socket_option()` | **신규** |
| gateway get (LAST_ENDPOINT) | `gateway_t::last_endpoint()` | 기존, 특례 경로 |
| gateway set/get (router 옵션) | `gateway_t::set/get_socket_option()` | 기존+신규 |
| gateway set/get (routing_id) | `gateway_t::set_routing_id()` / `routing_id()` | 기존 |
| discovery set/get (routing_id) | `discovery_t::set_routing_id()` / `routing_id()` | 기존 |
| unified spot pub set/get | `spot_pub_t::set_option()` / live getter | unified handle에서 pub side로 위임 |
| unified spot sub set/get (정수 옵션) | `spot_sub_t::set_option()` / live getter | unified handle에서 sub side로 위임 |
| unified spot sub set (SUBSCRIBE/UNSUBSCRIBE) | `spot_sub_t::subscribe()` / `unsubscribe()` | 기존 경로 재사용 |
| unified spot routing_id | publish side routing_id 경로 | 신규 public routing 연결 |
| unified spot_node pub set/get | `spot_node_t::set_pub_option()` / live getter | 기존, 자체 pub socket 직접 적용 |
| unified spot_node sub set/get (정수) | `spot_node_t::set_sub_option()` / live getter | 기존, 자체 sub socket 직접 적용 |
| unified spot_node sub set (SUBSCRIBE) | `spot_node_t` 자체 sub socket에 subscribe | 기존 경로 재사용 |
| unified spot_node routing_id | publish side routing_id 경로 | 신규 public routing 연결 |

#### 4.4.1 gateway getter 신규 추가

`gateway_t::get_socket_option(int option_, void *optval_, size_t *optvallen_)`을
신규 추가한다. 내부 router socket의 `getsockopt()`를 위임한다.
LAST_ENDPOINT는 `last_endpoint()` 특례 경로.

#### 4.4.2 SUBSCRIBE/UNSUBSCRIBE 경로

`zlink_set_sub_option()` 내부에서 option 값에 따라 분기:

- 정수형 옵션: 기존 `set_option()` 경로 재사용
- SUBSCRIBE/UNSUBSCRIBE: 문자열 filter 전용 경로
  - unified `spot`: `spot_sub->subscribe(...)` / `unsubscribe(...)`
  - unified `spot_node`: 자체 sub socket에 직접 subscribe / unsubscribe

#### 4.4.3 신규 shadow state 목록

| 대상 | 신규 state | 용도 |
| --- | --- | --- |
| `gateway_t` | `get_socket_option()` 메서드 | router socket getter 위임 |
| unified `spot` / `spot_node` helper | typed option 의미별 분기 경로 | unified handle에서 pub/sub 내부 적용 |

### 4.5 discovery 적용 규칙

discovery는 gateway/spot과 달리 단일 대표 socket이 아니라
SUB + 복수 DEALER socket 집합을 관리한다.
따라서 discovery option은 "특정 socket 하나의 typed option"이 아니라
"discovery transport option을 관리 중인 socket 집합에 fan-out 적용"하는
계약으로 정의한다.

이 문서에서 discovery는 다음만 지원한다:

- `zlink_set_option(discovery, ...)` — managed socket set에 fan-out
- `zlink_set_routing_id(discovery, ...)` / `zlink_get_routing_id(discovery, ...)`

`zlink_get_option(discovery, ...)`는 단일 source-of-truth가 없으므로 제공하지
않는다.

## 5. 삭제 대상 (하위 호환 없음)

기존 옵션 관련 함수/enum을 `zlink.h`에서 **삭제**한다.

### 5.1 삭제할 함수

| 삭제 대상 | 대체 |
| --- | --- |
| `zlink_setsockopt(sock, option, ...)` | `zlink_set_option` / `zlink_set_*_option` |
| `zlink_getsockopt(sock, option, ...)` | `zlink_get_option` / `zlink_get_*_option` |
| `zlink_subscribe(subject, filter)` | `zlink_set_subscription(subject, filter)` |
| `zlink_unsubscribe(subject, filter)` | `zlink_unset_subscription(subject, filter)` |
| `zlink_gateway_set_option(gw, opt, ...)` | `zlink_set_option(gw, ...)` |
| `zlink_gateway_set_routing_id(gw, data, size)` | `zlink_set_routing_id(gw, data, size)` |
| `zlink_gateway_routing_id(gw, &out)` | `zlink_get_routing_id(gw, &out)` |
| `zlink_gateway_last_endpoint(gw, buf, &size)` | `zlink_get_option(gw, ZLINK_OPT_LAST_ENDPOINT, buf, &size)` |
| `zlink_discovery_set_routing_id(disc, data, size)` | `zlink_set_routing_id(disc, data, size)` |
| `zlink_discovery_routing_id(disc, &out)` | `zlink_get_routing_id(disc, &out)` |
| 기존 서비스별 TLS 함수 (`zlink_gateway_set_tls_*`, `zlink_discovery_set_tls_client`, `zlink_spot_node_set_tls_*`, `zlink_registry_setsockopt(..., role, TLS_*, ...)`) | `zlink_set_tls_server` / `zlink_set_tls_client` |
| `zlink_spot_set_pub_option(spot, opt, ...)` | `zlink_set_pub_option(spot, opt, ...)` |
| `zlink_spot_set_sub_option(spot, opt, ...)` | `zlink_set_sub_option(spot, opt, ...)` |
| `zlink_spot_node_set_pub_option(node, opt, ...)` | `zlink_set_pub_option(node, opt, ...)` |
| `zlink_spot_node_set_sub_option(node, opt, ...)` | `zlink_set_sub_option(node, opt, ...)` |
| `zlink_spot_subscribe(spot, filter)` | `zlink_set_subscription(spot, filter)` |
| `zlink_spot_subscribe_pattern(spot, pattern)` | `zlink_set_subscription(spot, pattern)` |
| `zlink_spot_unsubscribe(spot, filter)` | `zlink_unset_subscription(spot, filter)` |
| `zlink_spot_publish(spot, ...)` | `zlink_publish(spot, ...)` |
| `zlink_spot_sub_recv(spot, ...)` | `zlink_subscribe(spot, ...)` |
| `zlink_spot_node_subscribe(node, filter)` | `zlink_set_subscription(node, filter)` |
| `zlink_spot_node_subscribe_pattern(node, pattern)` | `zlink_set_subscription(node, pattern)` |
| `zlink_spot_node_unsubscribe(node, filter)` | `zlink_unset_subscription(node, filter)` |
| `zlink_spot_node_publish(node, ...)` | `zlink_publish(node, ...)` |
| `zlink_spot_node_recv(node, ...)` | `zlink_subscribe(node, ...)` |

### 5.2 삭제할 enum

| 삭제 대상 | 대체 |
| --- | --- |
| `zlink_socket_option_t` (48개 멤버) | `zlink_option_t` + 타입별 enum 5개 + `zlink_set/get_routing_id` |
| `zlink_gateway_option_t` | `zlink_option_t` |
| `zlink_spot_pub_option_t` | `zlink_pub_option_t` |
| `zlink_spot_sub_option_t` | `zlink_sub_option_t` |

### 5.3 삭제할 internal alias

| 삭제 대상 | 비고 |
| --- | --- |
| `#define ZLINK_SUBSCRIBE ...` | 내부: `ZLINK_INTERNAL_OPT_SUBSCRIBE`로 치환 |
| `#define ZLINK_UNSUBSCRIBE ...` | 내부: `ZLINK_INTERNAL_OPT_UNSUBSCRIBE`로 치환 |
| `#define ZLINK_AFFINITY ...` ~ `#define ZLINK_TCP_NODELAY ...` | 약 48개 option alias |
| 소켓 타입 alias (`ZLINK_PAIR` ~ `ZLINK_STREAM`) | **유지** |

### 5.4 호출부 이관 범위

- `core/src/api/**` — 내부 `legacy_socket_option()` 및 관련 코드 삭제
- `core/src/services/**` — 내부 `zlink_setsockopt`/`zlink_getsockopt` 직접 호출 이관
- `core/src/services/registry/**` — registry TLS/option 호출 이관
- `core/src/sockets/**` — 내부 option alias 치환
- `core/tests/` — integration, unittest, e2e 테스트
- `core/perf/` — 성능 테스트
- `core/bench/` — 벤치마크 (zlink 측만, libzmq 측은 변경 없음)

## 6. subscribe-surface-renaming 플랜과의 관계

이 문서는 `subscribe-surface-renaming-plan.ko.md`의 subscribe 경로 통합을 **대체**한다.

| subscribe-surface-renaming 계획 | 이 문서의 대체 |
| --- | --- |
| `zlink_set_subscribe(subject, filter)` | `zlink_set_subscription(subject, filter)` |
| `zlink_unset_subscribe(subject, filter)` | `zlink_unset_subscription(subject, filter)` |

유지되는 항목: `zlink_subscribe` / `zlink_subscription_event` 이름 정리,
handler typedef/registration, 내부 `ZLINK_INTERNAL_OPT_SUBSCRIBE` 상수 전략.

XPUB manual mode subscription 승인/거부:
- 기존: `zlink_setsockopt(xpub, ZLINK_SUBSCRIBE, filter, len)`
- 새: `zlink_set_pub_option(xpub, ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, filter, len)`

## 7. 영향 범위

### 7.1 Public Header

- [`zlink.h`](core/include/zlink.h)
  - 새 enum 6개 추가 (§3.2)
  - 새 함수 선언 17개 추가 (§3.1)
  - unified `spot` / `spot_node` API와 generic typed option 연동 반영
  - 기존 enum/legacy 함수/option alias 삭제 (§5)

### 7.2 Core API 구현

- `core/src/api/zlink_option.cpp` (**신규**)
  - 17개 public 함수 구현
  - unified `spot` / `spot_node` + raw socket + 필요 시 side handle dispatch 로직
  - SUBSCRIBE/UNSUBSCRIBE direct live path 로직
  - TLS 전용 함수 dispatch 로직
  - discovery fan-out 로직

### 7.2.1 신규 내부 메서드

- `gateway_t::get_socket_option()` — 내부 router socket getter 위임 (**신규**)
- unified `spot` / `spot_node` typed option helper — pub/sub 의미별 내부 라우팅 (**신규/확장**)
- subscription query helper — 등록된 subscribe/pattern 필터 집합 export (**신규**)
- 서비스별 TLS apply helper — gateway/discovery/spot/spot_node/registry 공통 진입점 지원 (**신규/확장**)

### 7.3 Build

- [`core/CMakeLists.txt`](core/CMakeLists.txt) — `api-sources`에 새 파일 추가

### 7.4 Tests

- `core/tests/unittest/unittest_typed_option.cpp` (**신규**)
- [`core/tests/CMakeLists.txt`](core/tests/CMakeLists.txt) — 테스트 등록

## 8. 실행 단계

### Phase 1. raw socket typed option

목표: 새 enum 6개 + `zlink_set_option`/`zlink_get_option`(raw socket) 구현.

작업:
- `zlink.h`에 enum 6개 + 함수 선언 추가
- `core/src/api/zlink_option.cpp` 신규 생성
- `map_common_option()` switch 구현
- `ZLINK_OPT_LAST_ENDPOINT` 포함

완료 기준:
- `zlink_set_option(router, ZLINK_OPT_SNDHWM, ...)` 동작
- `zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, ...)` 동작
- `core/` 빌드 성공

### Phase 2. raw socket 타입별 옵션 + routing_id

목표: router/dealer/stream/pub/sub 옵션 함수 + routing_id 전용 함수 구현.

작업:
- 10개 함수 선언 + 구현 추가
- 각 함수에서 소켓 타입 검증
- `zlink_set_routing_id`: STREAM 소켓 검사 → EINVAL

완료 기준:
- `zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, ...)` 동작
- `zlink_set_pub_option(xpub, ZLINK_PUB_OPT_VERBOSE, ...)` 동작
- `zlink_set_routing_id(router, "node-01", 7)` 동작
- `zlink_set_routing_id(stream, ...)` → EINVAL
- `core/` 빌드 성공

### Phase 3. gateway 통합

목표: gateway에서 공통 옵션 + router 옵션 + routing_id + TLS + LAST_ENDPOINT 동작.

작업:
- `gateway_t::get_socket_option()` **신규 메서드 추가**
- `zlink_set_option()`/`zlink_get_option()`에 gateway 핸들 분기
- `zlink_set_router_option()`/`zlink_get_router_option()`에 gateway 핸들 분기
- `zlink_set_routing_id()`/`zlink_get_routing_id()`에 gateway 핸들 분기
- `zlink_set_tls_server()`/`zlink_set_tls_client()`에 gateway 핸들 분기

완료 기준:
- `zlink_set_option(gateway, ZLINK_OPT_SNDHWM, ...)` 동작
- `zlink_get_option(gateway, ZLINK_OPT_SNDHWM, ...)` 동작
- `zlink_get_option(gateway, ZLINK_OPT_LAST_ENDPOINT, ...)` 동작
- `zlink_set_router_option(gateway, ZLINK_ROUTER_OPT_MANDATORY, ...)` 동작
- `zlink_set_routing_id(gateway, ...)` 동작
- `zlink_set_tls_server(gateway, ...)` / `zlink_set_tls_client(gateway, ...)` 동작
- `core/` 빌드 성공

### Phase 4. unified spot typed option 연결

목표: unified `spot`에서 generic typed option을 직접 사용하도록 연결한다.

작업:
- `zlink_set_option()` / `zlink_get_option()`에 unified `spot` 핸들 분기 추가
- `zlink_set_pub_option()` / `zlink_get_pub_option()`에 unified `spot` 핸들 분기 추가
- `zlink_set_sub_option()` / `zlink_get_sub_option()`에 unified `spot` 핸들 분기 추가
- `zlink_set_subscription()` / `zlink_unset_subscription()`에 unified `spot` 분기 추가
- `zlink_subscription_at()`에 unified `spot` 분기 추가
- `zlink_publish()` / `zlink_subscribe()`에 unified `spot` direct path 정렬
- `zlink_set_routing_id(spot, ...)` / `zlink_get_routing_id(spot, ...)` 경로 정리
- `zlink_spot_publish()` / `zlink_spot_sub_recv()` 삭제
- `zlink_spot_set_pub_option()` / `zlink_spot_set_sub_option()` 삭제
- `zlink_spot_subscribe()` / `zlink_spot_subscribe_pattern()` / `zlink_spot_unsubscribe()` 삭제

완료 기준:
- `zlink_set_pub_option(spot, ZLINK_PUB_OPT_NODROP, ...)` 동작
- `zlink_set_option(spot, ZLINK_OPT_SNDHWM, ...)` 동작
- `zlink_set_option(spot, ZLINK_OPT_RCVHWM, ...)` 동작
- `zlink_set_subscription(spot, ...)` / `zlink_unset_subscription(spot, ...)` 동작
- `zlink_publish(spot, ...)` / `zlink_subscribe(spot, ...)` 동작
- `zlink_subscription_at(spot, ...)` 동작
- `zlink_set_routing_id(spot, ...)` / `zlink_get_routing_id(spot, ...)` 동작
- service-specific spot wrapper 삭제가 문서와 코드에 반영됨
- `core/` 빌드 성공

### Phase 5. unified spot_node typed option 연결

목표: unified `spot_node`에서 generic typed option을 직접 사용하도록 연결한다.

작업:
- `zlink_set_option()` / `zlink_get_option()`에 unified `spot_node` 핸들 분기 추가
- `zlink_set_pub_option()` / `zlink_get_pub_option()`에 unified `spot_node` 핸들 분기 추가
- `zlink_set_sub_option()` / `zlink_get_sub_option()`에 unified `spot_node` 핸들 분기 추가
- `zlink_set_subscription()` / `zlink_unset_subscription()`에 unified `spot_node` 분기 추가
- `zlink_subscription_at()`에 unified `spot_node` 분기 추가
- `zlink_publish()` / `zlink_subscribe()`에 unified `spot_node` direct path 정렬
- `zlink_set_routing_id(node, ...)` / `zlink_get_routing_id(node, ...)` 경로 정리
- `zlink_spot_node_publish()` / `zlink_spot_node_recv()` 삭제
- `zlink_spot_node_set_pub_option()` / `zlink_spot_node_set_sub_option()` 삭제
- `zlink_spot_node_subscribe()` / `zlink_spot_node_subscribe_pattern()` / `zlink_spot_node_unsubscribe()` 삭제

완료 기준:
- `zlink_set_option(node, ZLINK_OPT_SNDHWM, ...)` 자체 pub socket에 적용
- `zlink_get_option(node, ZLINK_OPT_SNDHWM, ...)` 자체 pub socket에서 읽음
- `zlink_set_subscription(node, ...)` / `zlink_unset_subscription(node, ...)` 동작
- `zlink_publish(node, ...)` / `zlink_subscribe(node, ...)` 동작
- `zlink_subscription_at(node, ...)` 동작
- child `spot` 생성 시 node option/filter가 자동 복제되지 않음
- `zlink_set_routing_id(node, ...)` / `zlink_get_routing_id(node, ...)` 동작
- `zlink_set_tls_server(node, ...)` / `zlink_set_tls_client(node, ...)` 동작
- service-specific spot_node option wrapper 삭제가 문서와 코드에 반영됨
- `core/` 빌드 성공

### Phase 6. discovery / registry transport + TLS 통합

목표: discovery/registry에서 transport option 및 TLS 전용 함수 동작.

작업:
- `zlink_set_option()`에 discovery 핸들 분기
  - managed socket set에 fan-out 적용
- `zlink_get_option(discovery, ...)` → ENOTSUP
- `zlink_set_routing_id()`/`zlink_get_routing_id()`에 discovery 핸들 분기
- `zlink_set_tls_client()`에 discovery 핸들 분기
- `zlink_set_tls_server()`/`zlink_set_tls_client()`에 registry 핸들 분기

완료 기준:
- `zlink_set_option(discovery, ZLINK_OPT_HEARTBEAT_IVL, ...)` fan-out 동작
- `zlink_get_option(discovery, ...)` → ENOTSUP
- `zlink_set_routing_id(discovery, ...)` 동작
- `zlink_set_tls_client(discovery, ...)` 동작
- `zlink_set_tls_server(registry, ...)` / `zlink_set_tls_client(registry, ...)` 동작
- `core/` 빌드 성공

### Phase 7. legacy API 삭제 + 호출부 이관

목표: §5의 삭제 대상 함수/enum/alias 제거 + 모든 호출부 이관.

작업:
- `zlink.h`에서 기존 함수 선언, enum, option alias 삭제
- `core/src/api/zlink.cpp`에서 기존 함수 정의 삭제
- `core/src/services/**` 내부 호출부 이관
- `core/tests/**`, `core/perf/**`, `core/bench/**` 호출부 이관
- 내부 option alias 치환 (`ZLINK_SUBSCRIBE` → `ZLINK_INTERNAL_OPT_SUBSCRIBE`)
- API 문서 갱신

완료 기준:
- `zlink.h`에 §5의 삭제 대상이 존재하지 않는다
- `core/src/**` 내부에 legacy public option API 직접 호출이 없다
- `core/` 전체 빌드와 테스트 통과

### Phase 8. 테스트/문서 마감

목표: 전체 (핸들 × 옵션 그룹) 조합 검증.

작업:
- `core/tests/unittest/unittest_typed_option.cpp` 신규 생성:
  - `test_set_option_on_all_socket_types`: raw socket 공통 옵션 set/get
  - `test_get_option_last_endpoint`: socket/gateway LAST_ENDPOINT get
  - `test_routing_id_set_get`: socket/gateway/discovery routing_id
  - `test_routing_id_stream_rejected`: STREAM EINVAL
  - `test_routing_id_spot_unified`: unified `spot` / `spot_node` routing_id 검증
  - `test_router_option_type_check`: ROUTER/DEALER/gateway router 옵션
  - `test_tls_dedicated_api`: gateway/discovery/spot/spot_node/registry TLS 전용 함수 동작
  - `test_stream_option_set_get`: STREAM set/get 대칭
  - `test_raw_pub_sub_common_getter`: raw PUB/XPUB, SUB/XSUB에서 side 공통 getter 동작
  - `test_spot_pub_option`: unified `spot` pub 전용 옵션
  - `test_spot_sub_option`: unified `spot` sub 전용 옵션
  - `test_spot_set_unset_subscription`: unified `spot` subscription 명령
  - `test_spot_subscription_query`: unified `spot` 등록 필터 조회
  - `test_spotnode_pub_option`: unified `spot_node` pub 전용 옵션
  - `test_spotnode_sub_option`: unified `spot_node` sub 전용 옵션
  - `test_spotnode_set_unset_subscription`: unified `spot_node` subscription 명령
  - `test_spotnode_subscription_query`: unified `spot_node` 등록 필터 조회
  - `test_spotnode_child_independent_options`: child spot 생성 시 node option/filter 비복제 확인
  - `test_spot_wrapper_removed`: `zlink_spot_*` data-plane/option wrapper 제거 검증
  - `test_discovery_fanout`: discovery set_option fan-out
  - `test_discovery_get_rejected`: discovery get_option ENOTSUP
  - `test_gateway_getter`: gateway get_option + bind 전/후

완료 기준:
- `ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)` 통과
- `ctest --test-dir core/build --output-on-failure -L integration -j1` 회귀 없음

## 9. 재사용할 기존 코드

| 코드 | 위치 | 용도 |
| --- | --- | --- |
| `legacy_socket_option()` | `core/src/api/zlink.cpp:2036` | 옵션 값 변환 switch 패턴 참조 |
| `as_socket_handle()` | `core/src/api/zlink.cpp:1590` | 소켓 핸들 감지 |
| unified spot/spot_node helper | `core/src/api/zlink_option.cpp` | unified handle typed option 라우팅 |
| `as_gateway_service()` | `core/src/api/zlink.cpp:1950` | gateway 핸들 감지 |
| `discovery_t::check_tag()` | `core/src/services/discovery/discovery.cpp:306` | discovery 핸들 감지 |
| `spot_node_t::check_tag()` | `core/src/services/spot/spot_node.cpp` | spot_node 핸들 감지 |
| `gateway_t::set_socket_option()` | `core/src/services/gateway/gateway.cpp:1881` | gateway setter |
| `gateway_t::set_routing_id()` | `core/src/services/gateway/gateway.cpp` | gateway routing ID |
| `gateway_t::last_endpoint()` | `core/src/services/gateway/gateway.cpp` | gateway last endpoint |
| `discovery_t::set_routing_id()` | `core/src/services/discovery/discovery.cpp` | discovery routing ID |
| `spot_pub_t::set_option()` | `core/src/services/spot/spot_pub.cpp:269` | spot pub 옵션 |
| `spot_sub_t::set_option()` | `core/src/services/spot/spot_sub.cpp:490` | spot sub 옵션 |
| `spot_node_t::set_pub_option()` | `core/src/services/spot/spot_node.cpp` | unified spot_node pub socket 옵션 |
| `spot_node_t::set_sub_option()` | `core/src/services/spot/spot_node.cpp` | unified spot_node sub socket 옵션 |

## 10. 수정 대상 파일 요약

| 파일 | 변경 | Phase |
| --- | --- | --- |
| `core/include/zlink.h` | enum 6개 추가, 함수 17개 추가, unified spot/spot_node와 generic typed option 정렬, legacy enum/function/alias 삭제 | 1-2, 7 |
| `core/src/api/zlink_option.cpp` | **신규** — 새 API 전체 구현 | 1-6 |
| `core/src/api/zlink.cpp` | 기존 함수 정의/`legacy_socket_option()` 삭제 | 7 |
| `core/CMakeLists.txt` | `api-sources`에 새 파일 추가 | 1 |
| `core/src/services/gateway/gateway.cpp` | `get_socket_option()` **신규**, TLS 전용 함수 분기 반영 | 3 |
| `core/src/services/gateway/gateway.hpp` | `get_socket_option()` 선언 추가 | 3 |
| `core/src/services/spot/spot_node.cpp` | unified spot_node typed option 연결, discovery/peer/bind, TLS 전용 함수 분기 반영 | 5 |
| `core/src/services/spot/spot_node.hpp` | unified typed option helper 선언 추가 | 5 |
| `core/src/services/discovery/discovery.cpp` | TLS 전용 함수 및 내부 호출부 이관 | 6-7 |
| `core/src/services/registry/**` | registry TLS 전용 함수 및 내부 호출부 이관 | 6-7 |
| `core/src/sockets/sub.cpp` | 내부 option alias 치환 | 7 |
| `core/src/sockets/xpub.cpp` | 내부 option alias 치환 | 7 |
| `core/src/services/spot/spot_sub.cpp` | 내부 option alias 치환 | 7 |
| `core/tests/**` | 기존 API → 새 API 변환 | 7 |
| `core/perf/**` | 기존 API → 새 API 변환 | 7 |
| `core/bench/**` | zlink 측 기존 API → 새 API 변환 | 7 |
| `core/tests/unittest/unittest_typed_option.cpp` | **신규** — 테스트 | 8 |
| `core/tests/CMakeLists.txt` | 테스트 등록 | 8 |

## 11. 검증 계획

### 11.1 Build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j$(nproc)
```

### 11.2 Test Lanes

```bash
./core/tests/run_test_lanes.sh
```

### 11.3 Symbol Verification

```bash
nm -D core/build/lib/libzlink.so | grep -E 'zlink_set_option|zlink_get_option|zlink_set_routing_id|zlink_get_routing_id|zlink_set_tls_server|zlink_set_tls_client|zlink_set_router_option|zlink_get_router_option|zlink_set_dealer_option|zlink_set_stream_option|zlink_get_stream_option|zlink_set_pub_option|zlink_get_pub_option|zlink_set_sub_option|zlink_get_sub_option|zlink_set_subscription|zlink_unset_subscription|zlink_subscription_at'
```

기대 결과: 17개 새 심볼이 export 목록에 존재.

## 12. 완료 정의

- 공개 `zlink.h`에 §3.2의 enum 6개가 존재한다.
- 공개 `zlink.h`에 §3.1의 함수 선언 17개가 존재한다.
- `zlink_set_option`이 socket/gateway 핸들에서 동작한다.
- `zlink_set_option(discovery, ...)`이 managed socket set에 fan-out 적용된다.
- `zlink_get_option(discovery, ...)`는 ENOTSUP를 반환한다.
- TLS 관련 설정이 `zlink_option_t`가 아니라 `zlink_set_tls_server` /
  `zlink_set_tls_client` 전용 함수로 제공된다.
- unified `spot` / `spot_node` public handle이 유지된다.
- `zlink_set_pub_option(spot, ...)`이 unified `spot`의 pub side에 적용된다.
- `zlink_set_sub_option(spot, ...)`이 unified `spot`의 sub side에 적용된다.
- `zlink_set_pub_option(node, ...)`이 unified `spot_node`의 pub side에 적용된다.
- `zlink_set_sub_option(node, ...)`이 unified `spot_node`의 sub side에 적용된다.
- `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscription_at`가 sub-capable handle에서 동작한다.
- §3.3.5의 허용 공통 옵션이 `zlink_set_option`/`zlink_get_option`으로 설정된다.
- `zlink_get_option(socket/gateway, ZLINK_OPT_LAST_ENDPOINT, ...)`이 동작한다.
- `zlink_set_routing_id`가 socket/gateway/discovery/unified `spot`/`spot_node`에서 동작한다.
- `zlink_get_routing_id`가 unified `spot` / `spot_node`의 publish side routing_id를 반환한다.
- `zlink_set_tls_server` / `zlink_set_tls_client`가 gateway/discovery/spot/spot_node/registry에서 동작한다.
- `zlink_set_routing_id(stream, ...)`이 EINVAL을 반환한다.
- `zlink_set_router_option` / `zlink_get_router_option`이 ROUTER/DEALER/gateway에서 동작한다.
- `zlink_set_stream_option` / `zlink_get_stream_option`이 STREAM에서 set/get 대칭으로 동작한다.
- 잘못된 핸들/소켓 타입 조합에서 EINVAL을 반환한다.
- `gateway_t::get_socket_option()` 구현 완료.
- TLS 전용 함수 dispatch 구현 완료.
- unified `spot` / `spot_node` typed option dispatch 구현 완료.
- unified `spot` / `spot_node` 자체 socket 옵션 set/get 동작 확인.
- child spot 생성 시 node option/filter 비복제 동작 확인.
- unified `zlink_spot_*` / `zlink_spot_node_*`에서는 lifecycle/management API만 유지된다.
- `zlink_spot_publish`, `zlink_spot_sub_recv`,
  `zlink_spot_subscribe`, `zlink_spot_subscribe_pattern`, `zlink_spot_unsubscribe`,
  `zlink_spot_node_publish`, `zlink_spot_node_recv`,
  `zlink_spot_node_subscribe`, `zlink_spot_node_subscribe_pattern`, `zlink_spot_node_unsubscribe`,
  `zlink_spot_set_pub_option`, `zlink_spot_set_sub_option`,
  `zlink_spot_node_set_pub_option`, `zlink_spot_node_set_sub_option`이 제거되었다.
- §5의 삭제 대상이 `zlink.h`에서 제거되었다.
- `core/src/**` 내부에 legacy public option API 직접 호출이 없다.
- 모든 호출부(tests, perf, bench, core/src)가 새 API로 이관되었다.
- `core/` 전체 빌드와 테스트가 통과한다.
- 문서가 새 API를 반영한다.
- `core/` 외부(bindings)는 이번 범위에서 변경하지 않는다.
