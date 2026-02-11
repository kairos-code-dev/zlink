[English](context.md) | [한국어](context.ko.md)

# Context

Context는 I/O 스레드를 관리하고 소켓 생성의 기반이 되는 최상위 컨테이너입니다.
모든 애플리케이션은 다른 zlink API를 사용하기 전에 최소한 하나의 context를
생성해야 합니다. Context는 스레드 안전하며 스레드 간에 공유할 수 있습니다.

## Context 옵션 상수

옵션은 `zlink_ctx_set`과 `zlink_ctx_get`으로 설정하고 조회합니다.

```c
#define ZLINK_IO_THREADS              1
#define ZLINK_MAX_SOCKETS             2
#define ZLINK_SOCKET_LIMIT            3
#define ZLINK_THREAD_PRIORITY         3
#define ZLINK_THREAD_SCHED_POLICY     4
#define ZLINK_MAX_MSGSZ               5
#define ZLINK_MSG_T_SIZE              6
#define ZLINK_THREAD_AFFINITY_CPU_ADD      7
#define ZLINK_THREAD_AFFINITY_CPU_REMOVE   8
#define ZLINK_THREAD_NAME_PREFIX      9
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_IO_THREADS` | 1 | Context의 I/O 스레드 수 |
| `ZLINK_MAX_SOCKETS` | 2 | 허용되는 최대 소켓 수 |
| `ZLINK_SOCKET_LIMIT` | 3 | 소켓 수의 하드 상한 (읽기 전용) |
| `ZLINK_THREAD_PRIORITY` | 3 | I/O 스레드 스케줄링 우선순위 |
| `ZLINK_THREAD_SCHED_POLICY` | 4 | I/O 스레드 스케줄링 정책 |
| `ZLINK_MAX_MSGSZ` | 5 | 최대 메시지 크기 (바이트 단위, -1 = 무제한) |
| `ZLINK_MSG_T_SIZE` | 6 | `zlink_msg_t`의 크기 (바이트 단위, 읽기 전용) |
| `ZLINK_THREAD_AFFINITY_CPU_ADD` | 7 | I/O 스레드 어피니티 집합에 CPU 추가 |
| `ZLINK_THREAD_AFFINITY_CPU_REMOVE` | 8 | I/O 스레드 어피니티 집합에서 CPU 제거 |
| `ZLINK_THREAD_NAME_PREFIX` | 9 | I/O 스레드 이름 접두사 |

## 기본값

```c
#define ZLINK_IO_THREADS_DFLT           2
#define ZLINK_MAX_SOCKETS_DFLT          1023
#define ZLINK_THREAD_PRIORITY_DFLT      -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT  -1
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_IO_THREADS_DFLT` | 2 | 기본 I/O 스레드 수 |
| `ZLINK_MAX_SOCKETS_DFLT` | 1023 | 기본 최대 소켓 수 |
| `ZLINK_THREAD_PRIORITY_DFLT` | -1 | 기본 스레드 우선순위 (OS 기본값) |
| `ZLINK_THREAD_SCHED_POLICY_DFLT` | -1 | 기본 스케줄링 정책 (OS 기본값) |

## 함수

### zlink_ctx_new

새 zlink context를 생성합니다.

```c
void *zlink_ctx_new(void);
```

기본 옵션 값으로 새 context를 할당하고 초기화합니다. Context는 I/O 스레드 풀을
관리하며 소켓 생성의 기반이 됩니다. 모든 소켓은 context와 연결되어야 합니다.
Context가 더 이상 필요하지 않으면 `zlink_ctx_term`으로 해제합니다.

**반환값:** 성공 시 context 핸들, 실패 시 `NULL` (errno가 설정됨).

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다. 반환된 context
핸들은 스레드 간에 공유할 수 있습니다.

**참고:** `zlink_ctx_term`, `zlink_ctx_set`

---

### zlink_ctx_term

Context를 종료하고 관련된 모든 리소스를 해제합니다.

```c
int zlink_ctx_term(void *context_);
```

Context를 파괴합니다. 이 호출은 context 내에서 생성된 모든 소켓이 닫힐 때까지
블로킹될 수 있습니다. Context에 속한 소켓의 블로킹 작업은 `zlink_ctx_shutdown`이
호출되거나 모든 소켓이 닫힌 후 `ETERM`을 반환합니다. 각 context는 정확히
한 번만 종료해야 합니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EFAULT` -- 유효하지 않은 context 핸들.
- `EINTR` -- 시그널에 의해 종료가 중단됨; 재시도할 수 있습니다.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있지만, context당 정확히
한 번만 호출해야 합니다. 이 호출이 반환된 후에는 context 핸들을 사용하지
마세요.

**참고:** `zlink_ctx_new`, `zlink_ctx_shutdown`

---

### zlink_ctx_shutdown

Context를 즉시 종료합니다.

```c
int zlink_ctx_shutdown(void *context_);
```

이 context에 속한 소켓의 모든 블로킹 작업이 `ETERM`과 함께 즉시 반환되도록
시그널을 보냅니다. 이것은 종료를 시작하지만 리소스를 해제하지 않는 논블로킹
호출입니다. 최종 정리를 위해 이후에 `zlink_ctx_term`을 호출해야 합니다.
term 전에 shutdown을 호출하면 여러 스레드에서 소켓을 사용할 때 데드락을
방지할 수 있습니다.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EFAULT` -- 유효하지 않은 context 핸들.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다.

**참고:** `zlink_ctx_term`

---

### zlink_ctx_set

Context 옵션을 설정합니다.

```c
int zlink_ctx_set(void *context_, int option_, int optval_);
```

소켓이 생성되기 전 또는 후에 context를 구성합니다. 일부 옵션(예:
`ZLINK_IO_THREADS`)은 소켓을 생성하기 전에 설정해야 적용됩니다. 유효한 옵션
이름과 의미는 위의 옵션 상수 테이블을 참조하세요.

**반환값:** 성공 시 `0`, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션 또는 유효하지 않은 값.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다.

**참고:** `zlink_ctx_get`

---

### zlink_ctx_get

Context 옵션을 조회합니다.

```c
int zlink_ctx_get(void *context_, int option_);
```

Context 옵션의 현재 값을 가져옵니다. `ZLINK_SOCKET_LIMIT` 및 `ZLINK_MSG_T_SIZE`
같은 읽기 전용 옵션을 포함하여 언제든지 context 구성을 검사하는 데 사용할 수
있습니다.

**반환값:** 성공 시 옵션 값, 실패 시 `-1` (errno가 설정됨).

**에러:**
- `EINVAL` -- 알 수 없는 옵션.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다.

**참고:** `zlink_ctx_set`
