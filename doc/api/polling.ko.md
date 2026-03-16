[English](polling.md) | [한국어](polling.ko.md)

# 프록시 & 유틸리티

메시지 전달 프록시를 구축하고 라이브러리 기능을 조회하기 위한 함수입니다.

callback-only receive 모델로 전환됨에 따라 polling API(`zlink_poll`,
`zlink_pollitem_t`, `ZLINK_POLLIN`)는 제거되었습니다. 모든 메시지 수신은
이제 handler callback을 통해 처리됩니다. 프록시 및 기능 확인 함수는 그대로
유지됩니다.

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
```

`zlink_fd_t`는 플랫폼 의존적 파일 디스크립터 타입입니다.

## 함수

### zlink_proxy

프론트엔드와 백엔드 소켓 간의 내장 프록시를 시작합니다.

```c
int zlink_proxy (void *frontend_, void *backend_, void *capture_);
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

**참고:** `zlink_proxy_steerable`

---

### zlink_proxy_steerable

추가 제어 소켓이 있는 제어 가능 프록시를 시작합니다.

```c
int zlink_proxy_steerable (void *frontend_,
                           void *backend_,
                           void *capture_,
                           void *control_);
```

`zlink_proxy`처럼 동작하지만 `control_`에서 명령을 수신합니다. 메시지 전달을
일시 중지하려면 `PAUSE` 문자열을, 계속하려면 `RESUME`을, 프록시를 종료하고
반환하려면 `TERMINATE`를 전송합니다. `control_`이 `NULL`이면 이 함수는
`zlink_proxy`와 동일하게 동작합니다.

**반환값:** 제어 소켓을 통해 종료 시 `0`, 실패 시 `-1` (errno가 설정됨).

**스레드 안전성:** 프록시가 실행 중인 동안 네 소켓 핸들은 다른 스레드에서
사용해서는 안 됩니다. 제어 소켓은 모든 스레드에서 쓸 수 있습니다.

**참고:** `zlink_proxy`

---

### zlink_has

라이브러리가 지정된 기능을 지원하는지 확인합니다.

```c
int zlink_has (const char *capability_);
```

라이브러리에 명명된 기능에 대한 컴파일 타임 또는 런타임 지원을 쿼리합니다.
일반적인 기능 문자열에는 `"ipc"`, `"tls"`, `"ws"`, `"wss"`가 포함됩니다.

**반환값:** 기능이 지원되면 `1`, 그렇지 않으면 `0`.

**스레드 안전성:** 언제든지 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_version`
