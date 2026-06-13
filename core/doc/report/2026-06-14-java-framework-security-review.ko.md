# Java Framework 계층 보안·버그 검토 리포트

- **작성일**: 2026-06-14
- **대상 범위**: `framework/languages/java/{zlink-framework-core,-spring-boot-starter,zlink-stream-connector,-codecs,-json,-msgpack,-protobuf}/src/main/java`
- **상태**: 2026-06-14 원격 DoS 항목(F1, F2) 수정 완료. F3, F4, F5와 부록 항목은 후속 순서에서 별도 확인.
- **참고**: 교차언어 공통 결함은 [README.ko.md](README.ko.md) 참조.

> **바인딩 범위 주의**: 본 계층에 **네이티브 바인딩 없음**(`native`/`System.loadLibrary`/`MemorySegment`/`Pointer`/JNA grep 0건;
> `ZLinkActorRuntime`의 매치는 주석/예외 문자열뿐). 순수 Java가 `systems.zlink.contracts`/bindings 위에 위치하며,
> FFM/JNI 경계는 bindings 모듈에 있어 **본 리뷰 범위 밖**이다. 남는 메모리-안전 표면은 **와이어 프레이밍**(attacker 제어 길이 → 힙 할당)과 **코덱**이다.
> 와이어 전송은 **repo 고유**(커스텀 길이-prefix 프레이밍), 코덱은 **얇은 래퍼**(Jackson/LZ4).

## 요약

| # | 심각도 | 분류 | 위치 | 상태 |
|---|--------|------|------|------|
| F1 | High | DoS / 무제한 수신 할당 + int 오버플로 | `ZLinkTcpTransportConnection.java:33`, `ZLinkTlsTransportConnection.java:191`, `ZLinkStreamWireProtocol.java:138` | **수정 완료(2026-06-14)** |
| F2 | Medium | DoS / LZ4 압축 폭탄 | `ZLinkStreamLz4Pickler.java:53` | **수정 완료(2026-06-14)** |
| F3 | Medium | TLS 호스트명 검증 미설정(Netty) | `ZLinkTlsTransportConnection.java:54,68` | 확인(버전 확인 권장) |
| F4 | Low | 동시성 / 비-thread-safe 핸들러 리스트 | `DefaultZLinkStreamConnector.java:43-45` | 확인 |
| S1~S3 | — | (부록) | — | 의심 |
| F5 | (footgun) | TLS 인증서 전체 신뢰 옵션 | `DefaultZLinkStreamConnector.java:472-509` | 확인(opt-in) |

> ⚠️ **아래 §Codex 교차검증이 최종 판정이다.** F1~F4 전부 확인, F2 범위 확대, trust-all TLS 옵션 추가.

---

## Codex 교차검증 결과 (2026-06-14)

작성 후 Codex에 문서 + 코드 위치를 주고 적대적 대조 리뷰를 요청한 결과:

| # | 초판 | Codex 판정 | 정정 요지 |
|---|------|-----------|-----------|
| F1 | High | **CONFIRMED** | 송신만 `maxSendPayloadSize`(`DefaultZLinkStreamConnector.java:752`, `WireProtocol:116`) 강제, 수신은 cap 없음. (WS 부분은 OVERSTATED — `ZLinkStreamWireProtocol:134`가 `:138` 할당 **전** 검증; 단 `ZLinkWebSocketTransportConnection:53`이 전체 메시지를 이미 복사) |
| F2 | Medium | **CONFIRMED (범위 확대)** | `ZLinkStreamLz4Pickler.java:53` 확인. **+ framework-core에 중복 버그**: `zlink-framework-core/.../ZLinkStreamLz4Pickler.java:51`, `ZLinkStreamRuntime.java:558` |
| F3 | Medium | **CONFIRMED** | Netty `4.1.100.Final`(`build.gradle.kts:11`)에서 `newHandler(host,port)`는 기본 호스트명 검증 미수행(`SslContext.java:1029-1038`). `setEndpointIdentificationAlgorithm` grep 0건 |
| F4 | Low | **CONFIRMED** | `ArrayList` 핸들러를 IO 스레드가 `List.copyOf`로 read |
| 코덱 | nuance | **CONFIRMED** | msgpack/protobuf 코덱이 실제 `JsonMapper` 사용(JSON emit), 와이어 태그만 다름 |
| clean | clean | **CONFIRMED** | Jackson default typing OFF, `ObjectInputStream` 부재 grep 확인 |

**Codex ADDITIONAL (초판 누락)**:
- **F2 중복**: 같은 LZ4 압축 폭탄 패턴이 connector 모듈뿐 아니라 `zlink-framework-core`에도 존재 → 수정 시 두 곳 모두.
- **F5 — TLS 인증서 전체 신뢰 옵션(footgun)**: `DefaultZLinkStreamConnector.java:472-473`이 활성 시 insecure SSL context 설치, `:488-509`이 **`checkServerTrusted`가 빈** `X509TrustManager` 생성. opt-in(`ZLinkStreamConnectorOptions.java:41` 기본 false)이라 자체 취약점은 아니나, **고위험 footgun이므로 문서에 강하게 경고** 필요.

**Codex 총평**: F1~F4와 코덱 의심 모두 반박 불가. 보정은 (1) WS의 F1 메커니즘 nuance, (2) **F2가 framework-core에도 있어 과소평가**됨.

## 처리 기록 (2026-06-14)

원격 DoS 항목(F1, F2)은 `framework/languages/java` 코드와 대조한 뒤 수정했다.

- `ZLinkStreamConnectorOptions`에 `maxReceivePayloadSize`를 추가하고 기본값을 64KiB로 맞췄다.
- TCP transport는 6바이트 prefix를 읽은 직후 payload 길이와 `headerLength + payloadLength`를 검사해 body buffer 할당 전에 초과 프레임을 거부한다.
- TLS transport의 Netty `FrameDecoder`도 같은 검사 함수를 사용해 `ByteBuf`에서 payload 배열을 만들기 전에 초과 프레임을 거부한다.
- WebSocket transport는 전체 메시지를 한 번 더 복사하기 전에 허용 가능한 최대 frame 길이를 검사하고, decode 단계에서도 payload 길이를 다시 검사한다.
- LZ4 unpickle은 result length를 `long`으로 계산한 뒤 출력 배열을 할당하기 전에 상한을 넘는지 검사한다. stream-connector와 framework-core의 중복 구현을 모두 수정했다.
- Java stream connector spec에 `maxReceivePayloadSize()` 공개 옵션을 추가했다.

검증:

- `./gradlew :zlink-stream-connector:test :zlink-framework-core:test` (`framework/languages/java`, 통과)

Claude 리뷰:

- 2026-06-14: F1, F2의 코드와 테스트를 직접 대조했고 "추가 이슈 없음" 판정을 받았다.

core runtime과 `core/include` public header는 수정하지 않았으므로 `bindings/dev_sync_local_core_libs.sh`는 실행 대상이 아니다.

---

## CONFIRMED

### F1 — 인바운드 무제한 할당(attacker 제어 길이) + 정수 오버플로
- **분류**: DoS / 무제한 수신 할당 · **확인 · repo 고유**

send 경로는 `maxSendPayloadSize` 강제(`DefaultZLinkStreamConnector.encodePayload:752`, `encodeFrame:116`)하나 **수신 경로엔 대칭 상한 없음**. 피어가 6바이트 prefix에 거대한 payload를 선언하면 바이트 도착 전 즉시 할당.

- `ZLinkTcpTransportConnection.java:33`
  ```java
  ByteBuffer body = ByteBuffer.allocate(headerLength + payloadLength);
  ```
  `payloadLength`는 와이어의 32비트 int, 유일 가드는 `payloadLength < 0`(`:29`). `00 00`(headerLength=0) + `7F FF FF FF`(≈2GB) → 즉시 ~2GB 힙 요청 → OOM. **정수 오버플로 변종**: `headerLength + payloadLength`(최대 65535 + ~2.1B)가 음수 int로 오버플로 → 비동기 I/O completion 핸들러에서 `NegativeArraySizeException`.
- `ZLinkTlsTransportConnection.java:191-192`(Netty `FrameDecoder`)
  ```java
  byte[] header = new byte[headerLength];
  byte[] payload = new byte[payloadLength];
  ```
  동일 무제한 `new byte[payloadLength]`. `:187` `input.readableBytes() < headerLength + payloadLength`도 int 오버플로 → 음수 RHS → 비교 false → 할당 진행.
- `ZLinkStreamWireProtocol.decodeFrame:138`(WebSocket 경로 `ZLinkWebSocketTransportConnection.onBinary:56`): `new byte[payloadLength]`. 단 `frame.length == 6+headerLength+payloadLength`를 int read **후** 검증해 over-alloc이 실제 프레임 크기로 제한 → WS는 셋 중 가장 덜 노출.

**수정**: `maxReceivePayloadSize`(또는 `maxSendPayloadSize` 재사용)를 3개 transport에 주입, `headerLength + payloadLength`(long 계산)가 cap 초과 시 **할당 전** 거부, 오버플로/`< 0` 거부. TCP·TLS가 위험.
**처리 결과(2026-06-14)**: `maxReceivePayloadSize`를 옵션에 추가하고 TCP, TLS, WebSocket 수신 경로에 주입했다. TCP/TLS는 body 또는 payload 배열 할당 전에 검사하고, WebSocket은 메시지 복사 전 최대 frame 길이와 decode 단계 payload 길이를 모두 검사한다.

### F2 — LZ4 압축 폭탄 증폭 (두 모듈 중복)
- `ZLinkStreamLz4Pickler.unpickle:53`(connector) **+ `zlink-framework-core/.../ZLinkStreamLz4Pickler.java:51`·`ZLinkStreamRuntime.java:558`(framework-core 중복)** · **확인(§Codex로 범위 확대)**
  ```java
  byte[] output = new byte[header.resultLength()];
  ```
  `resultLength = dataLength + resultDiff`, `resultDiff`는 `peekLittleEndian`(`decodeHeader:84`)로 와이어에서 최대 32비트 int. 작은 압축 프레임이 큰 `resultDiff` 선언 → `LZ4SafeDecompressor.decompress` 실행 전 ~4GB 할당. `FLAG_PAYLOAD_COMPRESSED` + `compression==LZ4`(`decodePayload:768`) 시 도달. F1 증폭.
  **수정**: 할당 전 `resultLength`를 수신 cap으로 clamp — **connector·framework-core 두 모듈 모두**.
  **처리 결과(2026-06-14)**: connector LZ4는 `maxReceivePayloadSize`를 사용하고, framework-core LZ4는 기본 64KiB 상한을 적용한다. 두 구현 모두 출력 배열 할당 전에 result length를 검사한다.

### F3 — TLS 호스트명 검증 미설정(Netty 경로 추정)
- `ZLinkTlsTransportConnection.connectStage:54,68` · **확인(Netty 버전 확인 권장)**

`SslContextBuilder.forClient().build()` + `sslContext.newHandler(channel.alloc(), host, port)`. Netty는 엔진에 `SSLParameters.setEndpointIdentificationAlgorithm("HTTPS")`(또는 `SslContextBuilder.endpointIdentificationAlgorithm`)를 설정해야만 RFC 2818 호스트명 검증을 수행하는데 **둘 다 미설정** → `skipServerCertificateValidation=false`라도 체인은 검증되나 **호스트명 미검증** → 다른 호스트용 CA-valid 인증서로 MITM. (WebSocket 경로는 JDK `HttpClient`로 기본 호스트명 검증 → TLS-transport 한정.)
**수정**: `SslHandler`/엔진에 endpoint identification `HTTPS` 설정. *사용 중인 Netty 버전 기본값 대조 권장.*

### F4 — 동시성: 핸들러 리스트가 비-thread-safe
- `DefaultZLinkStreamConnector.java:43-45` · **확인**
  ```java
  private final List<ZLinkStreamErrorHandler> errorHandlers = new ArrayList<>();
  private final List<...> disconnectedHandlers = new ArrayList<>();
  private final List<...> stateHandlers = new ArrayList<>();
  ```
  `onErrorReceived`/`onDisconnected`/`onConnectionStateChanged` 및 unregister 람다(`:267,274,281`)가 사용자 스레드에서 변형, 비동기 I/O completion 스레드가 `List.copyOf(...)`로 read(`publishError:621`, `notifyDisconnected:615`, `transitionTo:603`). 구조 변경 중 `List.copyOf` → `ConcurrentModificationException`/`AIOOBE`. `handlers` `ConcurrentHashMap`의 `ArrayList` 값도 동일 패턴(`on:260` add / `dispatchToHandlers:429` read).
  **수정**: `CopyOnWriteArrayList`.

### F5 — TLS 인증서 전체-신뢰 TrustManager (footgun, opt-in) — §Codex ADDITIONAL
- `DefaultZLinkStreamConnector.java:472-473`(활성 시 insecure SSL context 설치), `:488-509`(`checkServerTrusted`가 **빈** `X509TrustManager`), 기본값 false(`ZLinkStreamConnectorOptions.java:41`) · **확인(opt-in)**

opt-in 옵션이라 자체 취약점은 아니나, 활성화 시 **모든 서버 인증서를 무조건 신뢰**(체인·호스트명 검증 전무) → 완전한 MITM 노출. F3(호스트명만 미검증)보다 강한 우회다.
**수정**: 옵션 활성화 지점에 강한 경고 문서화, 프로덕션 가드(예: 명시적 `allowInsecure` + 경고 로그) 검토.

---

## 부록 A. SUSPECTED / 저신뢰

- **S1 codec mapper `findAndAddModules()`**(`ZLinkStreamJson:24`, `...MessagePack:25`, `...Protobuf:25`): 클래스패스의 모든 Jackson 모듈 자동 등록. default-typing 자체는 아니나, downstream 앱이 폴리모피즘 모듈 추가 시 묵시 적용 가능. 명시적 모듈 세트로 고정 권장. **현 상태 취약점 아님.**
- **S2 코덱 네이밍 불일치(정확성, 보안 아님)**: `ZLinkStreamMessagePack`/`ZLinkStreamProtobuf`가 둘 다 평범한 `JsonMapper`(`...MessagePack:22`, `...Protobuf:22`) 생성 후 `readValue`/`writeValueAsBytes` → 실제로 **JSON**을 emit/parse(msgpack/protobuf 아님). 와이어 `codec` 태그만 다름. **protobuf 경로엔 protobuf의 recursion/size limit이 없음**(protobuf가 아니므로). placeholder 여부 확인 필요.
- **S3 정적 미종료 executor/event loop**: `DefaultZLinkStreamConnector.TIMEOUTS:37`, `ZLinkTlsTransportConnection.EVENT_LOOP:30`은 프로세스 전역 싱글턴, 미종료. daemon 스레드라 JVM-exit 행 없음 → 허용, 단 노트.

---

## 부록 B. 검토 후 "이상 없음" 확인

- **Jackson default typing: 전부 OFF.** `enableDefaultTyping`/`activateDefaultTyping`/`@JsonTypeInfo`/`DefaultTyping`/`PolymorphicTypeValidator` 부재. 코덱은 caller 제공 concrete `Class<T>`로 `readValue(bytes, type)` → 폴리모픽 가젯 표면 없음.
- **Java 네이티브 역직렬화: 없음.** `ObjectInputStream`/`readObject` 부재.
- **XML/YAML unsafe load, SpEL/EL**(`ExpressionParser`/`parseExpression`) 부재, `Runtime.exec`/`ProcessBuilder` 부재.
- **와이어 헤더 디코더 경계 양호**: `decodeHeader`/`decodeMetadata`가 매 read 전 `requireRemaining`, trailing 거부, packet name ≤255·metadata ≤255 cap(`ZLinkStreamWireProtocol:84-99,173-204`). `decodeFrame`은 `frame.length == 6+headerLength+payloadLength` 검증. 무제한 할당은 **스트리밍 read 경로(F1) 한정**.
- **`skipServerCertificateValidation`은 opt-in**(caller boolean, insecure 기본값 없음; secure WS는 JDK 기본 호스트명 검증). **단 §F5의 trust-all `X509TrustManager`(`:488-509`)는 별개의 고위험 footgun — 본 "이상 없음"에서 제외.**
- **`pendingRequests`/`nextRequestSeq`**: `ConcurrentHashMap` + `AtomicLong`, zero-skip, 완료 시 timeout 취소 → 누수 미발견.
- **Actor 런타임 맵 변형**(`ZLinkActorRuntime`): `synchronized (this)` 일관 보호, `close()`가 락 하에 스냅샷.

---

## 처리 우선순위
1. **F1**(수신 payload 크기 cap을 `ZLinkTcpTransportConnection:33`·`ZLinkTlsTransportConnection:191-192`·`ZLinkStreamWireProtocol.decodeFrame:138`에 적용).
2. **F2**(LZ4 `resultLength` clamp — connector + framework-core **두 곳**).
3. **F3**(Netty 호스트명 검증).
4. F4(`CopyOnWriteArrayList`), S2(codec 네이밍 — 정확성).
5. **F5**(trust-all `X509TrustManager` — 강한 경고 문서화 / opt-in 프로덕션 가드 검토).
