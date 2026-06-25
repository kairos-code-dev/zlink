# Framework Stream 압축 확장 계획

## 목적

STREAM payload 압축은 frame payload를 보내기 전에 bytes를 변환하고, 받을 때 원래 bytes로 복원하는
runtime 정책이다. JSON, MessagePack, Protobuf 같은 payload codec은 업무 객체와 bytes 사이를 바꾸는
직렬화 정책이다. 두 정책은 적용 위치와 실패 의미가 다르므로 같은 registry에 섞지 않는다.

이 계획의 목적은 현재 LZ4에 고정된 STREAM 압축 처리를 codec extension과 비슷한 설정 방식으로
확장하는 것이다. 기본 활성 compression codec은 LZ4로 유지하고, 사용자가 connector와 server/framework
양쪽에 같은 압축 알고리즘을 설정할 수 있게 한다. 기본 codec이 LZ4라는 말은 모든 payload를 자동으로
압축한다는 뜻이 아니다. 실제 압축은 기존처럼 `.compress()`를 호출한 frame에만 적용한다.

wire frame에는 어떤 알고리즘인지 기록하지 않는다. 기존 `payload_compressed` flag만 유지한다. 압축된
frame을 주고받는 endpoint는 같은 compression codec을 runtime 설정으로 맞춰야 한다. 알고리즘이 다르면
수신 측은 payload를 복원하지 못하고 decompression error를 반환한다.

## 범위

| 영역 | 포함 |
|------|------|
| .NET stream connector | connector options에 compression codec 설정 표면 추가, LZ4 기본 구현과 custom 구현 연결 |
| .NET server/framework | stream session send/reply/dispatch 경로가 framework compression 설정을 사용하도록 정리 |
| C++ stream connector | `compression_t::lz4` 고정 switch를 compression codec provider로 분리 |
| C++ server/framework | stream runtime의 compress/decompress 경로가 framework options의 compression 설정을 사용하도록 정리 |
| Java stream connector | `ZLinkStreamCompression.LZ4` 고정 처리와 `ZLinkStreamLz4Pickler` 호출을 설정된 codec으로 분리 |
| Java server/framework | session send/reply/dispatch runtime에 같은 compression 설정 표면 적용 |
| Kotlin framework/connector 사용 | Java runtime 위의 Kotlin 설정 DSL, connector 사용 예제, sample, guide에 같은 compression 설정 노출 |
| Node stream connector | LZ4 pickle 고정 함수 대신 configured compression codec 사용 |
| Node server/framework | stream/session/actor send/reply/dispatch 경로에 같은 compression 설정 적용 |
| 문서와 테스트 | spec draft, guide, internals, cross-language 회귀 테스트 항목 반영 |

범위 밖:

- frame header에 compression algorithm id 추가
- 연결 시작 시 compression negotiation 추가
- message마다 다른 알고리즘을 고르는 API 추가
- `.compress("zstd")`처럼 업무 호출부에서 알고리즘을 고르는 API 추가
- payload codec registry와 compression registry 통합
- HTTP client response compression 정책 변경

## 원칙

1. `payload_compressed` flag의 wire 의미는 유지한다. 이 flag는 "payload가 runtime에 설정된 compression
   codec으로 변환되어 있다"는 뜻이다.
2. frame에는 알고리즘 이름이나 번호를 넣지 않는다. 같은 연결 또는 같은 통신 그룹의 endpoint가 같은
   compression codec을 설정해야 한다.
3. `.compress()`는 per-call 의도만 표시한다. 어떤 알고리즘을 쓸지는 connector 또는 server/framework
   runtime 설정이 결정한다.
4. 별도 설정을 하지 않은 runtime의 활성 compression codec은 LZ4다. 사용자가 compression을 명시적으로
   disable한 상태에서 `.compress()`를 호출하면 send 단계에서 compression error로 실패한다.
5. compression을 명시적으로 disable한 runtime이 compressed frame을 받으면 설정 오류로 분류할 수 있는
   decompression error로 실패한다. 이 문서에서는 전용 error code를 새로 만들지 않는다. 각 언어는 기존
   decompression error 계층 안에서 "compression codec is not configured" 메시지를 일관되게 사용한다.
6. 압축 해제 결과는 기존 `maxReceivePayloadSize` 또는 같은 의미의 수신 payload 제한을 반드시 통과해야
   한다. codec 구현이 압축 포맷의 원본 크기를 먼저 검사하더라도 runtime은 최종 복원 bytes 길이를 다시
   검사한다.
7. handler, request, reply, session, actor의 업무 API signature는 압축 알고리즘 변경 때문에 바꾸지 않는다.
8. custom compression codec은 built-in LZ4와 같은 설정 경로를 탄다.
9. compression registry는 payload codec registry와 별도로 둔다. payload codec은 업무 객체와 bytes
   사이를 바꾸고, compression codec은 이미 만들어진 bytes를 압축하거나 복원한다.
10. 한 connector 또는 server/framework runtime에서 활성 compression codec은 하나다. 설정 API를 두 번
    호출했을 때 마지막 설정으로 교체할지, configuration error로 막을지는 언어별 builder 관례에 맞추되,
    같은 runtime에서 여러 알고리즘을 동시에 선택하는 의미는 제공하지 않는다.

## 현재 기준 상태

이 절은 계획 작성 시점의 구현 방향을 파악하기 위한 기준이다. 현재 구조는 언어마다 이름은 다르지만
대체로 `none/lz4` enum과 LZ4 helper에 고정되어 있다.

| 영역 | 현재 상태 | 문제 |
|------|-----------|------|
| .NET connector | 내부 `IZlinkStreamCompressionCodec`가 있고 `ZlinkStreamCompression.Lz4`일 때 LZ4 구현을 만든다. | interface가 internal이고 custom codec을 설정할 public 표면이 없다. |
| .NET framework | stream payload decode 경로가 LZ4 default helper를 직접 호출한다. | server/framework 설정으로 알고리즘을 바꾸기 어렵다. |
| C++ connector | `compression_t::none/lz4` enum과 `lz4_compression_codec_t`가 runtime 내부에 있다. | enum 값 외 알고리즘을 등록할 수 없다. |
| C++ framework | stream runtime이 compression flag를 보고 LZ4 변환을 수행한다. | connector와 같은 compression provider 계약이 없다. |
| Java connector | `ZLinkStreamCompression.NONE/LZ4`와 LZ4 pickler가 options에 묶여 있다. | custom codec 객체를 주입할 수 없다. |
| Java/Kotlin framework | Java runtime의 LZ4 helper를 Kotlin도 함께 사용한다. | Kotlin DSL에서 압축 알고리즘을 설정할 독립 표면이 없다. |
| Node connector | `compressPayload(...)`와 `decompressIfNeeded(...)`가 LZ4 pickle 형식에 고정되어 있다. | Node 사용자 custom codec을 연결할 public options가 없다. |
| Node framework | stream runtime이 LZ4 pickle/unpickle helper를 직접 호출한다. | server와 connector가 같은 compression 계약을 공유하지 않는다. |

## To-be 모델

### 공통 compression codec 계약

각 언어는 public 또는 public-facing 계약으로 아래 의미를 제공한다.

```text
compress(payload) -> compressed payload
decompress(payload, maxDecompressedSize) -> restored payload
```

계약 규칙:

- `compress`는 이미 payload codec으로 encode된 bytes를 입력으로 받는다.
- `decompress`는 frame의 `payload_compressed` flag가 있을 때만 호출한다.
- `decompress`는 복원 후 크기가 `maxDecompressedSize`를 넘으면 실패해야 한다.
- runtime은 `decompress`가 반환한 bytes 길이도 다시 검사한다. 이 검사는 custom codec이 제한을
  잘못 구현해도 handler까지 oversized payload가 전달되지 않게 하기 위한 마지막 방어선이다.
- 실패는 각 언어의 기존 compression/decompression error code 또는 exception 계층으로 매핑한다.
- codec 구현은 thread-safe 또는 runtime에서 안전하게 재사용 가능한 방식이어야 한다. 그렇지 않으면
  registry가 call마다 새 instance를 만들 수 있는 factory 형태를 제공해야 한다.

### 공통 설정 표면

connector와 server/framework는 둘 다 compression 설정을 가진다.

```text
compression().useDefault()  // LZ4
compression().useLz4()
compression().use(customCompression)
compression().disable()
```

언어별 naming은 기존 builder 스타일에 맞춘다. 중요한 점은 connector와 server/framework가 같은 의미를
가져야 한다는 것이다.

활성 codec은 하나만 둔다. 위 목록은 선택지를 설명하기 위한 것이다. 실제 application 설정에서는 이 중
하나의 상태만 선택한다. 같은 builder에서 두 번 호출하는 경우는 언어별 builder 규칙에 맞춰 마지막 값으로
교체하거나 configuration error로 처리한다.

기본 상태는 `useDefault()`와 같고 LZ4를 의미한다. 다만 `.compress()`를 호출하지 않은 frame은 압축하지
않는다. `disable()`은 "기본 LZ4도 사용하지 않는다"는 명시적 선택이다.

### Wire model

wire model은 유지한다.

```text
uncompressed frame:
  payload_compressed flag 없음
  payload = payload codec이 만든 bytes

compressed frame:
  payload_compressed flag 있음
  payload = configured compression codec이 만든 bytes
```

수신 runtime은 flag만 보고 설정된 compression codec을 호출한다. flag 안에는 알고리즘 정보가 없다.

## API 적용 계획

### .NET

#### Connector

목표 표면:

```csharp
var connector = await ZlinkStreamConnector.ConnectAsync(options =>
{
    options.Endpoint = endpoint;

    // LZ4는 기본 제공 compression codec이다.
    options.Compression.UseLz4();
});
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```csharp
var connector = await ZlinkStreamConnector.ConnectAsync(options =>
{
    options.Endpoint = endpoint;

    // 이 connector는 MyCompressionCodec으로 압축된 frame만 복원할 수 있다.
    options.Compression.Use(new MyCompressionCodec());
});
```

record/options 기반 표면을 유지해야 하면 아래처럼 충돌이 없는 형태를 우선한다.

```csharp
var options = new ZlinkStreamConnectorOptions
{
    Endpoint = endpoint,
    Compression = ZlinkStreamCompressionOptions.Lz4()
};
```

변경 항목:

| 항목 | 계획 |
|------|------|
| compression interface | 현재 internal interface와 같은 의미의 public `IZlinkStreamCompressionCodec` 또는 public options wrapper 추가 |
| LZ4 구현 | 현재 LZ4 구현을 built-in codec으로 유지 |
| options | `ZlinkStreamCompression` enum만으로 알고리즘을 고르는 경로를 유지하되, custom codec 설정 표면을 추가 |
| send path | `.Compress()`가 call state flag만 세우고, submit 시점에 options의 codec으로 payload 압축 |
| receive path | `payload_compressed` flag가 있으면 options의 codec으로 payload 복원 |
| error | codec 없음, compression 실패, decompression 실패를 기존 `ZlinkStreamErrorCode`에 매핑 |

#### Server/framework

목표 표면:

```csharp
builder.AddZLinkFramework(options =>
{
    // server가 compressed frame을 보내거나 받을 때 사용할 codec이다.
    options.Streams.Compression.UseLz4();
});
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```csharp
builder.AddZLinkFramework(options =>
{
    // 이 endpoint는 MyCompressionCodec으로 압축된 frame만 복원할 수 있다.
    options.Streams.Compression.Use(new MyCompressionCodec());
});
```

변경 항목:

- `ZLinkStreamProtocolDefaults.Lz4Decompress(...)` 직접 호출을 framework compression provider 경유로 바꾼다.
- session send/reply, managed stream send/reply, stream packet dispatch decode가 같은 provider를 사용한다.
- framework host와 connector를 함께 쓰는 sample은 둘 다 같은 compression 설정을 보여준다.
- ASP.NET adapter가 stream runtime을 만들 때 compression options를 빠뜨리지 않도록 builder state에 포함한다.

### C++

#### Connector

목표 표면:

```cpp
auto options = zlink::stream_connector::options_t{};
options.compression().use_lz4();
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```cpp
auto options = zlink::stream_connector::options_t{};
options.compression().use(std::make_shared<my_compression_codec_t>());
```

또는 기존 options가 값 타입이면 다음 형태를 사용한다.

```cpp
auto options = zlink::stream_connector::options_t{};
options.compression = zlink::stream_connector::compression_options_t::lz4();
```

변경 항목:

| 항목 | 계획 |
|------|------|
| public contract | `compression_codec_t` abstract interface 추가. `compress(bytes)`와 `decompress(bytes, max_size)` 의미를 제공 |
| LZ4 구현 | `lz4_compression_codec_t`를 built-in adapter로 유지하되 public 설정에서 선택 가능하게 함 |
| enum 유지 | `compression_t::lz4`는 source 호환을 위해 options 변환 경로로 둘 수 있다. 새 코드는 provider를 기준으로 작성 |
| runtime | frame sender/reader가 enum switch 대신 resolved codec pointer를 사용 |
| package | LZ4 dependency가 항상 포함되는지, `WITH_LZ4` 같은 선택 feature로 남는지 CMake contract를 명시 |

C++에서 기존 `message_t`를 compression codec 입력 타입으로 재사용하더라도, 그 타입은 opaque byte payload
wrapper로만 다룬다. compression layer는 packet name, codec id, metadata 같은 framework message 의미를
읽거나 만들지 않는다.

LZ4가 선택 feature인 build에서는 `use_lz4()`의 실패 의미를 고정한다. configure 단계에서 LZ4 없는 build를
막을지, runtime에서 `compression_failed` / `decompression_failed`로 실패할지 중 하나를 선택하고 contract
test로 확인한다. 문서와 package metadata는 이 선택을 같은 문장으로 설명해야 한다.

#### Server/framework

목표 표면:

```cpp
options.streams().compression().use_lz4();
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```cpp
options.streams().compression().use(std::make_shared<my_compression_codec_t>());
```

변경 항목:

- framework stream runtime과 connector runtime이 같은 compression codec interface를 공유하거나, 얇은 adapter로
  같은 의미를 유지한다.
- `stream_write_call_t::compress()`는 그대로 두고 submit 시점에 framework options의 codec을 사용한다.
- inbound dispatch는 frame decode 직후 압축을 해제하고, handler에는 복원된 `message_t` 또는 framework
  message만 전달한다.
- CMake layout contract는 base framework target이 불필요하게 custom compression SDK에 묶이지 않는지 확인한다.

### Java

#### Connector

목표 표면:

```java
ZLinkStreamConnector connector = ZLinkStreamConnector.connect(options -> options
    .endpoint(endpoint)
    .compression(compression -> compression.useLz4()));
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```java
ZLinkStreamConnector connector = ZLinkStreamConnector.connect(options -> options
    .endpoint(endpoint)
    .compression(compression -> compression.use(new MyCompressionCodec())));
```

변경 항목:

| 항목 | 계획 |
|------|------|
| public contract | `ZLinkStreamCompressionCodec` interface 추가 |
| LZ4 구현 | `ZLinkStreamLz4Pickler`를 built-in `ZLinkStreamLz4CompressionCodec` 뒤로 숨김 |
| options | 기존 `ZLinkStreamCompression` enum 생성자는 deprecated 또는 adapter 경로로 유지 가능 |
| send path | `compress()` call builder는 flag만 세우고 sender가 configured codec으로 압축 |
| receive path | compressed frame이면 configured codec으로 복원하고 `maxReceivePayloadSize` 적용 |
| error | `CompressionFailed`, `DecompressionFailed`, configuration error를 기존 exception 정책으로 매핑 |

#### Server/framework

목표 표면:

```java
ZLinkFramework.configure(options -> options
    .streams(streams -> streams
        .compression(compression -> compression.useLz4())));
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```java
ZLinkFramework.configure(options -> options
    .streams(streams -> streams
        .compression(compression -> compression.use(new MyCompressionCodec()))));
```

변경 항목:

- `ZLinkStreamPayloadCompression.lz4Pickle/unpickle` 직접 호출을 compression provider로 대체한다.
- Spring Boot integration이 framework stream options를 runtime에 전달한다.
- session send/reply와 dispatch decode가 connector와 같은 compression contract를 사용한다.
- framework core가 LZ4 dependency를 계속 갖는지, 선택 dependency로 분리할지 별도 package contract에서 결정한다.
  이 계획의 1차 목표는 교체 가능한 설정 표면이며 dependency 분리는 별도 단계로 둘 수 있다.

### Kotlin

Kotlin은 Java runtime 위에 있으므로 core compression codec 구현은 Java 계획을 따른다. Kotlin에서 해야 할
작업은 Kotlin DSL, sample, guide를 Java runtime 설정과 같은 의미로 맞추는 것이다.

목표 표면:

```kotlin
ZLinkFramework.configure { options ->
    options.streams { streams ->
        streams.compression { compression ->
            compression.useLz4()
        }
    }
}
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```kotlin
ZLinkFramework.configure { options ->
    options.streams { streams ->
        streams.compression { compression ->
            compression.use(MyCompressionCodec())
        }
    }
}
```

connector 단독 사용:

```kotlin
val connector = ZLinkStreamConnector.connect { options ->
    options.endpoint(endpoint)
    options.compression { compression ->
        compression.useLz4()
    }
}
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```kotlin
val connector = ZLinkStreamConnector.connect { options ->
    options.endpoint(endpoint)
    options.compression { compression ->
        compression.use(MyCompressionCodec())
    }
}
```

변경 항목:

- Kotlin DSL이 Java compression builder를 자연스럽게 감싸도록 한다.
- Kotlin sample은 server와 connector 양쪽에 같은 compression 설정을 둔다.
- suspend session handler, actor, stream send/reply API는 바꾸지 않는다.
- Kotlin guide는 Java 공통 계약을 링크하되 Kotlin 예제로 설정 위치를 보여준다.

### Node.js

#### Connector

목표 표면:

```ts
const connector = await connectStream({
  endpoint,
  compression: (compression) => compression.useLz4()
});
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```ts
const connector = await connectStream({
  endpoint,
  compression: (compression) => compression.use(new MyCompressionCodec())
});
```

object options를 유지할 때는 아래 형태도 가능하다.

```ts
const connector = await connectStream({
  endpoint,
  compression: {
    codec: lz4CompressionCodec()
  }
});
```

변경 항목:

| 항목 | 계획 |
|------|------|
| public contract | `ZlinkStreamCompressionCodec` type 추가 |
| LZ4 구현 | 현재 LZ4 pickle/unpickle helper를 built-in codec 객체로 이동 |
| options | `ZlinkStreamCompression.Lz4` enum 경로는 built-in codec 선택 adapter로 유지 가능 |
| send path | `compressPayload(payload, compression)` 대신 configured codec 호출 |
| receive path | `decompressIfNeeded(header, payload, codec, maxSize)`처럼 codec 객체를 받도록 변경 |
| package | native dependency 없이 유지하는 현재 LZ4 pickle 호환 정책은 built-in codec 문서에 명시 |

#### Server/framework

목표 표면:

```ts
zlinkFramework()
  .streams((streams) => streams
    .compression((compression) => compression.useLz4()));
```

custom codec을 사용할 때는 LZ4 대신 사용자 구현을 설정한다.

```ts
zlinkFramework()
  .streams((streams) => streams
    .compression((compression) => compression.use(new MyCompressionCodec())));
```

변경 항목:

- `lz4Pickle` / `lz4Unpickle` 직접 호출을 configured compression codec 호출로 바꾼다.
- stream session send/reply, actor relay reply, inbound dispatch decode가 같은 provider를 사용한다.
- NestJS module options가 framework compression options를 받을 수 있게 한다.
- Node sample과 contract test는 connector와 server가 같은 custom compression을 설정하는 흐름을 포함한다.

## 구현 순서

1. 공통 plan과 draft spec을 확정한다.
   - 알고리즘은 frame에 기록하지 않는다는 전제를 명시한다.
   - endpoint는 같은 compression codec을 설정해야 한다는 운영 조건을 명시한다.
2. 각 언어 connector에 compression codec contract와 options를 추가한다.
   - 기존 LZ4 동작을 built-in codec으로 감싼다.
   - 기존 `none/lz4` enum 경로는 새 provider로 변환한다.
   - 새 builder에서 compression 설정을 생략하면 LZ4 provider를 사용한다.
   - 기존 enum의 `none` 값은 명시적 `disable()` 선택으로 해석한다.
3. connector send/receive path를 provider 기반으로 바꾼다.
   - `.compress()` call builder는 flag만 세운다.
   - submit/receive 시점에 provider를 사용한다.
4. 각 언어 server/framework options에 같은 compression 설정 표면을 추가한다.
   - builder state, host adapter, runtime constructor를 모두 연결한다.
5. server/framework send/reply/dispatch path를 provider 기반으로 바꾼다.
   - handler에는 항상 압축 해제된 payload를 전달한다.
6. deterministic custom compression test codec을 추가한다.
   - 실제 압축률은 중요하지 않다. codec이 호출되었는지와 mismatched 설정이 실패하는지가 중요하다.
7. 언어별 sample과 guide를 갱신한다.
   - connector와 server 양쪽 설정을 같은 예제로 보여준다.
   - 업무 handler/request/reply API는 바꾸지 않는다.
8. package/dependency contract를 확인한다.
   - built-in LZ4 dependency가 base package에 남는지, optional package로 분리되는지 언어별로 결정하고 문서화한다.

## 회귀 테스트 계획

### 공통 contract 테스트

| ID | 테스트 | 기대 결과 |
|----|--------|-----------|
| C-1 | 기본 options에서 `.compress()`를 호출하지 않은 send/request | payload가 그대로 전송되고 `payload_compressed` flag가 없다. |
| C-2 | 기본 options에서 `.compress()` send | LZ4로 압축되고 `payload_compressed` flag가 있으며 수신 handler는 원본 payload를 받는다. |
| C-3 | 명시적 LZ4 configured 상태에서 `.compress()` request/reply | request와 reply 모두 원본 payload로 복원된다. |
| C-4 | compression을 명시적으로 disabled한 상태에서 `.compress()` 호출 | send 단계에서 compression error가 난다. |
| C-5 | compression을 명시적으로 disabled한 상태에서 compressed frame 수신 | receive 단계에서 decompression error가 나고 메시지는 "compression codec is not configured" 의미를 포함한다. |
| C-6 | custom compression codec configured 상태에서 send/receive | custom codec의 magic bytes 또는 marker가 적용되고 handler는 원본 payload를 받는다. |
| C-7 | 송신 custom codec과 수신 custom codec이 다름 | decompression error가 난다. |
| C-8 | decompressed size가 receive limit을 초과 | decompression error가 나고 handler가 호출되지 않는다. |
| C-9 | zero-length payload 압축 roundtrip | 빈 payload가 정상 복원된다. |
| C-10 | metadata, packet name, request sequence가 compressed frame에서도 보존 | reply matching과 handler selection이 기존과 같다. |
| C-11 | compressed control frame을 거부해야 하는 경로가 있으면 기존 정책 유지 | control frame 정책이 회귀하지 않는다. |
| C-12 | codec registry 변경 없이 compression만 교체 | JSON/MessagePack/Protobuf payload codec 선택과 compression 선택이 서로 독립적으로 동작한다. |
| C-13 | custom codec이 oversized bytes를 반환 | runtime 후검사에서 receive limit 초과로 실패하고 handler가 호출되지 않는다. |
| C-14 | 같은 builder에서 compression codec을 두 번 설정 | 마지막 설정으로 교체되거나 configuration error가 나며, 여러 codec이 동시에 활성화되지는 않는다. |

### Cross-language 상호운용 테스트

알고리즘을 frame에 기록하지 않기 때문에, endpoint 설정이 실제로 같은 의미인지 상호운용 테스트로 확인해야
한다. 각 언어 전체 조합을 매번 실행하지 못하더라도 release gate에는 대표 matrix를 둔다.

| ID | 조합 | 테스트 |
|----|------|--------|
| X-1 | .NET server + Node connector | LZ4 compressed send/request/reply roundtrip |
| X-2 | Java server + C++ connector | LZ4 compressed send/request/reply roundtrip |
| X-3 | Node server + .NET connector | LZ4 compressed dispatch와 reply roundtrip |
| X-4 | C++ server + Java connector | LZ4 compressed dispatch와 reply roundtrip |
| X-5 | 같은 언어 server/connector | deterministic custom codec roundtrip |
| X-6 | 서로 다른 custom codec 설정 | decompression error와 handler 미호출 확인 |

### .NET 테스트

| 영역 | 항목 |
|------|------|
| connector unit | public compression options가 LZ4와 custom codec을 resolve하는지 확인 |
| connector contract | send/request/reply compressed roundtrip, no codec error, mismatched codec error |
| framework unit | `ZLinkStreamPacketPayloadCodec` 또는 대응 runtime이 configured codec을 사용하는지 확인 |
| framework integration | session send/reply와 inbound dispatch가 custom compression으로 roundtrip 되는지 확인 |
| ASP.NET adapter | host builder에서 설정한 compression options가 runtime에 전달되는지 확인 |
| package | custom compression 구현이 framework 업무 API signature를 바꾸지 않는지 contract example로 확인 |

### C++ 테스트

| 영역 | 항목 |
|------|------|
| connector unit | `compression_codec_t` custom 구현 호출 여부 확인 |
| connector protocol | LZ4 compressed fixture와 기존 frame bytes 호환 확인 |
| connector integration | TCP loopback send/request/reply compressed roundtrip |
| framework unit | `stream_write_call_t::compress()`가 configured codec을 사용하는지 확인 |
| framework integration | session dispatch, reply, actor relay compressed roundtrip |
| CMake contract | base target과 optional compression dependency 노출 여부 확인 |
| layout contract | compression provider가 public header에 내부 LZ4 구현 타입을 노출하지 않는지 확인 |

### Java 테스트

| 영역 | 항목 |
|------|------|
| connector unit | `ZLinkStreamCompressionCodec` custom implementation roundtrip |
| connector contract | LZ4 fixture, disabled compression error, receive limit 초과 |
| framework unit | stream payload compression provider가 runtime options에서 resolve되는지 확인 |
| Spring integration | Spring Boot stream host가 compression 설정을 runtime에 전달하는지 확인 |
| fake backend | compressed inbound frame dispatch와 compressed reply write 검증 |
| package | LZ4 dependency 유지 또는 optional 분리 정책을 build file contract로 확인 |

### Kotlin 테스트

| 영역 | 항목 |
|------|------|
| DSL unit | Kotlin compression DSL이 Java builder에 같은 codec을 등록하는지 확인 |
| sample contract | Kotlin server와 Kotlin/Java connector가 같은 compression 설정으로 roundtrip |
| coroutine/suspend handler | compressed dispatch가 suspend handler에서 원본 payload로 보이는지 확인 |
| guide sample compile | 문서 예제의 설정 call shape가 실제 DSL과 맞는지 확인 |

### Node 테스트

| 영역 | 항목 |
|------|------|
| connector unit | `ZlinkStreamCompressionCodec` custom object가 compress/decompress에 사용되는지 확인 |
| connector contract | current LZ4 pickle compatibility, disabled compression error, mismatched custom codec error |
| framework unit | stream runtime send/reply/dispatch가 configured codec을 사용하는지 확인 |
| NestJS contract | module options compression 설정이 framework runtime까지 전달되는지 확인 |
| sample regression | connector와 server 양쪽 custom compression 설정 sample이 실행되는지 확인 |
| package | exported type surface에 internal LZ4 helper가 불필요하게 새지 않는지 확인 |

## 문서 반영 계획

| 문서 위치 | 반영 내용 |
|-----------|-----------|
| `framework/doc/stream-connector/*/guide` | connector에서 compression codec을 설정하는 방법과 endpoint 설정 일치 조건 설명 |
| `framework/doc/framework/*/guide` | server/framework stream 설정에서 compression을 고르는 방법 설명 |
| `framework/doc/framework/*/spec` | public compression options, error 조건, `.compress()` 의미 명시 |
| `framework/doc/framework/*/internals` | `payload_compressed` flag와 runtime compression provider의 관계 설명 |
| `framework/doc/plan` | 이 계획을 기준으로 implementation checklist 유지 |

문서 작성 시 주의할 점:

- guide에는 내부 frame byte layout을 길게 설명하지 않는다. 사용자가 알아야 할 것은 같은 compression
  설정을 양쪽에 둬야 한다는 점이다.
- spec에는 현재 공개 계약만 쓴다. 구현 전 API 초안은 draft 또는 plan에 둔다.
- internals에는 frame flag, max decompressed size, provider 호출 순서처럼 유지보수자가 필요한 구조를 쓴다.
- 예제 코드는 connector와 server가 같은 compression 설정을 가진다는 점을 코드 주석으로 보여준다.

## 완료 기준

1. connector와 server/framework 양쪽에 compression codec 설정 API가 있다.
2. built-in LZ4는 기존 frame과 호환된다.
3. custom compression codec으로 connector send/receive가 동작한다.
4. custom compression codec으로 server/framework send/reply/dispatch가 동작한다.
5. `.compress()`를 호출하는 업무 API signature는 바뀌지 않는다.
6. handler는 압축 해제된 payload만 받는다.
7. compression을 명시적으로 disabled한 상태에서 compressed frame을 받으면 명확한 decompression error가 난다.
8. 알고리즘 불일치는 decompression error로 드러난다.
9. payload codec registry와 compression 설정이 독립적으로 동작한다.
10. .NET, C++, Java, Kotlin, Node.js 회귀 테스트가 각 언어의 connector와 server 경로를 모두 덮는다.
