# `framework/languages/java` (Java+Kotlin) POSD·DDD 리팩토링 수정 목록

> 2026-07-09 `framework/languages/java` 전수 리뷰. 대상은 core 바인딩과 **별개**인 Java/Kotlin **framework** 코드다:
> `zlink-framework-core`(370파일 ~30.7k줄) + 위성 9모듈(`zlink-framework-kotlin` coroutine/Flow 확장 11파일, `zlink-framework-spring-boot-starter`,
> `zlink-framework-locations-redis`, `zlink-http-client`(+`-kotlin`), `zlink-stream-connector`, `zlink-framework-codec-msgpack`,
> `zlink-framework-codec-protobuf`, `zlink-framework-testkit`). 이 문서는 두 리뷰의 **병합 정본**이다:
> (1) 8-에이전트 read-only 병렬 POSD/DDD 전수 리뷰(A~D 트랙, 입자 단위 dead·결함·중복·god-file) +
> (2) codex 병렬 리뷰(R1~R6 우선순위 P0~P2 아키텍처 책임 분리 + 실행 순서·검증 게이트). 겹치는 god-file 분해는 통합했다(R1=§D1/§C2, R2=§D2/§C6, R3=§D-Cross, R4=§D9, R5=§D8, R6=§D10).
> 파일:라인은 리뷰 시점 기준이므로 편집 전 현재 코드로 재확인한다. 정본 대조 기준은
> `dotnet-framework-posd-ddd-refactor-list.ko.md`(.NET) + `node-framework-posd-ddd-refactor-list.ko.md`(hardened Node)이며,
> Java는 대부분 동형(isomorphic) 결함이거나 이미 더 단순하게 해소돼 있다(§부록 A).

## dead-code 판정 규칙 (중요)

Java/Kotlin은 node의 `dist/internal` 문제는 없지만, **런타임 클래스는 각 모듈 `src/test`(+`contractTest`/`integrationTest`/`fakeBackendTest`)·`e2e`·`e2e-kotlin`·`samples`·`zlink-framework-kotlin`가
패키지 import로 직접 구동**한다(core `src/test` = 32 파일). 따라서 dead 판정은 반드시 **(a) `src/main` grep + (b) 위 모든 test/e2e/sample 디렉터리 grep**을
`--include=*.java --include=*.kt`로 하고 `build/`·`bin/`·`out/` 산출물은 제외한다. `public` 런타임 클래스가 어느 테스트에서든 쓰이면 dead 아님(node에서 이 규칙을 놓쳐 오탐한 사례 있음).
**동명 주의:** 여러 클래스가 같은 메서드명을 갖는다(예: `requireTurn`은 actor/spot/channel `YieldRequestCall`에서 live, 3개 bound-session runtime에서만 dead) — 삭제 전 소유 클래스별로 호출 여부를 분리 확인한다.
Java 컴파일러 unused 경고(`-Xlint`)나 IDE inspection이 declaration-level dead(미사용 import/private/param)의 보조 신호다.

**위험 표기:** **없음**(control plane, 빌드+테스트로 충분) / **code-motion**(코드 이동·의미 동일, public 표면·계약 유지) /
**벤치**(per-message/dispatch/reply/relay/location-read hot, baseline vs patched 무회귀 증명 필수, [[feedback_perf_measure_before_commit]]).

**가드레일(변경 금지 불변식):**
- public 계약(`framework/**` 계약 패키지, spring-boot-starter bean 표면, kotlin 확장 표면) 유지. "다른 언어에 있다"만으로 삭제/추가 금지([[CLAUDE.md]] parity 정책 — 미참조 public 계약은 삭제가 아니라 "목록화" 후 parity/deprecation 승인).
- 라우팅/조인/브릿지/bound-session 원시연산은 native 위임 — TS/Java 재구현 아님.
- codec 불변식: framework=JSON 기본, msgpack/protobuf는 독립 extension 모듈(content-type 판별), session/joinspot만 직접 Message. **모듈 병합·cross-module codec 공유 금지**(stream-connector 독립성).
- **Java/Kotlin 이디엄 유지:** 리플렉션/애노테이션 핸들러 스캔은 idiomatic Java(node/dotnet의 attribute/expression-tree와 형태만 다름, parity 갭 아님). Kotlin 모듈은 java 런타임 위의 얇은 coroutine/Flow 확장 — **java 로직 재구현·2-인터페이스 분할 제안 금지**([[feedback_api_parity_vs_language_idiom]]).

---

## 0. 우선순위 맵 (codex R1~R6 ↔ 상세 항목)

| ID | 항목 | 우선순위 | 상세 |
|---|---|---|---|
| R1 | SPOT god-file(`ZLinkSpotRuntime`) 분해 + Entry/User activation dispatch 상태기계 2벌 통합 | **P0** | §D1 + §C2 |
| R2 | Channel god-file(`ZLinkChannelRuntime`) 분해 + route-to-SPOT 전략 분리 + `Thread.sleep` pause 제거 | **P0** | §D2 + §C6 + §C11 |
| R3 | actor↔spot 협업 경계 정리(`ZLinkActorSessionCoordinator`) | P1 | §D-Cross |
| R4 | Spring capability registrar 분리 | P1 | §D9 |
| R5 | Redis location store 책임 분리 | P1 | §D8 |
| R6 | Kotlin location 확장 파일 분리 | P2 | §D10 |
| — | stdout 디버그 계측 소거([boot]+[zlink-java-stream-trace]) | **P0(즉효)** | §B1 |
| — | ⚠️ stream-connector가 framework-core 의존(레이어링 위반, 독립성 복원) | **P0** | §C0 |
| — | stream wire 중복(core↔connector) — 교차 spec 테스트/contracts-only 공유(코드병합 아님) | P1 | §C1 |
| — | RuntimeHost(`ZLinkFrameworkRuntime`) 조립 책임 분리 | P1 | §D3 |
| — | Actor deadline-retry 스케줄러/예외분류 통합 | P1 | §C4 + §C5 |
| — | 선언 단위 dead 정리 | 선행 | §A1 |

권장 실행 순서(codex): ① §A1 dead + §B1 stdout(즉효) → ② R1(§D1/§C2)·R2(§D2/§C6)를 함께(SPOT route ↔ channel route 강결합) → ③ R3(§D-Cross, R1 이후) → ④ R4(§D9, 병렬 가능) → ⑤ R5(§D8, 별 PR) → ⑥ R6(§D10, kotlin binary-compat 확인 후 마지막). C1(§C1)은 R1 이전 spec 게이트 선행 권장.

---

## A. 삭제 트랙

### A1. 무참조/도달불가 (src+test+e2e+samples grep 검증, 없음)

- **spots `ZLinkSpotRuntime.java:188,390` `routerSpotNodeNames`** — write-only dead 필드(read 0, grep 2=선언+add). 즉시 삭제.
- **backend `runtime/backend/ZLinkBackendAutoConnectType.java`(파일 전체)** — enum, 전 트리 grep 1(자기 파일). 동명처럼 보이는 매치는 전부 별개 `locations.ZLinkLocationAutoConnectType`(live). 파일 삭제.
- **actors — bound-session `turn` 플러밍(3파일 한정)** — `ZLinkBoundSessionRuntime.java:377`/`ZLinkNativeBoundSessionRuntime.java:207`/`ZLinkRoutedBoundSessionRuntime.java:229`의 `requireTurn()` + 각 SendCall record의 `turn` 필드(`:324`/`:141`/`:159`) + `captureCurrent`(`:302`/`:104`/`:91`) + `ZLinkAwait`/`ZLinkYieldTurn` import — 이 3파일에서는 **정의만 있고 호출 0**(`ZLinkBoundSessionSendCall` 계약에 `yield()` 없음). **⚠️ 주의:** actor/spot/channel/worker의 동명 `requireTurn()`은 `YieldRequestCall`에서 live(`ZLinkActorRuntime:1504…`, `ZLinkSpotRuntime:3819…`, `ZLinkChannelRuntime:3135`, `DefaultZLinkWorkerCall:80`) — 삭제 금지. 소유 클래스별 분리 삭제.
- **actors `ZLinkActorRuntime.java:654-658` `getOrCreateManagedActorWithoutCreateNotification`** — 호출 0 → 삭제 후 private `getOrCreateManagedActor(...,notifyCreated)`의 `notifyCreated` 항상 true 접기.
- **actors `ZLinkActorRuntime.java:929-934` `markJoined` 3-arg 오버로드** — 무참조(실사용은 4-arg via `markJoinedAsync`).
- **channels `ZLinkChannelRuntime.java:1611` `copyMessageBytes`** — private static, grep 1(자기). 삭제.
- **handlers `ZLinkHandlerScanner.java:815-835` `addSpotActorLifecycleInterfaceHandler`** — private static, 호출 0(~21줄). 삭제(7-arg `ZLinkScannedHandler` 생성자는 `addInterfaceHandler`가 계속 사용, 유지).
- **streams `ZLinkStreamHeaderCodec.java:119-121` 3-인자 `encode(int,String,Optional<Long>)`** — package-private, 전 트리 호출 0.
- **import 미사용:** `ZLinkFrameworkRegistration.java:5` `LinkedHashMap`.
- **test-only(계약 아님, package-private) — 기능 필요성 확인 후 정리:** `ZLinkOwnerLeaseTracker.getLiveOwnerSetVersionAsync`(`:45-60`, `ZLinkOwnerLeaseTrackerTest`만), `ZLinkLocationLifecycle.ownsActor`(`:218-223`, `ZLinkLocationLifecycleTest`만).
- **가시성 과다(삭제 아님, `private`/package-private 축소):** channels `closeSpotRouteBridges`(`:1220`, 외부 0), actors `markLeft`(`:1025`)·`markJoined` 4-arg(`:936`).

### A2. 미참조 public 계약 (⚠️ test=0만으로 삭제 불가, parity/deprecation 승인 후)

- `framework/configuration/ManualEndpointListBuilder.java`(4줄 public interface) — 전 트리 참조 0(spring/kotlin/e2e/samples 포함). public 계약이라 즉시 삭제 금지, "미참조 public 계약"으로 목록화. 그 외 `framework/{errors,configuration,handlers,spots,actors,channels,…}` public 타입은 전부 consumer≥2(삭제 대상 없음).

**A 착수 순서:** A1(빌드+테스트로 충분) → A2(parity 확인 후).

---

## B. 결함 수정 (correctness)

- [ ] **B1. ⭐stdout 디버그 계측 소거** (없음, 가장 큰 실효 정리) — 두 종류를 구분:
  - **(P0, 무조건 출력) `[boot]` 기동 로깅:** `boot()` 정의가 게이트 없는 `System.out.println`이고 무조건 실행된다. `host/ZLinkFrameworkRuntime.java:350`(정의) + 생성자 **26회 호출**, `spots/ZLinkSpotRuntime.java:929`, `locations/ZLinkLocationAutoConnectHost.java:293`, `locations/ZLinkAutoConnectReconciler.java:224`(+tick/claim 경로), spring `ZLinkFrameworkLifecycle.java:238-240`(3회). runtime 생성/기동 시마다 stdout 오염 → 최우선 제거.
  - **(opt-in, `STREAM_TRACE` 게이트) `[zlink-java-stream-trace]` 릴레이 트레이싱:** 전부 `STREAM_TRACE` 상수 게이트라 기본 무비용(오탐 정정 — core도 무조건 아님): `actors/ZLinkSessionActorsRuntime.java:43,383-384`·`streams/ZLinkStreamRuntime.java:61,920-921`·`channels/ZLinkChannelRuntime.java:109,1051-1052`·`spots/ZLinkSpotRuntime.java:148,4384/4622/4639`·`zlink-stream-connector/.../DefaultZLinkStreamConnector.java:1041-1043`. sweep에 포함하되 P0 아님(플래그 off 시 무비용).
  - 출처 commit `890cda7f6 "기동 순서 감사"` — 감사용 임시 계측 미소거([[project_unidir_auto_connect]] "java 기동 레이스 감사 잔여"). 정식 진단은 `ZLinkMessageFlowTracer`(JUL/파일 sink, 모드 게이트)가 담당하므로 `[boot]`는 제거, `[zlink-java-stream-trace]`는 제거 또는 게이트된 `LOGGER.log(Level.FINE,…)`로 라우팅.
- [ ] **B2. `requireChannelHandlerShape` no-op 검증 루프** (없음, 검증 게이트 결함)
  - `handlers/ZLinkHandlerScanner.java:847-852` — 파라미터 1..N 순회하나 `if(context/CancellationToken) continue;`만 있고 **else-throw 없음** → 채널 핸들러의 부적격 파라미터가 조용히 통과. `contextType` 파라미터가 이 죽은 비교에서만 읽힘. 형제 `requireActorPacketHandlerShape`(`:306`)/`requireSpotMethodShape`(`:269`)는 정상 throw → 비대칭. else-throw 추가 또는 루프+파라미터 제거. dotnet A-LO1(const-false validator)의 Java 대응.
- [ ] **B3. client-server/subscribe 수신 루프 try/catch 부재 → 프레임 하나로 수신 정지** (없음, DoS성)
  - `channels/ZLinkChannelRuntime.java:1320-1337 startRequestLoop` / `:1993-2006 startSubscribeLoop` — `while(running)` 안에서 dispatch를 **try/catch 없이** 호출. 반면 `startRouteLoop`(`:1691-1728`)은 `catch(RuntimeException)` report-continue. 동기 throw 지점(`replyErrorAndReport`→native, error-sink throw, `dispatchPublish`(`:2010`)가 probe 없이 `parsePacket`→빈 parts IOOBE) 발생 시 `receiveExecutor` 스레드 종료 → 채널 수신 영구 정지. node B1과 동형 부류(원인은 seq-null이 아니라 루프 보호 비대칭). route 루프와 동일하게 catch-report-continue 추가.
- [ ] **B4. observed-generation guard가 2개 독립 인스턴스로 분리됨** (벤치, correctness 경계)
  - `locations/ZLinkLocationRuntimeQueryService.java:38`와 `ZLinkStoreLocationResolvers.java:29`가 각각 `new ZLinkObservedLocationGenerations()` → query 경로(list*)와 resolver 경로(resolveSpot/Actor/Route)가 서로 다른 high-water mark. 한 표면이 stale로 거부한 generation을 다른 표면이 accept 가능(교차-표면 불변식 무력). host(`ZLinkFrameworkRuntime.java:126,131-134`)에서 단일 생성해 주입. dotnet D5 동형. (§C7 lease tracker 중복과 동일 배선 지점에서 함께 해소.)
- [ ] **B5. actor reply metadata/compression 유실 + 채널/spot outbound metadata no-op** (벤치/draft, silent no-op)
  - `actors/ZLinkSessionActorsRuntime.java:509-516 replyLocal`이 응답 헤더 metadata에 `Map.of()`·압축 없음 하드코딩. 또한 채널/spot outbound call의 `metadata(key,value)`가 전부 `return this;` no-op(`channels:2983,3028,3085,…`, `spots:3563,3632,3728,3891,4093,4148,4222,4500`). node B4 동형. **단 Java엔 `ZLinkSpotActorReplyOptions` 같은 공개 옵션 계약이 부재** → "무시되는 옵션"이 아니라 "표면 부재". 공통 spec에 채널/actor metadata 지원 여부 확인 후 배선 또는 draft로 분리(control-plane JSON framing 불변식 충돌 여부 점검).
- [ ] **B6. `ZLinkActorClientRuntime.decodeReply` 프레임 프리픽스 인라인 재구현** (벤치, C9와 결합)
  - `actors/ZLinkActorClientRuntime.java:314-334`가 6B prefix(2B header-len + 4B payload-len) + slice offset을 손코딩. `streams/ZLinkStreamFrameCodec.java`엔 `encode`만 있고 `decode`/`tryDecode` 부재 → wire 레이아웃 변경 시 client 조용히 깨짐. `ZLinkStreamFrameCodec.tryDecode` 추가 후 호출(§C3와 동반).
- [ ] **B7. http-client 2xx 빈 바디에서 `submit<T>` decode 실패** (없음, 저신뢰)
  - `zlink-http-client/.../ZLinkHttpRequestBuilder.java:209-221` — 204/304/빈 200에서 `MAPPER.readValue("", type)`가 "No content" throw → 성공 응답이 `HTTP response body decode failed`로 뒤집힘. node B8 동형. C++ `submit<T>` 계약(빈 바디 null 허용 여부) 대조 후 short-circuit.
- [ ] **B8. route HandlerNotFound reply framework-error 마커 미부착** (없음, 저신뢰)
  - `spots/ZLinkSpotRuntime.java:2109-2111,5381-5383` / `channels`(route) — route 오류를 평문 문자열로 reply(actor 경로 `:2576,5776`은 예외 wrap). 클라이언트가 정상 payload로 오인 여지. 오류 프레이밍 규약 spec 확인 후 정렬.

- [ ] **B9. routed bound-session `SendCall.submit()`가 send 실패를 삼킴** (없음, correctness)
  - `actors/ZLinkRoutedBoundSessionRuntime.java:216-225` `SendCall.submit()`이 `sendFrame(...)`(return type `CompletionStage<Void>`, `:113`)를 호출하고 **반환 stage를 버린 뒤 무조건 `ZLinkSubmitStage.completed()` 반환**(`:225`). `sendFrame`은 not-ready 시 `failedFuture`(`:135-139`)·route-channel 시 transport stage(`:124-128`)로 실패를 전달하는데 이를 무시 → 라우팅 bound-session send가 실패해도 caller는 성공으로 관측. 대조: native(`ZLinkNativeBoundSessionRuntime.java:197`)·local(`ZLinkBoundSessionRuntime.java:373`)은 `ZLinkSubmitStage.from(...)`로 stage를 감싼다. `submit()`이 `ZLinkSubmitStage.from(sendFrame(...))`로 stage 전파하도록 수정(§C8 SendCall 3벌 통합과 함께 처리 가능). (부수 점검: try-with-resources `frame` close 시점 vs 비동기 send 완료 경합.)

**B 착수 순서:** B1(즉효, 위험 없음) → B3(수신 정지) → B9(routed send 실패 삼킴) → B4(guard 단일화) → B6(client decode) → B2 → B5/B7/B8(계약/spec 확인 동반).

---

## C. 구조 통합 — 지식 중복 소거

- [ ] **C0. ⭐(P0, 레이어링 위반) `zlink-stream-connector`가 `zlink-framework-core`를 의존** (없음, 아키텍처 불변식 위반)
  - `zlink-stream-connector/build.gradle.kts:9 api(project(":zlink-framework-core"))` — **stream-connector는 framework 무의존 독립 패키지여야 한다는 언어 공통 불변식을 위반**한다. 확인: node connector(`node/packages/stream-connector`)는 `@zlink-systems/framework` import 0, dotnet `Systems.Zlink.Stream.Connector`는 `K4os.Compression.LZ4`(자체 lib)만 참조 — **둘 다 framework 무의존**. Java만 core 전체(370파일 30.7k줄)를 끌어온다.
  - 게다가 `api`(implementation 아님)라 **connector를 쓰는 모든 소비자에게 framework-core 전체가 transitive로 노출**된다(컴파일 classpath 오염). 이걸 정당화하는 실사용은 **단 한 클래스**: `ZLinkStreamLz4Pickler.java:3`이 core **내부** `runtime.streams.ZLinkStreamPayloadCompression`(public 계약 아님, `runtime.*` 구현)에 LZ4 pickle 3메서드를 위임(`pickle`/`unpickle`/`unpickle(max)`). 즉 20줄짜리 델리게이트 래퍼 하나 때문에 전체 core 의존이 걸려 있다. (앞선 리뷰가 이 델리게이션을 "LZ4 twin 이미 소거"라 **긍정적으로 오판**했는데, 실제로는 이것이 위반의 벡터다.)
  - **수정 방향(dotnet/node와 동일하게 독립성 복원):** connector가 자체 LZ4 pickle을 소유(node는 자체 구현, dotnet은 K4os 사용) — 또는 LZ4 pickle 바이트 규약만 담은 **framework 런타임 무의존 저수준 공유 모듈**(contracts-only, `runtime.*` 아님)로 추출해 core와 connector 양쪽이 참조. 그 후 **`api(project(":zlink-framework-core"))` 제거**. `lz4-java` 의존은 유지(connector 자체 압축 lib). 이 위반을 먼저 없애야 C1 wire-format 중복도 "core 의존을 이용한 코드 공유"가 아니라 언어 공통 방식(교차 spec 테스트/contracts-only 공유)으로 올바르게 풀린다.
- [ ] **C1. ⭐stream wire 포맷이 core `runtime/streams`와 `zlink-stream-connector`에 중복** (없음, 최대 유지보수 impact)
  - twin: 6B prefix(`ZLinkStreamFrameCodec.java:30-35` ↔ connector `ZLinkStreamWireProtocol.java:149-155`), 헤더(`ZLinkStreamHeaderCodec.java:123-186/26-103` ↔ `:34-140`), TLV metadata(`ZLinkStreamHeaderCodec.java:216-286` ↔ `:197-257`), kind/codec/flag enum(`ZLinkStreamHeaderCodec.java:15-21` ↔ `:11-28`). LZ4 pickle은 현재 C0의 위반 경로로 "공유"돼 있으나, C0 수정(독립성 복원) 후에는 나머지 wire 포맷과 동일하게 다뤄진다.
  - **검증 규칙 divergence(비대칭 위험):** connector `validateHeader`(`ZLinkStreamWireProtocol.java:259-297`)는 강한 의미 검증(send≠reqSeq, request/response reqSeq 필수, error=JSON codec, control 제약)인데 core `decodeOrPlain`(`ZLinkStreamHeaderCodec.java:26-103`)은 control 제약만 + **plain-string 폴백**(unknown kind→UTF-8 헤더). → 한쪽이 받는 프레임을 다른 쪽이 거부 가능. (dup-metadata-key는 양쪽 fail-fast로 일치 — node와 다름.)
  - **권장(dotnet/node C1과 동일 — 코드 병합/의존 아님, 가드레일=connector 독립 유지):** (최소) core↔connector 교차 왕복 spec 테스트 추가(현재 connector `ZLinkStreamWireProtocolTest`/`JavaNodeStreamInteropTest`만 있고 core는 `ZLinkStreamHeaderCodecTest`뿐, 교차 없음) + 검증 규칙 정렬. (이상) 바이트 레이아웃 상수(KIND/CODEC/FLAG/prefix)를 **framework 런타임 무의존 contracts-only 저수준 모듈**로 승격해 양쪽이 참조(C0의 LZ4 공유 모듈과 같은 계층). **core 의존을 이용한 공유는 금지**(그것이 C0 위반).
- [ ] **C2. ⭐Entry vs User activation dispatch 상태기계 2벌 near-verbatim** (벤치, per-message/dispatch)
  - `spots/ZLinkSpotRuntime.java` — `EntrySpotActivation`(동기 void)과 `SpotActivation`(CompletionStage async)이 dispatch 골격을 통째 복제: route(`:2069-2230` ≈ `:5340-5500`), actor-message(`:2437-2614` ≈ `:5656-5814`), subscription(`:2301-2368` ≈ `:5513-5582`), actor-lifecycle(`:2390-2436` ≈ `:5607-5655`), routed relay(`:2232-2289` ≈ `:5959-6016`). `replyActorDispatchError`(`:2676-2718` == `:5833-5877`)는 완전 동일. dotnet C2/node C2 동형. kind/surface/reply-strategy 매개화한 `ZLinkSpotDispatchPipeline` + 공통 base activation으로 추출(actor 해결원·spotRid원·동기/async 어댑터만 주입). `reportDispatchError` 12-인자 positional 21회 호출(정의 `:4414`)도 `ZLinkDispatchFailure` 빌더로.
  - channels도 동형(하위): `dispatchSend`(`:2066`)/`dispatchRouteSend`(`:2114`)/`dispatchPublish`(`:2008`)/`dispatchRequest`(`:1397`)/`dispatchRouteRequest`(`:1927`) 5벌 + `invoke*Handler` 3+2벌(`:2292-2569`). 같은 dispatch core로.
- [ ] **C3. stream frame decode 인라인 + response 헤더 생성 반복** (벤치, per-reply)
  - `ZLinkStreamFrameCodec`에 `tryDecode` 추가(§B6) → `ZLinkActorClientRuntime:314-334` 손코딩 제거. "request→response 헤더(correlationId echo)" 생성 5곳(`streams/ZLinkStreamRuntime.java:850-858,638-645`, `actors/ZLinkSessionActorsRuntime.java:509-516`, `actors/ActorPacketFrames.java:34-40,46-53`) → `ZLinkStreamHeader.createResponse(requestHeader,codec,flags,metadata)` 팩토리. dotnet C9 동형.
- [ ] **C4. ⭐actor deadline-retry 스케줄러 관용구 ~10벌 + 4 executor** (벤치, per-relay/send)
  - `Attempt implements Runnable` + `nanoTime()≥deadline` + `EXECUTOR.schedule(this,10,MS)` 패턴이 `ZLinkSessionActorsRuntime.java:352,520,555,579,645`·`ZLinkBoundSessionRuntime.java:116,144,193,390`·`ZLinkNativeBoundSessionRuntime.java:220`·`ZLinkActorClientRuntime.java:150,186`에 반복 + daemon single-thread `ScheduledExecutorService`가 파일마다 **4개 별도 생성**. 공유 `ZLinkActorRelayRetry`(deadline + attempt supplier + 단일 executor)로 수렴. node C5 확대형.
- [ ] **C5. actor 예외 분류 헬퍼 verbatim 중복** (없음)
  - `isRetryableSubmitResult` 3벌(`ZLinkSessionActorsRuntime.java:639`/`ZLinkBoundSessionRuntime.java:434`/`ZLinkNativeBoundSessionRuntime.java:265`), `isAlreadyBound` 2벌(`:716`/`:245`), `isRetriableBindFailure` 2벌(`:724`/`:253` 후자가 상위집합), `findRequestException`/`findConfigException` 2벌 → `ZLinkActorSubmitFaults` util. C4와 같은 파일군.
- [ ] **C6. (route-to-SPOT 전략) channels dispatch 대상 선택 + 전송 매트릭스 중복** (벤치/code-motion)
  - `channels/ZLinkChannelRuntime.java` — `sendToSpotViaRouterChannel`(`:747-786`)와 `requestToSpotViaRouterChannel`(`:836-946`)의 채널 선택 prefix가 verbatim(`:752-765`≡`:846-866`) + send/request × bridge/spot-router-node 4구현이 `copyMessages→submit→complete→finally close` 골격 공유. `ZLinkSpotRouteTarget`(bridge|spotRouterNode) 해석 헬퍼 + 완료 규칙 집약(codex R4 = route dispatch 전략 분리). raw bridge reply 큐(`SpotRouteBridgeRawReplyCorrelator`, `:1509-1690`)는 bridge 전용 객체 소유.
- [ ] **C7. Location live-row 필터 + lease tracker 중복** (벤치, per-read)
  - live-row 필터(observed accept → owner lease live) 재구현: `ZLinkLocationRuntimeQueryService.filterLive`(`:247-266`) vs `ZLinkStoreLocationResolvers.filterLivePeers`(`:70-83`)+`resolveLiveAsync`(`:85-96`) → `ZLinkLiveRowFilter` 추출. lease tracker 2 독립 인스턴스(`QueryService:47-49` + `StoreLocationResolvers:36-38` 각 `new ZLinkOwnerLeaseTracker` → 폴링 2배) → host에서 단일 생성 후 공유(B4 observed guard와 동일 지점). node C8/dotnet C12 동형. **주의:** mesh-scan resolver 쌍둥이는 Java 이미 단일화(부재).
- [ ] **C8. Bound-session SendCall record 3벌 + join 결과 디코드 중복** (code-motion)
  - `ZLinkBoundSessionSendCall` 구현 3벌 near-verbatim(`ZLinkBoundSessionRuntime$SendCall:315`/`Native:129`/`Routed:146`, header 빌드+metadata 누산+encode+sendWithRetry) → 공통 base(A1 dead `turn` 제거 후 shape 근접). `ZLinkActorRuntime` `JoinEntrySpotCall`(`:1415-1437`) vs `JoinSpotCall.decodeJoinResult`(`:1683-1761`) 조인 결과 디코드 중복 → `decodeJoinResult` 공용 헬퍼. `ZLinkActorSpotRoutePackets.encodeJoinRequest`(`:23-39`)는 join payload 손빌드(단, `encodeActorRef`(`:102`)는 `createBoundSessionSendParts`(`:66`)·`createActorPacketParts`(`:76`)에서 **live** — "미사용" 아님, 삭제 금지). join payload 자체는 이미 이 파일로 중앙화됨(node C7 3벌 손빌드 상황 부재).
- [ ] **C9. handler scanner Class/String(kotlin) 오버로드 5세트 + 술어 중복** (code-motion)
  - `ZLinkHandlerScanner`의 `addInterfaceHandler`(`:415-465`)/`addSpotPacketInterfaceHandler`(`:501-561`)/`addSpotSubscriptionInterfaceHandler`(`:563-622`)/`addSpotTimerInterfaceHandler`(`:624-697`)/`addSpotActorPacketInterfaceHandler`(`:753-813`)가 `findInterface` 인자 타입(Class vs String)만 다르고 본문 verbatim(~200줄). `ZLinkGenericTypeResolver`의 `findInterface`(`:36-68`)/`matchType`(`:70-106`)도 동형 쌍 → Predicate 주입 1벌. `ZLinkCodecRegistration`의 "fallback 수집→isEmpty→size>1 ambiguous throw" 2쌍(`:80-100`↔`:107-124`, `:134-150`↔`:187-200`) → `resolveSingle` 헬퍼.
- [ ] **C10. http-client 헤더 조회 불필요 스캔 + codec 모듈 내부 중복 + monitoring teardown** (없음/code-motion)
  - http-client `ResponseBodyReader.java:101-108 findHeader`+`stripEncodingHeaders:112-120`가 `equalsIgnoreCase` 선형 스캔인데 `collectHeaders:93-99`가 이미 소문자화 → 직접 조회(dotnet C16 잔재 1건). codec `MAPPER`/`encodeBytes`/`valueTypeName`가 각 모듈 내 stream-codec↔message-serializer에 복붙(msgpack `ZLinkMessagePackStreamCodec:18-22,57-78` ≡ `ZLinkMessagePackMessageSerializer:16-20,50-71`, protobuf 동형) → **모듈 내 package-private 헬퍼**(cross-module 공유 금지). spring `ZLinkMonitoringLifecycle` `stop`(`:98-119`)≡`stopAfterFailedStart`(`:187-201`) → `teardown()` 헬퍼.

- [ ] **C11. (R2) channel 수신 루프의 `Thread.sleep` no-data pause** (벤치, POSD red-flag)
  - `channels/ZLinkChannelRuntime.java:1834` `Thread.sleep(delayMillis)`(`pauseAfterNoData`) — no-data 시 `receiveExecutor` 스레드를 **블로킹**. runtime class가 스레드 타이밍 정책을 직접 소유(정보 은닉 붕괴) + 블로킹이 dispatch 지연에 영향. scheduled executor 기반 재개로 대체하거나 dispatch loop 내부 정책으로 숨겨 runtime class 밖으로. §D2 `ChannelReceiveLoops` 추출과 동반.

**C 착수 순서:** C0(레이어링 위반 — connector 독립성 복원, 최우선) → C1(spec 게이트, C0 후) → C3/C8/C9/C10(code-motion, 표면 축소) → C2/C4/C6/C7/C11(hot, 벤치 필수) → C5.

---

## D. God-file 분해 (POSD/DDD)

공개 계약 + 각 모듈 test 통과를 유지한 채 분해(java는 nested class를 top-level package-private 파일로 승격). hot 경로 dispatch 클러스터만 벤치.

- [ ] **D1. (P0) `spots/ZLinkSpotRuntime.java` (6236줄, 21 nested class, 코드베이스 최대)** — CRUD + 핸들러 스캔 + 두 dispatch 상태기계 + outbound + context + timer + location claim
  - **god-class `EntrySpotActivation`(2004-2846, ~843줄) + `SpotActivation`(5234-6236, ~1002줄):** §C2로 공유 `ZLinkSpotDispatchPipeline` + 공통 base activation 추출(최우선, **벤치**).
  - **Outbound 클러스터(3428-4530, ~1100줄):** `DefaultSpotOutbound`(3428)/`AmbientSpotOutbound`(4007)/`DefaultSpotPublisherClient`(4040) + send·request·publish call 8클래스 → `spot-outbound` 파일(**벤치** per-reply, C8 보일러 동반).
  - **Context/Timer:** `DefaultEntrySpotContext`(1798)/`EntrySpotTimerSurface`(1953)/`DefaultSpotContext`(2858-3427, `ManagedTimer` 3050) → `spot-context` + `spot-timer`.
  - **핸들러 스캔 정적(452-564 + `resolveActorPacketHandler` 1635 + `ScannedTimerRegistrar` 4696):** → `spot-handler-catalog`(리플렉션 스캔 idiomatic 유지).
  - **Spot CRUD + location claim(575-772, `activateAsync` 1085, `claim*`/`release*` 702/724/751/762):** → `spot-lifecycle`(dotnet E1 연결).
- [ ] **D2. (P1) `channels/ZLinkChannelRuntime.java` (3681줄)**
  - `ChannelSocketRegistry`(114-154,1192-1319), `ChannelRuntimeConfigurator`(279-497), `ChannelReceiveLoops`(1320/1691/1993, B3 착지), `ChannelDispatchers`(§C2 착지, 벤치), `ChannelHandlerInvokers`(2264-2955), `SpotRouteBridgeRawReplyCorrelator`(140-149,1509-1690), `SpotRouteBridgeDrainer`(1730-1828, 고정율 10ms drain `:1789` = §부록 D6 후보), `RouteSpotOutboundDispatcher`(747-1048, §C6 착지, 벤치), `ChannelClients`(2956-3678, call 7종).
- [ ] **D3. (R2/P1) `host/ZLinkFrameworkRuntime.java` (555줄)** — 생성자 `:72-308`(~236줄)이 8 서브시스템 조립 전량
  - wiring 번들화: Location(109-165)/Channel(166-184)/Spot(185-222)/Actor(223-280)/Stream(281-295)/AutoConnect(296-306) → 스테이지 팩토리(각 번들이 자기 `runtimeHandlers.add` 소유). `close()` 캐스케이드(`:472-520`, 6단 중첩 try/finally) → `Deque<Runnable>` LIFO 평탄화. **주의:** node/dotnet과 달리 Java의 bound-session relay는 host가 아니라 `actors/ZLinkNativeBoundSessionRuntime`·`streams/ZLinkStreamRuntime`에 있음(host는 배선만).
- [ ] **D4. `actors/ZLinkActorRuntime.java` (1903줄)** — `JoinSpotCall`(1542-1902, ~360줄, 최우선 → `ZLinkActorSpotJoiner`, 벤치) + `JoinEntrySpotCall`(1365-1540) + `DefaultActorContext`(1162-1363 → `ZLinkActorContextState`) + 위치알림(308-388 → `ZLinkActorLocationCoordinator`) + serial dispatch(820-869). `actors/ZLinkSessionActorsRuntime.java`(742줄): `BoundActor`(399-714, ~315줄 → `ZLinkBoundActor`, 벤치) + relay-header 레지스트리(42-107 → `ZLinkSessionRelayHeaders`) + 재시도/예외 정적(→ C4/C5).
- [ ] **D5. `streams/ZLinkStreamRuntime.java` (924줄)** — 실제 nested 심볼(→ 제안 추출 파일명): `DefaultSessionContext`(`:490-676` → `ZLinkStreamSessionContext.java`) / `SessionClient`(`:687`)+`SessionSendCall`(`:732`)+`SessionReplyCall`(`:809`)(→ `ZLinkStreamSessionCalls.java`) / payload 헬퍼 `encodePayload`(`:880`)·`decodePayload`(`:896`)·`EncodedStreamPayload`(`:914`)(→ `ZLinkStreamPayloadCodec.java`). `zlink-stream-connector`의 `DefaultZLinkStreamConnector.java`(1046줄) → `ConnectionLifecycle`(99-260,525-655) / `ReceiveDispatcher`(400-506) / calls(918-1030).
- [ ] **D6. `handlers/ZLinkHandlerScanner.java` (895줄)** — classpath 디스커버리(`scanPackage/Directory/Jar/loadClass` 327-390 → `ZLinkHandlerPackageScanner`) / 애노테이션 method(113-325,837-866 → `ZLinkAnnotationHandlerScanner`) / 인터페이스(414-813 → `ZLinkInterfaceHandlerScanner`, §C9 dedup 착지). `scan()`은 3 스캐너 조립 파사드로.
- [ ] **D7. `binding/ZLinkJavaBackendAdapterFactory.java` (795줄, code-motion, 저우선)** — 13 중첩 adapter를 개별 package-private 파일로 + 공유 헬퍼를 `JavaBackendCodec`/`JavaStreamFraming`로 hoist. **주의:** node R6(Proxy obscurity)와 달리 Java는 이미 타입안전 record adapter라 obscurity 낮음(우선순위 낮음). `JavaSpotNode.recvRoute`(`:542-549`) 수동 close 예외경로 누수 확인(저신뢰).
- [ ] **D-Cross. (R3, P1) actor↔spot 협업 경계 정리** (없음, back-door information leakage)
  - `actors/ZLinkActorRuntime.java`(actor ownership/생성/dispatch queue)와 `spots/ZLinkSpotRuntime.java`가 서로의 내부 순서를 알고 협업한다: SPOT의 actor-packet dispatch(`ZLinkSpotRuntime.java:1242-1436`)가 session bind + actor dispatch turn을 직접 호출하고, routed bound-session/actor-packet/actor-join(`ZLinkSpotRuntime.java:5959-6128`)도 SpotActivation 안에서 처리. 반대로 actor runtime이 Entry Spot route join·remote joined actor dispatch(`ZLinkActorRuntime.java:624-746`)·bind/native/routed session(`:929-1041`)을 소유. → session binding token/source node·session/no-bind 판정을 한 곳에 모으는 `ZLinkActorSessionCoordinator` 내부 객체 도입: SPOT은 "actor packet 처리" 요청만 넘기고 actor runtime은 생성·조회·dispatch queue invariant에 집중. public `ZLinkActorManager/Directory/Client` 유지. R1(§D1 spots) 이후 착수 — SPOT actor-packet dispatcher 분리 후 경계가 분명해짐. (§D1/§D4의 개별 추출과 조율.)
- [ ] **D8. (R5, P1) Location/Redis 구조 분해** — `locations/ZLinkLocationLifecycle.java`(290줄) 3책임(spot claim 41-74 / actor claim·takeover·상실 76-172,230 / actor-session route 173-216) → `ZLinkSpotLocationLifecycle`/`ZLinkActorOwnershipCoordinator`/`ZLinkActorSessionRouteLifecycle`(dotnet E1 동형; **Java 이점:** takeover가 intent 명시라 AsyncLocal scope 누출 없음).
  - `zlink-framework-locations-redis/.../ZLinkRedisLocationStore.java`(604줄) public facade 유지, 5객체로 분리(dotnet E2 동형). **⚠️ 범위 정확히**(메서드 경계 검증됨): `ZLinkRedisLocationKeys`(row/generation/index/owner/lease/stamp 키 naming `:554-598`), `ZLinkRedisLocationScriptsClient`(lease script `:204-272`[renew/remove/removeAll/listOwnerLeases] + write/remove script 호출 `:294-341` + result 변환 `toLeaseSnapshot`/`toWriteResult`/`propagateWriteFailure`/`unwrap` `:444-488`; **`getChangeStampAsync` `:275-279`는 script 아닌 plain `redis.get(stampKey)`라 별도 stamp accessor로**), `ZLinkRedisLocationRows`(`resolve`/`listRows`/`listPage`/`loadRows`/`toScannedPage`/`materialize` `:342-419`), `ZLinkRedisLocationFilters`(peer/spot/actor/route filter matching `:522-552`), `ZLinkRedisConnectionProvider`(`commands()`/`connection()` `:421-442` + `close()` future 버림 `:290-292` 함께 정리). row JSON wire format은 `ZLinkRedisLocationRowJson`이 계속 소유하되 **legacy actor-ref 호환 정책을 테스트로 고정**(row-key codec은 이미 별도 파일). **가드레일:** cross-language row format은 저장 계약이라 보존.
- [ ] **D9. (R4, P1) spring-boot-starter `ZLinkFrameworkCapabilityBeanRegistrar.java` (287줄)** — 4관심사 → `CapabilityBeanRegistrar`(capability delegate bean 등록 `:32-82`) + `ApplicationBeanRegistrar`(application type prototype + ctor collection dependency `:84-111`) + `SessionPacketHandlerDiscovery`(session-packet handler classpath 탐색 + Kotlin raw-type 이름 비교 `:113-194`, Kotlin 이름 비교를 이 객체에만 가둠) + package-private `ClasspathTypeScanner`(scanner 생성+class loading `:209-248`, `AssignableTypeFilter` 유무만 매개화). (`collectionElementType` `:196-207`은 `findCollectionDependencyImplementations`(`:101`)의 단일 소비 헬퍼 → `ApplicationBeanRegistrar`와 동거.) registrar는 `postProcessBeanFactory` orchestration만. http-client `ZLinkHttpRequestBuilder.java`(332줄) → `HttpRequestBodyEncoder`(form/multipart 295-331) + `HttpTargetBuilder`(query 251-265). testkit `FakeZLinkBackendAdapterFactory`(1243줄)는 테스트 인프라라 대상 아님.
- [ ] **D10. (R6, P2) Kotlin `ZLinkLocationExtensions.kt` (448줄) 파일 분리** — 파일 경계가 "location 관련 전부"라 store API 변경마다 suspend wrapper·Flow bridge·adapter base가 함께 바뀜. 파일만 책임별로 분할(로직 재구현 아님, 가드레일 부합): `ZLinkLocationStoreCoroutines.kt`(store suspend wrapper `:56-197`), `ZLinkLocationQueryFlows.kt`(page/runtime query Flow `:199-239`), `ZLinkPublisherFlowBridge.kt`(generic `Publisher<T>.asFlow()` `:241-271`), `ZLinkSuspendingLocationStore.kt`(suspending adapter base class `:273-448`). **⚠️ Kotlin binary/source compat:** top-level function JVM name이 파일명에 묶이므로 이동 시 `@file:JvmName` 또는 compatibility facade 선검토 필수. Kotlin source 사용자 import 경로 불변 목표. (그 외 kotlin 확장 파일은 얇은 이디엄 래퍼라 재편 불요.)

**D 착수 순서:** 무상태/무위험부터(D1 handler-catalog/context/timer/CRUD, D6 scanner, D4 context/relay-header, `replyActorDispatchError`/`reportDispatchError` 공유) → god-class dispatch 통합(D1 activation, D4 JoinSpotCall/BoundActor, D2 dispatchers)는 §C2/§C4 벤치와 동반.

---

## 부록 A. dotnet/node 대조 요약

Java에서 **부재/이미 해소**(재도입 금지): A-CH1(server-bundle bridge — `attachSpotRouteBridgeToServer:690` 실배선), A-SP1/2/3/4(peer disconnect·discovered-router·구독지표·descriptor 죽은멤버 — node 동형 부재), A-LO1(unused `_resolvers`/const-false param/discard ctor arg), A-ST1/A-ST2(죽은 stream encoder·단수 Lua), B1/B2(빈 metadata·이중 압축 — 대칭·1회), B5/B6(HandlerNotFound 예외 이중생성 — reply/report 분리), B8(AlreadyOwned — `ZLinkLocationWriteStatus`에 상태 자체 없음), C3(reporter 파이프라인마다 신규 — 서브시스템당 1개), C4(bridge near-dup — 단일 `dispatchSpotRouteBridgePacket`), C14(이중 message-flow 로깅 — 단일 tracer), C18(peer-weight 하드코딩 — native 직독), D1(host 이중 파사드 — host는 배선만), mesh-scan resolver 쌍둥이(단일화), R6(backend Proxy obscurity — 타입안전 record adapter).

Java에 **동형 존재** → 위 반영: C1(wire twin — 교차 spec/contracts-only 공유, dotnet/node와 동일 방식) / C2(dispatch 상태기계) / B4·B7(observed guard·http 빈바디) / C7(live-row·lease tracker) / dotnet E1(location lifecycle 3책임 → D8) / E2(redis 분해 → D8) / node C5(retry 스케줄러 → C4).

Java **고유**(dotnet/node에 없음): **C0(⚠️ stream-connector가 framework-core를 `api` 의존 — 언어 공통 "connector 독립" 불변식 위반, node/dotnet은 무의존)**, B1(stdout 디버그 계측 대량 상주), B2(no-op 검증 루프), B3(수신 루프 try/catch 비대칭), C9(Class/String 이디엄 오버로드 5세트), spring `ZLinkFrameworkCapabilityBeanRegistrar` 4관심사(→ D9).

## 부록 B. 진행 중 변경 주의 + 정리 후보

- **⚠️ address→ref 마이그레이션 진행 중(삭제 판정 금지):** 현재 worktree에 spot 주소 표현을 `*Address` → `*Ref`로 옮기는 변경이 진행 중이다([[project_spot_address_messaging]]). 삭제됨: `locations/ZLinkActorAddressResolver.java`, `locations/ZLinkSpotAddress(+Resolver).java`, `runtime/locations/ZLinkLocationSpotRemoteAddressResolver.java`, `spots/ZLinkSpotRemoteAddress(+Resolver).java`. 신규: `runtime/locations/ZLinkLocationSpotRemoteRefResolver.java`, `spots/SpotRemoteRef.java`, `spots/SpotRemoteRefResolver.java`. **이 파일들을 dead/삭제 후보로 판정하지 말 것** — 진행 중인 public surface 변경이다.
- **정리 가능 산출물(추적 대상 아님, grep 노이즈):** `.gradle/`·`.kotlin/`·`.idea/`·`build/`·`bin/`(각 모듈·e2e·e2e-kotlin·samples), sample flow `logs/*.log`, 그리고 **untracked JVM crash 로그**(`e2e-kotlin/{RegistryMessaging,SpotService}/hs_err_pid*.log`, `e2e/{RegistryMessaging,SpotService,YieldDispatch}/hs_err_pid*.log`). 소스 아님, 리뷰/grep 흐림 방지용으로 로컬 삭제 가능(gradle이 재생성).

## 부록 C. 방법론 / 검증 게이트

- 8-에이전트 read-only 병렬 리뷰(spots/channels/actors/streams+connector/host+backend/locations+redis/handlers+config/satellites+kotlin) + 메인 루프 grep 검증.
- dead 판정: `src/main` + 전 `src/test`(+contractTest/integrationTest/fakeBackendTest) + `e2e`/`e2e-kotlin`/`samples`/`zlink-framework-kotlin` grep(`build`/`bin`/`out` 제외). 동명 메서드는 소유 클래스별 분리 확인.
- **⚠️ 동시 세션 주의:** 병렬 `kairos-code-dev` 세션이 파일을 동시 수정/revert한 이력. 착수 전 `git fetch`+동기화, 검증 즉시 커밋+푸시.

Gradle은 shared output/runner 환경 때문에 **순차 실행 권장**(`--no-daemon`):

```bash
cd framework/languages/java
./gradlew --no-daemon :zlink-framework-core:test
./gradlew --no-daemon :zlink-framework-core:contractTest
./gradlew --no-daemon :zlink-framework-core:integrationTest
./gradlew --no-daemon :zlink-framework-kotlin:test
./gradlew --no-daemon :zlink-framework-spring-boot-starter:test
./gradlew --no-daemon :zlink-framework-locations-redis:test
./gradlew --no-daemon :zlink-stream-connector:test
./gradlew --no-daemon :zlink-framework-testkit:contractTest :zlink-framework-testkit:fakeBackendTest
./gradlew --no-daemon :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test
./e2e/run_e2e_all.sh && ./e2e-kotlin/run_e2e_all.sh && ./samples/run_samples.sh
```

R1/R2/D1/D2/C6 이후 특히: `SpotService`/`YieldDispatch`/`ToActorMessaging`(java+kotlin), route mesh 포함 `RegistryMessaging`. hot(벤치) 항목은 baseline vs patched 실측 무회귀 후에만 커밋. **검증 중 public contract 차이가 드러나면 다른 언어/E2E 요구만 근거로 Java/Kotlin public API를 바로 추가하지 말고 공통 spec/guide 또는 draft에 설계 후보로 분리**([[CLAUDE.md]]).
