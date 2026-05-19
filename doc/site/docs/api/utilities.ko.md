[English](./utilities.md) | [한국어](./utilities.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](./README.ko.md)

# 유틸리티

원자적 카운터, 스케줄링 타이머, 고해상도 타이밍, 스레드 관리 및 기타 작업을
위한 헬퍼 함수입니다. 이 유틸리티는 코어 메시징 API를 보완하며 이벤트 루프
구축, 벤치마킹, 백그라운드 스레드 관리에 유용합니다.

## 콜백 타입

```c
typedef void (*zlink_timer_handler_fn) (void *timer_,
                                        uint64_t fire_count_,
                                        void *userdata_);
typedef void (zlink_thread_fn)(void *);
```

`zlink_timer_handler_fn`은 타이머 만료 콜백 시그니처이다. `timer_`는
발화한 타이머 handle이고 `fire_count_`는 누적 발화 횟수이며 `userdata_`는
핸들러 등록 시 넘긴 사용자 포인터다.

`zlink_thread_fn`은 `zlink_thread_start`로 시작되는 스레드의 진입점
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

독립 실행형 타이머 핸들로 나노초 정밀도의 주기적/일회성 타이머를 제공한다.
`zlink_timer_new`로 컨텍스트 타이머를, `zlink_spot_timer_new`로 Spot 소유
타이머를 생성한다. 생성 후에는 동일한 `zlink_timer_*` API로 제어한다.

### 콜백 타입

```c
typedef void (*zlink_timer_handler_fn) (void *timer_,
                                        uint64_t fire_count_,
                                        void *userdata_);
```

타이머 만료 시 호출되는 콜백. `fire_count_`는 시작 이후 누적 발동 횟수.

---

### zlink_timer_new

독립 실행형 타이머를 생성한다.

```c
void *zlink_timer_new (void);
```

**반환값:** 성공 시 타이머 핸들, 실패 시 `NULL`.

**참고:** `zlink_spot_timer_new`, `zlink_timer_destroy`

---

### zlink_spot_timer_new

Spot 소유 타이머를 생성한다. 생성된 타이머의 수명과 event delivery 는 연결된
Spot 과 함께 동작한다.

```c
void *zlink_spot_timer_new (void *spot_);
```

**반환값:** 성공 시 타이머 핸들, 실패 시 `NULL`.

**참고:** `zlink_timer_new`, `zlink_timer_destroy`

---

### zlink_timer_destroy

타이머를 파괴하고 리소스를 해제한다.

```c
zlink_close_result_t zlink_timer_destroy (void **timer_p_);
```

**반환값:** 성공 시 `ZLINK_CLOSE_OK`. 실패 시에는 `zlink_close_result_t`
값을 반환한다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지된다.

---

### zlink_timer_start

타이머를 시작한다.

```c
zlink_config_result_t zlink_timer_start (void *timer_,
                                         uint64_t interval_ns_,
                                         uint64_t repeat_count_);
```

`interval_ns_` 나노초 간격으로 타이머를 시작한다. `repeat_count_`가 0이면
무한 반복, 양수이면 해당 횟수만큼 발동 후 자동 정지.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`. 실패 시에는 `zlink_config_result_t`
값을 반환한다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지된다.

**참고:** `zlink_timer_stop`

---

### zlink_timer_stop

실행 중인 타이머를 정지한다.

```c
zlink_config_result_t zlink_timer_stop (void *timer_);
```

**반환값:** 성공 시 `ZLINK_CONFIG_OK`. 실패 시에는 `zlink_config_result_t`
값을 반환한다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지된다.

**참고:** `zlink_timer_start`

---

### zlink_timer_recv

타이머 발동을 동기적으로 수신한다.

```c
zlink_recv_result_t zlink_timer_recv (void *timer_, uint64_t *fire_count_out_);
```

recv 모드에서 다음 타이머 발동을 기다린다. 성공하면 `*fire_count_out_`에
누적 발동 횟수가 설정된다.

**반환값:** 성공 시 `ZLINK_RECV_OK`. 실패 시에는 `zlink_recv_result_t`
값을 반환한다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지된다.

**에러:** 타이머가 이미 멈췄고 더 읽을 발동이 없으면 `ZLINK_RECV_NO_DATA`
(내부 `EAGAIN`).

**참고:** `zlink_timer_handler`

---

### zlink_timer_handler

타이머 만료 콜백 핸들러를 등록한다.

```c
zlink_handler_result_t zlink_timer_handler (void *timer_,
                                            zlink_timer_handler_fn handler_,
                                            void *userdata_);
```

콜백 핸들러를 등록하면 `zlink_timer_recv`는 `ZLINK_RECV_BUSY`로 실패한다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`. 실패 시에는 `zlink_handler_result_t`
값을 반환한다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지된다.

**참고:** `zlink_timer_recv`

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

### zlink_thread_start

지정된 함수를 실행하는 새 스레드를 시작합니다.

```c
void *zlink_thread_start(zlink_thread_fn *func_, void *arg_);
```

`arg_`를 유일한 인수로 사용하여 `func_`를 실행하는 새 운영 체제 스레드를
생성하고 시작합니다. 반환된 핸들은 완료를 대기하고 리소스를 해제하기 위해
`zlink_thread_join`에 전달해야 합니다.

**반환값:** 성공 시 불투명 스레드 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_thread_join`

---

### zlink_thread_join

스레드가 완료될 때까지 대기하고 핸들을 해제합니다.

```c
void zlink_thread_join(void *thread_);
```

`thread_`로 식별되는 스레드가 종료될 때까지 호출 스레드를 블록한 다음 핸들을
해제합니다. 이 호출 이후 핸들을 사용해서는 안 됩니다.

**스레드 안전성:** 핸들당 정확히 한 번만 호출해야 합니다. 조인 대상 스레드에서
호출하지 마십시오.

**참고:** `zlink_thread_start`
