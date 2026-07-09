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

> 2026-07-09 부분 완료: 현재 트리에서 다시 grep한 뒤 source-only dead 항목을 정리했다.
> 제거 범위는 `routerSpotNodeNames`, `ZLinkBackendAutoConnectType`, bound-session 3파일의 미사용
> `turn` 플러밍, actor runtime의 무참조 helper/overload, channel `copyMessageBytes`,
> handler scanner의 무참조 private helper, stream header codec의 무참조 overload, unused import다.
> public 계약 후보(`ManualEndpointListBuilder`)와 test-only 진단 메서드·가시성 축소 후보는 삭제하지 않았다.

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

- [x] **B1. ⭐stdout 디버그 계측 소거** (없음, 가장 큰 실효 정리) — 두 종류를 구분:
  - **(P0, 무조건 출력) `[boot]` 기동 로깅:** `boot()` 정의가 게이트 없는 `System.out.println`이고 무조건 실행된다. `host/ZLinkFrameworkRuntime.java:350`(정의) + 생성자 **26회 호출**, `spots/ZLinkSpotRuntime.java:929`, `locations/ZLinkLocationAutoConnectHost.java:293`, `locations/ZLinkAutoConnectReconciler.java:224`(+tick/claim 경로), spring `ZLinkFrameworkLifecycle.java:238-240`(3회). runtime 생성/기동 시마다 stdout 오염 → 최우선 제거.
  - **(opt-in, `STREAM_TRACE` 게이트) `[zlink-java-stream-trace]` 릴레이 트레이싱:** 전부 `STREAM_TRACE` 상수 게이트라 기본 무비용(오탐 정정 — core도 무조건 아님): `actors/ZLinkSessionActorsRuntime.java:43,383-384`·`streams/ZLinkStreamRuntime.java:61,920-921`·`channels/ZLinkChannelRuntime.java:109,1051-1052`·`spots/ZLinkSpotRuntime.java:148,4384/4622/4639`·`zlink-stream-connector/.../DefaultZLinkStreamConnector.java:1041-1043`. sweep에 포함하되 P0 아님(플래그 off 시 무비용).
  - 출처 commit `890cda7f6 "기동 순서 감사"` — 감사용 임시 계측 미소거([[project_unidir_auto_connect]] "java 기동 레이스 감사 잔여"). 정식 진단은 `ZLinkMessageFlowTracer`(JUL/파일 sink, 모드 게이트)가 담당하므로 `[boot]`는 제거, `[zlink-java-stream-trace]`는 제거 또는 게이트된 `LOGGER.log(Level.FINE,…)`로 라우팅.
- [x] **B2. `requireChannelHandlerShape` no-op 검증 루프** (없음, 검증 게이트 결함)
  - `handlers/ZLinkHandlerScanner.java:847-852` — 파라미터 1..N 순회하나 `if(context/CancellationToken) continue;`만 있고 **else-throw 없음** → 채널 핸들러의 부적격 파라미터가 조용히 통과. `contextType` 파라미터가 이 죽은 비교에서만 읽힘. 형제 `requireActorPacketHandlerShape`(`:306`)/`requireSpotMethodShape`(`:269`)는 정상 throw → 비대칭. else-throw 추가 또는 루프+파라미터 제거. dotnet A-LO1(const-false validator)의 Java 대응.
- [x] **B3. client-server/subscribe 수신 루프 try/catch 부재 → 프레임 하나로 수신 정지** (없음, DoS성)
  - `channels/ZLinkChannelRuntime.java:1320-1337 startRequestLoop` / `:1993-2006 startSubscribeLoop` — `while(running)` 안에서 dispatch를 **try/catch 없이** 호출. 반면 `startRouteLoop`(`:1691-1728`)은 `catch(RuntimeException)` report-continue. 동기 throw 지점(`replyErrorAndReport`→native, error-sink throw, `dispatchPublish`(`:2010`)가 probe 없이 `parsePacket`→빈 parts IOOBE) 발생 시 `receiveExecutor` 스레드 종료 → 채널 수신 영구 정지. node B1과 동형 부류(원인은 seq-null이 아니라 루프 보호 비대칭). route 루프와 동일하게 catch-report-continue 추가.
- [x] **B4. observed-generation guard가 2개 독립 인스턴스로 분리됨** (벤치, correctness 경계)
  - `locations/ZLinkLocationRuntimeQueryService.java:38`와 `ZLinkStoreLocationResolvers.java:29`가 각각 `new ZLinkObservedLocationGenerations()` → query 경로(list*)와 resolver 경로(resolveSpot/Actor/Route)가 서로 다른 high-water mark. 한 표면이 stale로 거부한 generation을 다른 표면이 accept 가능(교차-표면 불변식 무력). host(`ZLinkFrameworkRuntime.java:126,131-134`)에서 단일 생성해 주입. dotnet D5 동형. (§C7 lease tracker 중복과 동일 배선 지점에서 함께 해소.)
- [x] **B5. actor reply metadata/compression 유실 + 채널/spot outbound metadata no-op** (벤치/draft, silent no-op)
  - `actors/ZLinkSessionActorsRuntime.java:509-516 replyLocal`이 응답 헤더 metadata에 `Map.of()`·압축 없음 하드코딩. 또한 채널/spot outbound call의 `metadata(key,value)`가 전부 `return this;` no-op(`channels:2983,3028,3085,…`, `spots:3563,3632,3728,3891,4093,4148,4222,4500`). node B4 동형. **단 Java엔 `ZLinkSpotActorReplyOptions` 같은 공개 옵션 계약이 부재** → "무시되는 옵션"이 아니라 "표면 부재". 공통 spec에 채널/actor metadata 지원 여부 확인 후 배선 또는 draft로 분리(control-plane JSON framing 불변식 충돌 여부 점검).
- [x] **B6. `ZLinkActorClientRuntime.decodeReply` 프레임 프리픽스 인라인 재구현** (벤치, C9와 결합)
  - `actors/ZLinkActorClientRuntime.java:314-334`가 6B prefix(2B header-len + 4B payload-len) + slice offset을 손코딩. `streams/ZLinkStreamFrameCodec.java`엔 `encode`만 있고 `decode`/`tryDecode` 부재 → wire 레이아웃 변경 시 client 조용히 깨짐. `ZLinkStreamFrameCodec.tryDecode` 추가 후 호출(§C3와 동반).
- [x] **B7. http-client 2xx 빈 바디에서 `submit<T>` decode 실패** (없음, 저신뢰)
  - `zlink-http-client/.../ZLinkHttpRequestBuilder.java:209-221` — 204/304/빈 200에서 `MAPPER.readValue("", type)`가 "No content" throw → 성공 응답이 `HTTP response body decode failed`로 뒤집힘. node B8 동형. C++ `submit<T>` 계약(빈 바디 null 허용 여부) 대조 후 short-circuit.
- [x] **B8. route HandlerNotFound reply framework-error 마커 미부착** (없음, 저신뢰)
  - `spots/ZLinkSpotRuntime.java:2109-2111,5381-5383` / `channels`(route) — route 오류를 평문 문자열로 reply(actor 경로 `:2576,5776`은 예외 wrap). 클라이언트가 정상 payload로 오인 여지. 오류 프레이밍 규약 spec 확인 후 정렬.

- [x] **B9. routed bound-session `SendCall.submit()`가 send 실패를 삼킴** (없음, correctness)
  - `actors/ZLinkRoutedBoundSessionRuntime.java:216-225` `SendCall.submit()`이 `sendFrame(...)`(return type `CompletionStage<Void>`, `:113`)를 호출하고 **반환 stage를 버린 뒤 무조건 `ZLinkSubmitStage.completed()` 반환**(`:225`). `sendFrame`은 not-ready 시 `failedFuture`(`:135-139`)·route-channel 시 transport stage(`:124-128`)로 실패를 전달하는데 이를 무시 → 라우팅 bound-session send가 실패해도 caller는 성공으로 관측. 대조: native(`ZLinkNativeBoundSessionRuntime.java:197`)·local(`ZLinkBoundSessionRuntime.java:373`)은 `ZLinkSubmitStage.from(...)`로 stage를 감싼다. `submit()`이 `ZLinkSubmitStage.from(sendFrame(...))`로 stage 전파하도록 수정(§C8 SendCall 3벌 통합과 함께 처리 가능). (부수 점검: try-with-resources `frame` close 시점 vs 비동기 send 완료 경합.)

**B 착수 순서:** B1(즉효, 위험 없음) → B3(수신 정지) → B9(routed send 실패 삼킴) → B4(guard 단일화) → B6(client decode) → B2 → B5/B7/B8(계약/spec 확인 동반).

---

## C. 구조 통합 — 지식 중복 소거

- [x] **C0. ⭐(P0, 레이어링 위반) `zlink-stream-connector`가 `zlink-framework-core`를 의존** (없음, 아키텍처 불변식 위반)
  - `zlink-stream-connector/build.gradle.kts:9 api(project(":zlink-framework-core"))` — **stream-connector는 framework 무의존 독립 패키지여야 한다는 언어 공통 불변식을 위반**한다. 확인: node connector(`node/packages/stream-connector`)는 `@zlink-systems/framework` import 0, dotnet `Systems.Zlink.Stream.Connector`는 `K4os.Compression.LZ4`(자체 lib)만 참조 — **둘 다 framework 무의존**. Java만 core 전체(370파일 30.7k줄)를 끌어온다.
  - 게다가 `api`(implementation 아님)라 **connector를 쓰는 모든 소비자에게 framework-core 전체가 transitive로 노출**된다(컴파일 classpath 오염). 이걸 정당화하는 실사용은 **단 한 클래스**: `ZLinkStreamLz4Pickler.java:3`이 core **내부** `runtime.streams.ZLinkStreamPayloadCompression`(public 계약 아님, `runtime.*` 구현)에 LZ4 pickle 3메서드를 위임(`pickle`/`unpickle`/`unpickle(max)`). 즉 20줄짜리 델리게이트 래퍼 하나 때문에 전체 core 의존이 걸려 있다. (앞선 리뷰가 이 델리게이션을 "LZ4 twin 이미 소거"라 **긍정적으로 오판**했는데, 실제로는 이것이 위반의 벡터다.)
  - **수정 방향(dotnet/node와 동일하게 독립성 복원):** connector가 자체 LZ4 pickle을 소유(node는 자체 구현, dotnet은 K4os 사용) — 또는 LZ4 pickle 바이트 규약만 담은 **framework 런타임 무의존 저수준 공유 모듈**(contracts-only, `runtime.*` 아님)로 추출해 core와 connector 양쪽이 참조. 그 후 **`api(project(":zlink-framework-core"))` 제거**. `lz4-java` 의존은 유지(connector 자체 압축 lib). 이 위반을 먼저 없애야 C1 wire-format 중복도 "core 의존을 이용한 코드 공유"가 아니라 언어 공통 방식(교차 spec 테스트/contracts-only 공유)으로 올바르게 풀린다.
- [x] **C1. ⭐stream wire 포맷이 core `runtime/streams`와 `zlink-stream-connector`에 중복** (없음, 최대 유지보수 impact)
  - twin: 6B prefix(`ZLinkStreamFrameCodec.java:30-35` ↔ connector `ZLinkStreamWireProtocol.java:149-155`), 헤더(`ZLinkStreamHeaderCodec.java:123-186/26-103` ↔ `:34-140`), TLV metadata(`ZLinkStreamHeaderCodec.java:216-286` ↔ `:197-257`), kind/codec/flag enum(`ZLinkStreamHeaderCodec.java:15-21` ↔ `:11-28`). LZ4 pickle은 현재 C0의 위반 경로로 "공유"돼 있으나, C0 수정(독립성 복원) 후에는 나머지 wire 포맷과 동일하게 다뤄진다.
  - **검증 규칙 divergence(비대칭 위험):** connector `validateHeader`(`ZLinkStreamWireProtocol.java:259-297`)는 강한 의미 검증(send≠reqSeq, request/response reqSeq 필수, error=JSON codec, control 제약)인데 core `decodeOrPlain`(`ZLinkStreamHeaderCodec.java:26-103`)은 control 제약만 + **plain-string 폴백**(unknown kind→UTF-8 헤더). → 한쪽이 받는 프레임을 다른 쪽이 거부 가능. (dup-metadata-key는 양쪽 fail-fast로 일치 — node와 다름.)
  - **권장(dotnet/node C1과 동일 — 코드 병합/의존 아님, 가드레일=connector 독립 유지):** (최소) core↔connector 교차 왕복 spec 테스트 추가(현재 connector `ZLinkStreamWireProtocolTest`/`JavaNodeStreamInteropTest`만 있고 core는 `ZLinkStreamHeaderCodecTest`뿐, 교차 없음) + 검증 규칙 정렬. (이상) 바이트 레이아웃 상수(KIND/CODEC/FLAG/prefix)를 **framework 런타임 무의존 contracts-only 저수준 모듈**로 승격해 양쪽이 참조(C0의 LZ4 공유 모듈과 같은 계층). **core 의존을 이용한 공유는 금지**(그것이 C0 위반).
  - 2026-07-10 완료: connector production 의존은 framework-core로 되돌리지 않고,
    `ZLinkStreamCoreWireInteropTest`로 core↔connector header/frame 왕복을 고정했다. core
    `ZLinkStreamHeaderCodecTest`도 connector golden vector를 byte-exact로 검증한다.
- [ ] **C2. ⭐Entry vs User activation dispatch 상태기계 2벌 near-verbatim** (벤치, per-message/dispatch)
  - `spots/ZLinkSpotRuntime.java` — `EntrySpotActivation`(동기 void)과 `SpotActivation`(CompletionStage async)이 dispatch 골격을 통째 복제: route(`:2069-2230` ≈ `:5340-5500`), actor-message(`:2437-2614` ≈ `:5656-5814`), subscription(`:2301-2368` ≈ `:5513-5582`), actor-lifecycle(`:2390-2436` ≈ `:5607-5655`), routed relay(`:2232-2289` ≈ `:5959-6016`). `replyActorDispatchError`(`:2676-2718` == `:5833-5877`)는 완전 동일. dotnet C2/node C2 동형. kind/surface/reply-strategy 매개화한 `ZLinkSpotDispatchPipeline` + 공통 base activation으로 추출(actor 해결원·spotRid원·동기/async 어댑터만 주입). `reportDispatchError` 12-인자 positional 21회 호출(정의 `:4414`)도 `ZLinkDispatchFailure` 빌더로.
  - channels도 동형(하위): `dispatchSend`(`:2066`)/`dispatchRouteSend`(`:2114`)/`dispatchPublish`(`:2008`)/`dispatchRequest`(`:1397`)/`dispatchRouteRequest`(`:1927`) 5벌 + `invoke*Handler` 3+2벌(`:2292-2569`). 같은 dispatch core로.
- [x] **C3. stream frame decode 인라인 + response 헤더 생성 반복** (벤치, per-reply)
  - `ZLinkStreamFrameCodec`에 `tryDecode` 추가(§B6) → `ZLinkActorClientRuntime:314-334` 손코딩 제거. "request→response 헤더(correlationId echo)" 생성 5곳(`streams/ZLinkStreamRuntime.java:850-858,638-645`, `actors/ZLinkSessionActorsRuntime.java:509-516`, `actors/ActorPacketFrames.java:34-40,46-53`) → `ZLinkStreamHeader.createResponse(requestHeader,codec,flags,metadata)` 팩토리. dotnet C9 동형.
- [x] **C4. ⭐actor deadline-retry 스케줄러 관용구 ~10벌 + 4 executor** (벤치, per-relay/send)
  - `Attempt implements Runnable` + `nanoTime()≥deadline` + `EXECUTOR.schedule(this,10,MS)` 패턴이 `ZLinkSessionActorsRuntime.java:352,520,555,579,645`·`ZLinkBoundSessionRuntime.java:116,144,193,390`·`ZLinkNativeBoundSessionRuntime.java:220`·`ZLinkActorClientRuntime.java:150,186`에 반복 + daemon single-thread `ScheduledExecutorService`가 파일마다 **4개 별도 생성**. 공유 `ZLinkActorRelayRetry`(deadline + attempt supplier + 단일 executor)로 수렴. node C5 확대형.
  - 2026-07-10 완료: actor retry 스케줄링을 `ZLinkActorRetryScheduler` 단일 executor로 모았다. route retry,
    relay accepted retry, native bound-session retry, stream bind retry가 scheduler 메서드를 사용한다.
    `ZLinkActorClientRuntime`의 route deadline recursion, `ZLinkBoundSessionRuntime.bindActorWithRetry`,
    `ZLinkBoundActor.ensureNativeBinding`, `ZLinkBoundActor.relayWithRetry`의 직접 deadline loop를 제거했다.
- [x] **C5. actor 예외 분류 헬퍼 verbatim 중복** (없음)
  - `isRetryableSubmitResult` 3벌(`ZLinkSessionActorsRuntime.java:639`/`ZLinkBoundSessionRuntime.java:434`/`ZLinkNativeBoundSessionRuntime.java:265`), `isAlreadyBound` 2벌(`:716`/`:245`), `isRetriableBindFailure` 2벌(`:724`/`:253` 후자가 상위집합), `findRequestException`/`findConfigException` 2벌 → `ZLinkActorSubmitFaults` util. C4와 같은 파일군.
- [x] **C6. (route-to-SPOT 전략) channels dispatch 대상 선택 + 전송 매트릭스 중복** (벤치/code-motion)
  - `channels/ZLinkChannelRuntime.java` — `sendToSpotViaRouterChannel`(`:747-786`)와 `requestToSpotViaRouterChannel`(`:836-946`)의 채널 선택 prefix가 verbatim(`:752-765`≡`:846-866`) + send/request × bridge/spot-router-node 4구현이 `copyMessages→submit→complete→finally close` 골격 공유. `ZLinkSpotRouteTarget`(bridge|spotRouterNode) 해석 헬퍼 + 완료 규칙 집약(codex R4 = route dispatch 전략 분리). raw bridge reply 큐(`SpotRouteBridgeRawReplyCorrelator`, `:1509-1690`)는 bridge 전용 객체 소유.
  - 2026-07-10 완료: spot-router-node send/request submit·reply·close 규칙은
    `ZLinkSpotRouterNodeDispatcher`로, route-bridge send/request 제출 규칙은
    `ZLinkSpotRouteBridgeDispatcher`로, route-bridge raw reply correlation은
    `ZLinkSpotRouteBridgeRawReplies`로 분리했다.
- [x] **C7. Location live-row 필터 + lease tracker 중복** (벤치, per-read)
  - live-row 필터(observed accept → owner lease live) 재구현: `ZLinkLocationRuntimeQueryService.filterLive`(`:247-266`) vs `ZLinkStoreLocationResolvers.filterLivePeers`(`:70-83`)+`resolveLiveAsync`(`:85-96`) → `ZLinkLiveRowFilter` 추출. lease tracker 2 독립 인스턴스(`QueryService:47-49` + `StoreLocationResolvers:36-38` 각 `new ZLinkOwnerLeaseTracker` → 폴링 2배) → host에서 단일 생성 후 공유(B4 observed guard와 동일 지점). node C8/dotnet C12 동형. **주의:** mesh-scan resolver 쌍둥이는 Java 이미 단일화(부재).
- [x] **C8. Bound-session SendCall record 3벌 + join 결과 디코드 중복** (code-motion)
  - `ZLinkBoundSessionSendCall` 구현 3벌 near-verbatim(`ZLinkBoundSessionRuntime$SendCall:315`/`Native:129`/`Routed:146`, header 빌드+metadata 누산+encode+sendWithRetry) → 공통 base(A1 dead `turn` 제거 후 shape 근접). `ZLinkActorRuntime` `JoinEntrySpotCall`(`:1415-1437`) vs `JoinSpotCall.decodeJoinResult`(`:1683-1761`) 조인 결과 디코드 중복 → `decodeJoinResult` 공용 헬퍼. `ZLinkActorSpotRoutePackets.encodeJoinRequest`(`:23-39`)는 join payload 손빌드(단, `encodeActorRef`(`:102`)는 `createBoundSessionSendParts`(`:66`)·`createActorPacketParts`(`:76`)에서 **live** — "미사용" 아님, 삭제 금지). join payload 자체는 이미 이 파일로 중앙화됨(node C7 3벌 손빌드 상황 부재).
- [x] **C9. handler scanner Class/String(kotlin) 오버로드 5세트 + 술어 중복** (code-motion)
  - `ZLinkHandlerScanner`의 `addInterfaceHandler`(`:415-465`)/`addSpotPacketInterfaceHandler`(`:501-561`)/`addSpotSubscriptionInterfaceHandler`(`:563-622`)/`addSpotTimerInterfaceHandler`(`:624-697`)/`addSpotActorPacketInterfaceHandler`(`:753-813`)가 `findInterface` 인자 타입(Class vs String)만 다르고 본문 verbatim(~200줄). `ZLinkGenericTypeResolver`의 `findInterface`(`:36-68`)/`matchType`(`:70-106`)도 동형 쌍 → Predicate 주입 1벌. `ZLinkCodecRegistration`의 "fallback 수집→isEmpty→size>1 ambiguous throw" 2쌍(`:80-100`↔`:107-124`, `:134-150`↔`:187-200`) → `resolveSingle` 헬퍼.
- [x] **C10. http-client 헤더 조회 불필요 스캔 + codec 모듈 내부 중복 + monitoring teardown** (없음/code-motion)
  - http-client `ResponseBodyReader.java:101-108 findHeader`+`stripEncodingHeaders:112-120`가 `equalsIgnoreCase` 선형 스캔인데 `collectHeaders:93-99`가 이미 소문자화 → 직접 조회(dotnet C16 잔재 1건). codec `MAPPER`/`encodeBytes`/`valueTypeName`가 각 모듈 내 stream-codec↔message-serializer에 복붙(msgpack `ZLinkMessagePackStreamCodec:18-22,57-78` ≡ `ZLinkMessagePackMessageSerializer:16-20,50-71`, protobuf 동형) → **모듈 내 package-private 헬퍼**(cross-module 공유 금지). spring `ZLinkMonitoringLifecycle` `stop`(`:98-119`)≡`stopAfterFailedStart`(`:187-201`) → `teardown()` 헬퍼.

- [x] **C11. (R2) channel 수신 루프의 `Thread.sleep` no-data pause** (벤치, POSD red-flag)
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
- [x] **D3. (R2/P1) `host/ZLinkFrameworkRuntime.java` (555줄)** — 생성자 `:72-308`(~236줄)이 8 서브시스템 조립 전량
  - wiring 번들화: Location(109-165)/Channel(166-184)/Spot(185-222)/Actor(223-280)/Stream(281-295)/AutoConnect(296-306) → 스테이지 팩토리(각 번들이 자기 `runtimeHandlers.add` 소유). `close()` 캐스케이드(`:472-520`, 6단 중첩 try/finally) → `Deque<Runnable>` LIFO 평탄화. **주의:** node/dotnet과 달리 Java의 bound-session relay는 host가 아니라 `actors/ZLinkNativeBoundSessionRuntime`·`streams/ZLinkStreamRuntime`에 있음(host는 배선만).
- [x] **D4. `actors/ZLinkActorRuntime.java`** — 완료: `JoinSpotCall` → `ZLinkActorSpotJoinCall`,
  `JoinEntrySpotCall` → `ZLinkActorEntrySpotJoinCall`,
  위치알림 → `ZLinkActorLocationCoordinator`, serial dispatch → `ZLinkActorDispatchSerials`,
  actor context state → `ZLinkActorContextState`.
  `actors/ZLinkSessionActorsRuntime.java`: 완료: `BoundActor` → `ZLinkBoundActor`,
  relay-header 레지스트리 → `ZLinkSessionRelayHeaders`. 재시도/예외 정적은 C4/C5에서 처리했다.
  - 2026-07-10 부분 완료: actor location claim, actor-ref 저장, actor find fallback, joined/left/moved 알림,
    bound-session route bind/remove, release 처리를 `ZLinkActorLocationCoordinator`로 분리했다.
  - 2026-07-10 부분 완료: `ZLinkSessionActorsRuntime`의 relay-header ThreadLocal/dispatch weak-map 저장소를
    `ZLinkSessionRelayHeaders`로 분리했다. 기존 `enterRelayDispatch`/`exitRelayDispatch` public static wrapper는
    호환을 위해 유지했다. `BoundActor` 분리는 별도 항목에서 완료했고, `JoinSpotCall`은 별도 항목에서
    완료했다. `JoinEntrySpotCall`도 별도 항목에서 완료했다. 이 시점에는 `DefaultActorContext` 분리가
    남아 있어 D4 전체 체크를 유지하지 않았다.
  - 2026-07-10 부분 완료: `ZLinkActorRuntime`의 per-actor serial dispatch queue와 dispatch turn ThreadLocal을
    `ZLinkActorDispatchSerials`로 분리했다. actor 존재 여부와 actor map cleanup은 runtime이 계속 소유한다.
  - 2026-07-10 부분 완료: `ZLinkSessionActorsRuntime.BoundActor`를 `ZLinkBoundActor` package-private 클래스로
    분리했다. `JoinEntrySpotCall`은 별도 항목에서 완료했다. 이 시점에는 `DefaultActorContext` 분리가
    남아 있어 D4 전체 체크를 유지하지 않았다.
  - 2026-07-10 부분 완료: `ZLinkActorRuntime.JoinSpotCall`을 `ZLinkActorSpotJoinCall` package-private 클래스로
    분리했다. `JoinEntrySpotCall`은 별도 항목에서 완료했다. 이 시점에는 `DefaultActorContext` 분리가
    남아 있어 D4 전체 체크를 유지하지 않았다.
  - 2026-07-10 부분 완료: `ZLinkActorRuntime.JoinEntrySpotCall`을 `ZLinkActorEntrySpotJoinCall`
    package-private 클래스로 분리했다. 이 시점에는 `DefaultActorContext` 분리가 남아 있어 D4 전체
    체크를 유지하지 않았다.
  - 2026-07-10 완료: `DefaultActorContext`가 들고 있던 actor lifecycle mutable state를
    `ZLinkActorContextState` package-private 클래스로 분리했다. `DefaultActorContext`는 public
    `ZLinkActorContext` facade와 join call factory 배선만 맡는다.
- [x] **D5. `streams/ZLinkStreamRuntime.java` (924줄)** — 실제 nested 심볼(→ 제안 추출 파일명): `DefaultSessionContext`(`:490-676` → `ZLinkStreamSessionContext.java`) / `SessionClient`(`:687`)+`SessionSendCall`(`:732`)+`SessionReplyCall`(`:809`)(→ `ZLinkStreamSessionCalls.java`) / payload 헬퍼 `encodePayload`(`:880`)·`decodePayload`(`:896`)·`EncodedStreamPayload`(`:914`)(→ `ZLinkStreamPayloadCodec.java`). `zlink-stream-connector`의 `DefaultZLinkStreamConnector.java`(1046줄) → `ConnectionLifecycle`(99-260,525-655) / `ReceiveDispatcher`(400-506) / calls(918-1030).
  - 2026-07-10 완료: `ZLinkStreamRuntime` 내부 session context, session send/reply calls, payload codec 분리 완료. `DefaultZLinkStreamConnector`의 connection lifecycle, receive dispatcher, send/request calls, payload codec 분리 완료.
- [x] **D6. `handlers/ZLinkHandlerScanner.java` (895줄)** — classpath 디스커버리(`scanPackage/Directory/Jar/loadClass` 327-390 → `ZLinkHandlerPackageScanner`) / 애노테이션 method(113-325,837-866 → `ZLinkAnnotationHandlerScanner`) / 인터페이스(414-813 → `ZLinkInterfaceHandlerScanner`, §C9 dedup 착지). `scan()`은 3 스캐너 조립 파사드로.
- [x] **D7. `binding/ZLinkJavaBackendAdapterFactory.java` (795줄, code-motion, 저우선)** — 13 중첩 adapter를 개별 package-private 파일로 + 공유 헬퍼를 `JavaBackendCodec`/`JavaStreamFraming`로 hoist. **주의:** node R6(Proxy obscurity)와 달리 Java는 이미 타입안전 record adapter라 obscurity 낮음(우선순위 낮음). `JavaSpotNode.recvRoute`(`:542-549`) 수동 close 예외경로 누수 확인(저신뢰).
  - 2026-07-10 부분 완료: stream frame 조립을 `ZLinkJavaStreamFraming`으로 분리하고,
    `JavaContext`/`JavaSocketMonitor` wrapper를 package-private 파일로 옮겼다. socket/spot node adapter
    중첩 타입과 native submit/received 변환 헬퍼는 아직 남아 있으므로 D7 전체 체크는 유지하지 않는다.
  - 2026-07-10 부분 완료: channel socket wrapper(`Dealer`/`Router`/`Publisher`/`Subscriber`)를
    `ZLinkJavaDealerSocket`, `ZLinkJavaRouterSocket`, `ZLinkJavaPublisherSocket`, `ZLinkJavaSubscriberSocket`
    package-private 파일로 분리했다. send/request/reply/recv 공통 조립은 `ZLinkJavaSocketSupport`로,
    message part 복사는 `ZLinkJavaBackendCodec`으로 옮겼다. `JavaStreamSocket`, `JavaSpotNode`,
    `JavaSpotRouteBridge`, `JavaSpot`과 actor/spot native 변환 헬퍼는 아직 남아 있으므로 D7 전체 체크는
    유지하지 않는다.
  - 2026-07-10 부분 완료: spot route bridge wrapper와 spot socket wrapper를
    `ZLinkJavaSpotRouteBridge`, `ZLinkJavaSpot` package-private 파일로 분리했다. spot dispatch,
    actor received/join/lifecycle, actor ref 변환은 `ZLinkJavaSpotCodec`으로 옮겼다. `JavaStreamSocket`,
    `JavaSpotNode`, adapter shell, factory-level native actor 조립은 아직 남아 있으므로 D7 전체 체크는
    유지하지 않는다.
  - 2026-07-10 완료: stream socket wrapper를 `ZLinkJavaStreamSocket`, spot node wrapper를
    `ZLinkJavaSpotNode`, channel/spot/stream adapter shell을 `ZLinkJavaChannelBackendAdapter`,
    `ZLinkJavaSpotBackendAdapter`, `ZLinkJavaStreamBackendAdapter` package-private 파일로 분리했다.
    socket linger 설정 정책은 `ZLinkJavaSocketOptions`가 소유한다. `ZLinkJavaBackendAdapterFactory`는
    backend adapter factory 메서드와 monitoring adapter 생성만 남는다.
- [ ] **D-Cross. (R3, P1) actor↔spot 협업 경계 정리** (없음, back-door information leakage)
  - `actors/ZLinkActorRuntime.java`(actor ownership/생성/dispatch queue)와 `spots/ZLinkSpotRuntime.java`가 서로의 내부 순서를 알고 협업한다: SPOT의 actor-packet dispatch(`ZLinkSpotRuntime.java:1242-1436`)가 session bind + actor dispatch turn을 직접 호출하고, routed bound-session/actor-packet/actor-join(`ZLinkSpotRuntime.java:5959-6128`)도 SpotActivation 안에서 처리. 반대로 actor runtime이 Entry Spot route join·remote joined actor dispatch(`ZLinkActorRuntime.java:624-746`)·bind/native/routed session(`:929-1041`)을 소유. → session binding token/source node·session/no-bind 판정을 한 곳에 모으는 `ZLinkActorSessionCoordinator` 내부 객체 도입: SPOT은 "actor packet 처리" 요청만 넘기고 actor runtime은 생성·조회·dispatch queue invariant에 집중. public `ZLinkActorManager/Directory/Client` 유지. R1(§D1 spots) 이후 착수 — SPOT actor-packet dispatcher 분리 후 경계가 분명해짐. (§D1/§D4의 개별 추출과 조율.)
- [x] **D8. (R5, P1) Location/Redis 구조 분해** — `locations/ZLinkLocationLifecycle.java`(290줄) 3책임(spot claim 41-74 / actor claim·takeover·상실 76-172,230 / actor-session route 173-216) → `ZLinkSpotLocationLifecycle`/`ZLinkActorOwnershipCoordinator`/`ZLinkActorSessionRouteLifecycle`(dotnet E1 동형; **Java 이점:** takeover가 intent 명시라 AsyncLocal scope 누출 없음).
  - `zlink-framework-locations-redis/.../ZLinkRedisLocationStore.java`(604줄) public facade 유지, 5객체로 분리(dotnet E2 동형). **⚠️ 범위 정확히**(메서드 경계 검증됨): `ZLinkRedisLocationKeys`(row/generation/index/owner/lease/stamp 키 naming `:554-598`), `ZLinkRedisLocationScriptsClient`(lease script `:204-272`[renew/remove/removeAll/listOwnerLeases] + write/remove script 호출 `:294-341` + result 변환 `toLeaseSnapshot`/`toWriteResult`/`propagateWriteFailure`/`unwrap` `:444-488`; **`getChangeStampAsync` `:275-279`는 script 아닌 plain `redis.get(stampKey)`라 별도 stamp accessor로**), `ZLinkRedisLocationRows`(`resolve`/`listRows`/`listPage`/`loadRows`/`toScannedPage`/`materialize` `:342-419`), `ZLinkRedisLocationFilters`(peer/spot/actor/route filter matching `:522-552`), `ZLinkRedisConnectionProvider`(`commands()`/`connection()` `:421-442` + `close()` future 버림 `:290-292` 함께 정리). row JSON wire format은 `ZLinkRedisLocationRowJson`이 계속 소유하되 **legacy actor-ref 호환 정책을 테스트로 고정**(row-key codec은 이미 별도 파일). **가드레일:** cross-language row format은 저장 계약이라 보존.
- [x] **D9. (R4, P1) spring-boot-starter `ZLinkFrameworkCapabilityBeanRegistrar.java` (287줄)** — 4관심사 → `CapabilityBeanRegistrar`(capability delegate bean 등록 `:32-82`) + `ApplicationBeanRegistrar`(application type prototype + ctor collection dependency `:84-111`) + `SessionPacketHandlerDiscovery`(session-packet handler classpath 탐색 + Kotlin raw-type 이름 비교 `:113-194`, Kotlin 이름 비교를 이 객체에만 가둠) + package-private `ClasspathTypeScanner`(scanner 생성+class loading `:209-248`, `AssignableTypeFilter` 유무만 매개화). (`collectionElementType` `:196-207`은 `findCollectionDependencyImplementations`(`:101`)의 단일 소비 헬퍼 → `ApplicationBeanRegistrar`와 동거.) registrar는 `postProcessBeanFactory` orchestration만. http-client `ZLinkHttpRequestBuilder.java`(332줄) → `HttpRequestBodyEncoder`(form/multipart 295-331) + `HttpTargetBuilder`(query 251-265). testkit `FakeZLinkBackendAdapterFactory`(1243줄)는 테스트 인프라라 대상 아님.
- [x] **D10. (R6, P2) Kotlin `ZLinkLocationExtensions.kt` (448줄) 파일 분리** — 파일 경계가 "location 관련 전부"라 store API 변경마다 suspend wrapper·Flow bridge·adapter base가 함께 바뀜. 파일만 책임별로 분할(로직 재구현 아님, 가드레일 부합): `ZLinkLocationStoreCoroutines.kt`(store suspend wrapper `:56-197`), `ZLinkLocationQueryFlows.kt`(page/runtime query Flow `:199-239`), `ZLinkPublisherFlowBridge.kt`(generic `Publisher<T>.asFlow()` `:241-271`), `ZLinkSuspendingLocationStore.kt`(suspending adapter base class `:273-448`). **⚠️ Kotlin binary/source compat:** top-level function JVM name이 파일명에 묶이므로 이동 시 `@file:JvmName` 또는 compatibility facade 선검토 필수. Kotlin source 사용자 import 경로 불변 목표. (그 외 kotlin 확장 파일은 얇은 이디엄 래퍼라 재편 불요.)

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

## 부록 D. 2026-07-09 진행 로그

- 완료: A1 source-only dead 정리(부분), B1, B2, B3, B7, B9, C0, C10.
- C0 처리: `zlink-stream-connector`에서 `api(project(":zlink-framework-core"))`를 제거하고,
  connector가 필요한 Jackson/LZ4 의존과 LZ4 pickle 구현을 직접 소유하게 했다.
- 검증:
  - `./gradlew --no-daemon compileJava compileKotlin`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-core:contractTest :zlink-framework-core:integrationTest`
  - `./gradlew --no-daemon :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-framework-testkit:contractTest :zlink-framework-testkit:fakeBackendTest :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - e2e 실행은 하지 않고, `e2e/`와 `e2e-kotlin/`의 각 시나리오 Gradle `build`만 순차 실행했다.
  - `./samples/run_samples.sh`

## 부록 E. 2026-07-09 추가 진행 로그

- 완료: B7, C10.
- B7 처리: HTTP typed `submit(Class<T>)`가 2xx 빈 body에서 Jackson decode를 호출하지 않고
  `body == null`, `rawBody == ""`인 `HttpResponse<T>`를 반환하도록 했다. 204 회귀 테스트를 추가했다.
- C10 처리: HTTP response header 조회는 소문자 수집 map을 직접 조회하도록 바꾸고,
  MessagePack/Protobuf codec 모듈 내부의 mapper/encode/decode helper를 package-private helper로 모았다.
  Spring monitoring lifecycle의 실패 시작 정리와 정상 stop 정리는 `teardown()`으로 합쳤다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test :zlink-framework-spring-boot-starter:test`
  - `./gradlew --no-daemon :zlink-http-client:test`

## 부록 F. 2026-07-09 위치 조회/해석 공유 상태 정리

- 완료: B4, C7.
- 처리: `ZLinkLiveLocationRows`를 추가해 live-row 필터, owner lease 확인,
  observed-generation high-water mark를 한 객체에 모았다. `ZLinkFrameworkRuntime`은 이 객체를 한 번
  만들고 `ZLinkLocationRuntimeQueryService`와 `ZLinkStoreLocationResolvers`에 같이 전달한다.
  따라서 list/query 표면과 resolver 표면이 같은 stale-generation guard와 lease snapshot을 본다.
- 회귀 테스트: `ZLinkStoreLocationResolversTest.queryAndResolverShareObservedGenerationGuard`를 추가해
  한 표면에서 관측한 높은 generation이 resolver 표면에도 적용되는지 확인했다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolversTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-core:contractTest :zlink-framework-core:integrationTest`

## 부록 G. 2026-07-09 stream frame/header 중복 정리

- 완료: B6, C3.
- 처리: `ZLinkStreamFrameCodec.tryDecode`와 `DecodedFrame`을 추가해 actor client의 6바이트
  frame prefix 손해석을 제거했다. `ZLinkStreamHeader.createResponse`와
  `createErrorResponse`를 추가해 stream session reply, async error reply, local actor reply,
  actor-packet reply/error가 같은 request-sequence/correlation echo 규칙을 쓰게 했다.
- 회귀 테스트: `ZLinkStreamFrameCodecTest`를 추가하고 `ZLinkStreamHeaderCodecTest`에 response factory
  테스트를 추가했다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodecTest --tests systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodecTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntimeTest --tests systems.zlink.framework.runtime.spots.ZLinkSpotRuntimeTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-core:contractTest :zlink-framework-core:integrationTest`

## 부록 H. 리팩토링 산출물 POSD 자체 점검

- 새 리팩토링 산출물은 완료 처리 전에 얕은 모듈, 단순 pass-through, 정보 누출, public 표면 오염 여부를
  다시 확인한다.
- 이번 변경의 점검 결과:
  - `ZLinkLiveLocationRows`는 observed-generation high-water mark와 owner lease snapshot을 함께 소유해
    query/resolver 양쪽의 live-row 판정을 한 곳에 숨긴다. 단순 위임 객체가 아니며, host는 이 객체를
    한 번 만들어 두 표면에 공유한다.
  - `ZLinkStreamFrameCodec.tryDecode`는 6바이트 frame prefix 레이아웃 지식을 codec에 가둔다.
    actor client는 frame byte layout을 직접 해석하지 않는다.
  - `ZLinkStreamHeader.createResponse`/`createErrorResponse`는 request sequence와 correlation id echo 규칙을
    한 곳에 둔다. 개별 reply 경로는 어떤 값을 echo해야 하는지 다시 조립하지 않는다.
  - 새 public 사용자 계약은 추가하지 않았다. 변경된 타입은 `runtime.*` 내부 조립 표면에서만 사용한다.

## 부록 I. 2026-07-09 handler scanner/codec 선택 중복 정리

- 완료: C9.
- 처리: `ZLinkCodecRegistration`의 fallback serializer 선택과 type별 serializer 선택 규칙을
  `singleFallbackSerializer`/`singleSerializerFor`에 모았다. 여러 호출 경로가 같은 ambiguity 오류와
  fallback 규칙을 사용한다.
- 처리: `ZLinkGenericTypeResolver`의 Class 기반 interface 탐색과 Kotlin raw-name 기반 interface 탐색은
  `Predicate<Class<?>>` 기반 matcher로 합쳤다. `ZLinkHandlerScanner`의 interface handler 등록도
  private `HandlerInterfaceMatcher`를 통해 한 벌의 등록 로직을 사용한다.
- POSD 자체 점검: 새 scanner matcher는 public 표면이 아니며, Java `Class<?>`와 Kotlin raw type name의
  차이를 scanner 내부 한 곳에 숨기는 역할을 한다. 별도 top-level 얕은 모듈로 분리하지 않고 private
  helper로 유지했다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.handlers.ZLinkHandlerScannerTest --tests systems.zlink.framework.runtime.configuration.ZLinkCodecRegistrationTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-core:contractTest :zlink-framework-core:integrationTest`

## 부록 J. 2026-07-09 bound-session send/join 결과 중복 정리

- 완료: C8.
- 처리: `ZLinkBoundSessionSendOptions`를 추가해 bound-session send의 packet name 선택, metadata 누산,
  SEND header 생성, framed payload encoding 규칙을 한 곳에 모았다. local/native/routed runtime은 각자 다른
  transport와 retry 책임만 유지한다.
- 처리: actor join 결과의 public `ActorRef` 변환, reply deserialization, reply part close 정책을
  `ZLinkActorRuntime` private helper로 모았다. entry spot join과 normal spot join은 거절 처리 의미와
  상태 전이가 다르므로 그 차이는 각 call object에 그대로 남겼다.
- POSD 자체 점검: `SendCall` 3벌을 상속 base로 강제로 묶으면 transport별 전송 책임을 한 추상화에
  섞는 얕은 모듈이 된다. 이번 변경은 반복된 framing/options 정책만 숨겨 새 public 표면을 만들지 않았다.
  join helper도 record 타입 계층을 새로 만들지 않고 필요한 값만 받아 중복 정책만 줄였다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e-kotlin/SpotService`, `e2e/ToActorMessaging`, `e2e-kotlin/ToActorMessaging`
  - 전체 Java/Kotlin unit/contract/integration 묶음은 한 번 `zlink-framework-core:integrationTest`에서
    JVM SIGSEGV로 중단됐고, 같은 `:zlink-framework-core:integrationTest` 단독 재실행은 성공했다.
    이어서 남은 모듈 테스트를 별도 실행해 성공했다. `zlink-stream-connector:test`는 묶음 실행 중
    bounded dispatch 테스트가 한 번 assertion 실패했지만, 해당 테스트 단독 재실행과
    `:zlink-stream-connector:test` 전체 재실행은 모두 성공했다.
  - `./samples/run_samples.sh`는 최종 `All Java/Kotlin samples passed`까지 확인했다. 실행 중 Java `Bingo`와
    Kotlin `TicTacToe`에서 transient port bind retry가 있었고 runner 재시도 후 통과했다.
  - 정적 점검: `git diff --check -- bindings/doc/plan/framework/java-kotlin-framework-posd-ddd-refactor-list.ko.md framework/languages/java`,
    debug stdout grep, stream-connector framework-core 의존/import grep 모두 clean.

## 부록 K. 2026-07-09 route missing handler framework-error 정리

- 완료: B8.
- 처리: `ZLinkSpotRuntime`의 SPOT route request missing-handler와 request/send shape mismatch가 평문 reply를
  직접 만들던 경로를 기존 `replySpotRouteDispatchError`로 바꿨다. 이 경로는 `ZLinkFrameworkError`
  marker와 error text를 함께 reply하고 dispatch error observer 보고도 같은 정책을 쓴다.
- 회귀 테스트: `ChannelMessagingTest.routeMesh_missingRequestHandlerRepliesFrameworkError`를 추가해 route mesh
  request가 target node의 missing handler에 도달하면 클라이언트가 정상 payload가 아니라
  `ZLinkFrameworkException`으로 받는지 확인했다.
- POSD 자체 점검: 새 오류 helper를 추가하지 않고 기존 SPOT route error reply 정책을 재사용했다.
  직접 문자열 reply와 observer report를 반복하던 호출부 지식만 제거했으며 public API는 바꾸지 않았다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_missingRequestHandlerRepliesFrameworkError`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e-kotlin/RegistryMessaging`

## 부록 L. 2026-07-09 actor metadata 보존과 channel/spot metadata gap 분리

- 완료: B5.
- 처리: `ZLinkSessionActorsRuntime.replyLocal`과 `ActorPacketFrames.encodeReply`가 request stream header의
  metadata를 response header에 보존하도록 했다. reply payload를 실제로 압축하지 않는 경로이므로
  response header에는 `PAYLOAD_COMPRESSED` flag를 세우지 않는다.
- 처리: `ActorPacketFrames.decode`가 stream actor packet의 metadata와 compression flag를 보존하고,
  forwarding용 request header를 다시 만들 때 같은 header 정보를 사용하게 했다.
- 분리: channel/spot outbound `metadata(...)`는 수신 handler context와 wire 형식의 공통 public 계약이
  없어 즉시 배선하지 않았다. 새 public API나 raw metadata part를 Java/Kotlin에만 추가하면 계약 출처가
  공통 spec이 아니라 구현이 되므로, `framework/doc/framework/common/draft/channel-spot-metadata-contract.ko.md`
  초안으로 분리했다.
- POSD 자체 점검: metadata/flag 보존 책임은 stream frame/header를 이미 해석하는 `ActorPacketFrames`와
  session actor reply 경로 안에 머물렀다. 테스트도 `ZLinkStreamFrameCodec.tryDecode`를 사용해 새 테스트가
  6바이트 frame prefix 지식을 다시 복제하지 않도록 했다. channel/spot gap은 얕은 adapter나 테스트 전용
  metadata helper로 메우지 않았다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests.entrySpotActorDispatchNoBindRequestRepliesViaNoBindAndDoesNotBindSession`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.spots.ActorPacketFramesTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`

## 부록 M. 2026-07-09 actor submit/bind fault 판정 중복 정리

- 완료: C5.
- 처리: `ZLinkActorSubmitFaults`를 추가해 actor relay와 bound-session send가 공유하는 submit retry
  판정, already-bound 판정, missing-binding 무시 판정을 한 곳으로 모았다.
- 처리: bind retry 판정은 하나로 합치지 않고 `retryableSessionActorBindFailure`와
  `retryableBoundSessionBindFailure`로 분리했다. 기존 정책이 서로 달라서 같은 메서드로 합치면
  표면상 중복은 줄어도 동작 의미가 섞인다.
- 회귀 테스트: `ZLinkActorSubmitFaultsTest`를 추가해 submit retry 결과, already-bound 결과,
  session actor bind retry와 bound-session bind retry의 차이를 고정했다.
- POSD 자체 점검: 새 helper는 public API가 아니며, retry 오류 분류라는 한 가지 정책만 소유한다.
  scheduler 실행 순서와 transport send 책임은 기존 runtime에 남겨 얕은 pass-through 객체가 되지 않게 했다.
- 추가 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`

## 부록 N. 2026-07-09 channel no-data backoff 정리

- 완료: C11.
- 처리: request, route, subscribe receive loop가 직접 `noDataMisses` 정수와 sleep 시간을 관리하지 않도록
  private `NoDataBackoff`로 모았다. 호출부는 data 수신 시 `reset()`, no-data 수신 시 `pause()`만 호출한다.
- 처리: `Thread.sleep(...)` 호출을 제거하고 `LockSupport.parkNanos(...)`를 사용한다. blocking recv로 바꾸면
  close 순서와 native socket wake-up 계약까지 바꿔야 하므로 이번 범위에서는 polling 의미를 유지했다.
- POSD 자체 점검: 새 helper는 public API가 아니며, no-data miss count와 capped exponential delay 정책만
  소유한다. request/route/subscribe dispatch 책임은 loop에 남겨 helper가 전송/dispatch를 대신 아는
  범용 객체가 되지 않게 했다.
- 검증:
  - `rg -n "Thread\\.sleep|pauseAfterNoData|noDataMisses" framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntime.java` no-hit
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.channels.ZLinkChannelRuntimeTest --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest`
  - `timeout 180s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_sendDispatchesToHandler --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_missingRequestHandlerRepliesFrameworkError --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest`
  - `timeout 180s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.publisherAndSubscriber_workAcrossHosts --tests systems.zlink.framework.runtime.ChannelMessagingTest.scannedMethodHandlerGroup_publishDispatches`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e-kotlin/RegistryMessaging`
- 참고: `ChannelMessagingTest` 전체 class 실행은 `manualClientServer_sendDispatchesToHandler` 종료 중
  native `ctxTerm`에서 5분 이상 진행이 없어 중단했다. thread dump에는 channel runtime thread가 남아
  있지 않았고, 위 대표 slice들은 timeout 안에 통과했다.

## 부록 O. 2026-07-09 actor retry scheduler 공유

- 부분 완료: C4의 executor 중복 범위와 route-ready polling 중복 범위. 4개 actor retry executor를
  `ZLinkActorRetryScheduler` 하나로 모았고, session actor와 bound-session의 route-ready 대기 루프를
  같은 polling helper로 옮겼다.
- 처리: session actor relay, bound-session send/bind, native bound-session send, actor client route retry가
  같은 scheduler를 사용한다. 기존 10ms relay/bind retry, 20ms route retry, 25ms native bound-session retry
  지연은 이름 있는 메서드로 유지했다. route-ready 대기는 "조건이 준비되면 완료", "timeout이면 오류",
  "timeout이어도 계속 진행"이라는 기존 의미 차이를 helper 호출 이름으로 분리했다.
- POSD 자체 점검: 새 helper는 executor 생성, thread 정책, retry delay 이름, route-ready deadline polling만
  소유한다. 실제 send/bind 성공 조건, timeout 메시지, native bind 후 재시도 의미는 각 runtime에 남겼다.
  즉, 전송 책임을 끌어안는 범용 retry 엔진으로 넓히지 않았고, 기존 동작 차이를 숨겨 섞지 않았다.
- 리팩토링 산출물 재점검: `ZLinkActorRetryScheduler.execute(...)`는 단순 전달처럼 보이지만 현재
  bound-session send의 첫 attempt를 caller thread가 아니라 actor retry thread에서 시작하게 하는 내부
  실행 정책이다. 호출 의미가 send/bind 상태기계와 섞이지 않도록 public API로 노출하지 않고
  package-private helper 안에만 둔다.
- 남은 범위: submit/send/bind retry의 `Attempt implements Runnable` 상태기계 중복은 아직 남아 있다.
  false 반환, retryable submit 예외, native bind 뒤 재시도, timeout 성공 처리처럼 의미가 서로 달라
  단일 범용 retry 엔진으로 바로 합치면 파라미터가 많은 얕은 모듈이 된다. 다음 단계에서는 attempt
  결과 모델을 먼저 잡아야 한다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
    첫 실행은 JVM SIGSEGV(`hs_err_pid2355514.log`)로 실패했고, 같은 명령 재실행은 성공했다.
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`

## 부록 P. 2026-07-09 stream core/connector wire 교차 테스트

- 부분 완료: C1의 최소 검증 게이트. `zlink-stream-connector` 테스트에
  `ZLinkStreamCoreWireInteropTest`를 추가해 core `ZLinkStreamHeaderCodec`/`ZLinkStreamFrameCodec`이 만든
  header/frame을 connector `ZLinkStreamWireProtocol`이 decode/re-encode할 수 있는지, 반대 방향도 같은
  byte layout을 유지하는지 확인한다.
- 처리: connector의 제품 의존성에는 `zlink-framework-core`를 다시 추가하지 않았다. 교차 검증은
  `testImplementation(project(":zlink-framework-core"))`로만 연결해, 배포되는 stream connector가 framework
  core를 끌어오지 않는 C0의 독립성 불변식을 유지한다.
- 처리: core `ZLinkStreamHeader` 생성 경계에 connector와 같은 wire 의미 검증을 추가했다. SEND는 request
  sequence를 가질 수 없고, REQUEST/RESPONSE는 request sequence가 필요하며, ERROR는 JSON codec을 쓰고,
  CONTROL은 raw codec과 빈 flags만 허용한다. request sequence 0과 255바이트를 넘는 packet name도
  header 생성 시점에 거부한다.
- 처리: connector bounded manual dispatch 테스트가 receive loop 타이밍으로 최신 메시지 보존 정책을
  간접 검증하던 비결정성을 제거했다. 최신 메시지 보존은 `ZLinkStreamDispatchQueue` 책임이므로
  queue를 직접 검증하고, connector enqueue/dispatch 통합은 기존 `dispatch_invokesCallback` 테스트가
  맡도록 나눴다.
- POSD 자체 점검: 이번 변경은 wire layout 지식을 새 runtime helper로 복제하지 않고 테스트에만 둔다.
  header 의미 검증은 `ZLinkStreamHeader` record에 두어 runtime 호출부가 같은 사전 조건을 반복하지 않게
  했다. 따라서 새 public API나 shallow wrapper를 만들지 않는다. 다만 wire layout encode/decode 구현이
  core와 connector에 각각 남아 있으므로 C1은 완료가 아니다.
- 남은 범위: plain-string fallback 유지 여부와 contracts-only 저수준 모듈 추출 여부를 계약 문서와 맞춰
  결정해야 한다. 코드 공유가 필요하면 제품 의존성이 아니라 framework runtime에 의존하지 않는
  contracts-only 저수준 모듈로 분리해야 한다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodecTest`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodecTest --tests systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodecTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-stream-connector:test --tests systems.zlink.stream.connector.ZLinkStreamCoreWireInteropTest --tests systems.zlink.stream.connector.ZLinkStreamWireProtocolTest`
  - `./gradlew --no-daemon :zlink-stream-connector:test --tests systems.zlink.stream.connector.ConnectorDispatchTest`
  - `./gradlew --no-daemon :zlink-stream-connector:test`

## 부록 Q. 2026-07-09 C4/C1 후 focused 재검증

- core focused unit/build:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-stream-connector:test`
- core focused integration:
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
- 관련 e2e build만 실행:
  - `e2e/ToActorMessaging`에서 `../../gradlew --no-daemon build`
  - `e2e-kotlin/ToActorMessaging`에서 `../../gradlew --no-daemon build`
  - `e2e/SpotService`에서 `../../gradlew --no-daemon build`
  - `e2e-kotlin/SpotService`에서 `../../gradlew --no-daemon build`
  - full e2e sweep은 실행하지 않았다.
- sample 검증:
  - `./samples/run_samples.sh`는 Java `TicTacToe`, `Bingo`, `DeliveryDispatch`, `GameQuest`,
    `ShoppingMall`, `SupportChat`, Kotlin `TicTacToe`까지 통과 출력 후 exit 143으로 종료돼
    성공 마커까지 도달하지 못했다.
  - 이후 현재 코드 기준으로 Java `TicTacToe`, `Bingo`, `DeliveryDispatch`, `GameQuest`,
    `ShoppingMall`, `SupportChat`와 Kotlin `TicTacToe`, `Bingo`, `GameQuest`, `ShoppingMall`을
    각 `samples/<lang>/<sample>/run_sample.sh`로 순차 실행해 exit 0을 확인했다.
  - Kotlin `DeliveryDispatch`는 Docker Redis 자동 생성 경로에서 한 번 exit 143으로 중단됐고,
    `DELIVERYDISPATCH_REDIS_ENDPOINT=127.0.0.1:6379`를 명시한 단독 실행은
    `deliverydispatch full client/server self-check completed`까지 성공했다.
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.
  - 전체 runner의 마지막 forbidden sample pattern grep은 별도 실행했고 no-hit였다.

## 부록 R. 2026-07-09 route-to-SPOT target 선택 정리

- 부분 완료: C6의 target 선택 중복 범위. `sendToSpotViaRouterChannel`과
  `requestToSpotViaRouterChannel`이 각각 `registrationsByName`과 `spotRouterNodes`를 직접 비교하던
  prefix를 `resolveSpotRouteTarget(...)`로 모았다.
- 처리: route mesh channel이면 `RouteBridgeTarget`, 등록된 spot router node가 있으면
  `SpotRouterNodeTarget`으로 반환한다. target 타입은 nullable field 하나로 표현하지 않고 private
  target 타입 두 개로 분리했다.
- POSD 자체 점검: 새 helper는 "어느 route target을 쓸지"라는 선택 지식만 소유한다. bridge send/request
  retry, raw reply correlation, spot-router-node callback 완료 규칙은 서로 의미가 달라 그대로 각 경로에
  남겼다. 이 부분까지 즉시 합치면 파라미터가 많은 얕은 전송 엔진이 되므로 C6은 아직 완료가 아니다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.channels.ZLinkChannelRuntimeTest --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest`
  - `timeout 240s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_missingRequestHandlerRepliesFrameworkError --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_requestByRoutingIdSucceeds --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_sendByRoutingIdDispatchesToHandler --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e-kotlin/RegistryMessaging`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 S. 2026-07-09 actor submit retry loop 정리

- 부분 완료: C4의 submit retry loop 중복 범위. `ZLinkActorRetryScheduler`에
  `submitRelayUntilAccepted(...)`, `submitRelayUntilAcceptedAsync(...)`,
  `submitNativeBoundSessionUntilAcceptedAsync(...)`를 추가해 false submit 결과, retryable
  `ZlinkSubmitException`, deadline timeout, retry delay 선택을 한 곳에 모았다.
- 처리: local actor reply, remote bound-session bind relay, routed bound-session send,
  native bound-session send는 이제 각 전송 시도와 timeout 오류만 넘긴다. 첫 시도를 호출 thread에서
  시작하던 경로와 retry executor에서 시작하던 경로, relay 10ms delay와 native bound-session 25ms
  delay는 기존 의미를 유지했다.
- POSD 자체 점검: 새 helper는 submit retry의 공통 상태기계만 소유한다. `bindActorWithRetry(...)`,
  `ensureNativeBinding()`, `relayWithRetry(...)`는 bind retry 정책, native binding 이후 재시도,
  timeout 처리 의미가 서로 달라 아직 합치지 않았다. 이 범위를 지금 한 엔진에 넣으면 파라미터가 많은
  얕은 모듈이 되므로 C4는 아직 완료가 아니다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e-kotlin/SpotService`,
    `e2e/ToActorMessaging`, `e2e-kotlin/ToActorMessaging`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 T. 2026-07-09 Spring capability registrar 책임 분리

- 부분 완료: D9의 Spring registrar 범위. `ZLinkFrameworkCapabilityBeanRegistrar`는
  `postProcessBeanFactory(...)`에서 옵션 획득, session packet handler discovery, validation,
  application bean 등록, capability delegate 등록 순서만 조립한다.
- 처리: application prototype 등록과 collection dependency scan은 `ZLinkApplicationBeanRegistrar`,
  capability delegate bean 조건과 등록은 `ZLinkFrameworkCapabilityDelegates`, session packet handler
  discovery는 `ZLinkSessionPacketHandlerDiscovery`, classpath scan은 `ZLinkClasspathTypeScanner`,
  Spring bean definition 공통 작업은 `ZLinkSpringBeanDefinitions`가 소유한다.
- POSD 자체 점검: 새 클래스는 public API가 아니며, 각 파일은 한 가지 정책을 숨긴다. 단순 파일 쪼개기로
  끝나지 않도록 registrar에는 실행 순서만 남겼고, Kotlin raw-type 이름 비교는 session packet handler
  discovery 안에만 가뒀다. 이 시점에는 `ZLinkHttpRequestBuilder` 분리를 다음 범위로 남겨 두었다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-spring-boot-starter:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e-kotlin/SpotService`,
    `e2e/ToActorMessaging`, `e2e-kotlin/ToActorMessaging`, `e2e/RegistryMessaging`,
    `e2e-kotlin/RegistryMessaging`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    Kotlin DeliveryDispatch에서 transient port bind retry 1회 후 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 U. 2026-07-10 HTTP request builder 책임 분리

- 완료: D9. Spring registrar 범위에 이어 `ZLinkHttpRequestBuilder`의 target query 조립과
  form/multipart body encoding 책임을 분리했다.
- 처리: query parameter percent-encoding과 `?`/`&` separator 선택은 `ZLinkHttpTargetBuilder`가 소유한다.
  body source 단일성 검증, form-url-encoded body encoding, multipart boundary/header/body 조립은
  `ZLinkHttpRequestBodyEncoder`가 소유한다. `ZLinkHttpRequestBuilder`에는 fluent API 상태, one-shot client
  수명, terminal operation만 남겼다.
- POSD 자체 점검: 새 helper는 package-private이며 public HTTP client API를 바꾸지 않는다. target builder는
  URL target 조립 규칙만, body encoder는 request body와 header 결정을 함께 숨긴다. JSON typed body
  serialization과 typed response decode는 기존 Jackson 정책을 그대로 유지해 이번 분리 범위에 섞지 않았다.
- 검증:
  - `./gradlew --no-daemon :zlink-http-client:test`
  - 관련 e2e build만 실행: `e2e/RegistrationCodec`, `e2e/RegistryMessaging`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 V. 2026-07-10 Kotlin location extension 파일 분리

- 완료: D10. `ZLinkLocationExtensions.kt`를 `ZLinkLocationStoreCoroutines.kt`,
  `ZLinkLocationQueryFlows.kt`, `ZLinkPublisherFlowBridge.kt`, `ZLinkSuspendingLocationStore.kt`로
  나눴다.
- 처리: 네 파일 모두 `@file:JvmName("ZLinkLocationExtensionsKt")`와 `@file:JvmMultifileClass`를
  사용한다. 따라서 Kotlin source import 경로는 그대로이고, Java에서 보던 top-level function holder인
  `systems/zlink/framework/kotlin/ZLinkLocationExtensionsKt.class`도 jar에 유지된다.
- POSD 자체 점검: suspend store wrapper, paged query Flow, Publisher-to-Flow bridge, suspending store
  adapter base가 서로 다른 변경 이유를 갖도록 파일 경계를 나눴다. 새 helper나 public API는 만들지 않았고,
  기존 로직을 이동만 했다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-kotlin:test --tests systems.zlink.framework.kotlin.KotlinLocationExtensionsTest --tests systems.zlink.framework.kotlin.KotlinFrameworkExtensionsContractTest`
  - `jar tf zlink-framework-kotlin/build/libs/zlink-framework-kotlin-*.jar | rg 'systems/zlink/framework/kotlin/ZLinkLocationExtensionsKt'`
  - 관련 e2e build만 실행: `e2e-kotlin/RegistryMessaging`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 W. 2026-07-10 handler scanner 책임 분리

- 완료: D6. `ZLinkHandlerScanner`에는 marker package별 후보 수집, concrete class 필터, group 해석,
  스캐너 위임만 남겼다.
- 처리: classpath 탐색은 `ZLinkHandlerPackageScanner`, 애노테이션 메서드 handler 해석은
  `ZLinkAnnotationHandlerScanner`, Java/Kotlin interface handler 해석은 `ZLinkInterfaceHandlerScanner`가
  소유한다. subscription topic 검증은 분리 중복을 만들지 않도록 `ZLinkHandlerScanValidation`에 모았다.
- POSD 자체 점검: 새 클래스들은 package-private이며 public framework surface를 바꾸지 않는다.
  `ZLinkInterfaceHandlerScanner`는 여러 handler family를 포함하지만, 모두 "interface로 선언된 handler를
  catalog entry로 바꾸는 규칙"이라는 한 변경 이유를 가진다. 분리 과정에서 생긴 topic 검증 중복은 제거했다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.handlers.ZLinkHandlerScannerTest`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.handlers.ZLinkHandlerScannerTest --tests systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistrationTest --tests systems.zlink.framework.runtime.spots.EntrySpotActorDispatchTests --tests systems.zlink.framework.kotlin.KotlinSuspendAnnotationHandlerTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`
  - Kotlin e2e build 2개는 병렬 실행 중 Kotlin daemon incremental cache 충돌 경고가 있었지만 fallback
    compile 이후 `BUILD SUCCESSFUL`로 종료했다. full e2e sweep은 실행하지 않았다.
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 X. 2026-07-10 location/Redis 책임 분리

- 완료: D8. `ZLinkLocationLifecycle`에는 public-facing 메서드 위임,
  ownership-lost listener 등록/해제, ownership-lost event routing만 남겼다.
- 처리: core location lifecycle에서 spot claim/release tracking은 `ZLinkSpotLocationLifecycle`,
  actor claim/takeover/renew/release와
  actor 위치 갱신은 `ZLinkActorOwnershipCoordinator`, actor-session route bind/remove tracking은
  `ZLinkActorSessionRouteLifecycle`이 소유한다.
- 처리: Redis store에서 key naming은 `ZLinkRedisLocationKeys`, Redis connection lifecycle은
  `ZLinkRedisLocationConnection`, Lua script 호출과 write-result/lease 변환은
  `ZLinkRedisLocationScriptsClient`, row materialization과 paged scan은 `ZLinkRedisLocationRows`,
  filter predicate는 `ZLinkRedisLocationFilters`, plain change-stamp GET은 `ZLinkRedisLocationStampReader`가
  소유한다. public `ZLinkRedisLocationStore`는 `ZLinkLocationStore`/`ZLinkLocationChangeStampStore`
  facade로 남겼다.
- POSD 자체 점검: actor와 spot ownership-lost 처리 흐름은 비슷하지만 저장하는 tracked state와 stale 처리
  의미가 다르다. 이를 generic tracker로 합치면 호출 규약이 늘어나는 얕은 모듈이 되므로 각 coordinator에
  남겼다. Redis 분리에서도 script와 row scan을 한 helper에 섞지 않았고, change stamp는 Lua script 경로가
  아니므로 별도 reader로 유지했다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.locations.ZLinkLocationLifecycleTest`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.locations.ZLinkLocationLifecycleTest --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_requestByRoutingIdSucceeds --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-locations-redis:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 Y. 2026-07-10 runtime host subsystem wiring 분리

- 완료: D3. `ZLinkFrameworkRuntime` 생성자에는 옵션 검증, 공통 service 등록, subsystem 생성 순서,
  public facade 필드 할당만 남겼다.
- 처리: channel/backend context 생성과 channel service 등록은 `ZLinkFrameworkChannelSubsystem`,
  location store/runtime/query/lifecycle/auto-connect resolver 등록은 `ZLinkFrameworkLocationSubsystem`,
  spot runtime 생성과 remote ref resolver/route bridge 연결은 `ZLinkFrameworkSpotSubsystem`,
  actor runtime/directory/client 생성과 tracer/route join 연결은 `ZLinkFrameworkActorSubsystem`,
  stream runtime 생성은 `ZLinkFrameworkStreamSubsystem`, auto-connect 시작 인자 조합은
  `ZLinkFrameworkAutoConnectSubsystem`이 소유한다.
- 처리: `close()`의 6단 중첩 `try/finally`는 `ZLinkFrameworkShutdown`의 LIFO close stack으로 평탄화했다.
  shutdown은 `ZlinkCloseException`만 기존처럼 무시하고, 다른 runtime failure는 남은 close action을 계속
  실행한 뒤 다시 던진다.
- POSD 자체 점검: 새 subsystem은 public API가 아니며, 각 파일은 생성과 `runtimeHandlers.add(...)`,
  resolver/bridge/tracer 연결 같은 실제 배선 결정을 함께 숨긴다. host에서 새 helper로 인자를 그대로
  넘기기만 하는 pass-through 분리는 만들지 않았다. `ZLinkFrameworkAutoConnectSubsystem`은 얇지만 실제
  reconciliation 정책은 기존 `ZLinkLocationAutoConnectHost`가 계속 소유하고, 이번 파일은 host의
  null/spot-node 인자 조합만 감춘다. 따라서 이번 D3 결과 자체를 새 POSD 리팩토링 대상으로 보지는 않는다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.locations.ZLinkLocationLifecycleTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests --tests systems.zlink.framework.runtime.NodesAndServicesTest --tests systems.zlink.framework.runtime.SpotManagerTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e/YieldDispatch`, `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`,
    `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 Z. 2026-07-10 stream runtime/connector 책임 분리

- 완료: D5. `ZLinkStreamRuntime` 범위와 `DefaultZLinkStreamConnector` 범위를 모두 분리했다.
- 처리: session context 상태와 dispatch/reply-error 처리는 `ZLinkStreamSessionContextState`, public
  `ZLinkSessionClient`와 send/reply call 구현은 `ZLinkStreamSessionCalls`, payload compression/decompression
  규칙은 `ZLinkStreamPayloadCodec`이 소유한다. `ZLinkStreamRuntime`에는 stream socket lifecycle, session
  state map, notification/transport-error routing, handler 실행 orchestration만 남겼다.
- 처리: stream connector에서는 public `ZLinkStreamSendCall`/`ZLinkStreamRequestCall` 구현을
  `ZLinkStreamConnectorCalls`, payload copy·compression·wire codec mapping을
  `ZLinkStreamConnectorPayloadCodec`, inbound frame 해석·pending request 완료·handler dispatch·heartbeat
  control 처리를 `ZLinkStreamReceiveDispatcher`가 소유한다. connect/disconnect/reconnect/close, TCP/TLS/WS
  connect, receive loop, heartbeat timeout, reconnect backoff, connection state listener dispatch는
  `ZLinkStreamConnectionLifecycle`이 소유한다.
- POSD 자체 점검: 새 helper는 package-private이며 public stream API를 바꾸지 않는다. context 객체는
  dispatch 중 current request header, bound actor disconnect, async error reply retry를 함께 숨겨 단순
  getter wrapper가 아니다. call 객체는 send/reply submit 계약과 payload 소유권 close 규칙을 보존한다.
  payload codec은 compression flag와 maximum decompressed payload size를 한 곳에 모아 runtime/call 양쪽의
  중복 조건을 만들지 않는다. connector call 분리 중 `send(...).submit()`이 기존처럼 write completion을
  기다리지 않는 완료 stage를 반환하도록 보존했다. receive dispatcher는 wire frame을 handler/pending
  request/error/control로 바꾸는 정책을 숨기며, connector에 callback별 parse/dispatch 분기를 남기지
  않는다. connection lifecycle은 447줄로 작지는 않지만 상태·transport·heartbeat·reconnect·read-loop가
  하나의 불변식으로 움직이므로 더 쪼개지 않았다. 이 단계에서 TCP/TLS/WS connect만 별도 helper로 빼면
  lifecycle 상태를 다시 외부로 노출하는 얕은 모듈이 된다. 이 분리 결과에서 새 public surface나 호출자
  부담은 생기지 않았다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodecTest --tests systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodecTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-stream-connector:compileJava`
  - `./gradlew --no-daemon :zlink-stream-connector:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/YieldDispatch`
  - `e2e/DeliveryDispatch`와 `e2e-kotlin/DeliveryDispatch`는 디렉터리가 없어 실행 대상에서 제외했다.
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AA. 2026-07-10 actor location coordinator 분리

- 부분 완료: D4 중 `ZLinkActorRuntime`의 location store 협업 책임만 분리했다. `ZLinkActorRuntime`은
  actor 생성과 dispatch 상태를 계속 소유하고, `ZLinkActorLocationCoordinator`는 location lifecycle,
  store resolver, actor join/leave/move 알림, bound-session route 등록/제거, actor row 기반 public
  ref 조회를 소유한다.
- 처리: actor 생성 흐름에서는 `claimsActors(...)`로 location claim 필요 여부만 묻고, claim 실패의
  public error mapping과 actor-ref 저장은 coordinator 안에 둔다. actor find fallback은 `findStoredActorRef`
  로 이동했다. 이 이름은 과거 public contract 금지 문자열인 `resolveActorRef`를 되살리지 않도록 고른다.
- POSD 자체 점검: 새 파일은 package-private이며 public actor/location API를 바꾸지 않는다. 단순 getter
  wrapper가 아니라 "location store가 없으면 no-op", "actor type이나 mesh name이 없으면 알림 생략",
  "session route 제거 실패는 best-effort로 무시", "location claim 실패를 framework error로 매핑"이라는
  기존 정책을 한 곳에 숨긴다. `DefaultActorContext` 추출은 이번 단계에서 보류했다. 지금 그대로 빼면 join
  call 생성 콜백과 runtime 내부 상태 접근자를 새로 노출해야 해서 얕은 모듈이 될 가능성이 높다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.locations.ZLinkLocationLifecycleTest --tests systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolversTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests --tests systems.zlink.framework.runtime.NodesAndServicesTest`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AB. 2026-07-10 session relay header 저장소 분리

- 부분 완료: D4 중 `ZLinkSessionActorsRuntime`의 relay-header 저장소를 분리했다.
- 처리: dispatch별 relay header를 보관하는 synchronized weak map과 현재 dispatch의 ThreadLocal을
  `ZLinkSessionRelayHeaders`가 소유한다. 기존 `ZLinkSessionActorsRuntime.enterRelayDispatch(...)`와
  `exitRelayDispatch(...)`는 public static 호출자가 있으므로 wrapper로 유지했다.
- POSD 자체 점검: 새 helper는 package-private이며 public session actor API를 바꾸지 않는다. 단순
  pass-through가 아니라 ThreadLocal과 weak map을 함께 관리하고, dispatch context가 없을 때 empty header를
  반환하는 정책을 한 곳에 숨긴다. 반대로 `BoundActor`는 relay submit retry, local dispatch, remote binding,
  unbind notification을 함께 갖고 있어 이번 작은 분리 뒤에도 별도 큰 리팩토링 대상으로 남겼다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e/YieldDispatch`, `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`,
    `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    중간에 DeliveryDispatch Java의 transient port bind retry 1회를 거친 뒤 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AC. 2026-07-10 actor serial dispatch 분리

- 부분 완료: D4 중 `ZLinkActorRuntime`의 serial dispatch queue와 current dispatch actor id 관리 책임을
  분리했다.
- 처리: per-actor `ZLinkAsyncSerialQueue` map과 dispatch turn ThreadLocal은 `ZLinkActorDispatchSerials`가
  소유한다. `ZLinkActorRuntime`은 actor가 아직 관리 대상인지 확인하고, actor 제거 시 dispatch state를
  같이 제거하는 orchestration만 남긴다.
- POSD 자체 점검: 새 helper는 package-private이며 public actor API를 바꾸지 않는다. actor map을 새 helper로
  넘기지 않았기 때문에 actor ownership 정보가 새 모듈로 새지 않는다. helper는 queue 생성, nested dispatch
  inline 판정, dispatch turn enter/restore 정책을 함께 숨긴다. `QueuedTurn`은 queue 객체를 runtime에 직접
  노출하지 않는 내부 토큰 역할만 한다. `DefaultActorContext`는 아직 큰 흐름과 상태 변경을 함께 갖고 있고,
  `JoinSpotCall`과 `BoundActor` 분리 전에는 이 둘도 별도 리팩토링 대상으로 남아 있었다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.execution.ZLinkAsyncSerialQueueTest --tests systems.zlink.framework.runtime.spots.ZLinkSpotRuntimeActorArgumentsTest`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e/YieldDispatch`, `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`,
    `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AD. 2026-07-10 bound session actor 분리

- 부분 완료: D4 중 `ZLinkSessionActorsRuntime.BoundActor`를 `ZLinkBoundActor`로 분리했다.
- 처리: `ZLinkBoundActor`는 public `ZLinkSessionActor` 구현, relay header 조회, local actor dispatch/reply,
  native binding retry, remote bound-session/disconnect notification을 소유한다. `ZLinkSessionActorsRuntime`에는
  bind 대상 선택, bound list 등록/제거, managed actor용 `ZLinkBoundSessionRuntime` 생성이 남는다.
- POSD 자체 점검: 새 클래스는 package-private이며 public session actor API를 바꾸지 않는다. 단순 wrapper가
  아니라 bound actor 하나의 relay lifecycle과 retry 정책을 함께 숨긴다. relay header 저장소는 이미
  `ZLinkSessionRelayHeaders`가 소유하므로 `ZLinkBoundActor`는 저장소 구현을 알지 않고 조회만 한다.
  `ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT`은 bind 준비와 bound actor relay 양쪽의 같은 정책이라
  중복 상수로 복사하지 않고 package-private 상수로 공유했다. 이 분리 결과에서 새 public surface나 호출자
  부담은 생기지 않았다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/RegistryMessaging`, `e2e/SpotService`, `e2e/ToActorMessaging`,
    `e2e/YieldDispatch`, `e2e-kotlin/RegistryMessaging`, `e2e-kotlin/SpotService`,
    `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AE. 2026-07-10 actor Spot join call 분리

- 부분 완료: D4 중 `ZLinkActorRuntime.JoinSpotCall`을 `ZLinkActorSpotJoinCall` package-private 클래스로
  분리했다. `JoinEntrySpotCall`은 별도 항목에서 완료했고, `DefaultActorContext`는 아직 `ZLinkActorRuntime`
  안에 남아 있다.
- 처리: `ZLinkActorSpotJoinCall`은 local/remote Spot join 선택, routed join request/reply 처리,
  actor migration, reply decode, message-flow trace, yield 대기를 소유한다. `ZLinkActorRuntime`에는
  `joinSpot(...)` factory 배선과 actor context state owner 역할만 남겼다.
- 처리: join reply decode/close 규칙은 `ZLinkActorJoinResults`로 분리해 entry-spot join과 Spot join이
  같은 helper를 사용하게 했다. 따라서 `JoinSpotCall` 추출 결과가 reply decode 중복이라는 새 POSD
  대상으로 남지 않는다.
- POSD 자체 점검: 처음 분리한 `ZLinkActorSpotJoinCall` 생성자는 의존성 인자가 길어져 새 red flag가
  됐다. 바로 내부 `Services` record로 묶어 call 객체 생성 표면을 줄였다. 새 클래스는 public API를
  늘리지 않고 framework-managed call object만 구현한다. `ZLinkFrameworkTurns` allowlist에는 이 내부
  call object를 추가했다. 그렇지 않으면 `yield()`와 turn capture가 guard에 막혀 기존 동작과 달라진다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.HandlerContractTest --tests systems.zlink.framework.LocationContractTest --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    최종 `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AF. 2026-07-10 actor entry-spot join call 분리

- 부분 완료: D4 중 `ZLinkActorRuntime.JoinEntrySpotCall`을 `ZLinkActorEntrySpotJoinCall`
  package-private 클래스로 분리했다. `DefaultActorContext`는 아직 `ZLinkActorRuntime` 안에 남아 있다.
- 처리: `ZLinkActorEntrySpotJoinCall`은 entry-spot join 전송, join result 검증, reply decode, native
  bound-session actor ref 갱신, entry spot 위치 갱신, yield 대기를 소유한다. `ZLinkActorRuntime`에는
  `joinEntrySpot(...)` factory 배선과 actor context state owner 역할만 남겼다.
- 처리: actor context 상태 변경은 `markMovedToEntrySpot(...)` 메서드로 캡슐화했다. 새 call object가
  context 필드를 직접 조합하지 않고, "entry spot으로 이동 완료"라는 상태 전이를 한 메서드로 요청한다.
- POSD 자체 점검: 새 클래스는 package-private이며 public actor API를 바꾸지 않는다. `Services` record는
  `ZLinkBackendSpotNode`, serializer, location renewal 세 의존성만 묶고, entry-spot join 정책 자체는
  call object가 숨긴다. `ZLinkFrameworkTurns` allowlist에는 이 내부 call object를 추가했다. 그렇지 않으면
  `yield()`와 turn capture가 guard에 막혀 기존 동작과 달라진다. 이번 분리 결과로 `ZLinkActorRuntime` 안에
  남은 D4 큰 항목은 `DefaultActorContext`뿐이다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.HandlerContractTest --tests systems.zlink.framework.LocationContractTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는 Kotlin
    `DeliveryDispatch`에서 transient port bind failure 후 1/3 재시도했고, 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AG. 2026-07-10 actor context state 분리

- 완료: D4 중 `DefaultActorContext`의 actor lifecycle mutable state를 `ZLinkActorContextState`
  package-private 클래스로 분리했다. `ZLinkActorRuntime`은 actor map, 생성/삭제 orchestration,
  native/backend 협업을 계속 소유하고, `DefaultActorContext`는 public `ZLinkActorContext` facade와
  join call factory만 맡는다.
- 처리: actor ref, joined spot, bound session, session binding token, source node/session rid, destroy
  진행 상태를 `ZLinkActorContextState`가 한 곳에서 갱신한다. runtime과 join call object는 필드를 직접
  조합하지 않고 `markJoined`, `markMovedToEntrySpot`, `bindSession`, `clearAfterDestroy` 같은 상태 전이를
  요청한다.
- POSD 자체 점검: 새 클래스는 public API가 아니며 상태 전이를 메서드로 숨긴다. 따라서 이번 산출물은
  호출자에게 내부 binding token/source rid 지식을 더 노출하지 않는다. `DefaultActorContext`가 public
  facade와 상태 owner를 함께 맡던 얕은 모듈 위험은 줄었다. 다만 actor runtime과 spot runtime의 협업 순서
  지식은 여전히 `D-Cross` 항목의 별도 대상이다. 이번 분리가 그 경계를 새 public API나 테스트 전용
  adapter로 우회하지는 않았으므로, 추가 POSD 대상은 `D-Cross`로 남긴다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.HandlerContractTest --tests systems.zlink.framework.LocationContractTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh` →
    `All Java/Kotlin samples passed`

## 부록 AH. 2026-07-10 Java backend adapter framing/context 분리

- 부분 완료: D7 중 `ZLinkJavaBackendAdapterFactory` 내부의 stream frame 조립 책임을
  `ZLinkJavaStreamFraming` package-private 클래스로 분리했다. 기존 factory 안에 있던 stream header 생성,
  payload part 해석, frame encode, temporary `Message` close 책임을 한 파일이 소유한다.
- 부분 완료: native `Context` wrapper는 `ZLinkJavaContext`로, socket monitor wrapper와 native monitor event
  변환은 `ZLinkJavaSocketMonitor`로 분리했다. factory는 adapter 생성과 native socket wrapper 연결에 더
  집중한다.
- POSD 자체 점검: `ZLinkJavaStreamFraming`은 단순 패스스루가 아니라 "framework stream message parts를
  native one-frame stream payload로 바꾸는 규칙"을 숨긴다. 따라서 분리 산출물 자체는 얕은 모듈이 아니다.
  `ZLinkJavaContext`와 `ZLinkJavaSocketMonitor`는 작은 wrapper지만 native lifecycle/monitor event 변환
  결정을 factory 밖으로 이동해, factory가 모든 backend wrapper 지식을 한 파일에 쌓는 위험을 줄인다.
  남은 POSD 대상은 socket/spot node adapter 중첩 타입과 submit/received 변환 헬퍼다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.NodesAndServicesTest --tests systems.zlink.framework.runtime.RegistryAndMonitoringTest --tests systems.zlink.framework.runtime.MonitoringEventsTest`
  - `./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는 Kotlin
    `DeliveryDispatch`에서 transient port bind failure 후 1/3 재시도했고, 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AI. 2026-07-10 Java backend channel socket wrapper 분리

- 부분 완료: D7 중 channel socket wrapper 4개를 `ZLinkJavaDealerSocket`, `ZLinkJavaRouterSocket`,
  `ZLinkJavaPublisherSocket`, `ZLinkJavaSubscriberSocket` package-private 파일로 분리했다.
  `ZLinkJavaBackendAdapterFactory`는 channel adapter 생성과 native context 배선만 담당하고, socket별
  bind/connect/send/request/recv 정책은 각 wrapper가 소유한다.
- 처리: native send/request/reply/recv 조립은 `ZLinkJavaSocketSupport`로 옮겼다. 이 helper는
  `NO_DATA`/`BUSY`/`INTERNAL_ERROR`를 non-blocking no-data로 접는 정책, request callback reply 복사,
  `Received` reply close hook 보존을 한 곳에 둔다. topic/message part 복사는 `ZLinkJavaBackendCodec`에
  모아 channel/spot wrapper가 같은 규칙을 공유한다.
- POSD 자체 점검: 새 socket wrapper들은 단순 패스스루가 아니라 socket 종류별 public backend 계약과
  native socket 호출 순서, 동기화 범위를 숨긴다. `ZLinkJavaSocketSupport`는 여러 wrapper가 반복하던
  native submit/recv 규칙을 한 곳에 둔다. 다만 helper가 더 커지면 얕은 범용 유틸이 될 수 있으므로,
  다음 D7 단계에서는 stream/spot node/spot route bridge 쪽 native 변환 규칙을 무조건 이 helper에
  추가하지 않고, `ZLinkJavaSpotNode` 또는 spot 전용 codec 단위로 분리할지 다시 검토한다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.ZLinkFrameworkLocationRuntimeTest --tests systems.zlink.framework.runtime.NodesAndServicesTest --tests systems.zlink.framework.runtime.RegistryAndMonitoringTest --tests systems.zlink.framework.runtime.MonitoringEventsTest`
  - `timeout 240s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - `timeout 360s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh` →
    `All Java/Kotlin samples passed`

## 부록 AJ. 2026-07-10 Java backend spot/route bridge wrapper 분리

- 부분 완료: D7 중 spot route bridge wrapper를 `ZLinkJavaSpotRouteBridge` package-private 파일로,
  spot socket wrapper를 `ZLinkJavaSpot` package-private 파일로 분리했다. factory는 native 객체 생성과
  adapter shell 조립에 더 집중하고, route bridge send/request/drain/attach와 spot subscribe/route/actor
  join/lifecycle 호출 순서는 각 wrapper가 소유한다.
- 처리: spot dispatch event, actor received, actor join request, actor lifecycle event, actor ref 변환은
  `ZLinkJavaSpotCodec`으로 옮겼다. 이 codec은 일반 purpose util이 아니라 native spot 계약과 framework
  backend 계약 사이의 도메인 변환 규칙만 담는다.
- POSD 자체 점검: `ZLinkJavaSpot`과 `ZLinkJavaSpotRouteBridge`는 단순 패스스루가 아니라 native resource
  close, no-data recv, request callback submit, router attach의 호출 순서를 숨긴다. `ZLinkJavaSpotCodec`도
  actor/spot 변환 지식을 factory 밖으로 숨기므로 이번 산출물 자체가 새 god helper가 되지는 않았다.
  다만 `ZLinkJavaSpotCodec`에 `JavaSpotNode` orchestration이나 actor join completion 조립까지 계속
  넣으면 얕은 범용 변환 모음이 될 수 있다. 다음 D7 POSD 대상은 `JavaStreamSocket`, `JavaSpotNode`,
  adapter shell, factory-level native actor 조립이며, spot node 변환은 별도 node 단위로 분리할지 다시
  검토한다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test`
  - `timeout 360s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    `DeliveryDispatch.Java`에서 transient port bind failure 후 1/3 재시도했고, 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AK. 2026-07-10 Java backend stream/spot node/adapters 분리 완료

- 완료: D7의 남은 중첩 구현을 package-private 파일로 승격했다. `ZLinkJavaStreamSocket`은 stream
  socket의 packet callback, transport monitor, stream frame send/reply, bound actor bind/unbind/relay
  호출 순서를 소유한다. `ZLinkJavaSpotNode`는 spot node peer 설정, spot/bridge 생성, actor create/lookup,
  actor join/leave/destroy, bound-session 전달, actor request/reply 조립을 소유한다.
- 완료: channel/spot/stream adapter shell은 각각 `ZLinkJavaChannelBackendAdapter`,
  `ZLinkJavaSpotBackendAdapter`, `ZLinkJavaStreamBackendAdapter`로 분리했다. `ZLinkJavaBackendAdapterFactory`는
  adapter factory 메서드와 monitoring adapter 생성만 남아 102줄에서 30줄로 줄었다. 초기 795줄 factory의
  중첩 adapter 분리 목표는 닫았다.
- 처리: framework native socket의 linger-zero 정책은 `ZLinkJavaSocketOptions`로 분리했다. 이 helper는
  임의의 범용 util이 아니라 Java backend adapter가 생성하는 framework socket의 공통 옵션 정책만 담는다.
- POSD 자체 점검: 새 `ZLinkJavaStreamSocket`과 `ZLinkJavaSpotNode`는 native 객체 호출을 그대로 넘기는
  얕은 wrapper가 아니라, stream frame 조립·monitor lifecycle·actor ref 변환·join completion 변환·no-bind
  reply 조립처럼 호출자가 몰라도 되는 native 세부 순서를 숨긴다. `ZLinkJavaSocketOptions`는 현재 한 가지
  정책만 담지만, 그 정책이 모든 backend socket 생성에 적용되는 설계 결정이라 별도 이름으로 숨기는 것이
  중복보다 낫다. 이후 unrelated socket policy를 이 파일에 계속 추가하면 새 POSD 대상이 되므로, socket
  생성 옵션에 한정한다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - `timeout 360s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.StreamSessionTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.ActorManagerTest --tests systems.zlink.framework.runtime.SpotManagerTest`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh` →
    `All Java/Kotlin samples passed`

## 부록 AL. 2026-07-10 Java actor retry scheduler 통합

- 완료: C4의 남은 actor retry loop를 `ZLinkActorRetryScheduler`로 모았다. `ZLinkActorClientRuntime`은
  actor route not-connected 재시도에서 deadline 계산과 route delay를 직접 소유하지 않고
  `retryRouteUntil`에 submit attempt와 retryable predicate를 넘긴다.
- 완료: stream bound actor bind retry는 `bindRelayUntilAccepted`로 모았다. `ZLinkBoundSessionRuntime`과
  `ZLinkBoundActor`는 각각 bind stage와 오류 분류 predicate만 넘긴다. relay submit 재시도는
  `submitRelayUntilAcceptedAfterRetry`를 사용해서, 실패 후 native binding을 한 번 더 시도한 뒤 다음 relay
  attempt를 예약하는 기존 순서를 유지한다.
- POSD 자체 점검: 새 scheduler 메서드는 호출자에게 deadline 계산, executor, retry delay를 노출하지 않는다.
  다만 retry 전 native binding을 다시 시도하는 정책은 `ZLinkBoundActor`의 도메인 결정이므로 scheduler가
  그 내용을 알지 않고 `Consumer<Runnable>`으로 재시도 예약 방식만 받는다. 이 경계는 범용 scheduler가
  actor binding 의미까지 흡수하는 것을 막는다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorRetrySchedulerTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.runtime.actors.ZLinkActorSubmitFaultsTest --tests systems.zlink.framework.runtime.host.EntrySpotActorDispatchTests`
  - `timeout 360s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.SessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.JsonSessionActorsRuntimeIntegrationTest --tests systems.zlink.framework.runtime.StreamSessionTest`
    중 첫 실행은 JVM SIGSEGV(`libzlink.so` monitor handler thread, `hs_err_pid1795.log`)로 중단됐고,
    재실행 1회는 `StreamSessionTest.streamActorGatewayCanLeaveUserSpotDuringRelayedRequest`의
    transient assertion 실패가 있었다. 해당 단일 테스트와 동일 integration filter를 다시 실행해 최종
    `BUILD SUCCESSFUL`을 확인했다.
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는
    `DeliveryDispatch.Java`와 `Bingo.Kotlin`에서 transient port bind retry가 있었고, 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AM. 2026-07-10 channel route-to-SPOT dispatcher 분리

- 완료: C6의 route-to-SPOT 경로를 대상 선택, submit 규칙, raw reply correlation으로 나눴다.
  `ZLinkChannelRuntime`은 route target을 고른 뒤 전송을 위임하고, route loop에서는 받은 frame이 raw
  bridge reply인지 `ZLinkSpotRouteBridgeRawReplies`에 묻는다.
- 처리: spot-router-node 경로의 send/request submit·reply·close 규칙은
  `ZLinkSpotRouterNodeDispatcher`가 소유한다. route-bridge send/request 제출과 request callback 완료
  규칙은 `ZLinkSpotRouteBridgeDispatcher`가 소유한다. route-bridge raw reply queue, 최근 완료 suppression,
  echo 판정, bridge reply payload 정규화는 `ZLinkSpotRouteBridgeRawReplies`가 소유한다.
- POSD 자체 점검: 새 클래스들은 서로 다른 지식을 소유한다. dispatcher는 native bridge 호출 순서와 retry
  규칙만 알고, raw reply 객체는 pending/recent correlation만 안다. `ZLinkChannelRuntime`에 남은
  package-private static helper는 같은 package 내부 구현 표면이며 public API가 아니다. helper가 더 늘거나
  raw reply 객체가 trace/reporting/handler dispatch까지 알게 되면 새 POSD 대상이 되므로, D2에서는
  helper 노출 축소와 route loop/drainer 분리를 별도로 다룬다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:compileJava`
  - `./gradlew --no-daemon :zlink-framework-core:test --tests '*ZLinkChannelRuntimeTest'`
  - `timeout 180s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.clientServerSpotRouteEgress_requestReplySucceeds`
  - `timeout 240s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_requestByRoutingIdSucceeds --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_missingRequestHandlerRepliesFrameworkError --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_nonInitiatorRequestUsesInboundProbeIdentity --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_scannedHandlerGroupRequestSucceeds --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_matchesRepliesByRequestSequenceWhenPacketNameIsShared --tests systems.zlink.framework.runtime.ChannelMessagingTest.routeMesh_sendByRoutingIdDispatchesToHandler`
  - `timeout 180s ./gradlew --no-daemon :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.channels.ZLinkRouteMeshInboundIdentityIntegrationTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/SpotService`, `e2e/ToActorMessaging`, `e2e/YieldDispatch`,
    `e2e-kotlin/SpotService`, `e2e-kotlin/ToActorMessaging`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh`는 첫 실행에서
    Kotlin TicTacToe가 native library 교체 순간을 읽어 `file too short`로 실패했다. 현재
    `core/build/lib/libzlink.so.8.6.3`이 정상 ELF임을 확인한 뒤 재실행했고,
    `TicTacToe.Java`와 `DeliveryDispatch.Kotlin`에서 transient port bind retry가 있었지만 최종
    `All Java/Kotlin samples passed`까지 성공했다.

## 부록 AN. 2026-07-10 stream core/connector wire 규약 정렬

- 완료: C1의 최소 해법으로 core와 stream-connector production 코드를 하나로 묶지 않고, 양쪽 wire codec이
  같은 header/frame bytes를 읽고 다시 쓸 수 있음을 테스트로 고정했다. `ZLinkStreamCoreWireInteropTest`는
  core header/frame을 connector가 decode/re-encode하고, connector header/frame을 core가
  decode/re-encode하는 양방향 교차 테스트다.
- 처리: core `ZLinkStreamHeader`/`ZLinkStreamHeaderCodec`은 send/request/response/error/control의
  request sequence와 codec 제약을 검증한다. `decodeOrPlain`의 plain fallback은 기존 core frame 처리
  호환성 때문에 유지하지만, structured stream header로 인식되는 bytes는 connector와 같은 의미 규칙을
  적용한다. core `ZLinkStreamHeaderCodecTest`에는 connector golden vector를 추가해 byte layout drift를
  바로 잡는다.
- POSD 자체 점검: wire format 지식은 아직 두 구현에 남아 있지만, connector production이 framework-core를
  참조하는 얕은 공유로 되돌아가지 않는다. 지금 산출물은 "중복을 테스트로 가둔 상태"이며, 다음에 실제
  코드 공유가 필요하면 runtime 없는 contracts-only 저수준 모듈로 분리해야 한다. connector가 다시
  `runtime.streams`를 import하거나 core가 connector 내부 protocol을 import하면 새 POSD 대상이다.
- 검증:
  - `./gradlew --no-daemon :zlink-framework-core:test --tests systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodecTest --tests systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodecTest`
  - `./gradlew --no-daemon :zlink-stream-connector:test --tests systems.zlink.stream.connector.ZLinkStreamWireProtocolTest --tests systems.zlink.stream.connector.ZLinkStreamCoreWireInteropTest --tests systems.zlink.stream.connector.JavaNodeStreamInteropTest`
  - `./gradlew --no-daemon :zlink-framework-core:test :zlink-framework-kotlin:test :zlink-framework-spring-boot-starter:test :zlink-framework-locations-redis:test :zlink-stream-connector:test :zlink-http-client:test :zlink-framework-codec-msgpack:test :zlink-framework-codec-protobuf:test`
  - 관련 e2e build만 실행: `e2e/YieldDispatch`, `e2e-kotlin/YieldDispatch`
  - `ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:6379 timeout 900s ./samples/run_samples.sh` →
    `All Java/Kotlin samples passed`

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
# e2e는 변경과 관련 있는 시나리오만 선택해서 실행한다. full e2e sweep은 기본 검증에서 돌리지 않는다.
./samples/run_samples.sh
```

R1/R2/D1/D2/C6 이후 특히: `SpotService`/`YieldDispatch`/`ToActorMessaging`(java+kotlin), route mesh 포함 `RegistryMessaging`. hot(벤치) 항목은 baseline vs patched 실측 무회귀 후에만 커밋. **검증 중 public contract 차이가 드러나면 다른 언어/E2E 요구만 근거로 Java/Kotlin public API를 바로 추가하지 말고 공통 spec/guide 또는 draft에 설계 후보로 분리**([[CLAUDE.md]]).
