# SPOT Data/Control Plane 구조 개편 계획

> 상태 메모
> 이 문서는 2026-03-14 기준 SPOT 성능 회귀를 구조적으로 해소하기 위한
> 상세 설계안이다.
> 현재 기준 성능은 `main`의 single 보고서
> `core/perf/results/single/report/perf_linux_20260314_003239.txt`를 따른다.
> 최근 rewrite 단일 측정에서 SPOT `tcp/131072`는 `31.546 Kmsg/s`,
> `tcp/262144`는 `14.324 Kmsg/s` 수준이며,
> 같은 구간의 main 기준은 각각 `46.204 Kmsg/s`, `25.550 Kmsg/s`다.
> 즉 현재 rewrite는 main 대비 대략 `0.68x`, `0.56x` 수준이다.
>
> 2026-03-16 업데이트 메모:
>
> - 이 문서의 1차 채택안인 `data PUB/XSUB + peer control PUB/SUB` 구조는
>   현재 코드에 이미 반영되어 있다.
> - 이후 2차 단순화 리팩터로 `hidden default sub 비의존`,
>   `public handle과 node internal receiver 분리`,
>   `ready source 단일화`, `destroy ownership 단일화`까지
>   구현이 반영되었다.
> - node-level subscribe/monitor/handler 경로는 전용 internal receiver 타입으로
>   수렴했고, ready-ack snapshot은 별도 control topic으로 분리됐다.
> - 관련 `spot` thread-safe/scaling teardown 회귀도 현재 검증 범위에서는
>   재현되지 않는다.
> - 따라서 이 문서는 더 이상 “새 구조 제안”만이 아니라,
>   이미 반영된 1차 구조 위에서 2차 단순화 리팩터를 진행하기 위한
>   기준 문서로 함께 사용한다.
>
> 읽기 안내:
>
> - 1차 plane 분리 설계: §1-14 (이미 코드에 반영됨)
> - 1차 perf 복구 실행 지침: §15
> - 현재 구현 상태 점검 및 남은 구조 부채: §16 (현재 독자는 여기부터 읽으면 된다)
> - 2차 리팩터 목표 및 구현 단계: §17-18
> - 수용 기준 및 완료 판정: §19-23

## 1. 목적

이 문서의 목적은 다음 두 가지를 동시에 만족하는 SPOT 구조 개편안을
정의하는 것이다.

- SPOT single 성능을 먼저 `main` 근사치까지 회복한다.
- monitor/readiness 계약을 perf 전용 우회 없이 유지한다.

### 1.1 이번 문서가 바꾸지 않는 것

이번 리팩터는 아래를 유지한다.

- thread-safe public contract의 의미
- monitor / readiness public semantics의 강도
- `spot_node` 중심 public 모델
- perf 전용 shortcut이나 harness 특화 코드 허용 정책

즉 이 문서는 성능을 위해 계약을 약하게 만드는 문서가 아니라,
현재 계약을 더 단순한 구조 위에 다시 올리는 문서다.

### 1.1.1 API 변경 정책

public `spot` C API / ABI는 이번 리팩터의 절대 불변 조건은 아니다.
구조 단순화, ownership 명확화, hidden/internal resource 제거를 위해 필요하면
public C API도 변경할 수 있다.

API 변경 허용 범위는 아래처럼 제한한다.

- 허용:
  hidden default handle, implicit helper, lifecycle 누수를 제거하기 위한 API 정리
- 조건부 허용:
  node-level `SUB` monitor/handler backing object를 명시화하기 위한 surface 조정
- 금지:
  thread-safe 의미 약화, monitor/readiness 의미 약화, perf 전용 shortcut 노출

즉 API 변경은 "내부 구조를 public에 드러내기 위해서"가 아니라
"이미 public에 새어 있는 내부 구조를 제거하기 위해서만" 허용한다.

### 1.2 관련 문서

- [`gateway-thread-safe-control-lifecycle-refactor-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/spot-refactor/gateway-thread-safe-control-lifecycle-refactor-plan.ko.md)

`spot` 문서는 transport-level data/control plane 분리가 핵심이고,
`gateway` 문서는 single-router 기반 state/control/lifecycle ownership 분리가 핵심이다.

### 1.3 공통 용어 정리

이 문서에서 `control plane`은 실제 peer 간 transport-level control socket과
typed protocol을 뜻한다.

- `spot`: transport-level peer control plane
- `gateway`: logical control/state plane

같은 `control plane`이라는 단어를 쓰더라도
두 문서가 가리키는 구조 수준은 다르다.

### 1.4 POSD 관점에서 다시 정의한 이번 리팩터의 목적

John Ousterhout의 *A Philosophy of Software Design* 관점에서 보면,
이 리팩터의 진짜 목표는 "성능 최적화" 자체가 아니라
`spot`의 복잡성을 아래로 끌어내리는 것이다.

현재 `spot`의 핵심 문제는 아래 세 가지 복잡성 증상으로 요약된다.

- **변경 증폭**: readiness 또는 destroy 규칙 하나를 바꾸려면
  `spot_node`, `spot_sub`, `spot_data_plane`, public API glue를 함께 건드려야 한다.
- **인지적 부하**: public sub, hidden default sub, node handler, peer control snapshot을
  동시에 머리에 올려야 현재 동작을 이해할 수 있다.
- **미지의 미지**: late-connect, teardown, send-ready race 같은 경계 상황에서
  어느 레이어가 authoritative owner인지 코드만 읽어서는 즉시 드러나지 않는다.

따라서 이번 문서의 우선순위는 다음과 같이 고정한다.

1. hidden/internal resource가 public lifecycle에 새지 않게 한다.
2. data fast path에서 control bookkeeping을 제거한다.
3. readiness와 destroy의 authoritative owner를 하나씩만 남긴다.
4. 성능 회복은 위 구조 단순화의 결과로 얻고, perf 전용 우회는 허용하지 않는다.

즉 이번 리팩터는 "작동하는 코드를 유지하면서도,
미래 변경 시 알아야 할 것을 줄이는 것"을 1차 목표로 삼는다.

### 1.5 POSD 위반 매핑으로 본 현재 `spot` 구조 부채

이 절은 현재 구조 부채를 Ousterhout의 위험 신호에 직접 매핑한다.

| 위반 원칙 | 현재 구조 | 왜 문제인가 |
| --- | --- | --- |
| 정보 누출 | `ensure_default_sub()`가 node internal dispatch까지 담당 | internal dispatch라는 설계 결정이 public sub lifecycle에 누출된다 |
| 얕은 모듈 | hidden default sub가 일반 `spot_sub_t`와 attachment/destroy semantics 공유 | "node inbound dispatch"라는 단순 요구를 위해 public sub의 복잡성을 전부 떠안는다 |
| 특수-범용 혼합 | 하나의 `spot_sub_t`가 user-visible handle과 node internal dispatch를 둘 다 수행 | 특수 목적 변경이 범용 handle 계약을 오염시킨다 |
| 오류를 정의에서 제거하지 못함 | data plane, default sub, node가 같은 리소스를 닫을 수 있다 | close 중복이라는 오류 카테고리가 구조적으로 존재한다 |
| 시간적 분해 | readiness 의미가 `sub → data plane → node aggregate`로 분산 | 정보 기준이 아니라 실행 순서 기준으로 구조가 나뉘어 있다 |

핵심 해석은 다음과 같다.

- hidden default sub의 본질은 "숨겨진 객체"가 아니라 정보 누출이다.
- 3-way close의 본질은 teardown 버그가 아니라 오류 카테고리를 설계에서 제거하지 못한 것이다.
- readiness 분산의 본질은 시간적 분해다. 실행 순서가 아니라 정보 소유 기준으로 다시 묶어야 한다.

핵심 방향은 한 줄로 요약된다.

```text
SPOT의 data plane과 peer control plane을 분리한다.
```

즉 현재 `mesh_pub = XPUB`에 실려 있는 역할을 쪼개서,

- data는 다시 `PUB/XSUB` 기반의 단순 fast path로 되돌리고
- subscription/readiness/ack는 별도 peer control plane으로 이동한다.

### 1.6 핵심 깊은 모듈 선언

이번 리팩터가 도입하거나 강화하는 깊은 모듈은 아래 3개다.
각 모듈은 **무엇을 숨기는가**를 기준으로 정의한다.

Ousterhout는 좋은 모듈을 "인터페이스는 좁지만 내부는 깊은 직사각형"에 비유한다.
아래 표의 "숨기는 것" 열이 깊이를, "드러내는 인터페이스" 열이 폭을 나타낸다.
숨기는 것이 많을수록, 드러내는 것이 적을수록 깊은 모듈이다.

| 깊은 모듈 | 숨기는 것 (깊이) | 드러내는 인터페이스 (폭) |
| --- | --- | --- |
| **peer state sync module** | subscription epoch, sequence, snapshot generation, heartbeat, resync 프로토콜 세부 | subscription sync, ready ack, peer lost — 의미 3개만 노출 |
| **node internal receiver** | inbound dispatch 메커니즘, internal attachment, fanout 연결 세부 | 없음 (인터페이스 없는 깊은 모듈 — GC와 같은 수준) |
| **runtime close owner** | socket/attachment drain 순서, abortive fallback, tracked socket 정리 | `runtime.shutdown()` 단일 경로 |

```text
SPOT 깊은 모듈 구조 (리팩터 후)

┌─ public API surface ─────────────────────────────┐
│  zlink_spot_node_new / destroy / subscribe / ...  │ ← 좁은 인터페이스
├───────────────────────────────────────────────────┤
│                                                   │
│  ┌─ peer state sync module ────────────────────┐  │
│  │  epoch, sequence, heartbeat, resync ...     │  │ ← 깊은 내부
│  │  외부 노출: sync / ack / lost 의미만        │  │
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─ node internal receiver ────────────────────┐  │
│  │  fanout connect, handler dispatch ...       │  │ ← 인터페이스 없음
│  │  외부 노출: 없음 (node lifecycle에 종속)    │  │
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─ runtime close owner ──────────────────────┐   │
│  │  drain 순서, abortive fallback, tracked ... │   │ ← 단일 경로
│  │  외부 노출: shutdown() 하나                 │   │
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─ data fast path ───────────────────────────┐   │
│  │  ingress → fanout → mesh_pub (PUB)         │   │ ← payload only
│  │  control bookkeeping 없음                  │   │
│  └─────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────┘
```

### 1.7 ownership 표

리팩터 완료 후 아래 ownership이 성립해야 한다.
"닫지 않는 주체" 열은 해당 리소스에 대해 **절대 close를 시도하면 안 되는** 주체를 명시한다.
이것이 3-way close를 구조적으로 제거하는 핵심이다 — 특정 리소스에 대해
close를 호출할 수 있는 주체가 정확히 하나뿐이면, close 중복이라는 오류 카테고리가
설계에서 사라진다.

| 리소스 | authoritative owner | 닫지 않는 주체 |
| --- | --- | --- |
| public child attachment | handle destroy | node, runtime, data plane |
| internal receiver lifecycle | node destroy | handle destroy, data plane, runtime close path |
| node-owned internal attachment detach | node destroy | handle destroy, data plane |
| internal socket close (mesh, ctrl, fanout, ingress) | runtime (join 이후 단일 close) | data plane thread, node destroy |
| readiness aggregate | peer state sync module (단일 source model) | sub, data plane 개별 bookkeeping |

"same resource, single close owner" — 이 표에 위배되는 경로가 남아 있으면
리팩터가 완료된 것이 아니다.

## 2. 배경과 문제 정의

현재 성능 회귀는 SPOT 전체가 아니라
`remote peer mesh steady-state path`에 집중되어 있다.

확인된 사실:

- local-only SPOT은 빠르다.
- raw `PUBSUB`는 main과 큰 차이가 없다.
- 현재 병목은 SPOT의 remote peer mesh 경로다.

즉 문제는 callback 자체나 local facade가 아니라,
현재 rewrite의 SPOT data/control 배선 방식이다.

### 2.1 main과 current의 본질적 차이

`main`의 SPOT mesh sender는 본질적으로 data 전용이다.

- `mesh_pub = PUB`
- `mesh_xsub = XSUB`
- `fanout = XPUB`

현재 rewrite는 socket 타입 자체는 이미 분리되어 있다.

- `mesh_pub = PUB` (data 전용)
- `mesh_xsub = XSUB`
- `peer_ctrl_pub = PUB` (control 전용)
- `peer_ctrl_sub = SUB` (control 전용)

그러나 **socket 타입은 분리되었지만 경로 책임은 여전히 혼합**되어 있다.

- `ready_probe` / `ready_ack` 처리가 data plane sub receive path에 남아 있음
- `ensure_default_sub()`가 data plane attachment lifecycle을 암묵적으로 관리
- `outbound_ready_filters`가 `"ack:" + source_id` 문자열로 readiness source를 혼합

즉 socket 분리만으로는 해결되지 않는 **경로 수준 책임 혼합**이 핵심 부채다.

### 2.2 현재까지 유효한 개선과 한계

이미 유지 가치가 확인된 core 변경은 남겨 둔다.

- attachment socket pointer cache
- `mesh_pub`/`mesh_xsub` I/O thread affinity 분리
- local `fanout` XPUB subscription inbox drain

이 변경들은 hot path에서 lock/map 또는 backlog를 줄였지만,
근본 원인인 `data/control 혼합` 자체는 그대로다.
즉 미세 최적화만으로는 main 근사치까지 복구되지 않는다.

## 3. 설계 목표

### 3.1 목표

- SPOT data fast path를 `main`과 같은 성격으로 단순화한다.
- monitor/readiness 계약을 유지한다.
- 추가 네트워크 소켓은 `spot handle`별이 아니라 `spot_node`별로 둔다.
- benchmark harness 전용 분기 없이 core 내부 구조로 해결한다.
- hidden default child/default sub 같은 내부 전제에 기대지 않는다.

### 3.2 비목표

- large payload zero-copy 같은 bench-only 최적화
- perf 전용 shortcut 또는 test harness 특화 코드
- monitor 계약을 약하게 만들어 수치를 맞추는 것
- child handle마다 peer socket을 추가하는 것
- 구조 단순화와 무관한 public API 재설계

### 3.3 현재 문서의 2차 구조 부채 핵심

현재 1차 구조 위에 남아 있던 아래 네 가지 2차 부채는
현재 코드 기준으로 정리되었다.

- hidden default sub 비의존 원칙 반영
- public handle과 node internal receiver lifecycle 분리 반영
- readiness source / snapshot / ack bookkeeping 단일화 반영
- destroy ownership 단일화 반영

## 4. 현재 구조가 느린 이유

현재 rewrite의 문제는 단순히 `XPUB`가 느리다는 차원이 아니다.
더 정확한 문제는 다음과 같다.

### 4.1 data path가 여전히 control bookkeeping을 함께 떠안는다

현재 문제는 더 이상 socket 타입 자체가 섞여 있다는 뜻이 아니다.
문제는 이미 분리된 data/control socket 위에서,
실제 처리 경로의 책임이 아직 분리되지 않았다는 점이다.

- payload forwarding 경로가 ready probe / ready ack 결과를 함께 고려한다.
- remote peer readiness bookkeeping이 data plane receive path에 남아 있다.
- hidden default sub lifecycle이 data plane attachment 흐름과 간접 결합돼 있다.

즉 `mesh_pub = PUB`, `peer_ctrl_pub/sub` 분리 자체는 끝났지만,
data fast path가 아직 control bookkeeping에서 완전히 자유롭지 않다.

### 4.2 readiness 계약이 socket side-effect에 묶여 있다

monitor가 노출하는 강한 의미는 다음과 같다.

- `SPOT_SUB_FILTER_APPLIED`
- `SPOT_SUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

문제의 본체는 monitor callback 오버헤드가 아니라,
이 상태를 remote peer 사이에서 판별하고 합의해야 한다는 점이다.
현재는 그 합의 메커니즘이 data mesh에 얹혀 있다.

### 4.3 steady-state path에 불필요한 분기와 polling이 남는다

현재 data plane thread는 payload forwarding 외에도
peer readiness/control 관련 입력을 함께 poll하고 분기한다.
그래서 payload가 큰 구간일수록 회귀가 크게 드러난다.

## 5. 핵심 설계 원칙

### 5.1 plane 분리

SPOT는 아래 두 plane으로 나눈다.

- data plane: payload 전달만 담당
- peer control plane: subscription/readiness/state sync 담당

### 5.2 node 단위 소유

추가 소켓은 `spot_pub_t`/`spot_sub_t`별이 아니라
`spot_node_t`가 공용으로 소유한다.

즉 handle이 늘어도 peer control socket 수는 늘지 않는다.

### 5.3 monitor 의미는 유지하되, 근거는 socket 부수효과가 아니라 명시적 protocol로 바꾼다

현재도 transport-level로는 peer control protocol이 존재하지만,
monitor 의미의 authoritative source가 아직 한 곳으로 완전히 수렴하지 않았다.
개편 후에는 peer control message와 정규화된 readiness state를 기준으로
subscription 적용/ready ack/peer loss를 판단한다.

### 5.4 data fast path는 payload forwarding만 남긴다

개편 후 data plane에서 steady-state payload 경로는 아래만 남긴다.

- ingress에서 data 수신
- local fanout으로 전달
- remote mesh로 전달

control 관련 bookkeeping은 fast path 밖으로 뺀다.

### 5.5 `gateway`와 병행 진행할 때의 원칙

`spot`과 `gateway`는 같은 service 계층이지만,
실제 구조 부채는 다르다.

- `spot`의 핵심 부채:
  hidden internal receiver, readiness source 중복, destroy ownership 분산
- `gateway`의 핵심 부채:
  state ownership 혼합, attach/refresh ordering 혼합, monitor pipeline 결합

권장 순서는 아래와 같다.

1. `spot`에서 internal receiver / attachment / destroy ownership 정리를 먼저 수행
2. 그 다음 `gateway`에서 state ownership과 lifecycle 단계화를 정리
3. 마지막에 공통 service runtime / lifecycle 설명을 맞춤

즉 두 문서는 같은 철학을 공유하지만,
실제 코드 변경은 동일한 속도로 병렬 진행하지 않는다.

## 6. 대안 검토

### 6.1 대안 A: 1차 리팩터 이전 구조를 사실상 유지한 채 미세 최적화 계속

장점:

- 코드 변화 범위가 작다.

단점:

- 경로 수준 data/control 혼합이 그대로 남는다.
- hidden internal receiver / readiness bookkeeping / destroy ownership 부채가 그대로 남는다.
- 이미 여러 차례의 미세 최적화로도 main 근사치 복구에 실패했다.

결론:

- 1차 리팩터 이전 시점에는 기각 대상이었고,
  2차 리팩터 기준에서도 구조 부채를 해소하지 못하므로 다시 채택하지 않는다.

### 6.2 대안 B: data `PUB/XSUB` + control `XPUB/XSUB`

장점:

- 현재 control 의미를 옮기기 쉽다.
- socket family 차이가 작아서 이식 부담이 낮다.

단점:

- control plane도 다시 subscription side-effect에 기대기 쉽다.
- command/ack 의미를 topic/subscription으로 우회 표현하게 된다.
- long-term 관점에서 또 다른 암묵적 coupling이 생길 수 있다.

결론:

- 전환용 임시 단계로는 가능하다.
- 최종 구조로 채택하지는 않는다.

### 6.3 대안 C: data `PUB/XSUB` + 별도 peer control plane

control plane socket family 후보는 두 가지다.

- `PUB/SUB` 기반 명시적 control topic
- `DEALER/ROUTER` 기반 addressed RPC

`DEALER/ROUTER`는 command/ack 의미에는 잘 맞지만,
node-level 공유 socket으로 여러 peer를 다루려면
identity/bootstrap/routing 관리가 커진다.
현재 문제는 control 의미의 부재가 아니라
control이 data plane에 섞여 있다는 점이므로,
이번 리팩터의 1차 채택안은 더 단순한 `PUB/SUB` control plane으로 둔다.

결론:

- 최종 채택안은 `data PUB/XSUB + peer control PUB/SUB`다.
- 향후 control semantics가 더 복잡해질 경우에만
  `ROUTER` 계열로 재검토한다.

## 7. 채택안

### 7.1 목표 토폴로지

개편 후 SPOT topology는 다음과 같다.

```text
                 +----------------------+
                 |      spot_node       |
                 |                      |
pub handle ----> | local pub ingress    | --+
                 |                      |   |
sub handle <---- | local fanout         | <-+
                 |                      |
                 | data mesh pub  (PUB) | ----> peer data xsub
                 | data mesh xsub (XSUB)| <---- peer data pub
                 |                      |
                 | ctrl pub       (PUB) | ----> peer ctrl sub
                 | ctrl sub       (SUB) | <---- peer ctrl pub
                 +----------------------+
```

핵심은 다음 두 줄이다.

- `mesh_pub`는 다시 pure data sender `PUB`로 되돌린다.
- readiness/subscription/ack는 별도 `ctrl_pub`/`ctrl_sub`로 옮긴다.

### 7.2 왜 `PUB/SUB` control plane인가

이번 설계에서 control plane은
downstream subscription 관측이 아니라 명시적 message 교환이 목적이다.
따라서 `XPUB`의 핵심 기능이 더 이상 필요하지 않다.

`PUB/SUB` control plane의 장점:

- node-level 공유 socket 쌍으로 충분하다.
- target node/topic 단위 필터링이 가능하다.
- data plane과 동일한 connect/bind 운영 모델을 재사용할 수 있다.
- `ROUTER`류 identity 관리보다 구현 부담이 작다.

## 8. 상세 아키텍처

### 8.1 runtime 확장

`spot_runtime_t`에 아래 필드를 추가한다.

- `peer_ctrl_pub`
- `peer_ctrl_sub`
- `peer_ctrl_pub_endpoint`
- `peer_ctrl_node_id`

기존 내부 command 채널인 `data_ctrl_front`/`data_ctrl_back`은 그대로 유지한다.

**data path 불변식**: data path는 control backlog 크기와 무관하게
payload forwarding 계약만 보장한다. control churn이 아무리 커도
data path의 steady-state 설명은 변하지 않아야 한다.
이 불변식이 유지되는 한, 내부 스케줄링 정책은 구현 선택지에 불과하다.

이것은 Ousterhout의 "복잡성을 아래로 끌어내려라" 원칙의 직접 적용이다.
사용자(data path)는 control churn의 존재를 알 필요가 없고,
아래 계층(worker thread 스케줄링)이 그 복잡성을 떠안는다.

```text
data path 불변식 구조:

  payload publish
       │
       ▼
  ┌─ data fast path ──────────────────────┐
  │  ingress → fanout → mesh_pub (PUB)    │  ← control 상태와 무관
  │  이 설명은 control churn이 있어도     │
  │  변하지 않는다                        │
  └───────────────────────────────────────┘

  ┌─ control processing (같은 thread) ────┐
  │  ctrl_sub → peer state sync module    │  ← 별도 budget
  │  subscription/readiness/ack 처리      │
  │  data path에 간접 영향 없음           │
  └───────────────────────────────────────┘
```

현재 구현 선택:

- 단일 worker thread가 data plane과 peer control plane을 함께 poll한다.
- data socket 입력을 먼저 소진하고,
  control socket 입력은 tick당 bounded batch로만 처리한다.

성능 대응 옵션 (불변식이 유지되는 범위에서만 검토):

- budget으로도 churn 시 throughput 회귀가 남으면
  control worker 분리를 fallback으로 검토한다.
- 성능 기준은 "budget이 적절한가"가 아니라
  "control churn 시에도 data path 설명이 변하지 않는가"로 판단한다.

`peer_ctrl_node_id`는 endpoint에서 유도하지 않는다.
현재 runtime이 이미 가지는 session-scoped `node_id`를 재사용하거나,
같은 수준의 명시적 난수 id를 사용한다.

### 8.2 endpoint 정책

현재 runtime은 아래 inproc endpoint를 가진다.

- `pub_ingress_endpoint`
- `sub_fanout_endpoint`
- `data_ctrl_endpoint`

여기에 peer control용 network endpoint를 추가한다.

- `peer_ctrl_pub_endpoint`

중요한 원칙:

- control endpoint는 data endpoint에서 숨겨서 유도하지 않는다.
- discovery 또는 bootstrap message로 명시적으로 전달한다.
- control endpoint의 transport/security mode는 paired data endpoint와 같아야 한다.
- `tcp://`-`tcp://`, `tls://`-`tls://`, `ws://`-`ws://`, `wss://`-`wss://`처럼
  같은 family로 맞춘다.
- secure/insecure 혼합 쌍은 허용하지 않는다.
- 동일 peer에 대해 data와 control이 서로 다른 transport family를 쓰는 구성은
  invalid configuration으로 본다.

### 8.3 peer state sync module

control plane의 내부 프로토콜은 `peer state sync module`이라는
하나의 깊은 모듈로 캡슐화한다.
이 모듈의 **외부 인터페이스**는 아래 의미 수준으로만 정의한다.

| 외부 의미 | 설명 |
| --- | --- |
| **hello / peer discovery** | peer 연결 수립 및 identity 교환 |
| **subscription sync** | remote peer에게 local subscription mirror를 동기화 |
| **ready ack** | peer가 delivery-ready임을 선언 |
| **peer lost** | peer 연결 상실 또는 desync 감지 |

이 4가지 의미만 외부(node, runtime, readiness aggregate)에 노출된다.
readiness 조건이 추가되더라도 이 모듈 내부만 수정하면 된다.

Ousterhout의 비유로 설명하면, peer state sync module은
TCP가 패킷 손실을 자동 재전송하여 상위 계층에 reliable stream을 제공하듯,
epoch/sequence/heartbeat/resync 세부를 숨기고
상위 계층에 "subscription synced / ready / lost"만 제공하는 깊은 모듈이다.

```text
peer state sync module 정보 은닉 구조:

  ┌─ 외부 (node, runtime, readiness aggregate) ───┐
  │                                                │
  │  peer_state_sync.on_subscription_synced(...)   │
  │  peer_state_sync.on_ready_ack(...)             │ ← 의미 4개만 노출
  │  peer_state_sync.on_peer_lost(...)             │
  │                                                │
  ├────────── 정보 은닉 경계 ──────────────────────┤
  │                                                │
  │  ┌─ 숨겨진 내부 ─────────────────────────────┐ │
  │  │  subscription_epoch                       │ │
  │  │  control_sequence_no                      │ │
  │  │  snapshot_generation                      │ │
  │  │  heartbeat / timeout                      │ │
  │  │  RESYNC_REQUEST / gap detection           │ │
  │  │  snapshot vs delta 전환 규칙              │ │
  │  │  reserved topic prefix / frame codec      │ │
  │  └───────────────────────────────────────────┘ │
  └────────────────────────────────────────────────┘
```

**모듈 내부에 숨겨지는 프로토콜 세부** (구현 상세 — 부록 A 참조):

- reserved topic prefix (`__zlink.spot.ctrl.*`, `__zlink.spot.bootstrap.*`)
- multipart frame 구조, verb routing
- `subscription_epoch`, `control_sequence_no`, `snapshot_generation`
- heartbeat / timeout / `RESYNC_REQUEST` / gap detection
- snapshot vs delta 전환 규칙

이 세부가 상위 설계 문서의 핵심 설명으로 올라오면
readiness/lifecycle 변경 시 프로토콜 의미 전체를 다시 이해해야 하므로,
POSD 기준에서 얕은 모듈이 된다.

수용 기준은 "프로토콜 필드 수"가 아니라
"readiness 변경이 peer state sync module 한 곳에서 끝나는가"로 판단한다.

### 8.4 payload data path

steady-state payload path는 아래로 단순화한다.

1. local pub ingress가 payload를 받는다.
2. local subscriber가 있으면 `fanout`으로 전달한다.
3. remote peer가 있으면 `mesh_pub(PUB)`로 전달한다.

이 경로에서 제거되는 책임:

- remote subscription count 집계
- ready ack topic 생성/해석
- `mesh_pub` subscription inbox drain

remote peer가 payload forwarding 대상이 되는 조건은 명시적으로 제한한다.

- data 연결 완료
- control 연결 완료
- `HELLO`/`HELLO_ACK` 완료
- subscription snapshot sync 완료

즉 peer가 `bootstrap_pending` 또는 `ctrl_desynced` 상태면
해당 peer로의 remote payload forwarding은 하지 않는다.
이 구간에서 payload를 별도 queue에 보관하지도 않는다.
정책은 `hold without buffering`이 아니라 `not yet eligible for forwarding`이다.

예외:

- manual bootstrap용 reserved system topic 송신은 이 gating의 예외다.
- 이 경로는 사용자 payload가 아니라 peer bootstrap control metadata 전용이다.

### 8.5 subscription propagation

local sub가 새 filter를 등록하면 `spot_node_t`는
peer control plane에 `SUB_DELTA_ADD`를 broadcast한다.
unsubscribe는 `SUB_DELTA_REMOVE`를 broadcast한다.

peer가 새로 연결되면 다음 순서로 맞춘다.

1. `HELLO`
2. `HELLO_ACK`
3. local subscription snapshot 송신
4. snapshot 완료 확인
5. 이후 delta만 전송

이때 snapshot과 delta를 구분하기 위해 epoch를 둔다.

- `subscription_epoch`
- `snapshot_generation`
- `control_sequence_no`

peer는 더 오래된 epoch의 delta를 무시한다.
sequence gap이 감지되면 delta 적용을 중단하고
`RESYNC_REQUEST` 이후 새 snapshot 완료 전까지
그 peer의 subscription mirror를 authoritative하지 않은 것으로 본다.

### 8.6 readiness / first-delivery-ready

현재 readiness 계약은 유지한다.
단, 근거가 `mesh_pub XPUB` side-effect가 아니라 control protocol로 바뀐다.

#### sub readiness

- local sub filter 등록
- control plane으로 `SUB_DELTA_ADD`
- peer가 적용 후 `READY_PROBE` 또는 `READY_ACK` 절차 수행
- local node가 `FILTER_APPLIED` / `DELIVERY_READY_CHANGED`를 emit

#### pub readiness

- local pub는 subject별로 `ready source set`을 유지한다.
- remote peer가 자신이 그 subject를 전달 가능하다고 판단하면
  `READY_ACK`를 publish한다.
- local node는 peer node id 단위로 ready source set을 갱신한다.
- set cardinality를 기반으로
  `DELIVERY_READY_CHANGED`와 `FIRST_DELIVERY_READY_CHANGED`를 emit한다.

### 8.7 disconnect / loss 처리

peer control 또는 data 연결이 끊기면 해당 peer의 state를 즉시 내린다.

- peer가 제공하던 ready source 제거
- peer가 가진 subscription mirror 제거
- 필요 시 facade monitor에 peer down / ready down 전파

`ctrl_desynced` 진입도 같은 보수 정책을 따른다.
즉 연결은 살아 있어도 해당 peer의 ready source는 즉시 제거하고,
resync 완료 후 새 snapshot/ack 기준으로만 다시 구성한다.

disconnect는 retry loop가 아니라 state 전이로 본다.
복구는 새 `HELLO`/snapshot cycle로 다시 시작한다.

## 9. bootstrap / discovery / manual peer 연결

### 9.1 discovery 기반 연결

discovery provider metadata에 아래를 함께 넣는다.

- peer data pub endpoint
- peer ctrl pub endpoint
- peer control node id
- protocol version

discovery-managed peer는 이 metadata를 보고
data와 control을 각각 connect한다.

### 9.2 manual `connect_peer_pub()` 호환

manual peer 연결은 현재 data endpoint만 인자로 받는다.
hidden endpoint 유도는 금지이므로, control endpoint를 data endpoint 문자열에서
계산해서는 안 된다.

따라서 manual path는 다음 bootstrap 절차를 둔다.

1. 우선 data peer만 연결한다.
2. reserved system topic으로 자신의 control descriptor를 보낸다.
3. 상대도 같은 방식으로 control descriptor를 보낸다.
4. 양쪽이 `ctrl_pub`/`ctrl_sub`를 연결한다.
5. `HELLO`/snapshot sync가 끝나면 peer를 forwarding eligible로 승격한다.
6. 이후 steady-state control은 data plane을 더 이상 사용하지 않는다.

즉 data plane의 reserved system topic 사용은 bootstrap 한정이다.
steady-state에서 control을 다시 data mesh에 태우지 않는다.

중요한 정책:

- bootstrap 완료 전 peer 상태는 `bootstrap_pending`이다.
- `bootstrap_pending` peer는 active remote forwarding peer로 취급하지 않는다.
- 이 구간에서 payload를 따로 buffer하지 않는다.
- 따라서 manual peer 연결 직후 publish된 data는 local path만 흐르고,
  해당 remote peer에는 bootstrap 완료 후부터만 전달된다.
- application이 최초 remote 전달을 요구하면
  `DELIVERY_READY_CHANGED` 또는 `FIRST_DELIVERY_READY_CHANGED`를 기준으로
  publish 시점을 제어해야 한다.

이 정책은 undefined race를 없애고
strong readiness 계약과도 자연스럽게 맞는다.

manual bootstrap을 위해 각 node의 `mesh_xsub`에는
`__zlink.spot.bootstrap.` prefix에 대한 runtime-owned internal subscription을
상시 유지한다.
이 subscription은 user child/default sub 전제가 아니라
node transport bootstrap을 위한 명시적 system subscription이다.

### 9.3 향후 API 정리 후보

장기적으로는 manual peer API를 아래 형태로 정리하는 편이 낫다.

```c
zlink_spot_connect_peer_ex(spot, data_endpoint, ctrl_endpoint)
```

다만 이번 리팩터의 1차 목표는 성능 복구와 계약 보존이므로,
기존 API 호환을 깨지 않는 bootstrap 방식으로 먼저 진행한다.

## 10. 상태 모델

peer마다 아래 상태를 둔다.

- `peer_node_id`
- `peer_data_endpoint`
- `peer_ctrl_endpoint`
- `bootstrap_pending`
- `data_forwarding_enabled`
- `ctrl_connected`
- `ctrl_desynced`
- `hello_exchanged`
- `subscription_snapshot_synced`
- `subscription_epoch_seen`
- `snapshot_generation_seen`
- `last_control_sequence_seen`
- `ctrl_heartbeat_deadline`
- `ready_sources_by_filter`

local node 공통 상태:

- `local_subscription_epoch`
- `local_ctrl_descriptor_version`
- `pending_ready_probes`
- `pub_delivery_ready_sources`

자료구조 권장안:

- `pending_ready_probes`:
  `std::map<ready_probe_key_t, ready_probe_state_t>`
- `ready_probe_key_t`:
  `(target_peer_node_id, raw_filter)`
- `ready_sources_by_filter`:
  `std::map<std::string, std::set<uint64_t> >`

필요한 불변식:

- `FIRST_DELIVERY_READY_CHANGED`는 peer ready source set의 실제 변화에만 반응한다.
- 같은 peer의 중복 `READY_ACK`는 count를 증가시키지 않는다.
- disconnect 이후 stale ack는 무시한다.
- snapshot 이전 delta는 적용하지 않는다.

## 11. 구현 단계

### 11.1 1단계: runtime 뼈대 추가

- `spot_runtime_t`에 `peer_ctrl_pub/sub`와 endpoint 추가
- bind/connect/close lifecycle 정리
- 아직 동작 전환은 하지 않는다

완료 기준:

- build 및 기존 SPOT 테스트가 깨지지 않는다.

### 11.2 2단계: control protocol 골격 추가

- control topic/verb enum 또는 상수 정의
- `HELLO`, `HELLO_ACK`, snapshot/delta frame codec 추가
- peer state table 추가

완료 기준:

- control descriptor 송수신 로그까지 확인 가능
- payload 경로는 아직 기존과 동일

### 11.3 3단계: subscription mirror 이관

- local subscription add/remove를 control plane으로 전송
- peer별 snapshot sync 구현
- 현재 `mesh_pub XPUB` 경로와 병행하여 shadow compare

shadow compare 정책:

- 기본 release/perf 실행에서는 끈다.
- debug 또는 명시적 internal diagnostic flag에서만 켠다.
- perf acceptance 수치는 shadow compare가 꺼진 상태에서만 측정한다.

완료 기준:

- 두 경로의 effective remote subscription set이 일치한다.

### 11.4 4단계: readiness 이관

- `ready_probe`
- `ready_ack`
- `pub_delivery_ready_sources`

를 control plane 기반으로 이관한다.

완료 기준:

- `SPOT_SUB_FILTER_APPLIED`
- `SPOT_SUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

관련 기존 테스트가 유지된다.

### 11.5 5단계: data plane 단순화

- `mesh_pub`를 `PUB`로 복구
- data plane poll set에서 `mesh_pub` control 입력 제거
- `ready_ack` filter 파싱 제거
- remote data subscription count bookkeeping 제거

완료 기준:

- remote mesh steady-state가 data forwarding 중심으로 단순화된다.

### 11.6 6단계: bootstrap / discovery 정리

- discovery metadata 확장
- manual peer bootstrap reserved topic 정리
- protocol version mismatch 처리

완료 기준:

- discovery/manual 둘 다 control plane을 형성할 수 있다.

### 11.7 7단계: 정리와 제거

- current XPUB 기반 control 잔재 제거
- dead field/dead code 정리
- 문서/API 설명 갱신

## 12. 검증 계획

### 12.1 기능 검증

우선 유지해야 할 핵심 테스트:

- `test_spot_pubsub_scenario`
- `test_spot_service_introspection`
- `test_monitor_service_contract`

추가로 확인할 것:

- peer connect/disconnect 반복 시 ready count 정확성
- subscription snapshot 직후 delta ordering
- stale ack 무시
- manual peer bootstrap 후 control plane 전환
- peer 수가 많고 subscription churn이 큰 경우에도 data throughput이 급락하지 않는지

### 12.2 성능 검증

성능 검증 순서는 고정한다.

1. single SPOT 먼저 main 근사치까지 복구
2. single GATEWAY 확인
3. multi를 `core/perf/results/multi/report/perf_linux_20260314_002645.txt`와 비교
4. 마지막에만 `perf full` 전체 패턴/사이즈 무실패 확인

single SPOT의 1차 수치 목표:

- 아래 90% 기준은 `single -> multi` 진행을 허용하는 최소 gate다.
- 최종 목표치는 이보다 높다.

- `tcp/131072`: `46.204 Kmsg/s`의 90% 이상
- `tcp/262144`: `25.550 Kmsg/s`의 90% 이상

즉 대략:

- `tcp/131072 >= 41.6 Kmsg/s`
- `tcp/262144 >= 23.0 Kmsg/s`

최종 지향 수치:

- 구조 분리 후 안정화 단계에서는 main의 95% 이상을 우선 목표로 둔다.
- 90% 이상 95% 미만에 머무르면 perf profile 근거 없이 종료하지 않는다.

### 12.3 회귀 방지 포인트

- large payload bench-only 최적화 금지
- readiness contract 약화 금지
- hidden endpoint derivation 금지
- default child/default sub 전제 금지
- `iothread` 증가는 진단/튜닝용으로만 사용하고 구조 개선의 대체재로 쓰지 않음

## 13. 리스크와 대응

### 13.1 bootstrap 복잡도 증가

manual peer path는 control endpoint를 별도로 알아야 한다.
숨은 규칙으로 endpoint를 유도하면 안 되므로 bootstrap 절차가 필요하다.

대응:

- data plane bootstrap은 connect 직후 한정된 reserved system topic으로만 사용
- steady-state control은 반드시 분리

### 13.2 snapshot/delta ordering 버그

subscription mirror는 epoch가 없으면 stale delta 적용 문제가 생긴다.

대응:

- snapshot generation
- local subscription epoch
- peer별 last applied epoch

를 명시적으로 둔다.

### 13.3 ready count 중복/누락

같은 peer의 중복 ack 또는 disconnect 후 stale ack는
`FIRST_DELIVERY_READY_CHANGED`를 쉽게 깨뜨린다.

대응:

- peer node id 단위 set 기반 집계 유지
- disconnect 시 peer source 전부 제거
- snapshot 재동기화 시 old epoch ack 무시

### 13.4 control plane이 다시 hot path에 새 비용을 만들 위험

control 분리를 해도 구현을 잘못하면 payload 전송 함수 안에
control 상태 분기가 다시 들어갈 수 있다.

대응:

- payload forwarding 함수는 payload 전달 외 책임을 갖지 않게 강제
- control 수신은 별도 handler로 제한
- same-thread budget으로도 throughput 영향이 남으면 control worker 분리 검토

### 13.5 control backlog가 다시 성능 병목이 될 위험

control plane도 subscription delta와 ack가 누적되면 backlog가 쌓일 수 있다.
이 경우 data plane은 분리돼도 state sync가 늦어져 readiness가 흔들릴 수 있다.

대응:

- data/control socket HWM을 분리해서 관리
- 같은 filter에 대한 연속 delta는 coalesce
- peer별 sync 상태가 깨지면 delta 누적 대신 snapshot resync로 강등
- 모든 control message에 sequence number를 싣고 gap detection 수행
- heartbeat timeout도 resync trigger로 사용

## 14. 최종 결론

현재 회귀의 본질은 `SPOT에서 control 의미가 data socket에 얹혀 있는 구조`다.
따라서 근본적인 해결은 미세 최적화 누적이 아니라
`data/control plane 분리`여야 한다.

이번 리팩터의 최종 방향은 다음과 같다.

- data mesh는 `PUB/XSUB`로 복구한다.
- monitor/readiness 계약은 node-level peer control plane으로 유지한다.
- 추가 네트워크 소켓은 `spot_node`당 `ctrl_pub`/`ctrl_sub` 한 쌍만 둔다.
- discovery/manual bootstrap은 명시적 descriptor로 처리한다.

이 설계가 완료되면,
SPOT payload 경로는 다시 main과 같은 성격의 단순한 fast path를 가지면서도
현재 monitor/readiness contract를 유지할 수 있다.

## 15. 새 컨텍스트 실행 지침

이 절은 새 컨텍스트에서 이 문서만 읽고
구조 개편부터 single/multi 성능 복구, 최종 perf full 검증까지
중단 없이 진행하기 위한 실행 규칙이다.

### 15.1 기준과 종료 조건

기준 파일:

- single main 기준:
  `/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260314_003239.txt`
- multi main 기준:
  `/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260314_002645.txt`

종료 조건은 아래 3개를 모두 만족할 때만 충족된다.

1. single의 SPOT, GATEWAY가 main 근사치까지 회복됨
2. multi의 SPOT, GATEWAY가 main 근사치까지 회복됨
3. `perf full` single/multi 전체 실행에서 실패가 없음

이 문서에서 `main 근사치`의 실행 기준은 다음으로 둔다.

- 우선 목표: main 대비 95% 이상
- 최소 gate: main 대비 90% 이상
- 단, SPOT/GATEWAY 주요 tcp 구간에서 95% 미만이면
  perf profile 근거 없이 종료하지 않는다.
- full report에서 SPOT/GATEWAY 어떤 셀이라도 90% 미만이면 종료하지 않는다.

### 15.2 작업 방식

- 목표가 닫히기 전까지 중간 보고, 대기, 확인 요청 없이 계속 진행한다.
- 새로운 의미 있는 변경이 나오면 바로 구현, 빌드, 측정, 비교까지 이어서 한다.
- 멈출 수 있는 경우는 아래뿐이다.
  - 현재 변경과 직접 충돌하는 사용자 수정이 새로 들어온 경우
  - 환경 자체가 깨져서 빌드/실행이 불가능한 경우
  - destructive action이 필요한데 사용자가 명시적으로 요청하지 않은 경우

즉 기본 동작은 `계속 작업`이며,
마지막 보고는 종료 조건을 만족했을 때만 한다.

### 15.3 새 컨텍스트 시작 시 첫 확인 항목

1. 이 문서를 먼저 끝까지 읽는다.
2. `git status --short`로 dirty tree를 확인한다.
3. unrelated diff는 건드리지 않는다.
4. SPOT/GATEWAY core path 관련 변경만 본다.
5. 현재 worktree에서 이미 수정된 아래 파일은 우선 검토 대상이다.

- `core/src/services/spot/spot_data_plane.cpp`
- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_node.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_pub.hpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_sub.hpp`
- `core/src/sockets/fq.cpp`
- `core/src/sockets/router.cpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/xpub.cpp`
- `core/src/sockets/xpub.hpp`
- `core/src/sockets/xsub.cpp`
- `core/src/sockets/xsub.hpp`

### 15.4 금지사항 재확인

- perf 수치만 올리기 위한 harness 특화 코드 금지
- large payload zero-copy 같은 bench-only 최적화 금지
- hidden default child/default sub 전제 금지
- benchmark에서만 켜는 내부 shortcut 금지
- `PERF_IO_THREADS` 증가는 진단 용도로만 사용
- 최종 성능 복구를 perf 환경변수 조합에만 의존해서 닫지 않음

`PERF_IO_THREADS`를 실험해도 된다.
하지만 최종 완료는 core 구조 개선으로 설명 가능해야 하며,
기본 동작이 아닌 perf-only 설정값에 기대서는 안 된다.

### 15.5 실행 전 체크리스트

실제 코드 변경 전후에 아래를 반드시 점검한다.

- hidden internal receiver가 public child lifecycle에 섞여 있지 않은가
- readiness source / snapshot / ack ownership이 한 곳으로 수렴하는가
- payload forwarding 함수가 control bookkeeping을 다시 떠안지 않는가
- destroy ownership이 handle, node, runtime, data-plane 사이에서 중복되지 않는가
- manual bootstrap과 discovery path가 같은 control-plane 규칙으로 수렴하는가
- thread-safe acceptance와 perf-contract가 같은 구조 설명으로 이어지는가

### 15.6 실행 순서

항상 아래 순서로 진행한다.

1. 구조 개편을 먼저 진행한다.
2. single SPOT를 main 근사치까지 끌어올린다.
3. single GATEWAY를 확인하고 필요한 회귀를 닫는다.
4. multi SPOT/GATEWAY를 main 기준과 비교하며 닫는다.
5. 마지막에만 full single/multi 전체 패턴을 돈다.

single이 닫히기 전에는 multi나 full에 시간을 쓰지 않는다.
full은 오직 최종 게이트다.

### 15.7 권장 perf 실행 명령

집중 측정:

```bash
core/perf/run_benchmarks.sh \
  --reuse-build \
  --pattern SPOT,GATEWAY \
  --transports tcp \
  --msg-sizes 131072,262144
```

multi 집중 측정:

```bash
core/perf/run_benchmarks_multi.sh \
  --reuse-build \
  --pattern SPOT,GATEWAY \
  --transports tcp \
  --msg-sizes 131072,262144
```

진단용 `iothread` 실험:

```bash
core/perf/run_benchmarks.sh \
  --reuse-build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 131072,262144 \
  --io-threads 2
```

최종 full 게이트:

```bash
core/perf/run_benchmarks.sh --reuse-build --pattern ALL
core/perf/run_benchmarks_multi.sh --reuse-build --pattern ALL
```

필요 시 `--clean-build`, `--duration`, `--runs`, `--output`을 사용한다.
단, 최종 acceptance에 쓰는 수치는 perf 정책 기본값과 크게 다른 실험 설정이 아니어야 한다.

### 15.8 각 단계의 완료 기준

#### 15.8.1 구조 개편 단계

아래가 모두 만족되기 전에는 구조 개편이 끝난 것이 아니다.

- `mesh_pub`가 다시 data 전용 `PUB`가 됨
- peer control plane이 subscription/readiness를 담당함
- manual/discovery bootstrap이 동작함
- readiness contract 테스트가 유지됨

#### 15.8.2 single 단계

아래 순서로 확인한다.

1. focused single run으로 SPOT/GATEWAY tcp 대형 payload 비교
2. 결과 report를 main single 기준 파일과 비교
3. SPOT가 main 근사치에 도달할 때까지 반복
4. GATEWAY 회귀가 있으면 함께 닫음

single 단계는 아래를 만족해야 통과다.

- SPOT 주요 tcp 셀이 95% 이상에 근접
- GATEWAY 주요 tcp 셀이 95% 이상에 근접
- full single report에서 SPOT/GATEWAY 셀 중 90% 미만이 없음
- single 실행 실패가 없음

#### 15.8.3 multi 단계

single이 닫힌 뒤에만 진행한다.

1. focused multi run으로 SPOT/GATEWAY tcp 대형 payload 비교
2. 결과 report를 main multi 기준 파일과 비교
3. peer churn, readiness, bootstrap 경로에서 회귀가 없는지 확인
4. throughput과 failure를 함께 본다

multi 단계는 아래를 만족해야 통과다.

- SPOT 주요 tcp 셀이 95% 이상에 근접
- GATEWAY 주요 tcp 셀이 95% 이상에 근접
- full multi report에서 SPOT/GATEWAY 셀 중 90% 미만이 없음
- multi 실행 실패가 없음

### 15.9 구현 우선순위

single SPOT가 여전히 크게 낮으면 아래 순서로 본다.

1. `spot_data_plane.cpp`의 data/control 분리 완료 여부
2. `spot_node.cpp`의 subscription/readiness state machine
3. `spot_pub.cpp`, `spot_sub.cpp`의 event/readiness bookkeeping
4. `xpub.cpp`, `xsub.cpp`, `fq.cpp`, `router.cpp`, `socket_base.cpp`의
   socket hot path 영향

판단 기준:

- local-only는 빠른데 remote mesh만 느리면 SPOT peer path 문제다.
- raw `PUBSUB`가 main과 가깝다면 generic transport 문제가 아니다.
- `iothread` 증가로만 개선되면 구조 병목이 숨은 것일 가능성이 높다.

### 15.10 검증 순서

구조 변경 후에는 아래 순서를 반복한다.

1. 관련 unit/integration/e2e 테스트
2. focused single
3. single 기준 비교
4. focused multi
5. multi 기준 비교
6. 마지막 full single/multi

최소 유지 테스트:

- `test_spot_pubsub_scenario`
- `test_spot_service_introspection`
- `test_monitor_service_contract`

필요 시 관련 GATEWAY 테스트도 추가한다.

### 15.11 문서만으로 판단이 안 될 때의 우선순위

판단이 애매하면 아래 원칙을 따른다.

1. perf-only가 아닌 core hot path 개선인가
2. readiness contract를 약화하지 않는가
3. hidden internal assumption을 추가하지 않는가
4. single SPOT main 근사치 복구에 직접 연결되는가

위 조건을 만족하면 구현하고 측정한다.
만족하지 않으면 버리고 다음 가설로 바로 넘어간다.

## 16. 2026-03-16 현재 구현 상태 점검

이 절은 문서 작성 이후 실제 코드가 어디까지 왔는지,
그리고 어디서 다시 복잡도가 생겼는지를 정리한다.

### 16.1 현재 구현에서 이미 유지되고 있는 항목

다음 항목은 이 문서의 1차 채택안과 현재 코드가 대체로 일치한다.

- data plane과 peer control plane은 분리돼 있다.
  - 현재 runtime은 `mesh_pub(PUB)`, `mesh_xsub(XSUB)`,
    `peer_ctrl_pub(PUB)`, `peer_ctrl_sub(SUB)`를 사용한다.
- peer control plane은 별도 socket pair로 subscription/readiness를 운반한다.
- bootstrap/control descriptor 기반 peer control endpoint 연결이 구현돼 있다.
- runtime 내부 command 채널(`data_ctrl_front/back`)은 유지되며,
  data/control 처리는 단일 worker thread poll loop에서 budget 기반으로 소화한다.
- `SPOT_PUB_DELIVERY_READY_CHANGED`,
  `SPOT_SUB_DELIVERY_READY_CHANGED` 같은 monitor 계약은
  control message 기반으로 계산하는 방향으로 이미 이동했다.

즉 “plane 분리 자체가 아직 안 됐다”가 현재 문제는 아니다.
현재 문제는 1차 구조 위에 남은 hidden coupling과
수명/책임 경계의 불명확성이다.

### 16.2 현재 구현이 문서 원칙과 어긋난 지점

다음 항목은 현재 코드가 이 문서의 중요한 원칙을 충분히 만족하지 못하는 부분이다.

#### 16.2.1 hidden default sub 의존이 아직 남아 있다

문서 3.1의 핵심 원칙은
`hidden default child/default sub 같은 내부 전제에 기대지 않는다`였다.

그러나 현재 `zlink_spot_node_new(..., handler)` 경로는
node-level inbound handler를 설치하기 위해
암묵적으로 `ensure_default_sub()`를 호출해 hidden default sub를 만든다.

이 hidden default sub는 다음 특성을 가진다.

- user-visible child handle이 아니다.
- 그러나 실제 구현에서는 일반 `spot_sub_t` attachment/lifecycle을 공유한다.
- node destroy, child destroy, runtime drain과 얽혀 teardown bug를 만들 수 있다.

즉 “node 내부 수신기”가 독립된 internal receiver 타입이 아니라
public sub 구현체의 숨은 인스턴스로 남아 있는 것이 현재 구조 부채다.

#### 16.2.2 public handle과 node internal receiver가 같은 attachment 경로를 공유한다

현재 `spot_pub_t`, `spot_sub_t`, hidden default sub 모두
같은 attachment 생성/삭제 경로를 공유한다.

이 구조의 문제는 다음과 같다.

- user-visible handle은 explicit destroy semantics를 가진다.
- node internal receiver는 node lifecycle에 종속돼야 한다.
- 그런데 현재는 둘이 같은 `destroy_attachment()`/`destroy_attachment_async()`
  경로를 써서 close timing과 drain 책임이 섞인다.

지금 남아 있는 `spot scaling teardown timeout`도
이 공유 경로 때문에 hidden default sub attachment와 `fanout`이
동시에 drain되지 못하는 케이스로 수렴한다.

#### 16.2.3 readiness source가 아직 완전히 단일화되지 않았다

문서 5.3과 8.6은 readiness를 명시적 protocol로 옮기되,
그 의미를 명확한 state machine으로 유지하는 것을 목표로 했다.

현재는 방향은 맞지만 bookkeeping이 여전히 여러 레이어에 퍼져 있다.

- `spot_sub_t`는 local raw filter, ready endpoint, ready ack endpoint를 가진다.
- `spot_node_t`는 pub-side ready source aggregate를 가진다.
- `spot_data_plane_t`는 peer snapshot과 ready ack snapshot을 동시에 중계한다.

이 상태에서는 같은 의미의 변화가
subscription snapshot, ready ack snapshot, peer loss cleanup으로
중복 전달될 수 있다.

즉 현재 readiness contract는 “대체로 맞는 값”을 만드는 수준이지,
완전히 단일한 semantic source를 가진 상태는 아니다.

#### 16.2.4 destroy ownership이 아직 한 곳으로 모이지 않았다

문서 전체의 방향은
data plane은 payload/control processing만 하고,
lifecycle close ownership은 명확한 단일 경로로 모으는 것이었다.

현재는 다음 책임이 여전히 나뉘어 있다.

- handle destroy
- node destroy
- runtime close
- data-plane stop
- attachment async/sync close

이 중 일부는 이미 정리됐지만,
특히 `spot` shutdown에서는 여전히
“누가 어떤 socket/attachment를 언제까지 drain 책임지는가”가
완전히 단일화되지 않았다.

### 16.3 teardown bug 해결과 남은 구조 부채

`spot` thread-safe scaling teardown timeout은 core 수정으로 해결되었다.
(상세:
[`2026-03-16-spot-threadsafe-destroy-timeout-tracked-sockets.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/bug/threadsafe/2026-03-16-spot-threadsafe-destroy-timeout-tracked-sockets.md))

실제 root cause는 단일 문제가 아니라 아래 4건의 조합이었다.

1. `connect_peer_pub()`가 remote control endpoint를 local `node_id`로
   잘못 파생해 pending `inproc` connection을 남김
2. 일부 internal socket이 poll 대상이 아니어서
   cross-thread termination command를 제때 소모하지 못함
3. pipe termination이 `waiting_for_delimiter` 상태에서
   즉시 ack로 수렴하지 못하는 edge case
4. 64-handle scaling 계약에서 default socket cap이 낮아
   fixture 자체가 resource ceiling에 걸림

이 4건은 모두 core 수정으로 닫혔고,
`raw/gateway/spot` scaling 계약 테스트가 통과한다.

그러나 이 bug가 드러낸 구조 부채는 여전히 유효하다.

- hidden default sub가 일반 public sub와 같은 attachment destroy path를 쓰는 구조는
  이런 종류의 teardown 불일치를 발생시키기 쉬운 토양이다.
- data plane thread, default sub destroy, node destroy가
  같은 socket/attachment에 대해 중복 close를 시도할 수 있는 3-way close 위험이
  구조적으로 남아 있다.
- 현재 fix는 개별 원인을 각각 막았지만,
  close ownership 분리 없이는 유사 패턴의 재발 가능성이 존재한다.

따라서 2차 리팩터(17-18절)의 동기는 이 bug 자체가 아니라,
이 bug가 쉽게 발생할 수 있었던 구조적 토양을 제거하는 것이다.

## 17. 2차 리팩터 목표

이 절은 1차 `plane 분리` 이후 남은 2차 단순화 리팩터 목표를 정의한다.

2차 리팩터는 15절의 perf 복구 종료 조건과 독립적으로 진행한다.
15절은 1차 plane 분리 + perf 복구를 위한 실행 지침이고,
17-18절은 1차 구조 위에 남은 구조 부채를 정리하는 별도 작업이다.
다만 2차 리팩터가 1차에서 달성한 perf 수준을 후퇴시키지 않는 것은
필수 조건이다(19.2절 참조).

### 17.1 핵심 문제 정의: hidden default sub 의존

2차 리팩터의 중심 문제는 hidden default sub 의존이다.

현재 남아 있는 hidden default sub 의존은 내부 구현 세부가 아니라
public/node API 경로의 구조 문제다.
node-level handler install, node subscribe/unsubscribe, node monitor open이
모두 `ensure_default_sub()`를 통해 implicit sub 생성을 유발하므로,
2차 리팩터의 목표는 "default sub를 없앤다"가 아니라
"명시적 node-internal receiver와 user-visible handle을 분리하고,
API helper가 implicit default handle 생성에 기대지 않게 만든다"이다.

이 hidden default sub는:

- node-level inbound dispatch의 유일한 수단이다.
- 일반 public sub와 같은 `spot_attachment_t` lifecycle을 공유한다.
- teardown 시 data plane / default sub / node가 같은 socket/attachment를
  중복 close하는 3-way close 위험의 직접적 원인이다.
- readiness bookkeeping이 여러 레이어에 분산되는 근본 원인이기도 하다.

즉 hidden default sub 의존이 나머지 세 가지 부채
(destroy ownership 분산, attachment 경로 공유, readiness source 중복)의
공통 토양이다. 이 의존을 제거하면 나머지 문제는
자연스럽게 해소 가능한 범위로 축소된다.

### 17.2 목표 요약

위 핵심 문제를 포함해 2차 리팩터는 아래 네 줄을 달성한다.

- hidden default sub를 일반 public sub lifecycle에서 분리한다.
- node internal receiver를 별도 internal runtime component로 격리한다.
- readiness source를 단일 semantic source 기준으로 재정렬한다.
- destroy ownership을 `node destroy -> runtime drain` 중심으로 단일화한다.

### 17.3 구체 목표

#### 17.3.1 node internal receiver 분리

현재 hidden default sub가 담당하는 node-level inbound dispatch를
별도 internal receiver 타입으로 옮긴다.

이 internal receiver는 다음 성질을 가져야 한다.

- public `zlink_spot_t`가 아니다.
- `spot_sub_t` destroy semantics를 재사용하지 않는다.
- `spot_node_t` 또는 `spot_runtime_t` 수명에 완전히 종속된다.
- monitor child, callback self-close, public child busy 규칙의 대상이 아니다.

internal receiver 도입 시 기존 public sub attachment와 lifecycle을 공유하지 않아야 하며,
implicit default sub 생성 경로의 대체물로만 사용돼야 한다.
user-visible sub handle 생성/파괴와 node internal receiver는
서로 독립적으로 선형화돼야 한다.

추가로 아래 책임 경계를 명시적으로 고정한다.

- `zlink_spot_node_new(..., handler)`:
  internal receiver가 node-level inbound dispatch를 담당한다.
- node-level subscribe/unsubscribe:
  user-visible `spot_sub_t` 생성이 아니라
  internal receiver 또는 동등한 node-owned subscription store를 갱신한다.
  internal receiver의 subscription은 runtime의 `mesh_xsub`에 직접 반영되며,
  data plane의 user-visible subscription 경로를 거치지 않는다.
  이 합류 지점의 정확한 구현(internal receiver가 `mesh_xsub`에 직접 subscribe하는지,
  또는 runtime-level internal subscription 경로를 별도로 두는지)은
  18.2 단계에서 확정한다.
- node-level `SUB` monitor open:
  public child `spot_sub_t`에 붙지 않고,
  node-owned inbound/summary source에 붙는다.

즉 node-level `SUB` semantics는 "hidden public sub"의 부산물이 아니라
별도 node abstraction의 계약으로 다시 정의돼야 한다.

node-level `SUB` monitor contract는 아래처럼 고정한다.

| monitor surface | authoritative source | 포함 이벤트 | 제외 이벤트 |
| --- | --- | --- | --- |
| public `spot_sub_t` monitor | user-visible sub handle | handle socket peer/ready/error, handle-local delivery-ready | node-level inbound dispatch lifecycle |
| node-level `SUB` monitor | node-owned inbound receiver / node summary source | node inbound dispatch, node-level readiness/summary, node-owned terminal event | public child busy/self-close semantics |
| internal receiver diagnostics | internal only | implementation diagnostics only | public monitor contract 전체 |

즉 node-level `SUB` monitor는 public sub monitor의 alias가 아니며,
internal receiver가 만든 normalized node view를 관측하는 contract로 본다.

#### 17.3.2 attachment 클래스 분리

attachment를 최소 두 클래스로 나눈다.

- public child attachment
- node-owned internal attachment

두 클래스는 생성 API, destroy ownership, drain 전략을 분리한다.

핵심 원칙:

- public child attachment는 explicit handle destroy semantics를 따른다.
- internal attachment는 node/runtime destroy semantics를 따른다.
- 두 경로가 같은 helper를 쓰더라도
  ownership과 wait policy를 공유해서는 안 된다.

#### 17.3.3 readiness aggregate source 정규화

pub-side delivery ready aggregate는
오직 explicit ready-ack semantic source만을 기반으로 계산한다.

subscription snapshot과 ready snapshot의 역할은 분리한다.

- subscription snapshot:
  peer가 어떤 raw filter를 mirror하고 있는지 동기화
- ready ack snapshot:
  peer가 실제로 delivery-ready임을 선언
- pub aggregate:
  ready ack source set의 cardinality만을 반영

즉 “subscription mirror”와 “publisher-visible ready source”는
같은 map이나 같은 source key namespace를 공유하지 않게 한다.

#### 17.3.4 destroy ownership 단일화

현재 구조에도 graceful shutdown과 abortive fallback이 이미 존재한다.
따라서 2차 리팩터의 목표는 destroy 순서를 새로 발명하는 것이 아니라,
control task state, handle teardown, runtime socket teardown의
authoritative owner를 더 명확히 하고
각 단계의 terminal condition을 구조적으로 설명 가능하게 만드는 것이다.

최종 구조의 destroy ownership은 아래 규칙으로 고정한다.

- data plane thread:
  stop signal과 worker loop 종료만 담당
- runtime:
  internal socket close/drain만 담당
- public handle destroy:
  public attachment close만 담당
- node destroy:
  public child 정리 -> internal receiver 정지 -> runtime close/drain 순서만 담당

즉 같은 socket/attachment를 둘 이상의 레이어가 close하려고 시도하지 않게 한다.

## 18. 2차 리팩터 구현 단계

구현 순서의 핵심 원칙:
destroy ownership 단일화를 가장 먼저 수행한다.
close ownership이 정리되지 않은 상태에서 다른 리팩터를 진행하면,
새 타입이나 새 경로가 또 다른 close 중복을 만들 위험이 있다.
따라서 close ownership 정리가 나머지 모든 단계의 안전망이 된다.

### 18.1 1단계: destroy ownership 단일화

현재 data plane thread, default sub destroy, node destroy가
같은 socket/attachment에 대해 중복 close를 시도하는 3-way close 구조를 제거한다.

구체적으로:

- data plane thread의 `close_socket_ptr()` 호출을 제거한다.
  data plane은 `terminate` 시 event loop 종료와 endpoint `term_endpoint()`만 수행하고,
  소켓 자체를 close하지 않는다.
- runtime이 data plane thread join 이후 모든 internal socket을
  `close_socket_and_wait()`로 close하는 단일 경로를 확보한다.
- public handle destroy는 public attachment close만 담당한다.
- node destroy는 `public child 정리 → runtime close/drain` 순서만 담당하며,
  직접 socket close를 호출하지 않는다.

순서 보장:

```text
data plane terminate command
  → data plane thread: event loop 종료, endpoint term (close 안 함)
  → thread join 완료
  → runtime: internal socket close_socket_and_wait() (단일 소유자)
```

data plane thread가 internal socket을 생성하더라도,
생성 직후 runtime slot에 publish된 시점부터
close ownership은 runtime shutdown contract로 귀속된다.
terminate 이후에는 data plane이 직접 socket close를 수행하지 않고,
join 완료 후 runtime이 단일 close owner로 정리한다.
join 자체가 happens-before를 보장하므로 포인터 접근은 안전하지만,
"어느 시점에 runtime slot에 publish되는가"를 코드에서 명시해야 한다.

완료 기준:

- 같은 socket/attachment를 둘 이상의 레이어가 close하려고 시도하는 경로가 없다.
- `spot` scaling teardown이 추가 타이밍 완화 없이 안정적으로 통과한다.
- abortive shutdown fallback은 “예외 상황”으로만 남고 정상 경로에서 의존하지 않는다.
- 기존 테스트(`test_spot_pubsub_scenario`, `test_spot_service_introspection`,
  `test_monitor_service_contract`, `test_thread_safe_scaling_spot`)가 유지된다.

POSD 판단 기준:

- close/destroy 의미를 설명하기 위해 알아야 하는 클래스 수가 줄어들어야 한다.
- 정상 종료와 abortive fallback의 경계가 코드에서 즉시 읽혀야 한다.
- "누가 마지막 close owner인가"라는 질문에 한 문장으로 답할 수 있어야 한다.
- `unset`이 "없어도 정상"을 정의하듯,
  `spot`도 "누가 지금 닫아야 하는가"라는 질문 자체가 없어져야 한다.

### 18.2 2단계: internal receiver 모델링 및 타입 분리

hidden default sub의 역할을 분석하고 별도 internal receiver 타입으로 분리한다.
1단계에서 close ownership이 정리된 상태이므로,
새 타입이 기존 close 중복 문제를 재도입할 위험이 없다.

구체적으로:

- hidden default sub가 실제로 담당하는 책임을 목록화한다.
  (node-level inbound dispatch, fanout connect, handler 등록)
- `spot_node_inbound_receiver_t` 또는 동등한 내부 전용 타입을 도입한다.
- 이 타입은 `spot_sub_t` destroy semantics를 재사용하지 않으며,
  `spot_node_t` 또는 `spot_runtime_t` 수명에 완전히 종속된다.
- `install_spot_node_handler()`가 `ensure_default_sub()`를 통해
  암묵적으로 public child를 만드는 경로를 제거한다.
- internal receiver는 attachment map이 아니라
  runtime의 internal socket과 같은 레벨에 배치한다.
  이로써 close ownership이 1단계에서 확립한 runtime 단일 경로와 자연스럽게 합류한다.
- 현재 sub receive path 내부에서 수행되는 ready probe 판별을
  user-visible sub callback path에서 제거하고,
  node-internal receiver 또는 동등한 internal ingress 계층으로 이동시킨다.

완료 기준:

- `zlink_spot_node_new(..., handler)`가 hidden public sub를 만들지 않는다.
- node inbound dispatch가 public sub lifecycle과 완전히 분리된다.
- internal receiver attachment는 node/runtime teardown에서만 정리된다.
- public sub/pub destroy는 internal socket/fanout/ingress drain과 직접 얽히지 않는다.

POSD 판단 기준:

- node inbound dispatch는 깊은 모듈이어야 하며,
  public sub의 구현 세부를 인터페이스처럼 노출해서는 안 된다.
- hidden child를 이해해야만 node API를 이해할 수 있다면 실패다.
- internal receiver는 "인터페이스 없는 깊은 모듈"이어야 하며,
  사용자가 존재를 알 필요조차 없어야 한다.

### 18.3 3단계: attachment ownership 분리

public attachment와 internal attachment의 생성/삭제 경로를 분리한다.

- `destroy_attachment()`/`destroy_attachment_async()`의 호출 주체를
  public/internal로 구분한다.
- `spot_attachment_t`에 ownership 구분을 추가하거나,
  internal attachment를 attachment map에서 완전히 제거한다
  (2단계에서 internal receiver가 runtime 레벨로 이동했다면 후자가 자연스럽다).
- 두 경로가 같은 helper를 쓰더라도
  ownership과 wait policy를 공유해서는 안 된다.

완료 기준:

- attachment map에는 public child attachment만 남는다.
- internal receiver의 소켓은 runtime internal socket으로 관리된다.

POSD 판단 기준:

- attachment ownership은 정보 은닉 경계다.
- public attachment와 internal attachment가 같은 규칙을 공유한다면
  정보 누출이 남아 있는 것으로 본다.
- internal attachment가 public attachment API에 패스스루 위임을 시작하면
  분리 효과는 사라진다.

### 18.4 4단계: readiness state machine 정리

subscription snapshot source와 ready-ack source를 완전히 분리한다.

현재 `spot_data_plane.cpp`의 `outbound_ready_filters`는 ready ack source를
`”ack:” + source_id` string prefix로 인코딩하고,
수신 측에서 `decode_ready_ack_source_id()`로 런타임 파싱한다.
이 implicit typing을 제거하고, 별도 자료구조로 분리한다.

구체적으로:

- subscription source와 ready ack source를 별도 map/struct로 분리한다.
- pub aggregate source set은 ready-ack namespace만 사용하도록 정리한다.
- disconnect/loss/ctrl-desync 시 aggregate reset 규칙을 단일화한다.
- string prefix 기반 런타임 타입 판별을 명시적 타입 구분으로 교체한다.

완료 기준:

- `PUB_DELIVERY_READY_CHANGED`의 증가/감소가
  하나의 명시적 source set 변화로만 설명된다.
- subscription mirror와 publisher-visible ready source가
  같은 map이나 같은 source key namespace를 공유하지 않는다.
- monitor/late-connect/disconnect 회귀가 semantic duplication 없이 통과한다.

POSD 판단 기준:

- readiness 의미는 하나의 source model로만 설명돼야 한다.
- string prefix 파싱에 의미가 숨어 있으면 모호성이 제거되지 않은 것으로 본다.
- "subscription 등록 → probe → ack → aggregate"라는 시간 순서를 따라
  책임이 흩어져 있다면 아직 시간적 분해 상태다.

## 19. 리팩터 수용 기준

2차 리팩터는 아래를 모두 만족해야 완료로 본다.

### 19.1 기능 수용 기준

- hidden default sub에 대한 암묵 의존이 제거되거나,
  최소한 public sub lifecycle과 완전히 격리된다.
- 필요 시 public C API 변경이 있더라도,
  hidden/internal resource와 destroy/readiness 세부가 더 적게 노출된다.
- `spot` scaling teardown timeout이 구조적으로 사라진다.
- `test_spot_service_introspection`
  `test_spot_pubsub_scenario`
  `test_monitor_service_contract`
  `test_thread_safe_scaling_spot`
  이 안정적으로 통과한다.
- readiness/monitor 계약을 유지하면서도
  source bookkeeping 설명이 문서 한 절로 요약 가능할 정도로 단순해진다.

### 19.2 성능 비후퇴 기준

2차 리팩터는 구조 단순화가 목적이지만,
1차에서 달성한 성능 수준을 후퇴시키면 안 된다.

- 2차 리팩터 각 단계(18.1-18.4) 완료 후,
  focused single run으로 SPOT tcp/131072, tcp/262144를 확인한다.
- 1차 달성 수치 대비 5% 이상 후퇴하면 해당 단계를 재검토한다.
- 최종적으로 15절의 perf gate(main 대비 90% 이상)를 계속 만족해야 한다.

즉 2차 리팩터는 “성능을 올리는 작업”이 아니라
“성능을 유지하면서 구조를 단순화하는 작업”이다.
성능 후퇴가 감지되면 구조 변경의 hot path 영향을 먼저 분석한다.

### 19.3 최종 목표

최종 목표는 단순한 “성능 회복”이 아니라,
SPOT의 data/control/lifecycle 구조가
문서와 실제 코드에서 같은 모델로 설명되는 상태다.

### 19.4 리팩터 후 유지해야 하는 회귀 테스트 계약

리팩터 전후로 아래 계약이 깨지면 안 된다.

- **node send-ready isolation**: pub/sub 각각의 send-ready/delivery-ready 이벤트가
  다른 handle의 lifecycle 변화에 오염되지 않아야 한다.
- **self-close `EBUSY` contract**: callback 내부에서 handle destroy 시
  `EBUSY`를 반환하며, callback 완료 후에만 실제 close가 진행된다.
- **node API의 implicit default sub/pub 비의존**:
  `zlink_spot_node_new()`, subscribe/unsubscribe, monitor open 등
  node-level API가 hidden default handle 생성에 기대지 않는다.
- **node-level `SUB` monitor/readiness semantics 유지**:
  backing object가 hidden public sub에서 node-owned source로 바뀔 수는 있지만,
  사용자에게 보이는 node-level `SUB` monitor/readiness 의미 강도는 약화하지 않는다.

## 20. 실행 전 체크리스트

실제 코드 변경 전후에 아래를 반드시 점검한다.

- hidden/internal resource가 public lifecycle에 새지 않는가
- close ownership이 한 레이어에 모여 있는가
- runtime read / control snapshot / event emission source가 분리돼 있는가
- destroy accepted 이후 errno/latch 규칙이 유지되는가
- data plane fast path에 control bookkeeping이 직접 끼어들지 않는가
- readiness source가 단일 semantic source로 설명 가능한가

## 21. POSD 기반 최종 판단 질문

구현이나 코드 리뷰에서 판단이 애매하면 아래 질문으로 되돌아간다.

1. 이 변경이 `spot_node`를 더 깊은 모듈로 만들었는가, 아니면 단지 코드를 옮겼는가?
2. public API 사용자가 internal receiver, hidden default sub, ack source prefix를
   알 필요가 완전히 사라졌는가?
3. late-connect, disconnect, destroy race를 설명할 때
   "어느 레이어가 진실의 원천인가"를 한 문장으로 말할 수 있는가?
4. hot path를 이해하기 위해 control-path 자료구조 전체를 알아야 한다면
   설계가 여전히 얕은 것이다.

위 질문에 하나라도 "아니오"가 나오면
이번 리팩터는 아직 POSD 관점에서 완료된 것이 아니다.

## 22. API/계약 고정점

리팩터 중에도 아래 contract는 유지하거나, 변경 시 문서에서 명시적으로 재정의해야 한다.

- node-level handler는 public child handle 존재 여부와 독립적으로 동작해야 한다.
- node-level subscribe/unsubscribe는 hidden public sub 생성의 부수효과가 아니어야 한다.
- node-level `SUB` monitor는 public sub lifecycle/busy rule에 끌려 들어가지 않아야 한다.

이 셋 중 하나라도 현재 public C API로 표현이 불가능하면,
API 변경은 허용된다. 단 변경 후에도 설명은 더 짧아져야 한다.

## 23. POSD 기반 완료 판정법

### 23.1 3문장 인터페이스 테스트

리팩터 완료 후 아래 대상은 각각 3문장 이내로 설명 가능해야 한다.

- internal receiver:
  `spot_node` lifecycle에 종속된 internal inbound dispatch 계층이다.
  public API가 없고 user-visible sub handle과 수명을 공유하지 않는다.
  readiness/control bookkeeping의 internal ingress 역할만 담당한다.
- readiness state:
  하나의 authoritative source model이 있고,
  subscription mirror와 ready-ack source는 별도 구조다.
  aggregate 변화는 그 source set 변화만으로 설명된다.
- destroy:
  runtime이 authoritative close owner다.
  public child 정리와 runtime drain의 경계가 분명하다.
  abortive path는 예외 상황으로만 남아야 한다.

3문장으로 설명이 길어지거나 예외 설명이 주가 되면
추상화가 아직 얕은 것이다.

### 23.2 변경 증폭 리트머스 테스트

아래 변경이 한 곳 또는 한 계층에서 끝나야 한다.

| 변경 시나리오 | 리팩터 후 기대 영향 범위 |
| --- | --- |
| readiness 조건 하나 추가 | readiness state machine 한 곳 |
| 새 subscription filter 타입 | subscription mirror 계층 한 곳 |
| monitor event 종류 추가 | normalized event fanout 계층 한 곳 |
| teardown 규칙 하나 수정 | runtime destroy ownership 계층 한 곳 |

이 테스트를 통과하지 못하면 정보 은닉이 아직 부족한 것이다.

## 24. 현재 코드 기준 상태표

이 표는 현재 workspace 코드 기준 평가이며,
문서 목표 달성 여부를 `완료 / 부분 / 미완료`로 표시한다.

### 24.1 단계별 상태

| 항목 | 상태 | 메모 |
| --- | --- | --- |
| 18.1 destroy ownership 단일화 | 완료 | node destroy가 public handle 정리와 internal receiver 정리를 분리하고, internal receiver는 node-owned shutdown 경로로 정리된다 |
| 18.2 internal receiver 타입 분리 | 완료 | node API와 node handler install 경로가 더 이상 `ensure_default_sub()`에 의존하지 않고 dedicated internal receiver를 사용한다 |
| 18.3 attachment ownership 분리 | 완료 | public default sub와 node internal receiver의 owner slot이 분리됐고 node destroy가 internal receiver attachment를 별도 회수한다 |
| 18.4 readiness state machine 정리 | 완료 | ready-ack snapshot은 별도 control topic으로 분리되어 `ack:` prefix typing 없이 적용된다 |

### 24.2 수용 기준 상태

| 항목 | 상태 | 메모 |
| --- | --- | --- |
| hidden default sub 비의존 | 완료 | node-level subscribe/monitor/handler 경로는 internal receiver로 수렴하고 public default sub는 child handle 경로로만 남는다 |
| scaling teardown timeout 구조 제거 | 완료 | multi-publisher teardown 회귀를 포함한 e2e 검증이 다시 녹색으로 돌아왔다 |
| readiness/source bookkeeping 단순화 | 완료 | subscription snapshot과 ready-ack snapshot이 topic 수준에서 분리되어 source namespace가 명시화됐다 |
| 테스트 계약 유지 | 완료 | `test_spot_pubsub_scenario`, `test_spot_service_introspection` 재검증으로 기존 계약 유지가 확인됐다 |

### 24.3 다음 우선순위

1. peer state sync protocol에 snapshot/delta generation 메타데이터를 추가할 필요가 생기면 control codec 확장만 검토
2. internal receiver diagnostics가 필요해지면 public contract가 아닌 internal trace surface로 한정
3. perf 기준 재측정은 구조 검증 이후 별도 수행
4. service 공통 lifecycle 설명은 `gateway` 문서와 용어만 정렬

---

## 부록 A. peer state sync module 내부 프로토콜 상세

> 이 부록은 8.3절 `peer state sync module`의 구현 상세를 기술한다.
> 상위 문서의 설계 판단은 이 부록에 의존하지 않는다.
> 프로토콜 세부가 변경되더라도 외부 인터페이스(hello, subscription sync,
> ready ack, peer lost)의 의미는 유지돼야 한다.

### A.1 reserved topic namespace

control plane은 reserved topic prefix와 multipart frame으로 구성한다.

- `__zlink.spot.ctrl.`
- `__zlink.spot.bootstrap.`

public SPOT publish/subscribe subject가 이 prefix를 사용하는 것은
invalid input으로 취급한다.

### A.2 frame 구조

1. topic
2. source node id
3. target node id 또는 broadcast marker
4. verb-specific payload

모든 control message는 공통 메타를 함께 가진다.

- `protocol_version`
- `subscription_epoch`
- `control_sequence_no`
- `snapshot_generation` 또는 `0`

### A.3 topic routing 및 subscription 규칙

`ctrl_sub` subscription 규칙:

- 첫 frame의 topic은 verb만이 아니라 routing class를 포함한다.
- 예:
  - `__zlink.spot.ctrl.broadcast.ready_probe`
  - `__zlink.spot.ctrl.node.<target-node-id>.ready_ack`
- 모든 node는 broadcast control prefix를 구독한다.
- 모든 node는 자신의 `peer_ctrl_node_id`를 target으로 하는 prefix를 구독한다.
- 나머지 filtering은 topic과 payload의 verb/type으로 판별한다.

예시 topic:

- `__zlink.spot.ctrl.broadcast.hello`
- `__zlink.spot.ctrl.node.<target-node-id>.hello_ack`
- `__zlink.spot.ctrl.broadcast.heartbeat`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_begin`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_item`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_end`
- `__zlink.spot.ctrl.broadcast.sub_delta_add`
- `__zlink.spot.ctrl.broadcast.sub_delta_remove`
- `__zlink.spot.ctrl.node.<target-node-id>.ready_probe`
- `__zlink.spot.ctrl.node.<target-node-id>.ready_ack`
- `__zlink.spot.ctrl.node.<target-node-id>.resync_request`
- `__zlink.spot.ctrl.broadcast.peer_lost`
- `__zlink.spot.bootstrap.ctrl_descriptor`

### A.4 sequence tracking 및 resync

receiver는 peer별 마지막 `control_sequence_no`를 추적한다.
같은 `subscription_epoch`/`snapshot_generation` 안에서 gap이 보이거나,
heartbeat timeout이 나면 해당 peer를 `ctrl_desynced`로 내리고
`RESYNC_REQUEST`를 보낸 뒤 snapshot 재동기화를 강제한다.
`RESYNC_REQUEST` 자체도 lossy일 수 있으므로,
desynced peer는 heartbeat timeout마다 같은 요청을 재전송한다.
또한 heartbeat에는 현재 `subscription_epoch`와
`snapshot_generation`을 함께 실어,
송신측과 수신측이 epoch mismatch를 스스로 감지하면
명시적 요청이 없어도 snapshot push를 시작할 수 있게 한다.
