[English](errors.md) | [한국어](errors.ko.md)

# 에러 처리 & 버전

에러 정보를 조회하고 런타임에 라이브러리 버전을 확인하는 함수입니다.
에러 코드는 POSIX `errno` 규칙을 따르며, zlink는 `ZLINK_HAUSNUMERO`를 기반으로
자체 코드를 추가 정의합니다.

## 에러 코드 상수

zlink는 시스템 정의 `errno` 코드와의 충돌을 방지하기 위해 높은 기본값을
사용합니다:

```c
#define ZLINK_HAUSNUMERO 156384712
```

### Windows에서 제공되는 POSIX 코드

특정 POSIX 에러 코드를 기본적으로 정의하지 않는 플랫폼(특히 Windows)에서는
zlink가 `ZLINK_HAUSNUMERO`를 기준으로 해당 코드를 정의합니다. POSIX 시스템에서는
표준 값이 직접 사용됩니다.

| 상수 | 값 | 의미 |
|------|-----|------|
| `ENOTSUP` | ZLINK_HAUSNUMERO + 1 | 지원하지 않는 작업 |
| `EPROTONOSUPPORT` | ZLINK_HAUSNUMERO + 2 | 지원하지 않는 프로토콜 |
| `ENOBUFS` | ZLINK_HAUSNUMERO + 3 | 사용 가능한 버퍼 공간 없음 |
| `ENETDOWN` | ZLINK_HAUSNUMERO + 4 | 네트워크 다운 |
| `EADDRINUSE` | ZLINK_HAUSNUMERO + 5 | 주소가 이미 사용 중 |
| `EADDRNOTAVAIL` | ZLINK_HAUSNUMERO + 6 | 주소를 사용할 수 없음 |
| `ECONNREFUSED` | ZLINK_HAUSNUMERO + 7 | 연결 거부됨 |
| `EINPROGRESS` | ZLINK_HAUSNUMERO + 8 | 작업 진행 중 |
| `ENOTSOCK` | ZLINK_HAUSNUMERO + 9 | 소켓이 아님 |
| `EMSGSIZE` | ZLINK_HAUSNUMERO + 10 | 메시지가 너무 김 |
| `EAFNOSUPPORT` | ZLINK_HAUSNUMERO + 11 | 지원하지 않는 주소 체계 |
| `ENETUNREACH` | ZLINK_HAUSNUMERO + 12 | 네트워크에 도달할 수 없음 |
| `ECONNABORTED` | ZLINK_HAUSNUMERO + 13 | 연결 중단됨 |
| `ECONNRESET` | ZLINK_HAUSNUMERO + 14 | 연결 재설정됨 |
| `ENOTCONN` | ZLINK_HAUSNUMERO + 15 | 연결되지 않음 |
| `ETIMEDOUT` | ZLINK_HAUSNUMERO + 16 | 연결 시간 초과 |
| `EHOSTUNREACH` | ZLINK_HAUSNUMERO + 17 | 호스트에 도달할 수 없음 |
| `ENETRESET` | ZLINK_HAUSNUMERO + 18 | 네트워크 재설정 |

### zlink 전용 에러 코드

이 코드들은 항상 정의되며 POSIX 값과 절대 겹치지 않습니다:

| 상수 | 값 | 의미 |
|------|-----|------|
| `EFSM` | ZLINK_HAUSNUMERO + 51 | 현재 상태에서 작업을 수행할 수 없음 (유한 상태 머신 에러) |
| `ENOCOMPATPROTO` | ZLINK_HAUSNUMERO + 52 | 호환되는 프로토콜 없음 |
| `ETERM` | ZLINK_HAUSNUMERO + 53 | Context가 종료되었습니다 |
| `EMTHREAD` | ZLINK_HAUSNUMERO + 54 | 사용 가능한 스레드 없음 |

## 버전 매크로

`<zlink.h>`에 정의된 다음 매크로를 통해 컴파일 시점 버전 감지가 가능합니다:

```c
#define ZLINK_VERSION_MAJOR 1
#define ZLINK_VERSION_MINOR 0
#define ZLINK_VERSION_PATCH 0

#define ZLINK_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

#define ZLINK_VERSION \
    ZLINK_MAKE_VERSION(ZLINK_VERSION_MAJOR, ZLINK_VERSION_MINOR, ZLINK_VERSION_PATCH)
```

컴파일 시점 버전 가드에 `ZLINK_VERSION`과 `ZLINK_MAKE_VERSION`을 사용합니다:

```c
#if ZLINK_VERSION >= ZLINK_MAKE_VERSION(1, 1, 0)
    /* use features introduced in 1.1.0 */
#endif
```

## 함수

### zlink_errno

호출 스레드의 errno 값을 반환합니다.

```c
int zlink_errno(void);
```

각 스레드는 자체 에러 번호를 유지합니다. zlink 함수가 실패 표시(일반적으로
`-1` 또는 `NULL`)를 반환한 후 `zlink_errno()`를 호출하여 구체적인 에러 코드를
얻습니다. 값은 표준 POSIX errno이거나 위에 나열된 `ZLINK_HAUSNUMERO` 기반
확장 코드 중 하나입니다.

**반환값:** 현재 스레드 로컬 errno 값.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다. 각 스레드는
독립적인 에러 번호를 가집니다.

**참고:** `zlink_strerror`

---

### zlink_strerror

주어진 에러 번호를 설명하는 사람이 읽을 수 있는 문자열을 반환합니다.

```c
const char *zlink_strerror(int errnum_);
```

표준 POSIX 에러 코드와 zlink 전용 코드(`EFSM`, `ETERM` 등)를 설명 문자열로
변환합니다. 반환된 포인터는 정적 저장소를 가리키며 수정하거나 해제해서는
안 됩니다.

**반환값:** 정적, null 종료 문자열에 대한 포인터.

**스레드 안전성:** 모든 스레드에서 안전하게 호출할 수 있습니다. 반환된 문자열은
정적으로 할당됩니다.

**참고:** `zlink_errno`

---

### zlink_version

런타임 라이브러리 버전을 조회합니다.

```c
void zlink_version(int *major_, int *minor_, int *patch_);
```

링크된 라이브러리 버전의 major, minor, patch 구성 요소를 제공된 출력 포인터에
기록합니다. 이를 통해 애플리케이션은 런타임에 로드된 라이브러리가 컴파일 시
사용된 헤더와 호환되는지 확인할 수 있습니다.

**반환값:** 없음 (출력은 포인터 매개변수를 통해 기록됩니다).

**스레드 안전성:** 언제든지 모든 스레드에서 안전하게 호출할 수 있습니다.

**참고:** `ZLINK_VERSION_MAJOR`, `ZLINK_VERSION_MINOR`, `ZLINK_VERSION_PATCH`, `ZLINK_MAKE_VERSION`
