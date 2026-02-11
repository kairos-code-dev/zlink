[English](utilities.md) | [한국어](utilities.ko.md)

# 유틸리티

원자적 카운터, 스케줄링 타이머, 고해상도 타이밍, 스레드 관리 및 기타 작업을
위한 헬퍼 함수입니다. 이 유틸리티는 코어 메시징 API를 보완하며 이벤트 루프
구축, 벤치마킹, 백그라운드 스레드 관리에 유용합니다.

## 콜백 타입

```c
typedef void (zlink_timer_fn)(int timer_id, void *arg);
typedef void (zlink_thread_fn)(void *);
```

`zlink_timer_fn`은 타이머 만료 알림을 위한 콜백 시그니처입니다. `timer_id`는
어떤 타이머가 발동했는지 식별하고 `arg`는 타이머 생성 시 전달된 사용자 제공
컨텍스트 포인터입니다.

`zlink_thread_fn`은 `zlink_threadstart`로 시작되는 스레드의 진입점
시그니처입니다.

## 원자적 카운터

원자적 카운터는 공유 정수에 대한 잠금 없는 증가, 감소 및 읽기 작업을
제공합니다. 카운터는 `zlink_atomic_counter_new`로 생성하고
`zlink_atomic_counter_destroy`로 파괴해야 합니다.

> **참고:** `zlink_atomic_counter_new`만 공유 라이브러리에서 내보내집니다
> (`ZLINK_EXPORT`). 나머지 다섯 함수는 내보내기 속성 없이 선언되지만 여전히
> 공개 API이며 정적 링크 또는 헤더를 통해 사용할 수 있습니다.

### zlink_atomic_counter_new

0으로 초기화된 새 원자적 카운터를 생성합니다.

```c
void *zlink_atomic_counter_new(void);
```

초기값이 0인 원자적 카운터에 대한 불투명 핸들을 할당하고 반환합니다.

**반환값:** 성공 시 카운터 핸들, 실패 시 `NULL` (메모리 부족).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_atomic_counter_set`, `zlink_atomic_counter_destroy`

---

### zlink_atomic_counter_set

카운터를 명시적 값으로 설정합니다.

```c
void zlink_atomic_counter_set(void *counter_, int value_);
```

현재 카운터 값을 `value_`로 원자적으로 교체합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_atomic_counter_value`

---

### zlink_atomic_counter_inc

카운터를 1 증가시킵니다.

```c
int zlink_atomic_counter_inc(void *counter_);
```

카운터를 원자적으로 증가시키고 이전 값(증가 직전의 값)을 반환합니다.

**반환값:** 증가 전 카운터 값.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_atomic_counter_dec`

---

### zlink_atomic_counter_dec

카운터를 1 감소시킵니다.

```c
int zlink_atomic_counter_dec(void *counter_);
```

카운터를 원자적으로 감소시키고 이전 값(감소 직전의 값)을 반환합니다.

**반환값:** 감소 전 카운터 값.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_atomic_counter_inc`

---

### zlink_atomic_counter_value

현재 카운터 값을 반환합니다.

```c
int zlink_atomic_counter_value(void *counter_);
```

카운터의 현재 값을 원자적으로 읽습니다.

**반환값:** 현재 카운터 값.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_atomic_counter_set`

---

### zlink_atomic_counter_destroy

카운터를 파괴하고 메모리를 해제합니다.

```c
void zlink_atomic_counter_destroy(void **counter_p_);
```

카운터 핸들을 해제합니다. 파괴 후 `*counter_p_`의 포인터는 `NULL`로
설정됩니다.

**스레드 안전성:** 다른 스레드가 동일한 카운터에서 작업 중일 때 호출해서는
안 됩니다.

**참고:** `zlink_atomic_counter_new`

---

## 타이머

스케줄링 타이머를 사용하면 지정된 간격 후에 발동하는 콜백을 등록할 수
있습니다. 타이머는 집합으로 관리됩니다: `zlink_timers_new`로 집합을 생성하고,
`zlink_timers_add`로 개별 타이머를 추가하며, 이벤트 루프에서
`zlink_timers_execute`를 호출하여 실행합니다.

### zlink_timers_new

새 타이머 집합을 생성합니다.

```c
void *zlink_timers_new(void);
```

빈 타이머 집합에 대한 불투명 핸들을 할당하고 반환합니다.

**반환값:** 성공 시 타이머 집합 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_timers_destroy`, `zlink_timers_add`

---

### zlink_timers_destroy

타이머 집합을 파괴하고 모든 리소스를 해제합니다.

```c
int zlink_timers_destroy(void **timers_p);
```

집합의 모든 타이머를 취소하고 핸들을 해제합니다. 파괴 후 `*timers_p`의
포인터는 `NULL`로 설정됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 다른 스레드가 동일한 타이머 집합에서 작업 중일 때 호출해서는
안 됩니다.

**참고:** `zlink_timers_new`

---

### zlink_timers_add

지정된 간격과 콜백으로 타이머를 추가합니다.

```c
int zlink_timers_add(void *timers, size_t interval, zlink_timer_fn handler, void *arg);
```

`interval` 밀리초 후에 발동하는 새 타이머를 등록합니다. 타이머가 만료되면
타이머의 ID와 사용자 제공 `arg`와 함께 `handler`가 호출됩니다. 타이머는
취소될 때까지 동일한 간격으로 자동 반복됩니다.

**반환값:** 성공 시 음이 아닌 타이머 ID, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_cancel`, `zlink_timers_set_interval`

---

### zlink_timers_cancel

ID로 타이머를 취소합니다.

```c
int zlink_timers_cancel(void *timers, int timer_id);
```

집합에서 타이머를 제거합니다. 해당 콜백은 더 이상 호출되지 않습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_add`

---

### zlink_timers_set_interval

기존 타이머의 간격을 변경합니다.

```c
int zlink_timers_set_interval(void *timers, int timer_id, size_t interval);
```

타이머의 간격을 `interval` 밀리초로 업데이트합니다. 새 간격은 현재 주기가
완료된 후 적용됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_add`, `zlink_timers_reset`

---

### zlink_timers_reset

타이머의 카운트다운을 전체 간격으로 리셋합니다.

```c
int zlink_timers_reset(void *timers, int timer_id);
```

타이머의 카운트다운을 현재 간격의 처음부터 다시 시작하여 다음 만료를
효과적으로 연기합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_set_interval`

---

### zlink_timers_timeout

다음 타이머 발동까지의 시간을 반환합니다.

```c
long zlink_timers_timeout(void *timers);
```

집합에서 가장 빠른 타이머가 만료될 때까지 남은 밀리초 수를 계산합니다. 이
값은 `zlink_poll`의 `timeout_` 인수로 직접 전달하기에 적합합니다.

**반환값:** 다음 만료까지의 밀리초, 등록된 타이머가 없으면 `-1`.

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_execute`, `zlink_poll`

---

### zlink_timers_execute

만료된 모든 타이머를 실행합니다.

```c
int zlink_timers_execute(void *timers);
```

집합의 모든 타이머를 확인하고 간격이 경과한 각 타이머에 대해 콜백을
호출합니다. 일반적으로 `zlink_timers_timeout` 및 `zlink_poll`과 함께
루프에서 호출됩니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 동일한 타이머 집합에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_timers_timeout`, `zlink_timers_add`

---

## 스톱워치

벤치마킹 및 프로파일링을 위한 고해상도 타이밍 함수입니다. 스톱워치를 시작하고,
중간 측정값을 읽고, 중지하여 마이크로초 단위의 총 경과 시간을 얻습니다.

### zlink_stopwatch_start

고해상도 스톱워치를 시작합니다.

```c
void *zlink_stopwatch_start(void);
```

현재 시간을 캡처하고 경과 시간을 측정하는 데 사용되는 불투명 핸들을
반환합니다. 핸들은 최종적으로 `zlink_stopwatch_stop`으로 해제해야 합니다.

**반환값:** 성공 시 불투명 스톱워치 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다. 반환된 핸들은 한 번에
하나의 스레드에서만 사용해야 합니다.

**참고:** `zlink_stopwatch_intermediate`, `zlink_stopwatch_stop`

---

### zlink_stopwatch_intermediate

스톱워치를 중지하지 않고 경과 마이크로초를 반환합니다.

```c
unsigned long zlink_stopwatch_intermediate(void *watch_);
```

핸들을 해제하지 않고 `zlink_stopwatch_start`가 호출된 이후의 경과 시간을
읽습니다. 연속적인 측정을 위해 여러 번 호출할 수 있습니다.

**반환값:** 마이크로초 단위의 경과 시간.

**스레드 안전성:** 동일한 핸들에서 `zlink_stopwatch_stop`과 동시에 호출해서는
안 됩니다.

**참고:** `zlink_stopwatch_start`, `zlink_stopwatch_stop`

---

### zlink_stopwatch_stop

스톱워치를 중지하고 총 경과 마이크로초를 반환합니다.

```c
unsigned long zlink_stopwatch_stop(void *watch_);
```

`zlink_stopwatch_start`가 호출된 이후의 총 경과 시간을 반환하고 스톱워치
핸들을 해제합니다. 이 호출 이후 핸들을 사용해서는 안 됩니다.

**반환값:** 마이크로초 단위의 경과 시간.

**스레드 안전성:** 동일한 핸들에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_stopwatch_start`, `zlink_stopwatch_intermediate`

---

## 기타

### zlink_sleep

지정된 초 동안 슬립합니다.

```c
void zlink_sleep(int seconds_);
```

호출 스레드를 최소 `seconds_`초 동안 일시 중지합니다. 이는 플랫폼별 슬립
함수에 대한 이식 가능한 편의 래퍼입니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_stopwatch_start`

---

### zlink_threadstart

지정된 함수를 실행하는 새 스레드를 시작합니다.

```c
void *zlink_threadstart(zlink_thread_fn *func_, void *arg_);
```

`arg_`를 유일한 인수로 사용하여 `func_`를 실행하는 새 운영 체제 스레드를
생성하고 시작합니다. 반환된 핸들은 완료를 대기하고 리소스를 해제하기 위해
`zlink_threadclose`에 전달해야 합니다.

**반환값:** 성공 시 불투명 스레드 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_threadclose`

---

### zlink_threadclose

스레드가 완료될 때까지 대기하고 핸들을 해제합니다.

```c
void zlink_threadclose(void *thread_);
```

`thread_`로 식별되는 스레드가 종료될 때까지 호출 스레드를 블록한 다음 핸들을
해제합니다. 이 호출 이후 핸들을 사용해서는 안 됩니다.

**스레드 안전성:** 핸들당 정확히 한 번만 호출해야 합니다. 조인 대상 스레드에서
호출하지 마십시오.

**참고:** `zlink_threadstart`
