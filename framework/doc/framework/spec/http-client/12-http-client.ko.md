# HTTP Client — 공통 스펙

[스펙 목차](../README.ko.md) | [이전: Channel 메시징](../server/11-channel-messaging.ko.md) | [다음: SPOT 메시징](../server/20-spot-messaging.ko.md)

> 이 문서는 **framework 동반 HTTP client의 언어 중립 정본**이다. 정체성, fluent builder 형식,
> **실행 terminator**, Spot 실행 문맥과의 결합(turn seam), codec, 오류 모델을 소유한다.
>
> 상세 계약(redirect·retry·cookie, 인증·TLS·proxy, 압축, 회귀 테스트)은
> [`doc/http-client/spec/`](../README.ko.md)이 이어서 소유한다. 이 문서는
> 그중 **framework와 맞물리는 축**을 고정한다.
>
> 언어별 정확한 타입과 시그니처는 `languages/<lang>/04-http-client.ko.md`가 고정한다(**작성 예정**).
> 그때까지는 [`doc/http-client/spec/language-interfaces.ko.md`](language-interfaces.ko.md)가
> 그 자리를 대신한다.

## 1. 정체성 — framework 동반 client

**HTTP client는 STREAM connector와 같은 자리에 있다.** 별도 패키지로 배포하지만 **framework
전용 동반 client**이며, 계약은 framework가 소유한다.

| | STREAM connector | HTTP client |
|---|---|---|
| 패키지 | 별도 | 별도 |
| 계약 소유 | framework 공통 스펙([32](../stream-connector/32-stream-connector.ko.md)) | framework 공통 스펙(이 문서) |
| 언어별 인터페이스 | `languages/*/03-stream-connector.ko.md` | `languages/*/04-http-client.ko.md` |

**존재 이유는 하나다** — framework application이 **외부 API와 레거시 API를 zlink 스타일로**
호출할 수 있어야 한다. 범용 HTTP 라이브러리를 대체하려는 것이 아니다.

**바이너리 의존은 한 방향이다: framework → HTTP client.** HTTP client 패키지는 framework를
참조하지 않는다. 그래야 CLI와 client 시나리오가 framework 런타임을 끌고 오지 않는다. Spot 실행
문맥과의 결합은 §3의 **주입점 하나**로만 이뤄진다.

## 2. Fluent builder

**framework messaging과 같은 형식이다** — "operation 선택 → 설정 → 실행 방식 terminator".

```
client.post("/games")               // operation
      .header("x-request-id", ...)  // 설정
      .query("region", "kr")
      .body(createGameReq)
      .timeout(3s)
      .async<CreateGameRes>()       // terminator
```

- verb 7종: `get` / `post` / `put` / `delete` / `patch` / `head` / `options`.
- 설정 축: `header`, `query`, `timeout`, body 소스(typed / raw / streaming / form / multipart).
- **body 소스는 상호 배타다.** 섞으면 `RequestProtocolError`.

builder의 세부 계약(path 형식, percent-encoding, body 소스별 retry 가능 여부 등)은
[`doc/http-client/spec/03-request-builder.ko.md`](03-request-builder.ko.md)가
소유한다.

## 3. 실행 terminator — framework와 같은 세 축 (+ callback)

**Spot 실행 문맥에서 완료를 기다리는 모든 framework 호출은 같은 terminator를 갖는다**
([04 §1.1](../04-async-execution-policy.ko.md)). HTTP client도 예외가 아니다.

| terminator | 완료를 기다리나 | Spot 실행 줄 |
|---|---|---|
| **submit** | 아니오 | 그대로 진행. 응답을 쓰지 않는 fire 경로(telemetry beacon 등) |
| **async** | 예 | **turn을 유지한다.** handler는 하나의 turn이다 |
| **yield** | 예 | **turn을 반납한다.** 완료된 continuation은 Spot 실행 줄의 큐에 재삽입되어 순서대로 재개된다 |

**callback은 네 번째 terminator가 아니다.** framework의 terminator 축은 셋뿐이며
([04 §1.1](../04-async-execution-policy.ko.md)), callback은 **awaitable을 쓰지 않는 호출자**(CLI,
이벤트 루프 기반 client)를 위한 **별도 완료 경로**다. HTTP client는 그 경로도 함께 제공한다.

Spot 실행 문맥에서 callback을 쓰면 **`submit`과 같다** — 호출은 turn을 잡지 않고 그대로 진행하고,
완료 callback은 그 Spot 실행 줄의 **새 turn**으로 큐에 들어간다. 완료 값으로 그 turn의 판단을
이어가야 하면 callback이 아니라 `async` 또는 `yield`를 쓴다.

### 3.1 yield가 이 client의 존재 이유다

**actor 입·퇴장 시 외부 API나 레거시 API에서 데이터를 가져오는 경우**가 대표적이다. 그 대기는
spot의 공유 흐름과 무관하므로, 그것 때문에 room 전체와 timer가 멈추면 안 된다.

```
// spot handler 안
var profile = await http.Get($"/players/{id}").Yield<Profile>(ct);
```

`RunIoWorker`로 감싸도 같은 효과를 얻지만([04 §1.2](../04-async-execution-policy.ko.md)), **가장 흔한
경로에는 1급 단축을 준다.** 매 호출 지점에 worker boilerplate를 요구하면 사용자는 결국 그냥
`async`로 기다리게 되고 room이 멈춘다.

두 축의 역할은 다르다.

| 축 | 용도 |
|---|---|
| `RunIoWorker(...).Yield()` | **범용 escape hatch** — DB 드라이버, gRPC, 외부 SDK, 무엇이든 |
| HTTP client의 `.Yield()` | **1급 단축** — 외부 HTTP·레거시 API |

### 3.2 turn seam — 주입점 하나

HTTP client는 framework를 모른다. Spot의 turn을 아는 것은 **주입된 execution scheduler** 하나다.

- HTTP client는 **execution scheduler 주입점**을 공개 계약으로 둔다. scheduler는 completion을
  어디서 재개할지 정한다.
- **framework가 DI 등록 시 spot turn을 아는 scheduler를 꽂는다.** 그때 `yield`가 살아난다.
- scheduler가 없는 단독 사용(CLI·client 시나리오)에서는 **`yield`를 노출하지 않는다.** `async`와
  callback만 쓴다.

C++ HTTP client에 **같은 형태의 API 표면이 이미 있다**(`coroutines(resume_scheduler)` /
`framework_resume_scheduler_t`). 다만 framework 런타임이 아직 그것을 주입하지 않으므로 표면만
있고 통합은 검증되지 않았다.

### 3.3 blocking terminator를 두지 않는다

**완료 값을 동기로 언래핑하는 public terminator를 만들지 않는다**([04 §2](../04-async-execution-policy.ko.md)).
같은 의미의 blocking 대안 terminator는 계약 위반이다. 테스트나 CLI에서 동기로 기다려야 하면
호출자가 언어 관용(`GetAwaiter().GetResult()`, `runBlocking`, `.join()`)으로 직접 감싼다.

## 4. 서버 표면과 등록

**서버(Spot handler·channel handler)에서 쓰는 HTTP client는 DI로 주입받는다.** 정적 팩토리로
handler 안에서 client를 만들지 않는다 — 연결 pool과 turn seam을 잃는다.

- **application이 명명 등록한다.** baseUrl, 인증, timeout, retry 정책은 서비스마다 다르므로
  framework가 기본 client 하나를 자동 등록하지 않는다.
- 등록 표면의 형태는 channel 등록과 같다: 구성 단계에서 이름과 정책을 함께 등록하고, handler는
  그 이름으로 주입받는다.
- **정적 팩토리 진입점은 client-side 전용으로 남긴다.** CLI와 client 시나리오가 쓴다. turn이
  없으므로 `yield`도 없다.

| 표면 | 누가 쓰나 | terminator |
|------|-----------|------------|
| 정적 팩토리 | CLI · client 시나리오 | `async` / callback |
| **DI 주입 client** | **Spot handler · 서버 코드** | `submit` / `async` / `yield` / callback |

## 5. Codec

**HTTP client는 framework와 codec extension을 공유하지만 registry 인스턴스는 따로 가진다**
([11 §6](../server/11-channel-messaging.ko.md)). 같은 codec extension 객체를 양쪽에 각각 등록할 수 있으나,
**등록은 host마다 따로 해야 한다.**

typed body의 encode/decode는 그 registry가 담당한다. raw body API는 registry를 거치지 않는다.

## 6. 오류 모델

**HTTP client는 자체 예외 계층을 만들지 않는다.** framework 공용 오류 모델
([05 §2.3](../05-framework-api.ko.md))의 error kind와 retriable 판정을 그대로 쓴다. **HTTP client
전용 error kind를 새로 만들지 않는다.**

| 상황 | kind |
|------|------|
| 구성·사용 오류(builder 검증 실패, path 형식, body 소스 중복) | `RequestProtocolError` |
| 전송 실패, typed 제출의 status ≥ 400, redirect 한도 초과, body 크기 초과 | `RequestFailed` |
| typed body decode 실패, 압축 해제 실패 | `PayloadDecodeFailed` |
| 시도당 timeout 초과 | 언어별 timeout 경계 표현(§05의 오류 kind가 아니라 boundary 상태다) |

세부 매핑은
[`doc/http-client/spec/09-error-model.ko.md`](09-error-model.ko.md)가
소유한다.

## 7. 회귀 테스트

| 항목 | 검증 |
|---|---|
| terminator 축 | `submit` / `async` / `yield` / callback이 모두 있고, blocking 언래핑 terminator가 **없다** |
| turn 유지 | Spot handler가 `async`로 기다리는 동안 같은 Spot의 다른 callback이 시작하지 않는다 |
| turn 반납 | `yield`로 기다리는 동안 같은 Spot의 다른 callback이 실행되고, continuation이 큐에 재삽입되어 순서대로 재개된다 |
| seam 부재 | scheduler가 없는 단독 사용에서 `yield`가 노출되지 않는다 |
| 등록 | 서버 표면이 DI 주입으로만 얻어지고, handler 안에서 정적 팩토리로 client를 만들지 않는다 |
| 오류 kind | HTTP client 전용 kind가 없고 framework 공용 kind만 쓴다 |
| builder | body 소스를 섞으면 `RequestProtocolError`로 실패한다 |
