[스펙 목차](../README.ko.md)

# Draft -- Auto HWM Recalculation Policy

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API, 상수, 기본 동작을
> 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로 나누어
> 반영한다.

## 1. 목적

이 초안은 auto HWM을 **언제 다시 계산할지**에 대한 정책을 정리한다.

지금 필요한 것은 계산식 하나를 더 정교하게 만드는 일보다, 아래 두 문제를 함께
풀 수 있는 운영 규칙을 세우는 일이다.

- 연결이 붙고 떨어질 때마다 HWM을 완전히 고정된 값으로 두면, 실제 연결 수가
  크게 바뀌는 상황을 반영하기 어렵다.
- 반대로 연결 변화가 있을 때마다 즉시 전체를 다시 계산하면, 시작 구간이나
  대량 접속 구간에서 값이 너무 자주 바뀌고 준비 시점도 흔들릴 수 있다.

이 초안의 목표는 아래와 같다.

- 기본 동작은 자동으로 유지한다.
- 연결 변화가 많을 때 계산이 과도하게 반복되지 않게 한다.
- application이 매번 "언제 calculate를 불러야 하나"를 고민하지 않게 한다.
- `spot`, `spotnode`, `stream`처럼 동적으로 내부 구성이 바뀌는 경우도 같은 큰
  규칙으로 설명할 수 있게 한다.

## 2. 결론

이 초안이 제안하는 기본 정책은 아래와 같다.

1. auto HWM은 **연결 변화에 반응해서 자동 재계산**한다.
2. 다만 연결이 바뀔 때마다 즉시 계산하지 않고, **마지막 변화 이후 일정 시간
   기다렸다가 한 번만 계산**한다.
3. 이 대기 시간은 기본값을 `3000ms`로 둔다.
4. 대기 중에 새 연결 변화가 오면 타이머를 다시 미룬다.
5. 사용자가 직접 수동 HWM이나 buffer를 설정한 항목은 자동 계산이 덮어쓰지
   않는다.
6. 즉시 적용이 필요하면 `zlink_ctx_auto_hwm_recalculate(ctx)`를 호출해 예약을
   무시하고 바로 다시 계산할 수 있다.

즉 기본 모델은 아래와 같다.

```text
connection changes keep coming
-> schedule recalculation
-> wait for quiet period
-> recalculate once
-> apply once
```

이 문서는 이 방식을 "마지막 변화 기준 지연 재계산"이라고 부른다.

## 3. 왜 수동 calculate 기본 모델을 쓰지 않는가

처음에는 아래 같은 모델도 검토할 수 있다.

- context에 총 메모리만 설정한다.
- 처음에는 기본 HWM을 쓴다.
- application이 적절한 시점에 `calculate()`를 한 번 호출한다.

이 방식은 구현은 단순하다. 하지만 실제 application 입장에서는 호출 시점을
정하기가 애매하다.

예를 들면 아래 질문이 바로 생긴다.

- server가 bind만 끝나면 호출해야 하는가
- client들이 충분히 붙은 뒤에 호출해야 하는가
- `spot`이 더 생길 수 있는데 언제가 "완료된 시점"인가
- scale out 뒤에는 누가 다시 호출해야 하는가

이 초안은 이런 책임을 application에 넘기지 않는 편이 낫다고 본다.
auto HWM은 기본적으로 library가 스스로 맞추되, **너무 자주 재계산하지 않게**
만드는 편이 더 실용적이다.

## 4. 기본 동작

### 4.1 재계산 예약

아래 변화가 생기면 해당 context는 auto HWM 재계산이 필요하다고 본다.

- 연결 생성
- 연결 종료
- `spot` attachment 생성
- `spot` attachment 종료
- `spotnode` 내부 소켓 구성이 실제로 바뀌는 변화
- auto HWM 관련 context 옵션 변경
- auto HWM 관련 socket 옵션 변경

이때 바로 전체 재계산을 하지 않고, 먼저 context에 **재계산 예약**만 건다.

여기서 "auto HWM 관련 option"은 아래를 뜻한다.

- context total memory budget
- recalc debounce
- stream bootstrap
- spot bootstrap
- socket msg unit
- socket auto HWM enable/disable
- manual `SNDBUF` / `RCVBUF` override 상태
- manual `SNDHWM` / `RCVHWM` override 상태

반대로 아래 변화는 기본적으로 재계산 예약 사유로 보지 않는다.

- monitor socket 생성과 종료
- service monitor bridge 같은 내부 보조 socket 생성과 종료
- auto HWM과 무관한 일반 socket option 변경

### 4.2 지연 재계산

context에 재계산을 예약할 때는 아래 정보를 기록한다.

- 마지막 변화 시각
- 재계산 예약 시각

기본 규칙은 아래와 같다.

```text
recalc_deadline = last_change_time + debounce_ms
```

기본 `debounce_ms`는 `3000ms`를 제안한다.

옵션 의미는 아래처럼 고정하는 방향을 권장한다.

| 값 | 의미 |
|---|---|
| `0` | 예약 없이 바로 재계산 |
| 양수 | 마지막 변화 후 해당 시간만큼 기다린 뒤 재계산 |

이 시간이 지나기 전에 새 변화가 들어오면, 기존 예약은 취소하고 다시
`last_change_time + 3000ms`로 미룬다.

즉 접속 폭주 구간에서는 계산을 계속 미루고, 연결 상태가 잠잠해졌을 때 한 번만
계산한다.

구현은 context당 타이머 하나만 두는 방향이 맞다.
socket마다 따로 타이머를 두면 연결 수가 많을 때 불필요하게 복잡해진다.

### 4.3 계산 단위

재계산 단위는 **socket 하나씩**이 아니라 **context 전체**로 둔다.

이유는 아래와 같다.

- total memory budget은 context 단위 설정이다.
- 같은 context 안 소켓들은 결국 같은 메모리 예산을 나눠 쓴다.
- socket마다 제각각 따로 계산하면 같은 context 안에서도 기준 시점이 달라져
  해석이 더 어려워진다.

따라서 연결 이벤트는 개별 socket에서 발생해도, 실제 계산은 context 단위로
모아서 한 번 수행한다.

### 4.4 내부 상태

이 초안은 context가 아래 상태를 가지는 방향을 권장한다.

| 필드 | 의미 |
|---|---|
| `recalc_pending` | 예약된 재계산이 남아 있는지 |
| `last_change_time_ms` | 마지막 예약 사유가 발생한 시각 |
| `recalc_deadline_ms` | 자동 재계산 예정 시각 |
| `last_applied_generation` | 마지막으로 계산을 적용한 세대 번호 |
| `pending_generation` | 현재 예약이 가리키는 세대 번호 |

세대 번호를 두는 이유는 같은 예약을 중복 적용하지 않기 위해서다.
예를 들어 debounce 만료와 `send` 직전 강제 계산이 거의 동시에 들어오더라도,
같은 세대를 두 번 적용하지 않게 만들 수 있다.

## 5. 계산 시점

### 5.1 기본 시점

기본 계산 시점은 아래와 같다.

- 마지막 연결 변화 뒤 `3000ms`가 지났을 때

이 시점에서 context 안의 auto HWM 대상 socket들을 다시 살펴보고, 현재 상태를
기준으로 HWM과 auto buffer를 다시 계산한다.

### 5.2 실제 사용 직전 강제 계산

지연 재계산만 두면 문제가 하나 남는다. 연결 변화가 막 끝난 직후에 application이
바로 송수신을 시작할 수 있기 때문이다.

이 초안은 실사용 직전 강제 계산은 기본 정책에 넣지 않는다.
연결 생성과 종료, `spot` 생성과 종료처럼 topology가 바뀌는 모든 경우는 같은 규칙을
따른다.

1. 변화가 생기면 재계산을 예약한다.
2. 마지막 변화 뒤 `debounce_ms`가 지나면 다시 계산한다.
3. connect와 detach 모두 같은 debounce 규칙을 쓴다.

즉 계산 시점은 "마지막 변화 기준 지연 재계산" 한 가지로 고정한다.

### 5.3 즉시 적용 함수

외부에 노출하는 즉시 적용 함수는 아래 하나만 두는 방향을 권장한다.

- `zlink_ctx_auto_hwm_recalculate(ctx)`

이 함수의 의도된 동작은 아래와 같다.

1. 호출 시점에 context 안의 auto HWM 대상 socket을 다시 수집한다.
2. 현재 옵션과 현재 관찰 상태로 HWM / buffer를 즉시 다시 계산한다.
3. 계산된 값을 적용한다.
4. 성공하면 `recalc_pending`을 해제하고 예약을 비운다.
5. 이미 예약이 있던 경우에도 새 계산 결과로 덮어쓴다.

즉 이 함수는 "예약만 건다"가 아니라, **동기적으로 즉시 적용하고 반환하는 함수**로
정의하는 편이 맞다.

perf나 테스트처럼 timing noise를 줄이고 싶은 경우에는 이 함수를 직접 호출하는
편이 가장 명확하다.

## 6. spot, spotnode, stream 예외

### 6.1 공통 방향

`spot`, `spotnode`, `stream`은 현재 연결 수만 그대로 믿으면 값이 지나치게
작아지거나 반대로 너무 자주 흔들릴 수 있다.

그래서 이 초안은 일부 대상에 대해 **최소 계획 개수**를 둔다.
이 최소값은 "현재 0개여도, 이 정도 규모는 기본적으로 감안하고 계산한다"는 뜻이다.

### 6.2 stream

`stream`은 현재처럼 큰 bootstrap 값을 둘 필요가 있다.

- 제안 기본값: `5000`

`stream`은 연결 수가 짧은 시간에 크게 늘 수 있고, transport buffer 영향도 크기
때문에 보수적으로 잡는 편이 낫다.

### 6.3 spot

`spot`은 런타임에 생성되었다가 사라지는 경우가 많다.
따라서 `stream`보다는 작지만 별도 bootstrap이 필요하다.

- 제안 기본값: `500`

즉 실제 관찰 개수가 10이어도, 정책 계산에서는 `max(actual_spot_count, 500)`처럼
해석하는 방향을 제안한다.

### 6.4 spotnode

`spotnode`는 내부적으로 여러 `spot`을 수용하는 container 성격이므로,
실제 spot 수가 빠르게 바뀌더라도 계산이 지나치게 들쑥날쑥하지 않게 해야 한다.

이 초안은 `spotnode`도 아래 두 값을 함께 고려하는 방향을 제안한다.

- 현재 관찰된 spot 수
- configured bootstrap spot 수

즉 `spotnode`도 기본적으로는 아래 형태를 따른다.

```text
planning_spots = max(observed_spots, configured_spot_bootstrap)
```

기본 bootstrap 기본값은 `spot`과 같은 `500`을 우선 제안한다.

## 7. 수동 설정과의 관계

수동 설정은 여전히 자동 계산보다 우선한다.

아래 항목은 사용자가 직접 설정했다면 auto recalculation이 덮어쓰지 않는다.

- `SNDHWM`
- `RCVHWM`
- `SNDBUF`
- `RCVBUF`

즉 auto recalculation은 "자동 대상만 다시 맞추는 기능"으로 본다.
이미 운영자가 명시한 값까지 다시 바꾸면 예측 가능성이 오히려 나빠진다.

## 8. 권장 옵션 형태

이 초안은 아래 같은 context 옵션을 두는 방향을 권장한다.

| 옵션 | 의미 | 제안 기본값 |
|---|---|---:|
| auto HWM enable | 자동 계산 사용 여부 | on |
| auto HWM total memory budget | context 총 메모리 예산 | 기존 기본값 유지 |
| auto HWM recalc debounce ms | 마지막 변화 후 재계산 대기 시간 | 3000 |
| auto HWM stream bootstrap | `stream` 최소 계획 연결 수 | 5000 |
| auto HWM spot bootstrap | `spot` / `spotnode` 최소 계획 개수 | 500 |

이 문서에서 말하는 "bootstrap"은 초기 연결 수가 아니라, **재계산 시 최소한 이
정도 규모는 감안하자**는 정책값이다.

## 9. 추가 API 초안

이 초안이 구현되면 아래 공개 항목이 추가되어야 한다.

### 9.1 Context option

| 이름 | 방향 | 의미 |
|---|---|---|
| `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` | get/set | 마지막 변화 후 자동 재계산까지 기다리는 시간 |
| `ZLINK_CTX_OPT_AUTO_HWM_STREAM_BOOTSTRAP` | get/set | `stream` 계산 시 최소 계획 연결 수 |
| `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP` | get/set | `spot` / `spotnode` 계산 시 최소 계획 개수 |

초기값은 아래를 제안한다.

- `AUTO_HWM_RECALC_DEBOUNCE_MS = 3000`
- `AUTO_HWM_STREAM_BOOTSTRAP = 5000`
- `AUTO_HWM_SPOT_BOOTSTRAP = 500`

### 9.2 Function

| 이름 | 의미 |
|---|---|
| `int zlink_ctx_auto_hwm_recalculate(void *ctx);` | context 전체 auto HWM을 즉시 다시 계산하고 적용 |

이 함수의 반환 규칙은 아래 방향을 권장한다.

- 성공: `0`
- 실패: `-1`
- `errno`는 기존 context/socket API와 같은 방식으로 설정

이 함수는 아래 경우에도 동작을 명확히 정의하는 편이 좋다.

- auto HWM이 꺼진 context:
  - 실패로 보지 않고 `0`을 반환
  - 아무 값도 바꾸지 않는 no-op으로 처리
- 계산 대상 socket이 하나도 없는 context:
  - 실패로 보지 않고 `0`을 반환
  - 내부 상태만 정리하고 종료

초기 구현에서는 아래 오류를 최소한 정의해야 한다.

- `EINVAL`
  - `ctx == NULL`
  - 유효하지 않은 context handle
- `EFAULT` 또는 내부 공통 오류
  - context 내부 socket 집합 수집 실패
  - 내부 apply 단계 실패

정확한 errno 선택은 기존 core 스타일과 맞춰 확정해야 한다.

### 9.3 옵션과 함수의 관계

각 항목의 관계는 아래처럼 정리하는 편이 명확하다.

| 항목 | 역할 |
|---|---|
| `AUTO_HWM_ENABLE` | auto HWM 전체 on/off |
| `AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` | 총 메모리 budget |
| `AUTO_HWM_RECALC_DEBOUNCE_MS` | 자동 재계산 타이밍 |
| `AUTO_HWM_STREAM_BOOTSTRAP` | `stream` planning count 하한 |
| `AUTO_HWM_SPOT_BOOTSTRAP` | `spot` / `spotnode` planning count 하한 |
| `zlink_ctx_auto_hwm_recalculate(ctx)` | 예약과 무관한 즉시 계산 |

이 초안은 별도의 `mark_dirty()` 공개 API는 추가하지 않는 방향을 전제로 한다.
재계산 예약은 연결 변화와 option 변경 시 library 내부에서 자동으로 처리한다.

## 10. 계산식 방향

재계산 시점만 정하고 계산식이 비어 있으면 정책이 반쪽이 된다.
따라서 이 초안은 아래 계산 순서를 함께 제안한다.

### 9.1 큰 흐름

```text
context total memory budget
-> runtime reserve
-> transport auto buffer cost
-> queue budget
-> socket planning count
-> socket share
-> message slots
-> final HWM
```

핵심은 두 가지다.

- 총 예산에서 reserve와 transport buffer 비용을 먼저 뺀다.
- 남은 queue budget은 **현재 살아 있는 auto HWM 대상 socket들**이 나눠 가진다.

즉 쓰이지 않는 역할 묶음 몫을 미리 25%, 50%처럼 예약해 두는 방식은 이 초안의
기본 방향이 아니다.

또한 초기 구현은 아래 두 원칙을 같이 가져가는 편이 좋다.

- 계산은 항상 context 전체를 기준으로 한다.
- 한 번 계산이 시작되면 그 계산에 참여한 socket 집합과 옵션 스냅샷으로 끝까지
  계산한다.

즉 계산 도중에 새 연결 변화가 들어오더라도, 현재 계산은 중간에 흔들지 않고
다음 세대로 넘기는 편이 구현이 단순하다.

### 9.2 1단계: context queue budget 계산

먼저 context 총 메모리에서 reserve와 transport buffer 비용을 뺀다.

```text
queue_budget_bytes =
  context_total_memory_budget_bytes
  - runtime_reserve_bytes
  - total_auto_buffer_bytes
```

여기서:

- `context_total_memory_budget_bytes`
  사용자가 context에 배정한 총 메모리
- `runtime_reserve_bytes`
  내부 여유분
- `total_auto_buffer_bytes`
  auto `SNDBUF` / `RCVBUF`가 차지하는 총 비용

`total_auto_buffer_bytes`는 각 socket의 planning count를 기준으로 아래처럼
합산하는 방향을 권장한다.

```text
socket_auto_buffer_bytes =
  planning_count *
  ((manual_sndbuf ? 0 : auto_sndbuf_bytes)
   + (manual_rcvbuf ? 0 : auto_rcvbuf_bytes))

total_auto_buffer_bytes =
  sum(socket_auto_buffer_bytes for each auto-hwm socket)
```

### 9.3 2단계: socket planning count 계산

각 socket은 현재 관찰 수와 bootstrap 값을 함께 본다.

```text
planning_count =
  max(observed_count, socket_type_bootstrap)
```

예시는 아래와 같다.

- 일반 `dealer/router/pub/sub`: `max(current_connections, 1)`
- `stream`: `max(current_connections, 5000)`
- `spot`: `max(current_spot_count, 500)`
- `spotnode`: `max(current_spot_count, 500)`

즉 연결이 잠깐 줄었다고 해서 계산값이 지나치게 작아지는 것을 막는다.

구현에서는 먼저 "무엇을 observed count로 볼지"를 socket 종류별로 고정해야 한다.

| 대상 | observed count 기준 |
|---|---|
| 일반 socket | 현재 attached pipe 수 |
| `stream` | 현재 attached session 수 |
| `spot` | 현재 활성 attachment 수 |
| `spotnode` shared 내부 socket | 현재 활성 spot 수 |

초기 구현은 "현재 attached pipe 수"와 "현재 활성 spot 수"만 정확히 잡아도 충분하다.

진단과 perf report에는 아래 둘을 함께 보여주는 편이 맞다.

- `observed count`
- `planning count`

그래야 사용자가 "현재 실제 관찰 개수는 작지만, bootstrap 때문에 더 큰 planning count로
계산했다"는 점을 바로 이해할 수 있다.

### 9.4 3단계: context 안 socket share 계산

queue budget은 context 안의 auto HWM 대상 socket들이 planning count 비례로 나눈다.

```text
total_weight =
  sum(socket_planning_count for each auto-hwm socket)

socket_queue_share =
  queue_budget_bytes * socket_planning_count / total_weight
```

여기서 중요한 점은 아래와 같다.

- 분배 기준은 role 고정 비율이 아니라 **실제 참여 socket 집합**이다.
- monitor socket, service 보조 socket 같은 내부 보조 경로는 기본적으로 분배
  대상에서 제외해야 한다.

초기 구현에서는 아래 조건을 모두 만족하는 socket만 분배 대상으로 보는 편이
명확하다.

1. auto HWM enable이 켜져 있다.
2. auto HWM 대상 socket 종류다.
3. monitor용 보조 socket이 아니다.
4. service monitor bridge 같은 진단 전용 경로가 아니다.

수동 override가 있는 socket을 분배 대상에서 완전히 제외할지, 아니면 계산에는
참여시키되 적용 단계에서 일부 항목만 유지할지는 구현 전에 고정해야 한다.
이 초안은 아래 규칙을 권장한다.

- socket은 분배 계산에는 계속 참여한다.
- manual `SNDHWM`이 있으면 `SNDHWM`만 유지한다.
- manual `RCVHWM`이 있으면 `RCVHWM`만 유지한다.
- manual `SNDBUF` / `RCVBUF`가 있으면 해당 buffer만 자동값을 덮어쓰지 않는다.

즉 "수동 항목만 유지하고, 나머지 auto 항목은 계속 계산"하는 쪽이 mixed override
를 가장 자연스럽게 설명한다.

### 9.5 4단계: socket share를 메시지 수로 변환

각 socket이 받은 바이트 예산을 `msg unit`으로 나누어 메시지 수로 바꾼다.

```text
socket_message_slots =
  floor(socket_queue_share / effective_message_bytes)
```

여기서 `effective_message_bytes`는 그 socket이 계산에 사용하는 실효 메시지 크기다.
초기 구현에서는 우선 `msg unit`을 그대로 써도 된다.

다만 문서와 진단에는 아래 둘을 구분해 보여주는 편이 좋다.

- configured msg unit
- effective message bytes

### 9.6 5단계: 연결 단위 HWM 계산

최종 HWM은 socket 전체 메시지 수를 다시 planning count로 나누어 얻는다.

```text
final_hwm =
  floor(socket_message_slots / socket_planning_count)
```

필요하면 socket 종류별 최소 바닥값을 둘 수 있다.

```text
final_hwm =
  max(base_floor_per_connection, final_hwm)
```

이 초안에서는 최소 바닥값을 둘 수는 있지만, 계산의 중심은 어디까지나
`socket_queue_share / effective_message_bytes / planning_count`에 둔다.

### 9.7 한 줄 식

위 단계를 한 줄로 줄이면 아래처럼 읽을 수 있다.

```text
final_hwm
  ~= ((context_total - reserve - auto_buffers)
      * socket_weight / total_weight)
     / effective_message_bytes
     / socket_planning_count
```

여기서 보통 `socket_weight`와 `socket_planning_count`는 같은 값으로 둘 수 있다.
그러면 해석은 더 단순해진다.

- 총 queue budget을 socket이 나눠 가진다.
- socket이 받은 몫을 메시지 개수로 바꾼다.
- 그 socket이 감당해야 할 연결 수로 다시 나눈다.

## 11. 회귀 테스트 항목

이 초안이 구현되면 아래 회귀 테스트를 최소 범위로 둬야 한다.

### 10.1 기본 동작

| 항목 | 확인 내용 |
|---|---|
| auto off no-op | auto HWM이 꺼져 있을 때 즉시 함수가 성공 반환하고 값을 바꾸지 않는지 |
| 기본값 유지 | auto HWM 미사용 시 기존 default HWM/BUF가 그대로 유지되는지 |
| debounce 예약 | 연결 변화 직후 즉시 전체 재계산하지 않고 예약만 잡히는지 |
| debounce 만료 | 추가 변화가 없으면 deadline 이후 정확히 한 번만 계산되는지 |
| debounce reset | deadline 전에 새 연결 변화가 오면 예약 시각이 뒤로 밀리는지 |
| `debounce=0` | 연결 변화 직후 즉시 계산되는지 |
| 즉시 함수 | `zlink_ctx_auto_hwm_recalculate(ctx)`가 예약 여부와 무관하게 즉시 적용되는지 |

### 10.2 수동 설정 우선순위

| 항목 | 확인 내용 |
|---|---|
| manual HWM 유지 | manual `SNDHWM` / `RCVHWM`이 자동 계산에 덮어써지지 않는지 |
| manual BUF 유지 | manual `SNDBUF` / `RCVBUF`가 자동 buffer 계산에 덮어써지지 않는지 |
| mixed override | 일부만 manual일 때 나머지 auto 항목만 계산되는지 |

### 10.3 계산 대상 제외

| 항목 | 확인 내용 |
|---|---|
| monitor socket 제외 | monitor용 `PAIR`가 계산 대상과 weight 합산에서 제외되는지 |
| service monitor 제외 | service monitor socket이 제외되는지 |
| diagnostic bridge 제외 | 진단용 bridge socket이 제외되는지 |

### 10.4 socket 종류별 planning count

| 항목 | 확인 내용 |
|---|---|
| 일반 socket | attached pipe 수 기준으로 계산되는지 |
| `stream` bootstrap | 연결 수가 작아도 최소 `5000` 기준이 적용되는지 |
| `spot` bootstrap | 활성 spot 수가 작아도 최소 `500` 기준이 적용되는지 |
| `spotnode` bootstrap | 활성 spot 수가 작아도 최소 `500` 기준이 적용되는지 |

### 10.5 perf 회귀

| 항목 | 확인 내용 |
|---|---|
| single perf | `64B/tcp` 전 패턴이 끝까지 완료되는지 |
| multi perf | `64B/tcp` 다중 client 패턴이 hang 없이 끝나는지 |
| 즉시 함수 경로 | perf setup 직후 `recalculate()`를 호출한 경로가 안정적으로 동작하는지 |
| HWM report | report에 `observed count`와 `planning count`가 함께 보이고, bootstrap 반영 결과를 해석할 수 있는지 |

### 10.6 경쟁 조건

| 항목 | 확인 내용 |
|---|---|
| timer vs force | debounce 만료와 즉시 함수 호출이 겹쳐도 한 세대가 두 번 적용되지 않는지 |
| send/recv fast path | `send` 또는 `recv` 직전 강제 계산이 걸려도 deadlock이나 재진입 문제가 없는지 |
| rapid churn | attach/detach가 빠르게 반복되어도 예약 상태가 누수되지 않는지 |

## 12. 구현 기준 정리

이 초안이 구현 가능하려면 아래 항목을 먼저 고정해야 한다.

### 10.1 반드시 고정할 값

- context당 timer는 하나
- 자동 재계산 기본 debounce는 `3000ms`
- `debounce=0`은 즉시 재계산
- 계산 단위는 context 전체
- 즉시 적용 함수는 `zlink_ctx_auto_hwm_recalculate(ctx)` 하나
- `stream bootstrap = 5000`
- `spot bootstrap = 500`

### 10.2 반드시 고정할 제외 대상

- socket monitor용 `PAIR`
- service monitor socket
- 진단 bridge socket

### 10.3 반드시 고정할 우선순위

1. `AUTO_HWM_ENABLE=off`이면 자동 예약과 자동 적용은 모두 동작하지 않는다.
2. manual `SNDHWM` / `RCVHWM`이 있으면 자동 HWM은 그 항목을 덮어쓰지 않는다.
3. manual `SNDBUF` / `RCVBUF`가 있으면 auto buffer 비용에서 그 항목은 자동 비용을
   더하지 않는다.
4. 자동 재계산 예약이 있어도 `zlink_ctx_auto_hwm_recalculate(ctx)`가 호출되면 즉시
   계산이 우선한다.

## 13. 기대 효과

이 정책이 의도하는 결과는 아래와 같다.

- 대량 연결 시작 구간에서 계산이 과도하게 반복되지 않는다.
- application은 calculate 호출 타이밍을 직접 관리하지 않아도 된다.
- 연결 수가 정말 바뀌면 그 상태를 결국 반영한다.
- `spot` / `spotnode` / `stream`처럼 동적인 대상도 지나치게 작은 값으로
  쪼그라들지 않는다.

즉 이 초안은 "완전 수동"과 "변화마다 즉시 재계산" 사이에서, 운영상 가장
다루기 쉬운 중간 지점을 목표로 한다.

## 14. 남은 결정 사항

구현 전에 아래 사항은 더 확정해야 한다.

- `runtime_reserve` 비율을 몇 %로 둘지
- manual override가 있는 socket을 분배 계산에는 계속 참여시킬지
- `recalculate()` 도중 일부 socket apply가 실패하면 부분 적용을 허용할지

## 15. 구현 순서 메모

권장 구현 순서는 아래와 같다.

1. context 단위 `recalc_pending` / `deadline` 상태 추가
2. 연결 변화 이벤트에서 즉시 재계산 대신 debounce 예약 추가
3. context 단위 batch recalculation 추가
4. `stream` bootstrap 옵션 추가
5. `spot` / `spotnode` bootstrap 옵션 추가
6. perf report에 `observed count` / `planning count` 표시 추가
