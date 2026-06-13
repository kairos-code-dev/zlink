# Node/TypeScript Framework 계층 보안·버그 검토 리포트

- **작성일**: 2026-06-14
- **대상 범위**: `framework/languages/node/packages/{framework,nestjs,stream-connector,stream-connector-json,-msgpack,-protobuf}/src`
- **상태**: 리포트 전용. **코드 수정 없음.**
- **참고**: 교차언어 공통 결함은 [README.ko.md](README.ko.md) 참조.

> **네이티브 바인딩 범위 주의**: 프레임워크는 `@zlink-systems/zlink`(`bindings/node`)의 **N-API/node-addon-api**
> 네이티브 애드온에 의존한다(ffi-napi 아님). 프레임워크 계층 TS는 **Buffer를 네이티브 포인터로 직접 복사하지 않는다**
> (`node-backend-adapter.ts`는 `version()`만 호출). Buffer↔네이티브 수명/UAF 위험은 `bindings/node/native/*.cc`에
> 있으며 **본 리뷰 범위 밖**이다. 본 계층의 실제 공격면은 **순수 TS 와이어 디코더**(stream-connector)와 JSON/msgpack/protobuf 코덱이다.

## 요약

| # | 심각도 | 분류 | 위치 | 상태 |
|---|--------|------|------|------|
| C1 | High | DoS / 무제한 버퍼링 | `stream-connector/.../NodeDuplexStreamConnection.ts:65-77` | 확인 |
| C2 | High | DoS / 무제한 버퍼링 | `stream-connector/.../WebSocketFrameCodec.ts:29-46`, `NodeWebSocketConnection.ts:111-118` | 확인 |
| C3 | Medium | 신뢰 불가 역직렬화 / 입력 검증 부재 | `stream-connector-json/src/index.ts:51` | **NEEDS-NUANCE (하향)** |
| C4 | Medium | 신뢰 불가 역직렬화 / 입력 검증 부재 | `framework/.../channels/channel-envelope.ts:117,153`, `nestjs/src/index.ts:1409,1412` | **NEEDS-NUANCE (하향)** |
| S1 | Medium | DoS / LZ4 압축 폭탄 | `stream-connector/.../Compression/ZlinkStreamCompressionCodec.ts:64`, `framework/.../streams/protocol.ts:221` | **확인(§Codex)** |
| S2 | Low | DoS / 핸드셰이크 헤더 무제한 | `stream-connector/.../WebSocketHandshake.ts:68` | **확인(§Codex)** |
| S3 | Low | 리소스 누수(대체로 클린) | `ZlinkStreamConnector` 타이머 | 의심 |

> ⚠️ **아래 §Codex 교차검증이 최종 판정이다.** C3/C4는 "프로토타입 오염"에서 "신뢰 불가 JSON 미검증"으로 하향.

---

## Codex 교차검증 결과 (2026-06-14)

작성 후 Codex에 문서 + 코드 위치를 주고 적대적 대조 리뷰를 요청한 결과:

| # | 초판 | Codex 판정 | 정정 요지 |
|---|------|-----------|-----------|
| C1 | High | **CONFIRMED** | (문구 nuance) decode가 `MAX_SAFE_INTEGER`로 cap하는 게 아니라 u32 길이를 수용하고 그만큼 버퍼링 대기. 실질 무제한 동일 |
| C2 | High | **CONFIRMED** | (nuance) `high !== 0` 검사로 u32 크기로 제한되나 실질 수신 cap 없음 동일 |
| C3 | Medium | **NEEDS-NUANCE (과장)** | `JSON.parse`는 악의 reviver 없이는 프로토타입을 오염시키지 **않음**. read 경로에서 파싱 결과를 `Object.prototype`/공유 타깃에 병합하는 sink **미발견** → 실제 위험은 "신뢰 불가 JSON 객체 + 선택적 앱 reviver가 소비자에 반환"이지 **확정된 프로토타입 오염 아님** |
| C4 | Medium | **NEEDS-NUANCE (과장)** | 동일. 헤더는 필드 단위로 소비(`channel-envelope.ts:107-112`), 페이로드는 plain value/Buffer로 핸들러 전달(`channels/index.ts:689`) → 병합 sink 없음 |
| S1 | Medium | **CONFIRMED** | `ZlinkStreamCompressionCodec.ts:64-69` + `framework/.../streams/protocol.ts:221-226` 동일 흐름 확인 |
| S2 | Low | **CONFIRMED** | `WebSocketHandshake.ts:57-72` 바이트 cap 없음 확인 |

**Codex 총평**: 무제한 버퍼링/할당(C1/C2/S1/S2)은 정확(숫자 cap 문구만 사소). **C3/C4는 과장** — 신뢰 불가 JSON을 파싱하는 건 맞으나 read 경로가 프로토타입 오염을 입증하지 못함(추가 unsafe 병합/reviver 동작이 있어야 성립). → **"프로토타입 오염" 단정 대신 "신뢰 불가 입력 구조와 enum 검증 부재"로 재프레이밍.**

---

## CONFIRMED

### C1 — 인바운드 프레임 무제한 크기 (수신측 cap 없음)
- **분류**: DoS / 무제한 버퍼링 · **확인 · repo 고유**
- **위치**: `stream-connector/src/Runtime/Transport/NodeDuplexStreamConnection.ts:65-77`

```ts
const payloadLength = readUInt32BE(prefix, 2);
const frameLength = 6 + headerLength + payloadLength;   // 최대 6 + 65535 + 4_294_967_295
if (this.buffer.size < frameLength) { return undefined; }
return this.buffer.consume(frameLength);
```

`maxSendPayloadSize`(기본 64KiB)는 **encode에서만** 강제(`ZlinkStreamFrameCodec.encode`/`sendFrame`). decode(`tryReadFrame`)는 u32 payloadLength(최대 `0xFFFFFFFF`)를 그대로 수용 — **수신측 cap 부재**(`Number.MAX_SAFE_INTEGER` 검사는 sanity일 뿐 u32는 전부 통과). 피어가 4바이트 prefix `0xFFFFFFFF` 전송 시 `BufferedByteQueue`가 ~4GB까지 버퍼링 후 `new Uint8Array(frameLength)` 단일 할당.
**트리거**: 악의/탈취 서버 또는 평문 `tcp://` MITM이 거대한 payloadLength 전송.
**수정**: `maxReceivePayloadSize` 도입, `tryReadFrame`에서 초과 시 `FrameTooLarge` throw(추가 버퍼링 전).

### C2 — WebSocket 페이로드 + 단편 누적 무제한
- **분류**: DoS / 무제한 버퍼링 · **확인 · repo 고유**
- **위치**: `stream-connector/src/Runtime/Transport/WebSocketFrameCodec.ts:29-46`(64비트 길이 필드, `high !== 0`만 거부 → 실질 u32 수신 cap 없음), `NodeWebSocketConnection.ts:111-118`

```ts
this.currentMessageParts.push(frame.payload);
if (frame.fin) { this.messageQueue.push(concatParts(this.currentMessageParts)); ... }
```

총 메시지 cap·큐 깊이 cap 없음. 피어가 무제한 미-FIN continuation 프레임 스트림(메모리 증가) 또는 `read()` 소비보다 빠른 메시지 폭주(`messageQueue` 증가) 가능.
**수정**: 단일 프레임/조립 메시지 최대 크기 강제, `messageQueue` bound 또는 backpressure.

### C3 — JSON 코덱이 신뢰 불가 입력을 검증 없이 역직렬화 (← 초판 "프로토타입 오염", §Codex로 하향)
- **분류**: 신뢰 불가 역직렬화(입력 검증 부재) · **NEEDS-NUANCE · repo 고유(소비자 의존)**
- **위치**: `stream-connector-json/src/index.ts:51`

> 🟡 **§Codex 정정**: `JSON.parse`는 악의 reviver 없이는 프로토타입을 오염시키지 않고, read 경로에 병합 sink가 없어 **프로토타입 오염은 미입증**. 아래는 "신뢰 불가 JSON이 검증 없이 소비자에 도달"이라는 약화된 우려로 읽을 것.

```ts
return JSON.parse(new TextDecoder().decode(payload.payload), codecOptions.reviver) as T;
```

`JSON.parse`는 악의 reviver 없이는 `Object.prototype`을 오염시키지 않는다. 디코드 객체가 attacker-제어 `__proto__`/`constructor` 키를 운반할 수 있고, downstream에서 **unsafe 병합**(`Object.assign`/spread/deep-merge)을 한다면 오염이 전파될 수 있으나 — **현 read 경로에는 그런 병합 sink가 미발견(§Codex)**. 즉 가설적 위험이다. 노출된 `reviver` 훅(`:19-21`)이 모든 키에 실행되므로 잘못 설정된 앱 reviver가 주입 지점이 될 수는 있다.
**수정**: 디코드 객체는 신뢰 불가임을 문서화, `__proto__`/`constructor`/`prototype` 키 거부 또는 헤더 객체에 `Object.create(null)` 프레이밍.

### C4 — 프레임워크 채널 + nestjs 디코드의 동일 미검증 역직렬화 (← 초판 "프로토타입 오염", §Codex로 하향)
- **분류**: 신뢰 불가 역직렬화(입력 검증 부재) · **NEEDS-NUANCE · repo 고유**
- **위치**: `framework/src/runtime/channels/channel-envelope.ts:117,153`, `nestjs/src/index.ts:1409,1412`

> 🟡 **§Codex 정정**: 헤더는 필드 단위 소비, 페이로드는 plain value/Buffer 전달이라 병합 sink 없음 → **프로토타입 오염 미입증**. 헤더 구조와 enum 검증 부재라는 약화된 우려로 유지.

```ts
return JSON.parse(parts[1].data().toString()) as TReply;        // body
return JSON.parse(parts[0].data().toString()) as ZLinkChannelEnvelopeHeader; // header
```

네이티브 채널 계층(원격 출처)으로 전달된 바이트를 헤더/엔벨로프 객체로 파싱 후 타입 검증 없이 필드 읽기(`header.kind`, `header.correlationId`). 적대적 헤더가 예상 외 타입/`__proto__` 주입 가능.
**수정**: 디코드 헤더 구조와 enum(`kind`, `contentType`)을 검증한 뒤 사용하고, 헤더는 null-prototype 파싱으로 만든다.

---

## SUSPECTED (초판 분류) → §Codex에서 S1·S2 확인(CONFIRMED), S3만 의심 유지

### S1 — LZ4 unpickle result-length 할당 (압축 폭탄)
- **분류**: DoS · **확인(§Codex) · repo 고유**
- **위치**: `stream-connector/.../Compression/ZlinkStreamCompressionCodec.ts:64-69`, 중복 `framework/.../streams/protocol.ts:221-226`

```ts
const resultDiff = sizeOfDiff === 0 ? 0 : readLittleEndian(payload, 1, sizeOfDiff);
const resultLength = data.length + resultDiff;          // resultDiff는 u32 와이어 필드
... return decodeLz4Block(data, resultLength);           // new Uint8Array(resultLength)
```

`resultDiff`가 pickle 헤더에서 최대 4GB. `decodeLz4Block`이 per-run 출력 경계 검사 **전에** `new Uint8Array(resultLength)` 선할당. 작은 압축 페이로드 + 거대 `resultDiff` → multi-GB 할당. fill 루프 자체는 bound(안전). `PayloadCompressed` 플래그 + 압축 코덱 설정 시에만 트리거.
**수정**: 할당 전 `resultLength`를 max-decompressed-size로 clamp.

### S2 — 핸드셰이크 헤더 버퍼 무제한 concat
- `WebSocketHandshake.ts:68-72`: `buffer = Buffer.concat([buffer, chunk])`가 `\r\n\r\n`까지 루프, 시간 기반 cap만 있고 바이트 cap 없음. 클라이언트 개시라 심각도 낮음. **수정**: 누적 헤더 바이트 cap(예: 16KiB).

### S3 — 하트비트 타이머 누수 (대체로 클린)
- `ZlinkStreamConnector`가 `close()`/`stopHeartbeat()`/`stopReceiveLoop()`/`failPending`에서 타이머 정리. 누수 미발견. 단 `runReceiveLoop`가 Immediate 모드에서 `delay(1, signal)`(1ms)로 busy-poll(유휴 시 CPU spin) — 효율 이슈(보안 아님).

---

## 검토 후 "이상 없음" 확인 (코덱/디코드 경로)

- **`ZlinkStreamHeaderCodec.decode`**: 모든 길이 read가 잔여 버퍼 경계 검사(`header.length - offset < n`), `validateEnum`, name 길이 ≤255(단일 바이트), `requestSeq` 0 거부, trailing 바이트 거부. 견고.
- **`ZlinkStreamMetadataCodec.decode`** + `decodeStreamMetadata`: count ≤255(바이트), per-entry key/value 경계 검사, 중복 키 거부, trailing 거부. 무제한 할당 없음(길이 ≤u16, 기존 버퍼 슬라이스).
- **`ZlinkStreamFrameCodec.decode`**: 엄격 `frame.length === expected` 검사. transport 상류 사이징(C1)만 갭, 함수 내부는 클린.
- **LZ4 `decodeLz4Block` fill 루프**: literal/match run 전부 `target.length - targetOffset`·`source.length - sourceOffset` 검사, `matchOffset` 검증(`> targetOffset` 거부). 버퍼 오버런/back-reference underflow 없음. (선할당 크기만 S1.)
- **`ZlinkStreamPacketNameValidator.validateName`**: `startsWith` + 바이트 길이, **regex 없음** → ReDoS 없음.
- **msgpack 코덱**: `@msgpack/msgpack` `decode` 기본 옵션 위임 — 라이브러리가 길이 필드 자체 경계 검사. 래퍼가 unsafe 사이징 추가 없음.
- **protobuf 코덱**: caller 제공 `protobufjs` 타입 `decode` 위임, 순수 pass-through.
- **`eval`/`new Function`/`child_process`/명령 실행 전무.** 유일한 동적 `createRequire(...)(filePath)`(`nestjs/src/index.ts:660`)는 개발자 설정 모듈 디스커버리 디렉터리(startup, `.cjs/.mjs/.js` 필터)에 작동 — 런타임/네트워크 입력 아님. 허용 가능.

---

## 처리 우선순위
1. **C1 + C2**(인바운드 프레임/WS 무제한 크기) — 단일 최고가치 수정: `maxReceivePayloadSize` 도입 후 `NodeDuplexStreamConnection.tryReadFrame`·`WebSocketFrameCodec`·단편 조립기에서 강제.
2. **S1** — LZ4 `resultLength` 할당 전 clamp.
3. **C3/C4** — 신뢰 불가 JSON 헤더/엔벨로프 파싱 하드닝: 구조와 enum 검증이 주된 수정이고, null-prototype/`__proto__` 거부는 심층방어다. **§Codex로 프로토타입 오염은 미입증이므로 우선순위 하**.
