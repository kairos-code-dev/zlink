[스펙 목차](../README.ko.md)

# Draft -- Borrowed RoutingId Contract

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더, 바인딩 테스트가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 저장소 전체의 `routing_id` 공개 계약을 다시 정의한다.

이번 개정의 목표는 단순히 canonical 표현을 바이트 열로 맞추는 데서 끝나지 않는다.
핵심은 **binding library가 receive 경로에서 routing id를 추가 할당 없이 가져올 수
있게 하는 것**이다.

이 목표를 위해 아래 방향을 함께 고정한다.

- core C ABI의 `zlink_routing_id_t`를 borrowed bytes 형태로 바꾼다.
- core receive, callback, event, monitor 경로는 borrowed `RoutingId`를 돌려준다.
- binding public surface도 receive 경로에서는 owned copy가 아니라 borrowed
  `RoutingId`를 노출한다.
- copy는 필요할 때만 명시적으로 호출한다.
- `STREAM`, `ROUTER`, `SPOT`, monitor, service, discovery가 모두 같은
  `RoutingId` 계약을 쓴다.

이 문서는 "routing id를 바이트 열로 보자"는 기존 논의를 한 단계 더 밀어, 실제
public 시그니처와 수명 규칙까지 함께 정의한다.

## 2. 배경

현재 core의 `zlink_routing_id_t`는 inline value carrier다.

```c
typedef struct zlink_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zlink_routing_id_t;
```

이 구조는 C 값 타입으로는 다루기 쉽지만, binding library가 다음 목표를 이루는 데는
맞지 않는다.

- recv 결과에서 routing id를 새로 할당하지 않고 바로 노출
- callback 안에서 routing id를 복사 없이 읽기
- Java `ByteBuffer`, .NET `ReadOnlySpan<byte>`, Rust `&[u8]`처럼 언어가
  이미 가진 borrowed bytes 표면으로 연결

현재 값 타입 계약에서는 binding이 보통 아래 과정을 거친다.

1. native layer가 C struct를 받는다.
2. binding runtime object에 맞게 다시 복사한다.
3. public `RoutingId` wrapper를 새로 만든다.

이 흐름은 단순하지만, receive가 잦은 경로에서는 불필요한 할당과 복사를 강제한다.

이번 초안은 그 문제를 아래처럼 푼다.

- C는 borrowed `RoutingId`를 공개 운반 형태로 쓴다.
- binding도 receive 경로에서는 borrowed `RoutingId`를 그대로 노출한다.
- 사용자가 보관하고 싶을 때만 명시적으로 복사한다.

## 3. 핵심 정책

### 3.1 canonical 의미

- `RoutingId`의 본질은 길이가 있는 바이트 열이다.
- `STREAM`도 예외가 아니다.
- `u32`, `text`, `hex`는 helper 해석일 뿐 canonical 형식이 아니다.
- 빈 바이트 열도 유효한 `RoutingId`다.

### 3.2 소유권

- core C API가 돌려주는 `RoutingId`는 기본적으로 borrowed다.
- receive, callback, monitor, event, discovery snapshot, service snapshot도 같은
  규칙을 따른다.
- send와 setter는 borrowed `RoutingId` 입력을 받는다.
- 오래 보관하려면 명시적으로 copy한다.

### 3.3 binding 원칙

- binding receive 경로는 기본적으로 borrowed `RoutingId`를 public으로 노출한다.
- 복사 결과가 필요하더라도 public 기본 이름은 `RoutingId` 하나로 유지한다.
- public 이름에는 `View`, `Owned` 같은 접미사를 붙이지 않는다.
- 언어별 구현은 내부 storage 전략이 달라도, 사용자에게 보이는 타입 이름과
  변환 API는 `RoutingId`로 통일한다.

즉 이 초안의 중요한 선택은 아래와 같다.

- "owned `RoutingId`를 항상 반환한다"가 아니다.
- "receive 경로에서는 borrowed `RoutingId`를 기본으로 쓴다"가 맞다.

## 4. core C 공개 타입과 시그니처

### 4.1 `zlink_routing_id_t`

`zlink_routing_id_t`는 아래 형태로 바꾼다.

```c
typedef struct zlink_routing_id_t
{
    size_t size;
    const uint8_t *data;
} zlink_routing_id_t;
```

의미는 아래와 같다.

- `data`는 `size` 길이의 바이트 열을 가리킨다.
- `data == NULL`이면 `size`는 반드시 0이어야 한다.
- `size == 0`이면 빈 `RoutingId`다.
- 이 타입 자체는 소유권을 갖지 않는다.

### 4.2 조회와 수신

owning value를 out parameter로 채우는 대신, borrowed `RoutingId`를 가리키는
포인터를 돌려준다.

예시는 아래와 같다.

```c
zlink_config_result_t zlink_get_routing_id (
  void *handle_,
  const zlink_routing_id_t **out_);

zlink_config_result_t zlink_get_routing_id_bytes (
  void *handle_,
  uint8_t *buf_,
  size_t *inout_len_);

zlink_recv_result_t zlink_recv (
  void *socket_,
  const zlink_routing_id_t **source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

zlink_recv_result_t zlink_router_recv (
  void *socket_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

모니터, event, service, discovery 계열도 같은 방향으로 바꾼다.

- struct field에 inline `zlink_routing_id_t`를 박아 넣는 현재 방식은 없앤다.
- 대신 owner object가 가진 내부 저장소를 가리키는 borrowed `RoutingId`를 둔다.

### 4.3 설정과 송신

setter와 send는 borrowed 입력을 받는다.

```c
zlink_config_result_t zlink_set_routing_id (
  void *handle_,
  const zlink_routing_id_t *routing_id_);

zlink_submit_result_t zlink_send_rid (
  void *socket_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

여기서도 callee는 input bytes를 읽기만 하며, 호출이 끝난 뒤 caller 메모리를
계속 참조해서는 안 된다.

### 4.4 helper

`to_*` helper는 borrowed `RoutingId`를 읽는다.

```c
zlink_config_result_t zlink_routing_id_to_u32 (
  const zlink_routing_id_t *rid_,
  uint32_t *value_out_);

zlink_config_result_t zlink_routing_id_to_text (
  const zlink_routing_id_t *rid_,
  char *buf_,
  size_t *inout_len_);

zlink_config_result_t zlink_routing_id_to_hex (
  const zlink_routing_id_t *rid_,
  char *buf_,
  size_t *inout_len_);
```

`from_*` helper는 borrowed 타입에 쓸 backing storage를 호출자가 직접 제공해야
한다.

```c
zlink_config_result_t zlink_routing_id_from_u32 (
  uint32_t value_,
  uint8_t *buf_,
  size_t buf_cap_,
  zlink_routing_id_t *out_);

zlink_config_result_t zlink_routing_id_from_text (
  const char *text_,
  uint8_t *buf_,
  size_t buf_cap_,
  zlink_routing_id_t *out_);

zlink_config_result_t zlink_routing_id_from_bytes (
  const void *data_,
  size_t size_,
  uint8_t *buf_,
  size_t buf_cap_,
  zlink_routing_id_t *out_);

zlink_config_result_t zlink_routing_id_copy (
  const zlink_routing_id_t *src_,
  uint8_t *buf_,
  size_t buf_cap_,
  zlink_routing_id_t *out_);
```

이 규칙이 필요한 이유는 단순하다.

- `RoutingId`는 borrowed 타입이라 자기 storage를 소유하지 않는다.
- 따라서 `from_*` helper가 안전한 결과를 돌려주려면 caller가 backing buffer를
  줘야 한다.

## 5. 수명 규칙

### 5.1 기본 규칙

borrowed `RoutingId`의 수명은 owner에 종속된다.

- `zlink_get_routing_id()`가 돌려준 값은 다음 `zlink_set_routing_id()` 또는 owner
  teardown 전까지만 유효하다.
- `zlink_recv()`와 `zlink_router_recv()`가 돌려준 값은 그 receive result를 소유한
  내부 수신 저장소가 다음 receive로 덮이기 전까지만 유효하다.
- callback 인자로 받은 `RoutingId`는 callback이 반환되기 전까지만 유효하다.
- monitor/event/snapshot 경로도 각 owner object의 유효 범위를 넘겨 쓰면 안 된다.

### 5.2 binding에서의 기본 규칙

binding도 같은 수명 규칙을 public으로 드러낸다.

- Java: `Received` 또는 event wrapper가 살아 있는 동안만 `RoutingId`가 유효하다.
- .NET: `Received` stack scope 안에서만 `RoutingId`가 유효하다.
- Rust: owner lifetime `'a`에 묶인 `RoutingId`만 빌려준다.
- Python, Node: callback 반환 또는 `Received.close()` 전까지만 유효한 borrowed
  handle로 본다.

즉 binding이 dangling pointer를 숨기기 위해 매번 owned copy를 만드는 대신,
유효 범위를 문서와 타입으로 분명히 드러내는 쪽을 택한다.

## 6. binding 공개 시그니처 초안

이 절의 목적은 언어별 세부 문법을 고정하는 것이 아니라, 무할당 경로를 가능하게
하는 public 시그니처의 모양을 고정하는 것이다.

### 6.1 공통 원칙

- receive, callback, event, monitor가 주는 `RoutingId`는 borrowed다.
- send, setter, request, reply는 borrowed bytes 입력을 받는다.
- copy가 필요하면 `copy()`, `toBytes()`, `toArray()` 같은 명시적 API를 호출한다.
- `toText()`와 `toHex()`는 문자열 할당이 생길 수 있다.
- 완전한 무할당 경로는 bytes accessor를 읽을 때만 성립한다.

### 6.2 Java

Java에서는 `ByteBuffer`가 borrowed bytes 표면이 된다.

```java
public interface RoutingId extends AutoCloseable {
    int size();
    java.nio.ByteBuffer asByteBuffer();
    int toU32();
    String toText();
    String toHex();
    byte[] toBytes();
    RoutingId copy();
    @Override void close();
}

public interface Received extends AutoCloseable {
    RoutingId routingId();
    RoutingId spotRoutingId();
    @Override void close();
}

public interface Socket {
    void setRoutingId(java.nio.ByteBuffer routingId);
    void send(java.nio.ByteBuffer routingId, List<Message> parts, SendFlags flags);
}
```

규칙은 아래와 같다.

- `asByteBuffer()`는 read-only direct buffer여야 한다.
- `toBytes()`와 `copy()`만 새 할당을 만든다.
- `routingId()`가 돌려준 값은 `Received.close()` 이후에는 사용할 수 없다.

### 6.3 .NET

.NET에서는 `ReadOnlySpan<byte>`가 borrowed bytes 표면이 된다.

```csharp
public readonly ref struct RoutingId
{
    public int Length { get; }
    public ReadOnlySpan<byte> AsSpan();
    public uint ToU32();
    public string ToText();
    public string ToHex();
    public byte[] ToArray();
    public RoutingId Copy();
}

public readonly ref struct Received
{
    public RoutingId RoutingId { get; }
    public RoutingId SpotRoutingId { get; }
}

public interface ISocket
{
    void SetRoutingId(ReadOnlySpan<byte> routingId);
    void Send(ReadOnlySpan<byte> routingId,
              ReadOnlySpan<Message> parts,
              SendFlags flags = default);
}
```

규칙은 아래와 같다.

- `AsSpan()`만 무할당 경로다.
- `ToArray()`, `ToText()`, `ToHex()`, `Copy()`는 할당이 생길 수 있다.
- `RoutingId`와 `Received`는 `ref struct` 제약을 통해 scope 밖 capture를 막는다.

### 6.4 Rust

Rust에서는 `&[u8]`와 owner lifetime이 borrowed bytes 표면이 된다.

```rust
pub struct RoutingId<'a> {
    bytes: &'a [u8],
}

impl<'a> RoutingId<'a> {
    pub fn as_bytes(&self) -> &'a [u8];
    pub fn to_u32(&self) -> Option<u32>;
    pub fn to_text(&self) -> Result<&'a str, std::str::Utf8Error>;
    pub fn to_hex(&self) -> String;
    pub fn copy(&self) -> RoutingId<'static>;
}

pub struct Received<'a> {
    pub routing_id: Option<RoutingId<'a>>,
    pub spot_rid: Option<RoutingId<'a>>,
}
```

### 6.5 Python

Python은 runtime 특성상 진짜 raw pointer를 안전하게 일반 객체로 퍼뜨리기 어렵다.
그래서 borrowed handle object를 두되, 수명을 owner와 함께 강하게 묶는다.

```python
class RoutingId:
    def memoryview(self) -> memoryview: ...
    def to_u32(self) -> int: ...
    def to_text(self) -> str: ...
    def to_hex(self) -> str: ...
    def copy(self) -> "RoutingId": ...

class Received:
    @property
    def routing_id(self) -> RoutingId | None: ...
    @property
    def spot_rid(self) -> RoutingId | None: ...
    def close(self) -> None: ...
```

규칙은 아래와 같다.

- `memoryview()`가 bytes를 새로 복사하지 않는 경로여야 한다.
- `Received.close()` 또는 callback 반환 뒤에는 `RoutingId`를 쓸 수 없다.
- 사용자가 오래 들고 가려면 `copy()`를 호출해야 한다.

### 6.6 Node

Node는 `Buffer`를 borrowed 외관으로 노출할 수 있지만, GC와 native owner 수명을
함께 묶어야 한다.

```ts
class RoutingId {
  asBuffer(): Buffer;
  toU32(): number;
  toText(): string;
  toHex(): string;
  copy(): RoutingId;
}

interface Received extends Disposable {
  readonly routingId: RoutingId | null;
  readonly spotRid: RoutingId | null;
}

interface Socket {
  setRoutingId(routingId: Buffer | Uint8Array): void;
  send(routingId: Buffer | Uint8Array,
       parts: readonly MessageLike[],
       flags?: SendFlags): void;
}
```

여기서 `asBuffer()`는 owner를 잡고 있는 동안만 유효한 borrowed buffer여야 한다.

## 7. helper 규칙

helper 의미는 저장소 전체에서 아래처럼 통일한다.

- `from_u32`: 4-byte big-endian bytes 생성
- `to_u32`: 길이가 정확히 4일 때만 성공
- `from_text`: UTF-8 인코딩 bytes 생성
- `to_text`: 유효한 UTF-8일 때만 성공
- `to_hex`: 항상 성공

추가 규칙은 아래와 같다.

- invalid UTF-8을 조용히 다른 문자열로 바꾸지 않는다.
- 길이가 4가 아닌데 `to_u32()`에서 truncate 또는 zero-fill 하지 않는다.
- empty bytes는 raw `RoutingId`로는 허용하지만, `to_u32()`와 `to_text()`는 형식이
  맞지 않으면 실패한다.

### 7.1 helper 배치 원칙

변환 기능이 어디에 있어야 하는지도 함께 고정한다.

- core C는 언어 구조상 타입 메서드를 둘 수 없으므로 free function helper를 유지한다.
- binding은 별도 `routing_id_helpers`, `routing_id_utils`, codec 모듈을 public
  변환 경로로 두지 않는다.
- binding 변환 API는 `RoutingId` 타입 자체에 들어간다.

즉 실제 코드 수정 방향은 아래와 같다.

- core:
  `zlink_routing_id_from_u32`, `zlink_routing_id_to_u32`,
  `zlink_routing_id_from_text`, `zlink_routing_id_to_text`,
  `zlink_routing_id_to_hex` 같은 helper를 유지
- bindings:
  `RoutingId.from_u32()`, `RoutingId.to_u32()`,
  `RoutingId.from_text()`, `RoutingId.to_text()`,
  `RoutingId.to_hex()` 형태로 흡수

이 규칙은 왜 필요한지가 분명하다.

- core는 C라서 helper를 함수로 둘 수밖에 없다.
- binding은 타입 메서드가 가능하므로, 같은 기능을 별도 helper 계층으로 또 두면
  public surface만 중복된다.
- 특히 receive 경로가 borrowed `RoutingId`를 기본으로 쓰는 구조에서는, 변환도
  `RoutingId` 자신이 제공해야 lifetime 의미가 더 분명해진다.

## 8. sample, perf, guide 방향

- `STREAM` sample과 perf는 `RoutingId.to_u32()`를 사용한다.
- routed/service sample과 perf는 `RoutingId.to_text()`를 우선 쓰고,
  실패하면 `RoutingId.to_hex()`를 사용한다.
- guide는 "binding이 항상 새 `RoutingId` 값을 만든다"는 설명을 제거한다.
- guide와 binding spec은 receive 경로의 borrowed 수명 규칙을 반드시 적는다.

## 9. 구현 영향

이 초안이 실제 구현으로 이어지면 아래 변화가 필수다.

- `core/include/zlink.h` ABI 변경
- core event, monitor, discovery, service snapshot struct 재설계
- native bridge와 copied header 전면 동기화
- Python, Rust, Node, Java, .NET 우선 반영
- sample, perf, 단위 테스트, smoke test 동반 수정

특히 binding 쪽에서는 "receive 결과를 바로 owned `RoutingId`로 복사한다"는 현재
가정을 걷어내고, borrowed 수명 규칙을 public surface로 승격해야 한다.

추가로 변환 코드의 구조도 아래처럼 정리한다.

- core:
  free function helper 유지
- bindings:
  별도 변환 helper 제거 또는 내부 구현으로 축소
- bindings public API:
  모든 변환을 `RoutingId` 타입 메서드 또는 static constructor로 통일

즉 binding 코드에서 아래 성격의 독립 API는 정리 대상이다.

- `routing_id_from_u32(...)`
- `routing_id_to_u32(...)`
- `routing_id_to_text(...)`
- `routing_id_to_hex(...)`
- `RoutingIdHelper`, `RoutingIdCodec`, `routing_id_utils` 같은 별도 public helper

반대로 아래 형태가 최종 public surface가 된다.

- `RoutingId.from_bytes(...)`
- `RoutingId.from_u32(...)`
- `RoutingId.from_text(...)`
- `rid.to_bytes()` 또는 무할당 bytes accessor
- `rid.to_u32()`
- `rid.to_text()`
- `rid.to_hex()`
- 필요 시 `rid.copy()`

## 10. 검증 기준

이 초안이 구현될 때 최소 검증 기준은 아래와 같다.

- C helper round-trip
  - `u32 -> RoutingId -> u32`
  - `text -> RoutingId -> text`
- invalid length / invalid UTF-8 / invalid buffer 실패 검증
- empty raw bytes 경로 유지 검증
- receive 경로에서 binding helper가 추가 bytes allocation 없이 `RoutingId`를 노출
- copy API 호출 뒤 owner가 해제돼도 복사본은 계속 유효
- `STREAM` sample에서 `to_u32()` 사용
- routed/service sample에서 `to_text()` 또는 `to_hex()` 사용

## 11. 정리

이 초안의 결론은 분명하다.

- 저장소 전체의 `RoutingId` canonical은 borrowed bytes다.
- no-allocation receive를 원하면 binding public API도 borrowed `RoutingId`를
  노출해야 한다.
- 성능상 핵심 차이는 `struct` 대 `void* + size`가 아니라, receive 경로에서
  copy와 allocation을 없애느냐에 있다.
- 따라서 이번 개정의 본질은 단순한 타입 이름 변경이 아니라, 소유권과 수명 규칙을
  저장소 전체에서 다시 맞추는 일이다.
