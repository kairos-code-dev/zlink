[스펙 목차](../README.ko.md)

# Draft -- Routing ID Byte Canonical Form And Conversion Helpers

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더, 바인딩 테스트가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `routing_id`의 공개 표현을 바이트 열로 해석하고, 그 위에서 쓰는
정수/문자열 변환 규칙을 정리한다.

이 문서는 helper 의미와 실패 규칙에 집중한다. C ABI, receive 수명 규칙,
binding public receive surface는
[`routing-id-borrowed-routingid.ko.md`](routing-id-borrowed-routingid.ko.md)를
우선 기준으로 본다.

핵심 목표는 아래와 같다.

- socket 종류에 따라 공개 `routing_id` 타입이 갈라지지 않게 한다.
- `STREAM`, `ROUTER`, `DEALER`, `SPOT`, monitor/event 경로를 하나의 운반 형태로 맞춘다.
- 응용 코드가 매번 직접 encode/decode를 반복하지 않게 한다.
- 바인딩마다 제각각 다른 변환 규칙이 생기지 않게 공통 규칙을 먼저 정한다.

## 2. 배경

현재 `routing_id`는 core와 C API에서는 길이와 바이트 열로 표현한다.

이 표현은 유연하지만, 실제 응용 코드에서는 아래 불편이 반복된다.

- `STREAM`에서는 4바이트 값을 정수처럼 보고 싶다.
- 다른 routed socket에서는 UUID나 텍스트 이름처럼 보고 싶다.
- 하지만 실제 API 표면에서는 바이트 열을 직접 다뤄야 해서 변환 코드가 반복된다.
- 바인딩마다 `STREAM`은 숫자, 다른 경로는 문자열처럼 따로 public 타입을 만들면
  공용 문서와 공용 surface가 복잡해진다.

이 초안은 이 문제를 아래처럼 풀려고 한다.

- 공개 운반 형태는 바이트 열로 본다.
- 정수/문자열 해석은 helper 규칙으로 분리한다.

즉 "운반 타입은 하나로 유지하고, 해석 helper를 추가한다"는 방향이다.

## 3. 정책 요약

이 초안의 기본 정책은 아래와 같다.

- 공개 `routing_id` 표현은 전부 바이트 열로 본다.
- `STREAM`도 예외를 두지 않는다. 다만 `STREAM routing_id`는 관례상 4바이트 값으로 다룬다.
- 일반 routed socket과 service 계열의 `routing_id`는 여전히 opaque bytes로 본다.
- 응용 편의를 위해 `u32`, `text`, `hex` 변환 helper를 둔다.
- core C는 공통 규칙을 설명하는 helper를 둘 수 있다.
- binding은 같은 규칙을 따르되, public 변환 API는 각 binding의 `RoutingId`
  타입에 흡수한다.

## 4. canonical 의미

이 초안은 canonical 의미를 아래처럼 해석한다.

- `routing_id`의 본질은 **길이가 있는 바이트 열**이다.
- 이 바이트 열의 의미는 socket 종류나 사용 맥락에 따라 달라질 수 있다.
- 공개 API는 그 의미를 강제로 문자열이나 정수로 고정하지 않는다.

즉 canonical 타입은 "이 값이 문자열인지, 정수인지, UUID인지"를 스스로 설명하지
않는다. 그 해석은 helper 함수나 `RoutingId` 변환 메서드가 맡는다.

구체적인 C ABI shape는 이 문서의 범위가 아니다.
그 부분은 borrowed `RoutingId` draft를 기준으로 따로 정한다.

## 5. socket별 의미

공개 표현은 같지만, 실제 의미는 아래처럼 다를 수 있다.

| 경로 | 공개 표현 | 의미 |
|------|-----------|------|
| `STREAM` | 바이트 열 | 4바이트 connection id |
| `ROUTER` / `DEALER` | 바이트 열 | peer identity bytes |
| `SPOT` / Discovery / Registry | 바이트 열 | 논리 주소 또는 peer identity bytes |
| monitor/event | 바이트 열 | 해당 경로가 보고하는 subject 또는 peer identity |

여기서 중요한 점은 아래와 같다.

- `STREAM`도 공개 표면에서는 바이트 열이다.
- 다만 `STREAM` helper는 이 값을 4바이트 big-endian `uint32`로 해석할 수 있다.
- 다른 경로는 텍스트로 보일 수 있어도 계약상은 여전히 opaque bytes다.

또한 이 초안은 아래 상황을 함께 고려한다.

- 어떤 receive surface나 monitor/event surface는 **빈 routing id**를 돌려줄 수 있다.
- raw bytes 경로는 이런 빈 값을 그대로 표현할 수 있어야 한다.
- 다만 `to_u32()`와 `to_text()` 같은 typed helper는 형식 조건이 맞지 않으면 실패해야 한다.

## 6. 변환 helper

이 초안은 최소한 아래 helper 범주를 둔다.

### 6.1 정수 변환

- `from_u32`
- `to_u32`

규칙은 아래와 같다.

- `from_u32`는 항상 **4바이트 big-endian**으로 저장한다.
- `to_u32`는 길이가 정확히 4바이트일 때만 성공한다.
- 길이가 4바이트가 아니면 조용히 잘라 쓰거나 0으로 채우지 않고 실패해야 한다.

예시는 아래와 같다.

```text
value = 0x01020304
bytes = 01 02 03 04
```

### 6.2 문자열 변환

- `from_text`
- `to_text`

규칙은 아래와 같다.

- 문자열은 UTF-8로 encode한다.
- `from_text`는 NUL 문자를 포함한 문자열을 허용하지 않는다.
- 빈 문자열 허용 여부는 구현에서 분명히 정해야 한다.
  이 초안은 기본적으로 `from_text("")`는 허용하지 않는 방향을 권장한다.
  다만 raw bytes 경로는 API가 돌려주는 빈 routing id를 그대로 다룰 수 있어야 한다.
- `to_text`는 유효한 UTF-8일 때만 성공한다.

이 초안은 `to_text_lossy` 같은 별도 public 변환 API는 기본 규칙에 넣지 않는다.
문자열 해석이 실패하면 `to_hex`를 fallback으로 쓰는 쪽을 기준으로 본다.

### 6.3 표시용 변환

- `to_hex`

이 helper는 언제나 성공해야 하며, 로그와 디버그 출력에 쓴다.

`to_hex`는 문자열 해석이 불가능한 `routing_id`도 사람 눈으로 확인할 수 있게 해 준다.

## 7. C API 방향

이 초안은 core C helper의 의미를 아래처럼 본다.
정확한 시그니처와 buffer 규약은 borrowed `RoutingId` draft를 따른다.

```c
zlink_config_result_t zlink_routing_id_from_u32 (
  ...);

zlink_config_result_t zlink_routing_id_to_u32 (
  ...);

zlink_config_result_t zlink_routing_id_from_text (
  ...);

zlink_config_result_t zlink_routing_id_to_text (
  ...);
```

다만 이 초안의 핵심은 "반드시 C API helper만 써야 한다"가 아니다.

핵심은 아래 두 가지다.

- canonical encoding rule이 문서로 고정되어야 한다.
- core helper와 binding `RoutingId` 메서드가 같은 규칙을 따라야 한다.

즉 C API helper는 공통 기준을 제공하는 한 방법일 뿐이다.

## 8. binding 구현 원칙

이 초안은 binding 변환 규칙을 **각 언어의 `RoutingId` 타입 안에서 직접 구현하는
방향**을 권장한다.

그 이유는 아래와 같다.

- `u32 <-> 4-byte big-endian`
- `text <-> UTF-8 bytes`

이 두 변환은 대부분 언어 표준 기능만으로 쉽게 구현할 수 있다.

바인딩이 직접 구현하면 아래 장점이 있다.

- 언어별 자연스러운 타입을 바로 쓸 수 있다.
- FFI 왕복을 줄일 수 있다.
- 예외, `Result`, `TypeError` 같은 에러 방식을 언어에 맞출 수 있다.
- 테스트도 바인딩 단위에서 간단하게 작성할 수 있다.

즉 규칙은 공통으로 두되, 구현은 바인딩별 `RoutingId`에 둔다.

이 문서는 별도 public helper 계층을 권장하지 않는다.
예를 들어 아래 성격의 public API는 정리 대상이다.

- `routing_id_from_u32(...)`
- `routing_id_to_u32(...)`
- `routing_id_to_text(...)`
- `routing_id_to_hex(...)`
- `RoutingIdHelper`, `RoutingIdCodec`, `routing_id_utils`

## 9. 권장 API 모양

이 초안은 아래처럼 "`RoutingId` + 변환 메서드" 구성을 권장한다.

### 9.1 공통 개념

- raw bytes 생성
- 정수에서 생성
- 문자열에서 생성
- 정수로 해석
- 문자열로 해석
- hex로 표시

### 9.2 binding 예시

언어마다 이름은 달라도 아래 성격은 맞추는 것을 권장한다.

```text
RoutingId.from_bytes(...)
RoutingId.from_u32(...)
RoutingId.from_text(...)
rid.to_bytes()
rid.to_u32()
rid.to_text()
rid.to_hex()
```

여기서 `to_u32()`와 `to_text()`는 실패 가능한 API로 보는 것이 맞다.

예를 들어 아래 경우는 실패해야 한다.

- `to_u32()` 호출 대상 길이가 4바이트가 아님
- `to_text()` 대상이 유효한 UTF-8이 아님

### 9.3 binding별 helper 초안

이 초안은 구현 전 논의 단계에서 아래 정도의 helper shape를 기준으로 검토하는 것을
권장한다.

#### Python

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

#### Rust

```rust
impl RoutingId {
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, RoutingIdError>;
    pub fn from_u32(value: u32) -> Self;
    pub fn from_text(text: &str) -> Result<Self, RoutingIdError>;
    pub fn as_bytes(&self) -> &[u8];
    pub fn to_u32(&self) -> Result<u32, RoutingIdError>;
    pub fn to_text(&self) -> Result<String, RoutingIdError>;
    pub fn to_hex(&self) -> String;
}
```

#### Node

```ts
class RoutingId {
  static fromBytes(bytes: Buffer | Uint8Array): RoutingId;
  static fromU32(value: number): RoutingId;
  static fromText(text: string): RoutingId;
  toBytes(): Buffer;
  toU32(): number;
  toText(): string;
  toHex(): string;
}
```

이 예시는 helper의 성격을 설명하기 위한 것이다.
최종 이름과 예외 방식은 언어별 binding spec에서 조정할 수 있지만, 변환 기능은
독립 helper가 아니라 `RoutingId` 타입에 붙는 것을 기준으로 본다.

## 10. error 규칙

변환 helper는 입력이 잘못된 경우 조용히 다른 값으로 바꾸면 안 된다.

공통 원칙은 아래와 같다.

- 길이 불일치: 실패
- UTF-8 decode 실패: 실패
- 입력 포인터/출력 버퍼가 잘못됨: 실패
- 숫자 범위 초과: 실패

즉 helper는 "대충 편한 값으로 바꾸는 함수"가 아니라, "정해진 규칙으로 안전하게
변환하는 함수"여야 한다.

## 11. 예시 시나리오

### 11.1 STREAM

응용은 `STREAM`에서 받은 `routing_id`를 raw bytes로 받는다.
필요하면 helper로 `uint32`로 바꿔 쓴다.

```text
source_rid bytes = 00 00 00 2A
to_u32(source_rid) = 42
```

### 11.2 ROUTER

응용은 `ROUTER` peer id를 raw bytes로 다루되, 실제 값이 UTF-8이면 문자열 helper를
쓸 수 있다.

```text
peer_rid bytes = 63 6C 69 65 6E 74 2D 31
to_text(peer_rid) = "client-1"
```

### 11.3 비텍스트 ID

어떤 peer id가 임의 바이트라면 `to_text()`는 실패할 수 있다.
이 경우 `to_hex()`를 써야 한다.

```text
peer_rid bytes = A1 FF 00 13
to_text(peer_rid) -> fail
to_hex(peer_rid) = "a1ff0013"
```

## 12. rollout 방향

이 초안이 채택되면 도입 순서는 아래처럼 본다.

1. draft를 확정한다.
2. core/C spec에 canonical rule을 반영한다.
3. binding spec은 `RoutingId` 타입 정의 안에 helper surface를 구체화한다.
4. C API helper가 필요하면 추가한다.
5. 각 binding은 같은 규칙으로 helper를 직접 구현한다.
6. guide와 sample은 raw bytes만 직접 만지는 예제를 줄이고 helper 사용 예제로 옮긴다.

## 13. 작업 범위

이 초안이 실제 구현으로 이어지면 작업 범위는 아래처럼 나뉜다.

### 13.1 `core/include`

- `zlink_routing_id_t` canonical 표현 유지 여부 확인
- 필요한 경우 `routing_id` 변환 helper 선언 추가
- `STREAM`, `ROUTER`, `DEALER`, `SPOT`, monitor/event 함수 설명에서
  `routing_id` 공개 표현이 바이트 열이라는 점을 맞춤
- `to_u32`, `from_u32`, `to_text`, `from_text` helper가 들어가면 errno와 반환 규칙을
  함께 정의

### 13.2 `core/src`

- 새 helper를 C API로 제공하면 실제 encode/decode 구현 추가
- `STREAM` 경로가 내부에서 4바이트 id를 쓰더라도 외부 공개 surface와 충돌하지 않는지
  확인
- monitor, service, discovery, spot, socket API 경로에서 `routing_id`를 문자열이나
  정수로 암묵 해석하는 코드가 없는지 점검
- helper 추가 시 단위 테스트와 에러 경로를 함께 보강

### 13.3 `perf`

- perf helper나 샘플 코드가 `STREAM` id를 별도 public 타입처럼 가정하는지 점검
- `STREAM` 관련 경로는 raw bytes를 받되 helper로 `u32`로 해석하는 예제로 정리
- ROUTER/DEALER/SPOT 계열은 텍스트 helper 또는 hex helper를 쓰는 쪽으로 예시 정리
- perf 문서에서 `routing_id` 표현을 설명하는 부분이 있으면 바이트 열 기준으로 수정

### 13.4 `core sample`

- `core/samples/`가 새 helper 모델과 설명 규칙을 따르도록 정리
- `STREAM` sample은 `u32` helper 관점, routed/service sample은 `text`/`hex` helper
  관점으로 맞춤
- sample policy 문서와 실제 sample 코드가 같은 모델을 따르는지 점검

### 13.5 `bindings/`

- core 변경이 각 binding의 언어별 native 폴더에 반영되어야 하는지 점검
- native wrapper, generated binding input, copied header/source가 있으면 core 기준으로 동기화
- 동기화 뒤 binding library, sample, perf가 같은 helper 규칙을 따르도록 정리

- 각 binding의 `RoutingId` wrapper 또는 동등 타입을 raw bytes canonical로 통일
- `from_bytes`, `to_bytes`, `from_u32`, `to_u32`, `from_text`, `to_text`, `to_hex`
  helper 추가
- `STREAM` callback/send surface가 별도 정수 타입을 노출하지 않도록 정리
- 일반 routed/service surface가 문자열 전용 타입을 canonical처럼 취급하지 않도록 정리
- 바인딩별 테스트에 정수/문자열 변환 규칙과 실패 케이스를 추가

### 13.6 `doc/`

- core spec: `routing_id` 공개 표현 설명을 바이트 열 기준으로 맞춤
- binding spec: helper surface와 marshaling 규칙을 언어별로 반영
- guide: `STREAM`은 `u32` helper, 다른 경로는 `text`/`hex` helper를 쓰는 예제로 갱신
- internals: 내부적으로 `STREAM`이 4바이트 id를 쓰는 구조가 있으면 구현 상세로만 설명
- draft 문서 간 충돌 표현 제거

이 작업은 단일 파일 수정으로 끝나는 변경이 아니라, 계약 문서, helper API,
sample, test를 함께 맞춰야 하는 횡단 변경이다.

## 14. 구현 순서

이 초안 기준으로 실제 구현은 아래 순서를 권장한다.

운영 원칙은 아래와 같다.

- 특별한 blocker가 없으면 단계 중간 산출물에서 멈추지 않는다.
- 한 단계가 끝나면 바로 다음 단계로 이어서 진행한다.
- 중간 보고보다 최종 완료 상태와 최종 검증 결과를 우선한다.

### 14.1 1단계 -- `core/include`

먼저 공개 계약과 helper 규칙을 헤더 기준으로 고정한다.

주요 작업:

- `routing_id` 공개 표현이 바이트 열이라는 점을 헤더 문서와 spec wording에 반영
- C API helper를 둘지 여부 결정
- helper를 넣는다면 함수 이름, errno, 반환 코드, buffer 규약 확정

완료 기준:

- `core/include/zlink.h`와 core spec wording이 충돌하지 않음
- `from_u32`, `to_u32`, `from_text`, `to_text` 규칙이 문서로 확정됨

### 14.2 2단계 -- `core/src`

공개 계약이 고정되면 C API helper와 공통 검증 경로를 구현한다.

주요 작업:

- helper 구현 추가 또는 기존 유틸 정리
- invalid length, invalid UTF-8, invalid buffer 경로 처리
- 관련 단위 테스트 추가

완료 기준:

- C API helper가 있으면 round-trip 테스트가 통과
- helper 실패 경로의 errno와 반환 코드가 문서와 일치

### 14.3 3단계 -- `core perf`와 `core sample`

binding으로 내려가기 전에 core 쪽 perf와 sample을 먼저 맞춘다.

주요 작업:

- `core/perf/` 경로가 새 helper 설명 모델과 충돌하지 않도록 정리
- `core/samples/` 경로를 `STREAM u32 helper`, routed/service `text`/`hex` helper
  모델로 정리
- sample policy와 실제 sample이 같은 규칙을 따르는지 확인

완료 기준:

- core perf와 sample이 새 `routing_id` helper 규칙과 모순되지 않음
- core 기준 예제가 binding 쪽으로 내려갈 준비가 됨

### 14.4 4단계 -- binding native 동기화

core 쪽 변경을 끝낸 뒤, 각 binding의 언어별 native 폴더나 동기화 대상 파일을
최신화한다.

주요 작업:

- copied header/source가 있으면 core 기준으로 갱신
- generated native bridge input이 있으면 새 규칙을 반영
- 각 binding이 참조하는 native layer와 core 헤더가 어긋나지 않게 맞춤

완료 기준:

- binding native 폴더의 헤더/bridge/source가 core 변경과 일치
- binding 라이브러리 작업을 시작할 수 있는 기준점이 확보됨

### 14.5 5단계 -- `bindings/`

그 다음 각 binding의 라이브러리, sample, perf를 맞춘다.

예시 적용 순서:

1. Python
2. Rust
3. Node
4. 나머지 binding

주요 작업:

- raw bytes canonical 유지
- `from_u32`, `to_u32`, `from_text`, `to_text`, `to_hex` 추가
- `STREAM` callback/send surface가 별도 정수 canonical 타입을 노출하지 않도록 정리
- routed/service surface가 문자열 canonical 타입을 전제로 삼지 않도록 정리
- binding sample과 perf를 helper 기준으로 정리

완료 기준:

- binding helper round-trip 테스트 통과
- `STREAM` 예제는 `u32` helper, routed 예제는 `text` helper로 동작
- 기존 raw bytes 경로도 계속 동작

### 14.6 6단계 -- `doc/`

마지막으로 정식 문서와 guide를 draft 기준으로 반영한다.

주요 작업:

- core spec 반영
- binding spec 반영
- guide 예제 갱신
- internals 문서는 내부 `STREAM` 4바이트 id 사용을 구현 상세로만 설명

완료 기준:

- draft와 정식 spec 사이의 모순이 없음
- guide가 helper 중심 예제를 제공

## 15. 단계별 검증

각 단계는 아래 검증을 통과해야 다음 단계로 넘어가는 것이 좋다.

### 15.1 계약 검증

- 헤더 선언과 spec wording 일치
- helper 규칙의 byte order, UTF-8, 실패 조건이 문서에 명확히 적힘

### 15.2 C API 검증

- `u32 -> bytes -> u32` round-trip
- `text -> bytes -> text` round-trip
- 빈 routing id를 raw bytes 경로에서 그대로 유지하는지 검증
- invalid length / invalid UTF-8 / invalid buffer 실패 검증

### 15.3 binding 검증

- Python/Rust/Node helper 단위 테스트
- `STREAM` 예제에서 `to_u32()` 사용 검증
- routed/service 예제에서 `to_text()` 또는 `to_hex()` 사용 검증

### 15.4 회귀 검증

- 기존 raw bytes `RoutingId` 경로가 깨지지 않음
- monitor/event/service surface에서 `routing_id` 처리 회귀 없음
- perf/sample smoke가 helper 적용 후에도 정상 동작
- core 변경과 binding native 동기화 뒤 라이브러리/sample/perf가 같은 규칙으로 동작

## 16. 비목표

이 초안은 아래를 요구하지 않는다.

- canonical `routing_id`를 문자열 타입으로 바꾸는 일
- `STREAM`만 public에서 별도 정수 타입으로 분리하는 일
- opaque bytes를 완전히 숨기는 일
- 기존 low-level raw bytes 경로를 제거하는 일

이번 초안의 목적은 "공개 운반 형태는 바이트 열로 통일하고, 응용 편의를 위해
변환 helper를 추가한다"는 규칙을 먼저 분명히 세우는 데 있다.
