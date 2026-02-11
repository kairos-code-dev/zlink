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

### 메시지 플래그

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_MORE` | 1 | 멀티파트 메시지에서 더 많은 파트가 뒤따름을 나타냄 |
| `ZLINK_SHARED` | 3 | 메시지 데이터가 공유됨 (참조 카운팅) |

### 메시지 속성

다음 속성 식별자는 `zlink_msg_get()`, `zlink_msg_set()`, `zlink_msg_gets()`에서
사용됩니다:

| 함수 | 속성 | 설명 |
|------|------|------|
| `zlink_msg_more()` / `zlink_msg_get()` | `ZLINK_MORE` | 더 많은 파트가 뒤따르는지 여부 |
| `zlink_msg_get()` | `ZLINK_SHARED` | 메시지가 공유되었는지 여부 |
| `zlink_msg_gets()` | 문자열 키 | 키 이름으로 메타데이터 조회 (예: `"Socket-Type"`, `"Identity"`, `"Peer-Address"`) |

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

### zlink_msg_send

소켓에서 메시지를 송신합니다.

```c
int zlink_msg_send (zlink_msg_t *msg_, void *s_, int flags_);
```

소켓 `s_`에서 메시지 `msg_`를 송신합니다. 성공 시 메시지의 소유권이
라이브러리로 이전되고 `msg_`는 빈 메시지가 됩니다(`zlink_msg_init()`이 호출된
것과 같은 상태). 호출자는 성공적인 송신 후 원래 메시지 데이터에 접근해서는
안 됩니다. 실패 시 메시지는 변경되지 않으며 호출자가 소유권을 유지합니다.

`flags_`는 0, `ZLINK_DONTWAIT`, `ZLINK_SNDMORE`, 또는 이들의 비트 OR 조합일 수
있습니다. `ZLINK_SNDMORE`는 멀티파트 메시지에서 더 많은 파트가 뒤따를 것임을
나타냅니다.

**반환값:** 성공 시 메시지의 바이트 수, 실패 시 -1 (errno가 설정됨).

**에러:** 소켓이 즉시 송신할 수 없고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`.
Context가 종료된 경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_msg_recv`, `zlink_send`

---

### zlink_msg_recv

소켓에서 메시지를 수신합니다.

```c
int zlink_msg_recv (zlink_msg_t *msg_, void *s_, int flags_);
```

소켓 `s_`에서 메시지를 수신하여 `msg_`에 저장합니다. 새 메시지를 저장하기 전에
`msg_`의 이전 내용은 적절히 해제됩니다. 호출자가 수신된 메시지를 소유하며 완료
후 `zlink_msg_close()`로 닫아야 합니다.

**반환값:** 성공 시 수신된 메시지의 바이트 수, 실패 시 -1 (errno가 설정됨).

**에러:** 사용 가능한 메시지가 없고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`.
Context가 종료된 경우 `ETERM`.

**스레드 안전성:** 동일 소켓에서 스레드 안전하지 않습니다.

**참고:** `zlink_msg_send`, `zlink_recv`

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

**참고:** `zlink_msg_init`, `zlink_msgv_close`

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

### zlink_msg_more

멀티파트 메시지에서 더 많은 파트가 뒤따르는지 확인합니다.

```c
int zlink_msg_more (const zlink_msg_t *msg_);
```

메시지의 `ZLINK_MORE` 플래그를 조회합니다. 메시지가 멀티파트 시퀀스의 일부이고
더 많은 파트가 뒤따르면 1을, 그렇지 않으면 0을 반환합니다. 일반적으로
`zlink_msg_recv()` 후에 수신을 계속할지 결정하기 위해 호출합니다.

**반환값:** 더 많은 파트가 뒤따르면 1, 그렇지 않으면 0.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_get`, `ZLINK_MORE`

---

### zlink_msg_get

정수형 메시지 속성을 가져옵니다.

```c
int zlink_msg_get (const zlink_msg_t *msg_, int property_);
```

메시지에서 정수형 속성 값을 가져옵니다. 유효한 속성에는 `ZLINK_MORE`와
`ZLINK_SHARED`가 포함됩니다.

**반환값:** 성공 시 속성 값, 실패 시 -1 (알 수 없는 속성의 경우 errno가
`EINVAL`로 설정됨).

**에러:** 속성이 인식되지 않으면 `EINVAL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_set`, `zlink_msg_gets`

---

### zlink_msg_set

정수형 메시지 속성을 설정합니다.

```c
int zlink_msg_set (zlink_msg_t *msg_, int property_, int optval_);
```

메시지의 정수형 속성 값을 설정합니다. 쓰기 가능한 속성의 집합은 구현에 따라
정의됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 속성이 인식되지 않거나 쓰기 불가능한 경우 `EINVAL`.

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_get`

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

**참고:** `zlink_msg_get`

---

### zlink_msgv_close

멀티파트 메시지 배열의 모든 파트를 닫습니다.

```c
void zlink_msgv_close (zlink_msg_t *parts, size_t part_count);
```

`parts` 배열의 각 요소에 대해 `zlink_msg_close()`를 호출하는 편의 함수입니다.
`zlink_msg_t` 구조체의 연속 배열로 저장된 멀티파트 메시지를 수신하거나 구성한
후 정리하는 데 사용합니다.

**반환값:** 없음 (void).

**스레드 안전성:** 스레드 안전하지 않습니다.

**참고:** `zlink_msg_close`
