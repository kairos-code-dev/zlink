# .NET/C# Framework 계층 보안·버그 검토 리포트

- **작성일**: 2026-06-14
- **대상 범위**: `framework/languages/dotnet/src/{Zlink.Framework,Zlink.Framework.AspNetCore,Systems.Zlink.Stream.Connector,.Codecs,.Json,.MessagePack,.Protobuf}`
- **상태**: 2026-06-14 D1, D2, D3, D4, D5 종결. S1, S2는 오탐으로 종결.
- **참고**: 교차언어 공통 결함은 [README.ko.md](README.ko.md) 참조.

> **바인딩 범위 주의**: 본 계층은 **순수 관리 구현** — `DllImport`/`LibraryImport`/`Marshal`/`SafeHandle`/`AllocHGlobal`/`GCHandle`/`stackalloc` 없음.
> `unsafe` 인접으로 보였던 `ZlinkStreamFrameSender.cs`도 `ArrayPool`+`Span`만 사용(포인터·pinning·네이티브 메모리 없음).
> 네이티브 C-API P/Invoke 경계는 **별도 bindings 어셈블리**에 있어 본 리뷰 범위 밖. 메모리-안전 위험은 **신뢰 불가 와이어 입력 파싱**과 리소스/DoS 경계에 집중된다.

## 요약

| # | 심각도 | 분류 | 위치 | 상태 |
|---|--------|------|------|------|
| D1 | High | DoS / 무제한 수신 할당 | `Systems.Zlink.Stream.Connector/.../Framing/ZlinkStreamFrameCodec.cs:96-104` | 2026-06-14 수정 완료 |
| D2 | High | DoS / WS 단편 무제한 버퍼링 | `.../Transport/WebSocketConnection.cs:35-53,87-104` | 2026-06-14 수정 완료 |
| D3 | Medium | DoS / 무제한 수신 메시지 적재 | `.../Runtime/ZlinkStreamReceivedMessages.cs:17-34`, `ZlinkStreamReceiveDispatcher.cs:76` | 2026-06-14 수정 완료 |
| D4 | Medium | DoS / 무제한 디스패치 큐 | `.../Runtime/ZlinkStreamConnectorCallbacks.cs:230-236` | 2026-06-14 수정 완료 |
| D5 | Medium | DoS / LZ4 압축 폭탄 | `.../Compression/ZlinkStreamLz4CompressionCodec.cs:10-11` | 2026-06-14 수정 완료 |
| S1 | Low | 동시성 / CTS use-after-dispose 창 | `ZlinkStreamConnectorLifecycle.cs:256` | 의심 → **오탐(멱등 dispose)** |
| S2 | Low | 리소스 / pending-request 누수 | `ZlinkStreamPendingRequests.cs:46-66` | 의심 → **오탐(caller가 정리)** |

> ⚠️ **아래 §Codex 교차검증이 최종 판정이다.** S1/S2는 오탐 확정, D1 옵션 경로 정정.

---

## Codex 교차검증 결과 (2026-06-14)

작성 후 Codex에 문서 + 코드 위치를 주고 적대적 대조 리뷰를 요청한 결과:

| # | 초판 | Codex 판정 | 정정 요지 |
|---|------|-----------|-----------|
| D1 | High | **CONFIRMED** | 단 옵션 경로 정정 — 실제는 `Contracts/ZlinkStreamConnectorOptions.cs:17`(`MaxSendPayloadSize`만, `MaxReceivePayloadSize` 없음). 주장 유지 |
| D2 | High | **CONFIRMED (약간 과소)** | `:49` `messageLength + result.Count`도 `EnsureCapacity` 전 unchecked |
| D3 | Medium | **CONFIRMED** | `TryTake`(`:78-91`)가 `Consumed=true`만, 제거 안 함 |
| D4 | Medium | **CONFIRMED** | 단 영향은 producer가 `Dispatch`를 추월할 때 |
| D5 | Medium | **CONFIRMED** | `Unpickle` 출력 cap 없음 |
| S1 | 의심 | **❌ REFUTED(오탐 확정)** | `_closeCts` 단일 dispose(`:149-153`), 반복 실행 가드(`ZlinkStreamConnector.cs:281-285`), CTS.Dispose 멱등. 초판의 "오탐" 판단이 옳음 |
| S2 | 의심 | **❌ 오탐(과장)** | `WaitAsync` 단독으론 caller 취소 시 미제거지만, 실제 요청 경로(`ZlinkStreamConnector.cs:221-247`)가 send/wait/decode를 try/catch로 감싸 **모든 예외(취소 포함)에서 pending 제거** → 누수 없음 |
| clean | clean | **NEEDS-NUANCE** | 기본 경로(STJ Web, MessagePack Standard, no BinaryFormatter)는 클린. 단 public `Configure(...)` API(`ZlinkStreamJsonExtensions.cs:6`, `...MessagePackExtensions.cs:6`)로 호출자가 옵션 교체 가능 → "폴리모피즘 불가" 단정은 과함. **"기본 경로 클린"으로 표현** |

**Codex ADDITIONAL (초판 누락)**: framework 계층의 stream packet 압축 해제에도 connector 코덱 밖 동일 무제한 LZ4 갭 — `ZLinkStreamProtocolDefaults.cs:17-21`, 소비 `ZLinkStreamPacketPayloadCodec.cs:21-25`. send/reply 빌더(`ZLinkStreamSendBuilder.cs:47`, `ZLinkActorReply.cs:31`)에서 도달 가능. D5 범위 확대.

**Codex 총평**: D1~D5는 정확(D1 옵션 경로만 stale). 실질 보정은 **S1/S2 둘 다 오탐**(disposal 순서·caller catch/remove 고려 시) — 수정 대상에서 제외. checked-clean은 "기본 경로 근거"로 표현할 것.

---

## CONFIRMED

## 처리 기록 (2026-06-14)

- D1: `ZlinkStreamConnectorOptions.MaxReceivePayloadSize` 를 추가하고, TCP/TLS 수신 경로의
  `ZlinkStreamFrameCodec.ReadAsync` 가 payload 배열을 할당하기 전에 수신 payload 길이를
  검사하도록 수정했다.
- D2: `WebSocketConnection` 이 WebSocket message 를 조립하는 동안
  `header + payload` 최대 수신 frame 크기를 넘는지 먼저 검사하도록 수정했다. 버퍼 확장
  계산은 `checked` 산술을 사용한다.
- D5: connector LZ4 codec 과 framework stream protocol 기본 LZ4 해제 경로가
  `LZ4Pickler.UnpickledSize` 로 압축 해제 결과 길이를 먼저 확인한 뒤 `Unpickle` 을 호출한다.
- D3/D4는 메시지 저장소와 callback dispatch queue의 bounded 정책 문제라서 실행 순서
  5-4에서 별도로 처리한다.
- 실행한 검증:
  - `cd framework/languages/dotnet && dotnet test tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --no-restore --logger "console;verbosity=minimal"` 통과(48개).
  - `cd framework/languages/dotnet && dotnet test tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --logger "console;verbosity=minimal" --filter "FullyQualifiedName~StreamProtocolLz4DecompressRejectsDecodedPayloadAboveDefaultLimit"` 통과(1개).
  - 전체 `Zlink.Framework.UnitTests` 실행은 기존 문서 회귀 테스트
    `DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards` 의 matrix 문구 누락으로 1개
    실패했다. 이번 D1/D2/D5 수정과 직접 관련된 런타임 테스트는 위 필터 검증으로 통과했다.
- core runtime과 public header를 수정하지 않았으므로 `bindings/dev_sync_local_core_libs.sh` 는
  실행 대상이 아니다.
- Claude 코드 리뷰에서 D1/D2/D5가 실제 코드에서 닫혔고, D3/D4를 악화시키지 않았으며,
  "D1/D2/D5 종결 blocker가 되는 추가 이슈 없음"이라는 판정을 받았다.

후속 항목(D3, D4)은 같은 날 현재 코드와 다시 대조한 뒤 처리했다.

- `ZlinkStreamConnectorOptions.MaxReceivedMessages`를 추가하고 기본값을 1024로 두었다. `WaitFor`용 수신 메시지 보관소는 한도를 넘으면 가장 오래된 메시지를 제거한다. `WaitFor`는 메시지 확인과 도착 대기 task 캡처를 같은 lock 안에서 처리하고, 메시지를 소비할 때는 이름별 list와 전체 순서 list에서 함께 제거한다.
- `ZlinkStreamConnectorOptions.MaxPendingDispatchCallbacks`를 추가하고 기본값을 1024로 두었다. Manual dispatch callback queue는 한도를 넘으면 오래된 droppable callback을 제거해 느린 소비자나 callback 폭주가 힙을 무제한으로 키우지 못하게 했다. request completion callback은 queue 초과로 밀려나도 background에서 실행해 callback API가 조용히 사라지지 않게 했다.
- 두 새 옵션은 0 이하 값을 `ValidationFailed`로 거부한다.
- 실행한 검증:
  - `cd framework/languages/dotnet && dotnet test tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj --no-restore --logger "console;verbosity=minimal"` 통과(53개).
- core runtime과 public header를 수정하지 않았으므로 `bindings/dev_sync_local_core_libs.sh` 는 실행 대상이 아니다.
- Codex 에이전트 리뷰에서 lost-wake 가능성과 request completion callback drop 위험을 지적받아 수정했고, 재리뷰에서 "추가 이슈 없음" 판정을 받았다.

### D1 — 무제한 수신 페이로드 할당 (`MaxReceivePayloadSize` 부재)
- **분류**: DoS / 신뢰 불가 입력 · **확인 · repo 고유**
- **위치**: `Systems.Zlink.Stream.Connector/Runtime/Protocol/Framing/ZlinkStreamFrameCodec.cs:96-104`

```csharp
var payloadSize = BinaryPrimitives.ReadUInt32BigEndian(prefix.AsSpan(2, 4));
if (payloadSize > int.MaxValue) { ... }            // 유일 가드
var header  = new byte[headerSize];
var payload = new byte[(int)payloadSize];          // 와이어 제어, 최대 ~2GB
```

피어가 6바이트 prefix에 `payloadSize`를 `int.MaxValue`까지 선언 → per-message 경계 검사 전 즉시 할당. 반복 시 OOM. `Contracts/ZlinkStreamConnectorOptions.cs:17`(§Codex 경로 정정)은 `MaxSendPayloadSize=64KB`만 정의, **`MaxReceivePayloadSize` 없음**, `ReadAsync`가 옵션을 전혀 참조 안 함. `:98` `payloadSize > int.MaxValue`는 2.1GB–4.29GB만 거부(0–2.1GB는 자유 할당).
**수정**: `MaxReceivePayloadSize`(기본 64KB–1MB) 추가, `ZlinkStreamFrameCodec.ReadAsync`에 주입, `new byte[payloadSize]` 전 거부.

### D2 — WebSocket transport의 단편 메시지 무제한 버퍼링
- **분류**: DoS / 신뢰 불가 입력 · **확인 · repo 고유**
- **위치**: `.../Transport/WebSocketConnection.cs:35-53` + `EnsureCapacity` `:87-104`

```csharp
do {
    result = await webSocket.ReceiveAsync(_receiveBuffer, ...);
    EnsureCapacity(ref message, messageLength, messageLength + result.Count); // 2배 증가, cap 없음
    ...
} while (!result.EndOfMessage);
```

서버가 임의 크기 단편 바이너리 WS 메시지 스트림 → 버퍼 무제한 2배 증가 → OOM. **프레이밍 이전**이라 D1 수정으로 커버 안 됨.
**부차(Medium)**: `newLength *= 2`(`:97`)는 unchecked int 산술 — `int.MaxValue` 근처 오버플로. 실무상 `ArrayPool.Rent`가 거대 크기에서 먼저 throw하므로 fail-closed지만 `checked`/max clamp 권장.
**수정**: 누적 `messageLength`를 `MaxReceivePayloadSize + 프레임 오버헤드`로 cap, 초과 시 `FrameTooLarge` throw.

### D3 — 무제한 수신 메시지 적재 (Manual 디스패치 기본)
- **분류**: DoS · **확인 · repo 고유**
- **위치**: `.../Runtime/ZlinkStreamReceivedMessages.cs:17-34`, `ZlinkStreamReceiveDispatcher.cs:76`

`Record()`가 모든 인바운드 비-응답 메시지를 `Dictionary<string,List<ReceivedMessage>>`에 cap/eviction/TTL 없이 append. `DispatchTypedHandlersAsync`가 `WaitForAsync` 소비자/핸들러 유무와 무관하게 **모든** 메시지에 `Record()` 호출. 기본 `DispatchMode = Manual`. 소비된 메시지는 `Consumed=true` 플래그만 달고 **리스트에서 제거 안 됨** → drain 소비자도 메모리 단조 증가.
**트리거**: 앱이 `WaitForAsync` 안 하는 메시지 스트림 → 무제한 힙 증가.
**수정**: per-name/total 큐 카운트 cap(drop-oldest/backpressure), `TryTake`에서 `Consumed` 엔트리 prune.
**처리 결과(2026-06-14)**: `MaxReceivedMessages` 총량 한도를 추가하고, 한도 초과 시 가장 오래된 수신 메시지를 제거한다. `WaitFor`는 메시지 확인과 도착 대기 task 캡처를 같은 lock 안에서 처리하고, 소비된 메시지를 즉시 제거한다. 회귀 테스트는 한도 2에서 3개 메시지를 받은 뒤 오래된 첫 메시지가 제거되고 남은 두 메시지만 `WaitFor`로 수신되는지 확인한다.

### D4 — 무제한 디스패치 콜백 큐 (non-Immediate 모드)
- `.../Runtime/ZlinkStreamConnectorCallbacks.cs:230-236` · **확인**

`Enqueue`가 `_dispatchQueue`에 cap 없이 push, 앱이 명시적 `DispatchAsync` 호출 시에만 drain. 에러 폭주/재연결 폭풍/느린 소비자 시 무제한 증가.
**수정**: drop/backpressure 정책의 bounded 큐.
**처리 결과(2026-06-14)**: `MaxPendingDispatchCallbacks` 한도를 추가하고, Manual dispatch callback queue가 한도를 넘으면 오래된 droppable callback을 제거한다. request completion callback은 queue 초과로 밀려나도 background에서 실행한다. 회귀 테스트는 한도 2에서 3개 typed handler callback이 쌓일 때 마지막 두 callback만 실행되는지, 한도 1에서 request completion callback 2개가 모두 전달되는지 확인한다.

### D5 — LZ4 압축 폭탄 (압축 활성 시)
- `.../Compression/ZlinkStreamLz4CompressionCodec.cs:10-11` · **확인**
  ```csharp
  public ReadOnlyMemory<byte> Decompress(...) => LZ4Pickler.Unpickle(payload.Span);
  ```
  `Unpickle`이 pickled blob에 박힌 attacker-제어 original-length로 출력 할당. 작은 압축 프레임(어떤 cap 아래)이 큰 버퍼로 팽창, 사후 크기 검사 없음. `DecompressIfNeeded`(`FrameSender.cs:138-160`)도 cap 없음.
  **추가 경로(§Codex)**: framework 계층 packet 압축 해제에도 connector 코덱 밖 동일 무제한 LZ4 갭 — `ZLinkStreamPacketPayloadCodec.cs:21-25`(defaults `ZLinkStreamProtocolDefaults.cs:17-21`), `ZLinkStreamSendBuilder.cs:47`/`ZLinkActorReply.cs:31`에서 도달. 수정 시 두 곳 모두.
  **수정**: `Unpickle` 전후로 압축 해제 길이에 `MaxReceivePayloadSize` 강제.

---

## 부록 A. SUSPECTED → **§Codex에서 S1·S2 둘 다 오탐 확정**

> 🔴 아래 두 항목은 Codex 교차검증 결과 **오탐**으로 판명되어 수정 대상에서 제외한다(기록 보존).

- **S1 `_closeCts` use-after-dispose 창 — 오탐(§Codex)**: `ZlinkStreamConnectorLifecycle.cs:256`(`CreateLinkedTokenSource(_closeCts.Token)`)가 disposed 토큰을 링크할 가능성을 검토했으나, **단일 Dispose(`:149-153`) + 반복 실행 가드(`ZlinkStreamConnector.cs:281-285`) + CTS.Dispose 멱등**으로 실현 불가 → **오탐, 수정 불필요**. (sub-agent의 "double-dispose CRITICAL"도 동일 오탐.)
- **S2 caller-token 취소 시 pending-request 누수 — 오탐(§Codex)**: `WaitAsync`(`ZlinkStreamPendingRequests.cs:46-66`) 단독으로는 caller 취소 시 엔트리를 안 지우지만, 실제 요청 경로(`ZlinkStreamConnector.cs:221-247`)가 send/wait/decode를 try/catch로 감싸 **취소 포함 모든 예외에서 pending 제거** → **누수 없음, 오탐**.

---

## 부록 B. 기각된 sub-agent 주장 (검증 결과 거짓)
- **"FailAll 순회 race로 request 누수"** — 거짓. `ConcurrentDictionary` 열거 안전, `TryRemove`+`TrySetException`(멱등) 이중-fail 방지, FailAll 시작 후 추가된 request는 자체 완료 경로.
- **"`_closeCts` double-dispose가 CRITICAL"** — 버그 아님(single Dispose, 멱등).

---

## 부록 C. 검토 후 "이상 없음" 확인

- **`BinaryFormatter`/`NetDataContractSerializer`/`LosFormatter`/`ObjectDataProvider`/`SoapFormatter` 전무**(grep 확인).
- **JSON 코덱**(`ZlinkStreamJsonExtensions.cs`, dispatcher `WireError` parse): `System.Text.Json` + `JsonSerializerDefaults.Web`, **기본 경로**에 폴리모피즘/`[JsonDerivedType]`/커스텀 type-name handling 없음 → 기본 구성에서 STJ 폴리모픽 RCE 표면 없음. (단 public `Configure(...)` API(`ZlinkStreamJsonExtensions.cs:6`, `...MessagePackExtensions.cs:6`)로 호출자가 옵션 교체 가능 → "불가" 단정이 아니라 **기본 경로 한정 클린**.)
- **MessagePack 코덱**: `MessagePackSerializerOptions.Standard` — `Typeless`/`TypelessContractlessStandardResolver` 아님 → type-embedding RCE 없음. (하드닝 nit: `Standard`가 `WithSecurity(MessagePackSecurity.UntrustedData)` 미설정이라 deep/hash-collision 입력 특정 가드 없음 — 단 상류 `MaxReceivePayloadSize`가 실제 수정.)
- **Protobuf 코덱**: `Google.Protobuf` `MergeFrom` 기본 recursion/size limit, `CodedInputStream` 약화 재설정 없음.
- **Metadata 코덱**(`ZlinkStreamMetadataCodec.cs`): 모범적 경계 검사 — 매 길이 read를 잔여 버퍼 대조, count ≤255, key 1..255, value ≤65535, trailing/중복 키 거부. 클린.
- **TLS**: 기본 인증서 검증 사용(null 콜백), `SkipServerCertificateValidation` 명시 opt-in 시에만 우회(`ZlinkStreamTransportFactory.cs:134-159`) — 문서화된 escape hatch, 허용.
- **`ArrayPool` 사용**(`ZlinkStreamFrameSender` send 경로): rent/return을 `try/finally`로 정확 래핑, WS pending 버퍼를 `catch`·`CloseAsync` 포함 모든 경로에서 반환.
- **`SemaphoreSlim _sendGate`**: `try/finally`로 대칭 Wait/Release.
- **AspNetCore 계층**: path traversal·regex/ReDoS·헤더/route injection 없음, reflection은 startup 어셈블리 스캔(compile-time 알려진 인터페이스 타입, request 데이터 아님)에만, DI 수명 건전(싱글턴→싱글턴, per-event `CreateAsyncScope`), 적절한 `IAsyncDisposable`. nit: `ZLinkMonitoringPollingRunner.cs:53,78` 두 `Task.Delay`에 `ConfigureAwait(false)` 누락(Low, 백그라운드 루프).

---

## 처리 우선순위
1. **`MaxReceivePayloadSize`를 `ZlinkStreamConnectorOptions`에 추가**하고 (1) `ZlinkStreamFrameCodec.ReadAsync`, (2) `WebSocketConnection.ReadAsync` 누적, (3) LZ4 `Decompress` 출력에 강제 — 이 단일 경계가 주 OOM 벡터(D1/D2/D5)와 대부분의 DoS 증폭기를 무력화.
2. **D3/D4**(무제한 메시지/콜백 큐) — 2026-06-14 bounded 보관 정책으로 수정 완료.
3. ~~S1/S2~~ — **§Codex에서 둘 다 오탐 확정, 수정 대상 제외.**
