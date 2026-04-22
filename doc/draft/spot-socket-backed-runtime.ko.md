[스펙 목차](../README.ko.md)

# Draft -- SPOT Socket-Backed Runtime Unification

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `SPOT` 내부 구조를 한 방향으로 고정한다.

핵심 결정은 간단하다.

- `Spot`은 queue 위에 얹은 mailbox가 아니다.
- `Spot`은 **개별 runtime socket을 가진 논리 endpoint**다.
- routed plane의 그 socket은 **`Spot`마다 하나의 internal `ROUTER`**다.
- `pub/sub`와 `routed request/reply`는 같은 철학으로 구현한다.

이번 초안의 목표는 아래와 같다.

- `Spot`의 `pub/sub`와 `routed recv`를 같은 구조 철학으로 맞춘다.
- dispatch event 의미를 실제 `Spot` 기준 readiness로 좁힌다.
- 첫 `recv()` 때 숨겨진 자원을 여는 구조를 없앤다.
- `10,000 Spot` 규모에서도 구조가 어느 지점에서 무너지는지 미리 드러나게 한다.
- 구현자가 "왜 queue 모델이 아니라 socket-backed 모델을 고정하는가"를
  수치와 구조로 이해할 수 있게 한다.

이 문서는 새 public API를 많이 추가하는 문서가 아니다.
우선은 **내부 구조와 동작 기준**을 고정하는 문서다.

## 2. 배경

현재 구현은 `Spot` 내부에서 두 모델이 섞여 있다.

- `pub/sub`
  각 `Spot`이 실제 `spot_pub`, `spot_sub` runtime attachment를 가진다.
- `routed request/reply`
  각 `Spot`이 직접 socket으로 받는 것이 아니라, 내부 state와 queue를 통해
  메시지를 받는다.

이 혼합 모델은 아래 문제를 만들었다.

### 2.1 의미가 서로 다르다

`pub/sub`는 "그 `Spot`의 subject socket이 readable이면 그 `Spot`이 실제로
읽을 수 있다"는 뜻으로 이해할 수 있다.

반면 routed 쪽은 "dispatch event가 왔으니 읽을 일이 있을지도 모른다"에 더
가깝다.

즉 같은 `Spot`인데도 아래 질문에 대한 답이 다르다.

- readable이 왔을 때 정말 그 `Spot`에서 꺼낼 것이 있는가
- `recv()`가 첫 호출인지 아닌지에 따라 내부 준비가 달라지는가
- `Spot` 하나가 자기 own runtime을 가지는가, 아니면 shared queue를 빌려 쓰는가

### 2.2 구조가 숨겨져 있다

현재 routed path는 겉으로는 queue 모델처럼 보이지만, 실제로는 첫 `recv()`에서
state, queue, completion signal 같은 자원을 열고 등록한다.

이 구조는 두 가지 면에서 좋지 않다.

- 성능 비용이 API 호출 시점에 숨겨진다.
- scale limit이 create 시점이 아니라 첫 `recv()` 시점에 터진다.

### 2.3 POSD 관점에서 깊은 모듈이 아니다

지금 routed path는 "단순한 `Spot recv`"처럼 보이지만, 실제로는 아래 요소가
함께 들어 있다.

- lazy state 생성
- identity index 등록
- internal queue 준비
- completion signal 준비
- dispatch task 연동

즉 인터페이스는 단순해 보이는데, 호출자가 예상하지 못한 내부 준비가 너무 많다.
이것은 `Philosophy of Software Design` 기준으로 좋은 압축이 아니다.

## 3. 근거가 된 로컬 측정

이번 초안은 말로만 방향을 정한 것이 아니다.
`core/tests/bench/` 아래에 추가한 local bench를 기준으로 판단했다.

- `spot_node_10000_pubsub_bench.cpp`
- `spot_node_10000_router_bench.cpp`

측정은 `SpotNode` 하나 아래에 많은 `Spot`을 붙이는 local 시나리오로 진행했다.

### 3.1 pub/sub 측정

`1 pub + 9,999 sub` 구조에서 `10,000 Spot`은 동작했다.

대표 수치는 아래와 같다.

| 항목 | 측정값 |
|------|--------|
| `create_ms` | `620.013` |
| `subscribe_ms` | `43922.449` |
| `measured_publish_fanout_ms` | `31.369` |
| `destroy_ms` | `29268.089` |
| `rss_after_subscribe_kb` | `1119200` |

이 수치는 좋기도 하고 나쁘기도 하다.

- 좋은 점: `10,000 Spot`에서도 실제 fanout은 동작했다.
- 나쁜 점: attach, subscribe, destroy 비용이 매우 크다.

즉 `pub/sub` 모델은 **데이터 평면보다 관리 평면 비용이 큰 구조**다.

### 3.2 routed 측정

`1 router + N-1 routed receiver spot` 구조에서는 `100`개는 문제 없이 동작했다.

대표 수치는 아래와 같다.

| 항목 | 측정값 |
|------|--------|
| `create_ms` | `1.949` |
| `recv_prepare_ms` | `56.370` |
| `send_ms` | `0.381` |
| `recv_drain_ms` | `0.236` |
| `destroy_ms` | `114.903` |

그런데 `10,000 Spot` 근처에서는 결과가 달랐다.

- `9998` receiver는 통과
- `9999`, `10000` receiver는 실패
- 실패 지점은 send나 drain이 아니라 **recv prepare 단계**
- 오류는 `EMFILE` (`Too many open files`)

즉 현재 routed 모델은 "queue라서 가볍다"는 장점이 증명되지 않았다.
오히려 recv 준비 시점에 숨겨진 자원을 많이 열고 있다는 뜻이 더 강하다.

### 3.3 이번 초안이 내리는 해석

이번 초안은 위 결과를 아래처럼 해석한다.

- 현재 `pub/sub` 모델은 비싸지만 적어도 구조가 드러나 있다.
- 현재 routed queue 모델은 겉으로는 가벼워 보이지만 실제 scale limit은 더 일찍
  드러난다.
- 따라서 지금 코드 기준으로는 **queue 모델이 구조적으로 우월하다는 근거가 없다.**
- 반대로 `Spot = 개별 runtime socket endpoint`라는 해석은 이미 `pub/sub`에서
  구현과 측정으로 확인됐다.

## 4. 결정

이 초안은 아래 결정을 고정한다.

### 4.1 기본 결정

`Spot`은 **socket-backed runtime endpoint**다.

즉 `Spot`은 아래 세 평면을 모두 자기 own runtime으로 본다.

- publish plane
- subscribe plane
- routed receive plane

그리고 routed receive plane은 아래처럼 더 구체적으로 고정한다.

- `SpotNode`는 routed broker 역할의 node-level `ROUTER`를 가진다.
- 각 `Spot`은 routed ingress용 internal `ROUTER` 하나를 가진다.
- `SpotNode.ROUTER`와 각 `Spot.ROUTER`는 `inproc`으로 연결된다.
- local routed delivery와 remote routed delivery는 모두 마지막에 target
  `Spot.ROUTER`로 들어간다.
- `zlink_spot_recv(_part)`는 그 `Spot.ROUTER`에서 읽는다.

여기서 "own runtime"은 반드시 외부에 raw socket handle을 그대로 노출한다는 뜻은
아니다.
의미는 아래와 같다.

- 각 `Spot`에 귀속된 내부 runtime subject가 분명해야 한다.
- readiness와 monitor 의미가 그 subject 기준으로 닫혀야 한다.
- 메시지는 "이 `Spot`의 own ingress"에 먼저 도착한 뒤 `recv()` 되어야 한다.

이번 초안에서는 이 문장을 더 좁혀서 아래처럼 읽는다.

- routed 메시지는 "그 `Spot`의 internal `ROUTER`"에 먼저 도착한 뒤 `recv()` 된다.

### 4.2 이번 초안이 버리는 방향

이번 초안은 아래 방향을 채택하지 않는다.

- `Spot`을 queue-only mailbox로 해석하는 방향
- routed receive를 shared node queue 위에 얹는 방향
- 첫 `recv()` 때 target registration과 queue open을 수행하는 방향
- node-level broad readable을 per-spot delivery hint처럼 쓰는 방향

### 4.3 이번 초안이 유지하는 것

이번 초안은 아래 항목은 유지한다.

- inter-node transport로서 `SpotNode`의 역할
- channel request transport owner가 attached `DEALER`라는 규칙
- existing routed envelope 형식
- `SpotNode`가 topology, discovery, control을 관리하는 구조

즉 이 초안은 "모든 것을 per-spot external socket로 바꾼다"는 문서가 아니다.
핵심은 **Spot delivery owner를 분명히 한다**는 것이다.

### 4.4 1차 구현 범위

이번 초안이 바로 구현 대상으로 잡는 1차 범위는 아래까지다.

- routed target registration을 create 시점으로 옮긴다
- per-spot internal `ROUTER`를 도입한다
- `ROUTED_READABLE`, `SUBSCRIBE_READABLE` 의미를 실제 readiness로 맞춘다
- hidden queue / recv-prep 의존을 제거한다
- 기존 public `spot` API 이름은 가능하면 유지한다

반대로 아래 항목은 1차 구현 범위 밖으로 둔다.

- public poller의 `Spot` 직접 등록 계약 변경
- 새로운 사용자용 `Spot` raw subject 노출 API
- channel reply surface 재설계
- guide / binding에서 새로운 고수준 convenience API 추가

이 범위를 두는 이유는, 이번 작업의 핵심을 `Spot` delivery owner 정렬에
집중시키기 위해서다.

## 5. 이 문서에서 쓰는 말

- **spot-owned runtime**
  특정 `Spot`에 귀속된 internal runtime subject 또는 socket
- **routed ingress**
  `Spot`이 `zlink_spot_recv(_part)`로 읽는 own inbound path.
  이번 초안에서는 `Spot`마다 존재하는 internal `ROUTER`를 뜻한다.
- **activation**
  그 `Spot`이 routed delivery target으로 등록되고, own routed `ROUTER`
  준비가 끝난 상태
- **lazy-open**
  첫 `recv()`나 첫 event 시점에 숨겨진 queue, signal, fd, poller state를 여는 방식
- **delivery owner**
  최종적으로 메시지를 받아 `recv()`하는 owner
- **transport owner**
  inter-node 또는 inter-runtime 전달을 실제로 수행하는 owner

이번 초안에서 중요한 기준은 아래 한 문장으로 요약된다.

> transport owner와 delivery owner는 다를 수 있지만, delivery owner는 항상
> 명확해야 한다.

## 6. 목표 구조

### 6.1 high-level 구조

목표 구조는 아래와 같다.

```text
+------------------------------------------------------------------+
| SpotNode                                                         |
|------------------------------------------------------------------|
| discovery / topology                                             |
| node ROUTER for routed broker                                    |
| pubsub mesh transport                                            |
| local inproc wiring                                              |
+--------------------------+--------------------------+------------+
                           |                          |
                           | inproc                   | inproc
                           v                          v
                  +----------------+         +----------------+
                  | Spot A         |         | Spot B         |
                  |----------------|         |----------------|
                  | pub socket     |         | pub socket     |
                  | sub socket     |         | sub socket     |
                  | routed ROUTER  |         | routed ROUTER  |
                  +----------------+         +----------------+
```

설명의 핵심은 아래와 같다.

- `SpotNode`는 transport와 topology를 맡는다.
- 실제 recv owner는 각 `Spot`의 own internal `ROUTER`다.
- local delivery든 remote delivery든 마지막에는 target `Spot.ROUTER`로 들어간다.

### 6.2 routed path의 목표 모델

```text
+-------------+     +-------------------+     +-------------------+
| Sender      | --> | SpotNode ROUTER   | --> | Spot B ROUTER     |
|-------------|     |-------------------|     |-------------------|
| build env   |     | route / forward   |     | spot-owned ingress |
+-------------+     +-------------------+     +-------------------+
                                                     |
                                                     v
                                               zlink_spot_recv
```

현재 queue 모델과 가장 큰 차이는 마지막 단계다.

- 현재: target state queue에 바로 적재
- 목표: target `Spot`의 internal `ROUTER`로 전달

즉 target `Spot`은 "내 queue를 node가 대신 채운다"가 아니라
"내 own `ROUTER`에 도착한 것을 내가 읽는다"로 해석한다.

### 6.3 왜 `DEALER`가 아니라 `ROUTER`인가

이번 초안은 routed ingress를 `DEALER`로 두지 않는다.

이유는 아래와 같다.

- `zlink_spot_recv(_part)`는 이미 `source_node_rid`, `source_spot_rid`,
  `request_seq`를 함께 돌려준다.
- `Spot`은 받은 routed 입력을 기준으로 `reply_spot`, `reply_router`를 만들어야 한다.
- 즉 target `Spot` ingress는 source identity와 request identity를 보존한 채
  입력을 받아야 한다.

이 의미를 `DEALER` 위에 다시 얹으면 결국 위에서 router 의미를 다시 흉내 내야
한다.
그러면 모델은 단순해지지 않고 오히려 더 흐려진다.

그래서 이번 초안은 아래를 고정한다.

- node 쪽 routed broker: `SpotNode.ROUTER`
- `Spot` routed ingress: per-spot internal `ROUTER`

### 6.4 local inproc wiring 규칙

`SpotNode.ROUTER`와 `Spot.ROUTER`를 잇는 local inproc wiring은 아래 조건을
만족해야 한다.

- 각 `Spot`은 자기 own routed ingress에 대응하는 local endpoint를 하나만 가진다.
- 이 endpoint는 같은 `SpotNode` 안에서 충돌하지 않게 유일해야 한다.
- endpoint 문자열 형식은 내부 구현 세부로 둘 수 있지만, 최소한 node lifetime 동안
  안정적으로 재사용 가능해야 한다.
- target lookup은 `spot_rid -> Spot runtime`을 찾는 문제와 `Spot runtime -> local
  inproc peer`를 찾는 문제를 분리해서 다룰 수 있어야 한다.

이 절의 핵심은 "어떻게 이름을 짓느냐"보다 "lookup과 wiring이 뒤엉키지 않아야
한다"는 점이다.

## 7. 구조 요구사항

### 7.1 `zlink_spot_new()`의 의미

`zlink_spot_new(node)`는 더 이상 "가벼운 facade만 만든다"는 의미로 두지 않는다.

최소한 아래 조건을 만족해야 한다.

- `Spot` identity가 create 시점에 확정된다.
- routed target registration이 create 시점 또는 명시적 activation 시점에 끝난다.
- routed recv를 위한 own internal `ROUTER` 준비가 끝난다.
- 첫 `recv()`는 activation을 수행하지 않는다.

즉 create 성공 후에는 "이 `Spot`은 이미 delivery target으로 존재한다"고
볼 수 있어야 한다.

### 7.2 첫 `recv()`의 의미

첫 `recv()`는 아래 일을 해서는 안 된다.

- identity index 최초 등록
- internal routed `ROUTER` 최초 생성
- completion signal socket 최초 생성
- dispatch task 최초 설치
- delivery target 최초 활성화

첫 `recv()`는 "이미 준비된 own routed `ROUTER`에서 읽는다"는 뜻만 가져야 한다.

이 규칙을 두는 이유는 명확하다.

- scale limit이 `recv()` 시점에 숨겨지지 않게 하기 위해서다.
- `recv()` latency를 호출자 입장에서 예측 가능하게 만들기 위해서다.

### 7.3 routed delivery owner

`router -> spot`, `spot -> spot`, `spot -> router reply -> spot` 같은 모든 routed
경로는 마지막에 아래 규칙을 따라야 한다.

- destination이 `Spot`이면 target `Spot`의 internal `ROUTER`가 delivery owner다.
- destination이 `ROUTER`면 target `ROUTER` recv surface가 delivery owner다.

즉 `Spot` 대상 메시지는 shared queue나 node-wide inbox에서 직접 소비하지 않는다.

### 7.4 dispatch event 의미

dispatch event는 coarse hint가 아니라, 가능한 한 **실제 target readiness**에
가깝게 맞춰야 한다.

이 초안에서 고정하는 의미는 아래와 같다.

| 이벤트 | 의미 |
|--------|------|
| `SUBSCRIBE_READABLE` | 그 `Spot`의 subscribe ingress에서 실제 recv 가능한 항목이 있다 |
| `ROUTED_READABLE` | 그 `Spot.ROUTER`에서 실제 recv 가능한 항목이 있다 |
| `TIMER_READABLE` | 그 `Spot`이 소유한 timer 항목을 실제로 읽을 수 있다 |
| `CHANNEL_REPLY_READABLE` | 그 `Spot`이 소유한 dealer source completion을 progress할 수 있다 |

특히 아래 경로는 금지한다.

- node-wide sub readable을 모든 facade spot에 fan-out해서 `SUBSCRIBE_READABLE`로
  보내는 방식

이 방식은 "그 spot이 읽을 수 있다"와 "node 어딘가에 traffic이 있다"를 섞기
때문에 허용하지 않는다.

### 7.4.1 readiness와 drain 규칙

`SUBSCRIBE_READABLE`, `ROUTED_READABLE`은 **메시지 개수 알림**이 아니라
**readiness 알림**이다.

즉 아래처럼 이해해야 한다.

- 이벤트 1개가 메시지 1개를 뜻하지 않는다.
- 이미 readable인 동안 같은 plane으로 메시지가 더 와도, 새 이벤트 개수가
  메시지 개수와 1:1로 대응하지 않을 수 있다.
- handler 또는 poller consumer는 한 번 깨면 `EAGAIN`이 나올 때까지 drain하는
  방식으로 처리해야 한다.

이 규칙은 현재 socket poller가 notify fd와 실제 socket events를 조합해
readiness를 확인하는 구조와도 맞아야 한다.

따라서 이번 초안은 아래 처리를 권장 동작이 아니라 **필수 동작**으로 본다.

- `SUBSCRIBE_READABLE` 후 `zlink_spot_subscribe(_part)` 반복 호출
- `ROUTED_READABLE` 후 `zlink_spot_recv(_part)` 반복 호출

한 번의 이벤트에서 여러 메시지를 처리해도 이상 동작이 아니다.

### 7.5 send-ready와 monitor 의미

`Spot`의 send-ready와 monitor는 가능하면 실제 owned runtime에 맞춰야 한다.

즉 routed path도 아래 원칙을 따른다.

- send-ready가 올라오면 그 `Spot`이 실제로 보낼 수 있는 경로가 준비된 상태여야 한다.
- monitor snapshot은 shared node 상태만이 아니라, 그 `Spot` own runtime 준비 상태를
  반영해야 한다.

이번 초안은 monitor 구조체를 즉시 바꾸는 문서는 아니지만, 구현 방향은 이 원칙을
따른다.

### 7.6 poller 의미

이번 초안은 `Spot`을 poller에 직접 등록하는 사용을 **장기 목표**로 본다.

하지만 **1차 구현 단계에서는 기존 public poller 계약을 그대로 유지한다.**
현재 `zlink_poller_event_t`는 아래 필드만 제공하므로, owner spot / event kind /
subject를 함께 표현할 수 없다.

- `source_kind`
- `socket`
- `fd`
- `timer`
- `user_data`
- `events`

따라서 이 문서가 뜻하는 `spot-aware poll result`는 1차 구현의 공개 계약이 아니라,
후속 poller draft가 정의해야 할 확장 목표다.

그 이유는 단순하다. 단순히 "`Spot`이 readable"만 돌려주면 호출자가 아래 중 무엇을
해야 하는지 결정할 수 없기 때문이다.

- `zlink_spot_subscribe(_part)`
- `zlink_spot_recv(_part)`
- `zlink_spot_channel_reply_progress_from()`
- `zlink_timer_recv()`

그래서 이번 초안은 아래를 함께 고정한다.

- 1차 구현은 기존 poller public surface를 유지한다.
- `Spot` 직접 등록은 별도 poller draft에서 정의한다.
- 내부적으로는 `Spot`이 소유한 pollable subject들을 펼쳐서 등록한다.
- 후속 poller 확장은 owner `Spot`뿐 아니라, **어떤 event인지**와 **어떤 subject가
  ready인지**를 함께 돌려줘야 한다.

즉 `Spot` poller 결과는 아래 셋을 포함해야 한다.

- owner spot
- event kind
- subject

이 문서에서는 이를 **spot-aware poll result**라고 부른다.

## 8. poller 요구사항

이 절은 **후속 poller 확장 단계**의 요구사항이다.
1차 구현은 이 절을 즉시 공개 계약으로 올리지 않는다.

### 8.1 등록 모델

`Spot`을 poller에 등록하면 내부적으로는 아래 subject가 함께 등록된다.

- `spot.sub`
- `spot.router`
- `spot`이 소유한 timer
- `spot`이 소유한 channel reply dealer source

즉 public 등록은 아래처럼 보일 수 있다.

```text
poller_add(spot)
```

하지만 내부적으로는 아래처럼 풀려야 한다.

```text
poller_add(spot.sub)
poller_add(spot.router)
poller_add(spot.timer)
poller_add(spot.channel_dealer_source)
```

여기서 중요한 점은 poller가 `Spot`을 직접 관찰하는 것이 아니라,
`Spot`이 소유한 실제 pollable subject를 관찰한다는 것이다.

### 8.2 결과 모델

`Spot` poller 결과는 단순 `POLLIN`만으로 끝나면 안 된다.

최소한 아래 정보가 필요하다.

- `owner = spot`
- `event = SUBSCRIBE_READABLE | ROUTED_READABLE | TIMER_READABLE | CHANNEL_REPLY_READABLE`
- `subject = 실제 internal socket 또는 timer 또는 dealer`

이 구조가 있어야 호출자는 poll 결과만으로 다음 동작을 고를 수 있다.

- `SUBSCRIBE_READABLE`이면 subscribe recv
- `ROUTED_READABLE`이면 routed recv
- `TIMER_READABLE`이면 timer recv
- `CHANNEL_REPLY_READABLE`이면 dealer progress

### 8.3 high-level poller 구조

```text
+-------------------------------+
| User Poller Registration      |
|-------------------------------|
| add Spot A                    |
| add Spot B                    |
+---------------+---------------+
                |
                v
+-------------------------------+
| Internal Expansion            |
|-------------------------------|
| Spot A sub                    |
| Spot A router                 |
| Spot A timer                  |
| Spot A dealer source          |
| Spot B sub                    |
| Spot B router                 |
| ...                           |
+---------------+---------------+
                |
                v
+-------------------------------+
| Poller Ready Result           |
|-------------------------------|
| owner = Spot A                |
| event = ROUTED_READABLE       |
| subject = Spot A router       |
+-------------------------------+
```

### 8.4 dispatch event와 poller event의 관계

dispatch event와 poller event는 의미가 따로 놀면 안 된다.

이번 초안은 아래 관계를 목표로 둔다.

- dispatch event는 spot-aware poll result를 callback 실행 문맥에 맞게 순차화한 것이다.
- poller event는 callback 없이 사용자가 직접 처리할 수 있는 같은 의미의 결과다.

즉 둘은 의미가 달라서는 안 된다.

- poller에서 `ROUTED_READABLE`
- dispatch에서 `ROUTED_READABLE`

이 둘은 모두 "그 `Spot.ROUTER`에서 실제 recv 가능한 항목이 있다"는 같은 뜻이어야
한다.

### 8.5 이번 초안이 허용하지 않는 poller 모델

이번 초안은 아래 poller 모델을 허용하지 않는다.

- `Spot`을 등록했는데 결과가 단순 `POLLIN`만 나오는 모델
- owner `Spot`만 알려주고 어느 plane이 준비됐는지 숨기는 모델
- node-wide readiness를 `Spot` readiness처럼 돌려주는 모델

이 모델들은 모두 호출자가 다시 내부 구조를 추측해야 하므로 채택하지 않는다.

## 9. routed lifecycle과 identity 규칙

### 9.1 create 시점 준비

`zlink_spot_new()`가 성공하면 최소한 아래 상태가 이미 끝나 있어야 한다.

- `spot_rid`가 확정됐다.
- per-spot routed ingress owner가 확정됐다.
- node broker가 `spot_rid -> target Spot` lookup을 만들 수 있다.
- local delivery에 필요한 inproc wiring 정보가 확정됐다.
- close 시 unregister할 수 있는 runtime registration key가 확정됐다.

여기서 중요한 점은 **첫 recv가 create를 완성하면 안 된다**는 것이다.
첫 recv는 이미 준비된 ingress를 읽기만 해야 한다.

### 9.2 unregister와 destroy 순서

destroy 경로는 create와 반대로 아래 순서를 따라야 한다.

1. node broker의 target lookup에서 해당 `Spot`을 먼저 제외한다.
2. 이후 새 local routed delivery는 더 이상 이 `Spot`으로 들어오지 않게 한다.
3. 그 다음 dispatch/poller 등록을 해제한다.
4. 마지막에 spot-owned subject를 닫는다.

이 순서를 문서에 적는 이유는, close 도중 새 메시지가 다시 붙으면서
use-after-free나 late readiness가 생기는 일을 막기 위해서다.

### 9.2.1 destroy 중 unread 메시지 처리

destroy 직전에 이미 `Spot` own ingress에 들어온 unread 메시지는 별도 delivery
보장을 만들지 않는다.

이번 초안에서 고정하는 규칙은 아래다.

- destroy 시작 후 새 delivery는 더 이상 해당 `Spot`으로 들어오면 안 된다.
- destroy 시점에 아직 읽히지 않은 local unread 메시지는 close 과정에서 버려질 수
  있다.
- 구현은 "destroy 전에 unread를 끝까지 drain해야 한다"는 숨은 의무를 만들면
  안 된다.

이 규칙을 두는 이유는 close path를 단순하게 유지하고, destroy latency를 예측
가능하게 만들기 위해서다.

### 9.3 source identity 보존 규칙

routed path는 transport hop이 바뀌더라도 application recv metadata는 유지해야 한다.

즉 아래 값은 `zlink_spot_recv(_part)` 결과까지 그대로 살아 있어야 한다.

- `source_node_rid`
- `source_spot_rid`
- `request_seq`

`SpotNode` broker는 target lookup과 local forward를 수행할 수 있지만,
최종 recv surface에 전달할 때 위 의미를 잃어버리면 안 된다.

reply helper는 계속 이 metadata를 기준으로 동작해야 한다.

## 10. 권장 내부 구조

### 10.1 `Spot`이 직접 소유해야 하는 것

각 `Spot`은 아래 항목을 직접 소유하는 방향을 기준으로 잡는다.

- `spot_pub_t`
- `spot_sub_t`
- per-spot internal `ROUTER`
- identity metadata
- own dispatch registration state

여기서는 이름도 더 이상 미루지 않는다.
이번 초안 기준 routed ingress 타입은 **internal `ROUTER`**다.

### 10.2 `SpotNode`가 소유해야 하는 것

`SpotNode`는 아래 항목을 계속 소유한다.

- discovery attach와 peer topology
- inter-node router transport
- node-level routed broker `ROUTER`
- pub/sub mesh transport
- local inproc wiring
- node summary와 control task

즉 `SpotNode`는 transport orchestrator이고, `Spot`은 delivery owner다.

### 10.3 제거 대상

아래 구조는 점진적 제거 대상으로 둔다.

- routed recv를 위한 hidden internal pair queue 의존
- 첫 `recv()`에서만 열리는 completion signal socket
- `find_or_create_spot_state()` 호출에 따라 뒤늦게 생기는 delivery identity
- "shared queue에 넣고 나중에 `Spot recv`로 꺼낸다"는 설명이 필요한 구조

제거 대상이 모두 사라지기 전까지는, 새 구현도 old path와 new path가 어디서
공존하는지 문서와 테스트로 명확히 구분해야 한다.

## 11. 공개 계약에 미치는 영향

이번 초안의 1차 구현 단계에서는 public API 이름을 크게 바꾸지 않아도 된다.
하지만 의미는 아래처럼 더 엄격해진다.

### 11.1 `zlink_spot_recv(_part)`

의미는 아래처럼 읽어야 한다.

- 현재 그 `Spot`의 internal `ROUTER`에 있는 항목을 읽는다.
- 첫 호출이 hidden activation을 수행하지 않는다.
- `ZLINK_DONTWAIT`에서 `EAGAIN`이 오면, 정말 그 `Spot` own ingress에 데이터가 없는
  상태여야 한다.

### 11.2 `zlink_spot_dispatch_event_handler`

`ROUTED_READABLE`, `SUBSCRIBE_READABLE`은 "그 `Spot`이 실제로 읽을 수 있다"는
뜻으로 더 좁혀진다.

호출자가 "이벤트는 왔지만 내 `Spot`에는 아무것도 없다"를 자주 겪는 구조는
허용하지 않는다.

### 11.3 `zlink_spot_new`

반환 성공은 아래를 뜻해야 한다.

- `Spot` identity가 정해졌다.
- local routed target으로 등록 가능하다.
- 이후 첫 recv에서 구조적 lazy-open이 없어야 한다.

### 11.4 poller public 의미

1차 구현에서는 generic poller public API를 바꾸지 않는다.

즉 아래 항목은 **후속 확장**이다.

- `Spot` 직접 등록
- owner spot / event kind / subject를 담는 spot-aware poll result

이 확장을 하려면 현행 `zlink_poller_event_t`만으로는 부족하므로,
별도 poller draft에서 아래 둘 중 하나를 골라야 한다.

- `zlink_poller_event_t` 확장
- `Spot` 전용 poller result 타입 또는 API 추가

후속 확장 단계에서의 public poller 의미는 아래처럼 읽어야 한다.

- `Spot` 등록은 내부 subject 등록의 축약이다.
- poll 결과는 owner `Spot`, event kind, subject를 포함한다.
- 사용자는 dispatch callback 없이도 poll 결과만으로 직접 메시징을 처리할 수 있다.

예를 들면 아래처럼 동작해야 한다.

- `ROUTED_READABLE` 결과를 받는다.
- owner `Spot`과 `ROUTED_READABLE` event를 확인한다.
- 그 `Spot`에 대해 routed recv를 호출한다.
- 그 결과는 해당 `Spot`에 전달된 routed 메시지만 포함한다.

## 12. 구현 단계

이번 초안은 한 번에 갈아엎는 방식보다 아래 순서를 권장한다.

### 12.1 1단계: activation 시점 고정

먼저 routed target registration을 `recv()`에서 떼어낸다.

- `zlink_spot_new()` 또는 내부 activation 단계에서 routed state를 만든다.
- `find_spot_state_by_identity()`가 `recv()` 호출 여부에 의존하지 않게 만든다.

이 단계의 목적은 scale limit을 먼저 드러내는 것이다.

완료 조건은 아래와 같다.

- 첫 `zlink_spot_recv()`에서 새 state나 registration을 만들지 않는다.
- create / destroy benchmark에서 비용이 create 시점으로 이동한 것이 보인다.

### 12.2 2단계: per-spot routed `ROUTER` 도입

그 다음 target `Spot`이 직접 소유하는 internal `ROUTER`를 만든다.

- local delivery는 target state queue가 아니라 target `Spot.ROUTER`로 보낸다.
- remote delivery도 최종적으로 target `Spot.ROUTER`로 정착한다.

완료 조건은 아래와 같다.

- local routed delivery가 더 이상 target recv queue에 직접 적재되지 않는다.
- `zlink_spot_recv(_part)`가 spot-owned `ROUTER` 기준으로 동작한다.

### 12.3 3단계: dispatch 의미 정렬

`ROUTED_READABLE`, `SUBSCRIBE_READABLE`을 실제 ingress readiness와 맞춘다.

- broad node fan-out 기반 `SUBSCRIBE_READABLE`을 제거한다.
- routed queue pending 기반 notification을 target `Spot.ROUTER` pending과 맞춘다.

완료 조건은 아래와 같다.

- false positive `SUBSCRIBE_READABLE`이 사라진다.
- `ROUTED_READABLE` 뒤에는 실제 recv 가능한 메시지가 존재한다.

### 12.4 4단계: poller 의미 정렬

그 다음 poller도 같은 의미를 따르도록 바꾼다.

- `Spot` 등록 시 내부 subject expansion 추가
- 별도 draft를 통해 spot-aware poll result 추가
- dispatch event와 poller event 의미 일치

완료 조건은 아래와 같다.

- poller 확장이 실제로 공개됐다면, dispatch와 같은 drain 규칙을 가진다.
- poller 확장이 공개되지 않았다면, 기존 poller 계약과 충돌하는 내부 임시 API가
  밖으로 새지 않는다.

### 12.5 5단계: old queue 제거

마지막으로 hidden pair queue와 lazy recv-prep 구조를 제거한다.

- old helper queue
- hidden completion wake socket
- recv 시점 state bootstrap

이 단계가 끝나야 구조가 실제로 통일됐다고 볼 수 있다.

완료 조건은 아래와 같다.

- routed recv path에서 old queue 의존이 제거된다.
- 남은 compatibility shim이 있다면 제거 계획이 별도 이슈로 남아 있다.

### 12.6 작업 순서

아래 순서는 이번 draft를 실제 작업으로 옮길 때의 **권장 순서가 아니라 사실상
필수 순서**다.

앞 단계 산출물이 다음 단계 입력이 되므로, 가능한 한 이 순서를 유지한다.

#### 12.6.1 0단계: 기준선 고정

1. 현재 `Spot` 관련 테스트와 bench 기준선을 기록한다.
2. `pub/sub 10k`, `router 10k` 로컬 수치를 다시 남긴다.
3. 현재 known flaky test가 있는지 확인하고 메모한다.
4. 이번 작업 동안 비교할 회귀 테스트 목록을 고정한다.

이 단계 산출물:

- 기준 bench 수치
- 기준 회귀 테스트 목록
- 현재 known issue 메모

#### 12.6.2 1단계: create / destroy 경계 먼저 정리

1. `zlink_spot_new()` 경로에서 identity, registration, routed ingress 준비 지점을 찾는다.
2. 첫 `recv()`에 숨어 있는 registration / queue open / signal open을 제거한다.
3. destroy 경로에서 unregister 순서를 먼저 고정한다.
4. unread 메시지 처리 규칙이 close path에서 hang를 만들지 않는지 확인한다.

이 단계에서 먼저 추가하거나 고쳐야 할 테스트:

- `test_spot_first_recv_has_no_hidden_activation`
- `test_spot_destroy_unregisters_route_before_close`
- `test_spot_destroy_drops_or_closes_unread_without_hang`

이 단계 완료 후 바로 전체 테스트를 한 번 돌린다.

#### 12.6.3 2단계: per-spot routed `ROUTER` 도입

1. `Spot` runtime에 per-spot internal `ROUTER`를 추가한다.
2. local inproc wiring key와 runtime registration key를 정한다.
3. `spot_rid -> Spot runtime` lookup과 `Spot runtime -> inproc peer` wiring을 분리한다.
4. local routed delivery가 target queue가 아니라 target `Spot.ROUTER`로 가게 바꾼다.
5. `zlink_spot_recv(_part)`가 새 routed ingress를 읽도록 맞춘다.

이 단계에서 먼저 추가하거나 고쳐야 할 테스트:

- `test_spot_routed_recv_preserves_source_identity`
- routed recv surface 관련 기존 integration test

이 단계 완료 후:

- 전체 테스트
- routed 관련 회귀 테스트
- `single`, `multi` routed smoke

를 돌린다.

#### 12.6.4 3단계: subscribe / routed readiness 의미 정렬

1. broad node fan-out 기반 `SUBSCRIBE_READABLE`을 제거한다.
2. `ROUTED_READABLE`이 실제 target `Spot.ROUTER` readiness만 반영하게 한다.
3. dispatch coalescing이 있더라도 drain 규칙과 충돌하지 않게 맞춘다.
4. false positive readable이 남아 있는지 확인한다.

이 단계에서 먼저 추가하거나 고쳐야 할 테스트:

- `test_spot_dispatch_event_subscribe_drain_until_eagain`
- `test_spot_dispatch_event_routed_drain_until_eagain`
- `test_spot_dispatch_event_no_false_subscribe_fanout`
- `test_spot_subscribe_ready_is_level_like`
- `test_spot_routed_ready_is_level_like`

이 단계 완료 후:

- 전체 테스트
- `single`, `multi` pub/sub smoke
- `single`, `multi` routed smoke

를 돌린다.

#### 12.6.5 4단계: old queue / hidden helper 제거

1. 새 경로로 대체된 old helper queue를 제거한다.
2. hidden completion wake socket 중 routed recv용 잔여 의존을 제거한다.
3. old path와 new path가 공존한다면 feature flag나 분기점을 정리한다.
4. dead code와 obsolete comments를 함께 정리한다.

이 단계 완료 후:

- 전체 테스트
- perf smoke
- 10k bench 재실행

을 돌린다.

#### 12.6.6 5단계: POSD 반복 리뷰와 리팩토링

1. 구현 완료 직후 POSD 관점 구조 리뷰를 수행한다.
2. 큰 책임을 가진 모듈, 의미가 넓은 helper, 불필요한 include 결합을 다시 찾는다.
3. 독립적으로 줄일 수 있는 항목부터 리팩토링한다.
4. 리팩토링 후 전체 테스트와 smoke를 다시 돌린다.
5. 더 진행할 항목이 없을 때까지 반복한다.

이 단계 종료 조건:

- 남은 항목이 모두 별도 기능 설계 변경 없이는 할 수 없는 대규모 재설계뿐일 때
- 또는 추가 리팩토링이 의미 단순화나 변경 비용 감소에 실질 효과를 주지 않을 때

#### 12.6.7 6단계: 문서 승격

1. `doc/internals/`를 먼저 갱신한다.
2. 그 다음 `doc/spec/core/`를 갱신한다.
3. 그 다음 `doc/guide/`를 갱신한다.
4. public poller가 실제로 바뀌지 않았다면 polling spec은 건드리지 않는다.
5. draft 내용이 정식 문서에 모두 흡수됐는지 확인한다.

#### 12.6.8 7단계: core 배포 준비

1. release candidate를 만든다.
2. release note에 readiness / drain semantics 변경을 적는다.
3. perf smoke와 10k bench 결과를 릴리스 메모에 남긴다.
4. version compatibility 메모를 작성한다.

#### 12.6.9 8단계: bindings spec 반영

1. `doc/spec/bindings/README.md` 공통 정책을 갱신한다.
2. C binding spec을 먼저 맞춘다.
3. 그 다음 언어별 binding spec을 순차 갱신한다.
4. public poller 미변경 단계에서는 binding spec도 새 poller contract를 만들지 않는다.

#### 12.6.10 9단계: bindings 라이브러리 구현 반영

1. `bindings/c/`를 먼저 갱신한다.
2. C++ / .NET / Java / Go / Rust / Node / Python 순으로 반영한다.
3. 각 binding의 sample / tests / perf smoke를 같이 갱신한다.
4. binding helper가 readiness를 edge처럼 오해하지 않는지 확인한다.

#### 12.6.11 10단계: bindings 배포

1. core release에 맞는 native dependency를 묶는다.
2. 언어별 패키지를 순차 배포한다.
3. 각 배포 단위가 새 core 버전을 요구하는지 명시한다.
4. 배포 후 smoke 검증을 다시 한 번 수행한다.

#### 12.6.12 최종 완료 판정 순서

최종 완료는 아래 순서를 모두 통과했을 때만 선언한다.

1. 기능 구현 완료
2. 회귀 테스트 통과
3. `single`, `multi` perf smoke 통과
4. 10k bench 재검증
5. POSD 반복 리팩토링 종료
6. 내부 문서 / spec / guide 반영
7. core 배포 준비 완료
8. bindings spec 반영
9. bindings 라이브러리 반영
10. bindings 배포와 최종 smoke 확인

## 13. 검증 기준

이번 초안은 구현 완료 기준을 아래처럼 둔다.

### 13.1 기능 기준

- `Spot` 생성 후 routed target으로 즉시 지목할 수 있어야 한다.
- 첫 `zlink_spot_recv(_part)` 호출이 activation을 수행하지 않아야 한다.
- `ROUTED_READABLE` 이후 `zlink_spot_recv(_part)`는 실제 데이터를 반환해야 한다.
- `SUBSCRIBE_READABLE` 이후 `zlink_spot_subscribe(_part)`는 실제 데이터를 반환해야 한다.
- 후속 poller 확장 단계에서는 `Spot` 결과를 받은 뒤, event kind와 subject만으로
  같은 처리를 할 수 있어야 한다.

### 13.2 scale 기준

로컬 `SpotNode` 하나 아래에서 아래 검증을 통과해야 한다.

- `10,000 Spot pub/sub` 시나리오가 계속 동작해야 한다.
- `10,000 Spot routed recv prepare` 시나리오가 `EMFILE` 없이 끝나야 한다.
- routed path는 더 이상 "첫 recv에서만" 자원 한계가 드러나지 않아야 한다.
- `SpotNode.ROUTER -> Spot.ROUTER` 로컬 전달이 `inproc` 경로에서 안정적으로
  유지돼야 한다.

### 13.3 운영 비용 기준

아래 수치를 항상 같이 본다.

- create 시간
- activation 또는 prepare 시간
- steady-state send/recv 시간
- destroy 시간
- RSS

이 문서는 throughput 하나만 보고 구조를 정하지 않는다.
존재 비용과 정리 비용도 같은 비중으로 본다.

### 13.4 회귀 테스트 기준

구현 중간 단계마다 아래 회귀 테스트를 유지하거나 새로 추가해야 한다.

- `test_spot_dispatch_event_subscribe_drain_until_eagain`
  `SUBSCRIBE_READABLE` 한 번 뒤 여러 메시지를 연속으로 drain할 수 있어야 한다.
- `test_spot_dispatch_event_routed_drain_until_eagain`
  `ROUTED_READABLE` 한 번 뒤 여러 routed 메시지를 연속으로 drain할 수 있어야 한다.
- `test_spot_dispatch_event_no_false_subscribe_fanout`
  한 `Spot`에만 pub/sub 메시지가 왔을 때 다른 `Spot`에는
  `SUBSCRIBE_READABLE`이 올라오지 않아야 한다.
- `test_spot_first_recv_has_no_hidden_activation`
  첫 `zlink_spot_recv()`가 target registration이나 hidden socket open을 일으키지
  않아야 한다.
- `test_spot_destroy_unregisters_route_before_close`
  destroy 중 route lookup이 먼저 제거되어 late delivery가 닫힌 spot으로 들어가지
  않아야 한다.
- `test_spot_destroy_drops_or_closes_unread_without_hang`
  destroy 시 unread 메시지가 남아 있어도 hang 없이 정리되어야 한다.
- `test_spot_routed_recv_preserves_source_identity`
  local forward 이후에도 `source_node_rid`, `source_spot_rid`, `request_seq`가
  그대로 보여야 한다.
- `test_spot_subscribe_ready_is_level_like`
  이미 readable인 동안 메시지가 더 들어와도 이벤트 개수와 메시지 개수를 1:1로
  가정하지 않고 drain으로 처리할 수 있어야 한다.
- `test_spot_routed_ready_is_level_like`
  routed plane도 같은 readiness 규칙을 따라야 한다.

이 항목들은 단순 기능 테스트가 아니라, 이번 문서가 고정한 의미가 다시 흔들리지
않는지 확인하는 회귀 테스트다.

### 13.5 전체 테스트와 perf 스모크 기준

이번 작업은 core 내부 구조를 바꾸는 일이므로, 특정 unit test 몇 개만 통과해서는
완료로 보지 않는다.

최소한 아래 두 축을 함께 통과해야 한다.

- 전체 기능 테스트
- perf 스모크 테스트

그리고 perf 스모크는 아래 두 패턴을 모두 포함해야 한다.

- `single`
- `multi`

여기서 `single`은 단일 producer / consumer 또는 단일 requester / replier처럼
가장 단순한 배선을 뜻한다.

`multi`는 fan-in, fan-out, 여러 `Spot` 동시 활성, 여러 receiver / subscriber가
같이 붙는 배선을 뜻한다.

완료 기준은 아래와 같다.

- 기존 전체 테스트 스위트가 모두 통과한다.
- `Spot` 관련 perf 스모크가 `single`, `multi` 패턴에서 모두 통과한다.
- `pub/sub`, routed recv, channel reply가 각 패턴에서 최소 한 번 이상 검증된다.
- perf 수치는 최적화 비교표가 아니라 smoke 기준이라도 유지되어야 한다.
  즉 비정상 timeout, hang, 급격한 resource 폭증 없이 끝나야 한다.

이 문서에서 말하는 perf 스모크 최소 세트는 아래 예시를 포함한다.

- `single pub -> single sub`
- `single router -> single spot`
- `single spot -> single reply`
- `multi pub -> many sub`
- `single router -> many spot`
- `many spot -> many routed recv`

즉 이번 변경은 기능 테스트만이 아니라, 패턴별 실행 스모크까지 통과해야
실제 완료로 본다.

## 14. 이번 초안의 설계 판단

이번 초안은 아래 판단을 공개적으로 남긴다.

### 14.1 왜 queue 모델로 내리지 않는가

`pub/sub`가 이미 per-spot socket 모델로 동작하고 있고, 실제 local bench에서도
`10,000`까지 동작한다.

반면 routed queue 모델은 아래 문제가 드러났다.

- scale limit이 더 일찍 드러났다.
- 첫 `recv()`에서 hidden resource open이 있었다.
- dispatch event 의미가 더 흐려졌다.

따라서 지금 코드 기준에서는 queue 모델이 "더 단순하고 더 빠르다"고 보기 어렵다.

### 14.2 왜 socket-backed 모델로 올리는가

socket-backed 모델은 아래 장점이 있다.

- `Spot` 의미가 직관적이다.
- readiness 의미가 정확해진다.
- monitor와 poller 의미를 억지로 흉내 내지 않아도 된다.
- 비용이 create/activation 시점에 드러난다.
- routed plane에서 source identity를 숨기지 않고 그대로 유지할 수 있다.

즉 이 방향은 성능만이 아니라 **의미 보존**을 위한 선택이다.

## 15. 다른 draft와의 관계

이 초안은 아래 문서와 연결된다.

- `doc/draft/spot-routed-request-api.ko.md`
- `doc/draft/spot-dispatch-owned-channel-reply.ko.md`
- `doc/draft/spot-multi-service-topology.ko.md`

각 문서의 역할은 아래와 다르다.

- `spot-routed-request-api`
  routed request/reply public surface
- `spot-dispatch-owned-channel-reply`
  channel reply completion delivery owner
- `spot-multi-service-topology`
  `SpotNode`와 discovery/channel topology
- **이번 문서**
  `Spot` 자체를 어떤 runtime 모델로 구현할지에 대한 기준

즉 이번 문서는 API 추가 문서라기보다, 다른 `Spot` draft가 기대는 **구조 기준**
문서다.

## 16. 완료 후 문서와 바인딩 반영 계획

이 절은 구현이 끝난 뒤, 이 draft 내용을 어떤 정식 문서와 바인딩에 반영할지에
대한 작업 계획이다.

### 16.1 `doc/guide/` 반영 계획

가이드는 내부 소켓 배선이 아니라 **사용자가 체감하는 의미 변화**만 반영한다.

우선 반영 대상은 아래 문서다.

- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/02-core-api.ko.md`
- `doc/guide/02-core-api.md`
- 필요 시 `doc/guide/03-0-socket-patterns.ko.md`
- 필요 시 `doc/guide/03-0-socket-patterns.md`

가이드에는 아래만 적는다.

- `ROUTED_READABLE`, `SUBSCRIBE_READABLE`은 메시지 수가 아니라 readiness라는 점
- 이벤트를 받으면 `EAGAIN`까지 drain해야 한다는 사용 규칙
- 첫 `zlink_spot_recv()`가 hidden activation을 하지 않는다는 점
- poller direct `Spot` 등록이 정식 공개되면 그때의 사용 예제

가이드에는 아래를 넣지 않는다.

- `SpotNode.ROUTER -> Spot.ROUTER` 내부 배선 세부
- internal subject 확장 방식
- unregister/close ordering 같은 유지보수자용 설명

### 16.2 `doc/internals/` 반영 계획

내부 문서는 이번 draft의 핵심 내용을 가장 많이 흡수해야 한다.

우선 반영 대상은 아래 문서다.

- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/internals/services-internals.ko.md`
- `doc/internals/services-internals.md`
- 필요 시 `doc/internals/architecture.ko.md`
- 필요 시 `doc/internals/architecture.md`

내부 문서에는 아래를 반영한다.

- per-spot routed ingress owner 구조
- create / unregister / destroy ordering
- source identity 보존 규칙
- `SUBSCRIBE_READABLE`, `ROUTED_READABLE`의 level-like readiness 규칙
- dispatch와 poller의 의미 관계
- `SpotNode` broker와 target `Spot` delivery owner 경계

ASCII 다이어그램도 이 단계에서 함께 승격한다.

### 16.3 `doc/spec/` 반영 계획

정식 spec 문서는 공개 헤더와 테스트가 맞아떨어진 뒤에만 갱신한다.

우선 반영 대상은 아래 문서다.

- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- 필요 시 `doc/spec/core/polling.ko.md`
- 필요 시 `doc/spec/core/polling.md`
- 필요 시 `doc/spec/core/errno-map.ko.md`
- 필요 시 `doc/spec/core/errno-map.md`

반영 순서는 아래처럼 잡는다.

1. `core/include/zlink.h`와 실제 동작을 먼저 확정한다.
2. `spot` spec에 dispatch / recv / drain 의미를 반영한다.
3. public poller가 실제로 바뀐 경우에만 polling spec을 갱신한다.
4. 새 오류 코드나 실패 규칙이 생기면 errno map을 갱신한다.

특히 poller는 이번 draft에서 **후속 확장**으로 남겨 두었으므로,
같은 구현 단계에서 public poller가 바뀌지 않으면 `doc/spec/core/polling.*`는
건드리지 않는다.

### 16.4 `doc/spec/bindings/` 반영 계획

바인딩 spec은 core spec이 확정된 뒤 언어별 공개 표면에 맞춰 순차 반영한다.

공통 정책 반영 대상:

- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`

언어별 반영 대상:

- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/rust/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`

바인딩 spec에는 아래 항목을 반영한다.

- `Spot` dispatch event가 readiness 기반이라는 점
- callback / polling consumer가 drain-until-`EAGAIN`로 동작해야 한다는 점
- `ROUTED_READABLE`과 `SUBSCRIBE_READABLE`의 의미가 실제 `Spot` 기준으로 닫힌다는 점
- public poller 확장이 실제로 공개된 경우에만 그 언어 surface를 추가한다

### 16.5 개발 완료 후 POSD 리팩토링 반복 계획

기능 구현이 끝났다고 바로 배포 단계로 넘어가면 안 된다.
이번 변경은 `Spot` 내부 구조와 ownership 경계를 다시 세우는 작업이므로,
기능 완료 뒤에 **POSD 기준 반복 리팩토링 단계**를 반드시 둔다.

이 단계의 목표는 단순 cleanup이 아니다.
아래 질문에 대해 더 줄일 항목이 없을 때까지 반복하는 것이다.

- 한 모듈이 너무 많은 개념을 동시에 알고 있지 않은가
- public 또는 semi-public surface가 불필요한 내부 구조를 새고 있지 않은가
- readiness, delivery owner, transport owner 의미가 한 곳에서 섞이지 않는가
- 첫 호출, destroy, error path에 숨겨진 비용이나 surprise가 남아 있지 않은가
- 테스트가 구현 우연이 아니라 문서가 정한 의미를 붙잡고 있는가

반복 단위는 아래처럼 고정한다.

1. 기능 구현 완료
2. POSD 관점 구조 리뷰
3. 리팩토링 후보 목록 작성
4. 영향 범위가 명확한 항목부터 코드 반영
5. 회귀 테스트와 bench 재실행
6. 다시 POSD 관점 구조 리뷰

종료 조건도 문서에 명시한다.

- 새로 발견되는 리팩토링 항목이 더 이상
  "의미를 단순하게 만들고 변경 비용을 줄이는 실질 효과"를 주지 않을 때
- 또는 남은 항목이 전부 별도 기능 설계 변경 없이는 진행할 수 없는 대규모 재설계일 때

즉 배포 직전 완료 판정은 "기능이 된다"가 아니라 아래를 모두 만족해야 한다.

- 기능이 문서와 테스트 기준에 맞게 동작한다.
- POSD 기준의 구조적 리팩토링을 반복 수행했다.
- 더 진행할 가치가 있는 독립 리팩토링 항목이 남아 있지 않다.

### 16.6 배포 계획

바인딩 라이브러리에 기능을 반영하려면, 먼저 그 바인딩이 의존하는 core 산출물이
배포 가능 상태여야 한다.

이번 기능은 `Spot` readiness 의미와 dispatch 동작에 직접 닿으므로, 바인딩 쪽만
먼저 바꾸면 안 된다. 최소한 아래 배포 순서를 따른다.

1. core native 라이브러리와 public header를 새 계약에 맞춰 빌드한다.
2. core smoke / regression / bench 기준을 통과한 release candidate를 만든다.
3. C binding 기준 패키지 또는 설치 산출물을 먼저 배포한다.
4. 그 다음 상위 바인딩이 새 core 산출물에 링크되도록 언어별 패키지를 배포한다.

배포 준비 항목은 아래와 같다.

- release note에 `Spot` readiness / drain semantics 변경점을 적는다.
- 버전 호환 표에 "어떤 core 버전부터 새 `Spot` 의미를 보장하는가"를 적는다.
- public poller 확장이 같은 릴리스에 포함되지 않으면, "poller direct `Spot` 등록은
  아직 미지원"을 명확히 적는다.
- language package가 묶는 native binary 또는 shared library 버전을 고정한다.

언어별 배포 단위는 기존 바인딩 배포 체계를 따른다.

- C / C++: native package 또는 설치 산출물
- .NET: NuGet
- Java: Maven artifact
- Go: module tag
- Rust: crate release
- Node: npm package
- Python: PyPI package

즉 구현 반영 순서는 "문서 -> 코드 -> 바인딩"처럼 보이더라도,
실제 사용자 전달 순서는 **core 배포 -> bindings 배포 -> bindings 기능 노출**이다.

이 배포 계획은 바로 앞의 POSD 리팩토링 반복 단계가 끝난 뒤에만 시작한다.

### 16.7 `bindings/` 라이브러리 반영 계획

문서만 바꾸고 실제 바인딩 구현이 뒤따르지 않으면 계약이 다시 어긋난다.
그래서 언어별 바인딩 라이브러리도 아래 순서로 반영한다.

공통 원칙은 아래와 같다.

1. C core contract를 먼저 확정한다.
2. C binding smoke / regression test를 통과시킨다.
3. 관리형 바인딩과 고수준 바인딩은 같은 의미를 언어 스타일에 맞게 옮긴다.
4. 각 바인딩은 `Spot` readiness를 edge처럼 오해하는 helper를 남기지 않는다.

실제 반영 대상 디렉토리는 아래와 같다.

- `bindings/c/`
- `bindings/cpp/`
- `bindings/dotnet/`
- `bindings/java/`
- `bindings/go/`
- `bindings/rust/`
- `bindings/node/`
- `bindings/python/`

바인딩 구현에서 점검할 항목은 아래와 같다.

- dispatch event wrapper가 `ROUTED_READABLE`, `SUBSCRIBE_READABLE`을 drain 모델로
  설명하고 있는가
- `Spot.recv()` / `Spot.subscribe()` helper가 반복 drain 예제를 제공하는가
- poller wrapper가 public core poller와 같은 범위만 노출하는가
- public poller 확장을 하지 않은 단계에서 binding이 먼저 임의의 `Spot` poller
  contract를 만들지 않는가
- 언어별 test / sample / perf가 새 의미를 반영하는가

### 16.8 완료 판정 후 실제 승격 순서

구현이 끝난 뒤 문서와 바인딩 반영은 아래 순서를 권장한다.

1. core code와 core regression test를 먼저 확정한다.
2. POSD 기준 구조 리뷰와 리팩토링을 반복해 더 진행할 항목이 없을 때까지 정리한다.
3. `doc/internals/`를 갱신해 유지보수자 기준 구조를 고정한다.
4. `doc/spec/core/`를 갱신해 공개 계약을 확정한다.
5. `doc/guide/`를 갱신해 사용자 사용법을 맞춘다.
6. core release candidate를 만들고 배포 준비를 끝낸다.
7. `doc/spec/bindings/`를 갱신해 언어별 계약을 맞춘다.
8. `bindings/*` 라이브러리 구현과 테스트를 순차 갱신한다.
9. 언어별 bindings 배포를 진행한다.
10. 마지막으로 draft 문서를 축소하거나, 정식 문서 링크만 남기고 종료한다.

## 17. 비규범 작업 메모

이 절은 구현 전 초안의 **비규범 작업 메모**다.

구현 순서는 아래처럼 잡는 것이 현실적이다.

1. routed target registration을 create 시점으로 이동
2. routed recv prepare benchmark를 다시 측정
3. per-spot internal `ROUTER` 도입
4. local `SpotNode.ROUTER -> Spot.ROUTER` 전달 경로 전환
5. dispatch event 의미 정렬
6. poller 의미 정렬
7. hidden queue 제거
8. bench와 integration test 재측정

## 18. 비규범 검증 메모

이 절은 구현 전 초안의 **비규범 검증 메모**다. 공개 계약을 새로 정의하지는
않는다.

최소 검증 세트는 아래와 같다.

- `spot_node_10000_pubsub_bench`
- `spot_node_10000_router_bench`
- `test_spot_dispatch_event`
- `test_spot_dispatch_event_subscribe_drain_until_eagain`
- `test_spot_dispatch_event_routed_drain_until_eagain`
- `test_spot_dispatch_event_no_false_subscribe_fanout`
- `test_spot_first_recv_has_no_hidden_activation`
- `test_spot_destroy_unregisters_route_before_close`
- `test_spot_routed_recv_preserves_source_identity`
- `test_spot_subscribe_ready_is_level_like`
- `test_spot_routed_ready_is_level_like`
- `test_zmp_request_reply`
- `test_zmp_request_reply_router_recv_surface`
- `test_spot_pubsub_scenario`
- poller 관련 integration test

perf / smoke 검증은 아래 범주를 반드시 포함한다.

- `single` 패턴 smoke
- `multi` 패턴 smoke
- `Spot pub/sub` smoke
- `Spot routed recv` smoke
- channel reply progress smoke

특히 routed 쪽은 아래를 반드시 다시 확인해야 한다.

- `9999`, `10000` routed prepare에서 `EMFILE`이 사라졌는가
- `ROUTED_READABLE`과 실제 `recv` 가능 상태가 맞는가
- destroy 시간이 create 대비 비정상적으로 커지지 않는가
