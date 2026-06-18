[스펙 목차](../README.ko.md)

# Draft -- 자동 HWM 정책

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API, 상수, 기본 동작을
> 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 정식 spec 문서에 합친다.
>
> 주의:
> 이 문서 안의 일부 계산식과 예시는 초기 초안 기준으로 적혀 있다.
> 특히 역할 묶음 고정 비율로 queue budget을 자르는 부분은 최신 방향이 아니다.
> 재계산 시점과 최신 계산식 방향은
> [auto-hwm-recalculation-policy.ko.md](auto-hwm-recalculation-policy.ko.md)
> 를 우선 기준으로 본다.
> context 메모리 산정과 소켓 역할, client 수, `MsgUnit(B)` 기반 HWM 계산식은
> [auto-hwm-context-memory-sizing.ko.md](auto-hwm-context-memory-sizing.ko.md)
> 를 기존 계산 정책을 대체하는 개선 초안 기준으로 본다.

## 1. 목적

이 초안은 `zlink`가 `SNDHWM` / `RCVHWM` 값을 사용자가 매번 직접 정하지 않아도,
운영 환경에 맞게 **자동으로 계산하고 적용하는 기본 정책**을 정의하는 방향을
정리한다.

이 초안이 풀고 싶은 실제 문제는 아래와 같다.

- HWM은 연결별 대기열 상한이라서, 연결 수가 커지면 같은 값도 의미가 크게 달라진다.
- `spot` / `spotnode`는 내부 소켓이 여러 개라서 사용자가 소켓별 값을 직접 정하기
  어렵다.
- 운영 중 연결 수, 메시지 크기, 순간적으로 몰리는 양은 계속 변하는데, 수동
  튜닝은 그 변화를 따라가기 어렵다.
- HWM을 너무 크게 잡으면 큐 메모리가 커지고, 너무 작게 잡으면 조금만 느린
  peer가 생겨도 보내는 쪽이 빨리 막히거나 즉시 실패를 돌려받게 된다.

이 초안의 핵심 목표는 아래와 같다.

- 사용자가 소켓별 HWM 숫자를 직접 계산하지 않아도 되게 한다.
- context 전체에 허용할 최대 메모리를 먼저 정하고, 그 범위 안에서 HWM과
  transport buffer를 자동으로 맞춘다.
- 일반 코어 소켓과 서비스 계열 handle이 같은 큰 원칙을 공유하게 한다.
- `spot` / `spotnode`처럼 내부 소켓이 많은 경우에도 이해하기 쉬운 계산 기준을
  제공한다.

## 2. 범위

이 초안은 아래를 다룬다.

- context 단위 총 메모리 예산
- 예산을 바탕으로 한 자동 `SNDHWM` / `RCVHWM` 계산
- 예산을 바탕으로 한 자동 `SNDBUF` / `RCVBUF` 계산
- 소켓 역할별 기본 묶음
- `spot` / `spotnode` 내부 소켓에 대한 자동 적용 원칙
- 운영 가이드와 내부 계산 규칙

이 초안은 아래를 직접 바꾸지 않는다.

- `SNDHWM` / `RCVHWM`의 의미 자체
- `SNDBUF` / `RCVBUF`의 의미 자체
- 기존 수동 설정 API의 의미
- 응용 계층의 재시도 정책
- 큐 내부 구현 방식
- payload 압축, 메시지 병합, transport buffer 정책

즉 이 문서는 **새 흐름 제어 알고리즘**을 만드는 것이 아니라,
**HWM 값을 자동으로 정하는 정책 계층**을 추가하는 방향을 다룬다.

## 3. 배경

현재 `zlink`의 HWM은 연결별 대기열 상한이다. 즉 소켓 전체의 총 메시지 수를
제한하는 값이 아니라, 각 연결 `pipe`가 보관할 수 있는 최대 메시지 수를 정한다.

이 성질 때문에 아래 같은 단순 계산은 실제 운영과 잘 맞지 않는 경우가 많다.

- "최대 client 수를 10000으로 보고 총합 10만 HWM을 연결 수로 나눠서 쓴다"
- "현재 연결 수가 적으면 연결 하나당 HWM을 크게 올린다"
- "소켓이 N개니까 소켓마다 같은 메모리 몫을 나눠 준다"

이 계산이 어긋나는 이유는 아래와 같다.

- HWM은 바이트가 아니라 **메시지 개수**다.
- 메시지 크기가 바뀌면 같은 HWM도 메모리 사용량이 크게 달라진다.
- 내부 소켓이 많아도, 모든 소켓이 동시에 메시지를 오래 쌓아 두는 것은 아니다.
- `spot` / `spotnode`는 제어 경로, 지정 송신 경로, publish 분배 경로, 수신
  경로처럼 역할이 다른 내부 소켓이 함께 움직인다.

따라서 자동 HWM은 "소켓 하나당 얼마"보다 먼저, **이 context 전체가 얼마나 많은
메모리를 써도 되는가**를 기준으로 삼아야 한다.

## 4. 기본 원칙

### 4.1 HWM은 메모리를 미리 잡아 두는 값이 아니다

자동 HWM에서 먼저 분명히 해야 할 점은, HWM이 메모리를 미리 예약하는 값은
아니라는 점이다.

HWM은 아래 뜻에 가깝다.

- 각 연결 대기열이 최대로 몇 개 메시지까지 밀릴 수 있는가
- 그 이상 밀리면 보내는 쪽을 기다리게 할지, 바로 실패를 돌려줄지

즉 HWM이 크다고 해서 그만큼 메모리가 즉시 잡히는 것은 아니다.
하지만 최악 상황에서 큐가 커질 수 있는 **잠재 상한**은 HWM에 의해 크게
달라진다.

### 4.2 자동 계산은 socket보다 context를 먼저 본다

이 초안은 자동 HWM의 최상위 관리 단위를 **context**로 둔다.

이유는 아래와 같다.

- 소켓 수만으로는 실제 사용 형태를 설명하기 어렵다.
- 같은 context 아래의 여러 소켓이 같은 I/O 자원과 큐 부담을 함께 쓴다.
- `spot` / `spotnode`는 내부 소켓 수가 많지만, 실제 부담은 일부 경로에
  집중된다.
- 운영자가 이해하기 쉬운 설정도 "소켓별 HWM"보다 "이 context가 써도 되는 총
  메모리" 쪽이 더 낫다.

즉 자동 계산은 아래 흐름을 따른다.

```text
process or container memory
-> context total memory budget
-> queue / transport / reserve split
-> role group budget
-> busy connection estimate
-> per-connection HWM
```

### 4.3 소켓을 역할별 묶음으로 나눈다

같은 context 안에서도 모든 소켓이 같은 방식으로 메모리를 쓰지 않는다.
이 초안은 소켓을 아래 같은 역할별 묶음으로 나누는 방향을 제안한다.

| 역할 묶음 | 의미 |
|---|---|
| `control` | 상태 전파, 내부 제어, 작은 관리 메시지 |
| `routed` | 요청/응답, 지정 송신, peer별 경로 선택이 필요한 통신 |
| `fanout` | publish, broadcast, 여러 peer로 퍼뜨리는 경로 |
| `recv_ingress` | 외부 또는 peer에서 들어오는 메시지를 먼저 받는 수신 경로 |

자동 HWM은 먼저 context 메모리 예산을 이 묶음들로 나눈 뒤, 각 묶음 안에서 다시
연결 수와 메시지 크기를 기준으로 계산한다.

### 4.4 수동 설정은 항상 자동 설정보다 우선한다

자동 기능이 기본이더라도, 이미 검증된 운영값이 있거나 벤치마크처럼 정확한
고정값이 필요한 경우는 남아 있다.

따라서 이 초안은 아래 우선순위를 가정한다.

1. 사용자가 소켓 또는 서비스 handle에 `SNDHWM` / `RCVHWM`을 직접 설정했다.
2. 그러면 해당 handle은 자동 계산 대상에서 제외한다.
3. 사용자가 `SNDBUF` / `RCVBUF`를 직접 설정했다면 transport buffer 자동
   계산에서도 제외한다.
4. 별도 수동값이 없는 경우에만 context 정책이 자동값을 공급한다.

이 규칙이 있어야 자동 기능이 기본이 되더라도, 기존 수동 튜닝 경로와 충돌하지
않는다.

## 5. 개념 모델

### 5.1 Context Total Memory Budget

이 초안이 제안하는 가장 중요한 설정은 아래 개념이다.

```text
context total memory budget
```

이 값은 "이 context가 내부 메시지 큐, transport buffer, 기타 runtime 여유분을
합쳐서 써도 되는 총 메모리 한도"를 뜻한다.

여기서 중요한 점은 아래와 같다.

- 프로세스 전체 메모리와 같은 뜻이 아니다.
- application heap, cache, GC, 기타 버퍼를 제외한 뒤 이 context에 배정한
  몫이다.
- HWM 계산과 transport buffer 계산은 이 총 예산을 넘지 않는 방향으로
  보수적으로 진행한다.

총 예산은 내부적으로 아래처럼 나뉜다.

```text
context_total_memory_budget =
  queue_budget
  + transport_budget
  + runtime_reserve
```

각 항목의 뜻은 아래와 같다.

- `queue_budget`
  `SNDHWM` / `RCVHWM` 계산에 쓰는 내부 메시지 큐 메모리 예산
- `transport_budget`
  `SNDBUF` / `RCVBUF` 계산에 쓰는 transport 계층 버퍼 예산
- `runtime_reserve`
  통계, 임시 버퍼, 구현 오버헤드 같은 여유분

이렇게 나누는 이유는 아래와 같다.

- 사용자가 context에 줄 총 메모리 하나만 알아도 된다.
- HWM만 자동 계산하면 사용자가 실제 최대 메모리를 예측하기 어렵다.
- `SNDBUF` / `RCVBUF`도 연결 수에 따라 메모리 영향이 커지므로,
  같은 총 예산 안에서 함께 다루는 편이 예측 가능하다.

다만 `SNDBUF` / `RCVBUF`는 OS가 관리하는 소켓 버퍼다.
따라서 이 초안에서 말하는 총 메모리 한도는 "정확한 RSS 상한"이 아니라,
"이 정책이 목표로 하는 설계 상한"으로 읽는 편이 맞다.

### 5.1.1 내부 분배 비율

총 예산을 `queue_budget`, `transport_budget`, `runtime_reserve`로 나눌 때는
사용 형태를 반영해야 한다.

일반적인 시작 비율 초안은 아래와 같다.

| 항목 | 기본 비율 |
|---|---:|
| `queue_budget` | 60% |
| `transport_budget` | 30% |
| `runtime_reserve` | 10% |

이 비율은 모든 경우에 고정된 정답이 아니다.
예를 들면 `STREAM`처럼 연결 수가 매우 많고 transport 버퍼 영향이 큰 경우에는
`transport_budget` 비율이 더 커질 수 있다.

다만 현재 설계에서는 pattern별 total memory 분배 비율을 별도로 두지 않고,
위 `60 / 30 / 10` 기본값을 모든 context에 공통 적용한다.
즉 이 절의 기본 비율은 "설명용 예시"가 아니라 현재 설계의 실제 기본값으로 본다.

### 5.2 실효 메시지 크기

HWM은 메시지 개수 기준이므로, 바이트 예산을 메시지 수로 바꾸려면
"실제로 메시지 하나가 큐에서 얼마나 큰가"를 추정해야 한다.

이 초안은 이를 `effective_message_bytes`라고 부른다.

이 값은 아래 요소를 반영한 보수적 추정값을 쓴다.

- 최근 평균 메시지 크기
- 최근 큰 메시지의 영향
- 큐, frame, allocator 오버헤드

구현 초안 기준 계산식은 아래와 같다.

```text
effective_message_bytes =
  max(recent_ewma_message_bytes, recent_p95_message_bytes)
  * overhead_factor
```

여기서 `overhead_factor`는 "메시지 본문 외에 붙는 여유분까지 조금 더 잡아 주는
계수"이며, 1.1 ~ 2.0 정도를 가정한다.

현재 설계에서는 아래 고정 시작값을 쓴다.

- `recent_ewma_message_bytes` 기본값: `1024`
- `recent_p95_message_bytes` 기본값: `1024`
- `overhead_factor` 기본값: `1.25`
- `effective_message_bytes` 초기값: `1280`

현재 설계에서는 런타임 통계를 아직 모으지 않는다.
즉 위 값들은 생성 시점 1회 계산에 쓰는 고정 시작값이다.
실제 `recent_ewma_message_bytes`, `recent_p95_message_bytes` 갱신과 이를 이용한
자동 재계산은 runtime 재계산 확장 항목에서 다룬다.

다만 송신과 수신은 메시지 크기 분포가 다를 수 있다.
따라서 정식 구현에서는 아래 둘을 따로 둘 수 있어야 한다.

- `send_effective_message_bytes`
- `recv_effective_message_bytes`

초기 구현에서 두 값을 하나로 합쳐 쓸 수는 있지만, 그 경우에는 "송신과 수신의
메시지 크기 차이를 아직 따로 반영하지 않는다"는 점을 문서와 진단 정보에 함께
드러내야 한다.

### 5.3 실제로 HWM 계산에 반영해야 하는 연결 수

자동 HWM은 전체 연결 수로 단순히 나누지 않는다.
실제로 큐를 밀어 올릴 가능성이 있는 연결 수를 따로 본다.

이 초안은 이를 `active_hwm_connections`라고 부른다.

여기서 말하는 HWM은 연결별 대기열 상한이다.
이는 `ZLINK_OPT_BACKLOG`처럼 수신 대기 연결 개수를 뜻하는 listen backlog와는
다른 개념이다.

따라서 이 초안은 혼동을 줄이기 위해 `backlog`라는 표현보다
`연결별 HWM`, `연결별 대기열 상한`이라는 표현을 우선 사용한다.

예시는 아래와 같다.

- `fanout`: 지금 실제로 보내고 있는 대상 peer 또는 subscriber 수
- `routed`: 최근 send/recv 활동이 있었던 peer 수
- `control`: 거의 항상 작은 고정 추정값
- idle runtime: 최소값만 유지하고 큰 메모리 분배 대상에서 제외

즉 이 값은 "총 연결 수"보다 "지금 이 역할 묶음에서 메시지가 밀릴 가능성이 있는
연결 수"에 가깝다.

### 5.4 기본 보장 몫

이 초안은 모든 연결에 똑같은 고정 `min_hwm` 값을 두지 않는다.
대신 각 역할 묶음과 현재 규모를 반영해, 각 연결에 **최소한 유지해야 하는 기본
몫**을 계산식 안에서 구한다.

이 규칙이 필요한 이유는 아래와 같다.

- 연결 수가 1개일 때와 10000개일 때 같은 고정 하한값은 의미가 다르다.
- `control`과 `fanout`은 같은 하한값을 쓰기 어렵다.
- 하한을 별도 숫자로 고정해 두면 자동 정책의 일관성이 깨진다.

따라서 자동 계산은 아래 순서를 따라야 한다.

1. 먼저 관리 대상 연결 수를 센다.
2. 각 역할 묶음과 현재 상태를 보고 연결당 기본 보장 몫을 계산한다.
3. 그 연결들에 기본 보장 몫을 배정하는 데 필요한 기본 메시지 수를 계산한다.
4. 전체 예산에서 이 기본 몫을 먼저 뺀다.
5. 남은 몫만 바쁜 연결들에 추가로 분배한다.

이 초안은 관리 대상 연결 수를 아래처럼 본다.

```text
managed_connections
```

이 값은 "이 역할 묶음에서 기본 HWM을 유지해야 하는 연결 수"를 뜻한다.
보통은 현재 붙어 있는 연결 수, 또는 그와 거의 같은 수로 본다.

현재 설계에서는 `managed_connections`를 현재 붙어 있는 연결 수로 정의한다.

연결당 기본 보장 몫은 아래처럼 계산한다.

```text
base_floor_per_connection =
  floor_function(role, managed_connections, effective_message_bytes,
                 group_budget_bytes)
```

여기서 `floor_function(...)`은 사용자에게 노출되는 고정 숫자가 아니라,
역할과 현재 규모를 반영해 내부적으로 계산되는 값이다.

다만 현재 설계에서는 이 함수의 시작점이 분명해야 한다.
따라서 현재 설계는 아래 내부 고정 규칙으로 `floor_function(...)`을 시작한다.

```text
if role == control:
  return 4

if role == routed:
  if managed_connections <= 1000: return 8
  if managed_connections <= 5000: return 4
  return 2

if role == fanout:
  if managed_connections <= 100: return 16
  if managed_connections <= 1000: return 8
  if managed_connections <= 5000: return 4
  return 1

if role == recv_ingress:
  if managed_connections <= 1000: return 8
  if managed_connections <= 5000: return 4
  return 2
```

이 값들은 현재 설계의 내부 시작값이다.
운영 측정 결과에 따라 이후 조정될 수 있지만, 구현 시작 시점에는 위 규칙을
그대로 사용한다.

기본 메시지 수는 아래처럼 계산한다.

```text
base_slots =
  managed_connections * base_floor_per_connection
```

역할 묶음에 쓸 수 있는 전체 메시지 수에서 이 바닥 몫을 먼저 뺀 뒤,
남은 몫만 추가 분배에 쓴다.

```text
distributable_slots =
  max(0, group_message_slots - base_slots)
```

## 6. 자동 계산 규칙 초안

### 6.1 1단계: context 메모리 한도 결정

먼저 context 전체 총 메모리 한도를 정한다.

```text
context_total_memory_budget_bytes
```

이 값은 사용자가 직접 줄 수도 있고, 시스템이 기본 추정값으로 정할 수도 있다.

현재 설계에서는 사용자가 명시적으로 `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`
를 주는 경로를 우선한다.
명시값이 없을 때만 아래 `7.5 자동 기본값 초안`의 고정 시작값을 사용한다.

### 6.2 2단계: 역할 묶음별로 메모리 나누기

다음으로 context 총 메모리를 내부 용도별로 나눈다.

```text
queue_budget_bytes =
  context_total_memory_budget_bytes * queue_budget_weight

transport_budget_bytes =
  context_total_memory_budget_bytes * transport_budget_weight

runtime_reserve_bytes =
  context_total_memory_budget_bytes * runtime_reserve_weight
```

그 다음 `queue_budget_bytes`만 역할 묶음에 다시 배분한다.

```text
group_budget_bytes =
  queue_budget_bytes * group_weight
```

기본 비율 초안은 아래와 같다.

| 역할 묶음 | 기본 비율 |
|---|---:|
| `control` | 5% |
| `routed` | 25% |
| `fanout` | 50% |
| `recv_ingress` | 20% |

이 기본 비율은 사용 형태에 따라 달라질 수 있다.

이 수치는 측정으로 이미 확정된 값이 아니라, **현재 설계의 시작값**이다.
즉 "왜 5 / 25 / 50 / 20인가"에 대한 답은 "각 역할 묶음이 큐를 크게 만들 가능성과,
너무 작게 줬을 때의 피해 크기"를 기준으로 잡은 값이라는 뜻이다.

역할 묶음별 이유는 아래와 같다.

- `control = 5%`
  제어 메시지는 보통 작고 빈도도 낮다. 하지만 너무 작게 주면 상태 전파나 내부
  제어가 막힐 수 있으므로, 굶기지 않을 정도의 최소 몫은 따로 보장해야 한다.
- `routed = 25%`
  요청/응답 경로는 중요하지만, 보통 fanout처럼 모든 연결이 동시에 길게 밀리지는
  않는다. 일부 active peer가 주로 부담을 만든다고 보고 중간 비중을 둔 값이다.
- `fanout = 50%`
  publish, broadcast, 다수 peer 분배 경로는 느린 subscriber가 생겼을 때 큐가
  가장 빨리 커질 가능성이 있다. 연결 수가 많고 메모리를 가장 많이 먹기 쉬운
  축이므로 가장 큰 몫을 둔다.
- `recv_ingress = 20%`
  수신 경로는 순간적으로 몰리는 양을 받아내야 해서 너무 작으면 바로 막힌다.
  다만 fanout처럼 오랫동안 크게 누적되는 경우는 상대적으로 덜하다고 보고,
  fanout보다는 작고 routed보다는 무시할 수 없는 몫을 둔다.

따라서 이 비율은 "최종 정답"이 아니라, 아래 목적을 만족하기 위한 초기값으로
읽는 편이 맞다.

- control 경로가 굶지 않게 한다.
- routed 경로에 중간 정도의 여유를 둔다.
- fanout 경로에 가장 큰 메모리 몫을 준다.
- recv 경로가 순간적으로 몰리는 입력을 버틸 수 있게 한다.

정식 구현 단계에서는 이 비율이 아래 자료와 맞는지 다시 확인해야 한다.

- perf 측정 결과
- 실제 운영 트래픽 분포
- 역할 묶음별 backpressure 비율
- 역할 묶음별 평균 메시지 크기와 active peer 수

즉 현재 비율은 "운영 측정으로 이미 검증된 값"이 아니라,
"현재 설계에서 검증하고 조정할 기본값"이다.

### 6.3 3단계: queue budget으로 HWM 계산

각 역할 묶음에 배정한 메모리를 `effective_message_bytes`로 나누어, 그 묶음이
현재 조건에서 몇 개 메시지를 감당할 수 있는지 계산한다.

```text
group_message_slots =
  floor(group_budget_bytes / effective_message_bytes)
```

그 다음 이 묶음의 모든 관리 대상 연결에 기본 보장 몫을 먼저 배정하는 데 필요한
기본 메시지 수를 계산한다.

```text
base_floor_per_connection =
  floor_function(role, managed_connections, effective_message_bytes,
                 group_budget_bytes)

base_slots =
  managed_connections * base_floor_per_connection
```

그리고 실제로 나눠 줄 수 있는 남은 메시지 수를 계산한다.

```text
distributable_slots =
  max(0, group_message_slots - base_slots)
```

### 6.4 4단계: 바쁜 연결에 남은 몫 나누기

그 다음 그 역할 묶음 안의 활성 연결 수로 나누어 연결 하나당 추가 HWM을
계산한다.

```text
extra_hwm =
  active_hwm_connections > 0
    ? floor(distributable_slots / active_hwm_connections)
    : 0
```

여기서 중요한 규칙은 아래와 같다.

- `active_hwm_connections == 0`이면 나누기를 하지 않는다.
- 이 경우 추가 HWM은 `0`으로 두고, 관리 대상 연결은
  `base_floor_per_connection` 수준만 유지한다.
- 즉 쉬고 있는 연결이 많을 때는 큰 값을 억지로 나누지 않는다.

### 6.5 5단계: 최종 HWM 계산

마지막으로 기본 몫과 추가 몫을 합쳐 최종 HWM을 계산한다.

```text
final_hwm =
  base_floor_per_connection + extra_hwm
```

이 계산을 쓰면 아래 조건을 동시에 만족시키기 쉽다.

- 모든 관리 대상 연결에 역할별 기본 HWM을 보장한다.
- 남는 몫만 바쁜 연결에 추가로 배정한다.
- 연결 수가 많아져도 기본 보장 몫을 계산에 먼저 반영하므로
  context 메모리 한도와 더 잘 맞출 수 있다.
- 별도 `min_hwm`, `max_hwm` 정책 값을 사용자에게 요구하지 않아도 된다.

### 6.6 6단계: transport budget으로 `SNDBUF` / `RCVBUF` 계산

총 메모리 예산에는 transport buffer도 포함된다.
따라서 `SNDBUF` / `RCVBUF`도 같은 총 예산 안에서 계산해야 사용자가 최대 메모리
영향을 더 쉽게 예측할 수 있다.

계산 방향은 아래와 같다.

```text
transport_connection_count
```

이 값은 실제 transport 소켓 버퍼가 붙는 연결 수를 뜻한다.

다만 연결이 아직 붙기 전에도 transport buffer 정책값은 필요하다.
listener 생성 직후나 connect 직전처럼 아직 실제 연결 수가 `0`인 시점에도
적용할 기본값이 있어야 하기 때문이다.

따라서 초기 계산에서는 아래 값을 쓴다.

```text
planning_transport_connections =
  max(1,
      transport_connection_count,
      internally_known_capacity_connections_if_any,
      socket_type_bootstrap_connections)
```

이 규칙의 뜻은 아래와 같다.

- 실제 연결 수가 이미 있으면 그 값을 우선 사용한다.
- 서비스나 런타임이 이미 알고 있는 수용 연결 수가 있으면 그 값을 쓴다.
- 둘 다 없으면 소켓 타입별 bootstrap 기본값을 쓴다.

`STREAM`은 연결 수 변동이 크고 시작 시점 연결 수가 `0`인 경우가 흔하므로,
초기 계산용 bootstrap 기본값이 특히 중요하다.
이 초안은 `STREAM`의 내부 bootstrap 기본값으로 `5000` 연결을 시작점으로
가정하는 방향을 제안한다.

그 다음 transport 예산을 연결 수로 나누어 연결당 transport 몫을 계산한다.

```text
transport_bytes_per_connection =
  planning_transport_connections > 0
    ? floor(transport_budget_bytes / planning_transport_connections)
    : 0
```

이 몫은 송신과 수신으로 다시 나눌 수 있다.

```text
sndbuf_bytes =
  floor(transport_bytes_per_connection * send_transport_weight)

rcvbuf_bytes =
  floor(transport_bytes_per_connection * recv_transport_weight)
```

기본적으로는 send와 recv를 비슷하게 볼 수 있지만,
패턴에 따라 한쪽 비중이 더 커질 수 있다.

- `STREAM`은 연결 수가 많으므로 transport budget의 영향이 특히 크다.
- 큰 수신 burst를 받아야 하는 경우에는 `RCVBUF` 비중이 더 커질 수 있다.
- 송신이 더 자주 막히는 경우에는 `SNDBUF` 비중을 더 크게 잡을 수 있다.

다만 `SNDBUF` / `RCVBUF`는 OS 커널 소켓 버퍼라서 실제 반영량은 환경에 따라
차이가 날 수 있다.
따라서 이 값은 "정확한 실제 메모리"라기보다,
"이 정책이 목표로 하는 연결당 transport buffer 크기"로 보는 편이 맞다.

또한 버퍼는 구현과 OS에 따라 요청값과 실제 반영값이 다를 수 있다.
따라서 문서와 진단에서는 아래 둘을 구분해서 보여 주는 편이 맞다.

- requested `SNDBUF` / `RCVBUF`
  auto 정책이 계산한 목표값
- effective `SNDBUF` / `RCVBUF`
  실제 transport 소켓에 반영된 값

사용자는 requested 값으로 예산을 이해하고,
실제 동작 차이는 effective 값으로 확인하는 구조가 가장 덜 헷갈린다.

또한 현재 `STREAM` 가이드는 미설정 시 `SNDBUF` / `RCVBUF`를 각각 `262144`
바이트로 올리는 고정 기본값을 설명한다.
자동 total memory 정책이 켜진 경우에는 이 고정 기본값을 그대로 유지하면
총 메모리 예산과 쉽게 충돌한다.

따라서 이 초안은 아래 원칙을 제안한다.

- auto total memory 정책이 꺼져 있으면 기존 `STREAM` 고정 기본값을 유지한다.
- auto total memory 정책이 켜져 있으면 auto 계산값을 우선한다.
- 즉 auto 정책이 켜진 `STREAM`에서는 기존 고정 기본값을 그대로 강제하지 않고,
  total memory budget 안에서 계산한 값을 적용한다.

### 6.7 7단계: 송신과 수신을 따로 본다

`SNDHWM`과 `RCVHWM`은 같은 값으로 둘 수도 있지만, 자동 정책에서는 따로 계산할
수 있어야 한다.

그 이유는 아래와 같다.

- `fanout`은 송신 쪽 큐 부담이 더 중요하다.
- `recv_ingress`는 수신 쪽 순간 몰림을 버티는 능력이 더 중요하다.
- `routed`는 응답 지연과 요청 burst가 비대칭일 수 있다.

따라서 이 초안은 아래 두 축을 따로 계산하는 방향을 가정한다.

- send budget -> `SNDHWM`
- recv budget -> `RCVHWM`

정식 구현에서는 가능하면 아래 두 경로를 분리해 계산한다.

- send path: `send_effective_message_bytes`, send 쪽 active 연결 수
- recv path: `recv_effective_message_bytes`, recv 쪽 active 연결 수

### 6.8 8단계: 메모리를 예상 HWM 개수로 바꾸기

사용자는 총 메모리만 보면 실제로 HWM이 몇 개로 잡히는지 감이 잘 안 올 수 있다.
따라서 문서와 진단에는 평균 메시지 크기를 가정했을 때의 **예상 HWM 개수**도
함께 보여 주는 편이 좋다.

안내용 근사식은 아래와 같다.

```text
expected_hwm_per_connection_for_group =
  floor(group_budget_bytes / managed_connections / average_message_bytes)
```

이 식은 아래 가정을 둔 단순 안내식이다.

- 평균 메시지 크기를 알고 있다.
- 관리 대상 연결 수와 실제로 바쁜 연결 수가 크게 다르지 않다.
- 기본 보장 몫과 추가 몫을 나눈 내부 계산은 일단 단순화한다.

즉 이 값은 "정확한 최종 HWM"이라기보다,
"이 예산이면 connection 하나당 대략 몇 개 메시지까지 볼 수 있는가"를 빠르게
이해하기 위한 안내값이다.

예를 들어 평균 메시지 크기를 `1KB`로 보면:

- `98`이면 대략 `HWM 98`
- `393`이면 대략 `HWM 393`
- `15`이면 대략 `HWM 15`

이 안내값을 같이 보여 주면 사용자는 총 메모리, connection당 메모리,
예상 HWM 개수를 한 번에 연결해서 이해할 수 있다.

## 7. Context Total Memory Budget 가이드

### 7.1 왜 이 안내가 필요한가

자동 HWM 기능만 있으면 사용자는 곧바로 다른 질문을 하게 된다.

- "context 총 메모리를 몇 MB로 잡아야 하나"
- "프로세스에 context가 여러 개면 어떻게 나누나"
- "`spot` 인스턴스가 많으면 메모리 한도도 같이 늘어나야 하나"

따라서 이 초안은 기능과 함께 **운영 가이드**도 공개해야 한다고 본다.

### 7.2 기본 원칙

`context_total_memory_budget_mb`는 아래 순서로 정한다.

1. 프로세스 또는 컨테이너 전체 메모리 한도를 확인한다.
2. application working set, cache, GC, 기타 버퍼, 안전 여유를 먼저 뺀다.
3. 남은 몫 중 일부를 이 context의 총 메모리 예산으로 배정한다.
4. 여러 context가 있으면 실제 데이터를 많이 주고받는 context에 더 많이
   배정한다.

즉 이 값은 "전체 메모리 한도"가 아니라, **이 context에 허용할 총 메모리
상한**이다.

그 다음 내부에서는 이 총량을 아래처럼 다시 나눈다.

- `queue_budget`
- `transport_budget`
- `runtime_reserve`

### 7.2.1 사용자용 간단 계산법

사용자 입장에서는 내부 분배식을 전부 이해하기보다,
"연결 하나당 대략 얼마가 필요한가"를 먼저 보는 편이 쉽다.

이 초안은 아래 단위를 기준으로 안내하는 방향을 권장한다.

- `STREAM`: client 1개
- `SpotNode`: spot 1개
- 일반 소켓: 그 소켓에 연결되는 client 1개

그 다음 사용자는 아래 식으로 context 총 메모리 시작값을 잡을 수 있다.

```text
recommended_context_total_memory =
  expected_connections
  * recommended_total_memory_per_connection(avg_message_size, pattern)
```

평균 메시지 크기 보정까지 포함한 간단식은 아래처럼 쓸 수 있다.

```text
recommended_total_memory_per_connection =
  queue_memory_per_connection_at_1KB * (avg_message_size / 1KB)
  + transport_memory_per_connection_base
  + reserve_memory_per_connection_base
```

이 식은 사용자가 아래 세 값만 알면 시작값을 잡을 수 있게 한다.

- 예상 연결 수
- 평균 메시지 크기
- 패턴 종류

즉 사용자는 먼저 "한 개당 권장 메모리"를 보고,
그 뒤 예상 개수를 곱해 `context_total_memory_budget_mb`를 정하면 된다.

### 7.3 시작값 안내

이 초안은 사용자가 이해하기 쉬운 시작값 안내를 아래처럼 두는 방향을 제안한다.

| 사용 형태 | 권장 시작값 |
|---|---:|
| control 전용 context | 16MB ~ 64MB |
| 일반 RPC / routed 중심 context | 128MB |
| 일반 `spot` / `spotnode` context | 128MB ~ 256MB |
| fanout / pub-sub 중심 context | 256MB ~ 512MB |
| 대규모 stream / gateway context | 512MB 이상 |

이 표의 숫자는 절대값이 아니라 **첫 시작점**이다.
실제 값은 보내는 쪽이 막히는 비율, 평균 메시지 크기, 실제로 바쁜 peer 수를 보고
조정한다.

### 7.3.1 1개당 권장 total memory

아래 표는 평균 메시지 크기 `1KB`를 기준으로 한 **1개당 권장 시작값**이다.

| 사용 형태 | 기준 단위 | total | queue | transport | reserve |
|---|---|---:|---:|---:|---:|
| `ROUTER / DEALER` | client 1개 | 655.36 KiB | 393.22 KiB | 196.61 KiB | 65.54 KiB |
| `PUB / SUB` | subscriber 1개 | 1310.72 KiB | 786.43 KiB | 393.22 KiB | 131.07 KiB |
| `SpotNode` | spot 1개 | 1310.72 KiB | 786.43 KiB | 393.22 KiB | 131.07 KiB |
| `STREAM` | client 1개 | 104.86 KiB | 62.91 KiB | 31.46 KiB | 10.49 KiB |

이 표는 현재 문서의 기본 분배식과 시작값 표를 바탕으로 계산한 안내값이다.
따라서 사용자는 아래처럼 빠르게 시작값을 잡을 수 있다.

- `STREAM` client `5000`개, 평균 `1KB`
  `104.86 KiB * 5000` -> 약 `512 MiB`
- `SpotNode`에 spot `1000`개, 평균 `1KB`
  `1310.72 KiB * 1000` -> 약 `1280 MiB`

즉 이 절의 목적은 사용자가 먼저 `1개당 메모리`를 보고,
그 다음 예상 개수를 곱해 총 메모리를 정하게 하는 데 있다.

### 7.4 연결 수 기준 시작값 표

아래 표는 사용자가 `context_total_memory_budget_mb`를 잡을 때 참고할 수 있는
**운영 시작값**이다.

이 표는 아래 가정을 기준으로 한다.

- 평균 메시지 크기 `1KB`
- 기본 auto HWM 정책 사용
- 보통 수준의 순간 몰림
- 특별히 큰 연결별 HWM을 일부러 오래 유지하지 않음

즉 이 표는 정확한 계산식이 아니라, "처음 운영을 시작할 때 어느 정도에서
출발하면 되는가"를 보여 주는 안내표다.

#### 7.4.1 ROUTER / DEALER

이 표는 request/reply, directed send, 일반 routed 통신을 주로 쓰는 경우를
기준으로 한다.

이 경우는 모든 연결이 동시에 길게 밀리는 일이 상대적으로 적어서,
fanout 계열보다는 더 작게 시작해도 되는 편이다.

| 연결된 client 수 | 권장 `context_total_memory_budget_mb` 시작값 |
|---:|---:|
| 100 | 32 ~ 64 |
| 1,000 | 64 ~ 128 |
| 5,000 | 128 ~ 256 |
| 10,000 | 256 ~ 512 |

이 표는 "메모리를 넉넉히 잡아 두기"보다
"작게 시작한 뒤 필요한 경우 늘리기" 쪽에 더 가깝다.
즉 routed 계열은 가능한 한 작은 값에서 시작하고,
보내는 쪽이 자주 막히거나 응답 지연이 늘어날 때 조금씩 키우는 편이 안전하다.

#### 7.4.2 PUB / SUB

이 표는 publish fan-out, broadcast, 다수 subscriber 분배가 중심인 경우를
기준으로 한다.

이 경우는 느린 subscriber가 생기면 큐가 가장 빨리 커질 수 있으므로,
ROUTER / DEALER보다 더 큰 시작값을 잡는 편이 안전하다.
다만 시작값부터 과하게 크게 잡으면 queue 메모리가 빠르게 커질 수 있으므로,
아래 표도 "하한에서 시작하고 필요할 때 상한으로 올린다"는 뜻으로 읽는 편이
맞다.

| 연결된 client 수 | 권장 `context_total_memory_budget_mb` 시작값 |
|---:|---:|
| 100 | 64 ~ 128 |
| 1,000 | 128 ~ 256 |
| 5,000 | 256 ~ 512 |
| 10,000 | 512 ~ 1024 |

#### 7.4.3 SpotNode

`SpotNode`는 내부에 topic fan-out 경로와 routed 경로가 함께 있으므로,
보통 ROUTER / DEALER와 PUB / SUB의 중간이나 그보다 큰 값을 필요로 할 수 있다.

따라서 아래 표는 "SpotNode 1개에 붙어 있는 Spot 수"를 기준으로 한 일반적인
시작값이다.

| SpotNode 1개에 붙은 Spot 수 | 권장 `context_total_memory_budget_mb` 시작값 |
|---:|---:|
| 100 | 128 |
| 1,000 | 256 |
| 5,000 | 512 |
| 10,000 | 1024 |

다만 `SpotNode`는 통신 성격에 따라 아래처럼 읽는 편이 맞다.

- topic publish 비중이 크면 `PUB / SUB` 표에 더 가깝게 본다.
- direct request/reply 비중이 크면 `ROUTER / DEALER` 표에 더 가깝게 본다.
- 둘 다 많으면 두 표 중 큰 값을 기준으로 잡고, 거기에 25% 정도 여유를 두는
  편이 안전하다.

#### 7.4.4 평균 메시지 크기 보정

위 표는 평균 메시지 크기 `1KB`를 기준으로 한다.
메시지 크기가 다르면 아래처럼 비례 보정해서 보는 편이 쉽다.

```text
queue_budget_new =
  queue_budget_base * (평균 메시지 크기 / 1KB)

total_budget_new =
  queue_budget_new + transport_budget_base + runtime_reserve_base
```

즉 total memory 전체를 단순히 같은 비율로 키우는 것이 아니라,
먼저 queue 쪽만 메시지 크기에 비례 보정하고, transport와 reserve는 기본값을
별도로 유지한 뒤 다시 합치는 편이 이 초안의 총 메모리 모델과 더 잘 맞는다.

예를 들면 아래와 같다.

- 평균 메시지 `2KB` -> 표 값의 약 2배
- 평균 메시지 `4KB` -> 표 값의 약 4배
- 평균 메시지 `512B` -> 표 값의 약 절반

위 세 줄은 queue budget만 놓고 본 직관적 설명이다.
실제 total memory는 위 식처럼 `transport_budget_base`와
`runtime_reserve_base`를 더한 값으로 다시 계산해야 한다.

### 7.4.5 평균 1KB 기준 예상 HWM 개수

아래 표는 평균 메시지 크기 `1KB`, 관리 대상 연결 수와 실제로 바쁜 연결 수가
크게 다르지 않다는 가정에서, connection 하나당 대략 어느 정도 HWM이 나오는지
보여 주는 안내표다.

| 사용 형태 | control | routed | fanout | recv ingress |
|---|---:|---:|---:|---:|
| `ROUTER / DEALER` | 19 | 98 | 196 | 78 |
| `PUB / SUB` | 39 | 196 | 393 | 157 |
| `SpotNode` | 39 | 196 | 393 | 157 |
| `STREAM` | 3 | 15 | 31 | 12 |

이 표는 현재 문서의 기본 비율과 평균 `1KB` 가정으로 계산한 **안내용 근사값**이다.
실제 최종 HWM은 내부의 기본 보장 몫, 실제로 바쁜 연결 수, 역할별 비율 조정에
따라 달라질 수 있다.

#### 7.4.6 순간 몰림 보정

순간적으로 몰리는 양이 크면 같은 연결 수라도 더 큰 예산이 필요할 수 있다.

이 초안은 아래처럼 단순한 보정 규칙을 권장한다.

- 순간 몰림이 작다 -> 표 하한값 사용
- 순간 몰림이 보통이다 -> 표 중간값 사용
- 순간 몰림이 크다 -> 표 상한값 또는 표 값의 1.5배 ~ 2배 사용

예를 들어 subscriber 1000개 환경에서 publish burst가 크다면,
`128MB`보다 `256MB` 쪽에서 시작하는 편이 안전하다.

#### 7.4.7 표를 읽는 방법

위 표는 "바로 넉넉한 값부터 주자"는 뜻이 아니다.
총 메모리 예산은 크게 잡을수록 느린 peer를 오래 버틸 수 있지만, 그만큼
queue와 transport buffer 모두 더 큰 메모리를 쓰게 된다.

따라서 이 초안은 아래 순서를 권장한다.

1. 먼저 표의 작은 값 또는 하한값에서 시작한다.
2. 운영 중 메모리 사용량과 보내는 쪽이 막히는 비율을 본다.
3. 보내는 쪽이 너무 자주 막히면 예산을 올린다.
4. 메모리 사용량은 괜찮은데 지연만 커지면, 예산을 무작정 올리기보다 traffic
   특성과 burst 크기를 다시 본다.

즉 이 표의 목적은 "처음부터 큰 queue를 허용하기"보다
"현실적인 시작점에서 시작해, 필요한 만큼만 키우기"에 있다.

### 7.5 자동 기본값 초안

아무 설정도 주지 않았을 때는 아래 기본값을 쓸 수 있다.

```text
default_context_total_memory_budget =
  128MB
```

이 식의 의미는 아래와 같다.

- process memory limit 자동 탐지에 의존하지 않는다.
- 구현이 단순하고 예측 가능하다.
- 사용자가 아무것도 모르더라도 너무 작은 값에서 시작하지 않게 한다.

현재 설계에서는 process memory limit을 읽어 기본값을 계산하지 않는다.
명시 budget이 없으면 `128MB`를 고정 기본값으로 사용한다.

따라서 multi-context 상황에서는 아래 원칙을 따른다.

- 명시 budget이 필요한 context에는 사용자가 직접 `context_total_memory_budget_mb`
  를 준다.
- 고정 기본값 `128MB`는 "명시값이 전혀 없을 때의 안전한 시작점"으로만 본다.

### 7.6 여러 context가 있을 때

여러 context가 있다고 해서 메모리 한도를 균등 분배하면 안 된다.

이 초안은 아래 원칙을 권장한다.

- 메인 data-plane context: 전체 auto memory budget의 70% ~ 90%
- 보조 context: 나머지 10% ~ 30%

즉 context 수가 4개라고 해서 `25%`씩 나누는 방식은 권장하지 않는다.

### 7.7 perf multi 참고용 표

아래 표는 `bindings/c/perf/run_benchmarks_multi.sh` 기준으로
`perf multi`를 돌릴 때 참고할 수 있는 **초기 시작값**이다.

이 표는 실제 구현 규칙이 아니라, perf 측정을 시작할 때의 참고값이다.
즉 지금 단계에서는 "이 정도 예산에서 먼저 측정을 시작해 본다"는 뜻으로만
읽어야 한다.

정확한 값은 실제 perf 결과를 본 뒤 다시 조정해야 한다.
패턴별 처리량, 보내는 쪽이 막히는 비율, 실제 메모리 사용량이 확인되면
아래 수치는 다시 보정될 수 있다.

가정은 아래와 같다.

- `perf multi` 기본 패턴 사용
- 일반 패턴 client 수 `100`, `STREAM` client 수 `10000`
- 기본 메시지 크기
  일반 패턴: `64, 256, 1024, 65536, 131072, 262144`
  `STREAM`: `64, 256, 1024, 65536`
- `perf` 용도이므로 앞서 제시한 권장 시작값 범위에서 **큰 값**을 사용
- 아래 표는 Python으로 계산했다.
- 계산 단위는 `1 MiB = 1024 * 1024 bytes`다.
- 총 메모리 분배는 byte 단위 `floor` 규칙을 쓴다.
- 아래 표의 역할 묶음 budget은 **현재 문서의 전역 기본 비율**
  `control 5% / routed 25% / fanout 50% / recv_ingress 20%`를 그대로 적용한
  결과다.
- 즉 이 표는 "현재 초안의 기본 비율을 적용하면 이렇게 나온다"는 뜻이지,
  각 패턴의 최종 최적 비율을 확정했다는 뜻은 아니다.

계산식은 아래와 같다.

```text
total_bytes = context_total_memory_budget_mb * 1024 * 1024

queue_budget = floor(total_bytes * 0.60)
transport_budget = floor(total_bytes * 0.30)
runtime_reserve = total_bytes - queue_budget - transport_budget

control_budget = floor(queue_budget * 0.05)
routed_budget = floor(queue_budget * 0.25)
fanout_budget = floor(queue_budget * 0.50)
recv_ingress_budget =
  queue_budget - control_budget - routed_budget - fanout_budget

transport_bytes_per_connection =
  floor(transport_budget / clients)

sndbuf_per_connection =
  floor(transport_bytes_per_connection / 2)

rcvbuf_per_connection =
  transport_bytes_per_connection - sndbuf_per_connection
```

여기서 `SNDBUF` / `RCVBUF`는 send/recv를 50:50으로 나누는 가정을 썼다.
이 비율은 구현 단계에서 패턴별로 달라질 수 있지만, 이 절의 참고 표는 계산
기준을 명확히 하기 위해 동일 비율을 사용한다.

#### 7.7.1 패턴별 권장 total memory

| 패턴 | client 수 | 권장 `context_total_memory_budget_mb` |
|---|---:|---:|
| `DEALER_DEALER` | 100 | 64 |
| `DEALER_ROUTER` | 100 | 64 |
| `ROUTER_ROUTER` | 100 | 64 |
| `PUBSUB` | 100 | 128 |
| `SPOT` | 100 | 128 |
| `SPOT_REQREP` | 100 | 128 |
| `SPOT_SENDSEND` | 100 | 128 |
| `STREAM` | 10000 | 1024 |

#### 7.7.2 64 MiB total memory 적용 결과

대상 패턴:

- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`

가정:

- `clients = 100`
- `context_total_memory_budget = 64 MiB`

| 항목 | 예상 할당 메모리 |
|---|---:|
| total memory | 64.00 MiB |
| queue budget | 38.40 MiB |
| transport budget | 19.20 MiB |
| runtime reserve | 6.40 MiB |
| control budget | 1.92 MiB |
| routed budget | 9.60 MiB |
| fanout budget | 19.20 MiB |
| recv ingress budget | 7.68 MiB |
| transport per connection | 196.61 KiB |
| `SNDBUF` per connection | 98.30 KiB |
| `RCVBUF` per connection | 98.30 KiB |

위 표의 역할 묶음 budget은 routed 패턴 전용 튜닝값이 아니라,
현재 문서의 전역 기본 비율을 그대로 적용한 결과다.
정식 구현에서 routed 전용 비율을 따로 두게 되면 이 표도 다시 계산해야 한다.

#### 7.7.3 128 MiB total memory 적용 결과

대상 패턴:

- `PUBSUB`
- `SPOT`
- `SPOT_REQREP`
- `SPOT_SENDSEND`

가정:

- `clients = 100`
- `context_total_memory_budget = 128 MiB`

| 항목 | 예상 할당 메모리 |
|---|---:|
| total memory | 128.00 MiB |
| queue budget | 76.80 MiB |
| transport budget | 38.40 MiB |
| runtime reserve | 12.80 MiB |
| control budget | 3.84 MiB |
| routed budget | 19.20 MiB |
| fanout budget | 38.40 MiB |
| recv ingress budget | 15.36 MiB |
| transport per connection | 393.22 KiB |
| `SNDBUF` per connection | 196.61 KiB |
| `RCVBUF` per connection | 196.61 KiB |

위 표의 역할 묶음 budget도 현재 문서의 전역 기본 비율을 적용한 결과다.
`PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`가 실제로 같은 비율을 써야 한다는
뜻은 아니다.

#### 7.7.4 `STREAM` 1024 MiB total memory 적용 결과

대상 패턴:

- `STREAM`

가정:

- `clients = 10000`
- `context_total_memory_budget = 1024 MiB`

| 항목 | 예상 할당 메모리 |
|---|---:|
| total memory | 1024.00 MiB |
| queue budget | 614.40 MiB |
| transport budget | 307.20 MiB |
| runtime reserve | 102.40 MiB |
| control budget | 30.72 MiB |
| routed budget | 153.60 MiB |
| fanout budget | 307.20 MiB |
| recv ingress budget | 122.88 MiB |
| transport per connection | 31.46 KiB |
| `SNDBUF` per connection | 15.73 KiB |
| `RCVBUF` per connection | 15.73 KiB |

이 수치는 현재 STREAM 가이드의 고정 기본값 `256 KiB / 256 KiB`보다 훨씬 작다.
이 차이는 계산 오류가 아니라, auto total memory 정책에서는 기존 고정 기본값을
그대로 유지하지 않고 total memory budget 안에서 다시 계산한다는 가정을 썼기
때문이다.

다만 이 수치를 바로 "적절한 STREAM transport buffer"라고 단정할 수는 없다.
`STREAM`은 외부 raw client를 많이 붙이는 패턴이라 transport buffer가 너무 작으면
처리량, syscall 빈도, 커널 buffer 여유에 더 민감할 수 있기 때문이다.

즉 아래 두 경우를 구분해서 읽어야 한다.

- 기존 고정 기본값 모드
  `STREAM` 미설정 시 `SNDBUF = 256 KiB`, `RCVBUF = 256 KiB`
- auto total memory 모드
  total memory budget과 예상 연결 수를 기준으로 더 작은 값이 계산될 수 있음

따라서 이 표의 `15.73 KiB / 15.73 KiB`는 **계산식 그대로 넣으면 이렇게 나온다**는
참고값이지, perf 검증 없이 바로 기본 운영값으로 확정한다는 뜻은 아니다.

검증은 아래 기준으로 같이 봐야 한다.

1. 같은 client 수와 메시지 크기에서 기존 `256 KiB / 256 KiB` 대비 처리량이
   의미 있게 떨어지지 않는지
2. 보내는 쪽이 막히는 빈도와 재시도 빈도가 과하게 늘지 않는지
3. syscall 빈도와 CPU 사용량이 과하게 늘지 않는지
4. OS가 실제로 반영한 effective `SNDBUF` / `RCVBUF`가 requested 값과 얼마나
   차이 나는지

현재 설계에서는 이 표의 STREAM transport buffer 값을 **참고 계산 결과**로만 두고,
실제 기본값 전환 여부는 perf 검증 결과를 확인한 뒤 확정하는 것으로 본다.

#### 7.7.5 이 표를 읽는 방법

이 표는 "이 값이 정답이다"라는 뜻이 아니다.
다만 현재 초안의 계산식과 분배 비율을 그대로 적용했을 때,
perf 기본 client 수 기준으로 어느 정도 메모리가 항목별로 갈리는지를 명확하게
보여 주는 참고표다.

따라서 perf에서는 아래처럼 읽는 편이 맞다.

1. 먼저 이 표의 total memory로 측정을 시작한다.
2. 실제 메모리 사용량, 처리량, backpressure 빈도를 본다.
3. transport 쪽이 먼저 답답하면 total memory 또는 transport 비율을 다시 본다.
4. queue 쪽이 먼저 답답하면 total memory 또는 queue 비율을 다시 본다.

즉 이 표의 목적은 "실험을 시작할 기준"을 주는 것이지, 최종 운영값이나 최종
구현값을 확정하는 데 있지 않다.
실제 perf 결과가 쌓이면 이 표는 그 결과에 맞춰 다시 다듬어야 한다.

## 8. 사용자에게 보이는 설정 원칙

이 초안은 사용자가 알아야 하는 값을 가능한 한 줄이는 방향을 제안한다.

기본 원칙은 아래와 같다.

- 사용자는 `context_total_memory_budget_mb` 하나만 설정한다.
- 이 값의 뜻은 "이 context가 queue, transport buffer, reserve를 합쳐서 써도
  되는 최대 메모리"다.
- 시스템은 이 예산 안에서 내부 계산으로 HWM과 `SNDBUF` / `RCVBUF`를 자동으로
  정한다.

즉 사용자는 "HWM을 얼마로 둘지"를 고민하지 않고,
"이 context에 메모리를 얼마까지 허용할지"만 정하면 된다.

이 초안은 이 단순한 정책을 특히 중요하게 본다.
그 이유는 아래와 같다.

- raw 소켓도 HWM을 정확히 맞추기 어렵다.
- `spot` / `spotnode`는 내부 소켓 구조 때문에 사용자가 직접 맞추기 더 어렵다.
- 자동 기능을 만들고도 사용자가 역할별 비율, 하한값, 상한값까지 알아야 한다면
  실제 사용성은 크게 나아지지 않는다.

따라서 역할 묶음별 비율, 재계산 기준, 기본 보장 몫 계산식은
**내부 동작 설명에는 나오되, 기본 사용자 설정으로는 드러내지 않는 것**이
이 초안의 원칙이다.

## 9. `spot` / `spotnode` 적용 원칙

### 9.1 왜 별도 설명이 필요한가

`spot` / `spotnode`는 일반 소켓 하나와 달리 여러 내부 소켓이 함께 동작한다.
따라서 사용자가 "이 handle 하나에 HWM 얼마"라고 이해하면 실제 runtime 구조와
맞지 않는다.

현재 runtime 기준으로 보면 아래 같은 내부 소켓들이 함께 동작한다.

- `ctrl`
- `peer_ctrl_pub`
- `peer_ctrl_sub`
- `node_router`
- `route_ingress`
- `peer_route_ingress`
- `ingress`
- `fanout`
- `mesh_pub`
- `mesh_xsub`

따라서 자동 HWM은 이 내부 소켓들을 개별 숫자 나열로 설명하기보다,
역할별 묶음으로 설명해야 한다.

### 9.2 역할 묶음 나누기 초안

`spot` / `spotnode`에 대한 기본 역할 묶음 나누기 초안은 아래와 같다.

| 내부 소켓 | 역할 묶음 | 설명 |
|---|---|---|
| `ctrl`, `peer_ctrl_pub`, `peer_ctrl_sub` | `control` | 상태 전파와 제어 경로 |
| `node_router`, `route_ingress`, `peer_route_ingress` | `routed` | 지정 송신과 요청/응답 경로 |
| `fanout`, `mesh_pub` | `fanout` | publish fan-out과 peer 분배 |
| `ingress`, `mesh_xsub` | `recv_ingress` | publish / peer 수신 순간 몰림 흡수 |

이 매핑을 쓰면 사용자는 "내부 소켓이 많아서 모르겠다"가 아니라,
"이 context에서 fanout이 큰지 routed가 큰지"만 생각하면 된다.

### 9.2.1 현재 구현 메모

역할 묶음 자체는 현재 구현에도 그대로 반영한다. 다만 `spot` 내부 live socket에
generic auto-HWM policy를 각 소켓 단위로 그대로 켜 두는 방식은 현재 구현으로
채택하지 않는다.

이유는 `mesh_pub`, `fanout`, `mesh_xsub` 같은 data plane 내부 소켓에 generic
policy를 그대로 적용했을 때, publish fan-out 경로의 실제 live `SNDHWM` /
`RCVHWM` 값이 너무 작아져 `MULTI_SPOT` 작은 메시지 구간에서 처리량이 크게
떨어졌기 때문이다. 이 경우 문제는 계산 비용이 아니라, internal socket에 실제로
적용된 queue 크기가 너무 작아지는 데 있었다.

따라서 현재 구현은 아래처럼 나눈다.

- 역할 묶음과 역할별 budget 계산 규칙은 그대로 사용한다.
- `SpotNode` / `Spot` 공개 기본값 계산은 role-based auto-HWM 결과를 기준으로 한다.
- 하지만 `spot` runtime 내부 live socket에는 generic policy를 계속 켜 두지 않고,
  역할 계산 결과를 바탕으로 explicit HWM 값을 적용한다.
- live socket에 넣는 값도 `managed_connections`, `active_hwm_connections`,
  context memory budget을 함께 넣어 계산한다. 즉 internal socket이라고 해서 별도
  고정 큰 floor를 두지 않는다.

즉 이 초안의 역할 매핑은 유지하지만, `spot` internal socket의 적용 방식은
"generic policy를 socket마다 자동 적용"이 아니라 "role-based 계산 결과를 내부
runtime 규칙으로 명시 적용"으로 이해하는 편이 현재 코드와 맞다.

### 9.2.2 publish backpressure 처리 방향

`spot` 내부 publish 경로는 낮은 `HWM`에서 바로 전체 fan-out이 굳는 구조보다,
**논블로킹 송신과 bounded pending queue**를 조합하는 편이 맞다.

이 초안은 `mesh_pub`, `fanout` 같은 publish 경로에 대해 아래 원칙을 둔다.

- internal publish socket은 송신을 먼저 논블로킹으로 시도한다.
- 일시적인 `EAGAIN`은 fatal error가 아니라 **backpressure 상태**로 본다.
- backpressure가 걸린 logical message는 runtime 내부 pending queue에 넣는다.
- pending queue는 **peer별 전달 상태**를 따로 추적해야 한다. 느린 peer 하나가
  전체 relay를 막으면 안 되기 때문이다.
- writable 상태가 돌아온 peer에 대해서는 `POLLOUT` 기반으로 retry drain을
  수행한다.
- 다음 data-plane drain 시점은 보조 재시도 기회로만 쓰고, 주된 복구 신호는
  `POLLOUT`으로 본다.

이때 중요한 점은 queue가 단순한 무한 버퍼가 아니어야 한다는 것이다.

- queue는 **logical message 단위**로 저장해야 한다.
  frame 하나씩 따로 넣으면 multipart 경계와 전송 순서가 깨질 수 있다.
- queue는 **bounded** 여야 한다.
  pipe `HWM` 대신 runtime queue가 무한히 커지면 메모리 한도가 더 빨리 무너진다.
- queue가 가득 찼을 때의 정책을 분명히 둬야 한다.
  relay 내부에서 이미 받은 메시지는 버리지 않고, 이 경우 upstream backpressure를
  올리는 쪽이 맞다.

즉 핵심은 큰 고정 floor를 두는 것이 아니라, context budget과 연결 수 기반 HWM
계산 위에 pending queue와 retry 경로를 얹어 backpressure를 복구하는 데 있다.

### 9.2.3 context memory budget과 pending queue 관계

`ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`는 hard memory limit이라기보다
기본 `HWM` / transport buffer 크기를 계산하는 기준이다. 하지만 `spot` 내부에
pending queue를 두면, 이 queue도 결국 같은 context 메모리 예산 안에서 생각해야
한다.

따라서 `spot` internal publish queue는 아래 규칙을 따른다고 보는 편이 맞다.

- pending queue는 `queue_budget`의 일부를 쓰는 구조로 잡는다.
- role별 budget 중 `fanout` 묶음 예산이 이 queue와 pipe `HWM`을 함께 감당해야
  한다.
- pending queue가 커질수록 per-pipe `HWM`을 무한히 키우는 대신, retry와 drain
  기회를 더 자주 주는 방향이 낫다.
- pending queue 상한을 넘기면 relay 내부에서 drop하지 않고, ingress 쪽으로
  backpressure를 전파해야 한다.
- 사용자가 context memory budget을 줄이면 pending queue 허용치도 함께 줄어야
  한다.

즉 이 초안에서 context budget의 의미는 없어지지 않는다. 다만 `spot`에서는
"pipe `HWM` 값만으로 메모리를 설명한다"가 아니라, "pipe queue + internal pending
queue + transport buffer"를 함께 보는 쪽이 더 정확하다.

### 9.2.4 구현 체크포인트

`spot` internal publish backpressure를 정리할 때는 아래 항목을 함께 확인해야
한다.

1. `mesh_pub`, `fanout` 송신이 `EAGAIN`에서 즉시 fatal로 올라가지 않는가
2. retry queue가 multipart 원자성과 송신 순서를 보존하는가
3. `POLLOUT` 감시로 backpressure가 풀린 peer를 즉시 다시 drain하는가
4. 느린 peer 하나 때문에 전체 fan-out이 장시간 멈추지 않는가
5. context memory budget을 줄였을 때 pending queue와 live `HWM`이 함께
   작아지는가
6. 작은 메시지 다중 peer perf에서 큰 floor 없이도 처리량이 유지되는가

### 9.2.5 구현 순서와 완료 기준

이 절의 구현은 `local fanout`과 `remote mesh`를 따로 출시하는 단계형 작업으로
보지 않는다. 구현 순서는 둘 수 있지만, 완료 판정은 아래 두 경로가 모두 반영된
상태를 기준으로 한다.

구현 순서는 아래처럼 잡는다.

1. `local fanout` relay를 먼저 정리한다.
2. 이어서 `remote mesh` relay를 shared broadcast queue와 retry 경로까지 같은
   작업 안에서 끝까지 정리한다.

즉 구현 순서는 `local fanout -> remote mesh`로 둘 수 있지만, 이 초안의 완료
기준은 **느린 local subscriber가 전체 relay를 막지 않고, remote mesh도 shared
queue와 upstream backpressure로 fatal 없이 복구되는 상태**다.

그리고 이 완료 기준은 `core`만으로 닫지 않는다. `bindings/c/perf`도 같은 턴에
함께 정리되어야 한다.

- `perf`가 직접 만드는 `Spot` / `SpotNode` handle이나 raw socket에도 임의
  `SNDHWM` / `RCVHWM` 고정값을 남기지 않는다.
- `PERF_MULTI_HWM`, `PERF_MULTI_SNDHWM`, `PERF_MULTI_RCVHWM` 같은 benchmark 전용
  수동 HWM 주입은 기본 동작에서 빠져야 한다.
- bench는 `context budget + auto-HWM 계산`으로 돌고, 필요하면 budget만 바꾸는
  방향으로 본다.
- 즉 **core는 계산식, perf는 수동 HWM** 같은 반쪽 상태에서는 완료 판정을 하지
  않는다.

#### 9.2.5.1 local fanout relay

local subscriber 쪽은 이미 `spot_runtime_t::attachments`에 attachment id와 socket
handle이 따로 있다. 따라서 이 경로는 shared `fanout` broadcast send 대신,
attachment별 relay 상태를 두는 방식으로 구현한다.

이 경로의 구현 구조는 아래와 같다.

```cpp
struct spot_publish_pending_entry_t
{
    uint64_t message_id;
    std::string topic;
    spot_owned_msg_parts_t parts;
    size_t encoded_bytes;
    uint32_t remaining_targets;
};

struct spot_publish_local_target_t
{
    uint64_t attachment_id;
    socket_base_t *relay_socket;
    bool pollout_armed;
    std::deque<uint64_t> pending_message_ids;
};
```

핵심 규칙은 아래와 같다.

- `spot_data_plane_forwarding.cpp`, `spot_data_plane_mesh.cpp`는 local fanout이
  필요할 때 shared `fanout` socket에 바로 publish하지 않는다.
- 대신 현재 sub attachment snapshot을 읽고 target별 pending state를 갱신한다.
- topic filter 자체는 각 attachment 아래의 downstream `SUB`가 그대로 적용한다.
- target socket에 즉시 송신이 되면 queue에 넣지 않는다.
- `EAGAIN`이면 해당 target queue에 message id를 넣고 `POLLOUT`을 건다.
- message는 모든 local target이 완료될 때까지 한 번만 보관하고, target queue에는
  message id만 저장한다.

이 단계에서 가장 중요한 목표는 **느린 local subscriber 하나가 다른 local
subscriber 전달을 막지 않도록 하는 것**이다.

#### 9.2.5.2 remote mesh relay

remote peer 쪽은 local fanout과 달리 **shared mesh broadcast**가 기본 경로다.
`SpotNode`는 bind한 peer와 connect한 peer가 섞일 수 있기 때문에, publish hot
path를 peer별 direct sender에 의존시키면 bind/connect 비대칭 토폴로지에서 경로가
빠지거나, 반대로 peer 수만큼 복제 송신해 작은 메시지 perf가 크게 떨어진다.
따라서 remote publish 경로는 shared `mesh_pub`를 기본으로 유지하고, retry와
backpressure를 그 경로 위에서 정리해야 한다.

이 경로의 완료 기준은 아래와 같다.

- `mesh_pub` 송신의 `EAGAIN`을 fatal로 올리지 않는다.
- shared `mesh_pub` broadcast 경로에 bounded pending queue를 둔다.
- pending queue가 비어 있지 않으면 `mesh_pub`에 `POLLOUT`을 걸고, writable이
  돌아오면 queue head부터 다시 drain 한다.
- queue 상한은 `fanout` budget 안에서 계산하고, 상한을 넘으면 `mesh_xsub`
  drain을 pause 해서 upstream backpressure를 전파한다.
- routed request/reply나 지정 송신처럼 peer별 direct route가 필요한 경우는
  기존 `peer_route_ingress` / routed delivery 경로에서 따로 처리한다.

즉 remote mesh의 publish 경로는 **shared `mesh_pub` + bounded pending queue +
`POLLOUT` retry**로 마무리하고, peer별 direct route는 publish hot path가 아니라
routed delivery 전용 경로로 한정한다.

### 9.2.6 poller 변경 규칙

현재 data-plane poller는 `POLLIN`만 본다. 이 초안의 구현에서는 아래 변경이
필요하다.

- `socket_poller_t::modify()`를 사용해 target socket의 `POLLOUT` 관심을 동적으로
  켠다.
- target queue가 비어 있으면 `POLLOUT` 관심을 끈다.
- local target socket은 attachment add/remove 시 poller 등록/해제를 맞춘다.
- `mesh_pub` shared queue가 비어 있지 않으면 `mesh_pub`에도 `POLLOUT`을 건다.

즉 `POLLOUT`은 상시 감시가 아니라 **pending queue가 있는 대상에만 동적으로
거는 방식**으로 가야 한다.

### 9.2.7 upstream backpressure 전파 규칙

relay 내부 queue가 상한에 닿았을 때는 drop 대신 upstream을 막아야 한다. 이를
구현할 때는 아래 규칙을 둔다.

- local fanout pending bytes가 상한을 넘으면 `ingress` 읽기를 잠시 중단한다.
- remote mesh shared queue가 상한을 넘으면 `mesh_xsub` drain도 잠시 멈춘다.
- 재개 기준은 high watermark / low watermark 두 개를 둔 hysteresis 형태가
  적합하다.

예를 들면 아래처럼 둔다.

- queue bytes `>= pause_threshold` 이면 해당 `POLLIN` 경로를 끈다.
- queue bytes `<= resume_threshold` 이면 다시 `POLLIN`을 켠다.

이 방식이면 relay 내부에서 메시지를 버리지 않고도, upstream socket의 기존 HWM이
자연스럽게 backpressure를 전파하게 된다.

### 9.2.8 파일 단위 구현 범위

이 초안을 코드로 옮길 때 손댈 파일 범위는 아래 정도가 적절하다.

| 파일 | 변경 내용 |
|---|---|
| `core/src/services/spot/spot_runtime.hpp` | internal HWM 입력 snapshot helper 추가 |
| `core/src/services/spot/spot_data_plane_internal.hpp` | pending entry, local target state, relay byte counters 추가 |
| `core/src/services/spot/spot_data_plane_runtime.cpp` | poller 등록 시 `mesh_pub` / local target socket `POLLOUT` 관리 진입점 추가 |
| `core/src/services/spot/spot_data_plane_loop.cpp` | `POLLOUT` 처리 pass, pause/resume watermark 제어 추가 |
| `core/src/services/spot/spot_data_plane_forwarding.cpp` | ingress -> relay enqueue 경로 추가, `EAGAIN` fatal 제거 |
| `core/src/services/spot/spot_data_plane_mesh.cpp` | mesh_xsub -> local relay enqueue 경로 추가, `EAGAIN` fatal 제거 |
| `core/src/services/spot/spot_runtime_attachment.cpp` | attachment add/remove 시 local target poller 등록/정리, relay downstream HWM 정렬 |
| `bindings/c/perf/multi/common/perf_common_multi.hpp` | multi 기본 HWM 정책 제거, budget 중심 기본 동작으로 정리 |
| `bindings/c/perf/multi/common/perf_multi_runtime.hpp` | current relay 구조 기준 `ZLINK_MAX_SOCKETS` 기본 계산 보정 |
| `bindings/c/perf/multi/common/perf_multi_spot_control.hpp` | control plane용 수동 HWM 제거, auto-HWM 기준으로 정리 |
| `bindings/c/perf/multi/src/perf_multi_spot_client.cpp` | public spot handle에 수동 HWM 주입 제거 |
| `bindings/c/perf/multi/src/perf_multi_spot_server.cpp` | public spot handle에 수동 HWM 주입 제거 |

이 파일 범위는 구현 순서를 나누기 위한 목록이 아니라, 같은 작업 안에서 최종
구조까지 반영할 변경 집합이다.

### 9.3 idle runtime 처리

`spot` 인스턴스 수가 많더라도, 쉬고 있는 runtime까지 같은 비율로 메모리를
나누면 실제로 바쁘게 일하는 runtime이 너무 작은 HWM을 받게 된다.

따라서 이 초안은 아래 원칙을 제안한다.

- 쉬고 있는 runtime은 기본 보장 몫 수준만 유지한다.
- 최근 활동이 있는 runtime만 큰 메모리 분배 대상에 넣는다.
- 실제로 바쁜 peer가 적고 최근 송수신량이 거의 없는 runtime은 가중치를 낮춘다.

현재 확정 범위에서는 idle runtime 별도 판정을 넣지 않는다.
현재 설계는 생성 시점 1회 계산만 수행하므로, `spot` / `spotnode`도 현재 붙어 있는
연결 수를 기준으로만 계산한다.

아래 조건은 runtime 적응형 재계산을 추가할 때 idle 판정 기준으로 사용할 수 있다.

- 최근 `recalc_window_ms` 동안 send byte 증가가 없음
- 최근 `recalc_window_ms` 동안 recv byte 증가가 없음
- 최근 `recalc_window_ms` 동안 active peer 수 변화가 없음

즉 `spot` 인스턴스 수 자체는 자동 계산의 직접 기준이 아니라,
실제로 얼마나 바쁜지 판단할 때 참고하는 보조 지표에 가깝다.

## 10. 공개 설정 초안

이 초안은 사용자가 소켓별 HWM 대신 context 정책을 설정할 수 있어야 한다고
본다.

하지만 사용자가 직접 알아야 하는 값은 가능한 한 하나로 줄인다.

아래 이름은 초안이다.

```c
typedef enum zlink_ctx_option_t
{
    ZLINK_CTX_OPT_AUTO_HWM_ENABLE,
    ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB
} zlink_ctx_option_t;
```

### 10.1 이 기능을 위해 추가되는 C API 옵션

이 기능의 기본 경로에서 새로 추가되는 C API 옵션은 아래 두 개다.

| 옵션 | 소유자 | 타입 | 단위 | 기본값 | 의미 |
|---|---|---|---|---:|---|
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | context | `int` | boolean | `1` | auto total memory 정책 사용 여부 |
| `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` | context | `int` | MiB | 없음 | 이 context가 queue, transport, reserve를 합쳐서 써도 되는 총 메모리 |

이 절에서 중요한 점은 아래와 같다.

- 이 기능의 기본 public 설정은 **context 옵션**만 추가한다.
- 사용자는 소켓별 `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF`를 새로 배울 필요가 없다.
- 즉 auto 정책의 기본 입력은 `ENABLE`과 `TOTAL_MEMORY_BUDGET_MB` 두 개뿐이다.

권장 사용 형태는 아래와 같다.

```c
int enabled = 1;
int budget_mb = 512;

zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_ENABLE, enabled);
zlink_ctx_set(ctx, ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB, budget_mb);
```

### 10.2 이번 기능에 포함하지 않는 public 옵션

이 초안은 아래 항목을 **기본 public 옵션으로 추가하지 않는다**.

- 역할 묶음별 비율을 직접 주는 context 옵션
- `min_hwm`, `max_hwm` 같은 고정 숫자 옵션
- `SNDBUF`, `RCVBUF` 자동 비율을 직접 주는 옵션
- `spot` / `spotnode` 내부 소켓별 세부 옵션

이 항목들은 내부 계산 규칙이나 별도 고급 override 성격으로 보고,
기본 기능의 public C API에는 넣지 않는 방향을 전제로 한다.

### 10.3 기존 수동 옵션과의 관계

이번 기능이 추가되더라도 기존 수동 옵션은 그대로 남는다.

| 옵션 | owner | 계속 지원 여부 | 설명 |
|---|---|---|---|
| `ZLINK_OPT_SNDHWM` | socket | 유지 | raw 소켓 수동 override |
| `ZLINK_OPT_RCVHWM` | socket | 유지 | raw 소켓 수동 override |
| `ZLINK_OPT_SNDBUF` | socket | 유지 | raw 소켓 수동 override |
| `ZLINK_OPT_RCVBUF` | socket | 유지 | raw 소켓 수동 override |

즉 새 기능은 "수동 옵션을 없애는 것"이 아니라,
"수동 옵션을 주지 않았을 때 context 총 메모리 기준으로 자동 계산하는 기본 정책을
추가하는 것"으로 본다.

이 초안은 아래 규칙을 가정한다.

- 기본은 `AUTO_HWM_ENABLE = 1`
- 사용자가 설정하는 핵심 값은 `AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` 하나다.
- 시스템은 이 값을 바탕으로 내부 계산으로 HWM과 transport buffer를 자동으로
  정한다.
- 사용자는 역할 묶음별 비율, 기본 보장 몫, 재계산 기준을 직접 설정하지 않는다.

예외는 아래 두 가지다.

- raw 기본 소켓은 기존처럼 `SNDHWM` / `RCVHWM` 직접 설정을 허용한다.
- raw 기본 소켓은 기존처럼 `SNDBUF` / `RCVBUF` 직접 설정도 허용한다.
- `spot` / `spotnode`는 내부 소켓별 설정은 공개하지 않고,
  정말 필요한 경우에만 의미 단위 일괄 override를 허용할 수 있다.

여기서 일괄 override란 아래 같은 수준을 뜻한다.

- topic send 쪽 전체 HWM override
- topic recv 쪽 전체 HWM override
- routed send 쪽 전체 HWM override
- routed recv 쪽 전체 HWM override

즉 `spot` / `spotnode` 사용자가 내부 소켓 이름을 알고 각각 값을 넣게 만들지는
않는다.

## 11. runtime 재계산 확장 방향

자동 정책이더라도 매 메시지마다 HWM을 다시 계산하면 안 된다.
계산 비용이 커지고 값이 너무 자주 흔들리기 때문이다.

현재 확정 범위는 생성 시점 또는 정책이 처음 적용되는 시점에 **1회 계산**만
수행한다.

즉 현재 설계의 기준은 아래와 같다.

- context budget을 읽고 1회 계산한다.
- 계산 결과를 초기 소켓 정책값과 새 pipe 생성 시점 기본값으로 사용한다.
- 런타임 중 active peer 수, 메시지 크기, 보내는 쪽이 막히는 비율을 다시 모아
  자동 재계산하지 않는다.

아래 내용은 runtime 적응형 정책을 추가할 때의 확장 방향이다.

runtime 재계산을 도입한다면 아래 조건을 재계산 기준으로 사용할 수 있다.

- active peer 수가 이전 관측 구간 대비 25% 이상 변했다.
- `effective_message_bytes`가 이전 관측 구간 대비 25% 이상 변했다.
- 최근 관측 구간의 보내는 쪽이 막히는 비율이 임계치를 넘었다.
- 쉬고 있던 runtime이 다시 바쁜 상태로 넘어갔다.
- 실제로 보낼 수 있는 연결 집합이 크게 변했다.

여기서 관측 구간을 명시하지 않으면 구현마다 해석이 달라질 수 있다.
따라서 이 초안은 아래 개념을 함께 두는 방향을 제안한다.

```text
recalc_window_ms
```

이 값은 active peer 수, 메시지 크기 변화, 보내는 쪽이 막히는 비율을 어떤 시간
구간으로 집계할지 정한다.

초기 기본값으로는 1000ms ~ 5000ms 정도의 짧은 관측 구간을 가정한다.
너무 짧으면 값이 흔들리고, 너무 길면 반응이 늦어진다.

또한 아래 보호 장치를 둔다.

- `recalc_cooldown_ms` 동안은 다시 계산하지 않는다.
- 새 HWM이 기존값과 충분히 다를 때만 적용한다.
- 재계산 결과가 예산 계산과 기본 보장 몫 규칙을 벗어나지 못하게 한다.

runtime 재계산을 도입한다면 반영 규칙은 아래처럼 둔다.

- `SNDHWM` / `RCVHWM`:
  기존 pipe와 새 pipe 모두에 적용
- `SNDBUF` / `RCVBUF`:
  새 transport connection에만 적용
  기존 live connection은 reconnect 전까지 이전 값을 유지

## 12. 측정 오버헤드

자동 HWM을 구현할 때 자주 나오는 질문은 "이 기능이 성능을 많이 깎지 않느냐"는
점이다.

이 초안의 답은 아래와 같다.

- 계산 자체의 비용은 크지 않다.
- 실제 부담은 통계를 어떻게 모으느냐에 따라 달라진다.
- 따라서 **매 메시지마다 무거운 계산을 하지 않는 방식**으로 구현해야 한다.

즉 오버헤드는 크게 세 부분으로 나누어 봐야 한다.

### 12.1 계산 오버헤드

아래 계산 자체는 매우 무거운 편이 아니다.

- context 총 메모리 한도 읽기
- queue / transport / reserve 내부 분배
- 역할 묶음별 비율 적용
- 평균 메시지 크기 반영
- 활성 연결 수로 나누기
- 기본 보장 몫과 추가 몫 합치기

문제는 이 계산을 **매 송신, 매 수신마다 하면 안 된다**는 점이다.
자동 HWM 계산은 hot path가 아니라, 일정 시점에 다시 계산하는 관리 경로에 있어야
한다.

### 12.2 통계 수집 오버헤드

실제로 더 조심해야 하는 쪽은 통계 수집이다.

아래 일을 hot path에서 무겁게 하면 오버헤드가 커질 수 있다.

- 매 메시지마다 정밀한 백분위 계산
- 연결별 상세 HWM 사용량 추적
- 큰 자료구조 조회
- 잠금이 필요한 전역 통계 업데이트

따라서 이 초안은 아래 원칙을 권장한다.

- hot path에서는 단순 카운터나 가벼운 합계만 모은다.
- 정밀한 p95 계산보다 EWMA나 샘플링 기반 추정을 우선한다.
- 연결별 상세 통계보다 역할 묶음 단위 집계를 우선한다.
- 재계산은 별도 관리 경로에서 하거나, 변화가 클 때만 수행한다.

즉 자동 HWM은 "정확하지만 무거운 통계"보다
"조금 거칠어도 충분히 싼 통계"를 쓰는 편이 맞다.

### 12.3 적용 오버헤드

새 HWM 값을 계산한 뒤 실제 소켓과 연결에 반영할 때도 비용이 있다.

특히 연결 수가 매우 많을 때는 아래 문제가 생길 수 있다.

- HWM 값을 너무 자주 바꾸면 붙어 있는 pipe에 자주 전파해야 한다.
- 값이 자주 흔들리면 관리 경로가 불필요하게 바빠진다.

따라서 이 초안은 아래 같은 방어 규칙을 함께 둔다.

- 일정 시간 동안은 다시 계산하지 않는 cooldown
- 새 값이 기존값과 충분히 다를 때만 반영
- 연결 수가 매우 많을 때는 반영 빈도를 더 낮추는 정책

### 12.4 구현 시 지켜야 할 원칙

측정 오버헤드를 작게 유지하려면 아래 원칙을 지켜야 한다.

- HWM 계산은 매 메시지마다 하지 않는다.
- hot path에서는 무거운 잠금과 복잡한 자료구조 접근을 피한다.
- 메시지 크기 통계는 EWMA나 샘플링처럼 싼 방식으로 모은다.
- 역할 묶음 단위 집계를 우선하고, 연결별 정밀 추적은 최소화한다.
- 값이 충분히 달라질 때만 실제 HWM을 다시 적용한다.

즉 자동 HWM은 구현 방식만 조심하면 충분히 실용적인 오버헤드 안에서 넣을 수
있다. 반대로 통계를 욕심내서 너무 정교하게 모으면, HWM 계산보다 통계 수집이 더
비싸질 수 있다.

## 13. 관찰과 진단

자동 HWM이 켜져 있으면 사용자가 "왜 지금 이 값이 적용됐는지"를 볼 수 있어야
한다.

이 초안은 최소한 아래 항목을 볼 수 있게 해야 한다고 본다.

- context total memory budget
- queue / transport / reserve 분배 결과
- 역할 묶음별 budget
- 최근 `effective_message_bytes`
- 역할 묶음별 active connection 수
- 현재 적용된 `SNDHWM` / `RCVHWM`
- requested `SNDBUF` / `RCVBUF`
- effective `SNDBUF` / `RCVBUF`
- 예산 기준 예상 최대 메모리
- 평균 메시지 크기 기준 예상 HWM 개수
- 최근 HWM 재계산 시각
- 최근 재계산 사유
- 보내는 쪽이 막히는 비율

이 정보가 없으면 자동 정책이 실제로는 동작하더라도, 사용자는 그 이유를 이해하기
어렵다.

현재 설계에서는 이 정보를 **별도 context 옵션이 아니라 monitoring snapshot 경로**로
노출하는 것을 기준으로 한다.
즉 이 기능 자체를 위해 새 setter/getter 성격의 public C API를 더 추가하지는
않는다.

## 14. 안전 장치

자동 정책은 편리해야 하지만, 예측 불가능하면 안 된다.
이 초안은 아래 안전 장치를 기본으로 제안한다.

- 계산 결과가 음수가 되지 않음
- 계산 결과가 예산으로 허용되는 메시지 수를 넘지 않음
- budget이 아주 작더라도 control 묶음은 너무 작은 값으로 무너지지 않음
- budget이 아주 크더라도 fanout 묶음이 과도한 큐를 허용하지 않음
- 메시지 크기 통계가 없을 때는 보수적인 기본 크기를 사용함

또한 아래 기본 대체 규칙을 둔다.

- 통계가 아직 충분하지 않으면 보수적인 기본 메시지 크기와 기본 비율을 사용
- 명시 budget이 없으면 고정 시작 예산 `128MB`를 사용
- active connection 추정이 어렵다면 보수적으로 전체 active peer 수를 사용

## 15. 기본 동작 전환

이 초안은 호환성 유지를 전제로 하지 않는다.
즉 구현 시점부터 auto total memory 정책을 기본 동작으로 바로 반영하는 방향을
가정한다.

따라서 사용자가 명시적으로 HWM이나 buffer 값을 주지 않았을 때 관찰되는 기본
동작은 이전과 달라질 수 있다.

예를 들면 아래 변화가 생길 수 있다.

- 작은 시스템에서는 기존 고정 기본값 `1000`보다 훨씬 작은 HWM이 적용될 수 있다.
- 큰 fanout 사용 형태에서는 연결 수 증가에 따라 연결 하나당 HWM이 자동으로
  낮아질 수 있다.
- `STREAM`에서는 기존 고정 `SNDBUF` / `RCVBUF` 기본값보다 작은 값이 적용될 수
  있다.
- 쉬고 있는 peer가 많고 실제로 바쁜 peer가 적은 경우, 바쁜 경로에 더 큰 값이
  돌아갈 수 있다.

이 변화는 의도된 것이다.
목표는 고정된 숫자 `1000`이나 고정 buffer 기본값을 어디서나 유지하는 것이
아니라, context 전체 메모리 상한 안에서 더 예측 가능한 큐와 transport 동작을
얻는 데 있다.

즉 이 초안이 구현되면 아래 원칙으로 바로 전환한다.

- 수동 설정이 없으면 auto total memory 정책을 기본 적용
- 기존 고정 HWM 기본값에 대한 호환성 보장 없음
- 기존 고정 `STREAM` buffer 기본값에 대한 호환성 보장 없음
- 문서, 공개 헤더, 테스트를 같은 턴에 함께 갱신

## 16. 비목표

이 초안은 아래를 목표로 하지 않는다.

- 모든 사용 형태에 완전히 최적의 HWM을 찾는 것
- 응용 계층의 대기열을 완전히 없애는 것
- message loss, timeout, retry 문제를 자동 HWM 하나로 해결하는 것
- 내부 소켓 구조를 숨기기 위해 진단 정보까지 모두 감추는 것

자동 HWM은 큐 상한을 더 합리적으로 정하는 기본 정책일 뿐이다.

## 17. 구현 순서 메모

구현은 아래 순서가 현실적이다.

1. context에 `total memory budget` 옵션 추가
2. 내부 `queue / transport / reserve` 분배 추가
3. 일반 소켓용 자동 HWM 계산 경로 추가
4. 일반 소켓용 자동 `SNDBUF` / `RCVBUF` 계산 경로 추가
5. `spot` / `spotnode` 내부 소켓을 역할 묶음에 매핑
6. 생성 시점 계산만 먼저 적용
7. raw 소켓 수동 override와 `spot` / `spotnode` 일괄 override 정리
8. 마지막으로 runtime 재계산과 관찰 정보 확장 여부를 정리

구현 순서는 이렇게 잡을 수 있지만, 완료 판정은 중간 단계가 아니라 이 초안에서
현재 범위로 확정한 항목이 모두 반영된 상태를 기준으로 한다.

## 18. 문서 반영 범위

이 초안이 구현되면 아래 문서들을 함께 정리해야 한다.

### 18.1 `doc/guide/`

사용자 문서에는 "왜 이 기능이 필요한가", "어떻게 설정하는가", "어떻게 읽는가"를
반영해야 한다.

- `doc/guide/10-performance.ko.md`
- `doc/guide/10-performance.md`
  auto total memory 정책, 패턴별 시작값, `perf multi` 참고표, 평균 메시지 크기
  보정 규칙 추가
- `doc/guide/12-socket-options.ko.md`
- `doc/guide/12-socket-options.md`
  `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF`, `STREAM` 기본 정책이 auto total memory
  기준으로 어떻게 바뀌는지 반영
- `doc/guide/03-5-stream.ko.md`
- `doc/guide/03-5-stream.md`
  `STREAM`의 internal bootstrap connections, auto total memory, requested/effective
  `SNDBUF` / `RCVBUF` 설명 추가

### 18.2 `doc/internals/`

내부 문서에는 실제 분배식, 역할 묶음, 소켓 매핑, runtime 계산 경로를 반영해야
한다.

- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`
  고정 기본값 중심 설명을 auto total memory 중심 설명으로 갱신
- `doc/internals/services-internals.ko.md`
- `doc/internals/services-internals.md`
  서비스 계열 handle이 context 예산에서 어떻게 메모리를 나누는지 반영
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
  `spot` / `spotnode` 내부 소켓의 역할 묶음, budget 분배, idle runtime 처리 반영
- `doc/internals/stream-socket.ko.md`가 있다면 함께 반영
- `doc/internals/stream-socket.md`가 있다면 함께 반영

### 18.3 `doc/spec/`

공개 계약 문서에는 새 context 옵션, 기본 동작 전환, 진단 노출 항목을 반영해야
한다.

- `doc/spec/core/context.ko.md`
- `doc/spec/core/context.md`
  `AUTO_HWM_ENABLE`, `AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` 같은 context 옵션 계약 추가
- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/monitoring.md`
  requested/effective `SNDBUF` / `RCVBUF`, 예상 HWM 개수, budget 분배 결과 노출
  항목 추가
- `doc/spec/core/socket/stream.ko.md`
- `doc/spec/core/socket/stream.md`
  `STREAM` auto total memory와 internal bootstrap connection 기본값 관련 계약 추가
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/dealer.ko.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/socket/pub.ko.md`
- `doc/spec/core/socket/pub.md`
- `doc/spec/core/socket/sub.ko.md`
- `doc/spec/core/socket/sub.md`
  각 소켓이 auto 정책의 영향을 어떻게 받는지 정리
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
  `spot` / `spotnode` 일괄 override와 자동 정책 계약 반영

필요하면 아래 문서도 함께 정리한다.

- `doc/spec/core/errors.ko.md`
- `doc/spec/core/errors.md`
- `doc/spec/core/errno-map.ko.md`
- `doc/spec/core/errno-map.md`
  자동 계산 경로에서 새로 생기는 설정 오류나 진단 오류가 있다면 반영

### 18.4 `doc/spec/bindings/`

언어별 바인딩 문서에는 context 옵션 API와 기본 동작 전환을 반영해야 한다.

- `doc/spec/bindings/README.md`
  공통 auto total memory 개념과 binding 공통 주의사항 추가
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`
  각 언어에서 context total memory 옵션을 어떻게 설정하는지, raw socket 수동
  override는 어떻게 주는지, `STREAM`은 별도 public expected connections 옵션이
  없고 internal bootstrap 기본값을 쓴다는 점을 반영

### 18.5 반영 순서

문서 반영은 아래 순서로 진행하는 편이 맞다.

1. `doc/spec/draft/auto-hwm.ko.md` 확정
2. 공개 헤더와 테스트 반영
3. `doc/spec/core/` 계약 문서 반영
4. `doc/spec/bindings/` 언어별 문서 반영
5. `doc/guide/` 사용자 가이드 반영
6. `doc/internals/` 내부 구조 문서 반영

## 19. 구현 확정안

이 초안은 구현 시작 기준으로 아래 항목을 확정한다.

- 새 public context 옵션 이름은 `ZLINK_CTX_OPT_AUTO_HWM_ENABLE`,
  `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`로 확정한다.
- 현재 설계에서는 process memory limit을 읽어 기본값을 계산하지 않는다.
  사용자가 budget을 주지 않으면 `128MB`를 고정 기본값으로 사용한다.
- `effective_message_bytes`의 초기값은 `1280` 바이트로 둔다.
  계산에 쓰는 내부 기본값은 `recent_ewma_message_bytes = 1024`,
  `recent_p95_message_bytes = 1024`, `overhead_factor = 1.25`다.
- `floor_function()`의 현재 시작값은 `control = 4`,
  `routed = 8/4/2`, `fanout = 16/8/4/1`, `recv_ingress = 8/4/2` 규칙을 쓴다.
  각 숫자는 `managed_connections` 구간에 따라 적용하며, 본문 5.4 절의 표를
  기준으로 구현한다.
- 역할 묶음 비율은 모든 context에 공통 기본값
  `control 5% / routed 25% / fanout 50% / recv_ingress 20%`를 사용한다.
- `spot` internal socket은 역할 묶음 계산을 그대로 참고하되, live socket에는
  generic auto-HWM policy를 계속 켜 두지 않는다. 대신 context budget,
  `managed_connections`, `active_hwm_connections`를 넣어 계산한 값을 explicit하게
  적용한다.
- `spot` publish 경로는 큰 internal floor 유지가 아니라, `mesh_pub` / `fanout`에
  대한 논블로킹 송신, bounded pending queue, retry drain 경로를 두는 쪽으로
  확정한다. `EAGAIN`은 fatal error가 아니라 일시적 backpressure로 해석한다.
- `spot` internal pending queue는 logical message 단위로 저장하고, role별
  `fanout` budget 안에서 상한을 계산한다. queue overflow가 나면 relay 내부에서
  drop하지 않고 ingress 쪽으로 upstream backpressure를 전파한다.
- `spot` publish retry는 `POLLOUT` 감시를 기본 복구 신호로 삼는다. local
  fanout은 attachment별 전달 상태를 분리하고, remote mesh는 shared `mesh_pub`
  queue를 기준으로 retry한다.
- remote `mesh_pub`는 shared broadcast 경로를 기본으로 유지하되, 이 경로도
  bounded pending queue와 `POLLOUT` retry를 가져야 한다.
- `bindings/c/perf`는 benchmark 편의용 수동 HWM 기본값을 남기지 않는다. perf가
  `Spot` / `SpotNode` public handle에 직접 `SNDHWM` / `RCVHWM`를 넣는 기본 경로는
  제거하고, 기본 동작은 context auto-HWM과 budget 계산을 그대로 타야 한다.
- perf에서 조정 가능한 기본 입력은 `PERF_CTX_AUTO_HWM_ENABLE`,
  `PERF_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` 같은 context budget 계열로 본다.
  HWM 숫자를 직접 주입하는 방식은 디버그용 예외 경로로만 남기거나 제거한다.
- peer별 direct route는 publish hot path가 아니라 routed request/reply,
  지정 송신, `peer_route_ingress` 같은 routed delivery 경로로 한정한다.
- relay queue 상한 초과 시 내부 drop은 두지 않고, `ingress` / `mesh_xsub`
  `POLLIN`을 pause 하여 upstream backpressure를 전파한다.
- `spot` idle runtime 판정은 현재 확정 범위에 넣지 않는다.
  최근 `recalc_window_ms` 구간의 send byte, recv byte, active peer 수 변화를
  기준으로 하는 판정은 runtime 재계산 확장 규칙으로 둔다.
- 재계산 결과 반영 규칙은 `HWM`은 기존 pipe와 새 pipe 모두에 적용하고,
  `SNDBUF` / `RCVBUF`는 새 transport 연결부터 적용하며 기존 연결은 재연결 전까지
  이전 값을 유지하는 것으로 확정한다.
- 진단 정보는 새 public setter/getter 옵션을 추가하지 않고, monitoring
  snapshot 경로로 노출하는 것으로 확정한다.
- 현재 확정 범위는 생성 시점 1회 계산으로 한정한다.
  active peer 수, 메시지 크기, 보내는 쪽이 막히는 비율을 이용한 runtime 재계산은
  확장 규칙으로 둔다.
- `STREAM` transport buffer의 자동 계산값은 perf 참고 계산 결과로 먼저 두고,
  기존 `256 KiB / 256 KiB` 대비 처리량, 보내는 쪽이 막히는 빈도, syscall 빈도,
  effective buffer 반영값을 확인한 뒤 기본 전환 여부를 확정한다.
