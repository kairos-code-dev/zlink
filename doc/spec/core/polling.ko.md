[English](polling.md) | [한국어](polling.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 폴링, 프록시 & 기능 확인

소켓, 파일 디스크립터, 타이머에 걸친 I/O 멀티플렉싱과 메시지 전달 프록시 및
런타임 기능 쿼리를 위한 함수와 타입입니다.

## 타입

### zlink_fd_t

플랫폼 의존적 파일 디스크립터 타입입니다.

```c
#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif
```

### Poller Event Masks

공개 헤더는 `typedef short zlink_poller_event_mask_t;`를 편의 별칭으로
내보냅니다. poller API의 `events`, `revents`, 각 이벤트 마스크 인자에
사용되는 `short` 비트마스크의 별칭입니다.

### zlink_poller_source_kind_t

폴러 이벤트를 생성한 소스의 종류를 식별합니다.

```c
typedef enum zlink_poller_source_kind_t
{
    ZLINK_POLLER_SOURCE_SOCKET = 1,
    ZLINK_POLLER_SOURCE_FD     = 2,
    ZLINK_POLLER_SOURCE_TIMER  = 3
} zlink_poller_source_kind_t;
```

| 값 | 의미 |
|---|------|
| `ZLINK_POLLER_SOURCE_SOCKET` | zlink 소켓에서 발생한 이벤트 |
| `ZLINK_POLLER_SOURCE_FD` | 네이티브 파일 디스크립터에서 발생한 이벤트 |
| `ZLINK_POLLER_SOURCE_TIMER` | 타이머에서 발생한 이벤트 |

### zlink_pollitem_t

`zlink_poll` 함수에서 사용하는 디스크립터입니다.

```c
typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;
```

| 필드 | 설명 |
|------|------|
| `socket` | zlink 소켓 핸들, 원시 fd를 폴링할 경우 `NULL` |
| `fd` | 네이티브 파일 디스크립터 (`socket`이 `NULL`일 때 사용) |
| `events` | 요청 이벤트 마스크 (`ZLINK_POLLIN`, `ZLINK_POLLOUT` 등) |
| `revents` | `zlink_poll`이 채우는 반환 이벤트 마스크 |

### zlink_poller_event_t

폴러 API가 반환하는 이벤트 구조체입니다.

```c
typedef struct zlink_poller_event_t
{
    zlink_poller_source_kind_t source_kind;
    void *socket;
    zlink_fd_t fd;
    void *timer;
    void *user_data;
    short events;
} zlink_poller_event_t;
```

| 필드 | 설명 |
|------|------|
| `source_kind` | 이벤트를 트리거한 소스의 종류 |
| `socket` | zlink 소켓 핸들 (`source_kind`가 `SOCKET`일 때 유효) |
| `fd` | 네이티브 파일 디스크립터 (`source_kind`가 `FD`일 때 유효) |
| `timer` | 타이머 핸들 (`source_kind`가 `TIMER`일 때 유효) |
| `user_data` | 소스 등록 시 제공된 불투명 포인터 |
| `events` | 발생한 이벤트의 비트마스크 |

현재 public poller는 `Spot` 전용 결과 타입을 따로 두지 않는다. 즉 이 구조체만으로는
owner `Spot`, dispatch event kind, drain(큐에 쌓인 메시지를 꺼내 소비하는 행위) 대상 subject를 함께 표현할 수 없다.
SPOT의 subscribe / routed / channel reply / timer readiness를 한 owner 기준으로
직렬 처리하려면 현재 공개 계약에서는 `zlink_spot_dispatch_event_handler()`를
사용해야 한다.

## 상수

```c
typedef enum zlink_poller_event_flag_e
{
    ZLINK_POLLIN         = 1,
    ZLINK_POLLOUT        = 2,
    ZLINK_POLLERR        = 4,
    ZLINK_POLLPRI        = 8,
    ZLINK_POLLITEMS_DFLT = 16
} zlink_poller_event_flag_e;

#define ZLINK_HAVE_POLLER 1
```

| 상수 | 값 | 설명 |
|------|----|------|
| `ZLINK_POLLIN` | 1 | 읽기 가능한 데이터가 있음 |
| `ZLINK_POLLOUT` | 2 | 송신 재시도 준비 신호. 해당 핸들이 backpressure(배압) 상태에서 벗어나 송신을 다시 시도할 가치가 있음을 뜻합니다. transport가 단순히 writable하다는 뜻이 아니며, 재시도 성공을 보장하지도 않습니다. `zlink_send_ready_handler()` 콜백과 동일한 readiness 축을 공유합니다. `ZLINK_SUBMIT_BACKPRESSURED` 이후 이 신호가 관찰되면 재시도할 수 있지만, 재시도가 다시 `BACKPRESSURED`로 실패할 수도 있습니다. |
| `ZLINK_POLLERR` | 4 | 디스크립터에서 오류 발생 |
| `ZLINK_POLLPRI` | 8 | 긴급/우선순위 데이터 사용 가능 |
| `ZLINK_POLLITEMS_DFLT` | 16 | 기본 poll-item 배열 크기 |
| `ZLINK_HAVE_POLLER` | 1 | 폴러 지원이 포함되어 컴파일됨 |

## 함수 -- 배열 Poll

### zlink_poll

소켓 및/또는 파일 디스크립터 집합의 I/O 준비 상태를 폴링합니다.

```c
int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_, zlink_config_result_t *error_out_);
```

`items_`에 나열된 디스크립터의 이벤트를 기다립니다. 각 항목은 `events` 필드에
관심 이벤트를 지정하며, 반환 시 각 항목의 `revents` 필드에 발생한 이벤트가
표시됩니다. 실패 시 `*error_out_`에 설정 결과(`zlink_config_result_t`)가
기록되고, 성공 시 항목 수가 기본 반환값으로 반환됩니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `items_` | 모니터링할 poll 항목 배열 |
| `nitems_` | 배열의 항목 수 |
| `timeout_` | 최대 대기 시간(밀리초); `0`이면 즉시 반환, `-1`이면 무한 대기 |
| `error_out_` | 실패 시 `zlink_config_result_t`가 기록됨 |

**반환값:** 이벤트가 시그널된 항목 수, 타임아웃 시 `0`, 실패 시 `-1`이며
`*error_out_`에 `zlink_config_result_t`가 기록됩니다. `zlink_errno()`는
진단용 내부 errno를 그대로 유지합니다.

**에러:**
- `ETERM` -- 컨텍스트가 종료되었습니다.
- `EFAULT` -- `items_` 포인터가 유효하지 않습니다.
- `EINTR` -- 시그널에 의해 호출이 중단되었습니다.

**스레드 안전성:** 호출 중 각 poll 항목의 소켓은 다른 스레드에서 사용해서는
안 됩니다.

**참고:** `zlink_poller_wait`

---

## 함수 -- 폴러 API

폴러는 `zlink_poll`을 보완하는 객체 기반 API입니다. 단일 이벤트 루프에서 소켓,
네이티브 파일 디스크립터, 타이머를 지원합니다.

### zlink_poller_new

새 폴러 인스턴스를 생성합니다.

```c
void *zlink_poller_new (void);
```

불투명 폴러 핸들을 할당하고 반환합니다. 더 이상 필요하지 않으면
`zlink_poller_destroy`로 파괴합니다.

**반환값:** 성공 시 폴러 핸들, 실패 시 `NULL` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_poller_destroy`

---

### zlink_poller_destroy

폴러를 파괴하고 리소스를 해제합니다.

```c
zlink_close_result_t zlink_poller_destroy (void **poller_p_);
```

폴러 핸들을 해제합니다. 파괴 후 `*poller_p_`의 포인터는 `NULL`로 설정됩니다.

**반환값:** 성공 시 `ZLINK_CLOSE_OK`, 실패 시 `zlink_close_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 다른 스레드가 동일한 폴러를 사용 중일 때 호출해서는 안 됩니다.

**참고:** `zlink_poller_new`

---

### zlink_poller_size

폴러에 등록된 소스의 수를 반환합니다.

```c
int zlink_poller_size (void *poller_, zlink_config_result_t *error_out_);
```

실패 시 `*error_out_`에 설정 결과(`zlink_config_result_t`)가 기록되고,
성공 시 항목 수가 기본 반환값으로 반환됩니다.

**반환값:** 등록된 소켓, 파일 디스크립터, 타이머의 현재 수, 실패 시 `-1`이며
`*error_out_`에 `zlink_config_result_t`가 기록됩니다. `zlink_errno()`는
진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 추가/제거 작업과 동시에 호출해서는 안 됩니다.

---

### zlink_poller_add

zlink 소켓을 폴러에 등록합니다.

```c
zlink_config_result_t zlink_poller_add (void *poller_, void *socket_, void *user_data_, short events_);
```

`socket_`을 폴러에 추가하고 `events_`에 지정된 이벤트를 모니터링합니다.
`user_data_` 포인터는 저장되어 이벤트 발생 시 `zlink_poller_event_t`에
반환됩니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `poller_` | 폴러 핸들 |
| `socket_` | 모니터링할 zlink 소켓 |
| `user_data_` | 이벤트와 함께 반환되는 불투명 포인터 |
| `events_` | 이벤트 마스크 (`ZLINK_POLLIN`, `ZLINK_POLLOUT` 등) |

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_modify`, `zlink_poller_remove`

---

### zlink_poller_modify

등록된 소켓의 모니터링 이벤트를 변경합니다.

```c
zlink_config_result_t zlink_poller_modify (void *poller_, void *socket_, short events_);
```

`zlink_poller_add`로 이전에 추가한 `socket_`의 이벤트 마스크를 업데이트합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_add`

---

### zlink_poller_remove

폴러에서 zlink 소켓을 제거합니다.

```c
zlink_config_result_t zlink_poller_remove (void *poller_, void *socket_);
```

소켓의 등록을 해제합니다. 더 이상 이벤트를 생성하지 않습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_add`

---

### zlink_poller_add_fd

네이티브 파일 디스크립터를 폴러에 등록합니다.

```c
zlink_config_result_t zlink_poller_add_fd (void *poller_, zlink_fd_t fd_, void *user_data_, short events_);
```

파일 디스크립터 `fd_`를 추가하고 지정된 이벤트를 모니터링합니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `poller_` | 폴러 핸들 |
| `fd_` | 네이티브 파일 디스크립터 |
| `user_data_` | 이벤트와 함께 반환되는 불투명 포인터 |
| `events_` | 이벤트 마스크 |

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_modify_fd`, `zlink_poller_remove_fd`

---

### zlink_poller_add_timer

타이머를 폴러에 등록합니다.

```c
zlink_config_result_t zlink_poller_add_timer (void *poller_, void *timer_, void *user_data_);
```

타이머 핸들 `timer_`를 폴러에 추가합니다. 타이머가 발동하면 폴러는
`source_kind`가 `ZLINK_POLLER_SOURCE_TIMER`로 설정된 이벤트를 반환합니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `poller_` | 폴러 핸들 |
| `timer_` | 타이머 핸들 (`zlink_timer_new` 또는 `zlink_spot_timer_new`에서 반환) |
| `user_data_` | 이벤트와 함께 반환되는 불투명 포인터 |

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_remove_timer`

---

### zlink_poller_modify_fd

등록된 파일 디스크립터의 모니터링 이벤트를 변경합니다.

```c
zlink_config_result_t zlink_poller_modify_fd (void *poller_, zlink_fd_t fd_, short events_);
```

`zlink_poller_add_fd`로 이전에 추가한 `fd_`의 이벤트 마스크를 업데이트합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_add_fd`

---

### zlink_poller_remove_fd

폴러에서 파일 디스크립터를 제거합니다.

```c
zlink_config_result_t zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_);
```

파일 디스크립터의 등록을 해제합니다. 더 이상 이벤트를 생성하지 않습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_add_fd`

---

### zlink_poller_remove_timer

폴러에서 타이머를 제거합니다.

```c
zlink_config_result_t zlink_poller_remove_timer (void *poller_, void *timer_);
```

타이머의 등록을 해제합니다. 더 이상 폴러 이벤트를 생성하지 않습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_add_timer`

---

### zlink_poller_wait

단일 이벤트를 기다립니다.

```c
int zlink_poller_wait (void *poller_, zlink_poller_event_t *event_, long timeout_, zlink_config_result_t *error_out_);
```

등록된 소스 중 하나에 이벤트가 준비되거나 타임아웃이 만료될 때까지
블록합니다. 성공 시 `event_`에 이벤트 세부 정보가 채워집니다. 실패 시
`*error_out_`에 설정 결과(`zlink_config_result_t`)가 기록되고, 성공 시
기본 결과값이 반환됩니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `poller_` | 폴러 핸들 |
| `event_` | 채울 단일 이벤트 구조체 포인터 |
| `timeout_` | 최대 대기 시간(밀리초); `0`이면 즉시, `-1`이면 무한 대기 |
| `error_out_` | 실패 또는 타임아웃 시 `zlink_config_result_t`가 기록됨 |

**반환값:** 이벤트를 수신한 경우 `0`, 타임아웃 또는 실패 시 `-1`이며
`*error_out_`에 `zlink_config_result_t`가 기록됩니다(타임아웃 시 `EAGAIN`).
`zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_wait_all`

---

### zlink_poller_wait_all

단일 호출로 여러 이벤트를 기다립니다.

```c
int zlink_poller_wait_all (void *poller_,
                           zlink_poller_event_t *events_,
                           int n_events_,
                           long timeout_,
                           zlink_config_result_t *error_out_);
```

등록된 소스 중 하나 이상에 이벤트가 준비될 때까지 블록한 후, `events_`에
최대 `n_events_`개의 이벤트를 채웁니다. 실패 시 `*error_out_`에 설정
결과(`zlink_config_result_t`)가 기록되고, 성공 시 이벤트 수가 기본 반환값으로
반환됩니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `poller_` | 폴러 핸들 |
| `events_` | 채울 이벤트 구조체 배열 |
| `n_events_` | 반환할 최대 이벤트 수 |
| `timeout_` | 최대 대기 시간(밀리초); `0`이면 즉시, `-1`이면 무한 대기 |
| `error_out_` | 실패 또는 타임아웃 시 `zlink_config_result_t`가 기록됨 |

**반환값:** `events_`에 저장된 이벤트 수, 실패 시 `-1`이며 `*error_out_`에
`zlink_config_result_t`가 기록됩니다(타임아웃 시 `EAGAIN`). `zlink_errno()`는
진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 동일한 폴러에서 다른 작업과 동시에 호출해서는 안 됩니다.

**참고:** `zlink_poller_wait`

---

## 함수 -- 프록시

### zlink_proxy

프론트엔드와 백엔드 소켓 간의 내장 프록시를 시작합니다.

```c
zlink_config_result_t zlink_proxy (void *frontend_, void *backend_, void *capture_);
```

프론트엔드 소켓을 백엔드 소켓에 연결하여 양방향으로 메시지를 전달합니다.
`capture_`가 `NULL`이 아닌 경우 모든 메시지가 로깅 또는 검사를 위해 캡처
소켓으로도 전송됩니다. 이 호출은 (컨텍스트가 종료될 때까지) 영구적으로
블록하며 정상 작동 중에는 반환하지 않습니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `frontend_` | 클라이언트를 향하는 소켓 |
| `backend_` | 워커/서비스를 향하는 소켓 |
| `capture_` | 메시지 캡처용 선택적 소켓, 또는 `NULL` |

**반환값:** 프록시 종료 시 `zlink_config_result_t` 값을 반환합니다. 컨텍스트
종료로 정상 종료되면 내부적으로 `ETERM`이 `zlink_errno()`에 설정됩니다.

**에러:**
- `ETERM` -- 컨텍스트가 종료되었습니다.

**스레드 안전성:** 프록시가 실행 중인 동안 세 소켓 핸들은 다른 스레드에서
사용해서는 안 됩니다.

**참고:** `zlink_proxy_steerable`

---

### zlink_proxy_steerable

추가 제어 소켓이 있는 제어 가능 프록시를 시작합니다.

```c
zlink_config_result_t zlink_proxy_steerable (void *frontend_,
                                             void *backend_,
                                             void *capture_,
                                             void *control_);
```

`zlink_proxy`처럼 동작하지만 `control_`에서 명령을 수신합니다. 메시지 전달을
일시 중지하려면 `PAUSE` 문자열을, 계속하려면 `RESUME`을, 프록시를 종료하고
반환하려면 `TERMINATE`를 전송합니다. `control_`이 `NULL`이면 이 함수는
`zlink_proxy`와 동일하게 동작합니다.

**매개변수:**

| 이름 | 설명 |
|------|------|
| `frontend_` | 클라이언트를 향하는 소켓 |
| `backend_` | 워커/서비스를 향하는 소켓 |
| `capture_` | 메시지 캡처용 선택적 소켓, 또는 `NULL` |
| `control_` | 제어 소켓 (PAIR 타입), 또는 `NULL` |

**반환값:** 성공 시 `ZLINK_CONFIG_OK`. 실패 시에는 `zlink_config_result_t`
값을 반환합니다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지됩니다.

**스레드 안전성:** 프록시가 실행 중인 동안 네 소켓 핸들은 다른 스레드에서
사용해서는 안 됩니다. 제어 소켓은 모든 스레드에서 쓸 수 있습니다.

**참고:** `zlink_proxy`

---

## 함수 -- 기능 확인

### zlink_has

라이브러리가 지정된 기능을 지원하는지 확인합니다.

```c
bool zlink_has (const char *capability_);
```

라이브러리에 명명된 기능에 대한 컴파일 타임 또는 런타임 지원을 쿼리합니다.
일반적인 기능 문자열에는 `"ipc"`, `"tls"`, `"ws"`, `"wss"`가 포함됩니다.

**반환값:** 기능이 지원되면 `true`, 그렇지 않으면 `false`.

**스레드 안전성:** 언제든지 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_version`
