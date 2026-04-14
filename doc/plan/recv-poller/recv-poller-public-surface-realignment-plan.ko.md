# Recv/Poller Public Surface Re-Alignment Plan

## Summary

이 작업의 목표는 `recv / callback / poller / send_ready_handler` 공개
surface를 느슨하게 조합하는 방식에서, 사용자가 이해해야 할 두 개의
배타적인 I/O 모델로 재정렬하는 것이다.

현재 상태는 다음 문제가 있다.

- raw socket은 `recv` 기본 + `zlink_recv_handler()` 전환 모델을 가진다.
- `gateway`, `spot`, `spot_node`는 생성자/recv/callback/poller 정책이
  제각각이다.
- `spot` 문서는 callback-only처럼 보이지만 실제로는 recv surface가 있다.
- poller 문서는 제거된 것으로 쓰였지만 monitor/service 쪽에는 polling
  개념이 남아 있다.
- `callback`, `recv`, `poller`, `send_ready_handler`를 어디까지 같이 쓸 수
  있는지 사용자가 계속 조합 규칙을 기억해야 한다.

이번 정렬의 핵심은 “기능을 다 열어두되 섞지 않는다”가 아니라,
각 handle이 두 개의 완결된 모델 중 하나로만 동작하도록 만드는 것이다.

## Start Here

새 컨텍스트에서 이 문서를 다시 열었을 때는 아래 순서로 시작한다.

1. 이 문서의 `Canonical I/O Models`, `Service Policy`, `Failure contract`를
   먼저 읽고 정책을 다시 결정하지 않는다.
2. `Implementation Plan`의 순서대로만 진행한다.
3. 각 단계가 끝날 때마다 `Regression Gate` 중 해당 단계에 필요한 최소
   검증을 먼저 통과시킨다.
4. 문서와 구현이 충돌하면 구현이 아니라 문서를 먼저 기준으로 재검토한다.
   문서가 잘못된 경우에만 문서를 수정한다.

이 문서는 단순 아이디어 메모가 아니라 구현 기준 문서다.  
새 세션에서 다시 시작하는 구현자는 이 문서에 없는 정책 결정을 새로 만들지
말고, 필요한 정보가 부족할 때만 이 문서를 보강한 뒤 구현을 계속한다.

## Decision Lock

아래 항목은 이번 작업에서 다시 열지 않는 결정이다.

- canonical I/O model은 `callback model`과 `recv model` 두 개다.
- 한 handle은 두 모델 중 하나로만 동작한다.
- callback model은 `callback + send_ready_handler` 조합만 허용한다.
- recv model은 direct recv 중심이고, poller는 선택적 readiness 메커니즘이다.
- `gateway`, `spot`, `spot_node`는 동일한 mode policy를 따른다.
- `gateway` callback setter는 `zlink_recv_handler()`다.
- `spot` / `spot_node` callback setter는 `zlink_recv_spot_handler()`다.
- mode separation 위반의 기본 실패 errno는 `EBUSY`다.
- `gateway routing_id`는 constructor 인자가 아니라 setter다.
- `gateway routing_id`를 지정하지 않으면 internal ROUTER auto routing id를
  기본 representative routing id로 사용한다.
- monitor handle polling은 parent handle mode와 독립이다.

## Execution Rules

구현 중에는 아래 규칙을 지킨다.

- 정책 변경이 필요해 보이더라도 먼저 문서의 다른 섹션과 충돌하는지 확인한다.
- 같은 변경을 header, 구현, 테스트, 문서 중 한 곳에만 반영하지 않는다.
- 새 helper 이름이나 새 mode 예외를 임의로 만들지 않는다.
- callback model과 recv model의 조합 허용 범위를 넓히지 않는다.
- raw socket은 이번 작업의 직접 reshaping 범위가 아니므로, service/public
  facade 정렬에 필요한 최소 범위만 손댄다.

## Non-Goals

이번 작업에서 하지 않는 일:

- raw socket public API를 service facade와 같은 폭으로 전면 재설계하지 않는다.
- mode separation과 무관한 unrelated refactor를 끼워 넣지 않는다.
- thread-safety model 자체를 새로 설계하지 않는다.
- poller/helper naming을 새 체계로 재명명하지 않는다.
- callback setter reconfiguration을 새 기능으로 설계하지 않는다.
- service facade 외 영역까지 constructor philosophy를 억지로 통일하지 않는다.

## Progress Tracker

새 세션에서는 아래 상태를 먼저 갱신한 뒤 진행한다.

- [x] Phase 1. Header surface lock
- [x] Phase 2. API guard and mode enforcement
- [x] Phase 3. Gateway recv path
- [x] Phase 4. Spot / SpotNode recv path
- [x] Phase 5. Poller restoration
- [x] Phase 6. Docs / bindings / examples sync
- [x] Phase 7. Regression closure

Current Session Status

- 현재 진행 중: 완료
- 마지막 기준 점검: `Start Here`, `Decision Lock`, `Execution Rules`,
  `Non-Goals`, `Implementation Sequence`, `Session Handoff Checklist`
  재확인 완료
- 이번 세션 완료:
  `gateway` / `spot` / `spot_node` two-model policy, recv-first constructor,
  `zlink_gateway_recv()`, `zlink_spot_node_recv()`, public poller C API,
  helper 복원, callback/recv 분리 회귀 테스트, docs/bindings/examples 동기화,
  `Regression Gate` 및 관련 e2e 검증 통과

표시 규칙:

- 작업 시작 전 해당 phase를 `[-] in progress` 같은 임시 표기로 바꾸지 말고,
  세션 메모나 커밋/작업 로그에서 현재 진행 상태를 남긴다.
- phase가 문서의 완료 조건을 모두 만족할 때만 `[x]`로 바꾼다.
- 선행 phase가 끝나지 않았는데 후행 phase를 완료 처리하지 않는다.

## Open Questions

현재 문서 기준으로 남겨 둔 정책 open question은 없다.  
구현 중 open question이 새로 생기면 먼저 이 문서에 기록하고, 기존
`Decision Lock`과 충돌하는지 확인한 뒤에만 범위를 넓힌다.

## Implementation Sequence

이 작업은 아래 순서로 진행한다.  
순서를 바꾸면 API shape와 테스트가 다시 흔들리기 쉽다.

### Phase 1. Header surface lock

목표:

- public header에서 최종 API shape를 먼저 고정한다.

작업:

- `core/include/zlink.h`
- 필요 시 `bindings/cpp/include/zlink.h`

완료 조건:

- `gateway`, `spot`, `spot_node` 생성자/recv/setter signature가 문서와 같다.
- poller declaration과 service helper declaration이 문서와 같다.
- doc comment가 두-model policy와 충돌하지 않는다.

### Phase 2. API guard and mode enforcement

목표:

- C API layer에서 mode separation을 먼저 강제한다.

작업:

- `core/src/api/zlink.cpp`

완료 조건:

- callback model에서 recv/poller가 `EBUSY`로 막힌다.
- recv model에서 `send_ready_handler`가 `EBUSY`로 막힌다.
- poller 등록 중 callback setter 설치가 `EBUSY`로 막힌다.
- service facade callback setter 재설치가 보수적으로 거부된다.

### Phase 3. Gateway recv path

목표:

- `gateway`의 direct recv path와 routing id 정책을 문서대로 맞춘다.

완료 조건:

- `zlink_gateway_recv()`가 callback과 같은 semantic unit을 반환한다.
- `zlink_gateway_set_routing_id()`의 시점 제약이 구현된다.
- setter 미호출 시 auto routing id 기본값이 동작한다.

### Phase 4. Spot / SpotNode recv path

목표:

- `spot`과 `spot_node`의 recv/callback 정책을 동일한 모델로 맞춘다.

완료 조건:

- `spot`은 existing `zlink_spot_sub_recv()`를 유지한다.
- `spot_node`는 `zlink_spot_node_recv()`를 공개한다.
- `zlink_recv_spot_handler()`가 service facade `spot` / `spot_node`에도
  동작한다.

### Phase 5. Poller restoration

목표:

- recv model에서만 쓰는 poller surface를 복원한다.

완료 조건:

- generic poller API가 복원된다.
- gateway/spot/monitor helper가 복원된다.
- callback model handle의 data-plane poller 등록은 거부된다.

### Phase 6. Docs / bindings / examples sync

목표:

- 사용자 문서와 bindings surface를 코드와 맞춘다.

완료 조건:

- constructor-with-handler 예제가 제거된다.
- callback model / recv model 예제가 섞이지 않는다.
- bindings 생성자/등록 메서드가 새 surface와 일치한다.

### Phase 7. Regression closure

목표:

- lane 기준 회귀를 통과시키고 잔여 모순을 제거한다.

완료 조건:

- `Regression Gate`를 모두 통과한다.
- 문서, header, 테스트, bindings sample 간 모순이 없다.

## File Touch Map

새 컨텍스트에서 구현을 이어받을 때는 아래 파일부터 확인한다.

정책 기준:

- `doc/plan/recv-poller/recv-poller-public-surface-realignment-plan.ko.md`

우선 구현 대상:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`
- `core/src/services/gateway/`
- `core/src/services/spot/`

poller 관련:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`
- internal poller/socket readiness glue가 있는 `core/src/` 하위 관련 파일

문서/바인딩:

- `bindings/cpp/include/zlink.h`
- `doc/spec/core/`
- `doc/guide/`

테스트:

- `core/tests/unittest/`
- `core/tests/integration/`
- `core/tests/e2e/`

## Session Handoff Checklist

세션을 끝낼 때는 아래 항목을 문서 기준으로 스스로 점검한다.

- 이번 세션에서 변경한 phase가 무엇인지 명확한가
- 변경한 코드가 문서의 정책 결정과 충돌하지 않는가
- 새 예외 규칙이나 새 helper 이름을 만들지 않았는가
- 해당 phase의 최소 테스트를 실행했는가
- 실패한 테스트나 미완료 항목을 다음 세션이 바로 이어받을 수 있게
  남겼는가

다음 세션에 넘길 때는 최소한 아래 정보를 남긴다.

- 완료된 phase
- 진행 중 phase
- 마지막으로 수정한 파일
- 아직 남은 blocker 또는 실패 테스트
- 다음 실행 명령

## Canonical I/O Models

### Scope

이 문서의 기본 대상은 `gateway`, `spot`, `spot_node` 같은 service/public
facade다.

적용 범위:

- service/public facade에는 이 문서의 두-model policy를 그대로 적용한다.
- raw socket도 같은 철학을 따른다.
  - callback receive를 쓰면 recv/poller를 같은 handle에 섞지 않는다.
  - recv model을 쓰면 callback receive와 `send_ready_handler`를 같은 handle에
    섞지 않는다.
- 다만 raw socket은 기존 public API와 호환성 고려가 더 크므로,
  이번 문서의 직접 변경 대상은 우선 service/public facade와 public docs다.
- raw socket 쪽은 service/public facade와 모순되지 않도록 문서/계약을
  정렬하되, 구체적인 API 삭제/이름 변경은 이 작업의 1차 범위로 두지 않는다.

### Basic / Raw socket treatment

기본 소켓(raw/basic socket)도 이 문서의 두-model 철학에 포함된다.

정리:

- 기본 소켓은 이미 existing `recv` surface를 가진 recv-capable subject로 본다.
- 이번 작업에서 기본 소켓에 새로운 recv API를 추가하는 것은 아니다.
- 대신 existing raw/basic socket `recv`를 canonical `recv model`의 기준
  surface로 해석한다.
- callback receive를 사용하는 raw/basic socket은 같은 handle에서 direct recv,
  data-plane poller, `send_ready_handler`와 느슨하게 섞는 사용법을 더 이상
  권장하지 않는다.
- recv를 사용하는 raw/basic socket은 direct recv 중심으로 사용하고,
  readiness가 필요할 때만 poller를 선택적으로 결합하는 모델로 설명한다.

범위:

- 이번 작업의 직접 구현 범위는 service/public facade를 우선한다.
- raw/basic socket은 기존 API를 최대한 유지한 채 문서와 계약을 service
  facade와 같은 철학으로 정렬한다.
- 따라서 기본 소켓은 "recv 항목에 포함된다"가 맞지만, 의미는 새 recv API
  추가가 아니라 existing recv를 canonical recv model로 재정렬하는 것이다.

### 1. Callback model

callback model은 라이브러리가 receive를 직접 소비하고 callback으로 전달하는
모델이다.

계약:

- receive는 callback으로만 처리한다.
- direct recv API는 사용하지 않는다.
- recv-side poller `POLLIN`은 사용하지 않는다.
- send-side backpressure는 `send_ready_handler`로 처리한다.
- send-side poller `POLLOUT`은 사용하지 않는다.

정리:

- `receive = callback`
- `send backpressure = send_ready_handler`
- `poller = 사용 안 함`

### 2. Recv model

recv model은 사용자가 direct recv와 poller를 통해 I/O를 직접 구동하는
모델이다.

계약:

- receive는 direct recv로 처리한다.
- readiness가 필요하면 recv-side poller `POLLIN`을 사용할 수 있다.
- callback receive는 사용하지 않는다.
- send-side backpressure나 writable readiness가 필요하면 poller
  `POLLOUT`을 사용할 수 있다.
- `send_ready_handler`는 사용하지 않는다.

정리:

- `receive = recv`
- `poller = 선택적 readiness 메커니즘`
- `callback = 사용 안 함`
- `send_ready_handler = 사용 안 함`

### 3. Mode separation policy

한 handle은 두 모델 중 하나로만 동작한다.

규칙:

- 모든 receive-capable public handle은 기본적으로 recv model로 시작한다.
- callback setter를 설치하면 callback model로 전환된다.
- callback model handle은 recv model API를 사용할 수 없다.
- recv model handle은 callback model API를 사용할 수 없다.
- 같은 handle에서 두 모델을 섞는 것은 canonical usage가 아니다.
- 잘못된 모델 API 조합은 명시적으로 실패시킨다.

#### Transition rules

- 모든 receive-capable public handle은 생성 직후 recv model이다.
- callback setter는 recv model에서 callback model로의 단방향 전환이다.
- callback setter를 설치한 뒤 다시 recv model로 되돌리는 public API는 두지
  않는다.
- direct recv를 한 번이라도 사용한 뒤에도 callback setter를 설치할 수는
  있지만, setter 성공 이후부터는 callback model로 간주한다.
- poller에 data-plane handle이 등록된 상태에서는 callback setter 설치를
  허용하지 않는다.
- callback model handle에는 data-plane poller 등록을 허용하지 않는다.

이 정책의 목적은 다음과 같다.

- 사용자가 기억해야 하는 지식 범위를 줄인다.
- receive 소비 주체를 한 곳으로 고정한다.
- send 재개 책임을 한 곳으로 고정한다.
- poller / callback / send_ready_handler 조합 규칙이 지식 부채로
  누적되는 것을 막는다.

## Service Policy

### 1. Gateway

`gateway`는 다른 receive-capable handle과 동일한 두-mode policy를 따른다.
생성자도 recv-first + 최소 인자 방식으로 통일한다.  
단, raw socket과 달리 receive payload shape는 service-level semantic
message이므로 direct recv 함수는 전용 API를 둔다.

정책:

- `zlink_gateway_new()`는 recv-first constructor로 정리한다.
- `routing_id`는 생성자 인자에서 제거하고 setter로 옮긴다.
- callback setter는 새 전용 API를 만들지 않는다.
- existing generic setter인 `zlink_recv_handler()`를 `gateway`에 공식 허용한다.
- recv model에서는 direct recv + poller를 사용한다.
- callback model에서는 callback + `send_ready_handler`만 사용한다.
- callback setter 설치 전 이미 gateway가 data-plane poller에 등록돼 있으면
  setter는 실패해야 한다.

이 제약의 목적은 기능 제한이 아니라 모델 분리다.

- callback model 사용자는 callback + `send_ready_handler`만 알면 된다.
- recv model 사용자는 recv + poller만 알면 된다.
- 같은 handle에서 두 모델의 send/readiness 규칙을 섞지 않도록 service
  facade 수준에서 책임을 강하게 분리한다.

권장 shape:

```c
void *zlink_gateway_new(void *ctx,
                        const char *service_name);

int zlink_gateway_set_routing_id(void *gateway,
                                 const void *data,
                                 size_t size);

int zlink_gateway_recv(void *gateway,
                       zlink_routing_id_t *source_rid_out,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags);
```

`zlink_gateway_recv()`는 callback이 전달하던 것과 동일한 semantic unit을
반환해야 한다.

- `source_rid`
- multipart payload parts

`routing_id`는 생성 후 setter로 설정하되, 최초 bind/connect 전에만
허용하는 정책으로 정리한다.

기본값 정책:

- `zlink_gateway_set_routing_id()`를 호출하지 않으면 gateway는 내부 ROUTER의
  auto routing id를 기본 representative routing id로 사용한다.
- 즉 setter는 override 용도이며, 미호출이 에러 조건은 아니다.

### 2. Spot

`spot`은 topic-aware service API이므로 recv/callback 둘 다 유지하되,
동일한 두-mode policy를 따른다.

정책:

- `zlink_spot_new()`는 recv-first constructor로 정리한다.
- callback setter는 생성자가 아니라 `zlink_recv_spot_handler()`로 설치한다.
- `zlink_spot_sub_recv()`는 유지한다.
- `zlink_recv_spot_handler()`는 raw `SUB/XSUB`뿐 아니라 service facade
  `spot`에도 공식 허용하도록 확장한다.
- callback contract는 기존 semantic API를 유지한다.
  - `topic`은 별도 인자
  - `parts[]`는 topic 제외 payload
- recv contract도 동일 semantic API를 유지한다.
  - `topic_id_out`
  - `parts + part_count`

권장 shape:

```c
void *zlink_spot_new(void *spot_node);

int zlink_recv_spot_handler(void *spot,
                            zlink_spot_handler_fn handler,
                            void *userdata);
```

모델별 사용:

- callback model
  - receive = spot callback
  - callback setter = `zlink_recv_spot_handler()`
  - send backpressure = `zlink_spot_send_ready_handler()`
  - poller 사용 안 함
- recv model
  - receive = `zlink_spot_sub_recv()`
  - poller는 필요할 때만 사용하는 선택적 readiness 메커니즘
  - `send_ready_handler` 사용 안 함

### 3. SpotNode

`spot_node`도 동일한 두-mode policy를 따른다.

근거:

- 내부적으로 node-owned default receiver가 이미 존재한다.
- `spot`과 `spot_node`가 서로 다른 mode policy를 가지면 같은 서비스 계열
  안에서 사용 규칙이 다시 갈라진다.
- node owner가 unified `spot` child를 따로 만들지 않고 node-owned default
  receiver를 직접 pull하고 싶은 사용 사례가 있다.
  - node 단위 service wiring + bind/connect/discovery를 유지한 채 직접 recv
    loop를 돌리는 경우
  - poller 기반 단일-thread event loop에서 node-owned default sub를 직접
    다루는 경우

정책:

- `zlink_spot_node_new()`는 recv-first constructor로 정리한다.
- callback setter는 생성자가 아니라 `zlink_recv_spot_handler()`로 설치한다.
- `zlink_recv_spot_handler()`는 raw `SUB/XSUB`뿐 아니라 service facade
  `spot_node`에도 공식 허용하도록 확장한다.
- direct recv API를 추가한다.
- recv shape는 `zlink_spot_sub_recv()`와 동일한 semantic contract로 맞춘다.

권장 shape:

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name);

int zlink_spot_node_recv(void *node,
                         zlink_msg_t **parts,
                         size_t *part_count,
                         int flags,
                         char *topic_id_out,
                         size_t *topic_id_len);

int zlink_recv_spot_handler(void *node,
                            zlink_spot_handler_fn handler,
                            void *userdata);
```

이 API는 node-owned default receiver를 pull한다.  
별도 public `spot_sub` child constructor는 추가하지 않는다.

모델별 사용:

- callback model
  - receive = node callback
  - callback setter = `zlink_recv_spot_handler()`
  - send backpressure = `zlink_spot_node_send_ready_handler()`
  - poller 사용 안 함
- recv model
  - receive = `zlink_spot_node_recv()`
  - poller는 필요할 때만 사용하는 선택적 readiness 메커니즘
  - `send_ready_handler` 사용 안 함

## Public API Matrix

이 섹션은 사용자가 "내가 지금 callback model인지 recv model인지"만 알면
사용 가능한 public API를 바로 판단할 수 있도록 전체 함수를 정리한다.

규칙:

- 아래 signature는 계획 기준 최종 public surface를 뜻한다.
- 반환값 규칙은 기본적으로 다음을 따른다.
  - 생성자: 성공 시 handle, 실패 시 `NULL`
  - 일반 API: 성공 시 `0`, 실패 시 `-1`
  - recv API: 성공 시 `0`, 실패 시 `-1`
- `recv` 계열의 `flags`는 `0` 또는 `ZLINK_DONTWAIT`만 허용한다.

### 1. Callback function signatures

raw/general callback:

```c
typedef void (*zlink_socket_msg_handler_fn)(
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *parts,
  size_t part_count,
  void *userdata);
```

topic-aware callback:

```c
typedef void (*zlink_spot_handler_fn)(
  const zlink_routing_id_t *source_rid,
  const char *topic,
  size_t topic_len,
  zlink_msg_t *parts,
  size_t part_count,
  void *userdata);
```

send-ready callback:

```c
typedef void (*zlink_send_ready_handler_fn)(
  void *subject,
  void *userdata);
```

### 2. Recv function signatures

gateway recv:

```c
int zlink_gateway_recv(void *gateway,
                       zlink_routing_id_t *source_rid_out,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags);
```

spot recv:

```c
int zlink_spot_sub_recv(void *spot,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        char *topic_id_out,
                        size_t *topic_id_len);
```

spot node recv:

```c
int zlink_spot_node_recv(void *node,
                         zlink_msg_t **parts,
                         size_t *part_count,
                         int flags,
                         char *topic_id_out,
                         size_t *topic_id_len);
```

### 3. Full public signatures by service

#### Gateway public surface

constructor / identity:

```c
void *zlink_gateway_new(void *ctx,
                        const char *service_name);
```

반환값:

- 성공 시 `gateway` handle
- 실패 시 `NULL`

```c
int zlink_gateway_set_routing_id(void *gateway,
                                 const void *data,
                                 size_t size);
```

파라미터:

- `gateway`: 대상 gateway handle
- `data`: routing id 바이트 버퍼
- `size`: routing id 길이

반환값:

- 성공 시 `0`
- 실패 시 `-1`

callback model entrypoint:

```c
int zlink_recv_handler(void *gateway,
                       zlink_socket_msg_handler_fn handler,
                       void *userdata);
```

recv model entrypoint:

```c
int zlink_gateway_recv(void *gateway,
                       zlink_routing_id_t *source_rid_out,
                       zlink_msg_t **parts,
                       size_t *part_count,
                       int flags);
```

send / runtime:

```c
int zlink_gateway_send(void *gateway,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);

int zlink_gateway_send_rid(void *gateway,
                           const zlink_routing_id_t *routing_id,
                           zlink_msg_t *parts,
                           size_t part_count,
                           zlink_send_flags_t flags);

int zlink_gateway_send_ready_handler(void *gateway,
                                     zlink_send_ready_handler_fn handler,
                                     void *userdata);

int zlink_gateway_bind(void *gateway,
                       const char *bind_endpoint);

int zlink_gateway_connect(void *gateway,
                          const char *endpoint,
                          const zlink_routing_id_t *routing_id);

int zlink_gateway_disconnect(void *gateway,
                             const char *endpoint);

int zlink_gateway_attach_discovery(void *gateway,
                                   void *discovery);

int zlink_gateway_set_lb_strategy(void *gateway,
                                  zlink_gateway_lb_strategy_t strategy);

int zlink_gateway_set_option(void *gateway,
                             zlink_gateway_option_t option,
                             const void *optval,
                             size_t optvallen);

int zlink_gateway_set_tls_client(void *gateway,
                                 const char *ca_cert,
                                 const char *hostname,
                                 int trust_system);

int zlink_gateway_set_tls_server(void *gateway,
                                 const char *cert,
                                 const char *key);

int zlink_gateway_last_endpoint(void *gateway,
                                char *endpoint,
                                size_t *size);

int zlink_gateway_routing_id(void *gateway,
                             zlink_routing_id_t *out);

int zlink_gateway_update_peer_weight(void *gateway,
                                     const zlink_routing_id_t *routing_id,
                                     uint32_t weight);

void *zlink_gateway_monitor_open(void *gateway,
                                 zlink_gateway_monitor_event_mask_t events,
                                 zlink_service_monitor_handler_fn handler,
                                 void *userdata);

int zlink_gateway_destroy(void **gateway_p);
```

#### Spot public surface

constructor / callback / recv:

```c
void *zlink_spot_new(void *spot_node);

int zlink_recv_spot_handler(void *spot,
                            zlink_spot_handler_fn handler,
                            void *userdata);

int zlink_spot_sub_recv(void *spot,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        char *topic_id_out,
                        size_t *topic_id_len);
```

runtime:

```c
int zlink_spot_publish(void *spot,
                       const char *topic_id,
                       zlink_msg_t *parts,
                       size_t part_count,
                       zlink_send_flags_t flags);

int zlink_spot_subscribe(void *spot,
                         const char *topic_id);

int zlink_spot_subscribe_pattern(void *spot,
                                 const char *pattern);

int zlink_spot_unsubscribe(void *spot,
                           const char *topic_id_or_pattern);

int zlink_spot_send_ready_handler(void *spot,
                                  zlink_send_ready_handler_fn handler,
                                  void *userdata);

int zlink_spot_set_pub_option(void *spot,
                              zlink_spot_pub_option_t option,
                              const void *optval,
                              size_t optvallen);

int zlink_spot_set_sub_option(void *spot,
                              zlink_spot_sub_option_t option,
                              const void *optval,
                              size_t optvallen);

void *zlink_spot_monitor_open(void *spot,
                              zlink_spot_role_t role,
                              zlink_spot_monitor_event_mask_t events,
                              zlink_service_monitor_handler_fn handler,
                              void *userdata);

int zlink_spot_destroy(void **spot_p);
```

#### SpotNode public surface

constructor / callback / recv:

```c
void *zlink_spot_node_new(void *ctx,
                          const char *service_name);

int zlink_recv_spot_handler(void *node,
                            zlink_spot_handler_fn handler,
                            void *userdata);

int zlink_spot_node_recv(void *node,
                         zlink_msg_t **parts,
                         size_t *part_count,
                         int flags,
                         char *topic_id_out,
                         size_t *topic_id_len);
```

runtime:

```c
int zlink_spot_node_publish(void *node,
                            const char *topic_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);

int zlink_spot_node_subscribe(void *node,
                              const char *topic_id);

int zlink_spot_node_subscribe_pattern(void *node,
                                      const char *pattern);

int zlink_spot_node_unsubscribe(void *node,
                                const char *topic_id_or_pattern);

int zlink_spot_node_send_ready_handler(void *node,
                                       zlink_send_ready_handler_fn handler,
                                       void *userdata);

int zlink_spot_node_bind(void *node,
                         const char *endpoint);

int zlink_spot_node_connect_peer_pub(void *node,
                                     const char *peer_pub_endpoint);

int zlink_spot_node_disconnect_peer_pub(void *node,
                                        const char *peer_pub_endpoint);

int zlink_spot_node_attach_discovery(void *node,
                                     void *discovery);

int zlink_spot_node_set_tls_server(void *node,
                                   const char *cert,
                                   const char *key);

int zlink_spot_node_set_tls_client(void *node,
                                   const char *ca_cert,
                                   const char *hostname,
                                   int trust_system);

int zlink_spot_node_set_pub_option(void *node,
                                   zlink_spot_pub_option_t option,
                                   const void *optval,
                                   size_t optvallen);

int zlink_spot_node_set_sub_option(void *node,
                                   zlink_spot_sub_option_t option,
                                   const void *optval,
                                   size_t optvallen);

void *zlink_spot_node_monitor_open(void *node,
                                   zlink_spot_role_t role,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler,
                                   void *userdata);

int zlink_spot_node_destroy(void **node_p);
```

### 4. Callback model: allowed API list

#### Gateway callback model

constructor / setup:

- `zlink_gateway_new(ctx, service_name)`
- `zlink_gateway_set_routing_id(gateway, data, size)` before first bind/connect
- `zlink_recv_handler(gateway, handler, userdata)`

runtime:

- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_send_ready_handler()`
- `zlink_gateway_bind()`
- `zlink_gateway_connect()`
- `zlink_gateway_disconnect()`
- `zlink_gateway_attach_discovery()`
- `zlink_gateway_set_option()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_set_tls_server()`
- `zlink_gateway_monitor_open()`
- `zlink_gateway_destroy()`

not allowed in callback model:

- `zlink_gateway_recv()`
- data-plane poller `POLLIN` / `POLLOUT`

#### Spot callback model

constructor / setup:

- `zlink_spot_node_new(ctx, service_name)`
- `zlink_spot_new(spot_node)`
- `zlink_recv_spot_handler(spot, handler, userdata)`

runtime:

- `zlink_spot_publish()`
- `zlink_spot_subscribe()`
- `zlink_spot_subscribe_pattern()`
- `zlink_spot_unsubscribe()`
- `zlink_spot_send_ready_handler()`
- `zlink_spot_set_pub_option()`
- `zlink_spot_set_sub_option()`
- `zlink_spot_monitor_open()`
- `zlink_spot_destroy()`

not allowed in callback model:

- `zlink_spot_sub_recv()`
- data-plane poller `POLLIN` / `POLLOUT`

#### SpotNode callback model

constructor / setup:

- `zlink_spot_node_new(ctx, service_name)`
- `zlink_recv_spot_handler(node, handler, userdata)`

runtime:

- `zlink_spot_node_publish()`
- `zlink_spot_node_subscribe()`
- `zlink_spot_node_subscribe_pattern()`
- `zlink_spot_node_unsubscribe()`
- `zlink_spot_node_send_ready_handler()`
- `zlink_spot_node_bind()`
- `zlink_spot_node_connect_peer_pub()`
- `zlink_spot_node_disconnect_peer_pub()`
- `zlink_spot_node_attach_discovery()`
- `zlink_spot_node_set_tls_client()`
- `zlink_spot_node_set_tls_server()`
- `zlink_spot_node_set_pub_option()`
- `zlink_spot_node_set_sub_option()`
- `zlink_spot_node_monitor_open()`
- `zlink_spot_node_destroy()`

not allowed in callback model:

- `zlink_spot_node_recv()`
- data-plane poller `POLLIN` / `POLLOUT`

### 5. Recv model: allowed API list

#### Gateway recv model

constructor / setup:

- `zlink_gateway_new(ctx, service_name)`
- `zlink_gateway_set_routing_id(gateway, data, size)` before first bind/connect

runtime:

- `zlink_gateway_recv()`
- `zlink_gateway_send()`
- `zlink_gateway_send_rid()`
- `zlink_gateway_bind()`
- `zlink_gateway_connect()`
- `zlink_gateway_disconnect()`
- `zlink_gateway_attach_discovery()`
- `zlink_gateway_set_option()`
- `zlink_gateway_set_lb_strategy()`
- `zlink_gateway_set_tls_client()`
- `zlink_gateway_set_tls_server()`
- `zlink_gateway_monitor_open()`
- poller `POLLIN` / `POLLOUT`
- `zlink_gateway_destroy()`

not allowed in recv model:

- `zlink_recv_handler(gateway, ...)` after poller registration
- `zlink_gateway_send_ready_handler()`

#### Spot recv model

constructor / setup:

- `zlink_spot_node_new(ctx, service_name)`
- `zlink_spot_new(spot_node)`

runtime:

- `zlink_spot_sub_recv()`
- `zlink_spot_publish()`
- `zlink_spot_subscribe()`
- `zlink_spot_subscribe_pattern()`
- `zlink_spot_unsubscribe()`
- `zlink_spot_set_pub_option()`
- `zlink_spot_set_sub_option()`
- `zlink_spot_monitor_open()`
- poller `POLLIN` / `POLLOUT` when needed
- `zlink_spot_destroy()`

not allowed in recv model:

- `zlink_recv_spot_handler(spot, ...)` after poller registration
- `zlink_spot_send_ready_handler()`

#### SpotNode recv model

constructor / setup:

- `zlink_spot_node_new(ctx, service_name)`

runtime:

- `zlink_spot_node_recv()`
- `zlink_spot_node_publish()`
- `zlink_spot_node_subscribe()`
- `zlink_spot_node_subscribe_pattern()`
- `zlink_spot_node_unsubscribe()`
- `zlink_spot_node_bind()`
- `zlink_spot_node_connect_peer_pub()`
- `zlink_spot_node_disconnect_peer_pub()`
- `zlink_spot_node_attach_discovery()`
- `zlink_spot_node_set_tls_client()`
- `zlink_spot_node_set_tls_server()`
- `zlink_spot_node_set_pub_option()`
- `zlink_spot_node_set_sub_option()`
- `zlink_spot_node_monitor_open()`
- poller `POLLIN` / `POLLOUT` when needed
- `zlink_spot_node_destroy()`

not allowed in recv model:

- `zlink_recv_spot_handler(node, ...)` after poller registration
- `zlink_spot_node_send_ready_handler()`

## Poller Restoration

### Goal

public C poller API를 canonical surface로 복원한다.  
단, 복원 대상은 recv model을 위한 것이다. callback model과 poller를 섞는
새 조합을 열기 위한 것이 아니다.

### Restored public types / constants

복원 대상:

- `zlink_poller_event_mask_t`
- `zlink_pollitem_t`
- `zlink_poller_event_t`
- `ZLINK_POLLIN`
- `ZLINK_POLLOUT`
- `ZLINK_POLLERR`
- `ZLINK_POLLPRI`
- `ZLINK_HAVE_POLLER`
- `ZLINK_POLLITEMS_DFLT`

### Restored generic poller functions

복원 대상:

- `zlink_poll()`
- `zlink_poller_new()`
- `zlink_poller_destroy()`
- `zlink_poller_size()`
- `zlink_poller_add()`
- `zlink_poller_modify()`
- `zlink_poller_remove()`
- `zlink_poller_add_fd()`
- `zlink_poller_modify_fd()`
- `zlink_poller_remove_fd()`
- `zlink_poller_wait()`
- `zlink_poller_wait_all()`

### Restored service helper functions

복원 대상은 기존 설계/문서에 근거가 있는 helper로 제한한다.

- `zlink_poller_add_gateway()`
- `zlink_poller_modify_gateway()`
- `zlink_poller_remove_gateway()`
- `zlink_poller_add_spot_pub()`
- `zlink_poller_modify_spot_pub()`
- `zlink_poller_remove_spot_pub()`
- `zlink_poller_add_spot_sub()`
- `zlink_poller_remove_spot_sub()`
- `zlink_poller_add_monitor()`
- `zlink_poller_modify_monitor()`
- `zlink_poller_remove_monitor()`

`spot_node`에 대해서는 새 helper 이름을 만들지 않는다.  
node-owned default pub/sub도 existing `spot_pub` / `spot_sub` helper 계약으로
poller에 연결하는 방식으로 정리한다.

구체 규칙:

- `spot` recv model에서는 unified `spot` handle에 대해 existing
  `spot_pub` / `spot_sub` helper contract를 적용한다.
- `spot_node` recv model에서는 node-owned default pub/sub를 public poller
  helper가 내부적으로 target으로 삼는다.
- 즉 사용자는 `spot_node`에 대해 새 helper 이름을 배우지 않고, 문서상
  same role helper contract만 이해하면 된다.

### Poller event semantics

recv model에서만 아래 semantics를 canonical하게 사용한다.

- `gateway`
  - `POLLIN` = `zlink_gateway_recv()` 가능
  - `POLLOUT` = `zlink_gateway_send()` / `send_rid()` 가능
- `spot_sub`
  - `POLLIN` = `zlink_spot_sub_recv()` 가능
- `spot_node` default sub
  - `POLLIN` = `zlink_spot_node_recv()` 가능
  - public helper naming은 `spot_sub` 계열 helper contract를 재사용
- `spot_pub`
  - `POLLOUT` = publish 가능
- `spot_node` default pub
  - `POLLOUT` = publish 가능
  - public helper naming은 `spot_pub` 계열 helper contract를 재사용
- `monitor`
  - `POLLIN` = monitor event/snapshot polling 가능

callback model handle에 recv-side/send-side poller를 붙이는 것은 canonical
usage가 아니며, 명시적으로 실패시킨다.

주의:

- 여기서 금지하는 것은 data-plane handle에 대한 poller 사용이다.
- monitor handle 자체는 별도의 public handle이며, callback model subject를
  감시하는 경우에도 monitor handle polling은 허용한다.
- 즉 mode separation은 parent service/socket handle의 I/O 모델에 대한
  규칙이며, opened monitor handle의 event consumption 방식까지 금지하는
  규칙은 아니다.
- monitor handle polling은 recv model의 부속 기능이 아니라,
  parent handle mode와 독립된 별도 handle 소비 방식이다.

### Failure contract

mode separation 위반은 공개 계약으로 고정한다.

- callback model handle에서 direct recv 호출:
  `EBUSY`
- callback model handle에서 data-plane `POLLIN` / `POLLOUT` poller 등록:
  `EBUSY`
- recv model handle에서 `send_ready_handler` 설치:
  `EBUSY`
- data-plane poller에 등록된 recv model handle에 callback setter 설치:
  `EBUSY`
- 이미 callback model인 handle에 같은 receive callback setter를 다시 설치:
  기존 setter 규칙을 따른다.
  - reentrant replace는 existing callback/send-ready 규칙에 따라 `EDEADLK`
    또는 현재 API contract가 정의한 오류를 유지한다.
  - non-reentrant replace 허용 여부는 기존 setter contract를 우선한다.

service facade 추가 규칙:

- `gateway`, `spot`, `spot_node`의 callback setter는 최초 설치만
  canonical하게 지원한다.
- service facade에 대해 callback setter reconfiguration을 새 기능으로
  확장하지 않는다.
- 따라서 service facade에서 callback setter를 재설치/교체하는 동작은
  기존 raw socket setter 의미를 그대로 끌어오지 않고, 필요 시 `EBUSY`로
  보수적으로 거부하는 쪽을 우선한다.

원칙:

- mode separation 위반은 capability mismatch가 아니라 same-handle usage
  conflict로 본다.
- 따라서 기본 실패 errno는 `EBUSY`로 통일한다.
- 이미 각 API가 더 강한 existing contract를 갖는 경우에는 그 계약을
  유지한다.

## Thread-Safety Position

이번 작업은 thread-safety 설계를 새로 바꾸는 작업이 아니다.

적용 원칙:

- existing `public_api_guard`
- existing callback inflight / close gating
- existing monitor busy/close 규칙
- existing service lifecycle gate

를 그대로 재사용한다.

이번 작업에서 필요한 것은 새 concurrency 모델 설계가 아니라, 두 개의
I/O 모델을 service/public surface에 일관되게 투영하는 것이다.

검증 포인트:

- callback model handle에서 recv / poller 사용을 어떻게 금지할지
- recv model handle에서 callback / `send_ready_handler` 사용을 어떻게
  금지할지
- 잘못된 mode API 조합에 대해 어떤 errno로 실패시킬지
- poller 등록 상태와 callback setter 설치 사이의 전환 규칙을 어떻게
  강제할지
- model separation 정책이 구현/문서/테스트에서 동일하게 드러나는지
- close/destroy/monitor-open과 기존 busy/terminal contract가 유지되는지

문서에는 이를 "thread-safety model reused, not redesigned"로 명시한다.

## Bindings Impact

C API 함수 수는 늘어난다.  
하지만 bindings에서는 이 증가가 클래스 단위로 흡수되므로 사용자 체감
복잡도는 제한적이다.

이번 작업의 우선순위는 API 수를 최소화하는 것보다 사용자가 이해해야 할
운용 모델을 두 개의 self-contained model로 제한하는 것이다.

핵심 판단:

- C surface는 넓어진다.
- 그러나 bindings에서는 `Gateway`, `Spot`, `Poller`,
  monitor-related classes에 메서드로 묶인다.
- 실제 중요한 것은 함수 개수보다 모델 단순화다.
- 이번 작업은 `recv / callback / poller / send_ready_handler / monitor`를
  두 개의 일관된 모델로 재배치하는 것이다.
- 다만 bindings에는 실제 surface change가 있다.
  - `Gateway`, `Spot`, `SpotNode` 생성자에서 handler 인자가 빠진다.
  - callback 등록은 constructor가 아니라 별도 setter 메서드로 이동한다.
  - 기존 constructor-with-handler 예제와 샘플 코드는 모두 갱신해야 한다.
  - binding 생성자 overload/팩토리 시그니처가 바뀌므로 compile-time
    breaking change가 발생한다.

## Implementation Plan

### 1. Header / binding sync

대상:

- `core/include/zlink.h`
- `bindings/cpp/include/zlink.h`

작업:

- `zlink_gateway_new()` constructor shape를 recv-first 기준으로 정리
- `routing_id`를 생성자에서 제거하고 setter-only로 정리
- `zlink_gateway_recv()` declaration 추가
- `zlink_spot_new()` constructor shape를 recv-first 기준으로 정리
- `zlink_spot_node_new()` constructor shape를 recv-first 기준으로 정리
- `zlink_spot_node_recv()` declaration 추가
- poller public types/constants/functions 복원
- service helper declarations 복원
- 주석을 두-model policy 기준으로 재작성

### 2. C API layer

대상:

- `core/src/api/zlink.cpp`

작업:

- `zlink_gateway_new()`를 recv-first constructor로 정리
- `zlink_gateway_set_routing_id()` 호출 시점 제약을 bind/connect 전으로 고정
- `zlink_recv_handler(gateway, ...)` 허용 경로 추가
- `zlink_gateway_recv()` 구현
- `zlink_spot_new()`를 recv-first constructor로 정리
- `zlink_recv_spot_handler(spot, ...)` 허용 경로 추가
- `zlink_spot_node_new()`를 recv-first constructor로 정리
- `zlink_spot_node_recv()` 구현
- `zlink_recv_spot_handler(spot_node, ...)` 허용 경로 추가
- generic poller API wrapper 복원
- gateway/spot/monitor poller helper wrapper 복원
- callback model handle에서 poller registration을 거부하는 정책 연결
- recv model handle에서 `send_ready_handler` 설치를 거부하는 정책 연결
- 잘못된 mode API 조합에 대한 failure errno를 공개 계약으로 고정
- data-plane poller 등록 중 callback setter 설치를 거부하는 정책 연결

원칙:

- thin adapter 유지
- 새 독립 상태 머신 추가 금지
- mode mutual exclusion은 existing guard 재사용

### 3. Gateway internal receive path

대상:

- `core/src/services/gateway/`

작업:

- callback dispatch 외에 direct recv path를 public API에 연결
- `source_rid`를 recv path에서도 동일 의미로 반환
- multipart ownership 규칙을 callback path와 동등 수준으로 명확화
- poller `POLLIN`과 direct recv가 같은 readiness unit을 가리키도록 정렬
- callback model gateway에서는 recv/poller path를 금지

### 4. Spot / SpotNode internal receive path

대상:

- `core/src/services/spot/`

작업:

- `spot` recv model은 existing `zlink_spot_sub_recv()` 경로를 유지
- node-owned default receiver를 `zlink_spot_node_recv()`에 연결
- callback model spot/node에서는 recv/poller path를 금지
- recv model spot/node에서는 callback / `send_ready_handler`를 금지
- recv model에서는 local/remote/discovery mesh message를 semantic message
  단위로 pull 가능하게 정리

### 5. Poller bridge restoration

대상:

- internal poller/socket readiness glue
- public C API wrappers

작업:

- generic poller glue 복원
- gateway/spot/monitor helper validation 복원
- node-owned default pub/sub는 existing spot role helper contract로 연결
- callback model handle은 recv-side/send-side poller helper 등록을 거부

## Documentation Plan

### API docs

갱신 대상:

- `doc/spec/core/gateway.md`
- `doc/spec/core/gateway.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/polling.md`
- `doc/spec/core/polling.ko.md`
- `doc/spec/core/socket/README.md`
- `doc/spec/core/socket/README.ko.md`
- `doc/spec/core/README.md`
- `doc/spec/core/README.ko.md`

반영:

- 두 개의 I/O model을 명시적으로 설명
- `gateway`, `spot`, `spot_node`가 동일한 mode policy를 따른다고 명시
- `gateway` callback setter는 `zlink_recv_handler()`로 명시
- `spot` / `spot_node` callback setter는 `zlink_recv_spot_handler()`로 명시
- `gateway routing_id`는 constructor 인자가 아니라 setter라고 명시
- `zlink_gateway_recv()` 추가
- `zlink_spot_sub_recv()`를 recv model API로 명시
- `zlink_spot_node_recv()` 추가
- poller removed 문구 삭제
- poller는 recv model 전용이라고 명시
- `send_ready_handler`는 callback model 전용이라고 명시
- monitor handle polling은 parent handle mode와 독립된 별도 소비 방식이라고
  명시
- mode separation 위반 시 `EBUSY`를 기본 실패 계약으로 명시
- constructor-with-handler 문서를 setter 기반 문서로 전환

### Guides

갱신 대상:

- `doc/guide/03-0-socket-patterns.*`
- `doc/guide/07-2-gateway.*`
- `doc/guide/07-3-spot.*`
- `doc/guide/11-thread-safety.*`
- `doc/guide/01-overview.*`
- 필요 시 `doc/guide/02-core-api.*`

반영:

- 두 개의 I/O model을 명시적으로 설명
- callback model 예제는 callback + `send_ready_handler`만 사용
- recv model 예제는 direct recv를 기본으로 설명하고, poller는 선택적
  readiness 메커니즘으로 추가 설명
- 같은 handle에서 두 모델을 섞지 않는다고 명시
- `gateway` recv sample 추가
- `spot` recv model sample + callback model sample 병기
- `spot_node` recv model sample + callback model sample 병기
- callback-only surface / poller removed 같은 상충 문구 제거
- constructor-with-handler 예제를 setter 기반 예제로 전환
- binding 예제도 constructor-with-handler에서 constructor + setter 패턴으로
  전환

### Monitoring docs

갱신 대상:

- `doc/spec/core/monitoring.md`
- `doc/spec/core/monitoring.ko.md`

반영:

- monitor direct polling은 parent handle mode와 독립된 별도 handle 소비
  방식으로 정리
- ignore handler + polling usage를 복원된 poller 계약과 연결
- callback model handle과 monitor polling relation을 명확히 구분

## Test Plan

이 작업의 회귀 테스트는 "기능 확인"이 아니라 "mode policy가 surface 전체에
일관되게 반영됐는지"를 검증하는 용도다. 따라서 테스트는 아래 네 층으로
나눈다.

- 단위 테스트:
  mode state, errno, helper validation, option timing 같은 작은 정책 검증
- 통합 테스트:
  실제 `gateway` / `spot` / `spot_node` handle을 띄워 메시지 흐름과 mode
  전환을 검증
- 문서/바인딩 회귀:
  생성자 시그니처 변경과 setter 기반 사용 예제가 실제 surface와 맞는지 검증
- 회귀 게이트:
  기존 lane에서 반드시 돌려야 하는 최소 명령 집합

### Unit

필수 추가/갱신 항목:

- `gateway`:
  - 생성 직후 recv model 상태
  - `set_routing_id()`가 bind/connect 전후에 다르게 동작하는지
  - callback setter 설치 후 mode 전환 고정
  - callback model에서 recv/poller 금지 errno
  - recv model에서 `send_ready_handler` 금지 errno
- `spot`:
  - unified `spot` 생성 직후 recv model 상태
  - `zlink_recv_spot_handler()` 설치 후 mode 전환 고정
  - callback model에서 recv/poller 금지 errno
  - recv model에서 `send_ready_handler` 금지 errno
- `spot_node`:
  - 생성 직후 recv model 상태
  - `zlink_recv_spot_handler()` 설치 후 mode 전환 고정
  - callback model에서 recv/poller 금지 errno
  - recv model에서 `send_ready_handler` 금지 errno
- poller helper:
  - callback model handle 등록 거부
  - recv model handle 등록 허용
  - monitor handle polling은 parent mode와 독립 허용

권장 위치:

- `core/tests/unittest/`에 새 단위 테스트 추가
- pure API guard/errno 성격은 가능하면 단위 테스트로 먼저 고정

### Gateway

필수 시나리오:

- recv model:
  - `zlink_gateway_new()` 직후 recv 가능
  - `zlink_gateway_set_routing_id()`가 bind/connect 전에는 성공, 이후에는 실패
  - request/reply를 `zlink_gateway_recv()`로 수신
  - poller 없이 blocking/nonblocking recv만으로도 동작
  - 필요 시 `POLLIN` / `POLLOUT` readiness와 결합 가능
- callback model:
  - `zlink_recv_handler(gateway, ...)` 설치 성공
  - 이후 request/reply가 callback으로만 전달
  - callback model에서 recv / `POLLIN` / `POLLOUT` 사용 실패
  - `zlink_gateway_send_ready_handler()`는 허용
- mode transition:
  - poller 등록된 gateway에서 callback setter 설치 실패
  - callback setter 재설치가 service facade에서 보수적으로 거부
  - destroy/monitor-open이 mode와 무관하게 기존 lifecycle gate를 유지

권장 위치:

- `core/tests/integration/discovery/` 및 service integration test에 추가

### Spot

필수 시나리오:

- recv model:
  - `zlink_spot_new()` 직후 `zlink_spot_sub_recv()` 동작
  - exact topic / pattern topic 수신
  - local publish / remote mesh publish / discovery mesh publish 수신
  - poller 없이 recv만으로도 동작
  - 필요 시 `spot_sub` `POLLIN`, `spot_pub` `POLLOUT`와 결합 가능
- callback model:
  - `zlink_recv_spot_handler(spot, ...)` 설치 후 topic-aware callback 수신
  - callback model에서 recv / poller 사용 실패
  - `zlink_spot_send_ready_handler()`는 허용
- mode transition:
  - poller 등록된 `spot`에서 callback setter 설치 실패
  - callback setter 재설치 보수적 거부
  - monitor handle polling은 mode와 독립 허용

권장 위치:

- `core/tests/e2e/spot/`과 `core/tests/integration/`의 SPOT 계약 테스트에 추가

### SpotNode

필수 시나리오:

- recv model:
  - `zlink_spot_node_new()` 직후 `zlink_spot_node_recv()` 동작
  - local publish -> node recv
  - remote mesh -> node recv
  - discovery-attached node recv
  - poller 없이 recv만으로도 동작
  - 필요 시 node-owned default sub/pub가 existing helper contract로 poller와
    결합 가능
- callback model:
  - `zlink_recv_spot_handler(spot_node, ...)` 설치 후 topic-aware callback 수신
  - callback model node에서 recv / poller 사용 실패
  - `zlink_spot_node_send_ready_handler()`는 허용
- mode transition:
  - poller 등록된 node에서 callback setter 설치 실패
  - callback setter 재설치 보수적 거부
  - destroy/monitor-open과 mode 분리 유지

권장 위치:

- `core/tests/e2e/spot/`의 node 시나리오와 `core/tests/integration/` 서비스
  계약 테스트에 추가

### Poller

필수 시나리오:

- restored symbols compile/link
- generic `add/modify/remove/wait/wait_all`
- fd helper `add/modify/remove`
- gateway/spot/monitor helper `add/remove`
- invalid subject/event combination failure
- callback model handle의 poller registration 거부
- recv model handle의 helper registration 허용
- mode separation 위반이 `EBUSY`로 실패
- timeout/empty wait behavior
- multi-item fan-in behavior

권장 위치:

- poller 관련 단위 테스트 + service integration tests 양쪽에 분산

### Docs And Bindings Regression

필수 항목:

- C header와 문서 시그니처 일치 확인
- constructor-with-handler 예제가 모두 constructor + setter 예제로
  교체되었는지 확인
- bindings 생성자/팩토리 signature 변경 반영 확인
- bindings에서 callback 등록 메서드가 새 surface와 맞는지 확인
- callback model / recv model 사용 예제가 서로 섞이지 않는지 확인

완료 조건:

- docs, header, bindings sample이 모두 같은 mental model을 설명한다.

### Regression Gate

구현 완료 전 최소 실행 세트:

- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- `cmake --build core/build`
- `ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)`
- `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 필요 시 `./core/tests/run_test_lanes.sh`

추가 게이트:

- `spot` / `gateway` / `monitor` 관련 신규/수정 테스트는 integration lane에
  반드시 포함
- constructor signature 변경에 영향 받는 bindings/example 빌드가 있으면
  해당 빌드 검증까지 포함

## Acceptance Criteria

- `gateway`, `spot`, `spot_node`가 동일한 두-mode policy를 따른다.
- callback model은 `callback + send_ready_handler` 조합으로만 설명된다.
- recv model은 direct recv 중심으로 설명되고, poller는 선택적 readiness
  메커니즘으로 설명된다.
- `gateway` callback setter는 `zlink_recv_handler()`로 고정된다.
- `spot` / `spot_node` callback setter는 `zlink_recv_spot_handler()`로
  고정된다.
- `zlink_gateway_recv()`와 `zlink_spot_node_recv()`가 공개/문서화된다.
- mode transition은 recv -> callback 단방향으로 문서화된다.
- mode separation 위반의 기본 실패 errno가 `EBUSY`로 문서화된다.
- public poller C API가 헤더/구현/문서/테스트에 복원된다.
- `poller removed`, `callback-only`, 느슨한 조합 허용 등 상충 문구가
  public docs에서 제거된다.
- thread-safety model 변경이 아니라 reuse라는 점이 문서에 명시된다.
- bindings 관점에서 surface 증가보다 model 단순화가 우선이라는 점이
  설명된다.
- constructor signature 변경이 compile-time breaking change로 문서화된다.

## Assumptions

- `gateway` constructor handler와 constructor `routing_id` 인자는 제거하고
  recv-first + setter 기반으로 정리한다.
- callback setter는 새 서비스별 전용 API를 만들지 않는다.
- `spot` recv는 삭제하지 않고 constructor handler는 제거한다.
- `spot_node`는 recv API를 추가하고 constructor handler는 제거한다.
- unified `spot` 자체에 대한 새 poller helper는 만들지 않는다.
- `spot_node`용 새 helper 이름도 만들지 않고 existing spot role helper
  contract를 재사용한다.
- compatibility wrapper보다 public contract 정렬과 model 분리를 우선한다.
- raw socket은 같은 철학으로 문서 정렬 대상이지만, 이번 작업의 직접 API
  reshaping 범위는 service/public facade를 우선한다.
