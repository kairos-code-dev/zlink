[English](message.md) | [한국어](message.ko.md)

# 메시지 API 레퍼런스

메시지 API는 zlink 메시지의 생성, 송신, 수신, 관리를 위한 함수를 제공합니다.
메시지는 소켓 간 데이터 교환의 기본 단위이며, 임의의 바이너리 페이로드를
전달하고, 제로카피 시맨틱을 지원하며, 멀티파트 시퀀스를 구성할 수 있습니다.

## 타입

```c
typedef struct zlink_msg_t
{
    unsigned char _[64];
} zlink_msg_t;
```

`zlink_msg_t`는 64바이트 불투명 메시지 구조체입니다. 내부 레이아웃은
플랫폼에 따라 다르며 직접 접근해서는 안 됩니다. 모든 메시지는 사용 전에
초기화하고 사용 후에 닫아야 합니다.

```c
typedef struct zlink_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zlink_routing_id_t;
```

`zlink_routing_id_t`는 `ROUTER` 소켓이 특정 피어에 주소를 지정하는 데 사용하는
라우팅 아이덴티티를 전달합니다. `size`는 `data`에서 유효한 바이트 수를 나타냅니다.

```c
typedef void (zlink_free_fn) (void *data_, void *hint_);
```

`zlink_free_fn`은 제로카피 메시지 생성을 위해 `zlink_msg_init_data()`에서
사용되는 콜백 타입입니다. 메시지 데이터 버퍼가 더 이상 필요하지 않을 때
라이브러리가 이 함수를 호출합니다.

## 상수

### 문자열 메타데이터 속성

다음 문자열 메타데이터 키는 `zlink_msg_gets()`로 조회할 수 있습니다:

| 키 | 설명 |
|------|------|
| `"Socket-Type"` | 피어의 소켓 타입 |
| `"Identity"` | 피어 아이덴티티 |
| `"Peer-Address"` | 피어 네트워크 주소 |

## 함수

### zlink_msg_init

빈 메시지를 초기화합니다.

```c
int zlink_msg_init (zlink_msg_t *msg_);
```

`msg_`를 빈 길이 0 메시지로 초기화합니다. 메시지는 최종적으로
`zlink_msg_close()`로 해제해야 합니다. `zlink_msg_t`를 다른 메시지 함수에
전달하기 전에 항상 초기화하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않습니다. 각 `zlink_msg_t`는 한 번에 하나의
스레드에서만 사용해야 합니다.

**참고:** `zlink_msg_init_size`, `zlink_msg_init_data`, `zlink_msg_close`

---

### zlink_msg_init_size

지정된 크기의 메시지를 초기화합니다.

```c
int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);
```

`size_` 바이트의 내부 버퍼를 할당하고 `msg_`를 초기화합니다. 버퍼 내용은
초기화되지 않습니다. `zlink_msg_data()`를 사용하여 버퍼에 대한 포인터를 얻고
송신 전에 데이터를 채우세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 할당 실패 시 `ENOMEM`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_data`, `zlink_msg_size`

---

### zlink_msg_init_data

외부 데이터 버퍼로부터 메시지를 초기화합니다 (제로카피).

```c
int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);
```

호출자가 제공한 `size_` 바이트의 버퍼 `data_`를 복사하지 않고 참조하는 메시지를
생성합니다. 라이브러리가 더 이상 버퍼를 필요로 하지 않을 때(메시지가 송신되거나
닫힌 후) 호출자가 버퍼를 해제할 수 있도록 `data_`와 `hint_`를 인수로 콜백
`ffn_`을 호출합니다. `ffn_`이 `NULL`이면 콜백이 호출되지 않으며, 호출자는
버퍼가 메시지보다 오래 존재하도록 보장해야 합니다.

이 함수는 진정한 제로카피 메시지 전달을 가능하게 합니다. 호출자는 `ffn_`이
호출될 때까지 `data_`를 수정하거나 해제해서는 안 됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_free_fn`, `zlink_msg_data`

---

### zlink_msg_close

메시지 리소스를 해제합니다.

```c
int zlink_msg_close (zlink_msg_t *msg_);
```

메시지와 관련된 모든 리소스를 해제합니다. 초기화된 모든 메시지는 정확히 한 번
닫아야 합니다. 닫은 후 `zlink_msg_t` 구조체는 유효하지 않으며 재사용하기 전에
다시 초기화해야 합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_init`, `zlink_multipart_close`

---

### zlink_msg_move

소스에서 대상으로 메시지 내용을 이동합니다.

```c
int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);
```

`src_`의 내용을 `dest_`로 이동합니다. 성공적인 이동 후 `src_`는 빈 메시지가
되고(새로 초기화된 메시지와 동일) `dest_`는 원래 내용을 포함합니다. `dest_`의
이전 내용은 해제됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_copy`

---

### zlink_msg_copy

메시지를 복사합니다.

```c
int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);
```

`src_`의 내용을 `dest_`로 복사합니다. 두 메시지는 참조 카운팅을 통해 기본
데이터 버퍼를 공유합니다. `dest_`의 이전 내용은 해제됩니다. 복사는 경량이며
데이터 페이로드를 복제하지 않습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_move`

---

### zlink_msg_data

메시지 데이터 버퍼에 대한 포인터를 반환합니다.

```c
void *zlink_msg_data (zlink_msg_t *msg_);
```

메시지의 원시 데이터 페이로드에 대한 포인터를 반환합니다. 포인터는 메시지가
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

메시지 페이로드의 크기를 바이트 단위로 반환합니다. 빈 메시지의 경우 0을
반환합니다.

**반환값:** 바이트 단위 크기.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_data`

---

### zlink_msg_refcnt

메시지 스토리지의 reference count를 반환합니다.

```c
int zlink_msg_refcnt (const zlink_msg_t *msg_);
```

reference-counted large/zero-copy storage면 현재 internal reference count를
반환합니다. inline storage나 borrowed constant storage처럼 internal
reference counting 대상이 아닌 메시지 종류는 1을 반환합니다.

내부 reference count는 atomic 연산으로 관리됩니다. `zlink_msg_copy()`는
count를 atomic으로 증가시키고, `zlink_msg_close()`는 atomic으로 감소시킵니다.
따라서 같은 underlying storage를 공유하는 서로 다른 `zlink_msg_t` handle을
서로 다른 스레드에서 copy/close하는 것은 안전합니다.

`zlink_msg_refcnt()`는 atomic counter의 relaxed read를 수행합니다.
반환값은 시점 스냅샷이며, 호출자가 값을 확인하는 시점에 다른 스레드가
copy/close로 이미 값을 변경했을 수 있습니다. 따라서 이 함수는 진단이나
assertion 용도에 적합하며, 제어 판단에는 적합하지 않습니다.

단일 `zlink_msg_t` 인스턴스를 여러 스레드에서 동시에 접근하면 안 됩니다.
동시 접근이 필요하면 `zlink_msg_copy()`로 별도 handle을 만들어야 합니다.

**반환값:** 현재 storage reference count. internal reference counting 대상이
아니면 1.

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

키 이름으로 메시지의 문자열 메타데이터 값을 가져옵니다. 메타데이터는
트랜스포트 계층에 의해 첨부되며 `"Socket-Type"`, `"Identity"`,
`"Peer-Address"` 같은 키를 포함할 수 있습니다. 반환된 포인터는 메시지가 닫힐
때까지만 유효합니다.

**반환값:** 성공 시 null 종료 문자열, 실패 시 `NULL` (errno가 설정됨).

**에러:** 속성 이름이 메시지 메타데이터에서 발견되지 않으면 `EINVAL`.

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

## Request-Reply Envelope

메시지별 request-reply 필드를 위한 상수와 함수입니다. 이 필드는 send 시
core가 자동으로 wire envelope에 직렬화하고, recv 시 자동으로 파싱하여
복원합니다. 바인딩은 이 값으로 dispatch만 하면 됩니다.

### 상수

```c
#define ZLINK_MSG_TYPE_DATA     0
#define ZLINK_MSG_TYPE_REQUEST  1
#define ZLINK_MSG_TYPE_REPLY    2
```

| 상수 | 값 | 설명 |
|------|---|------|
| `ZLINK_MSG_TYPE_DATA` | 0 | 기본값. wire에 envelope 없음. |
| `ZLINK_MSG_TYPE_REQUEST` | 1 | Request 메시지. |
| `ZLINK_MSG_TYPE_REPLY` | 2 | Reply 메시지. |

### zlink_msg_set_request

메시지를 REQUEST로 설정합니다.

```c
int zlink_msg_set_request (zlink_msg_t *msg_, uint64_t correlation_id_);
```

`msg_type`을 `ZLINK_MSG_TYPE_REQUEST`로, `correlation_id`를 지정된 값으로
동시에 설정합니다. send 시 core가 자동으로 request-reply envelope를
wire에 직렬화합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `msg_`가 NULL이거나 초기화되지 않으면 `EINVAL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_set_reply`, `zlink_msg_get_request_info`

---

### zlink_msg_set_reply

메시지를 REPLY로 설정합니다.

```c
int zlink_msg_set_reply (zlink_msg_t *msg_, uint64_t correlation_id_);
```

`msg_type`을 `ZLINK_MSG_TYPE_REPLY`로, `correlation_id`를 지정된 값으로
동시에 설정합니다. correlation_id는 원본 request의 correlation_id와
동일한 값을 사용합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `msg_`가 NULL이거나 초기화되지 않으면 `EINVAL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_set_request`, `zlink_msg_get_request_info`

---

### zlink_msg_get_request_info

메시지의 request-reply 정보를 조회합니다.

```c
int zlink_msg_get_request_info (const zlink_msg_t *msg_,
                                uint8_t *type_out_,
                                uint64_t *correlation_id_out_);
```

`msg_type`과 `correlation_id`를 한 번의 호출로 반환합니다. `type_out_`이
`ZLINK_MSG_TYPE_DATA`(0)이면 `correlation_id_out_` 값은 무의미합니다.
출력 포인터가 NULL이면 해당 필드를 생략합니다.

recv 후 이 함수를 호출하면 core가 wire envelope에서 파싱한 msg_type과
correlation_id를 얻을 수 있습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `msg_`가 NULL이거나 초기화되지 않으면 `EINVAL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_set_request`, `zlink_msg_set_reply`

---

## Per-Message Metadata

개별 메시지에 application 정의 key-value metadata를 첨부하는 함수입니다.
metadata는 send 시 wire에 직렬화되고 recv 시 복원됩니다.
ZMP 프로토콜 메타데이터(`zlink_msg_gets`)와는 완전히 별개의 namespace입니다.

### 상수

```c
#define ZLINK_MSG_METADATA_KEY_USER_MIN   0x0100
#define ZLINK_MSG_METADATA_VALUE_MAX      65535
```

| 상수 | 값 | 설명 |
|------|---|------|
| `ZLINK_MSG_METADATA_KEY_USER_MIN` | 0x0100 | 사용자 정의 key 최소값. 미만은 예약 대역. |
| `ZLINK_MSG_METADATA_VALUE_MAX` | 65535 | 단일 항목 최대 value 크기 (바이트). |

### zlink_msg_set_metadata

메시지에 metadata key-value를 설정합니다.

```c
int zlink_msg_set_metadata (zlink_msg_t *msg_, uint16_t key_,
                            const void *value_, size_t value_size_);
```

metadata 항목을 설정하거나 덮어씁니다. 이 함수를 한 번이라도 호출하면
send 시 metadata header가 wire에 추가됩니다. `0x0000`~`0x00FF` 범위의
key는 zlink 내부 예약이므로 `EINVAL`로 거부됩니다. `value_ = NULL`을
전달하면 길이 0 값을 설정합니다 (키 존재 자체가 의미).

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:**
- `EINVAL`: `msg_`가 NULL, `key_` < 0x0100, 또는 `value_size_`가
  `ZLINK_MSG_METADATA_VALUE_MAX` 초과.
- `ENOMEM`: heap 할당 실패.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_get_metadata`

---

### zlink_msg_get_metadata

메시지에서 metadata 값을 조회합니다.

```c
const void *zlink_msg_get_metadata (const zlink_msg_t *msg_,
                                    uint16_t key_, size_t *size_);
```

`key_`에 연결된 값의 포인터를 반환합니다. 키가 없으면 NULL을 반환합니다.
반환된 포인터는 메시지가 유효하고 동일 key에 `zlink_msg_set_metadata()`를
다시 호출하지 않는 동안만 유효합니다. `size_ = NULL`을 전달하면 길이
출력을 생략합니다.

**반환값:** 값 바이트 포인터. 키가 없으면 NULL.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_set_metadata`
