# Framework Codec Extension 통합 계획

## 목적

framework의 기본 payload codec은 JSON으로 둔다. Protobuf, MessagePack, 사용자 정의 codec은
별도 framework extension으로 추가한다. codec을 바꾸더라도 handler, client, connector의
업무 API는 바뀌지 않아야 한다. 바뀌는 것은 codec extension 등록과 패키지 의존성뿐이다.

## 현재 상태

이 절은 2026-06-18 현재 checkout의 상태를 정리한다. 아래 내용은 목표 상태가 아니라
전환 전 기준이다.

| 영역 | 현재 상태 | 문제 |
|------|-----------|------|
| framework codec registry | JSON, Protobuf, MessagePack 이름을 framework core가 직접 알고 있다. custom serializer는 `addSerializer(...)`로 등록한다. | built-in codec과 custom codec의 등록 방식이 다르고, Protobuf/MessagePack 의존성이 core에 남는다. |
| .NET framework | `Zlink.Framework`가 `Google.Protobuf`, `MessagePack` package를 직접 참조한다. stream packet payload encode/decode도 Protobuf/MessagePack 타입을 직접 검사한다. | 선택 codec을 쓰지 않는 사용자도 binary codec dependency를 가진다. |
| Java/Kotlin framework | `zlink-framework-core`가 `protobuf-java`를 직접 참조하고, `addProtobuf()`가 core serializer 선택에 영향을 준다. | Protobuf가 framework core 기능처럼 보인다. |
| Node framework | framework package 안에 기본 Protobuf serializer 구현이 있고, `addProtobuf()`가 serializer를 암묵적으로 추가한다. | Protobuf가 선택 package가 아니라 framework 내장 기능처럼 보인다. |
| stream connector | 언어별 connector codec package가 따로 있다. 예: `stream-connector-json`, `stream-connector-msgpack`, `stream-connector-protobuf`, `.NET`의 `Systems.Zlink.Stream.Connector.*`. | framework codec registry와 connector codec registry가 나뉘어 같은 codec을 여러 곳에서 관리한다. |
| bindings codec extension | bindings 아래에 언어별 codec package가 따로 있다. 예: `bindings/*/codecs`, `zlink-codec-json`, `zlink-codec-messagepack`, `zlink-codec-protobuf`, `.NET`의 `Systems.Zlink.Codecs.*`. | low-level binding과 framework codec 정책이 섞이고, 같은 codec package를 bindings와 framework 양쪽에서 관리한다. |
| HTTP client | typed body/response 설명과 구현이 JSON 문자열 중심이다. | MessagePack, Protobuf, custom binary codec을 같은 extension으로 적용하기 어렵다. |
| guide/spec 문서 | 여러 guide가 `AddProtobuf`, `addMessagePack`, connector codec helper, typed JSON 같은 기존 구조를 기준으로 설명한다. | 구현을 바꿔도 문서가 이전 package와 helper를 계속 안내할 수 있다. |

현재 상태에서 이미 유지해야 하는 점은 업무 API다. handler signature, client request/reply API,
stream connector typed send/request/on/wait API는 codec 전환의 대상이 아니다.

## To-be 상태

전환 완료 뒤의 목표 상태는 아래와 같다.

| 영역 | 목표 상태 |
|------|-----------|
| framework core | JSON serializer와 codec extension 계약만 가진다. Protobuf/MessagePack 구현 의존성은 없다. |
| built-in extension | Protobuf와 MessagePack은 framework codec extension package로 제공한다. extension은 framework, connector, HTTP client에 필요한 adapter를 함께 제공한다. |
| custom extension | 사용자는 built-in extension과 같은 계약으로 custom codec extension을 만든다. Avro, Thrift, 사내 binary codec도 같은 경로를 탄다. |
| bindings | bindings는 raw `Message`/byte payload와 core protocol API만 제공한다. JSON/Protobuf/MessagePack codec extension package는 bindings에서 제거한다. Python, Go, Rust는 제거 뒤 대체 codec package를 제공하지 않는다. |
| stream connector | connector는 wire protocol과 typed API를 제공한다. codec-specific package는 없고, framework codec extension의 connector adapter를 받아 encode/decode한다. |
| HTTP client | public `body(dto)` / `submit<T>()` 모양은 유지한다. 내부 body는 bytes 기준이고, typed body/response는 등록된 codec extension으로 encode/decode한다. 기본값은 JSON이다. |
| sample | DTO, handler, client 호출 모양은 codec별로 바뀌지 않는다. sample 차이는 dependency와 extension 등록만으로 드러난다. |
| 문서 | guide는 JSON 기본값과 codec extension 등록법을 설명한다. connector codec helper package나 raw serializer helper를 업무 API처럼 소개하지 않는다. |

## 원칙

1. JSON은 framework 기본 기능으로 남긴다.
2. Protobuf와 MessagePack은 framework core 의존성에서 제거하고 선택 extension으로 옮긴다.
3. 사용자가 만든 codec도 built-in codec과 같은 등록 경로를 사용한다.
4. connector 전용 codec package는 제거한다. connector는 framework codec extension이 제공하는
   payload codec adapter를 받아서 encode/decode만 수행한다.
5. HTTP client도 같은 codec extension을 사용한다. 이를 위해 내부 request/response body 표현은
   문자열이 아니라 bytes를 기준으로 바꾼다.
6. handler method, request method, reply type, payload DTO 같은 업무 API는 codec 변경 때문에
   바꾸지 않는다.
7. 문서와 sample은 codec별 helper 사용법을 업무 API처럼 설명하지 않는다. codec은 구성 단계에서
   고르는 선택 사항이고, sample 코드는 같은 DTO와 같은 handler/client API를 유지해야 한다.

## 공통 계약

각 언어는 아래 역할을 분리한다.

| 역할 | 설명 |
|------|------|
| codec extension | codec 이름, content type, serializer를 등록한다. |
| message serializer | 업무 객체와 byte payload 사이를 변환한다. |
| connector adapter | serializer 결과를 stream payload로 감싼다. |
| HTTP adapter | serializer 결과를 HTTP body bytes와 content-type으로 감싼다. |

extension 작성자는 framework, connector, HTTP client마다 다른 codec 구현을 만들지 않는다. 하나의
serializer를 만들고, 필요한 대상에 adapter만 붙인다.

## To-be codec 설정 인터페이스

framework, stream connector, HTTP client는 같은 codec extension을 사용한다. 대상별 builder는
다르지만 설정 모양은 모두 `use(extension)`으로 맞춘다. 사용자는 codec을 바꾸기 위해 handler,
request, response API를 바꾸지 않는다.

### Framework 설정

```csharp
builder.AddZLinkFramework(options =>
{
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    options.Codecs.Use(ZLinkJsonCodec.Default);

    // 선택 codec은 extension package가 제공한다.
    options.Codecs.Use(ZLinkProtobufCodec.Default);
    options.Codecs.Use(ZLinkMessagePackCodec.Default);

    // 사용자 정의 codec도 같은 표면을 쓴다.
    options.Codecs.Use(new AvroCodecExtension(...));
});
```

```java
ZLinkFramework.configure(options -> options
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    .codecs(codecs -> codecs.use(ZLinkJsonCodec.defaultCodec()))
    // 선택 codec은 extension package가 제공한다.
    .codecs(codecs -> codecs.use(ZLinkProtobufCodec.defaultCodec()))
    .codecs(codecs -> codecs.use(new AvroCodecExtension(...))));
```

```ts
zlinkFramework()
  // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
  .codecs((codecs) => codecs.use(zlinkJsonCodec()))
  // 선택 codec은 extension package가 제공한다.
  .codecs((codecs) => codecs.use(zlinkProtobufCodec()))
  .codecs((codecs) => codecs.use(new AvroCodecExtension(...)));
```

C++은 fluent builder와 template registration을 같이 사용하되 의미는 같다.

```cpp
options.codecs()
  // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
  .use(zlink::framework::codecs::json())
  // 선택 codec은 extension package가 제공한다.
  .use(zlink::framework::codecs::protobuf())
  .use(zlink::framework::codecs::message_pack())
  .use(my_avro_codec_extension{});
```

### Stream connector 설정

stream connector는 framework runtime 없이 단독으로도 쓸 수 있다. 이때도 connector 전용 codec
package를 따로 설치하지 않고, framework codec extension package가 제공하는 connector adapter를
등록한다.

```csharp
var connector = ZlinkStreamConnectorFactory.Create(options =>
{
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    options.Codecs.Use(ZLinkJsonCodec.Default);

    // framework codec extension을 connector에도 그대로 적용한다.
    options.Codecs.Use(ZLinkProtobufCodec.Default);
    options.Codecs.Use(new AvroCodecExtension(...));
});

await connector.Request(new AuthenticateReq(...)).SubmitAsync<AuthenticateRes>();
```

```java
ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options -> options
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    .codecs(codecs -> codecs.use(ZLinkJsonCodec.defaultCodec()))
    // framework codec extension을 connector에도 그대로 적용한다.
    .codecs(codecs -> codecs.use(ZLinkProtobufCodec.defaultCodec()))
    .codecs(codecs -> codecs.use(new AvroCodecExtension(...))));

connector.request(new AuthenticateReq(...)).submit(AuthenticateRes.class);
```

```ts
const connector = zlinkStreamConnectorFactory.create({
  endpoint,
  codecs: (codecs) => codecs
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    .use(zlinkJsonCodec())
    // framework codec extension을 connector에도 그대로 적용한다.
    .use(zlinkProtobufCodec())
    .use(new AvroCodecExtension(...))
});

await connector.request(new AuthenticateReq()).submit<AuthenticateRes>();
```

```cpp
auto connector = zlink::stream_connector::connector_t::create(
  zlink::stream_connector::options_t{}
    .codecs([](auto& codecs) {
      // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
      codecs.use(zlink::framework::codecs::json());
      // framework codec extension을 connector에도 그대로 적용한다.
      codecs.use(zlink::framework::codecs::protobuf());
      codecs.use(my_avro_codec_extension{});
    }));

connector.request(authenticate_req_t{...}).submit<authenticate_res_t>();
```

connector 설정의 To-be 원칙:

- `codec` 옵션에 codec별 payload codec 객체를 직접 넘기는 표면은 제거한다.
- `ZLinkStreamProtobuf.decode(...)`, `zlinkStreamJsonCodec`, `codec_traits<T>` 같은 codec별 helper를
  업무 코드에서 호출하지 않는다.
- connector는 등록된 extension의 connector adapter로 typed payload를 encode/decode한다.
- raw packet API는 남긴다. raw API는 codec extension 대상이 아니라 wire payload를 직접 다루는
  고급 표면이다.

### HTTP client 설정

HTTP client도 같은 extension을 사용한다. typed body/response API는 유지하고, codec extension이
HTTP body bytes와 `content-type`을 결정한다.

```csharp
var client = ZLinkHttpClient.Create("https://api.example")
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    .Codecs(codecs => codecs.Use(ZLinkJsonCodec.Default))
    // framework codec extension을 HTTP client에도 그대로 적용한다.
    .Codecs(codecs => codecs.Use(ZLinkProtobufCodec.Default))
    .Codecs(codecs => codecs.Use(new AvroCodecExtension(...)))
    .Build();

var reply = await client.Post("/sessions")
    .Body(new AuthenticateReq(...))
    .SubmitAsync<AuthenticateRes>();
```

```java
ZLinkHttpClient client = ZLinkHttpClient.create("https://api.example")
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    .codecs(codecs -> codecs.use(ZLinkJsonCodec.defaultCodec()))
    // framework codec extension을 HTTP client에도 그대로 적용한다.
    .codecs(codecs -> codecs.use(ZLinkProtobufCodec.defaultCodec()))
    .codecs(codecs -> codecs.use(new AvroCodecExtension(...)))
    .build();

HttpResponse<AuthenticateRes> reply = client.post("/sessions")
    .body(new AuthenticateReq(...))
    .submit(AuthenticateRes.class);
```

```ts
const client = ZLinkHttpClient.create('https://api.example')
  // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
  .codecs((codecs) => codecs.use(zlinkJsonCodec()))
  // framework codec extension을 HTTP client에도 그대로 적용한다.
  .codecs((codecs) => codecs.use(zlinkProtobufCodec()))
  .codecs((codecs) => codecs.use(new AvroCodecExtension(...)))
  .build();

const reply = await client.post('/sessions')
  .body(new AuthenticateReq())
  .submit<AuthenticateRes>();
```

```cpp
auto client = zlink::http_client::client_t::create()
  .base_url("https://api.example")
  .codecs([](auto& codecs) {
    // JSON은 기본값이다. 명시해도 되고 생략해도 된다.
    codecs.use(zlink::framework::codecs::json());
    // framework codec extension을 HTTP client에도 그대로 적용한다.
    codecs.use(zlink::framework::codecs::protobuf());
    codecs.use(my_avro_codec_extension{});
  })
  .build();

auto reply = client.post("/sessions")
  .body(authenticate_req_t{...})
  .submit<authenticate_res_t>();
```

HTTP client 설정의 To-be 원칙:

- `.Json()` 같은 client-wide JSON 모드가 typed body codec 선택을 대표하지 않는다. JSON은 기본
  codec으로 동작하고, 필요한 경우 `codecs.use(JsonCodec)`으로 명시한다.
- raw body, form, multipart, streaming upload는 codec extension 적용 대상이 아니다.
- typed `body(dto)`는 등록된 codec extension으로 encode하고 `content-type`을 설정한다.
- typed `submit<T>()`는 response `content-type`과 등록된 codec extension으로 decode한다.
- response `content-type`에 맞는 extension이 없으면 명확한 payload decode error를 낸다.

### 설정 인터페이스의 책임

`Codecs` 빌더는 codec extension을 framework registry에 등록하는 표면이다. handler나 client가
사용할 API를 늘리지 않는다.

| 인터페이스 | 책임 |
|------------|------|
| `CodecExtension` | codec id, content type, serializer, connector adapter, HTTP adapter를 등록한다. |
| `CodecRegistryBuilder.use(extension)` | extension의 등록 함수를 실행한다. |
| `PayloadSerializer` | 업무 객체와 byte payload 사이를 변환한다. |
| `CodecSelector` | 주어진 payload type과 content type에 맞는 serializer를 고른다. framework 내부 타입이다. |

`addProtobuf()` / `addMessagePack()` 같은 built-in 이름별 메서드는 To-be 공개 표면에서 제거한다.
사용자는 항상 `use(extension)`을 호출한다. JSON은 기본값이므로 별도 설정이 없어도 동작한다.
명시적인 설정이 필요할 때만 `use(JsonCodec)`을 허용한다.

### codec 선택 규칙

codec 선택은 언어마다 다르게 추론하지 않는다. framework, connector, HTTP client는 아래 순서를
같은 의미로 적용한다.

1. 수신 payload에 content type이나 stream codec id가 있으면 그 값과 일치하는 extension을 먼저
   찾는다. 일치하는 extension이 없으면 payload decode error를 낸다.
2. 송신 payload type에 대해 사용자가 명시 등록한 custom serializer가 있으면 그 serializer를 쓴다.
3. Protobuf extension이 등록되어 있고 payload type이 generated Protobuf message이면 Protobuf를 쓴다.
4. MessagePack extension이 등록되어 있고 payload type이 해당 언어의 MessagePack serializer로
   처리 가능하다고 명시되어 있으면 MessagePack을 쓴다.
5. 위 조건에 걸리지 않으면 JSON 기본 serializer를 쓴다.

여러 custom extension이 같은 payload type을 처리한다고 주장하면 구성 오류로 막는다. 우선순위로
조용히 하나를 고르지 않는다. application code가 handler나 request API에서 `ToJson`, `ToProto`,
`decode(...)` 같은 helper를 직접 호출해서 선택 규칙을 우회하면 안 된다.

### extension이 등록해야 하는 값

각 codec extension은 최소한 아래 값을 등록한다.

| 값 | 설명 |
|----|------|
| codec id | stream header나 내부 registry가 쓰는 짧은 식별자. 예: `json`, `protobuf`, `messagepack`, `avro` |
| content type | envelope와 HTTP에 쓰는 MIME type. 예: `application/json`, `application/x-protobuf` |
| serializer | framework channel/spot/actor/session payload encode/decode에 쓰는 serializer |
| connector adapter | stream connector typed send/request/on/wait가 같은 serializer를 쓰도록 연결하는 adapter |
| HTTP adapter | HTTP `body(dto)`와 `submit<T>()`가 같은 serializer를 쓰도록 연결하는 adapter |

extension은 자신이 처리할 수 있는 payload type 판별자를 등록한다. 예를 들어 Protobuf extension은
generated Protobuf message type만 처리하고, 처리할 수 없는 DTO는 JSON 기본 serializer로 넘긴다.
이 fallback은 호출자가 handler나 request API에서 직접 선택하지 않는다.

### custom codec 예시

사용자 정의 codec은 built-in codec과 같은 계약을 구현한다.

```csharp
public sealed class AvroCodecExtension :
    IZLinkFrameworkCodecExtension,
    IZLinkStreamConnectorCodecExtension,
    IZLinkHttpClientCodecExtension
{
    public void RegisterFramework(IZLinkFrameworkCodecRegistryBuilder codecs)
    {
        codecs.RegisterSerializer(
            codecId: "avro",
            contentType: "application/avro",
            serializer: new AvroPayloadSerializer(...),
            canHandle: type => AvroSchemaRegistry.Contains(type));
    }

    public void RegisterConnector(IZLinkStreamConnectorCodecRegistryBuilder codecs)
    {
        codecs.RegisterAdapter("avro", "application/avro", new AvroStreamPayloadAdapter(...));
    }

    public void RegisterHttpClient(IZLinkHttpClientCodecRegistryBuilder codecs)
    {
        codecs.RegisterAdapter("application/avro", new AvroHttpBodyAdapter(...));
    }
}
```

언어별 이름은 다를 수 있지만 최종 형태의 의미는 같다. framework, connector, HTTP client builder는
같은 extension 객체나 같은 extension factory를 받아 각자 필요한 adapter 등록 함수만 호출한다.
application code는 `Use(new AvroCodecExtension(...))`만 호출한다.

## 공개 API 불변 기준

codec 전환 작업은 아래 표면을 바꾸면 안 된다.

| 영역 | 유지해야 하는 표면 |
|------|-------------------|
| channel/spot/actor handler | method 이름, parameter DTO, reply DTO, context parameter |
| framework client | `send/request/publish/submit` 호출 모양과 typed reply 반환 |
| stream connector | typed `send/request/on/wait` 호출 모양과 packet name override 방식 |
| HTTP client | `body(dto)`, `submit<T>()` 또는 언어별 동등 호출 모양 |
| sample DTO | codec별 wrapper DTO를 새로 만들지 않고 기존 message type을 유지 |

변경할 수 있는 범위는 dependency, import, codec extension 등록, 내부 serializer/adapter wiring,
문서 설명뿐이다.

## 단계별 진행 규칙

각 단계에서 코드 수정이 끝나면 바로 다음 단계로 넘어가지 않는다. 수정한 범위에 대해 POSD 기반
리팩토링 점검을 먼저 수행한다. 이 점검은 public API를 새로 넓히기 위한 단계가 아니라, 변경 과정에서
생긴 불필요한 복잡성을 줄이기 위한 단계다.

POSD 점검 항목:

| 항목 | 확인할 내용 |
|------|-------------|
| 깊은 모듈 | codec 변경 때문에 caller가 serializer, adapter, transport detail을 직접 알게 되지 않았는지 확인한다. |
| 정보 은닉 | codec 선택, content type, binary library 의존성, connector adapter 선택이 한 곳에 모여 있고 호출부로 새지 않는지 확인한다. |
| 복잡성을 아래로 | 샘플이나 업무 handler가 codec별 분기, raw bytes 해석, package별 helper 호출을 반복하지 않는지 확인한다. |
| 오류를 정의로 제거 | codec 미등록, 중복 등록, 처리 불가 type 같은 상황이 명확한 구성 오류나 JSON fallback 규칙으로 정리되었는지 확인한다. |
| 두 번 설계 | 새 helper나 public method가 필요해 보이면 기존 extension 계약으로 해결할 수 있는지 먼저 비교한다. |
| 위험 신호 제거 | pass-through wrapper, codec별 중복 builder, sample 전용 adapter, 반복 주석, 임시 변환 helper가 남지 않았는지 확인한다. |

진행 규칙:

- 각 단계의 코드 수정이 끝나면 위 항목을 기준으로 리팩토링 필요 여부를 기록한다.
- 리팩토링이 필요하면 같은 단계 안에서 끝낸 뒤 검증을 실행한다.
- POSD 점검 때문에 public API 변경이 필요해 보이면, 업무 API 불변 기준과 충돌하지 않는지 먼저
  확인한다. 충돌하면 구현을 멈추고 별도 설계 이슈로 분리한다.
- POSD 점검 없이 “테스트 통과”만으로 다음 단계에 넘어가지 않는다.

## 단계

### 1단계: extension 등록 계약 추가

현재 framework codec registry는 built-in 이름과 custom serializer 등록을 직접 다룬다. To-be는
custom codec과 built-in codec이 모두 `use(extension)` 경로를 탈 수 있는 구조다. 이 단계에서는
기존 JSON 동작과 업무 API를 바꾸지 않는다.

완료 기준:

- .NET: `IZLinkCodecExtension`이 `IZLinkCodecRegistryBuilder`에 serializer를 등록할 수 있다.
- Java/Kotlin: `ZLinkCodecExtension`이 `ZLinkCodecRegistryBuilder`에 serializer를 등록할 수 있다.
- Node: `ZLinkCodecExtension`이 `ZLinkCodecRegistryBuilder`에 serializer를 등록할 수 있다.
- custom serializer 테스트가 extension 경로로 통과한다.
- 이 단계에서 기존 `addProtobuf()` / `addMessagePack()`은 바로 삭제하지 않아도 된다. 다만 다음
  단계에서 제거할 deprecated surface로 표시하고, 새 문서와 sample은 `use(extension)`만 사용한다.

문서 반영:

- 공통 framework API 문서에 `addSerializer(...)`와 `use(extension)`의 관계를 설명한다.
- `addProtobuf()` / `addMessagePack()` 예시는 새 문서에 추가하지 않는다.
- 언어별 guide에서는 custom codec을 “새 request API”가 아니라 “구성 단계에 붙이는 extension”으로
  설명한다.

### 2단계: HTTP client body 내부 표현 변경

현재 HTTP client typed body는 JSON 문자열로 고정되어 있다. To-be에서는 public API는 그대로
두고, 내부 request body와 raw response body를 bytes로 다룬다. 그래야 binary codec과 custom
codec을 같은 extension으로 적용할 수 있다.

완료 기준:

- public `body(dto)` / `submit<T>()` 호출 모양은 유지한다.
- raw string body API는 유지한다. 내부 전송은 bytes로 바꾸되, `body(string, contentType)` 같은
  명시 raw API는 codec extension을 거치지 않는다.
- raw response API는 기존처럼 text body를 돌려준다. binary raw response가 필요하면 별도 bytes
  API를 추가하되 기존 text API를 깨지 않는다.
- 기본 codec은 JSON이고, 기존 JSON 테스트는 그대로 통과한다.
- 내부 `HttpRequestSpec` body는 bytes 또는 byte provider를 사용한다.
- response decode는 등록된 codec extension을 통해 수행할 수 있다.

문서 반영:

- `framework/doc/http-client/{dotnet,java,node}/` 아래 overview, getting started, request body,
  response handling 문서를 수정한다.
- “typed JSON”이라고 고정한 문장은 “typed body는 기본 JSON codec을 사용한다”로 바꾼다.
- raw body, form, multipart, streaming upload는 codec extension 대상이 아니라는 점을 분리해서
  설명한다.
- HTTP client spec 문서는 request/response body의 내부 기준이 문자열이 아니라 bytes임을 명시한다.

### 3단계: built-in codec extension 분리

현재 Protobuf와 MessagePack 구현은 framework core나 connector codec package에 흩어져 있다.
To-be에서는 Protobuf와 MessagePack 구현을 framework core에서 별도 extension package로 옮긴다.

extension package는 framework가 작성된 언어만 대상으로 만든다. 즉 `.NET`, Java/Kotlin, Node,
C++ framework slice에 대해 각각 해당 언어의 package를 만든다. Python, Go, Rust binding에는 이
framework extension package를 만들지 않는다. 이 언어들은 bindings codec package를 제거한 뒤
대체 codec package 없이 raw `Message`/bytes API만 유지한다.

package 단위와 프로젝트 작성 위치:

| 언어 | Protobuf extension package | Protobuf 작성 위치 | MessagePack extension package | MessagePack 작성 위치 |
|------|----------------------------|--------------------|-------------------------------|----------------------|
| .NET | `Zlink.Framework.Codecs.Protobuf` | `framework/languages/dotnet/src/Zlink.Framework.Codecs.Protobuf/` | `Zlink.Framework.Codecs.MessagePack` | `framework/languages/dotnet/src/Zlink.Framework.Codecs.MessagePack/` |
| Java/Kotlin | `zlink-framework-codec-protobuf` | `framework/languages/java/zlink-framework-codec-protobuf/` | `zlink-framework-codec-msgpack` | `framework/languages/java/zlink-framework-codec-msgpack/` |
| Node | `@zlink-systems/framework-codec-protobuf` | `framework/languages/node/packages/framework-codec-protobuf/` | `@zlink-systems/framework-codec-msgpack` | `framework/languages/node/packages/framework-codec-msgpack/` |
| C++ | `zlink::framework_codec_protobuf` | `framework/languages/cpp/extensions/framework-codec-protobuf/` | `zlink::framework_codec_messagepack` | `framework/languages/cpp/extensions/framework-codec-messagepack/` |
| Python, Go, Rust | 작성하지 않는다. | 작성하지 않는다. | 작성하지 않는다. | 작성하지 않는다. |

각 package는 개별 배포가 가능해야 한다. Protobuf를 쓰는 application은 Protobuf extension만
참조하고, MessagePack을 쓰지 않는다면 MessagePack dependency를 가져오지 않는다. 반대도 같다.
두 package를 모두 참조할 수는 있지만 서로 의존하지 않는다.

각 extension package가 제공해야 하는 public surface:

| 대상 | 제공 표면 |
|------|-----------|
| framework | `Codecs.Use(ProtobufExtension)` 또는 언어별 동등 호출에서 사용할 extension 객체 |
| stream connector | 같은 extension 객체가 connector builder에 등록할 connector adapter |
| HTTP client | 같은 extension 객체가 HTTP client builder에 등록할 HTTP adapter |
| tests | framework, connector, HTTP client에서 같은 DTO가 같은 bytes/content-type 규칙으로 왕복되는 contract test |

package topology:

- 기본은 codec별 단일 package다. 예를 들어 Protobuf extension package 하나가 framework serializer,
  connector adapter, HTTP adapter를 함께 제공한다.
- 이 package는 해당 언어의 framework, connector, HTTP client package를 compile-time dependency로
  가져도 된다. codec extension의 목적은 세 대상에 같은 codec을 적용하는 것이므로, adapter type을
  숨기기 위해 runtime reflection이나 dynamic import 우회를 쓰지 않는다.
- 특정 언어의 package manager에서 dependency 분리가 꼭 필요하면 하위 adapter entrypoint를 둘 수
  있다. 예: `framework-codec-protobuf/connector`, `framework-codec-protobuf/http-client`.
  그래도 사용자에게 보이는 codec factory 이름과 codec id/content type은 동일해야 한다.
- Protobuf package와 MessagePack package는 서로 의존하지 않는다.

예상 사용 예:

```csharp
options.Codecs.Use(ZLinkProtobufCodec.Default);
connectorOptions.Codecs.Use(ZLinkProtobufCodec.Default);
httpClientBuilder.Codecs(codecs => codecs.Use(ZLinkProtobufCodec.Default));
```

```java
codecs.use(ZLinkProtobufCodec.defaultCodec());
```

```ts
codecs.use(zlinkProtobufCodec());
```

```cpp
codecs.use(zlink::framework::codecs::protobuf());
```

구현 규칙:

- extension package 내부에 serializer와 adapter를 둔다. framework core, connector core, HTTP client
  core에는 Protobuf/MessagePack library import가 남으면 안 된다.
- JSON fallback은 framework core의 기본 serializer를 사용한다. Protobuf extension이 처리할 수 없는
  DTO를 application code에서 직접 JSON으로 변환하게 만들지 않는다.
- Protobuf extension은 generated Protobuf message type을 처리한다. generic object를 임의 Protobuf
  schema로 변환하는 기능은 기본 목표가 아니다.
- MessagePack extension은 해당 언어에서 명시적으로 MessagePack 직렬화가 가능한 type만 처리한다.
  처리할 수 없는 type은 JSON 기본 serializer로 넘긴다.
- adapter 등록은 public API로 구현한다. reflection, dynamic private member 접근, internal API
  접근으로 connector/httpclient에 끼우지 않는다.

완료 기준:

- framework core는 Protobuf/MessagePack dependency를 갖지 않는다.
- extension package를 참조하고 등록하면 기존 Protobuf/MessagePack sample이 같은 업무 API로 동작한다.
- extension package를 참조하지 않으면 JSON만 사용된다.
- Protobuf extension package만 설치한 application은 MessagePack dependency를 가져오지 않는다.
- MessagePack extension package만 설치한 application은 Protobuf dependency를 가져오지 않는다.
- 같은 extension 객체 또는 같은 extension factory가 framework, connector, HTTP client에 모두
  등록된다.
- extension package 작성 위치가 위 표와 일치하고, build graph에서 기존 connector/bindings codec
  package를 참조하지 않는다.

문서 반영:

- 공통 `framework/doc/framework/common/framework-api.ko.md`에서 “기본 제공 codec” 표현을 고친다.
  JSON은 기본 제공, Protobuf/MessagePack은 선택 extension으로 설명한다.
- 언어별 install/getting started 문서에 Protobuf/MessagePack extension package를 개별 설치하는
  예시를 넣는다.
- 언어별 guide의 getting started, channel, spot, actor/session, stream, registry 문서에서
  `AddProtobuf`, `addProtobuf`, `addMessagePack` 같은 예시를 extension package 등록 예시로 바꾼다.
- feature map의 codec 열은 “sample이 선택한 extension”을 보여 주는 보조 정보로만 둔다.
- case study 문서는 codec 자체를 아키텍처 핵심 기능처럼 설명하지 않는다.

### 4단계: Protobuf/MessagePack 샘플 extension 참조 전환

Protobuf나 MessagePack을 사용하는 샘플은 extension package 작성 직후 새 package를 참조하도록
바꾼다. 이 단계는 connector/bindings codec package를 삭제하기 전에 끝나야 한다. 그래야 샘플이
삭제 예정 package에 기대지 않는 상태에서 다음 단계를 진행할 수 있다.

수정 대상:

| 대상 | 전환 내용 |
|------|-----------|
| Protobuf 샘플 | 언어별 Protobuf extension package를 dependency에 추가하고, framework/connector/HTTP client 구성에서 Protobuf extension을 등록한다. |
| MessagePack 샘플 | 언어별 MessagePack extension package를 dependency에 추가하고, framework/connector/HTTP client 구성에서 MessagePack extension을 등록한다. |
| 샘플 build metadata | framework core, connector codec package, bindings codec package가 아니라 `framework/languages/<lang>/...framework-codec-*` package를 참조한다. |
| 샘플 code | DTO, handler, client request/reply API는 유지하고, 변경은 import와 `use(extension)` 등록으로 제한한다. |
| 샘플 testkit/release gate | `addProtobuf()`, `addMessagePack()`, `ZLinkStreamProtobuf.codec()`, `ZLinkStreamMessagePack.decode(...)` 같은 이전 helper 검사를 extension 등록 검사로 바꾼다. |

언어별 샘플 전환 위치:

| 언어 | Protobuf sample 전환 | MessagePack sample 전환 |
|------|----------------------|-------------------------|
| .NET | code: `framework/languages/dotnet/samples/**`, docs: `framework/doc/framework/dotnet/guide/samples/*.ko.md` | 같은 위치에서 MessagePack 사용 샘플이 있으면 전환 |
| Java/Kotlin | code: `framework/languages/java/samples/**`, `framework/languages/java/zlink-framework-testkit/**`, docs: `framework/doc/framework/java/guide/samples/*.ko.md` | 같은 위치에서 MessagePack 사용 샘플과 testkit gate 전환 |
| Node | code: `framework/languages/node/samples/**`, docs: `framework/doc/framework/node/guide/samples/*.ko.md` | 같은 위치에서 MessagePack 사용 샘플이 있으면 전환 |
| C++ | code: `framework/languages/cpp/samples/**`, `framework/languages/cpp/tests/**`, docs: `framework/doc/framework/cpp/guide/14-samples-map.ko.md` | 같은 위치에서 MessagePack 사용 샘플이 있으면 전환 |
| Python, Go, Rust | framework extension 대상이 아니므로 전환하지 않는다. bindings sample은 raw `Message`/bytes 기준으로 정리한다. | framework extension 대상이 아니므로 전환하지 않는다. bindings sample은 raw `Message`/bytes 기준으로 정리한다. |

완료 기준:

- Protobuf/MessagePack 샘플은 extension package dependency를 명시한다.
- Protobuf/MessagePack 샘플의 server, client, connector, HTTP client 구성은 같은 codec extension을
  등록한다.
- sample DTO, handler method, client request/reply method는 codec 전환 때문에 바뀌지 않는다.
- sample이나 testkit이 connector 전용 codec helper 또는 bindings codec package를 직접 참조하지 않는다.
- Protobuf sample은 Protobuf extension package만 추가하고 MessagePack dependency를 가져오지 않는다.
- MessagePack sample은 MessagePack extension package만 추가하고 Protobuf dependency를 가져오지 않는다.

문서 반영:

- 공통 sample README와 언어별 sample guide는 Protobuf/MessagePack을 “샘플의 업무 API 차이”가
  아니라 “extension package dependency와 구성 등록 차이”로 설명한다.
- sample 실행 문서에는 extension package 설치/참조 위치를 명시한다.
- feature map은 sample이 선택한 codec extension만 표시하고, connector/bindings codec package를
  설치 대상으로 안내하지 않는다.

### 5단계: connector codec package 제거

현재 connector 전용 JSON/MessagePack/Protobuf package가 따로 있다. To-be에서는 이 package를
제거한다. connector typed API는 유지하고, payload codec은 framework codec extension에서
제공하는 adapter를 사용한다.

완료 기준:

- connector package는 wire protocol과 typed send/request/on API만 제공한다.
- codec-specific connector helper package가 build graph에서 사라진다.
- sample의 DTO, handler, request/reply API는 바뀌지 않는다.
- sample은 이미 4단계에서 framework codec extension 참조로 전환되어 있어야 한다.

문서 반영:

- 언어별 stream connector spec/guide에서 connector 전용 codec package 이름을 제거한다.
- `stream-connector-json`, `stream-connector-msgpack`, `stream-connector-protobuf`,
  `Systems.Zlink.Stream.Connector.Json`, `Systems.Zlink.Stream.Connector.MessagePack`,
  `Systems.Zlink.Stream.Connector.Protobuf`, `ZLinkStreamJson`, `ZLinkStreamMessagePack`,
  `ZLinkStreamProtobuf`, `zlinkStreamJsonCodec`, `zlinkStreamMessagePackCodec`,
  `createZlinkStreamProtobufCodec`, `codec_traits<T>` 같은 connector codec helper 중심 설명을
  framework codec extension 설명으로 바꾼다.
- C++ connector guide는 raw wire codec enum과 typed payload serializer를 분리해서 설명한다.
  `codec_traits<T>`는 connector 전용 확장 표면이 아니라 framework extension adapter로 옮긴다.
- package/installation guide에서 connector codec feature 또는 package를 제거한다.

### 6단계: bindings codec extension 제거

현재 bindings 아래에도 JSON/MessagePack/Protobuf codec package가 언어별로 따로 있다. To-be에서는
bindings가 codec extension을 소유하지 않는다. bindings는 core API, raw `Message`, byte payload,
protocol enum만 제공하고, codec 구현은 framework codec extension package로 이동한다.

제거 대상:

| 언어 | 제거 대상 |
|------|----------|
| .NET | `bindings/dotnet/codecs/Zlink.Codecs.Json`, `Zlink.Codecs.MessagePack`, `Zlink.Codecs.Protobuf`, `Zlink.Codecs.Tests`, solution/test 참조 |
| Java | `bindings/java/codec/zlink-codec-json`, `zlink-codec-messagepack`, `zlink-codec-protobuf`, `settings.gradle`, `tests/run_tests.sh` codec target |
| Node | `bindings/node/packages/zlink-codec-json`, `zlink-codec-messagepack`, `zlink-codec-protobuf`, `build:codecs`, `test:codecs` scripts |
| Python | `bindings/python/codecs/zlink_codec_json`, `zlink_codec_messagepack`, `zlink_codec_protobuf`, codec test runner entries |
| Rust | `bindings/rust/crates/zlink-codec-json`, `zlink-codec-messagepack`, `zlink-codec-protobuf`, workspace/test runner entries |
| C++ | `bindings/cpp/codecs/zlink-codec-json`, `zlink-codec-messagepack`, `zlink-codec-protobuf`, `zlink::cpp_codec_*` targets and package exports |
| Go | `bindings/go/codec` 아래 codec package 또는 codec package generator 산출물이 있으면 제거한다. |

대체 위치:

- JSON serializer는 framework core 기본 serializer로 유지한다. bindings package로 배포하지 않는다.
- Protobuf와 MessagePack serializer는 framework codec extension package로 이동한다.
- bindings sample이나 low-level tests가 codec helper를 쓰고 있으면 raw `Message`/bytes 기준으로 바꾼다.
- framework sample이 bindings codec package를 참조하고 있으면 4단계에서 framework codec extension
  package 참조로 먼저 바꾼다.
- Python, Go, Rust는 bindings codec package 제거 뒤 대체 codec package를 제공하지 않는다. 이
  언어에서 남는 public 표면은 raw `Message`/bytes API다.
- Python, Go, Rust에 framework 구현이 나중에 승격되면 그때 `framework/languages/<lang>/` 아래에
  framework codec extension package를 새로 만든다. 이 경우에도 제거한 bindings codec package를
  되살리지 않는다.

완료 기준:

- `bindings/**/codec*`, `bindings/**/zlink-codec-*`, `Systems.Zlink.Codecs.*`,
  `zlink::cpp_codec_*`가 build graph와 package metadata에서 사라진다.
- bindings test runner가 codec package test를 더 이상 실행하지 않는다.
- bindings public docs가 codec helper package를 설치 대상으로 안내하지 않는다.
- framework 쪽 Protobuf/MessagePack/custom codec 테스트는 framework extension package에서 통과한다.
- raw `Message`/bytes API는 유지한다. codec package 제거를 이유로 core binding API를 줄이지 않는다.
- Python, Go, Rust는 codec package 제거 뒤 대체 package가 없다는 점을 build metadata, sample,
  guide에 일관되게 반영한다.

문서 반영:

- `doc/spec/bindings/**`와 언어별 binding guide에서 codec extension package 설명을 제거한다.
- framework guide/spec에서는 codec package의 새 위치가 framework extension임을 설명한다.
- 삭제된 bindings codec package 이름이 문서, sample, build script, test runner에 남지 않게 `rg`
  검증을 수행한다.

### 7단계: 문서와 샘플 정리

현재 guide/spec/sample 문서에는 이전 codec package와 helper를 직접 안내하는 내용이 남아 있다.
To-be 문서는 codec을 API 사용법과 섞어 설명하지 않는다. codec은 구성 단계의 선택 사항으로
설명한다.

문서 위치 기준:

- 언어별 문서의 기준 위치는 모두 `framework/doc/`이다. 이후 언어별 guide, spec, internals,
  HTTP client, stream connector 문서를 수정할 때는 이 디렉토리 아래의 컴포넌트별 위치에서
  진행한다.
- 언어별 framework guide, spec, internals 문서는 `framework/doc/framework/<lang>/` 아래에서
  관리한다.
- HTTP client 문서는 `framework/doc/http-client/<lang>/` 아래에서 관리한다.
- 샘플 구현 코드는 기존처럼 `framework/languages/<lang>/samples/**`에 두지만, 샘플 설명 문서는
  `framework/doc/framework/<lang>/guide/samples/`에서 수정한다.
- `framework/languages/<lang>/doc/` 아래에 새 언어별 문서를 추가하지 않고, 기존 언어별 문서도
  수정 대상이면 `framework/doc/` 아래의 대응 위치로 옮기거나 그 위치에서 갱신한다.

완료 기준:

- sample 문서는 JSON 기본값을 명시한다.
- Protobuf/MessagePack sample은 framework codec extension 적용 예시로 설명한다.
- connector 전용 codec package 문서는 제거하거나 framework extension 문서로 이동한다.

수정 대상:

- 공통 문서
  - `framework/doc/framework/common/framework-api.ko.md`
  - `framework/doc/framework/common/message-model.ko.md`
  - `framework/doc/framework/common/sample/README.ko.md`
  - `framework/doc/framework/common/session-actor-dispatch.ko.md`
- .NET guide/spec/http-client 문서
  - `framework/doc/framework/dotnet/guide/01-overview.ko.md`
  - `framework/doc/framework/dotnet/guide/02-getting-started.ko.md`
  - `framework/doc/framework/dotnet/guide/04-channel-messaging.ko.md`
  - `framework/doc/framework/dotnet/guide/05-spot.ko.md`
  - `framework/doc/framework/dotnet/guide/06-actor-session.ko.md`
  - `framework/doc/framework/dotnet/guide/07-stream.ko.md`
  - `framework/doc/framework/dotnet/guide/08-registry.ko.md`
  - `framework/doc/framework/dotnet/guide/10-feature-map.ko.md`
  - `framework/doc/framework/dotnet/guide/samples/*.ko.md`
  - `framework/doc/http-client/dotnet/*.ko.md`
  - `framework/doc/http-client/dotnet/spec/*.ko.md`
  - `framework/doc/framework/dotnet/spec/*.ko.md`
- Java/Kotlin guide/spec/http-client 문서
  - `framework/doc/framework/java/guide/01-overview.ko.md`
  - `framework/doc/framework/java/guide/02-getting-started.ko.md`
  - `framework/doc/framework/java/guide/04-channel-messaging.ko.md`
  - `framework/doc/framework/java/guide/05-spot.ko.md`
  - `framework/doc/framework/java/guide/06-actor-session.ko.md`
  - `framework/doc/framework/java/guide/07-stream.ko.md`
  - `framework/doc/framework/java/guide/08-registry.ko.md`
  - `framework/doc/framework/java/guide/10-feature-map.ko.md`
  - `framework/doc/framework/java/guide/samples/*.ko.md`
  - `framework/doc/http-client/java/*.ko.md`
  - `framework/doc/http-client/java/spec/*.ko.md`
  - `framework/doc/framework/java/spec/*.ko.md`
- Node guide/spec/http-client 문서
  - `framework/doc/framework/node/guide/01-overview.ko.md`
  - `framework/doc/framework/node/guide/02-getting-started.ko.md`
  - `framework/doc/framework/node/guide/04-channel-messaging.ko.md`
  - `framework/doc/framework/node/guide/05-spot.ko.md`
  - `framework/doc/framework/node/guide/06-actor-session.ko.md`
  - `framework/doc/framework/node/guide/07-stream.ko.md`
  - `framework/doc/framework/node/guide/08-registry.ko.md`
  - `framework/doc/framework/node/guide/10-feature-map.ko.md`
  - `framework/doc/framework/node/guide/samples/*.ko.md`
  - `framework/doc/http-client/node/*.ko.md`
  - `framework/doc/http-client/node/spec/*.ko.md`
  - `framework/doc/framework/node/spec/*.ko.md`
- C++ guide/spec/connector 문서
  - `framework/doc/framework/cpp/guide/05-configuration.ko.md`
  - `framework/doc/framework/cpp/guide/07-channel-messaging.ko.md`
  - `framework/doc/framework/cpp/guide/10-stream.ko.md`
  - `framework/doc/framework/cpp/guide/11-registry.ko.md`
  - `framework/doc/framework/cpp/guide/14-samples-map.ko.md`
  - `framework/doc/framework/cpp/guide/15-feature-map.ko.md`
  - `framework/doc/framework/cpp/spec/*.ko.md`
  - `framework/doc/framework/cpp/guide/*connector*.ko.md`
  - `framework/doc/http-client/cpp/*.ko.md`

문서 완료 기준:

- `rg`로 connector 전용 codec package 이름이 guide/spec/http-client 문서에서 더 이상 나오지 않는다.
- `rg`로 `ToJson`, `ToProto`, `FromJson`, `ZLinkStreamProtobuf.decode`,
  `ZLinkStreamMessagePack.decode`, `zlinkStreamJsonCodec`, `codec_traits<T>` 같은 helper 중심 예시가
  sample-specific 예외 없이 사라진다.
- 문서 예시는 codec을 바꾸기 위해 handler/client API를 바꾸지 않는다.
- 각 언어 `framework/doc/framework/<lang>/README.ko.md`와 공통 `framework/doc/README.ko.md`의 링크 목록이 실제 파일과 맞다.

### 8단계: Codex 누락 리뷰 게이트

구현, 샘플, 문서 수정이 끝난 뒤에는 별도 Codex 에이전트 리뷰를 완료해야 한다. 이 리뷰는
코드를 수정하는 단계가 아니라 완료 판정 단계다. 리뷰에서 누락이나 잘못된 부분이 하나라도
나오면 완료로 보지 않고, 수정한 뒤 같은 리뷰를 다시 실행한다.

리뷰 범위:

| 영역 | 확인할 내용 |
|------|-------------|
| framework core | JSON만 기본 의존성으로 남고, Protobuf/MessagePack 구현 의존성이 core에서 제거되었는지 확인한다. |
| extension package | 언어별 Protobuf/MessagePack extension package가 계획한 위치에 있고, 개별 배포와 개별 dependency 조건을 지키는지 확인한다. |
| custom codec | 사용자 정의 codec이 built-in extension과 같은 계약으로 framework, connector, HTTP client에 붙을 수 있는지 확인한다. |
| connector | connector 전용 codec package와 helper 표면이 남아 있지 않고, typed API가 유지되는지 확인한다. |
| bindings | bindings codec package가 제거되고 raw `Message`/bytes API만 남았는지 확인한다. Python, Go, Rust에 대체 codec package가 생기지 않았는지 확인한다. |
| sample | Protobuf/MessagePack 샘플이 extension package를 참조하고, DTO/handler/client API를 바꾸지 않았는지 확인한다. |
| 문서 | guide/spec/sample/http-client 문서가 JSON 기본값, 선택 extension, connector/HTTP 공유 구조, bindings 제거 정책을 같은 뜻으로 설명하는지 확인한다. |
| test gate | testkit, release gate, build script가 삭제된 connector/bindings codec package나 이전 helper 이름을 기준으로 검사하지 않는지 확인한다. |
| POSD 리팩토링 | 각 단계의 코드 수정 뒤 POSD 점검과 필요한 리팩토링이 끝났는지 확인한다. |

리뷰 실행 규칙:

- 리뷰는 live checkout 기준으로 수행한다. 계획 문서만 읽고 통과시키지 않는다.
- 리뷰 결과는 파일과 line을 포함한 findings로 남긴다.
- “누락 없음, 잘못된 내용 없음”이라는 결론이 나와야 이 계획을 완료할 수 있다.
- findings가 있으면 해당 항목을 수정하고, 같은 범위로 Codex 리뷰를 다시 수행한다.
- 세부 구현이 아직 없는 언어는 “미구현”으로 통과시키지 않는다. 이 계획에서 제외한 언어인지,
  아니면 구현 누락인지 구분해서 판정한다.

## 검증 기준

- codec 변경 전후로 handler method와 client method 시그니처가 바뀌지 않았는지 검사한다.
- JSON 기본 sample이 추가 설정 없이 동작한다.
- Protobuf/MessagePack sample은 extension package 등록만으로 동작한다.
- custom codec 테스트가 framework, connector, HTTP client 경로를 각각 통과한다.
- 각 코드 수정 단계 뒤 POSD 기반 리팩토링 점검과 필요한 리팩토링을 완료해야 한다.
- Codex 누락 리뷰 게이트에서 “누락 없음, 잘못된 내용 없음” 결론을 받아야 한다.
- 문서 검증은 구현 검증과 같은 stage에서 실행한다. 코드가 green이어도 문서의 package 이름,
  helper 이름, codec 설명이 이전 구조를 가리키면 완료로 보지 않는다.
