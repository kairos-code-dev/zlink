[English](socket.md) | [한국어](socket.ko.md)

# 소켓 API 레퍼런스

소켓 API는 zlink 소켓의 생성, 구성, 바인딩, 연결, I/O 수행을 위한 함수를
제공합니다. 소켓은 데이터 송수신의 주요 인터페이스이며 게시/구독, 요청/응답,
파이프라인을 포함한 여러 메시징 패턴을 지원합니다.

## 상수

### 소켓 타입

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PAIR` | 0 | 배타적 쌍 (양방향 일대일) |
| `ZLINK_PUB` | 1 | 퍼블리셔 (일대다 브로드캐스트) |
| `ZLINK_SUB` | 2 | 서브스크라이버 (퍼블리셔로부터 수신) |
| `ZLINK_DEALER` | 5 | 비동기 요청/응답 클라이언트 |
| `ZLINK_ROUTER` | 6 | 비동기 요청/응답 서버 (아이덴티티 라우팅) |
| `ZLINK_XPUB` | 9 | 확장 퍼블리셔 (구독 메시지 수신) |
| `ZLINK_XSUB` | 10 | 확장 서브스크라이버 (구독 메시지 송신) |
| `ZLINK_STREAM` | 11 | 원시 TCP 스트림 소켓 |

### 송수신 플래그

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_DONTWAIT` | 1 | 논블로킹 작업; 작업이 블로킹될 경우 즉시 `EAGAIN`과 함께 반환 |
| `ZLINK_SNDMORE` | 2 | 멀티파트 메시지에서 더 많은 메시지 파트가 뒤따를 것임을 나타냄 |

### 보안 메커니즘

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_NULL` | 0 | 보안 메커니즘 없음 (기본값) |
| `ZLINK_PLAIN` | 1 | PLAIN 사용자명/비밀번호 인증 |

### 소켓 옵션

소켓 옵션은 `zlink_setsockopt()`으로 설정하고 `zlink_getsockopt()`으로
조회합니다. 아래 테이블은 카테고리별로 그룹화된 모든 옵션 상수를 나열합니다.

#### 일반

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_AFFINITY` | 4 | I/O 스레드 어피니티 비트마스크 (`uint64_t`) |
| `ZLINK_ROUTING_ID` | 5 | ROUTER 주소 지정을 위한 소켓 아이덴티티 (`binary`, 최대 255바이트) |
| `ZLINK_TYPE` | 16 | 소켓 타입 (읽기 전용, `int`) |
| `ZLINK_LINGER` | 17 | 소켓 종료 시 대기 기간 (밀리초, `int`; -1 = 무한, 0 = 즉시 폐기) |
| `ZLINK_BACKLOG` | 19 | 대기 중인 연결 큐의 최대 길이 (`int`) |
| `ZLINK_LAST_ENDPOINT` | 32 | 마지막으로 바인딩된 엔드포인트 (읽기 전용, `string`) |
| `ZLINK_FD` | 14 | 외부 이벤트 루프 통합을 위한 파일 디스크립터 (읽기 전용, `zlink_fd_t`) |
| `ZLINK_EVENTS` | 15 | 이벤트 상태 비트마스크: `ZLINK_POLLIN`, `ZLINK_POLLOUT` (읽기 전용, `int`) |
| `ZLINK_RCVMORE` | 13 | 더 많은 메시지 파트 대기 중 (읽기 전용, `int`; 1 = 예) |

#### 하이 워터 마크

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SNDHWM` | 23 | 송신 하이 워터 마크; 송신 대기열의 최대 메시지 수 (`int`; 0 = 무제한) |
| `ZLINK_RCVHWM` | 24 | 수신 하이 워터 마크; 수신 대기열의 최대 메시지 수 (`int`; 0 = 무제한) |
| `ZLINK_MAXMSGSIZE` | 22 | 최대 인바운드 메시지 크기 (바이트, `int64_t`; -1 = 무제한) |

#### 버퍼

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SNDBUF` | 11 | 커널 송신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |
| `ZLINK_RCVBUF` | 12 | 커널 수신 버퍼 크기 (바이트, `int`; 0 = OS 기본값) |

#### 타이밍

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_RCVTIMEO` | 27 | 수신 타임아웃 (밀리초, `int`; -1 = 무한) |
| `ZLINK_SNDTIMEO` | 28 | 송신 타임아웃 (밀리초, `int`; -1 = 무한) |
| `ZLINK_RECONNECT_IVL` | 18 | 초기 재연결 간격 (밀리초, `int`) |
| `ZLINK_RECONNECT_IVL_MAX` | 21 | 최대 재연결 간격 (밀리초, `int`; 0 = `RECONNECT_IVL`만 사용) |
| `ZLINK_CONNECT_TIMEOUT` | 79 | 연결 타임아웃 (밀리초, `int`) |
| `ZLINK_TCP_MAXRT` | 80 | 최대 TCP 재전송 타임아웃 (밀리초, `int`) |
| `ZLINK_HANDSHAKE_IVL` | 66 | ZMTP 핸드셰이크 타임아웃 (밀리초, `int`) |

#### TCP

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_TCP_KEEPALIVE` | 34 | SO_KEEPALIVE 재정의 (`int`; -1 = OS 기본값, 0 = 끄기, 1 = 켜기) |
| `ZLINK_TCP_KEEPALIVE_CNT` | 35 | TCP_KEEPCNT 재정의 (`int`; -1 = OS 기본값) |
| `ZLINK_TCP_KEEPALIVE_IDLE` | 36 | TCP_KEEPIDLE 재정의 (초, `int`; -1 = OS 기본값) |
| `ZLINK_TCP_KEEPALIVE_INTVL` | 37 | TCP_KEEPINTVL 재정의 (초, `int`; -1 = OS 기본값) |

#### Pub/Sub

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SUBSCRIBE` | 6 | 토픽 접두사 구독 (`binary`) |
| `ZLINK_UNSUBSCRIBE` | 7 | 토픽 접두사 구독 해제 (`binary`) |
| `ZLINK_XPUB_VERBOSE` | 40 | 모든 구독 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_XPUB_NODROP` | 69 | HWM에서 메시지를 자동 삭제하지 않고 `EAGAIN` 반환 (`int`; 0 또는 1) |
| `ZLINK_XPUB_MANUAL` | 71 | XPUB에서 수동 구독 관리 활성화 (`int`; 0 또는 1) |
| `ZLINK_XPUB_WELCOME_MSG` | 72 | 새 서브스크라이버 연결 시 전송되는 메시지 (`binary`) |
| `ZLINK_XPUB_VERBOSER` | 78 | 모든 구독 및 구독 해제 메시지를 업스트림으로 전달 (`int`; 0 또는 1) |
| `ZLINK_XPUB_MANUAL_LAST_VALUE` | 98 | 수동 XPUB 모드에서 최신 값 캐싱 활성화 (`int`; 0 또는 1) |
| `ZLINK_INVERT_MATCHING` | 74 | 토픽 매칭 반전: 구독과 일치하지 않는 메시지 전달 (`int`; 0 또는 1) |
| `ZLINK_CONFLATE` | 54 | 토픽당 가장 최근 메시지만 유지 (`int`; 0 또는 1) |
| `ZLINK_ONLY_FIRST_SUBSCRIBE` | 108 | 토픽 접두사당 첫 번째 구독만 처리 (`int`; 0 또는 1) |
| `ZLINK_TOPICS_COUNT` | 116 | 구독된 토픽 수 (읽기 전용, `int`) |

#### Router

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_ROUTER_MANDATORY` | 33 | 연결되지 않은 피어로 라우팅 시 `EHOSTUNREACH` 반환 (`int`; 0 또는 1) |
| `ZLINK_ROUTER_HANDOVER` | 56 | 새 연결이 기존 라우팅 아이덴티티를 인수하도록 허용 (`int`; 0 또는 1) |
| `ZLINK_PROBE_ROUTER` | 51 | 연결 시 빈 메시지를 보내 ROUTER 피어에서 아이덴티티 설정 (`int`; 0 또는 1) |

#### 하트비트

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_HEARTBEAT_IVL` | 75 | ZMTP 하트비트 간격 (밀리초, `int`; 0 = 비활성화) |
| `ZLINK_HEARTBEAT_TTL` | 76 | ZMTP 하트비트 TTL (밀리초, `int`) |
| `ZLINK_HEARTBEAT_TIMEOUT` | 77 | ZMTP 하트비트 타임아웃 (밀리초, `int`) |

#### TLS

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_TLS_CERT` | 95 | PEM 인코딩된 TLS 인증서 경로 (`string`) |
| `ZLINK_TLS_KEY` | 96 | PEM 인코딩된 TLS 개인 키 경로 (`string`) |
| `ZLINK_TLS_CA` | 97 | PEM 인코딩된 CA 인증서 번들 경로 (`string`) |
| `ZLINK_TLS_VERIFY` | 98 | TLS 피어 인증서 검증 활성화 (`int`; 0 또는 1) |
| `ZLINK_TLS_REQUIRE_CLIENT_CERT` | 99 | 서버 소켓에서 TLS 클라이언트 인증서 요구 (`int`; 0 또는 1) |
| `ZLINK_TLS_HOSTNAME` | 100 | TLS SNI 및 인증서 검증을 위한 예상 호스트명 (`string`) |
| `ZLINK_TLS_TRUST_SYSTEM` | 101 | 시스템 CA 인증서 저장소 신뢰 (`int`; 0 또는 1) |
| `ZLINK_TLS_PASSWORD` | 102 | 암호화된 TLS 개인 키의 비밀번호 (`string`) |

#### 기타

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_IPV6` | 42 | 소켓에서 IPv6 활성화 (`int`; 0 또는 1) |
| `ZLINK_IMMEDIATE` | 39 | 완료된 연결에만 메시지 대기열 사용 (`int`; 0 또는 1) |
| `ZLINK_BLOCKY` | 70 | 레거시 옵션: context 종료 시 블로킹 (`int`; 0 또는 1) |
| `ZLINK_USE_FD` | 89 | 새로 생성하는 대신 미리 생성된 파일 디스크립터 사용 (`int`) |
| `ZLINK_BINDTODEVICE` | 92 | 소켓을 특정 네트워크 인터페이스에 바인딩 (`string`) |
| `ZLINK_CONNECT_ROUTING_ID` | 61 | 다음 발신 연결에 사용할 라우팅 아이덴티티 설정 (`binary`) |
| `ZLINK_RATE` | 8 | 멀티캐스트 데이터 전송률 (kbps, `int`) |
| `ZLINK_RECOVERY_IVL` | 9 | 멀티캐스트 복구 간격 (밀리초, `int`) |
| `ZLINK_MULTICAST_HOPS` | 25 | 최대 멀티캐스트 홉 수 (TTL) (`int`) |
| `ZLINK_TOS` | 57 | IP Type-of-Service 값 (`int`) |
| `ZLINK_MULTICAST_MAXTPDU` | 84 | 최대 멀티캐스트 전송 데이터 유닛 크기 (바이트, `int`) |
| `ZLINK_ZMP_METADATA` | 117 | 발신 연결에 ZMP 메타데이터 속성 첨부 (`binary`) |
| `ZLINK_REQUEST_TIMEOUT` | 90 | REQ 소켓의 요청 타임아웃 (밀리초, `int`) |
| `ZLINK_REQUEST_CORRELATE` | 91 | REQ 소켓에서 엄격한 요청-응답 상관관계 활성화 (`int`; 0 또는 1) |

## 함수

### zlink_socket

소켓을 생성합니다.

```c
void *zlink_socket (void *context_, int type_);
```

지정된 context 내에서 새 소켓을 생성합니다. `type_` 매개변수는 메시징 패턴을
선택합니다 (`ZLINK_PAIR`, `ZLINK_PUB`, `ZLINK_SUB`, `ZLINK_DEALER`,
`ZLINK_ROUTER` 등). 반환된 핸들은 이후 모든 소켓 작업에 사용됩니다. 소켓은
context가 종료되기 전에 `zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 소켓 핸들, 실패 시 `NULL` (errno가 설정됨).

**에러:** 소켓 타입이 유효하지 않으면 `EINVAL`. 최대 소켓 수에 도달하면
`EMFILE`. Context가 종료된 경우 `ETERM`.

**스레드 안전성:** Context에 대해 스레드 안전합니다.

**참고:** `zlink_close`, `zlink_ctx_new`

---

### zlink_close

소켓을 닫고 리소스를 해제합니다.

```c
int zlink_close (void *s_);
```

소켓을 닫고 관련된 모든 리소스를 해제합니다. 송신 대기열에 남아 있는 메시지는
`ZLINK_LINGER` 설정에 따라 폐기되거나 송신됩니다. 이 호출 후 소켓 핸들은
유효하지 않습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들이 유효한 소켓이 아니면 `ENOTSOCK`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_socket`

---

### zlink_setsockopt

소켓 옵션을 설정합니다.

```c
int zlink_setsockopt (void *s_, int option_, const void *optval_, size_t optvallen_);
```

소켓 옵션을 구성합니다. `option_` 매개변수는 옵션을 식별합니다 (예:
`ZLINK_SNDHWM`, `ZLINK_LINGER`, `ZLINK_SUBSCRIBE`). `optval_` 포인터는 값을
제공하고 `optvallen_`은 크기를 바이트 단위로 지정합니다. 정수형 옵션의 경우
`int`에 대한 포인터를 전달하고 `optvallen_`을 `sizeof(int)`로 설정합니다.
문자열/바이너리 옵션의 경우 데이터 포인터와 길이를 전달합니다.

일부 옵션은 소켓을 바인딩하거나 연결하기 전에 설정해야 합니다. 각 옵션의 예상
타입과 의미는 위의 소켓 옵션 테이블을 참조하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 옵션이 알 수 없거나 값이 범위를 벗어나면 `EINVAL`. Context가 종료된
경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_getsockopt`

---

### zlink_getsockopt

소켓 옵션을 조회합니다.

```c
int zlink_getsockopt (void *s_, int option_, void *optval_, size_t *optvallen_);
```

소켓 옵션의 현재 값을 가져옵니다. 호출자는 버퍼 `optval_`을 제공하고
`optvallen_`을 통해 크기를 전달합니다. 성공 시 `optvallen_`은 실제 기록된
크기를 반영하도록 업데이트됩니다. 정수형 옵션의 경우 `optval_`은 `int`를
가리켜야 하며 `*optvallen_`은 최소 `sizeof(int)` 이상이어야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 옵션이 알 수 없거나 버퍼가 너무 작으면 `EINVAL`. Context가 종료된
경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_setsockopt`

---

### zlink_bind

소켓을 주소에 바인딩합니다.

```c
int zlink_bind (void *s_, const char *addr_);
```

소켓을 로컬 엔드포인트에 바인딩합니다. 엔드포인트 문자열은
`transport://address` 형식을 사용하며, 지원되는 트랜스포트는 다음과 같습니다:

- `tcp://interface:port` 또는 `tcp://*:port`
- `inproc://name` (프로세스 내)
- `ipc://pathname` (프로세스 간, POSIX 전용)
- `ws://interface:port` (WebSocket)
- `tls://interface:port` (TLS 암호화 TCP)

소켓은 여러 엔드포인트에 바인딩할 수 있습니다. TCP의 경우 포트 0을 지정하면
시스템이 임시 포트를 할당합니다. 실제 엔드포인트를 가져오려면
`ZLINK_LAST_ENDPOINT`를 사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 주소가 이미 사용 중이면 `EADDRINUSE`. 인터페이스가 존재하지 않으면
`EADDRNOTAVAIL`. 트랜스포트가 지원되지 않으면 `EPROTONOSUPPORT`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_connect`, `zlink_unbind`

---

### zlink_connect

소켓을 원격 주소에 연결합니다.

```c
int zlink_connect (void *s_, const char *addr_);
```

소켓을 원격 엔드포인트에 연결합니다. 엔드포인트 형식은 `zlink_bind()`와
동일합니다. 소켓은 여러 엔드포인트에 연결할 수 있으며, 피어가 사용 불가능해지면
라이브러리가 자동으로 재연결을 처리합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 트랜스포트가 지원되지 않으면 `EPROTONOSUPPORT`. 트랜스포트가 소켓
타입과 호환되지 않으면 `ENOCOMPATPROTO`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_bind`, `zlink_disconnect`

---

### zlink_unbind

소켓의 주소 바인딩을 해제합니다.

```c
int zlink_unbind (void *s_, const char *addr_);
```

이전에 설정된 바인딩을 제거합니다. `addr_` 문자열은 원래 `zlink_bind()` 호출에서
사용된 엔드포인트(또는 임시 포트의 경우 `ZLINK_LAST_ENDPOINT`에서 가져온 값)와
일치해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 엔드포인트가 바인딩되지 않은 경우 `ENOENT`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_bind`

---

### zlink_disconnect

소켓의 원격 주소 연결을 해제합니다.

```c
int zlink_disconnect (void *s_, const char *addr_);
```

이전에 설정된 연결을 제거합니다. `addr_` 문자열은 원래 `zlink_connect()` 호출에서
사용된 엔드포인트와 일치해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 엔드포인트가 연결되지 않은 경우 `ENOENT`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_connect`

---

### zlink_send

소켓에서 버퍼 데이터를 송신합니다.

```c
int zlink_send (void *s_, const void *buf_, size_t len_, int flags_);
```

소켓 `s_`에서 `buf_`의 `len_` 바이트를 송신합니다. 데이터는 전송 전에 내부
메시지로 복사됩니다. `flags_` 매개변수는 0, `ZLINK_DONTWAIT`, `ZLINK_SNDMORE`,
또는 이들의 비트 조합일 수 있습니다. 멀티파트 메시지를 보내려면
`ZLINK_SNDMORE`를 사용하세요. 마지막 파트에서만 이 플래그를 생략합니다.

**반환값:** 성공 시 송신된 바이트 수, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. Context가
종료된 경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_send_const`, `zlink_recv`, `zlink_msg_send`

---

### zlink_send_const

소켓에서 상수 데이터를 송신합니다 (제로카피 힌트).

```c
int zlink_send_const (void *s_, const void *buf_, size_t len_, int flags_);
```

`zlink_send()`와 동일하게 동작하지만 `buf_`가 상수, 불변 데이터(예: 문자열
리터럴 또는 정적 버퍼)를 가리킨다는 것을 라이브러리에 알립니다. 라이브러리는
가능한 경우 내부적으로 데이터 복사를 피하여 자주 송신하는 상수 페이로드의
성능을 향상시킬 수 있습니다. 호출자는 `buf_`가 프로그램의 수명 동안 유효하고
변경되지 않도록 보장해야 합니다.

**반환값:** 성공 시 송신된 바이트 수, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. Context가
종료된 경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_send`, `zlink_msg_init_data`

---

### zlink_recv

소켓에서 데이터를 수신합니다.

```c
int zlink_recv (void *s_, void *buf_, size_t len_, int flags_);
```

소켓 `s_`에서 `buf_`로 최대 `len_` 바이트를 수신합니다. 수신 메시지가 `len_`보다
크면 자동으로 잘리며 반환값은 여전히 원래 메시지 크기를 반영합니다(`len_`을
초과). 잘림을 감지하려면 반환값을 `len_`과 비교하세요. `flags_` 매개변수는
0 또는 `ZLINK_DONTWAIT`일 수 있습니다.

**반환값:** 성공 시 원래 메시지의 바이트 수 (잘린 경우 `len_`을 초과할 수 있음),
실패 시 -1 (errno가 설정됨).

**에러:** 사용 가능한 메시지가 없고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`.
Context가 종료된 경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_send`, `zlink_msg_recv`

---

### zlink_socket_monitor

inproc 주소를 통해 소켓 모니터를 시작합니다 (레거시).

```c
int zlink_socket_monitor (void *s_, const char *addr_, int events_);
```

소켓 이벤트 모니터링을 시작하고 지정된 inproc 엔드포인트 `addr_`에 이벤트를
발행합니다. 다른 소켓(일반적으로 `ZLINK_PAIR`)이 `addr_`에 연결하여 이벤트
알림을 수신할 수 있습니다. `events_` 매개변수는 모니터링할 이벤트를 선택하는
`ZLINK_EVENT_*` 상수의 비트마스크입니다. 모든 이벤트를 모니터링하려면
`ZLINK_EVENT_ALL`을 전달합니다.

이것은 레거시 모니터링 인터페이스입니다. 새 코드에서는 별도의 inproc 소켓이
필요 없는 직접 모니터 핸들을 반환하는 `zlink_socket_monitor_open()`을
사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_socket_monitor_open`

---

### zlink_socket_monitor_open

소켓 모니터 핸들을 직접 열고 반환합니다.

```c
void *zlink_socket_monitor_open (void *s_, int events_);
```

소켓 `s_`에 대한 모니터를 생성하고, inproc 엔드포인트 없이
`zlink_monitor_recv()`로 직접 이벤트를 수신할 수 있는 핸들을 반환합니다.
`events_` 매개변수는 `ZLINK_EVENT_*` 상수의 비트마스크입니다. 반환된 핸들은
더 이상 필요하지 않을 때 `zlink_close()`로 닫아야 합니다.

**반환값:** 성공 시 모니터 핸들, 실패 시 `NULL` (errno가 설정됨).

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_socket_monitor`, `zlink_monitor_recv`
