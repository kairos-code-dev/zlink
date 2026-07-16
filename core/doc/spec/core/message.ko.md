[English](message.md) | [한국어](message.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 메시지 API 레퍼런스

> 주의:
> 이 문서는 메시지 생성과 multipart 처리를 다룹니다.
> request-reply와 service routing은 각 socket·service 정식 문서가 정의합니다.
> 공개 메시지 API는 request-reply 또는 service routing 상태를 노출하지 않습니다.

메시지 API는 zlink 메시지의 생성, 데이터 접근, 소유권, multipart 관리를 위한 함수를 제공합니다(송신/수신은 socket API 범위).
메시지는 소켓 간 데이터 교환의 기본 단위이며, 임의의 바이너리 payload를
전달하고, 제로카피 시맨틱을 지원하며, 멀티파트 시퀀스를 구성할 수 있습니다.

### 용어

| 용어 | 설명 |
|------|------|
| payload | 메시지가 운반하는 사용자 데이터 바이트 |
| multipart | 여러 프레임(part)을 하나의 논리적 메시지로 묶어 전송하는 방식 |
| zero-copy | 데이터를 복사하지 않고 포인터/참조만 전달하여 전송하는 기법 |
| reference count (refcount) | 같은 데이터 버퍼를 공유하는 메시지 핸들의 수. 0이 되면 버퍼를 해제한다 |
| routing_id | Router 소켓이 peer를 식별하는 데 사용하는 고유 바이트 열 (최대 255바이트) |
| control part | 상위 프로토콜이 payload 앞에 붙이는 내부 part |

## 타입

```c
typedef struct zlink_msg_t
{
    unsigned char _[64];
} zlink_msg_t;
```

`zlink_msg_t`는 64바이트 불투명 메시지 구조체입니다. 내부 레이아웃은
플랫폼에 따라 다르며 직접 접근해서는 안 됩니다. 공개 헤더는 플랫폼별 alignment(예:
64비트에서 8바이트)를 함께 선언합니다. 모든 메시지는 사용 전에 초기화하고 사용 후에
닫아야 합니다.

```c
typedef struct zlink_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zlink_routing_id_t;
```

`zlink_routing_id_t`는 `ROUTER` 소켓이 특정 peer에 주소를 지정하는 데 사용하는
라우팅 아이덴티티를 전달합니다. `size`는 `data`에서 유효한 바이트 수를 나타냅니다.

```c
typedef void (zlink_free_fn) (void *data_, void *hint_);
```

`zlink_free_fn`은 제로카피 메시지 생성을 위해 `zlink_msg_init_data()`에서
사용되는 콜백 타입입니다. 메시지 데이터 버퍼가 더 이상 필요하지 않을 때
라이브러리가 이 함수를 호출합니다.

## 상수

### 메타데이터 매크로

| 상수 | 값 | 의미 |
|------|------|------|
| `ZLINK_MSG_METADATA_KEY_USER_MIN` | `0x0100` | 사용자 정의 metadata 키의 최소값 |
| `ZLINK_MSG_METADATA_VALUE_MAX` | `65535` | metadata 값의 최대 바이트 길이 |

### 문자열 메타데이터 속성 (예약)

아래 키들은 `zlink_msg_gets()`용으로 예약된 식별자입니다.
10.0.0에서 `zlink_msg_gets()`는 모든 호출에 `NULL`과 `errno=EINVAL`을 반환합니다.
peer 상세 정보가 필요하면 socket monitor 이벤트 payload나 service snapshot/query
API를 사용하세요.

| 키 (예약) | 의미 |
|------|------|
| `"Socket-Type"` | peer의 소켓 타입 |
| `"Identity"` | peer 아이덴티티 |
| `"Peer-Address"` | peer 네트워크 주소 |

## 함수

### zlink_msg_init

빈 메시지를 초기화합니다.

```c
zlink_config_result_t zlink_msg_init (zlink_msg_t *msg_);
```

`msg_`를 빈 길이 0 메시지로 초기화합니다. 메시지는 최종적으로
`zlink_msg_close()`로 해제해야 합니다. `zlink_msg_t`를 다른 메시지 함수에
전달하기 전에 항상 초기화하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다. 각 `zlink_msg_t`는 한 번에 하나의
스레드에서만 사용해야 합니다.

**참고:** `zlink_msg_init_size`, `zlink_msg_init_data`, `zlink_msg_close`

---

### zlink_msg_init_size

지정된 크기의 메시지를 초기화합니다.

```c
zlink_config_result_t zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);
```

`size_` 바이트의 내부 버퍼를 할당하고 `msg_`를 초기화합니다. 버퍼 내용은
초기화되지 않습니다. `zlink_msg_data()`를 사용하여 버퍼에 대한 포인터를 얻고
송신 전에 데이터를 채우세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 할당 실패 시 `ENOMEM`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_data`, `zlink_msg_size`

---

### zlink_msg_init_data

외부 데이터 버퍼로부터 메시지를 초기화합니다 (제로카피).

```c
zlink_config_result_t zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);
```

호출자가 제공한 `size_` 바이트의 버퍼 `data_`를 복사하지 않고 참조하는 메시지를
생성합니다. 라이브러리가 더 이상 버퍼를 필요로 하지 않을 때(메시지가 송신되거나
닫힌 후) 호출자가 버퍼를 해제할 수 있도록 `data_`와 `hint_`를 인수로 콜백
`ffn_`을 호출합니다. `ffn_`이 `NULL`이면 콜백이 호출되지 않으며, 호출자는
버퍼가 메시지보다 오래 존재하도록 보장해야 합니다.

이 함수는 진정한 제로카피 메시지 전달을 가능하게 합니다. 호출자는 `ffn_`이
호출될 때까지 `data_`를 수정하거나 해제해서는 안 됩니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_free_fn`, `zlink_msg_data`

---

### zlink_msg_close

메시지 리소스를 해제합니다.

```c
zlink_config_result_t zlink_msg_close (zlink_msg_t *msg_);
```

메시지와 관련된 모든 리소스를 해제합니다. 초기화된 모든 메시지는 정확히 한 번
닫아야 합니다. 닫은 후 `zlink_msg_t` 구조체는 유효하지 않으며 재사용하기 전에
다시 초기화해야 합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_init`, `zlink_multipart_close`

---

### zlink_msg_move

소스에서 대상으로 메시지 내용을 이동합니다.

```c
zlink_config_result_t zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);
```

`src_`의 내용을 `dest_`로 이동합니다. 성공적인 이동 후 `src_`는 빈 메시지가
되고(새로 초기화된 메시지와 동일) `dest_`는 원래 내용을 포함합니다. `dest_`의
이전 내용은 해제됩니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_copy`

---

### zlink_msg_copy

메시지를 복사합니다.

```c
zlink_config_result_t zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);
```

`src_`의 내용을 `dest_`로 복사합니다. large/zero-copy storage는 두 메시지가
참조 카운팅으로 기본 데이터 버퍼를 공유하고, 작은 inline 메시지는 값으로
복사됩니다. `dest_`의 이전 내용은 해제됩니다. 복사는 경량이며 큰 데이터
payload를 복제하지 않습니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_move`, `zlink_msg_adopt`

---

### zlink_msg_adopt

별도의 init+move 단계 없이 source 메시지의 소유권을 인수합니다.

```c
zlink_config_result_t zlink_msg_adopt (zlink_msg_t *dest_, zlink_msg_t *src_);
```

이미 `dest_`에 대한 저장소를 보유하고 있고, 새로 수신한 네이티브 메시지의
소유권을 효율적으로 가져와야 하는 바인딩을 위한 함수입니다. `zlink_msg_move`와
달리 `dest_`는 현재 초기화된 메시지를 소유하지 않아야 합니다 — 이미 초기화된
`dest_`에 `zlink_msg_adopt`를 호출하면 정의되지 않은 동작이 발생합니다.

성공 시 `dest_`는 `src_`의 원래 내용을 소유하고, `src_`는 빈 초기화 상태의
메시지가 됩니다. `zlink_msg_adopt`가 내부적으로 `src_`를 빈 초기화 상태로
재설정하므로, 인수한 내용을 위해 `src_`를 별도로 close할 필요는 없습니다.
(이미 비워진 `src_`를 close해도 무해하지만 불필요합니다.)

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_move`, `zlink_msg_copy`

---

### zlink_msg_data

메시지 데이터 버퍼에 대한 포인터를 반환합니다.

```c
void *zlink_msg_data (zlink_msg_t *msg_);
```

메시지의 원시 데이터 payload에 대한 포인터를 반환합니다. 포인터는 메시지가
닫히거나, 이동되거나, 송신될 때까지 유효합니다. 메시지가 초기화되지 않은 경우
`NULL`을 반환합니다.

**반환값:** 메시지 데이터 버퍼에 대한 포인터.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_size`

---

### zlink_msg_size

메시지 데이터 크기를 바이트 단위로 반환합니다.

```c
size_t zlink_msg_size (const zlink_msg_t *msg_);
```

메시지 payload의 크기를 바이트 단위로 반환합니다. 빈 메시지의 경우 0을
반환합니다.

**반환값:** 바이트 단위 크기.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_data`

---

### zlink_msg_refcnt

메시지 스토리지의 reference count를 반환합니다.

```c
int zlink_msg_refcnt (const zlink_msg_t *msg_, zlink_config_result_t *error_out_);
```

reference-counted large/zero-copy storage면 현재 internal reference count를
반환합니다. inline storage나 borrowed constant storage처럼 internal
reference counting 대상이 아닌 메시지 종류는 1을 반환합니다. 실패 시
`*error_out_`에 설정 결과(`zlink_config_result_t`)가 기록되고, 성공 시
reference count가 기본 반환값으로 반환됩니다.

내부 reference count는 atomic 연산으로 관리됩니다. `zlink_msg_copy()`는
count를 atomic으로 증가시키고, `zlink_msg_close()`는 atomic으로 감소시킵니다.
따라서 같은 underlying storage를 공유하는 서로 다른 `zlink_msg_t` handle을
서로 다른 스레드에서 복사하거나 닫는 것은 안전합니다.

`zlink_msg_refcnt()`는 counter의 atomic read를 수행합니다.
반환값은 시점 스냅샷이며, 호출자가 값을 확인하는 시점에 다른 스레드가
copy/close로 이미 값을 변경했을 수 있습니다. 따라서 이 함수는 진단이나
assertion 용도에 적합하며, 제어 판단에는 적합하지 않습니다.

하나의 `zlink_msg_t` 인스턴스를 여러 스레드에서 동시에 접근하면 안 됩니다.
동시 접근이 필요하면 `zlink_msg_copy()`로 별도 handle을 만들어야 합니다.

**반환값:** 현재 storage reference count. internal reference counting 대상이
아니면 1. 실패 시 `-1`을 반환하며 `*error_out_`에
`zlink_config_result_t`가 기록됩니다. `zlink_errno()`는 진단용 내부 errno를
그대로 유지합니다.

**스레드 안전성:** underlying reference count는 atomic입니다. 같은 storage를
공유하는 *서로 다른* `zlink_msg_t` handle이 다른 스레드에서 copy/close되는
동안 이 함수를 호출하는 것은 안전합니다. 단, *같은* `zlink_msg_t` 인스턴스에
대해 이 함수와 다른 `zlink_msg_*` 함수를 여러 스레드에서 동시에 호출하는 것은
안전하지 않습니다.

**참고:** `zlink_msg_copy`, `zlink_msg_close`

---

### zlink_msg_gets

문자열 메시지 속성을 가져옵니다.

```c
const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_);
```

메시지별 문자열 메타데이터 조회를 위해 예약된 심볼입니다(`"Socket-Type"`,
`"Identity"`, `"Peer-Address"` 등). 10.0.0에서는 모든 호출이 `NULL`을 반환하며
`errno`에 `EINVAL`을 설정합니다. 응용 코드에서 이 함수에
의존하면 안 됩니다. peer 상세 정보는 socket monitor 이벤트 payload와 service
snapshot/query API를 통해 제공합니다.

**반환값:** `NULL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_refcnt`

---

### zlink_multipart_close

멀티파트 메시지 배열의 모든 파트를 닫습니다.

```c
void zlink_multipart_close (zlink_msg_t *parts, size_t part_count);
```

`parts` 배열의 각 요소에 대해 `zlink_msg_close()`를 호출하는 편의 함수입니다.
`zlink_msg_t` 구조체의 연속 배열로 저장된 멀티파트 메시지를 수신하거나 구성한
후 정리하는 데 사용합니다.

**반환값:** 없음 (void).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_close`

---

## 메시지 API 의 범위

공개 메시지 API 는 payload part 컨테이너입니다. message-level
request-reply 함수를 제공하지 않으며, per-message metadata 값도 현재 노출하지
않습니다(예약 심볼 `zlink_msg_gets` 는 헤더에 있으나 stub).
request-reply 는 `zlink_msg_t` 바깥의 ZMP control part 로 전달되며,
metadata 는 공개 메시지 경로에 포함되어 있지 않습니다.

관련 계약은 다음 문서를 참조합니다.

- socket request-reply 공개 표면: [socket/README.ko.md](socket/README.ko.md)
- SPOT routed request-reply 공개 표면: [service/spot.ko.md](service/spot.ko.md)
- wire 형식과 control part 구조: `doc/internals/protocol-zmp.ko.md`
