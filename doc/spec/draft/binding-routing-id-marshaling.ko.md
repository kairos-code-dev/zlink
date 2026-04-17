[스펙 목차](../README.ko.md)

# Draft -- Binding Routing ID Marshaling Policy

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더, 바인딩 테스트가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 binding 경계에서 `routing_id`를 어떻게 노출하고 변환할지 정리한다.

이 문서는 helper 의미를 다루는
[`routing-id-bytes-and-conversions.ko.md`](routing-id-bytes-and-conversions.ko.md)
와, ABI 및 수명 규칙을 다루는
[`routing-id-borrowed-routingid.ko.md`](routing-id-borrowed-routingid.ko.md)
를 바탕으로 바인딩 public surface의 적용 원칙만 정리한다.

핵심 목표는 아래와 같다.

- binding마다 다른 public `routing_id` 기본 타입이 생기지 않게 한다.
- raw bytes 경로와 convenience helper 경로의 역할을 분명히 나눈다.
- `STREAM`, `ROUTER`, `DEALER`, `SPOT`을 한 binding 모델 안에서 일관되게 노출한다.
- 언어별 구현은 달라도 encode/decode 규칙은 같게 유지한다.

## 2. 전제

이 초안은 아래 전제를 따른다.

- core canonical routing id는 borrowed `zlink_routing_id_t`를 기준으로 본다.
- 공개 운반 형태는 전부 바이트 열이다.
- `STREAM`도 public에서 별도 정수 타입으로 분리하지 않는다.
- 응용 편의를 위해 binding `RoutingId` 타입에 변환 메서드를 둔다.
- raw bytes 경로는 빈 routing id도 그대로 다룰 수 있어야 한다.

즉 이 문서는 "binding public surface를 string-only 또는 uint32-only로 바꾼다"는
문서가 아니다.

## 3. binding public surface 원칙

각 binding은 `routing_id`를 아래 두 층으로 노출하는 것을 권장한다.

### 3.1 raw canonical layer

- raw bytes accessor 제공
- send/recv/callback에서 bytes canonical을 유지할 수 있는 escape hatch 제공
- receive 경로에서는 owner 수명에 종속된 borrowed `RoutingId`를 기본으로 노출

이 층은 low-level 제어, interop, 기존 코드 호환을 위한 경로다.

### 3.2 convenience layer

- `RoutingId.from_u32`
- `RoutingId.to_u32`
- `RoutingId.from_text`
- `RoutingId.to_text`
- `RoutingId.to_hex`

이 층은 응용이 raw encode/decode를 직접 반복하지 않게 만드는 경로다.

즉 binding public surface의 기본 철학은 아래와 같다.

- **운반은 bytes**
- **사용성은 helper**

## 4. STREAM marshaling

`STREAM`도 binding public surface에서는 raw bytes를 쓴다.

다만 binding helper는 `STREAM routing_id`를 4바이트 big-endian `u32`로 다루는
경로를 제공할 수 있다.

권장 규칙은 아래와 같다.

- callback이 주는 `source_rid` 타입은 raw bytes 기반 borrowed `RoutingId`다.
- `send`도 raw bytes 기반 `RoutingId`를 받는다.
- `RoutingId.to_u32()`는 길이가 4바이트일 때만 성공한다.
- `RoutingId.from_u32()`는 항상 4바이트 big-endian 값을 만든다.

즉 `STREAM`은 public type을 따로 나누지 않고, helper만 더한다.

여기서도 raw bytes 경로는 길이 0의 routing id를 그대로 전달할 수 있어야 한다.
다만 `to_u32()`는 길이 4 조건을 만족할 때만 성공해야 한다.

## 5. 일반 routed marshaling

`ROUTER`, `DEALER`, `SPOT`, Discovery, Registry, monitor/event 경로는 모두
raw bytes 기반 `RoutingId`를 공통으로 쓴다.

다만 실제 응용에서 텍스트 기반 id를 많이 쓰므로, binding helper는 아래를
제공하는 것이 좋다.

- `RoutingId.from_text(...)`
- `rid.to_text()`
- `rid.to_hex()`

여기서 중요한 점은 아래와 같다.

- 텍스트 helper는 convenience일 뿐 canonical 의미를 바꾸지 않는다.
- 어떤 routed id가 UTF-8이 아니면 `to_text()`는 실패할 수 있다.
- 이 경우 `to_hex()`가 fallback이어야 한다.

## 6. binding별 구현 원칙

이 초안은 각 binding이 변환 로직을 **`RoutingId` 타입에 직접 구현**하는 방향을
권장한다.

이유는 아래와 같다.

- 정수와 문자열 변환은 대부분 언어 표준 기능만으로 충분하다.
- FFI 호출 없이 각 언어의 자연스러운 타입을 쓸 수 있다.
- 에러 보고 방식도 언어에 맞게 구성할 수 있다.

예를 들면 아래와 같다.

- Python: `int.to_bytes`, `int.from_bytes`, `str.encode`, `bytes.decode`
- Rust: `u32::to_be_bytes`, `u32::from_be_bytes`, `String::from_utf8`
- Node: `Buffer.writeUInt32BE`, `buf.readUInt32BE`, `Buffer.from(text, "utf8")`

즉 규칙은 공유하되, 구현은 바인딩별 `RoutingId`에 둔다.

이 문서는 별도 public helper 계층을 권장하지 않는다.
예를 들어 아래 성격의 API는 정리 대상이다.

- `routing_id_from_u32(...)`
- `routing_id_to_u32(...)`
- `routing_id_to_text(...)`
- `routing_id_to_hex(...)`
- `RoutingIdHelper`, `RoutingIdCodec`, `routing_id_utils`

## 7. 공통 검증 규칙

각 binding의 `RoutingId` 변환 메서드는 아래 규칙을 맞춰야 한다.

- `from_u32`: 항상 4-byte big-endian
- `to_u32`: 길이 4가 아니면 실패
- `from_text`: UTF-8 encode, NUL 포함 문자열 거부
- `to_text`: UTF-8 decode 실패 시 실패
- `to_hex`: 항상 성공

helper는 아래처럼 조용히 보정하면 안 된다.

- 길이가 4가 아닌데 `to_u32()`에서 잘라 쓰기
- invalid UTF-8을 조용히 다른 문자열로 바꾸기
- overflow 값을 자동 truncate하기

## 8. binding `RoutingId` 초안

### 8.1 Python

권장 모양:

```python
class RoutingId:
    @classmethod
    def from_bytes(cls, data: bytes) -> "RoutingId": ...
    @classmethod
    def from_u32(cls, value: int) -> "RoutingId": ...
    @classmethod
    def from_text(cls, text: str) -> "RoutingId": ...
    def to_bytes(self) -> bytes: ...
    def to_u32(self) -> int: ...
    def to_text(self) -> str: ...
    def to_hex(self) -> str: ...
```

### 8.2 Rust

권장 모양:

```rust
impl RoutingId<'_> {
    pub fn copy(&self) -> RoutingId<'static>;
    pub fn as_bytes(&self) -> &[u8];
    pub fn to_u32(&self) -> Result<u32, RoutingIdError>;
    pub fn to_text(&self) -> Result<&str, RoutingIdError>;
    pub fn to_hex(&self) -> String;
}

impl RoutingId<'static> {
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, RoutingIdError>;
    pub fn from_u32(value: u32) -> Self;
    pub fn from_text(text: &str) -> Result<Self, RoutingIdError>;
}
```

### 8.3 Node

권장 모양:

```ts
class RoutingId {
  static fromBytes(bytes: Buffer | Uint8Array): RoutingId;
  static fromU32(value: number): RoutingId;
  static fromText(text: string): RoutingId;
  asBuffer(): Buffer;
  copy(): RoutingId;
  toU32(): number;
  toText(): string;
  toHex(): string;
}
```

여기서 `asBuffer()`는 owner가 살아 있는 동안만 유효한 borrowed bytes accessor이고,
독립 보관이 필요하면 `copy()`를 호출한다.

## 9. sample과 guide 방향

sample과 guide는 raw bytes만 직접 다루는 예제를 기본 경로로 두지 않는 것이 좋다.

권장 우선순위는 아래와 같다.

1. 텍스트 기반 routed id 예제는 `from_text` / `to_text` 사용
2. `STREAM` 예제는 `from_u32` / `to_u32` 사용
3. raw bytes 예제는 low-level 또는 interop 설명에서만 사용

즉 guide는 helper 중심으로 보여 주고, raw bytes는 escape hatch로 남긴다.

## 10. binding 작업 범위

이 초안 기준으로 실제 변경이 시작되면 binding 쪽 작업 범위는 아래를 포함한다.

- core 변경을 각 binding의 언어별 native 폴더 또는 동기화 대상 파일에 먼저 반영
- `bindings/python/`, `bindings/rust/`, `bindings/node/` 등 각 언어의
  `RoutingId` wrapper 정리
- raw bytes canonical 유지 여부 확인
- `RoutingId` 타입에 `from_u32`, `to_u32`, `from_text`, `to_text`, `to_hex` 추가
- 별도 public helper 계층 제거 또는 내부 구현으로 축소
- binding library 뿐 아니라 sample, perf 경로까지 함께 정리
- `STREAM` packet/raw callback, send, recv 예제의 helper 사용 경로 정리
- 일반 routed/service 예제의 text helper 사용 경로 정리
- 바인딩 테스트에 변환 성공/실패 케이스 추가
- 각 binding spec 문서와 sample 문서 wording 수정

이 문서의 범위는 binding 경계에 집중하지만, 실제 구현에서는 companion draft에 적은
`core/include`, `core/src`, `perf`, `doc/` 변경과 함께 맞물려 진행된다.

## 11. binding 구현 순서

binding 쪽은 아래 순서로 진행하는 것이 좋다.

운영 원칙은 아래와 같다.

- 특별한 blocker가 없으면 중간 보고 없이 다음 단계로 이어서 진행한다.
- native 동기화, library, sample, perf, test를 끊어서 따로 끝내지 않고 순서대로 마무리한다.
- 보고는 최종 완료 상태와 최종 검증 결과를 기준으로 한다.

1. core 변경을 binding native 폴더 또는 동기화 대상 파일에 반영
2. `RoutingId` raw bytes canonical 유지 확인
3. `RoutingId` 타입에 `from_u32`, `to_u32`, `from_text`, `to_text`, `to_hex` 추가
4. 별도 public helper 정리
5. `STREAM` callback/send helper 사용 경로 정리
6. routed/service helper 사용 경로 정리
7. binding library, sample, perf 갱신
8. sample과 test 갱신
9. binding spec 문서 반영

구체적인 적용 순서는 저장소 상태와 각 binding의 준비 정도에 맞춰 조정할 수 있다.

## 12. 비목표

이 초안은 아래를 목표로 하지 않는다.

- raw bytes 기반 `RoutingId` 제거
- `STREAM`만 다른 public 타입으로 분리
- routed id를 모두 문자열이라고 가정
- 모든 binding이 같은 API 이름을 정확히 그대로 쓰게 강제
- 별도 public helper 계층 유지

이 문서의 목표는 binding 표면이 서로 다른 타입 체계로 흩어지지 않게 하는 데 있다.
