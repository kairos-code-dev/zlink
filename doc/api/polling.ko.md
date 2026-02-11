[English](polling.md) | [한국어](polling.ko.md)

# 폴링 & 프록시

여러 소켓에 걸쳐 I/O를 다중화하고 메시지 전달 프록시를 구축하기 위한
함수입니다. 폴링 API를 사용하면 단일 호출에서 zlink 소켓과 네이티브 파일
디스크립터의 모든 조합에 대한 이벤트를 대기할 수 있습니다.

## 타입

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

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;
```

`zlink_fd_t`는 플랫폼 의존적 파일 디스크립터 타입입니다: 64비트 Windows에서는
`unsigned __int64`, 32비트 Windows에서는 `unsigned int`, POSIX 시스템에서는
`int`입니다.

`zlink_pollitem_t`는 폴링할 하나의 항목을 설명합니다. `socket`을 zlink 소켓
핸들로 설정하거나 (`socket`이 `NULL`인 경우) `fd`를 네이티브 파일 디스크립터로
설정합니다. `events`는 감시할 이벤트를 지정하고 `revents`는 `zlink_poll`이
실제로 발생한 이벤트로 채웁니다.

## 상수

```c
#define ZLINK_POLLIN   1
#define ZLINK_POLLOUT  2
#define ZLINK_POLLERR  4
#define ZLINK_POLLPRI  8

#define ZLINK_POLLITEMS_DFLT  16
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_POLLIN` | 1 | 블록 없이 최소 하나의 메시지를 수신할 수 있음 |
| `ZLINK_POLLOUT` | 2 | 블록 없이 최소 하나의 메시지를 전송할 수 있음 |
| `ZLINK_POLLERR` | 4 | 에러 상태가 존재함 |
| `ZLINK_POLLPRI` | 8 | 높은 우선순위 데이터가 사용 가능함 (원시 파일 디스크립터용) |
| `ZLINK_POLLITEMS_DFLT` | 16 | 폴 항목 배열에 권장되는 기본 할당 크기 |

## 함수

### zlink_poll

소켓 및/또는 파일 디스크립터 집합에 대한 이벤트를 폴링합니다.

```c
int zlink_poll(zlink_pollitem_t *items_, int nitems_, long timeout_);
```

최소 하나의 항목이 요청된 이벤트를 신호하거나 타임아웃이 만료될 때까지
대기합니다. `timeout_`을 무한 블로킹에는 `-1`, 즉시 비블로킹 확인에는 `0`,
최대 대기 밀리초에는 양수 값으로 설정합니다. 반환 시 각 항목의 `revents`
필드가 발생한 이벤트를 나타냅니다.

**반환값:** 이벤트가 신호된 항목 수, 이벤트 없이 타임아웃이 만료되면 `0`,
실패 시 `-1` (errno가 설정됨).

**에러:**
- `ETERM` -- 소켓 중 하나와 연결된 컨텍스트가 종료되었습니다.
- `EFAULT` -- `nitems_`가 0이 아닌데 `items_`가 `NULL`입니다.
- `EINTR` -- 시그널에 의해 호출이 중단되었습니다.

**스레드 안전성:** 각 폴 항목은 호출 중 다른 스레드와 공유해서는 안 됩니다.
서로 다른 스레드가 서로 다른 항목 집합을 동시에 폴링할 수 있습니다.

**참고:** `zlink_proxy`, `zlink_proxy_steerable`

---

### zlink_proxy

프론트엔드와 백엔드 소켓 간의 내장 프록시를 시작합니다.

```c
int zlink_proxy(void *frontend_, void *backend_, void *capture_);
```

프론트엔드 소켓을 백엔드 소켓에 연결하여 양방향으로 메시지를 전달합니다.
`capture_`가 `NULL`이 아닌 경우 모든 메시지가 로깅 또는 검사를 위해 캡처
소켓으로도 전송됩니다. 이 호출은 (컨텍스트가 종료될 때까지) 영구적으로
블록하며 정상 작동 중에는 반환하지 않습니다.

**반환값:** 프록시 종료 시 `-1` (errno가 `ETERM`으로 설정됨).

**에러:**
- `ETERM` -- 컨텍스트가 종료되었습니다.

**스레드 안전성:** 프록시가 실행 중인 동안 세 소켓 핸들은 다른 스레드에서
사용해서는 안 됩니다.

**참고:** `zlink_proxy_steerable`, `zlink_poll`

---

### zlink_proxy_steerable

추가 제어 소켓이 있는 제어 가능 프록시를 시작합니다.

```c
int zlink_proxy_steerable(void *frontend_,
                          void *backend_,
                          void *capture_,
                          void *control_);
```

`zlink_proxy`처럼 동작하지만 `control_`에서 명령을 수신합니다. 메시지 전달을
일시 중지하려면 `PAUSE` 문자열을, 계속하려면 `RESUME`을, 프록시를 종료하고
반환하려면 `TERMINATE`를 전송합니다. `control_`이 `NULL`이면 이 함수는
`zlink_proxy`와 동일하게 동작합니다.

**반환값:** 제어 소켓을 통해 종료 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `ETERM` -- 컨텍스트가 종료되었습니다.

**스레드 안전성:** 프록시가 실행 중인 동안 네 소켓 핸들은 다른 스레드에서
사용해서는 안 됩니다. 제어 소켓은 모든 스레드에서 쓸 수 있습니다.

**참고:** `zlink_proxy`, `zlink_poll`

---

### zlink_has

라이브러리가 지정된 기능을 지원하는지 확인합니다.

```c
int zlink_has(const char *capability_);
```

라이브러리에 명명된 기능에 대한 컴파일 타임 또는 런타임 지원을 쿼리합니다.
일반적인 기능 문자열에는 `"ipc"`, `"tls"`, `"ws"`, `"wss"`가 포함됩니다.

**반환값:** 기능이 지원되면 `1`, 그렇지 않으면 `0`.

**스레드 안전성:** 언제든지 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_version`
