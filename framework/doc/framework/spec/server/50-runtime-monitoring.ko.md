# 런타임 모니터링 — 공통 스펙

[스펙 목차](../README.ko.md) | [이전: Location Store](41-location-store-redis.ko.md) | [다음: 런타임 메트릭 계기 (Runtime Metrics Instruments)](51-runtime-metrics.ko.md)

> 이 문서는 **runtime 변화 관측의 언어 중립 정본**이다. source별 표면을 나누는 근거, event
> 종류, polling 규칙, startup validation을 소유한다.
>
> 메시지 한 건의 생애주기 관측은 [message-flow-tracing](52-message-flow-tracing.ko.md),
> 계기 카탈로그는 [runtime-metrics](51-runtime-metrics.ko.md), 흐름 상관관계는
> [flow-correlation](53-flow-correlation.ko.md)이 소유한다.
>
> 언어별 타입과 등록 표면은 `languages/<lang>/`의 monitoring 문서가 고정한다.

## 1. 목적

**handler 호출만 관측해서는 부족하다.** 운영은 runtime 변화도 framework 표면에서 받아야 한다.

- socket connect / disconnect / handshake 실패
- location store 상태와 topology 변화
- spot node의 peer / subject 변화
- spot timer handler 실패

## 2. source마다 표면을 나눈다

**하부 표면이 source마다 모양이 다르다.** 하나의 API로 네 source를 전부 덮으려면 **가장 낮은
수준의 모양으로 내려가야 하고**, 그러면 location과 spot은 **실제 하부 능력보다 더 많은 것을
약속하게 된다.**

| source | 관측 방식 |
|---|---|
| **socket** | **raw monitor를 그대로 감싼다.** payload에 native enum과 상태 코드를 함께 노출한다 |
| **location / spot 상태** | **주기적으로 상태를 읽고 직전 상태와 비교해 바뀐 때만 event를 합성한다** |
| **spot timer 실패** | **polling interval을 기다리지 않고 발생 시점에 즉시 발행한다** |
| **location row**(peer·spot·actor·route) | location 계열 source로 관찰하거나 location runtime query로 직접 조회한다 |
| **application** | typed runtime event handler로 받는다 |

**이렇게 나눠야 framework가 source별 구현 차이를 숨기면서도, 없는 기능을 있는 것처럼 보이지 않게
할 수 있다.**

## 3. Event 표현 규칙

- **socket event 종류는 필터 등록용 enum으로 둔다.**
- **callback event는 source별 추상 타입 아래의 variant로 구분한다.**
- **필터 enum 하나만으로 callback payload를 표현하지 않는다.** 운영 코드는 event 종류뿐 아니라
  **source 이름, endpoint, routing id, snapshot 본문**도 함께 필요하다.
- **location·spot callback은 variant의 타입 자체가 event 종류를 나타낸다.** 그래서 nullable
  payload 조합이 생기지 않는다.

### 3.1 event 종류

**의미만 고정한다. 정확한 타입·variant 이름은 언어별 스펙이 소유한다.**

| source | 관측해야 하는 변화 |
|---|---|
| **location runtime** | 상태 변경 · topology 변경 · 서비스 요약 변경 · store 실패 · store 복구 |
| **spot** | 상태 변경 · peer 집합 변경 · subject 집합 변경 · timer handler 실패 · 처리되지 않은 예외로 timer 중단 |

**drain 이벤트는 이 등록 모델 밖이다.** graceful drain의 lifecycle 이벤트는 저빈도이고 노드당
하나뿐이라 source 등록이나 polling 주기가 필요 없다. runtime event handler가 등록되어 있으면
monitoring 구성과 무관하게 항상 수신한다. 그 계약은
[54 §9](54-graceful-drain-handoff.ko.md)가 소유한다.

**timer handler 실패는 두 갈래를 구분한다** — timer가 **계속 도는** 실패와, **timer가 중단된**
실패다.

**제거된 자동 발견 runtime event payload를 다시 두지 않는다.**

### 3.2 handler 실패 격리

application의 runtime event handler는 여러 개 등록할 수 있다.

- **한 handler가 예외를 던져도 다음 handler를 계속 실행한다.** monitoring loop를 깨지 않는다.
- **예외는 runtime error sink로 보고한다.**
- **취소만 전파한다.** 취소 신호로 인한 중단은 그대로 위로 올린다.

## 4. Polling

- **location·spot polling 주기는 monitoring 등록 시점에 항상 명시한다.**
- **숨은 기본 주기를 두지 않는다.** 운영 코드가 **polling 비용을 설정에서 바로 읽을 수 있어야**
  한다.

## 5. Source 이름

source 이름은 topology와 역할을 읽을 수 있게 잡는다
([channel-topology §5.4](10-channel-topology.ko.md)).

## 6. Startup validation

| 조건 | 결과 |
|---|---|
| **socket source가 `<channel>.<capability>` 형식이 아니거나 그 channel 역할이 등록되지 않음** | **설정 오류** |
| **spot source가 등록된 SpotNode 이름을 가리키지 않음** | **설정 오류** |
| **location source를 쓰는데 location runtime이 등록되지 않음** | **설정 오류** |
| **polling interval이 0 이하** | **설정 오류** |
| **monitoring을 두 번 구성** | **설정 오류** — monitoring 등록은 프로세스당 하나다 |
| **source 이름이 비어 있음** | **설정 오류** |

**모든 설정 오류는 host 시작 전에 실패한다.**

- **location source 이름은 event를 구분하는 사용자 지정 이름이다.** store row나 역할 이름과
  대조하지 않는다.
- **임의 source 자동 발견은 지원하지 않는다.** 자동 연결 상태는 location runtime source와 runtime
  query로 관찰한다.

## 7. 회귀 테스트

| 항목 | 검증 |
|---|---|
| source 분리 | socket은 raw monitor, location·spot은 합성 event, timer 실패는 즉시 발행 |
| timer 실패 | handler 예외가 **계속 실행되는 실패**와 **timer 중단 실패**로 구분되어 event에 반영된다 |
| polling | 등록 시점에 명시하지 않은 숨은 기본 주기가 없다 |
| startup validation | §6의 각 행이 그대로 동작한다 |
