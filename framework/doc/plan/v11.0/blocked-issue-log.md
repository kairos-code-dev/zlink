# RouteMesh 11.0.0 blocked issue log

이 문서는 [통합 execution ledger](route-mesh-11.0.0-execution-ledger.ko.md) §2.2의 자율 실행 중 발생한
blocked issue와 그 해결 내용을 기록한다. 진행 상태와 완료 증거의 단일 기준은 계속 ledger의 담당 행이며,
이 문서는 blocker의 원인과 수정 이력만 남긴다.

기록 규칙은 다음과 같다.

1. 문제를 발견한 즉시 `BLK-<3자리 번호>` 항목을 추가하고 담당 ID, 기준 revision, 증상과 실행 command를
   먼저 채운다. 해결을 기다렸다가 한꺼번에 기록하지 않는다.
2. 해결한 뒤 같은 항목의 근본 원인, 수정 내용과 변경 path, 재검증 command와 결과, 상태를 갱신한다.
3. 상태는 `조치 중`, `해결`, `차단`만 사용한다. `차단`은 ledger §2.2의 두 예외에만 쓰고 사유를 함께 남긴다.
4. ledger §2.2가 금지한 우회로 닫은 항목은 `해결`로 기록하지 않는다.
5. 담당 ledger row의 증거 칸에는 이 항목 ID만 참조하고 같은 내용을 양쪽에 중복 기록하지 않는다.
6. 여러 row가 같은 원인으로 막히면 항목 하나를 만들고 담당 ID 칸에 관련 ID를 모두 적는다.

## 2026-07-30 C++ ClientServer listener checkpoint

이 절은 아래 표의 `BLK-032`, `BLK-033`, `BLK-035` 진행 기록을 갱신한다.

- `BLK-032`는 해결했다. RouteChannel 제거 뒤 남은 channel messaging test를 정식 CMake target으로
  유지했으며 pending RouteChannel scenario는 다시 활성화하지 않았다.
- `BLK-033`은 해결했다. ClientServer Server는 Location discovery publication 여부와 관계없이 listener를
  bind한다. 같은 runtime이 listener 하나를 소유하며, automatic discovery를 사용한 경우에만 이미 bind한
  listener의 descriptor를 Location Store에 게시한다. Manual·automatic 경로를 위해 listener나 peer
  connection을 하나 더 만들지 않는다.
- `BLK-035`는 해결했다. `test_cpp_framework_channel_messaging`을 C++ formal runtime regression의
  internal build·test 단계에 등록했다.

Focused 검증은 다음과 같다.

```text
cmake --build framework/languages/cpp/build-v11-tests \
  --target test_cpp_framework_channel_messaging -j2
ctest --test-dir framework/languages/cpp/build-v11-tests \
  --output-on-failure -R '^test_cpp_framework_channel_messaging$'
```

결과는 focused `1/1 passed`, formal internal `5/5 passed`, 전체 formal command `10/10 passed`다.
이 test는 같은 process Client·Server, manual endpoint, request/reply와 handler dispatch를 함께 검증한다.
증거는
`.artifacts/v11/evidence/V11-M6A-CPP/result-clientserver-20260730.json`
(`sha256:1c8966c89ac494e283da16bc8a9b4890092a9d498d29acea4128e2ba02a6a1fb`)에
기록했다.

## 2026-07-30 C++ queue-free relocation restore checkpoint

- `BLK-051`: C++ canonical relocation은 accepted application record가 0개면
  `prepare_target`을 호출하지 않았다. Queue가 비어 있는 Actor·Spot도 policy에 따라 target factory와
  `Restore`를 실행해야 하므로 production 계약 위반이다.
- `maintenance_runtime_t::prepare_replay_source()`가 target 준비를 항상 먼저 실행하도록 고쳤다.
  Replay record가 없으면 target 준비 성공 뒤 source replay 등록만 생략한다. Wire frame, connection과
  public API는 추가하지 않았다.
- `test_cpp_framework_m6c_runtime`의 production vertical에 queue가 비어 있는 Actor를 추가했다. Target
  restore 1회, replay record 0개, authority publication 완료와 abort 0회를 함께 검증한다.

Focused M6C는 `1/1 passed`, formal internal은 `5/5 passed`, 전체 formal command는 `10/10 passed`다.
Candidate SHA-256은
`ad89686694df8a150900142c94bedd12a764875ae0e52126794cc03b23687f0e`, result SHA-256은
`7579b98cf6315f1556b1eab8c553cf8903f673050aebd4fd66ee1873db916382`다.
상태는 `해결`이다. 이 항목은 `BLK-044` 전체가 아니라 queue-free target restore production gap만
닫는다.

## 2026-07-28 .NET canonical close checkpoint

- `BLK-044`: command `40→30 offer→30 accept→41`에서 User Spot은 standalone capacity fence를 만들지
  않는다. Seal 시점의 단일 root와 participant 전체 typed bundle로 aggregate prepare를 한 번 실행한다.
  Standalone Actor와 Instance Spot만 단일 capacity fence를 사용한다. Focused User Spot 1/1과 canonical
  reservation owner 28/28이 통과했다.
- `BLK-046`: actual `ReconcilePublishedStageAsync`를 호출하는 Pending·Completed 2/2가 통과했다.
  Canonical decode 뒤 빈 `CompletionPayload`를 authority의 `SourceCleanupState`로 다시 구성한다. Provider
  authority 31/31, parallel tree I/O 10/10, canonical reservation owner 28/28, relocation runtime 144/144와
  Framework build warning·error 0이 통과했다. 이 수치는 아래 BLK-046 행의 이전 26/26·142/142 checkpoint를
  대체한다.
- `BLK-048`: `SpotWideTree_UsesBoundedParallelIo_AndPublishesRootLast`가 Actor 100개×64 KiB에서 provider
  write·read concurrency 2 이상, 최대 64 operation·256 MiB와 root-last publication을 검증했다.
  `RelocationTreeParallelIoTests` 10/10이 통과했다. Fresh-process `ACTORS-10`·`ACTORS-100` 1초 E2E는 남아 있다.

## 2026-07-26 Node command 31 production checkpoint

- `BLK-042`: Node production host가 `ZLRH1`·`node:v8`·binding request wrapper를 제거하고 command
  30·31·32·34·35·40·41을 raw `sendToNode`와 bounded pending correlation table에 연결했다. Immutable root
  write, remote prepare, authority CAS 순서를 고정하고 target은 shared root CRC32C와 exact source·candidate fence를
  검증한다. Non-empty Actor accepted queue는 canonical command 31 record로 remote target에서 replay하고 command
  32 high-water ACK 뒤 seal한다. Two-owner focused test와 M6C 61/61이 통과했다. Active User Spot aggregate의
  production slice는 진행됐지만 standalone Actor·Instance Spot, bound Session replay와 pre-commit abort target
  cleanup이 남아 있어 상태는 `조치 중`이다.
- `BLK-044`: Node command 31은 frozen-record kind 1~14 전체 closed union, source kind 1~4,
  operation·reply route matrix와 phase 0~9를 검증하며 canonical bytes를 보존한다. Shared fixture의 inner
  relocation-control operation ID는 authoritative zero 값으로 맞췄다. Workspace typecheck·build, 변경 source
  scoped ESLint와 M6C 61/61이 통과했다. Command 33·46의 independent raw send, authenticated pending ACK,
  durable completion·exact source lease fence와 mixed-language process 검증은 남아 있어 상태는 `조치 중`이다.
- `BLK-044` Node 후속 갱신: 앞 문단에서 남겼던 command 33·46 production 연결을 완료했다. 두 command는
  일반 relocation request/response union에서 제거했으며 각각 independent raw `sendToNode` frame으로 송신한다.
  Pending ACK는 target RID·operation·relocation·target coordinator를 key로 사용한다. Original request source
  owner·lease·RID·lifecycle은 terminal completion을 `delivery=pending`으로 successor root에 먼저 CAS한 뒤 다시
  읽어 command 33과 pending expected fence를 만든다. Target coordinator는 current target owner/node와 current
  authority store version에서 별도로 고정한다. Source는 coordinator를 authenticated target descriptor와 current
  relocation authority에 대조하고 request source를 local reply capability·current source owner에 독립적으로
  대조한다. ACK 유실 retry와 `alreadyTerminal`, source route collision, admitted generation·authority version
  mismatch, exact source lease expiry가 focused two-owner test에서 통과했다. Command 40은 root fingerprint와 실제
  target replay budget만 저장하는 side-effect-free offer로 바뀌었고 exact source accept 뒤에만 factory·Restore를
  single-flight 실행한다. Offer 전 factory·Restore 0회와 zero-work non-zero capacity도 검증했다. Node M6C
  62/62, typecheck·build, scoped ESLint가 통과했다. Node private relay wrapper는 제거됐지만 mixed-language process
  E2E와 다른 runtime의 production 전환이 남아 있어 `BLK-044` 상태는 `조치 중`이다.
- `BLK-044` Node 재검증(2026-07-30): production host가 command 33·46을 shared service-wire codec과
  raw `sendToNode`로 처리하며 private relay wrapper는 남아 있지 않다. 수신 peer RID는
  `ReceiveRecord.sourceNodeRid`에서 얻고, pending ACK는 target RID·operation·relocation·coordinator와
  durable source owner·lease·node·lifecycle generation을 함께 검증한다. Shared golden 1/1,
  ACK loss·retry·duplicate terminal·stale source fence focused 2/2, 전체 M6C 74/74와
  정식 `M6-RUNTIME` 7/7, `ROW-GATE` files 337·commands 7이 통과했다. Node command 33·46
  범위는 해결됐지만 mixed-language process E2E와 다른 runtime의 잔여 production 전환은 별도 조건이므로
  `BLK-044` 전체 상태는 `조치 중`을 유지한다.
- `BLK-044` Node target lifecycle 후속 갱신: command 40 offer는 capacity를 소비하지 않고 command 30 exact
  accept가 실제 64-unit·256 MiB permit을 획득한다. User Spot은 participant가 하나여도 aggregate
  `PrepareAggregate`를 사용하며 standalone Actor·Instance Spot만 단일 relocation capacity fence를 사용한다.
  Unknown Store 응답은 처음 고정한 exact request 또는 aggregate plan만 재실행해 reconcile하고, cleanup 중
  authority를 다시 읽어 다른 reservation을 만들지 않는다. Command 30 accept와 command 31 prepared에서 target
  precommit timer를 다시 설정하고, commit 뒤 abort를 금지하며 success·abort·shutdown에서 permit·timer·pending
  control과 relay를 해제한다. Live offer·stage와 terminal tombstone은 각각 1,024개 한도를 갖고 tombstone은 5분
  뒤 제거한다. Runtime dispose는 Location lifecycle·Store stop보다 먼저 실행한다. `npm run lint`·`build`·
  `typecheck`, focused host relocation 2/2와 M6C 62/62가 통과했다. 이 checkpoint는 target reservation lifetime을
  닫지만 bound Session ACK와 mixed-language process E2E는 아직 남아 있어 상태는 `조치 중`이다.
- `BLK-044` C++ 후속 갱신: command 33·46을 request correlation과 분리한 독립 raw
  infrastructure frame으로 처리하는 terminal coordinator를 실제 `public_host_runtime_t::dispatch_ready`에
  연결했다. Request-source owner는 `RelocationId`·coordinator·target attempt·participant sequence,
  128-bit operation ID와 별도 reply route를 exact 등록한다. Target은 original source owner·lease·RID·lifecycle과
  terminal payload를 bounded pending record로 보존하고 ACK를 받을 때까지 retry한다. 첫 terminal만 completion
  callback을 실행하며 같은 payload의 재전송에는 `alreadyTerminal`을 반환하고 다른 payload의 같은 operation은
  거부한다. ACK 유실 뒤 재전송·중복 terminal, exact source lease expiry, 1,024-record·64 MiB bound와 24시간
  tombstone retention을 focused test에서 검증했다. Frozen application decoder summary도 canonical bytes를
  변경하지 않고 Spot·Actor payload를 target staging에 제공한다. Codec·M6A·M6B·M6C executable과 전체
  `zlink_framework` build가 통과했다. 다만 legacy `maintenance_runtime_t` transfer envelope은 아직
  `RelocationId`·coordinator·participant sequence를 소유하지 않으므로 실제 factory·Restore가 이 coordinator를
  자동 등록하지 않고, request-source owner도 process restart 뒤 terminal registration을 복구하지 못한다.
  따라서 이번 증거는 wire owner와 bounded terminal state checkpoint이며 C++ production relocation 완료나
  `BLK-044` 해결로 판정하지 않는다.
- `BLK-044`의 .NET command 40 target 입력은 Location Store current authority의 Active allocation과 Actor
  authority scan으로 exact 복원한다. 공통 spec은 command `40` exact inventory, command `30` target capacity
  offer(empty participants), command `30` source accept(exact prepare participants), command `41` reservation ACK
  순서를 확정했다. owner·lease는 ingress Location read와 admitted descriptor로 검증하며 RID에서 추정하지
  않는다. .NET은 이 state machine을 raw production ingress와 Retire scheduler의 가장 이른 private stage 앞에
  연결했다. Offer는 side effect 없이 budget만 반환하고 exact source accept 뒤 runtime permit과 Location capacity
  fence를 single-flight로 예약한다. 2026-07-28 재검증에서 User Spot은 standalone capacity fence를 만들지 않고
  seal 시점의 단일 root와 participant 전체 typed bundle로 `PrepareAggregate`를 한 번 실행한다. Standalone
  Actor·Instance Spot만 단일 capacity fence를 사용한다. 동일 accept retry는 같은 결과에 합류하며 participant
  mismatch는 `InvalidDataException`으로 거부한다. Focused User Spot 1/1, canonical reservation owner 28/28,
  relocation runtime 144/144가 통과했다. Production source의 `.stage.v1`·`.publish.v1`·`.abort.v1`·
  `.held-relay.v1` packet symbol은 0개다. Mixed-language process E2E가 남아 있어 상태는 `조치 중`이다.
- `BLK-044` .NET canonical wire 후속 갱신: private `.stage.v1`·`.publish.v1`·`.abort.v1`·
  `.held-relay.v1` DTO와 handler·dispatcher를 제거하고 command `31`·`32`·`34`·`35`를 raw production
  ingress와 Retire source·target state machine에 연결했다. Target reservation owner가 exact peer·lifecycle,
  coordinator, operation, participant high-water, immutable root와 authority publication을 검증한다. User Spot
  aggregate는 단일 immutable root를 사용하고 standalone Instance Spot은 capacity fence handoff·commit·abort와
  terminal slot cleanup을 수행한다. Framework·UnitTests build warning·error 0, focused 103/103, 전체 Unit
  987/987, Contract 65/65가 통과했으며 private wire symbol scan은 0건이다. Standalone Actor canonical
  maintenance owner와 mixed-language process E2E가 남아 있으므로 상태는 `조치 중`으로 유지한다.

| 항목 | 담당 ID·lane | 기준 revision | 증상과 실행 command | 근본 원인 | 수정 내용과 변경 path | 재검증 command와 결과 | 상태 |
|---|---|---|---|---|---|---|---|
| `BLK-001` | `V11-M6A-DN`, `V11-M6B-DN`; .NET lane | working tree(2026-07-25, base `a67bc4419d`) | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj`에서 `EntrySpotActorDispatchTests.Current_Spot_Publish_Emits_Sent_With_Spot_Rid_And_Current_Flow`와 `..External_Spot_Publish_Emits_Internal_Publisher_Rid_Without_Correlation`이 flow log 파일 부재로 실패했다. | v11 정식 spec `26-message-flow-tracing.ko.md` §3·§9는 Logical Multicast·classic fanout publish가 message-flow event를 만들지 않도록 못박는다. Runtime은 이 규칙을 이미 지키고 있고, 두 unit test가 v10 시절의 `phase=sent` 기대를 그대로 들고 있었다. C++ reference에도 publish `sent` 기대는 없다. | Runtime 동작은 그대로 두고 두 test의 flow-log 기대만 spec 규칙(“publish는 event를 만들지 않는다”)으로 교체했다. Envelope header의 flow·correlation·publisher identity 검증은 유지했다. `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/EntrySpotActorDispatchTests.cs` | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --filter "FullyQualifiedName~EntrySpotActorDispatchTests"` → 71 passed / 1 failed(남은 1건은 이 항목과 무관한 `Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply` stream ingress gap). | 해결 |
| `BLK-002` | `V11-M6A-DN`; .NET lane | working tree(2026-07-25, base `a67bc4419d`) | `HttpExecutionSchedulerTests.Captured_http_callback_is_posted_as_a_new_serial_turn`이 5초 timeout으로 실패했다. | `859fcf07fe`가 `ZLinkSpotHttpExecutionScheduler.Capture()`를 `ZLinkApplicationExecutionContext.Current is { YieldAllowed: true }` 조건으로 강화했다(`05-async-execution-policy.ko.md`의 “Yield는 SpotWide User Spot·Instance Spot application callback에서만 유효” 규칙). Test는 bare serial queue turn만 만들어 capture가 `null`을 반환했고, 그 결과 delegate가 예외로 끝나 신호가 오지 않았다. | Runtime 강화는 유지하고 test가 yield 허용 application scope를 push하도록 맞췄다. `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/HttpExecutionSchedulerTests.cs` | `dotnet test --filter "FullyQualifiedName~HttpExecutionSchedulerTests"` → 1/1 통과. | 해결 |
| `BLK-003` | `V11-M6-DEFERRED-JOIN-DN`; .NET lane | working tree(2026-07-25, base `a67bc4419d`) | `EntrySpotActorDispatchTests.Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply`가 join admission callback에서 flow context를 관찰하지 못했다(`probe.JoinFlow == null`). | `ZLinkDeferredActorJoin.Activate()`가 join을 detached로 실행해 제출 callback의 causal flow(AsyncLocal)를 잃었다. 추가로 detached 실행이라 dispatch 반환 시점에 join 완료 순서가 보장되지 않는다. | Flow 손실은 근본 수정했다. 제출 시점 `ZLinkFlowContext.Current`를 capture해 `RunAsync`에서 같은 flow로 재진입한다. `framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkDeferredActorJoin.cs` | 단독 실행 `--filter "FullyQualifiedName~Flowless_Actor_Stream_Ingress"` → 1/1 통과. §2.3 교차 리뷰로 네 언어 활성화 형태를 비교해 ordering 축 최선안(제출 실행 문맥에 등록 순서대로 post)을 확정하고 .NET을 `ZLinkSerialTurn.TryPost`로 정렬했다. Deferred Join focused test 7/7 통과. 다만 전체 병렬 실행의 해당 1건은 여전히 실패한다. 원인은 순서가 아니라 '완료 가시성' 축이다. Java만 deferred join을 dispatch stage에 체인해 호출자가 완료를 관찰할 수 있고 C++·Node·.NET은 terminal 이후 비동기로 실행한다. 완료 가시성 축은 비결합(handler terminal 이후 비동기 실행)을 기준안으로 확정했다. C++·Node·.NET 세 언어가 비결합이고, Node는 "terminal reply를 deferred 실패가 대체할 수 없다"는 근거를 코드에 명시하며, 결합형은 원격 join 완료가 dispatch 수명과 mailbox turn을 잡아두는 위험이 있다. 이 기준에 따라 test가 관찰 전에 join 완료를 기다리도록 맞췄고(assertion은 모두 유지) `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/EntrySpotActorExactRequestMessageFlowTests.cs`를 수정했다. 전체 suite 789/791로 이 항목의 실패가 사라졌다. Java가 intent를 handler stage에 체인하는 결합형이라 남은 동형 수렴 대상이며 JVM lane row에 후속으로 남긴다. | 해결 |
| `BLK-005` | `V11-M6B-NODE`, `V11-M6-DEFERRED-JOIN-NODE`; Node.js lane | working tree(2026-07-25, base `956d2696ba`) | `npm test`의 `test/contract/actor-manager.test.js`가 18건 실패했다(다른 test 파일은 통과). | 원인이 세 갈래다. (1) `lifecycleSource`의 오류 경로가 `actor.context`를 가드 없이 역참조해 `ZLinkConfigurationException` 대신 `TypeError`가 났다. (2) transfer·join·Entry Spot 통지 내부 경로가 public Actor Context의 `actorRef` projection에 의존해 Context-only identity 계약과 충돌했다. (3) 나머지 다수는 test가 `joinSpot(...).timeout(...).submit()`을 호출하는데, deferred Join amendment 이후 Node exact interface(`05-actors.ko.md`)의 `ZLinkActorJoinCall`은 `timeout(timeoutMs)`와 `defer()`만 정의한다. 즉 amendment 이전 API를 든 stale test다. | (1)·(2)는 근본 수정했다. 오류 경로가 Context 부재를 안전하게 보고하고, runtime identity는 lifecycle snapshot symbol과 private Actor state에서 읽도록 정리했다. Public `DefaultZLinkActorContext.actorRef` projection은 제거했다. `framework/languages/node/packages/framework/src/runtime/actors/actor-lifecycle-snapshot.ts`, `.../actor-context.ts`. (3)은 exact interface에 없는 `submit()` 기대를 amended `defer()` 계약으로 옮기는 test 정렬이 필요하며 아직 진행하지 않았다. | `node --test test/contract/actor-manager.test.js` → 50 pass/19 fail에서 53 pass/16 fail로 개선했다. (3)의 join call 9곳을 `submitDeferredActorJoin(actor, call)` 헬퍼(파일에 이미 있었으나 호출부가 옛 API로 남아 있었다)로 옮겼고, deferred Join이 절대 deadline을 유지하므로 coordinator가 받는 timeout을 정확한 ms 대신 `(0, 설정값]` 범위로 검증하도록 고쳤다(네 언어 runtime이 모두 remaining을 넘긴다). 이어서 completion 계약 정렬을 마쳤다. 헬퍼가 `{accepted,...}` 대신 completion 객체(`status`/`actor`/`reply`/`rejection`)를 그대로 넘기고, raw reply 수명은 runtime이 소유하므로 test의 `reply.close()` 호출을 제거했다. 최종 55 pass/14 fail(시작 50/19). `actor-manager.test.js`는 69/69로 정리됐다. 현재 candidate에서는 Actor Manager 74건과 Object Context 3건을 함께 실행해 77/77이 통과했다. Runtime 결함 다섯 건을 근본 수정했다. join reply를 등록된 serializer로 decode, RouteMesh membership 단일 해석, deferred join을 다음 큐 turn보다 먼저 실행(C++ deferred-after-active와 동형), 선택 항목인 `reply` key 생략, 그리고 committed target SPOT을 RoutingId 값으로 비교(참조 비교라 일치하는 SPOT을 거부하고 ownership publish 전에 claim이 실패했다). Test 쪽은 amendment 이후 계약(`defer()`, `joinEntrySpot(request)`, completion `status`, framework context 소유 identity, 절대 deadline)으로 정렬했다. 이어서 `actor-transfer-authority.test.js` 3/3과 backend contract 30/34까지 진행했다. Core service projection이 binding에서 빠지면서 service runtime enum(`ReceiveKind`·`ReadyOwnerKind`·`ReadyDomain`·`ActorTransferPhase`·`ActorTransferRole`)을 framework가 소유하는데 foundation barrel이 재export하지 않아 소비자가 binding에서 `undefined`를 읽던 문제도 고쳤다. 남은 backend contract 4건은 단일 MeshNode가 자기 ChannelName으로 보낼 때 `NotConnected`가 나오는 시나리오다. §2.3 교차 확인 결과 이는 Node 구현 gap이 아니다. C++ `service_topology_registry_t::select`도 admitted peer만 후보로 보고 local descriptor를 후보에 넣지 않으며, `08-channel-messaging.ko.md` §3.2는 RouteMesh 후보를 같은 ChannelName의 Server membership으로 정의하고 local 우회 경로를 금지한다. 즉 peer 없는 단일 node는 후보가 없다. 자기 endpoint로 self-connect를 시도하면 transport가 `send failed(code 2)`로 거부한다. 따라서 이 4건은 두 node로 구성한 membership 위에서 pump를 검증하도록 test scenario를 다시 세워야 하며, 그 재구성은 M6A Node lane에서 진행한다. | 해결 |
| `BLK-006` | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE`; contract lane 판정 필요 | working tree(2026-07-25, base `2f7ea9caab`) | Node backend contract 4건이 단일 MeshNode의 자기 ChannelName 전송에서 `SubmitResult.NotConnected`로 실패한다. | `08-channel-messaging.ko.md` §3.2는 RouteMesh 후보를 “같은 ChannelName의 Server membership”으로 정의하고, 바로 아래 문단은 ClientServer에서 local Server도 remote와 같은 후보이며 local 우회 경로를 제공하지 않는다고 못박는다. 그런데 RouteMesh 경로에서 자기 node가 그 ChannelName의 member일 때 후보에 들어가는지는 명시가 없다. 현재 구현은 네 언어 모두 admitted peer만 후보로 본다(C++ `service_topology_registry_t::select`, Node `ServiceTopologyRegistry.selectChannel`). 즉 test는 자기 자신을 후보로 기대하고 구현은 제외한다. 계약 판정 완료: RouteMesh는 자기 자신을 후보로 넣지 않는다(제외가 정본). 근거 세 가지가 같은 방향을 가리킨다. (1) RouteMesh 후보는 `07-channel-topology.ko.md` §4.2가 descriptor에 게시하는 Server membership인데, 그 문단은 이 set이 "모든 local Channel 설정이 아니라 remote target이 될 수 있는 Server membership만" 나타낸다고 명시한다. (2) 같은 문서 §4.2.1은 선택된 target에 기존 RouteMesh peer 연결로 보내며 Channel 등록이 새 socket을 만들지 않는다고 못박는데, MeshNode는 자기 자신과 peer 연결을 맺지 않는다. (3) 같은 §4.2.1이 Logical Multicast를 remote Server membership으로 명시한다. `08-channel-messaging.ko.md`의 local 후보 문단이 ClientServer로 한정된 것도 ClientServer에는 listener bind와 DEALER→ROUTER라는 성립 기전이 있기 때문이며, RouteMesh에는 그 기전이 없다. self-connect가 transport에서 code 2로 거부된 실측도 이와 일치한다. | 네 언어 runtime은 admitted peer만 후보로 보고 있어 이미 정본과 일치하므로 구현을 바꾸지 않는다. `08-channel-messaging.ko.md` §3.2에 RouteMesh 자기 후보 제외를 명시하는 문단을 추가해 침묵을 없앴다. 남은 조치는 Node backend contract 4건을 두 node membership scenario로 재구성하는 것이다(판정 (b) 경로). | 정식 spec에 명문화한 뒤 Node fixture 세 건을 실제 TCP peer 두 개의 admission·remote Channel dispatch로 재구성하고 node-direct fixture의 제거된 `routerChannelId` 기대를 `meshName`과 비어 있는 `channelName`으로 바꿨다. `node --test --test-reporter=spec test/contract/backend-contract.test.js`가 34/34로 통과했다. | 해결 |
| `BLK-007` | `V11-M6A-CPP`; C++ lane | working tree(2026-07-25, base `9b5ff0f1a2`) | 이관한 `zlink_cpp_framework_mesh_node_vertical_test`의 cross-process 구간에서 두 프로세스가 서로 admission되지 않는다. 진단 출력은 `rid=vertical-c peers=0 state=1`, `rid=vertical-b peers=0 state=1`이다. | `raw_mesh_node_owner_t`는 peer admission의 시작인 `hello`를 socket monitor의 `connection_ready` 이벤트에서만 보낸다(`raw_mesh_node_owner.cpp` 1149행 부근). 따라서 monitor 이벤트가 오지 않으면 connect 자체는 성공해도 admission이 영원히 일어나지 않는다. 앞선 네 단계(public runtime surface, drain barrier, multi-mesh drain, local submit bridge)는 모두 통과하므로 runtime 기동과 Location 연동은 정상이다. | 근본 원인은 pump 책임이었다. Transport는 socket monitor를 `public_host_runtime_t::dispatch_ready` 안에서만 배수하므로, dispatch를 돌리지 않고 admission만 기다리는 대기 루프에서는 `connection_ready`가 영원히 소비되지 않는다. Host service는 production에서 dispatch loop를 돌리므로 정상 동작한다. 대기 helper가 host service와 같은 방식으로 node를 pump하도록 고쳤다. `framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_mesh_node_vertical.cpp` | `ctest -R "^(test_cpp_framework_m6[abc]_runtime|zlink_cpp_framework_mesh_node_vertical_test)$"` → 4/4 통과. vertical test는 cross-process 구간까지 전 구간 통과한다. | 해결 |
| `BLK-004` | `V11-M6A-DN`; .NET lane(원인은 `bindings/dotnet` 소유) | working tree(2026-07-25, base `a67bc4419d`) | `ClientServerChannelRuntimeTests.BackendWrappers_DeliverUnsolicitedLivenessProbe`와 `..ManualClient_RequestUsesDedicatedDealerAndServerRouter`가 실패한다. 후자의 진단값은 `serverProbe=1, clientProbe=0`으로 server가 보낸 liveness probe를 client가 못 받는다. | 근본 원인은 Framework .NET code도 Core도 아니었다. `bindings/dotnet/native/linux-x64/`가 `libzlink.so.10.6.0`을 vendoring하고 있었고, 이 바이너리는 Core의 dealer-dispatch 수정(`11ca0f5340`, `d49f60d5a0`)보다 앞선 빌드다. 아래 진단 5종은 전부 수정 이전 바이너리 위에서 실행됐기 때문에 결함 위치를 managed 경로로 잘못 지목했다. 진단 기록은 보존한다. 임시 진단 test 3종으로 계층을 분리했다. (1) 선행 request 없이 ROUTER→DEALER unsolicited send: **통과**. (2) DEALER가 async request를 한 번 완료한 뒤 같은 unsolicited send: **실패**. (3) binding의 private completion poller가 idle 종료(1s)한 뒤 재시도해도 **실패**. 즉 pump race가 아니라, async request를 한 번 수행한 DEALER는 이후 raw record를 `Recv`(`zlink_dealer_recv_part`)로 더 이상 surface하지 않는다. Core는 같은 시나리오를 통과 계약으로 갖고 있고 실제로도 통과한다. 현재 build로 `core/build/bin/test_zmp_request_reply`를 실행하면 22 tests 0 failures이며 `test_dealer_receives_unsolicited_message_after_request_reply`와 `test_dispatcher_dealer_generic_recv_and_poller_preserve_raw_queue`가 모두 PASS다. `06-dealer.ko.md` §3·§6도 raw record 수신을 선행 request 여부로 제한하지 않는다. 따라서 결함은 Core가 아니라 .NET binding 경로에 있다. 네 번째 진단으로 routing id 가설도 배제했다. raw record에서 학습한 peer rid로 보내도 request 이후에는 동일하게 도착하지 않으므로, request record와 raw record의 rid 차이가 원인이 아니다. 다섯 번째 진단으로 private completion poller 가설도 배제했다. 요청 전에 public `Zlink.CreatePoller()`로 dealer를 등록해 external progress를 선점하면 binding의 private pump(`RequestProgressPump`, `ZLINK_POLLCOMPLETION`)가 뜨지 않는데, 이 구성에서도 request 완료 뒤 unsolicited raw record가 `Recv`로 오지 않았다. 같은 진단에서 router는 자기 probe를 되받지 않으므로 send는 socket을 떠났다. Core 쪽 `zlink_poller_add`의 `ZLINK_POLLCOMPLETION` 등록은 별도 completion signal socket을 쓰고 completion queue는 reply completion만 담으므로 raw record를 가로채지 않는다. 따라서 남은 용의자는 .NET `DealerSocket.Recv`가 `zlink_dealer_recv_part` 결과(message type·request seq)를 해석하는 경로이며, 다음 단계는 binding 내부에서 native 반환값을 직접 관찰하는 것이다(framework test project는 internal API에 접근할 수 없어 bindings lane에서 수행한다). | Core 11 아티팩트(`libzlink.so.11.0.0`)로 교체하고 `libzlink.so`·`libzlink.so.11` symlink를 재지정했다. `bindings/dotnet/tests/Zlink.Tests/test_request_reply.cs`에 dealer-dispatch 경로 직접 커버리지를 추가해, 다음 아티팩트 회귀가 framework 레벨의 설명 불가 flake가 아니라 여기서 실패하도록 했다. `bindings/dotnet/native/linux-x64/`, `bindings/dotnet/tests/Zlink.Tests/test_request_reply.cs` (commit `9cfb957a01`) | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj` → 791 passed / 0 failed (교체 전 789/791). `ClientServerChannelRuntimeTests` 2건 모두 통과한다. | 해결 |
| `BLK-008` | `V11-M6B-NODE`; Node.js lane(계약 판정 완료) | working tree(2026-07-25, base `e5ca0ae65e`) | `node --test test/contract/spot-manager.test.js`의 `user Spot join runs source leave on the caller turn without target-to-source deadlock`이 `onActorJoin`의 첫 인자를 plain `actorId: string`으로 기대해 실패했다(`admit:room-b:[object Object]`). | Node runtime이 정식 spec을 어긴 drift였다. 다섯 언어 정식 spec이 모두 identity 형태를 못박는다. node `04-spots.ko.md:65` `onActorJoin(actorId: string, request: ZLinkMessage)`, dotnet `05-spots.ko.md:225`와 .NET 기준 구현 `Contracts/Spots/ZLinkSpot.cs:152` `OnActorJoinAsync(string actorId, ZLinkMessage, CancellationToken)`, cpp `04-spots.ko.md:108`·`133` `on_actor_join(std::string_view actor_id, const message_t &)`, java `spots.ko.md:403`·kotlin `spots.ko.md:306` `onActorJoin(java.lang.String, ZLinkMessage)`. 즉 spec 5/5와 §2.3 기준 구현인 .NET이 일치하고 Node runtime 하나만 달랐다. `ZLinkActorJoinRequest`는 어느 spec 문서에도 없다. 개수와 별개로 형태 자체도 틀렸다. 이 객체는 `expectedMembershipEpoch`(runtime이 소유하는 fencing 상태)를 담고 있어(`contracts/RouteMesh/Contracts.ts:15`) application admission callback에 넘기면 정보 은닉이 깨지고 caller 부담이 늘어난다. 그래서 이 객체 형태는 나머지 네 언어로 승격할 후보가 아니다. Guide 문서에는 `onActorJoin` 언급이 없어 3자 대조의 세 번째 축은 판정에 기여하지 않았다. | Spec 방향으로 정렬했다. 공개 계약 `packages/framework/src/contracts/Spots/ZLinkSpot.ts`의 `ZLinkUserSpotActorLifecycle.onActorJoin`을 `(actorId: string, request: ZLinkMessage)`로 바꾸고, 내부 호출부 다섯 곳(`runtime/spots/index.ts`, `spot-native-actor-join-admission.ts`, `spot-routed-actor-admission.ts`, `spot-routed-frame-dispatch.ts`, `spot-actor-join-dispatch.ts`)과 `runtime/actors/spot-actor-dispatch.ts`를 identity 인자로 옮겼다. 마지막 경로는 framework snapshot에서 id를 뽑는 `actorJoinIdentity()`(`runtime/actors/actor-lifecycle-snapshot.ts`)를 쓰므로 framework identity가 없는 actor에 대한 기존 `ZLinkConfigurationException` 실패 의미가 그대로 유지된다. `spot-routed-actor-admission.ts`와 `runtime/spots/index.ts`의 ActorRef 부재 guard는 의도적으로 남겼다. routed transfer의 commit 단계가 그 ActorRef로 actor를 materialize하므로 admission 전에 실패해야 한다. TS 샘플 11개도 spec 형태로 옮겼다. Bingo room·SupportChat conversation·TicTacToe game 세 곳은 admission에서 ActorRef를 읽고 있었는데, .NET 기준 구현(`samples/Bingo/.../BingoRoom.cs`, `TicTacToeGame.cs`)과 동일하게 admission은 actorId만 보고 ActorRef는 membership callback(`onJoinedActor`)에서 확보하도록 바꿨다. Test는 assertion·scenario를 하나도 지우지 않고 callback 인자 형태만 옮겼다. `sample-regression.test.js:1851`이 drift된 텍스트 `onActorJoin(actor: ZLinkActorJoinRequest`를 pin하고 있어 spec 텍스트로 다시 pin했다. `ZLinkActorJoinRequest` 타입과 `createActorJoinRequest()`는 그대로 둔다. shipped 공개 표면 제거는 계약 lane 결정이며 이번 정렬 범위가 아니다(`BLK-014`). 이번 항목에 섞지 않고 남긴 잔여는 셋이다. (1) `onLeaveActor`도 같은 계열이지만 `entrySpotCallbacks.onLeaveActor` 경로는 `spot-actor-membership.ts:74`에서 raw `ZLinkActor`를 그대로 넘겨(`createActorMembership` 미적용) 별도 경로다. (2) membership callback 자체가 아직 spec과 다르다. node `04-spots.ko.md`의 `ZLinkSpotActorMembershipLifecycle<TActor extends ZLinkActor>`는 `onJoinedActor(actor: TActor)`·`onLeaveActor(actor: TActor)`인데 Node 구현은 non-generic이고 `ZLinkActorMembership`을 넘긴다. 고친 줄 바로 옆 문단이지만 admission 계약과 별개의 family라 의도적으로 범위 밖에 뒀다. (3) `e2e/`의 14개 파일(`ObservabilityOps`·`SpotService`·`AutomaticTurnDispatch`·`ToActorMessaging`·`SpotActorTransfer`·`RuntimeMonitoring`)은 아직 객체 형태를 구현한다. root typecheck graph에 없고 어떤 gate도 compile하지 않으며 ledger §2의 11·13에 따라 E2E source는 amendment 전까지 불변 입력이라 손대지 않았다. 필요한 변경은 `actor.actor.actorId` → `actorId` 기계적 치환뿐이므로 M7 E2E 재활성화 구간에서 나머지 v11 drift와 함께 처리한다. | `npm run build` 뒤 파일 단위 실행. `node --test test/contract/spot-manager.test.js` → 58/58(변경 전 55/58), `actor-manager.test.js` → 69/69(변경 전 68/69). 인접 회귀: `actor-transfer-authority` 3/3, `spot-activation-state` 4/4, `entry-spot-serial-dispatch` 22/22, `location-key-codec` 2/2로 모두 baseline 유지. 샘플 gate는 baseline과 동일하다. `sample-regression.test.js` 44 pass/3 fail(변경 전과 같은 GameQuest 2건과 `run_samples.sh` 1건), `sample-spot-lifecycle.test.js` 0 pass/3 fail(변경 전과 같음, 실패 원인은 모두 `context.handlers`·`ZLinkHandlerContext`·`ZLinkActorJoinSpotCall.submit` 같은 기존 샘플 drift이며 join admission이 아니다). `contract-surface.test.js` 26/28도 baseline 그대로다(남은 2건은 handler catalog 관련으로 이 항목과 무관). 전체 root `npm run typecheck` 0 error. 샘플별 `tsc -p tsconfig.json --noEmit`에서 수정한 선언 위치의 오류는 없다. 편집한 샘플을 읽는 sample gate도 확인했다. `sample-supportchat-open-state-gate` 1/1, `sample-tictactoe-internal-join-contract-gate` 1/1, `sample-zoneworld-gate` 9/9 통과. `sample-bingo-yield-record-gate`는 0/1 실패지만 원인은 이 항목과 무관한 기존 drift다(gate는 2인자 `requestToChannel(SampleNames.apiChannel, ...)`을 pin하는데 샘플은 3인자 형태를 쓰며, 이 호출부는 이번 커밋 diff에 없다). | 해결 |
| `BLK-009` | `V11-M6A-NODE`, `V11-M6B-NODE`; Node.js lane | working tree(2026-07-25, base `8a228601dd`) | `node --test` file-by-file sweep에서 5개 파일이 hang한다: `channel-client.test.js`, `client-server-location-runtime.test.js`, `stream-connector.test.js`, `stream-connector-codecs.test.js`, `stream-session-runtime.test.js`. 병렬 sweep(`-P 6`)에서 4개가 `exit=124`(150s timeout)였고, 포트 경합 가설을 배제하려고 각각 단독·순차 재실행해도 동일하게 hang했다. `channel-client.test.js`는 단독 실행에서 subtest 51(`DERR-007`)까지 정상 종료된 뒤 subtest 52 `CH-002 manual endpoint round-robin distributes requests across three servers`(2355행)에서 멈춘다. `timeout 20 node --test --test-name-pattern="CH-002 manual endpoint round-robin" test/contract/channel-client.test.js` → 20초 뒤 `Terminated`로 확인. `stream-session-runtime.test.js`는 sweep에서 `pass 1/fail 0`(비정상 count)로 나타났는데, 단독 재실행 결과 subtest 2 `ConnectionReady before the first packet keeps the native routing id for replies`(36행)가 `await context.client.reply('ready-first').submit()`에서 6초 넘게 멈추고, node test runner의 전체 실행 timeout이 그 시점에 나머지 33개 subtest를 전부 `cancelledByParent`로 취소한다. | 다섯 파일이 서로 다른 원인이었고 “채널/STREAM outbound reply 공유 원인” 가설은 성립하지 않는다. 증상 자체도 정정한다. gate runner는 `node --test --test-force-exit`로 실행하므로(`scripts/run_node_runtime_gate.js:43`) test 종료 뒤 남은 handle은 gate를 막지 않는다. 다섯 중 셋은 `--test-force-exit` 없는 sweep에서만 hang으로 보였다. (1) `stream-session-runtime.test.js`는 hang이 아니라 6.2초에 exit 1로 종료한다. subtest 2의 `await context.client.reply('ready-first').submit()`이 즉시 `DeadlineExceeded`로 끝나 test의 `replied` promise가 resolve되지 않고, 다른 pending 작업이 없어 node가 “Promise resolution is still pending but the event loop has already resolved”로 나머지 34건을 cancel한다. 근본 원인은 test의 `FakeStreamSocket`이 내부 backend port `ZLinkBackendStreamSocket`이 필수로 선언한 `sendTimeoutMs`·`sendHighWaterMark`·`onSendReady`를 구현하지 않은 것이다(`runtime/backend/contracts/index.ts:469`~`472`). `ZLinkStreamSessionNodeRuntime` 생성자의 `capacity: Math.max(1, options.socket.sendHighWaterMark)`에 `undefined`가 들어와 capacity가 `NaN`이 되고, `ZLinkAsyncSubmitter.rejectHardOverflow()`의 `queue.length < NaN`·`capacityWaiters.length < NaN`이 항상 false라 첫 submit부터 hard overflow로 거부된다. `?? 1` 가드는 `664c067e61`이 지웠지만 port가 `number`를 필수로 선언하므로 계약 위반은 fake 쪽이다. 같은 suite의 다른 fake(`channel-client.test.js:5060`, `client-server-location-runtime.test.js:107`)는 세 멤버를 모두 갖고 있다. (2) `stream-connector.test.js`·`stream-connector-codecs.test.js`는 모든 subtest가 통과한 뒤 종료하지 않는다. connect한 connector를 `close()`하지 않아 heartbeat `setInterval`이 각각 35개·2개 남는다(`ZlinkStreamConnectorLifecycle.startHeartbeat`). Runtime은 정본대로다. `32-stream-connector.ko.md` §6이 heartbeat 기본값을 “켜짐 — interval 1초”로 못박고 `closeOnce()`가 `stopHeartbeat()`를 호출한다. (3) `client-server-location-runtime.test.js`도 18건이 모두 끝난 뒤 종료하지 않는다. test 13이 `this.sockets.clientServerServerWeight is not a function`으로 실패해 `discovery.stop()`·`runtime.stop()`이 실행되지 않고 60초 polling timer와 location heartbeat timer가 남았다. `ZLinkClientServerLocationRuntime.publishServers()`는 descriptor 갱신 weight를 `sockets.clientServerServerWeight(channelName)`에서 읽는데(`client-server-location-runtime.ts:98`, 구현 `channel-socket-registry.ts:312`) test stub은 옛 이름 `clientServerServerSocket()`만 갖고 있었다. (4) `channel-client.test.js`만 진짜 hang이며 아직 해결하지 못했다. subtest 52 `CH-002`의 `finally`에서 `clientRuntime.stop()` → `stopRuntimeParts` → `state.dispose()` → `ZLinkNodeBackendContext.dispose()` → native `Context.close()`(`zlink_ctx_term`)가 돌아오지 않고 JS main thread 전체가 block한다(단계별 marker로 `state.dispose` 직전까지 도달 확인, procfs `wchan=do_sys_poll`). 그 시점에 해당 context가 소유한 socket과 monitor는 framework 기준 0이고(전용 계측으로 확인) 선행 test가 만든 context 14개는 전부 정상 종료했다. 동일 registration 형태(server 3 + manualConnections client)를 fresh process에서 25회 start·stop 반복해도 재현되지 않으므로 trigger는 반복 횟수가 아니라 선행 51개 subtest가 남긴 process 상태다. | (1)·(3)은 test를 계약에 맞췄다. `test/contract/stream-session-runtime.test.js`의 `FakeStreamSocket`에 backend port가 요구하는 `sendTimeoutMs`·`sendHighWaterMark`·`onSendReady`를 추가했다(assertion·scenario 무변경). `test/contract/client-server-location-runtime.test.js`의 stub `clientServerServerSocket()`을 runtime이 실제로 호출하는 `clientServerServerWeight()`로 옮겼다(같은 `serverWeight`를 반환하므로 75→25 재가중 기대는 그대로다). (2)는 test가 연 자원을 닫도록 했다. `test/contract/stream-connector.test.js`에 `createStreamConnector()` 추적 helper와 suite 종료 후 일괄 `close()`하는 `test.after()`를 넣고 59개 생성 지점을 옮겼다(모든 assertion 뒤에 실행되므로 시나리오에 영향이 없다). `test/contract/stream-connector-codecs.test.js`의 connector 두 개는 `try/finally`로 닫았다. (4)는 §2.3 기준 구현과의 terminal cleanup parity 결함 한 건을 근본 수정했지만 hang은 남았다. .NET 기준 binding `Context.Dispose()`는 `zlink_ctx_shutdown` 뒤에 `zlink_ctx_term`을 호출하는데(`bindings/dotnet/src/Zlink/Runtime/Handles/Context.cs:76`) Node framework의 `ZLinkNodeBackendContext.dispose()`는 term만 호출하고 있었다. `packages/framework/src/runtime/backend/node/node-backend-adapter-factory.ts`에 shutdown 선행을 추가했다. 이 수정만으로 5회 중 1회는 파일이 완주하지만 나머지는 같은 지점에서 멈춘다. 남은 원인은 native context termination이며 다음 단계는 `bindings/node` `Context.close()`와 Core `zlink_ctx_term` reaper 경로 확인이다. §2.2에 따라 timeout 확대·skip·scenario 삭제로 닫지 않았다. | `npm run build` 뒤 파일 단위 `node --test --test-reporter=spec test/contract/<file>`. `stream-session-runtime.test.js` → 35 pass/0 fail, exit 0(수정 전 1 pass/34 cancelled, exit 1). `stream-connector.test.js` → 56 pass/0 fail, exit 0(수정 전 56건 통과 후 미종료, exit 124). `stream-connector-codecs.test.js` → 6 pass/0 fail, exit 0(수정 전 6건 통과 후 미종료, exit 124). `client-server-location-runtime.test.js` → 18 pass/0 fail, exit 0(수정 전 17 pass/1 fail 후 미종료, exit 124). 네 파일 모두 clean rebuild 위에서 재확인했다. `channel-client.test.js`는 gate와 같은 `node --test --test-force-exit`로도 여전히 미종료다(연속 4회 exit 124, 매번 subtest 51까지 출력한 뒤 `CH-002`에서 정지). 완주한 1회의 실측치는 94 tests / 60 pass / 34 fail / 0 cancelled, duration 44.4s이고 그 34건의 원인은 이 항목이 아니라 `BLK-023`이다. shutdown 선행 추가 뒤 `channel-client.test.js`를 제외한 `test/contract/*.test.js` 전량 sweep(113개 파일)에서 미종료 파일이 없고 pass/fail 분포도 baseline과 같음을 확인했다. 현재 candidate에서 `node --test --test-force-exit --test-reporter=spec test/contract/channel-client.test.js`를 다시 실행해 91/91, exit 0과 3.9초 내 정상 종료를 확인했다. 이전 `zlink_ctx_term` hang과 34개 fixture drift는 모두 재현되지 않았다. | 해결 |
| `BLK-023` | `V11-M6A-NODE`, `V11-M6B-NODE`; Node.js lane | working tree(2026-07-25, base `c3cc26f50a`) | `node --test --test-force-exit test/contract/channel-client.test.js`가 완주한 실행에서 94건 중 34건이 실패한다. 대표 오류는 `ZLinkConfigurationException: Channel packetName is required when the payload type cannot provide one.`이며 `CH-001`·`CH-002`·`CH-006`·`DERR-001`·`DERR-002`·`REG-003`·`DERR-007`·`DSC-008`·`CDC-001` 등 channel client 경로 전반에 걸쳐 있다. 이 항목이 소유하는 것은 그 34건 중 arity·class 원인에 해당하는 부분이며, `해소`는 그 원인을 없앴다는 뜻이지 실패 0을 뜻하지 않는다. 남은 20건은 아래 재검증 칸이 원인별로 분류했고 별도 항목 배정이 필요하다. | 근본 원인은 인자 개수가 아니라 **class 분리**다. 커밋 `6e37590047`이 RouteMesh channel 의미를 `DefaultZLinkChannelClient`에서 떼어 `DefaultZLinkRouteClient`로 옮기면서 두 class의 `sendToChannel`·`requestToChannel`을 모두 2인자로 바꿨고, 같은 커밋이 `contract-surface`·`message-packet-name`·`node-binding-parity` 세 test만 정렬하고 `channel-client.test.js`는 두고 갔다. 정식 exact interface는 2인자다. `02-channel-messaging.ko.md:295`~`296`(`ZLinkRouteClient`), `01-foundation-configuration.ko.md:133`~`134`(`ZLinkChannelClient`), `04-spots.ko.md:147`~`148`(`ZLinkSpotOutbound`)이 모두 같고 355행 예제도 `client.requestToChannel("checkout", request)`다. 구현은 `contracts/Channels/IZLinkChannelClient.ts:5`와 `contracts/Channels/RouteCalls.ts:8`이 일치한다. 두 class의 MeshName 해석 경로는 서로 다르다. `DefaultZLinkRouteClient`만 `resolveMeshChannel(channelName)`으로 registration의 `spotNodes`에서 mesh를 찾아 transport의 `submitToChannel`·`requestToChannel`에 넘기고(`runtime/channels/channel-clients.ts:173`~`205`, `254`~`267`), `DefaultZLinkChannelClient`는 `registration.channelClients`를 `requireChannel`로 검사한 뒤 transport의 `send`·`request`를 부른다(같은 파일 `47`~`88`). 그래서 3인자 호출은 `client.requestToChannel('play', 'play', typedPacket(...))`처럼 payload 자리에 문자열이 들어가 `resolveFrameworkPacketName()`이 던지고, RouteMesh registration을 쓰는 stub test는 인자만 지워도 `Channel client 'api' is not registered.`로 바뀐다. | `channel-client.test.js`의 37개 호출 지점을 두 갈래로 나눠 정렬했다(`4e6a372a43`). ClientServer channel을 부르는 31개 호출(`channels: { X: { client } }` registration + 실제 `clientRuntime.channelTransport` 또는 `ZLinkDealerChannelClientTransport`)은 MeshName 인자만 지웠다. RouteMesh registration(`meshChannelRegistration`)과 `submitToChannel`·`requestToChannel` transport stub을 쓰는 test 5건의 호출 6개(64~68·90~127·327~357·359~380·382~406행)는 `DefaultZLinkRouteClient`로 옮겼다. 31+6이 37이다. 이 6건은 인자만 지우면 class가 틀린 채로 남는다. stub의 `meshName: 'mesh'` assertion은 지우지 않고 유지했다. 이제 이 값은 caller가 준 문자열의 전달이 아니라 runtime이 ChannelName에서 mesh를 해석했다는 증거이므로 더 강한 assertion이다. one-way admission test(90행)는 반대로 `meshName`·`channelName`을 새로 assertion에 넣어 해석 결과를 고정했다. 개별 판정이 필요했던 rejection 2건은 실패 이유가 그대로다. 85행은 `sendToChannel('api', { ok: true })`, 591행은 `sendToChannel('missing-channel', 'hello')`로 ChannelName만 남겼고 둘 다 빈 registration의 `requireChannel`이 같은 `ZLinkConfigurationException`을 던진다. 591행 test의 "validation이 pre-abort보다 먼저"라는 취지도 `DefaultZLinkSendCall.submit()`이 `validate()`를 `throwIfAborted()`보다 먼저 부르므로 유지된다(`transportAttempts` 0 그대로). 같은 파일이 fork로 띄우는 helper `test/contract/helpers/route-mesh-peer.js:100`도 같은 결함이라 함께 고쳤다(`05eb3ceeef`). 변경 path는 `framework/languages/node/test/contract/channel-client.test.js`와 `framework/languages/node/test/contract/helpers/route-mesh-peer.js` 둘뿐이며 runtime source는 건드리지 않았다. | `node --test --test-force-exit test/contract/channel-client.test.js`. 정렬 전 `9c479bdf31` 사본으로 실행한 baseline은 94건 중 60 pass·34 fail, 정렬 뒤는 2회 독립 실행 모두 94건 중 74 pass·20 fail이었다. 실패 이름 집합을 diff하면 20건이 baseline 34건의 진부분집합이고 새로 깨진 test는 0건이다. 사라진 14건은 subtest 3·10·11·12·24·25·42·44·47·49·51·55·57·71이다. 남은 20건은 이 항목과 다른 원인이다. subtest 48·50·56은 `Channel 'X' has no admitted ClientServer target.`으로 `BLK-033`·`BLK-034`와 같은 모양이고, 34·35·37~40은 route bridge의 `ZLink async submit timed out.`, 23은 Nest DI token 기대값 drift, 43은 `SpotNode 'api' router must define a bind endpoint.`, 53·54는 location peer 미수렴, 65·66은 `User Spot creation requires a Location Store.`, 67~69는 backpressure다. 두 실행 모두 완주했고 `BLK-009`의 `zlink_ctx_term` hang은 재현하지 않았다. 다른 Node 파일에도 같은 3인자 잔재가 있다. `test/contract/nestjs-module.test.js:565`·`568`, `samples/SupportChat.Ts/.../supportchat-session.ts:51`, `samples/ShoppingMall.Ts/.../zlink-order-workflow-router.ts:68`, `samples/Bingo.Ts/.../bingo-room-spot.ts:207`, `e2e/RuntimeMonitoring/.../trigger-endpoints.ts:80`이며 e2e는 M7 재활성화까지 동결이라 손대지 않았다. sample은 root `tsconfig.json`의 include가 `packages/*/src`와 `test`뿐이라 typecheck가 잡지 못한다. 현재 candidate의 같은 file은 91/91로 통과해 이 항목이 남겼던 후속 실패도 모두 사라졌다. | 해결 |
| `BLK-020` | `V11-M6A-CPP`, `V11-M6B-CPP`; C++ lane | working tree(2026-07-25, base `f0dbf6e36f`) | `.artifacts/v11/build/framework-runtime-regression/cpp`에서 `test_cpp_framework_location_runtime`·`test_cpp_framework_location_lifecycle`·`test_cpp_framework_location_key_codec`이 compile 실패했다(`spot_location_key_t`/`spot_location_t`/`actor_location_t`의 `spot_id`에 `zlink::routing_id_t`를 대입). | `spot_location_key_t.spot_id`, `spot_location_t.spot_id`, `actor_location_t.spot_id`는 모두 `std::string`이다(`framework/include/zlink/framework/contracts/locations/{keys,rows}.hpp`). dotnet parity lane의 `ZLinkSpotLocationKey(string SpotId)`도 동일하다. 세 test 파일이 `zlink::routing_id_t::from(...)`을 대입해 v11 Core 제거 이후 처음 빌드하면서 드러났다. `test_cpp_framework_location_key_codec`은 추가로 `encode_spot_key()`가 `mesh_name`+hex RoutingId를 인코딩한다고 기대했지만, 구현과 dotnet `ZLinkCanonicalLocationKeyFormatter.EncodeSpotKey`는 항상 `spot_id` 문자열 하나만 인코딩했다(`spot_location_key_t`에는 애초에 `mesh_name` 필드가 없다). | Runtime은 그대로 두고 세 test 파일의 대입·기대값을 계약 타입에 맞췄다. `test_cpp_framework_location_key_codec.cpp`(기대 byte string을 `"6:spot-a"`로 교체), `test_cpp_framework_location_runtime.cpp`, `test_cpp_framework_location_lifecycle.cpp`. | `cmake --build ... --target test_cpp_framework_location_key_codec test_cpp_framework_location_runtime test_cpp_framework_location_lifecycle` → 3/3 compile 성공, `ctest -R "location"` → 6/6 통과. | 해결 |
| `BLK-010` | `V11-M6A-CPP`, `V11-M6C-CPP`; C++ lane | working tree(2026-07-25, base `f0dbf6e36f`) | `test_cpp_framework_layout_contract`가 5건 실패했다: forbidden-symbol 스캔이 `store_unavailable`을 걸었고, `contract_headers_have_compile_coverage`가 3개 public header 누락을 걸었고, actor-destroy-lifecycle 3종 doc 검사가 실패했고, `SubmitAdmission/run_e2e.sh`가 0600 권한 검사에 걸렸다. | (1) forbidden 목록의 `store_") + "unavailable"`은 2026-07-04에 삭제된 `location_write_status_t::store_unavailable`(구 store 설계) 회귀를 막으려던 것인데, 그 뒤 추가된 `termination_reason_t::store_unavailable`/`target_preflight_status_t::store_unavailable`(공식 spec `28-graceful-drain-handoff.ko.md` §5가 요구하는 별개의 public contract)까지 우연히 잡았다. (2) `locations/spot_kind.hpp`·`locations/maintenance_stores.hpp`·`contracts/placement.hpp`는 실재하는 public header인데 `test_cpp_framework_contract_headers.cpp`가 `#include`하지 않았다. (3) `14-actor-model.ko.md`/`06-framework-api.ko.md`/`20-session-actor-dispatch.ko.md`의 actor-destroy-lifecycle 산문은 v10 draft tree(`framework/doc/framework/spec/`, 2026-07-25 `0a4c5ba9da`에서 삭제)를 기준으로 pin한 needle을 그대로 들고 있었고, `14-actor-model.ko.md` 쪽은 추가로 줄바꿈 때문에도 깨졌다(needle의 공백이 파일의 개행과 매칭되지 않음). (4) `SubmitAdmission/run_e2e.sh`의 `write_role_config()`만 다른 모든 e2e runner가 쓰는 `os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)` 패턴이 없었다(`sample-e2e-configuration-policy.ko.md` §6). | (1) 금지 needle을 `location_write_status_t::store_") + "unavailable"`로 한정하고, `writes.hpp`의 `location_write_status_t` enum 본문만 별도로 검사하는 `location_write_status_does_not_regain_store_unavailable()`을 추가했다. (2) 세 header의 `#include`를 `test_cpp_framework_contract_headers.cpp`에 추가했다. (3) 세 doc 파일의 현재 원문에서 검증된 substring으로 need·le을 다시 pin했다(예: `14-actor-model.ko.md`는 줄 경계를 넘지 않는 문장으로 분할, `06-framework-api.ko.md`는 destroy 오류 분류 표 행으로, `20-session-actor-dispatch.ko.md`는 §3·§4.1에 두 번 나오는 disconnect/destroy 경계 문장으로 재pin). (4) `write_role_config()`에 다른 runner와 동일한 `os.chmod` 호출을 추가했다. `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp`, `test_cpp_framework_contract_headers.cpp`, `framework/languages/cpp/e2e/SubmitAdmission/run_e2e.sh`. | `ctest -R test_cpp_framework_layout_contract` → 통과. | 해결 |
| `BLK-011` | `V11-M6A-CPP`; C++ lane | working tree(2026-07-25, base `0ffcde771e`) | `test_cpp_framework_label_contract`가 `framework-sample-parity` wildcard label 미포함과 `framework-foundation`/`framework-m6-runtime`/`yield` 등 16개 label의 unknown-label 오류로 실패했다. `test_cpp_framework_target_contract`는 `E2E-CP-55`·`E2E-CP-19` gate 2건과 `CPP-G0-SPOTHANDLE-001`·`CPP-G0-ACTOR-002`(2건) gate 3건, 총 5건이 실패했다. `test_cpp_framework_submit_admission`은 매 실행마다 exit 12(=10+2)로 실패했다. | `verify_ctest_label_contract.cmake`의 `known_labels`/`required_labels`는 m6a/m6b/m6c runtime·foundation·`yield` label이 추가된 뒤 갱신되지 않았고, `framework-sample-parity`는 `ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON`일 때만 `required_labels`에 들어가도록 되어 있었지만 그 label을 다는 `test_cpp_framework_sample_parity`는 `ZLINK_FRAMEWORK_CPP_BUILD_TESTS` 아래 무조건 등록된다. `target_contract`의 `E2E-CP-55`·`E2E-CP-19`는 SpotActorTransfer·SpotService 시나리오 코드가 이미 정확히 그 검증을 구현하고 있는데 needle이 `spot_id`/`source_spot_id`/`target_spot_id`(구 이름)를 찾고 실제 코드는 `spot_rid`/`source_spot_rid`/`target_spot_rid`(RoutingId 표면 결정 이후 현재 이름)를 쓴다. `CPP-G0-SPOTHANDLE-001`·`CPP-G0-ACTOR-002`는 `framework/doc/plan/log/framework-public-contract-gap-implementation/cpp-g0-contract-ledger.ko.md`에 `GAP`(미해결)로 기록된, `spot_ref_t`를 opaque `spot_handle_t`로 교체하고 Actor Join을 deferred·result-free terminal로 바꾸는 별도 진행 중인 이니셔티브이며 이번 v11 Core 제거 sweep의 원인이 아니다. `submit_admission`은 test 쪽 결함 2건이었다. `reservation_covers_in_flight_retry()`가 owner reservation capacity가 이미 in-flight retry로 다 찬 상태에서의 새 admission을 "즉시 완료되면 안 된다"고 기대했는데, `05-async-execution-policy.ko.md` §1.3은 "내부 bounded waiter capacity까지 모두 사용 중이면 새 payload를 보관하지 않고 `DeadlineExceeded`로 즉시 완료한다"를 명시한다. `public_call_terminator_is_one_shot()`은 복사한 `send_call_t`의 재제출이 `request_protocol_error`로 끝난다고 기대했는데, `send_call_t::_submission`은 복사본도 같은 one-shot claim을 공유하도록 일부러 `shared_ptr`이고, 같은 spec §1.3 표는 "같은 call의 terminal을 두 번 실행"을 `AlreadySubmitted`로 분류한다(`request_protocol_error`는 submit 함수에 아예 바인딩되지 않은 call을 위한 별개 kind다). | `verify_ctest_label_contract.cmake`에 누락 16개 label을 추가하고 `framework-sample-parity`를 무조건 `required_labels`로 옮겼다. `target_contract`의 두 needle을 `_rid` 이름으로 재pin했다(GAP 3건은 손대지 않음). `submit_admission`은 두 assertion을 spec에 맞게 뒤집었다(`!rejected.await_ready()` → `rejected.await_ready()`, `request_protocol_error` → `already_submitted`). `framework/languages/cpp/tests/Zlink.Framework.ContractTests/verify_ctest_label_contract.cmake`, `test_cpp_framework_target_contract.cpp`, `framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_submit_admission.cpp`. | `ctest -R "^(test_cpp_framework_label_contract|test_cpp_framework_submit_admission)$"` → 2/2 통과, `test_cpp_framework_submit_admission`은 단독 실행 5회 연속 exit 0. `ctest -R test_cpp_framework_target_contract` → 여전히 실패하며 남은 3건은 모두 `CPP-G0-*` GAP(별도 이니셔티브)뿐임을 확인했다. 이후 그 GAP 3건 중 `CPP-G0-ACTOR-002` 2건은 `BLK-021`에서 닫았고 `CPP-G0-SPOTHANDLE-001` 1건은 `BLK-022`가 이어받았다. | 해결(label·submit_admission·E2E-CP-55/19), GAP 3건은 별도 이니셔티브로 이관 |
| `BLK-012` | `V11-M6A-CPP`; C++ lane, 원인은 `bindings/cpp` 소유 | working tree(2026-07-25, base `8513313258`) | `test_cpp_framework_install_consumer`가 실패했다. 1차: consumer 빌드가 `/usr/bin/ld: cannot find -llibzlink`로 link 실패. 원인 수정 뒤 2차: `No rule to make target '.../install/lib/libzlink.so'`. | (1) `bindings/cpp/CMakeLists.txt`가 `zlink::cpp`의 `INTERFACE_LINK_LIBRARIES`를 `$<INSTALL_INTERFACE:libzlink>`로 선언한다. 이는 raw linker flag가 아니라 Core 자체 빌드가 만드는, 리터럴로 `libzlink`라는 이름의 CMake target을 가리키는 참조다(`core/CMakeLists.txt`의 `add_library(libzlink SHARED ...)` + `OUTPUT_NAME`/`PREFIX` override로 실제 산출물은 `libzlink.so`). Framework 자체 빌드에서는 `CMAKE_PREFIX_PATH`가 `zlink-core` local package를 함께 갖고 있어 `zlink_cppConfig.cmake.in`의 `find_dependency(zlink 11 CONFIG)`가 진짜 `libzlink` target을 resolve하지만, `zlink_framework_cppConfig.cmake.in`/`zlink_stream_connector_cppConfig.cmake.in`은 `zlink_cppConfig.cmake`를 거치지 않고 `zlink_cppTargets.cmake`를 직접 `include()`해서 이 경로를 건너뛴다. 두 템플릿은 이미 native `.so`를 가리키는 자체 IMPORTED target을 만들어 두었지만 이름이 `zlink-native`라 `libzlink` 참조가 계속 미해결이었다. (2) `zlink-native`를 `libzlink`로 개명해 참조가 풀린 뒤 드러난 2차 결함은, 그 IMPORTED target의 `IMPORTED_LOCATION`이 가리키는 `${PACKAGE_PREFIX}/lib/libzlink.so`가 framework 자신의 install 결과물에 실제로 없었던 것이다. `framework/languages/cpp/CMakeLists.txt`의 native runtime install 블록(~678행, `COMPONENT FrameworkDependency`)이 `ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME` 경로를 `ZLINK_FRAMEWORK_CPP_LOCAL_ZLINK_CPP_PREFIX`(zlink_cpp 바인딩 package prefix, `libzlink_cpp.a`만 있고 `.so`는 없음)로 계산해 `EXISTS` guard가 항상 거짓이었다. 실제 `libzlink.so`는 `ZLINK_FRAMEWORK_CPP_LOCAL_ZLINK_CORE_PREFIX`(Core package prefix)에 있다. | (1) `zlink_framework_cppConfig.cmake.in`/`zlink_stream_connector_cppConfig.cmake.in`의 `zlink-native` target을 `libzlink`로 개명했다. (2) `ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME` 계산에 쓰는 prefix를 `ZLINK_FRAMEWORK_CPP_LOCAL_ZLINK_CPP_PREFIX`에서 `ZLINK_FRAMEWORK_CPP_LOCAL_ZLINK_CORE_PREFIX`로 교체했다(WIN32/APPLE/Linux 세 분기 모두). `framework/languages/cpp/cmake/zlink_framework_cppConfig.cmake.in`, `zlink_stream_connector_cppConfig.cmake.in`, `framework/languages/cpp/CMakeLists.txt`. | reconfigure 뒤 `ctest -R test_cpp_framework_install_consumer` → 통과. 이 결함은 test 전용이 아니라 실제 하위 소비자가 이 패키지를 설치했다면 동일하게 link 또는 runtime load에서 막혔을 것이다. | 해결 |
| `BLK-013` | `V11-M6A-CPP`; C++ lane | working tree(2026-07-25, base `fe671b6236`) | `test_cpp_framework_messaging`이 간헐적으로 abort했다. `.artifacts/v11/build/framework-runtime-regression/cpp`에서 `./test_cpp_framework_messaging` 단독 반복 실행 60회 중 1회, `ctest -j4` 병렬 실행에서도 재현했다. `terminate called after throwing an instance of 'zlink::framework::framework_exception_t' what(): one-way admission deadline was exceeded`. | 이전 판정("여러 시나리오가 runtime의 공유 timer thread를 block해 다른 시나리오의 pending admission이 굶는다")은 사실이 아니었다. 이 test의 `main()`은 완전히 순차이고 블록마다 `reset_async_submit_runtime_for_tests()`를 호출하므로 두 시나리오의 admission이 동시에 pending인 순간이 없다. 첫 async one-way 블록은 deadline이 지나면 retry callback을 아예 실행하지 않아 `attempts`가 2에 머물고 exit code 63으로 끝나므로 abort 지점이 될 수 없다. 실제 원인은 Logical Multicast executor의 worker slot 회계다. `logical_multicast_executor_t`는 submit 시점에 `_available`을 줄이고 job body가 반환된 뒤 `release_slot()`에서 되돌리는데, caller task는 그보다 앞선 worker dequeue에서 완료된다(코드 주석 "Dequeue is the public terminal boundary"). 따라서 `.result()`가 돌아와도 slot은 아직 반환되지 않았을 수 있다. Test의 saturation 블록 두 곳은 진입 시 slot이 전부 비어 있다고 가정하고 worker 수만큼 publish를 채우는데, 직전 publish가 slot 하나를 아직 쥐고 있으면 마지막 호출이 즉시 overflow(`deadline_exceeded`)로 완료되고 그 task의 `.value()`가 uncaught로 던져 process가 abort한다. 같은 원인이 exit code 형태로도 나타난다. `handoff_calls != 1`(exit 83)과 `multicast_calls != 1`(exit 80)은 dequeue로 완료됐지만 아직 실행되지 않은 publisher body를 세는 assertion이다. 지점은 marker 계측으로 특정했다. 120회 중 abort 4회는 모두 두 번째 saturation 블록의 `deadline_workers` task `.value()`였고 1회는 exit 83이었다. | Runtime 동작은 바꾸지 않고 slot 상태를 test가 관측할 수 있게만 했다. 기존 `_for_tests` 계열과 같은 성격으로 `multicast_worker_count_for_tests()`와 `wait_for_idle_multicast_executor_for_tests()`를 추가했다. 후자는 executor 자신의 condition variable을 기다리는 blocking barrier이며 polling도 timeout 확대도 아니다. Test는 두 saturation 블록에 들어가기 전, `multicast_calls`·`handoff_calls` assertion 직전, 마지막 `recovered` publish 직전에 idle을 관측한 뒤 진행한다. worker 수도 test가 다시 계산하지 않고 executor에서 가져온다. `wait_until_idle`은 timeout이 없다. `release_slot()`이 idle 전이마다 notify하므로 predicate는 항상 도달 가능하지만, 이후 slot 회계가 깨지면 abort 대신 ctest timeout hang으로 나타난다. Assertion은 한 건도 바꾸지 않았고 시나리오를 삭제하거나 순서를 바꾸지 않았으며 timer thread를 의도적으로 block하는 retry 시나리오는 그대로 두었다. 원인이 사라졌으므로 `43ebdef227`이 넓혔던 첫 블록의 admission deadline 2000ms와 local poll budget 1000ms는 원래 값 100ms로 되돌렸다. `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.hpp`, `framework/languages/cpp/framework/src/runtime/messaging/async_submit_runtime.cpp`, `framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_messaging.cpp` | 100ms를 복원한 최종 상태로 `./test_cpp_framework_messaging` 단독 400회 연속 통과(0/400 실패), 4-wide 병렬 160회 통과(0/160), 20-core host에서 24-wide 초과구독 240회 통과(0/240). `ctest -j4`로 messaging을 포함한 framework unit test 10개를 25회 반복 실행해 25회 모두 `100% tests passed`. 수정 전 같은 build에서는 단독 60회 중 1회, marker 계측본 120회 중 5회 실패했다. 같은 pattern을 쓰는 sibling test도 확인했다. `test_cpp_framework_async_task`는 publisher body가 세운 flag를 condition variable로 기다리므로 같은 결함이 없고, `test_cpp_framework_contract_headers`는 compile-time trait만 본다. `test_cpp_framework_submit_admission`은 단독 200회 연속 통과했다. install consumer·perf·unreal·redis를 제외한 `ctest -j4` 전체 실행에서 남는 실패는 이 항목과 무관한 `test_cpp_framework_target_contract`(public surface gate)와 `test_cpp_framework_sample_parity`·`test_cpp_framework_parity_contract`(Not Run)이다. | 해결 |
| `BLK-014` | `V11-M6B-NODE`, `V11-M6B-DN`, `V11-M6B-CPP`, `V11-M6B-JVM`; contract lane 판정 필요 | working tree(2026-07-25, base `9fc5d01f74`) | `BLK-008` 정렬 중 확인한 계약 공백이다. Actor join admission callback 안에서 application이 joining Actor의 `actorType`을 알 방법이 다섯 언어 어디에도 없다. 별도 실행 command는 없고 계약 문서 판정이 필요하다. | .NET 기준 구현을 확인한 결과 per-join 동적 `actorType` 경로가 없다. `OnActorJoinAsync`는 `actorId`와 application join payload(`ZLinkMessage`)만 받고, `IZLinkSpotCommonContext`/`IZLinkSpotContext`에는 actor 조회가 아예 없으며(`Contracts/Spots/ZLinkSpot.cs:247`~`281`), commit은 accept 이후에 실행되므로 callback 시점에는 joining Actor가 membership에도 없다(`Runtime/Spots/ZLinkSpotActorJoinDispatcher.cs`). 남는 정보는 Spot의 정적 generic bound(`IZLinkSpot<TActor>`)와 application이 자기 payload에 직접 넣은 값뿐이다. Node의 `ZLinkActorJoinRequest.actorType`은 이 정보를 유일하게 노출하던 필드였지만 그 객체 자체가 어느 spec에도 없다. 실측상 이 공백은 이론적이다. 샘플·E2E 어디에서도 `onActorJoin` 안에서 `actorType`을 읽지 않으며 `e2e/ObservabilityOps/Server/Play/main.ts`는 자기 evidence 문자열에 `input=actor-id-only`라고 기록한다. 함께 남는 질문은 이제 runtime이 쓰지 않는 shipped 표면 `ZLinkActorJoinRequest`·`createActorJoinRequest()`의 disposition이다(Node에만 있고 spec에는 없다). | 공통 spec과 다섯 exact interface가 admission을 `actorId + application payload`로 고정하므로 actorType을 새 callback 인자로 추가하지 않는 것으로 판정했다. Node public contracts에서 spec에 없던 `ZLinkActorJoinRequest`를 제거하고, Framework가 쓰는 actor type·membership fence snapshot은 runtime 내부 interface로 한정했다. | `npm run build`와 Actor Manager 74건·public contract surface 31건을 함께 실행해 105/105를 통과했다. Public declaration에는 `ZLinkActorJoinRequest`가 없고 내부 `createActorJoinRequest()`는 application callback에 노출되지 않는다. | 해결 |
| `BLK-015` | `V11-M6A-DN`; .NET lane | working tree(2026-07-30, base `9ff7e843ceb2`) | 이전 candidate가 정식 계약에 없는 `IZLinkInstanceSpotLocationStore`를 public으로 내보내 contract coverage가 실패했다. | Instance Spot 단계별 저장 동작을 별도 public interface로 노출한 것이 원인이었다. Provider가 구현하는 공개 SPI는 opaque `IZLinkLocationStore`와 `IZLinkRelocationStore`뿐이어야 한다. | 금지된 interface와 rich Location 계약을 application package에서 제거했다. Provider SPI는 `Zlink.Framework.Provider.Abstractions`의 두 Store interface로 분리했고 authority·reservation·aggregate 처리는 Framework 내부 repository가 소유한다. 현재 source와 exact interface에서 `IZLinkInstanceSpotLocationStore` 검색 결과는 0건이다. | `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-r5a-20260730.json`: public contract 70/70, internal runtime 1294/1294, resource 88/88, protocol 105/105 통과. 같은 candidate의 `ROW-GATE`도 files 21·commands 5로 통과했다. | 해결 |
| `BLK-016` | `V11-M6A-JVM`, `V11-M6B-JVM`; JVM lane | working tree(2026-07-30, base `9ff7e843ceb2`) | Java Actor Manager가 exact interface의 fluent Create·GetOrCreate를 제공하지 않았고 Spot Create·GetOrCreate에는 create-time `yield()`가 없었다. Kotlin에는 exact interface가 요구하는 Actor·Spot manager와 create-call wrapper 네 타입이 없었다. | Java manager가 terminal을 직접 실행하던 표면과 Kotlin의 얕은 extension이 fluent single-use state machine을 표현하지 못했다. | Java에 `ZLinkActorCreateCall`·`ZLinkActorGetOrCreateCall`과 timeout을 포함한 runtime terminal을 추가했다. Spot Create·GetOrCreate에는 허용된 turn에서만 동작하는 `yield()`를 연결했다. Kotlin에는 `ZLinkKotlinActorManager`·`ZLinkKotlinActorCreateCall`·`ZLinkKotlinSpotManager`·`ZLinkKotlinSpotCreateCall`을 실제 Java call에 위임하는 public wrapper로 구현하고 fluent state·coroutine terminal test를 추가했다. | Java core unit 636/636, Java·Kotlin public contract와 Kotlin unit 통과. Fresh candidate의 `M6-RUNTIME` 11/11과 `ROW-GATE` files 398·commands 11 통과. 증거: `.artifacts/v11/evidence/V11-M6A-JVM/result-blk016-018-20260730.json`. | 해결 |
| `BLK-017` | `V11-M6A-JVM`; JVM E2E registration | working tree(2026-07-30) | Java fixture와 all-runner에 공통 E2E scenario ID가 빠져 documentation contract가 실패했다. | 이후 Java E2E source와 registration을 보강했다. 기대 ID 집합이나 assertion은 줄이지 않았다. | 현재 `JavaDocumentationRegressionTest.everyCommonScenarioIdHasAnActiveJavaFixtureAndAllRunnerSuite`를 단독 실행해 통과했다. | `:zlink-framework-core:contractTest --tests systems.zlink.framework.JavaDocumentationRegressionTest.everyCommonScenarioIdHasAnActiveJavaFixtureAndAllRunnerSuite` → `BUILD SUCCESSFUL`. Actual process 검증은 JVM M6·M7 E2E row가 소유한다. | 해결 |
| `BLK-018` | `V11-M6A-JVM`, `V11-M8-CLEAN-JVM`; JVM testkit lane | working tree(2026-07-30, base `9ff7e843ceb2`) | Testkit unit은 MeshNode의 Spot backend를 구성하지 않았고 fakeBackend source set은 제거된 handler context·Actor Join·ClientServer builder 계약에 고정돼 compile되지 않았다. Module boundary와 sample runner contract도 삭제된 package·test 이름을 요구했다. | 제거된 public API를 검증하던 fixture와 현재 runtime composition을 사용하지 않는 fake가 함께 남아 있었다. | Fake MeshNode가 현재 SpotNode를 소유하도록 고쳤다. 제거된 API에만 결합된 fixture는 삭제하고 현재 Actor·Spot fluent manager와 create-time Yield를 검증하는 `CurrentManagerFakeBackendTest`로 교체했다. Module boundary와 runtime package inventory를 현재 private package에 맞췄고 삭제된 actor lifecycle test 이름을 sample runner에 강제하던 assertion을 제거했다. | Clean `:zlink-framework-testkit:test :zlink-framework-testkit:fakeBackendTest` 통과. Focused module·runner contract 통과. 전체 contract는 27건 중 testkit 소유 24건이 통과했고 남은 3건은 Java·Kotlin Bingo 누락 handler와 DeliveryDispatch의 금지된 `RoutingId` 사용으로 sample lane에 이관했다. Fresh JVM `M6-RUNTIME` 11/11과 `ROW-GATE` 398/11 통과. | 해결 |
| `BLK-021` | `V11-M6-DEFERRED-JOIN-CPP`; C++ lane | working tree(2026-07-25, base `c3cc26f50a`) | `.artifacts/v11/build/framework-runtime-regression/cpp`에서 `ctest -R "^test_cpp_framework_target_contract$"` → gate 실패 3건. 그중 2건이 `CPP-G0-ACTOR-002: legacy result-bearing Actor Join surface remains: actor_join_accepted_t`와 `... actor_join_rejected_t`다. `BLK-011`이 이 3건을 "별도 이니셔티브"로 이관한 뒤 남아 있던 항목이다. | Gate가 자기가 강제해야 할 authority의 부정을 assert하고 있었다. C++ exact interface `framework/doc/framework/common/spec/server/languages/cpp/interfaces/05-actors.ko.md` 41~74행이 `actor_join_accepted_t`·`actor_join_rejected_t`·`actor_join_failed_t` 세 struct와 그 셋을 alternative로 갖는 `actor_join_completion_t`, 그리고 `actor_t::on_join_completed(...)`를 선언한다. 같은 commit `859fcf07fe`가 이 선언을 exact interface에 추가하면서 동시에 `test_cpp_framework_target_contract.cpp`의 `CPP-G0-ACTOR-002`를 require-present에서 require-absent로 뒤집었다. 이름이 .NET에서 제거된 `ZLinkActorJoinResult.Accepted`·`Rejected`와 겹치지만, C++의 두 type은 .NET이 유지하는 `ZLinkActorJoinCompletion.Accepted`·`Rejected`·`Failed`에 대응하는 completion union 항이다. 즉 runtime drift가 아니라 gate가 stale이었다. Runtime `framework/include/zlink/framework/contracts/actors/actor.hpp`는 이미 exact interface 형태(88~114행 세 struct와 variant, 124행 `on_join_completed`, 331~390행 `timeout()`+`void defer()`만 가진 `actor_join_call_t`)를 구현하고 있었고 진짜로 제거된 `actor_join_result_t`는 이미 없다. | Runtime과 public header는 손대지 않고 gate만 exact interface로 다시 pin했다. Spec이 요구하는 두 이름을 forbidden에서 required로 옮기고, 이전에 없던 positive coverage를 더했다. 세 completion struct와 `on_join_completed` 선언 존재, `actor_join_completion_t` alias가 정확히 세 alternative를 열거하는지, `actor_join_call_t` 블록이 `submit (`·`async (`·`yield (` terminal을 노출하지 않는지(04-spots.ko.md 326~337행·696~701행)를 검사한다. cpp exact interface 디렉터리에서 hit 0인 `actor_join_result_t`와 `task_t<actor_join`은 forbidden으로 유지했다. 실행되는 assertion은 5개에서 15개로, `gate.require` 호출 지점은 2곳에서 7곳으로 늘었다. 두 `substr` 추출은 잘려도 조용히 통과하지 않도록 anchor로 고정했다(`actor_join_call_t` 블록은 `void defer ()`를, completion alias는 종결 `;`를 기준으로 삼는다). `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_target_contract.cpp` | `ctest -R "^test_cpp_framework_target_contract$"` → gate 실패 3건에서 1건으로. 남은 1건은 `CPP-G0-SPOTHANDLE-001`이며 별개 contract family다. 같은 build의 전체 `ctest -j4`는 변경 전후 모두 42 pass / 4 fail(46). | 해결 |
| `BLK-022` | `V11-M6B-CPP`; C++ contract lane | working tree(2026-07-25, base `e8038df1fa`) | `ctest -R "^test_cpp_framework_target_contract$"` → `CPP-G0-SPOTHANDLE-001: public spot_ref_t address snapshot is still exported`. `BLK-011`이 이관한 GAP 3건 중 남은 1건이다. | 이 gate도 `CPP-G0-ACTOR-002`와 같은 계열로 v11 authority와 어긋났다. `framework/doc/plan/log/framework-public-contract-gap-implementation/cpp-g0-contract-ledger.ko.md`의 v10 목표는 `spot_ref_t`를 opaque `spot_handle_t`로 교체하는 것이지만, v11 exact interface `04-spots.ko.md`는 `spot_ref_t`를 global SpotId·generation·조회 시점 location의 immutable snapshot으로 선언하고 manager `Find`·`Close`와 Actor `FindSpot`이 사용하도록 고정한다. 일반 Spot messaging target은 SpotId이며 `spot_ref_t`가 아니다. 반면 v11 정식 spec 전체에서 `spot_handle_t`·resolver는 선언하지 않는다. | Production API를 v10 draft 방향으로 되돌리지 않고 gate를 v11 exact interface에 다시 pin했다. `spot_ref_t`, `send_to_spot`, `request_to_spot`의 존재를 요구하고 `spot_handle_t`·두 resolver와 `spot_ref_t` direct messaging overload가 없음을 함께 검사한다. 따라서 lifecycle snapshot 유지와 global SpotId messaging을 한 gate에서 구분한다. `framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_target_contract.cpp` | `cmake --build framework/languages/cpp/build --target test_cpp_framework_target_contract -j1` 성공. `test_cpp_framework_target_contract` 단독 실행은 `target contract gate satisfied`, exit 0이다. | 해결 |
| `BLK-030` | `V11-M6-SCAFFOLD-ZERO`, `V11-M7-CONTRACT`, `V11-M8-INVENTORY`; 전 lane 공통 | working tree(2026-07-25, base `2c2999e861`) | 검증 스위트 전수 감사에서 "게이트가 실행하지 않는 test"와 "구조적으로 실패할 수 없는 test"가 다수 확인됐다. 통과 숫자가 실제 커버리지를 나타내지 않으므로 `V11-M6-SCAFFOLD-ZERO`의 "모든 runtime regression 통과" 판정과 M7·M9 게이트가 그대로는 신뢰할 수 없다. | 원인은 개별 결함이 아니라 세 가지 형태다. (1) 검증이 실행되지 않음: 게이트가 태스크를 개별 지정하면서 `check` 연결을 우회했고, 언어별 test 선택이 파일·클래스 목록을 손으로 들고 있다. (2) 검증이 자기 자신과 비교: 기대값을 검증 대상과 같은 낡은 출처에서 작성해 항등식이 됐다. (3) 검증이 권위의 부정을 단언: 커밋 `859fcf07fe`가 exact interface에 타입을 추가하면서 같은 커밋에서 게이트를 존재 금지로 뒤집었다. | 해소한 것: C++ public header contract를 게이트에 편입(`9e102bd237`), JVM `core:test`를 전체 실행으로 확대해 35/98에서 98/98 클래스로(`a5c3ff56ff`), 오류 kind 표를 네 언어에서 전수 검증으로 교체(`1bf62d1550`, `a36ef41335`, `2c2999e861`, java `972f02a054`). 남은 것: .NET `Zlink.Framework.Locations.Redis.Tests`(18파일)와 `Zlink.HttpClient.UnitTests`가 어떤 CI에서도 실행되지 않는다. `Zlink.Framework.M5FoundationTests`는 어느 sln에도 없어 `dotnet build Zlink.Framework.sln`이 컴파일조차 하지 않는다. xUnit이 아니라 `Program.cs` 154줄 console harness이며 내용은 비어 있지 않다 — completion failure result와 raw router lifecycle·multipart ownership을 실제로 검증한다. 중요한 것은 migration inventory가 이 항목을 `disposition: target-contract`, `action: retain-framework-owned-service-runtime`으로 명시하고 removalGate `V11-M8-CLEAN-DN`, finalGate `V11-M9-PKG-DN`을 걸어 뒀다는 점이다. 즉 삭제 대상 잔재가 아니라 유지하기로 한 consumer test인데 아무도 빌드하지 않는다. v11 표면에 대해 컴파일되는지 확인된 바 없어, 깨져 있다면 M8·M9에서야 드러난다. JVM testkit `contractTest`·`fakeBackendTest`, core·kotlin `integrationTest`, codec·redis·http-client·spring-boot 모듈 test가 게이트 밖이다. Node `run_node_framework_ci_gate.js`의 skip 목록 4개 파일이 `verify:p0`에서만 돌고 어떤 workflow도 이를 호출하지 않는다. Node `pickEnumValues`가 실제 enum을 기대 key set으로 투영해 8개 enum에서 추가된 멤버가 보이지 않는다. C++ `test_cpp_framework_channel_messaging.cpp`(3363줄)를 포함해 9개 타깃이 어떤 게이트에서도 빌드되지 않는다. | 진행 중. 각 항목은 게이트에 넣기 전에 실제로 통과하는지 먼저 측정한다. 통과하지 않는 것을 넣으면 row가 막히고, 측정 없이 넣으면 같은 실수를 반복한다. | 조치 중 |
| `BLK-031` | `V11-M6C-JVM`; JVM lane | working tree(2026-07-25, base `fc10c3dcb4`) | `zlink-framework-spring-boot-starter:test`가 실패한다. 최초 기록은 "14건 중 3건"이었으나 이는 truncate된 측정이었다. 실제 module은 31건이고, 최초 실행은 9건만 돈 뒤 JVM이 SIGABRT(exit 134, `Invalid argument (core/src/runtime/utils/mutex.hpp:108)`)로 죽었다. 보고된 3건은 `ZLinkFrameworkAutoConfigurationTest.monitoringOptionsBeanExistsWithoutCustomizerAndDoesNotOpenBackend`(monitoring backend를 열지 않아야 하는데 열림), `ZLinkFrameworkAutoConfigurationTest.autoConfigurationAppliesCustomizersBeforeRuntimeStarts`와 `HostTest.host_startsAndStops_frameworkRuntimeContext`(둘 다 기대 backend 호출 시퀀스와 실제가 불일치)다. `cd framework/languages/java && ./gradlew --console=plain --project-cache-dir build/v11-framework-runtime-gradle-cache -p ../../../scripts/v11/gradle/framework-runtime `:zlink-framework-java-runtime:zlink-framework-spring-boot-starter:test` | 세 실패는 한 원인이다. 실제 호출 로그와 기대의 차이는 monitoring 호출 4~5건뿐이고 `factory.channel → create.context → create.dealer → dealer.setChannelName → dealer.connect`의 상대 순서는 기대와 완전히 같다. 즉 `autoConfigurationAppliesCustomizersBeforeRuntimeStarts`가 이름으로 주장하는 "customizer가 runtime 시작 전에 적용된다" 불변은 깨지지 않았고, ordering regression이 아니다. `assertEquals(List.of(...))`가 호출 로그 전체를 고정하기 때문에 함께 빨개졌을 뿐이다. monitor를 여는 주체는 runtime monitoring 기능이 아니라 ClientServer admission 경로다(`ZLinkChannelRuntime.tryCreateClientServerMonitoringBackend`, `openManualClientServerConnection`). 정식 spec이 이 배선을 지지한다. `09-client-server-channel.ko.md` §4.4는 manual connection도 실제 transport 연결에서 ChannelName·server RID·lifecycle generation·weight·drain state를 확인한다고 규정하고, `29-transport-liveness.ko.md` §3은 transport 연결·service admission·identity 검증이 모두 성공한 뒤에만 ready라고 규정하며 §1은 raw transport monitor 배선을 언어별 internals 소유로 명시한다. 참조 lane인 .NET도 동형이다. `ZLinkChannelRuntimeManager.InitializePublisherChannelsAsync`가 `HasClientServerClient` channel마다 `CreateMonitoringAdapter()`를 만들고, `ZLinkClientServerClientRuntime.Connection` 생성자가 `OpenSocketMonitor`와 `OnEvent`를 `Start()`의 `Socket.Connect`보다 먼저 배치하며 `OnMonitorEvent`의 `ConnectionReady`에서 Hello를 보낸다. 따라서 runtime이 정본이고 세 test의 기대가 stale이다. test 1이 걱정한 "아무도 요청하지 않은 monitoring backend"는 실제로 열리지 않는다. `ZLinkMonitoringLifecycle`은 socket source가 비면 아무것도 열지 않으며, 남는 monitor는 ClientServer client DEALER의 admission monitor 하나뿐이다. 다만 teardown 순서에는 진짜 runtime 결함이 하나 있었다. JVM은 admission monitor를 자기 DEALER보다 늦게 닫았다(`closeClientServerPhysical`이 dealer→monitor 순이고, `closeAll()`은 `ownedSockets`를 삽입 순서로 훑어 같은 결과). `29-transport-liveness.ko.md` §6은 terminal cleanup이 monitor subscription을 connection보다 늦게 남기지 않는다고 규정하고, .NET `Connection.DisposeCoreAsync`도 `_monitor.DisposeAsync()`를 `DisposeSocketAsync()`보다 먼저 실행한다. 이 순서 결함이 assertion 실패보다 큰 피해를 냈다. 같은 module의 handler scan test 4건이 이 지점에서 JVM을 SIGABRT(`UpcallLinker::on_entry`)로 죽이고 있었고, 그래서 31건 중 9건만 실행된 채 "14건 중 3건 실패"로 보였다. | runtime 결함을 근본 수정했다. monitor를 DEALER보다 먼저 닫도록 `closeClientServerPhysical` 순서를 뒤집고, `closeAll()`이 `ownedSockets`를 삽입 순서로 훑기 전에 ClientServer physical connection을 먼저 닫게 했다. `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelSocketRegistry.java` test 세 건은 admission monitor 호출을 포함한 정본 시퀀스로 기대를 교체했다. 세 assertion 모두 exact `assertEquals`로 유지했고 약화하거나 삭제하지 않았다. test 1의 `assertFalse(calls.contains("factory.monitoring"))`은 token 생산자가 두 곳(runtime monitoring 기능, ClientServer admission)이 되면서 판별력을 잃었으므로, "runtime monitoring이 socket monitor를 하나도 열지 않는다"를 직접 확인하도록 `monitoring.open.*` 호출이 ClientServer client DEALER 하나뿐임을 exact list로 단언하게 바꾸고 이름도 `monitoringOptionsBeanExistsWithoutCustomizerAndOpensNoRuntimeMonitoringSocketMonitor`로 맞췄다. `framework/languages/java/zlink-framework-spring-boot-starter/src/test/java/systems/zlink/framework/spring/ZLinkFrameworkAutoConfigurationTest.java`, `.../spring/HostTest.java` (commit `f9a22bb296`) | 증상 칸의 command 기준으로 수정 전 `9 tests completed, 3 failed, 1 skipped` 뒤 SIGABRT(exit 134), 수정 후 `31 tests completed, 4 failed`이고 SIGABRT는 사라졌다. 이 항목이 지목한 3건은 모두 통과한다(`--tests HostTest --tests ...AutoConfigurationTest.monitoringOptions... --tests ...autoConfigurationAppliesCustomizersBeforeRuntimeStarts --tests ...monitoringCustomizerStartsLifecycleAndRegistersRuntimeEventHandlers` → 5/5 BUILD SUCCESSFUL). close 순서 수정이 SIGABRT의 원인이었음은 되돌려 확인했다. 수정 전 source로 handler scan test 4건만 실행하면 다시 exit 134로 죽고, 수정 후에는 `ZLinkFrameworkException`으로 정상 실패한다. core 회귀는 `:zlink-framework-core:test` → 528건 중 1건 실패이며, 그 1건 (`ZLinkAsyncSerialQueueTest.queuedRelocationIntentCannotRacePastYieldRegistration`)은 단독 재실행 3/3 통과하는 기존 flake이고 serial queue 영역이라 이 수정과 무관하다. 남은 starter 4건은 별도 원인이며 `BLK-034`에 기록했다. 따라서 이 module의 게이트 편입은 `BLK-034` 해결 뒤에 한다. | 해결 |
| `BLK-034` | `V11-M6C-JVM`; JVM lane (`BLK-033`과 원인 다름을 실측으로 확인, `BLK-036` 계약 판정은 커밋 `f831f6c781`로 끝남) | working tree(2026-07-25, 조사 base `fc10c3dcb4`, 수정 base `f831f6c781`) | `zlink-framework-spring-boot-starter:test` 31건 중 4건이 `ZLinkFrameworkException: client/server channel has no admitted server: profile`로 실패한다. `annotatedHandlerGroupHandlesRequestsInsideSpringLifecycle`, `scannedHandlersAndCollectionDependenciesAreSpringBeans`, `scannedHandlersAndSetDependenciesAreSpringBeans`, `handlerFiltersAreCreatedThroughSpringDependencyInjection`이다. 네 건 모두 같은 process에 ClientServer client와 server를 같은 inproc endpoint로 등록하고 실제 Java backend로 request를 보낸다. `cd framework/languages/java && ./gradlew --console=plain --project-cache-dir build/v11-framework-runtime-gradle-cache -p ../../../scripts/v11/gradle/framework-runtime `:zlink-framework-java-runtime:zlink-framework-spring-boot-starter:test` | 미확정. 이 4건은 `BLK-031`의 monitor close 순서 결함이 JVM을 SIGABRT로 죽이는 바람에 지금까지 실행조차 되지 않았고, 그 결함을 고친 뒤 처음 드러났다. `BLK-031`의 수정이 원인이 아님은 확인했다. 수정 전 source로 이 4건만 실행하면 실패가 아니라 exit 134로 죽는다. 증상은 같은 process의 local ClientServer server가 admission을 끝내지 못하는 것이며(`clientForOutbound`가 admitted connection을 못 찾음), `09-client-server-channel.ko.md` §5.1이 규정한 "같은 process의 Server도 remote와 같은 후보이며 listener bind와 service admission을 마쳐야 Ready" 계약 구간이다. **`BLK-033`과 같은 원인이라는 위 가설은 실측으로 반증됐다.** JVM의 local ClientServer admission은 실제로 완료된다. `ScannedHandlerConfig`(위 4건 중 `annotatedHandlerGroupHandlesRequestsInsideSpringLifecycle`의 설정이며 나머지 3건도 같은 형태)로 `context.refresh()` 뒤 50ms 간격 재시도를 붙인 일회성 진단 test를 돌리면 attempt 1(경과 132ms)에서 `ProfileReply[value=profile:42]`가 정상 반환된다. 재시도가 admission을 유발한 것이 아니다. 실패 경로는 `clientForOutbound`가 `null`을 돌려주고 `hasClientRegistration`이 참이라 곧바로 throw하는 것뿐이라 부작용이 없으며, admission은 이미 monitor callback에서 진행 중이었다. 즉 JVM은 listener도 있고 admission도 끝나며 DEALER→ROUTER 왕복도 성공한다. 진짜 원인은 **startup readiness race**다. `ZLinkChannelRuntime` 생성자의 `attachProcessLocalClientServerAdmissions`가 local server descriptor endpoint로 manual connection을 열지만 admission은 monitor event 기반 비동기이고, `sendToChannel`·`requestToChannel`은 admitted connection이 아직 없으면 기다리지 않고 즉시 `ROUTE_NOT_CONNECTED`로 throw한다(`ZLinkChannelRuntime.java:876-881`, `:908-913`). test는 `context.refresh()` 직후 호출하므로 항상 진다. 참조 lane인 .NET은 호출 시점에 기다린다. `ZLinkClientServerClientRuntime.WaitForReadyAsync`가 `min(requestTimeout, 5초)` deadline 안에서 5ms 간격으로 `SelectReady()`를 재확인하고, 그래도 없으면 `RequestTargetNotFound`로 끝낸다(`framework/languages/dotnet/src/Zlink.Framework/Runtime/Channels/ZLinkClientServerClientRuntime.cs:127-176, 443-458`; 이 파일은 동시 재작성 중인 .NET 변경 목록에 없어 HEAD 상태 그대로다). Node도 JVM과 같아서 `channel-outbound-operations.ts:44-47, 137-143`이 즉시 실패한다. 이 축은 그 시점에 정식 spec이 규정하지 않았으나(`BLK-036`), 커밋 `f831f6c781`이 `08-channel-messaging.ko.md` §3.2에 .NET 형태를 명문화하면서 판정이 끝났다. 규정된 규칙은 "ClientServer는 ready 후보가 없으면 호출 시점에 제한된 시간 동안 기다린 뒤 target 없음으로 실패하고, 대기 한도는 해당 호출의 request timeout과 5초 중 짧은 쪽이며, framework startup은 local ClientServer admission을 기다리지 않는다"이다. 대기는 admission을 유발하지 않고 이미 진행 중인 admission만 기다리며 remote 후보에도 같게 적용한다. 추가로 오류 분류 자체도 이미 spec 위반이었다. `06-framework-api.ko.md` §13 표 다음 문단은 `RouteNotConnected`를 "알려진 target의 pipe가 준비되지 않은 상태", `RequestTargetNotFound`를 "등록한 송신 경로에 현재 선택 가능한 target snapshot이 없는 상태"로 갈라놓는다. `clientForOutbound`가 `null`을 돌려주는 것은 후자이므로 기존 `ROUTE_NOT_CONNECTED`는 대기 규칙과 무관하게 잘못된 kind였다. 부수적으로 test 설정 자체에도 별개 결함이 하나 있다. 커밋 `164007cf52`가 `enableServer(<endpoint>)`를 `server().listen()`으로 기계적으로 옮기면서 endpoint 인자를 버렸고, 그 결과 server는 `tcp://0.0.0.0:0`에 bind하고 client는 아무도 bind하지 않는 `inproc://zlink-spring-…`로 manual connect한다. 현재 exact interface의 ClientServer server builder에는 endpoint 형태가 없어(spec `09-client-server-channel.ko.md` §4.2는 `Listen(int port = 0)`만 정의) 원래 형태로 되돌릴 수 없다. 이 죽은 manual endpoint는 후보를 하나 줄일 뿐 race의 원인은 아니다. | 두 축을 각각 근본 수정했다. **(1) readiness 대기(커밋 `e782afd374`).** `ZLinkChannelSocketRegistry`에 `awaitClientForOutbound(channelName, bound)`를 두어 ready 후보가 생길 때까지 5ms 간격으로 `clientForOutbound`를 다시 확인한다. 이 method는 의도적으로 `synchronized`가 아니다 — admission은 monitor thread의 `admitClientServerConnection`이 같은 monitor를 잡아야 진행하므로, 매 시도의 `clientForOutbound` 호출만 monitor를 잡고 sleep은 잡지 않는다. 참조 lane도 같은 구조로, `SelectReady()`가 내부에서 `_gate`를 잡고 `Task.Delay`는 밖에 있다. 대기는 `openManualClientServerConnection`·`reconnectClientServerConnection`을 부르지 않으므로 admission을 유발하지 않고, startup 경로에는 어떤 대기도 넣지 않았다. `ZLinkChannelRuntime.awaitClientServerTarget`이 한도를 `min(defaultRequestTimeout(channelName), 5초)`로 계산하고 실패 시 `REQUEST_TARGET_NOT_FOUND`로 끝낸다. 대기는 weight 0·draining으로 제외된 후보에도 똑같이 적용한다. `SelectReady()`가 `Ready && Weight > 0`로 거르므로 .NET도 그 경우에 기다리며, spec §3.2는 "ready 후보가 없으면"만 조건으로 둔다. **참조 형태와의 차이 2건(ledger §2.3 7단계 기록).** 첫째, 대기 한도를 Channel 설정 request timeout에서 뽑는다. .NET `WaitForReadyAsync`도 생성자에서 잡아둔 `_requestTimeout`을 쓰고 `RequestAsync`의 per-call `timeout` 인자를 쓰지 않으므로 동형이며, 따라서 per-call `.timeout()` override는 대기를 줄이지 않는다. 둘째, JVM은 ClientServer target을 async operation이 아니라 call builder에서 고른다. 이는 이번 변경 이전부터 있던 형태 차이이고, 참조의 component 경계(연결·admission·선택을 소유하는 곳에 대기를 둔다)를 지키려고 선택 지점을 옮기지 않고 그 자리에 대기를 넣었다. **(2) 죽은 manual endpoint(커밋 `ad2c7b43f1`).** 네 config에서 아무도 bind하지 않는 inproc endpoint로의 `client().connect(...)`를 걷어내고 `channel.client()`만 남겼다. spec `09-client-server-channel.ko.md` §5.1은 같은 process의 Client·Server 등록만으로 local Server를 후보로 인정하며 manual endpoint나 Location Store를 요구하지 않는다. `connect()` 생략은 automatic discovery를 켜지 않으므로 §4.2 주석과 §4.4의 store 요구도 걸리지 않는다. 구현도 같다 — `installClientServerLocationRuntime`은 `store()`가 null이면 no-op이고, `attachProcessLocalClientServerAdmissions`는 `clientEnabled() && !serverBinds().isEmpty()`에서 돈다. 네 scenario가 검증하는 것(scanned handler·collection·set 의존성 주입·DI filter의 Spring auto-configuration)은 그대로이고 assertion은 하나도 바꾸지 않았다. **낡은 기대 2건 교체.** 위 §13 판정에 따라 `ZLinkChannelRuntimeTest.canSelect`(`:test`)와 `ChannelMessagingTest`(`:integrationTest`)의 `ROUTE_NOT_CONNECTED` 기대를 `REQUEST_TARGET_NOT_FOUND`로 바꿨다. 두 곳 모두 exact kind를 exact throw에 대해 계속 단언하며 약화나 삭제가 아니다. 변경 path: `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelSocketRegistry.java`, `.../channels/ZLinkChannelRuntime.java`, `.../src/test/java/systems/zlink/framework/runtime/channels/ZLinkChannelRuntimeTest.java`, `.../src/integrationTest/java/systems/zlink/framework/runtime/ChannelMessagingTest.java`, `framework/languages/java/zlink-framework-spring-boot-starter/src/test/java/systems/zlink/framework/spring/ZLinkFrameworkAutoConfigurationTest.java`. Kotlin은 같은 JVM runtime을 공유하므로 별도 수정이 없다. | 증상 칸의 command로 수정 전 `31 tests completed, 4 failed`, 수정 후 **31 tests / 0 failed / 0 skipped**(`BUILD SUCCESSFUL`, test-results XML 집계)다. 두 변경의 기여도를 분리해 실측했다. (2)만 적용한 중간 상태는 여전히 `31 tests completed, 4 failed`이므로 (2)는 죽은 재연결 intent를 없앤 것이고 네 건을 초록으로 만든 것은 (1)이다. core 회귀는 `:zlink-framework-java-runtime:zlink-framework-core:test` → **528건 / 0 실패 / 0 skip**이며 연속 2회(두 번째는 `--rerun-tasks`) 모두 같은 결과다. `BLK-031` 시점에 1건 실패로 잡혔던 기존 flake `ZLinkAsyncSerialQueueTest.queuedRelocationIntentCannotRacePastYieldRegistration`은 두 실행 모두 발화하지 않았다. 대기 도입으로 늘어난 시간은 `ZLinkChannelRuntimeTest` 10.7초 하나이며, weight 0·draining을 기대하는 negative probe 2건이 각각 5초 한도를 소진한 결과로 예측치와 일치한다. core test에서 no-candidate 경로를 타는 호출부는 그 helper 하나뿐임을 전수 확인했다. `:integrationTest` source set은 이 저장소 HEAD에서 이미 compile되지 않는다(오류 100건, `ZLinkActorJoinResult` 등 무관한 표면). `ChannelMessagingTest.java`를 stash한 채 다시 compile해도 오류 수가 같아 이번 변경과 무관한 기존 상태임을 확인했다. 따라서 그 파일의 교체된 기대는 실행으로 검증하지 못했고, source set이 compile되는 시점에 함께 확인해야 한다. | 해결 |
| `BLK-032` | `V11-M6A-CPP`; C++ lane (결정은 `V11-M7-CONTRACT`) | working tree(2026-07-25, base `c3cc26f50a`) | `BLK-030`이 지목한 미빌드 타깃 중 `test_cpp_framework_channel_messaging.cpp`를 CMake에 등록하려 하자 링크가 `route_channel_host_service_t`의 생성자·소멸자·`start`·`stop` undefined reference로 실패했다. `cmake --build .artifacts/v11/build/framework-runtime-regression/cpp --target test_cpp_framework_channel_messaging` | `1de8f43917`이 `framework/src/runtime/channels/route_channel_host_service.cpp`를 `zlink_framework` source 목록에서 빼면서 파일만 tree에 남겼다. 이 파일은 Core 11 표면에 대해 아예 compile되지 않는다(`router_socket_options_t`에 `heartbeat_interval`·`heartbeat_timeout` 없음, `router_socket_t::reply_to_spot` 없음, `received_t::spot_id` 없음, 같은 커밋이 `route_channel_runtime_t::attach_native_backend` 삭제). 유일한 협력자 `native_route_backend_t`는 library에 남아 있지만 모든 `target_spot_id`를 "legacy RouteChannel Spot bridge is unavailable"로 거부한다. 정식 spec 어디에도 RouteChannel이 없다: C++ exact interface `server/languages/cpp/interfaces/03-channel-messaging.ko.md`의 `route_client_t`에 `route_channel_builder_t`가 없고, `08-channel-messaging.ko.md` §1·§3.1은 Node direct를 MeshName+MeshNode RID로 정의한다. 즉 amendment가 버린 표면이다. | Runtime은 건드리지 않았다. 해당 scenario(원본 2158-2417행)를 verbatim으로 `framework/languages/cpp/tests/Zlink.Framework.UnitTests/pending/route_channel_host_service_scenarios.pending.inc`에 보존하고 build graph에서만 빼 ledger §2 규칙 13·14의 `pending-disabled-by-contract-amendment`로 고정했다. 삭제나 주석 처리는 하지 않았고 원본 위치에는 참조 주석만 남겼다. 나머지 파일은 v11 표면으로 이관해 `add_zlink_framework_test(... "framework-unit;framework-regression;channel")`으로 등록했다. 참고로 impact manifest 항목 `regression-test:cpp:fc3afa2d3db01201`의 `baselineHash 85db2b9b…`는 HEAD의 `a4253fb1…`와 이미 어긋나 있으며 (이번 변경 이전부터), manifest는 coordinator 소유라 손대지 않았다. | `cmake --build ... --target test_cpp_framework_channel_messaging` 통과(링크 오류 해소). 격리한 gate 384·385·386(2회)·387·388은 실행하지 않으며 skip이나 성공으로 계산하지 않는다. | 해결 |
| `BLK-033` | `V11-M6A-CPP`; C++ ClientServer lane | working tree(2026-07-30, base `9ff7e843ceb2`) | C++ ClientServer server가 Location discovery 여부에 따라 서로 다른 두 owner로 나뉘어 있었다. Store가 없는 manual server는 listener가 생성되지 않아 같은 process request가 `ECONNREFUSED`로 끝났다. | Listener bind와 Location descriptor publication을 같은 `discovery` flag로 분기해 두 runtime이 상호 배타적으로 server를 소유했다. Discovery는 client의 endpoint 획득 방식이지 server listener의 존재 조건이 아니다. | `client_server_location_runtime_t`가 모든 bind된 ClientServer server listener를 하나만 소유한다. Discovery server만 owner lease를 요구하고 descriptor를 Store에 게시한다. Manual server는 같은 listener·admission 경로를 사용하되 Store row를 만들지 않는다. `channel_host_service_t`는 이 shared runtime이 활성화되면 중복 listener를 만들지 않는다. | `test_cpp_framework_channel_messaging` 전체 통과. Fresh current-worktree `M6-RUNTIME` 10/10과 `ROW-GATE` files 4·commands 10 통과. 증거: `.artifacts/v11/evidence/V11-M6A-CPP/result-clientserver-20260730.json`. | 해결 |
| `BLK-035` | `V11-M6-SCAFFOLD-ZERO`, `V11-E2E-SPEC-FINAL`; coordinator | working tree(2026-07-25, base `fc10c3dcb4`) | impact manifest 항목 `regression-test:cpp:fc3afa2d3db01201`은 `framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp`에 대해 `disposition: retain`, `quarantineStatus: active-regression`, acceptanceIntent "assertion과 실행 registration을 유지하고 runtime 구현 중 계속 실행한다"를 선언한다. 그런데 이 파일은 CMakeLists 등록이 없어 target도 ctest도 아니었고 한 번도 실행되지 않았다. | manifest는 "실행 registration 유지"를 선언하지만 이를 검증하는 게이트가 없다. `AMENDMENT-IMPACT`는 `active-regression` 항목을 quarantine 모드에서 의도적으로 건너뛰고(구현 중 변경은 정상), `raw-regression-test` 종류만 현재 content hash를 대조한다. `DOC` 게이트는 manifest 구조를 검사하고 ledger row를 파싱하지만 선언된 파일이 실제 build graph에 들어 있는지는 확인하지 않는다. 즉 manifest가 "계속 실행한다"고 적어둔 파일이 어떤 build system에도 등록되지 않은 채 남아 있어도 모든 게이트가 조용하다. | `scripts/v11/run-framework-runtime-regression.mjs`의 C++ plan에 `test_cpp_framework_channel_messaging` build target과 CTest pattern을 required internal regression으로 추가했다. CMake registration과 formal gate registration이 모두 존재하므로 retained test가 더 이상 미실행 상태가 아니다. | Fresh C++ `M6-RUNTIME`은 internal 5/5와 전체 required command 10/10을 통과했다. 증거는 `.artifacts/v11/evidence/V11-M6A-CPP/result-clientserver-20260730.json`이며 SHA-256은 `1c8966c89ac494e283da16bc8a9b4890092a9d498d29acea4128e2ba02a6a1fb`다. | 해결 |
| `BLK-036` | `V11-M6C-JVM`, `V11-M6C-NODE`, `V11-M7-CONTRACT`; coordinator 계약 판정 (`BLK-034` 선행) | working tree(2026-07-25, base `04ffe9f683`) | 정식 spec이 "ClientServer 호출 시점에 ready 후보가 아직 하나도 없을 때"의 동작을 규정하지 않는다. 네 lane이 서로 다르게 구현했고 그 차이가 `BLK-034`의 4건을 red로 만든다. 판정 없이는 JVM·Node lane이 자기 행동을 고칠 근거가 없다. 조사 command는 `BLK-034` 행과 같다. | spec `08-channel-messaging.ko.md` §5는 RouteMesh에 대해서만 "후보가 없으며, 이때는 target 없음으로 실패한다"를 명시하고 ClientServer에는 같은 문장을 두지 않는다. `09-client-server-channel.ko.md` §5는 weight 비교 대상을 `Ready`이고 draining이 아닌 Server로 한정하지만 후보 집합이 빈 경우를 다루지 않고, §5.1도 local Server가 "listener bind와 service admission을 마쳐 Ready"여야 한다고만 적는다. §4.4는 startup 실패 조건을 automatic discovery + Location Store 부재 하나로만 legislate하며, `06-framework-api.ko.md` §14의 startup validation은 전부 설정 검증이라 admission 완료를 startup 완료 조건으로 두지 않는다. `29-transport-liveness.ko.md` §2.1의 ready 정의는 connection 단위이고 host startup 단위가 아니다. 따라서 다음 두 규칙이 spec에 없다. (1) framework startup(또는 host lifecycle의 running 전이)이 같은 process의 local ClientServer server admission 완료를 기다려야 하는가. (2) 기다리지 않는다면 첫 send·request가 ready 후보를 유한 시간 동안 기다려야 하는가, 아니면 즉시 target 없음으로 실패해야 하는가. 구현은 이미 갈라져 있다. .NET은 (2)의 "기다린다"를 택해 `min(requestTimeout, 5초)` 안에서 5ms 간격으로 재확인한 뒤 `RequestTargetNotFound`로 끝내고(`ZLinkClientServerClientRuntime.cs:443-458`), JVM과 Node는 즉시 실패한다(`ZLinkChannelRuntime.java:876-881`·`:908-913`, `channel-outbound-operations.ts:44-47`·`:137-143`). | 아직 수정하지 않았다. spec이 침묵하는 축이라 언어 lane이 wait를 발명하면 §18 `I4` 동형 수렴에서 다시 뒤집힌다. 규칙을 만들어내지 않고 빠진 문장만 지목한다. | 계약 판정 완료. `08-channel-messaging.ko.md` §3.2에 ClientServer ready 후보 부재 시 호출 시점 제한 대기(`min(request timeout, 5초)`)와 그 뒤 target 없음 실패를 명문화했다(커밋 `f831f6c781`). JVM 구현 완료(`e782afd374`), spring-boot starter 31/31·core 528/0·kotlin 48/0으로 재검증하고 게이트에 편입했다(`af0c8d5091`). 실측으로 확인된 사실 하나: readiness 대기 단독으로 네 건이 초록이 됐고 죽은 endpoint 제거 단독으로는 4건이 그대로 실패했다 — 원인은 대기 부재였다. Node 구현(`channel-outbound-operations.ts:44/137`)은 JVM이 확정한 .NET 미러 형태를 따라 진행한다. C++는 `BLK-033`으로 listener가 복구되기 전까지 이 축을 평가할 수 없다(후보가 존재하나 아직 ready가 아닌 상태에 도달한 적이 없다). **Node 구현 완료(커밋 `9b3c47ba29`).** 대기는 연결·admission·가중 선택을 소유하는 `channel-socket-registry.ts`의 `awaitClientDealerForOutbound(channelName, signal?)`에 두었고, 실패를 던지던 `channel-outbound-operations.ts`에는 두지 않았다. 한도는 registry 안에서 `min(Channel request timeout, 5초)`로 계산한다. **참조 형태와의 차이 1건(ledger §2.3 7단계 기록).** JVM은 caller가 한도를 넘기지만 .NET은 `WaitForReadyAsync` 안에서 생성자에 잡아둔 `_requestTimeout`을 읽는다. 값은 같고(per-call timeout override는 어느 lane에서도 대기를 줄이지 않는다), per-call timeout 인자가 없는 send 경로도 같은 한도를 쓰게 된다. **Node의 실행 모델 위험은 event loop 기아다.** ClientServer admission은 backend poll timer(`node-raw-mesh-backend.schedulePoll`)가 펌프하는 monitor callback에서 끝나므로, 동기 대기는 자기가 기다리는 admission을 굶기고 한도를 다 쓴 뒤 실패한다. 그래서 매 시도 사이에 실제 timer를 `await`하고 abort signal은 .NET이 `Task.Delay`에 cancellation token을 넘기는 것과 같게 매 회 확인한다. weight 0·draining 때문에 비어 있는 후보 집합에도 똑같이 기다린다. `trySend`는 즉시 실패로 남겼다 — try 형 변형이고 `ZLinkChannelClientTransport`에 optional로 선언돼 아무도 호출하지 않는 pass-through 사슬로만 닿는다(POSD 부채로 관찰만 기록, 이번 범위에서 삭제하지 않음). 공개 `sendToChannel` 경로는 `DefaultZLinkChannelClient` → `ZLinkRuntimeChannelTransport.send` → `ZLinkChannelRuntimeManager.send` → `ZLinkChannelOutboundOperations.send`(비동기)이므로 spec 규칙을 구현할 수 있으며 실제로 대기한다. 즉 Node의 공개 send 경로가 동기라서 규칙을 못 지킨다는 가정은 성립하지 않아 새 BLK row를 열지 않았다. 오류 kind도 같은 위반이었다: `request`는 `RequestTargetNotFound`로, 두 one-way 경로는 `TargetNotFound`(`requireOneWayCompletion`이 이미 `RequestTargetNotFound`로 승격)로 고쳤다. 변경 path: `framework/languages/node/packages/framework/src/runtime/channels/channel-socket-registry.ts`, `.../channels/channel-outbound-operations.ts`, `framework/languages/node/test/contract/client-server-location-runtime.test.js`. Node 실측: `cd framework/languages/node && node node_modules/typescript/bin/tsc -b tsconfig.build.json` 통과. `node --test --test-force-exit test/contract/<file>` 결과는 `client-server-location-runtime` **22/22**(변경 전 18/18, 신규 4건 추가. cap 검증 1건이 5.0초를 소모하며 이는 대기가 실제로 발화한 증거다), `backend-contract` 30/34(남은 4건은 `BLK-006`으로 변화 없음), `fanout-location-runtime` 4/4, `drain-control` 8/8, `channel-envelope-error` 2/2, `actor-client` 12/12, `spot-manager` 58/58, `entry-spot-serial-dispatch` 22/22, `message-packet-name` 2/2. `contract-surface` 26/28과 `nestjs-module` 33/60은 이번 변경 이전부터 실패하던 catalog 선언 누락과 `User Spot creation requires a Location Store.`이며 이 경로와 무관하다. **남은 coverage 공백:** `channel-client.test.js`가 `ZLinkChannelRuntimeManager`를 네 곳에서 만들어 이 경로를 가장 많이 건드리지만 `BLK-009`로 hang하고 동시에 다른 작업자가 편집 중이라 실행도 수정도 하지 않았다. 따라서 이 동작의 회귀는 `client-server-location-runtime`에만 있다. 후속 커밋 `2c1269194c`가 두 성질을 추가로 고정했다. (1) 대기가 admission을 유발하지 않는다는 조항을 dealer 생성 횟수로 단언한다. (2) 오류 kind가 retry 정책을 함께 나른다 — `RequestTargetNotFound`는 .NET `IsRetriableByDefault`, JVM `ZLinkFrameworkErrorKind.retriable`, Node `isZLinkFrameworkErrorRetriableByDefault` 셋 모두에서 비재시도이고 세 lane의 참조 throw가 모두 flag를 생략하므로 동형이다. 이전 `RouteNotConnected` throw는 `true`를 명시했으므로 이 변화는 단언 없이는 보이지 않았다. 같은 파일을 `--test-force-exit` 없이 실행해도 22/22로 exit 0이다(대기 중 dispose를 관측하는 경로는 없으며, 참조 .NET `WaitForReadyAsync`도 caller token만 보고 connection의 stop token은 보지 않는다). C++는 `BLK-033` 해결 뒤 bounded wait와 오류 분류를 재확인했다. Wait는 이미 `min(request timeout, 5초)`와 condition-variable wake를 사용했고, 만료 시 selectable Ready server가 없으면 `RequestTargetNotFound`를 반환하도록 교정했다. Fresh C++ M6-RUNTIME 10/10과 row gate가 통과했다. | 해결 |
| `BLK-040` | `V11-M6A-NODE`, `V11-M6B-NODE`, `V11-M6C-NODE`; Node.js lane | working tree(2026-07-25, base `c465f4d85e`) | `node --test --test-force-exit test/contract/channel-client.test.js` → 94 tests, 74 pass / 20 fail. `BLK-023`(3-인자 arity와 client 클래스 오선택)이 34건 중 14건을 해소했고 남은 20건은 원인이 다르다. 두 번 실행에서 실패 집합이 동일하며 baseline 34건의 진부분집합이다(신규 실패 0). | 원인이 최소 여섯 갈래다. (1) subtest 48·50·56 `Channel has no admitted ClientServer target` — C++ `BLK-033`·JVM `BLK-034`와 같은 모양이며 Node 항목이 없었다. `BLK-036`의 Node 구현으로 해소되는지 먼저 확인한다. (2) 34·35·37~40 route bridge `ZLink async submit timed out`. (3) 23 Nest DI token drift. (4) 43 `SpotNode router must define a bind endpoint`. (5) 53·54 location peer 미수렴, 65·66 `User Spot creation requires a Location Store`. (6) 67~69 backpressure. | 아직 수정하지 않았다. `BLK-023` 담당자가 원인을 분류만 하고 항목을 발급하지 않아 20건이 무주공산이었다. 이 행이 그 소유권을 받는다. 갈래별로 분리가 필요해지면 하위 항목으로 쪼갠다. | (1)은 `BLK-036` Node 구현 뒤 재측정한다. 나머지는 갈래별로 spec 대조 후 런타임·test 귀속을 판정한다. 현재 candidate에서 여섯 갈래의 후속 변경이 모두 합류했고 같은 file이 91/91, exit 0으로 통과했다. 실패 집합·hang·cancelled subtest가 남지 않아 별도 하위 항목은 열지 않는다. | 해결 |
| `BLK-041` | `V11-M7-SAMPLES`, `V11-SAMPLE-SPEC-FINAL`; Node.js lane | working tree(2026-07-30) | Node 샘플 `.ts` 소스가 workspace typecheck 대상이 아니어서 제거된 호출과 타입 오류를 검출하지 못했다. | `tsconfig.json`에 `samples/**/*.ts`를 추가했다. 현재 샘플의 channel 호출은 2-인자 계약으로 정렬했고, Nest factory dependency tuple, Bingo nullable state와 TicTacToe HTTP 입력 타입 오류를 수정했다. | Sample 전수 typecheck와 lint를 required workspace 검증으로 실행한다. | `npm run typecheck`, `npm run lint -- --no-cache`, `npm run build`가 모두 통과했다. | 해결 |
| `BLK-042` | `V11-M6C-NODE`; Node.js lane | working tree(2026-07-30) | Production host relocation의 User Spot aggregate·standalone Actor·Instance Spot two-owner 검증이 없었다. | 실제 `relocateMesh()` inventory와 production ports를 사용하는 contract를 추가했다. 검증 중 durable root와 service-wire participant 순서 불일치로 Actor queue가 잘못 연결되는 결함을 찾아 authority-key 기준 canonical 순서로 정렬했다. | User Spot·member Actor·Instance Spot·standalone Actor의 hidden restore, authority·membership commit, queue·timer replay, Message Follow, command 44·45 route ACK, source cleanup과 target admission을 한 test에서 검증한다. | 신규 two-owner 1/1, M6C 79/79, command 44·45 focused 5/5, workspace build와 production ESLint 통과. Actual mixed-language process는 M7이 소유한다. | 해결 |
| `BLK-043` | `V11-M6C-JVM`; JVM lane | working tree(2026-07-26, base `01b13d41cb`) | `ZLinkUserSpotRetireRuntime`이 host composition에 생성되지만 target endpoint에 `(lane, record) -> completedFuture(null)` journal replayer와 `request -> completedFuture(null)` steady normalizer를 넘긴다. `supportsActiveInventory()`는 User Spot aggregate member Actor만 허용해 standalone Actor가 하나라도 있으면 active Retire를 차단한다. 추가 production audit에서 source builder가 accepted journal이 비어 있지 않으면 `requireReplaySupport()`로 실패하고, serial queue commit이 captured·held request의 source reply capability를 즉시 해제하는 것도 확인했다. | Component 단위 aggregate coordinator·staging owner·control wire는 구현됐지만 production accepted journal dispatch/reply relay, Completed authority 뒤 steady normalization, standalone Actor relocation owner가 composition root에 연결되지 않았다. Frozen record에는 sequence와 payload만 남고 original request의 reply capability·source lease fence가 없어 target terminal을 source에 relay·ACK할 수 없다. No-op callback은 target replay와 normalization을 완료한 것처럼 보이게 하므로 component test만으로 실제 continuity를 증명하지 못한다. | Existing accepted journal record에 source reply capability와 exact owner·lease fence를 durable하게 보존하고 실제 hidden Spot·Actor dispatch terminal을 command 기반 source relay와 closed ACK로 연결한다. ACK 또는 exact source lease expiry proof 전에는 capability와 recovery root를 해제하지 않는다. Completed aggregate authority를 확인한 뒤 recovery pointer와 admission을 정상화하는 owner를 구성한다. Standalone Actor는 같은 deep component를 재사용하는 별도 relocation unit으로 inventory·capture·remote restore한다. 새 public API나 Core·bindings 우회는 추가하지 않는다. | 완료 gate는 production `ZLinkFrameworkRuntime.retire()`를 사용하는 two-owner User Spot aggregate와 standalone Actor process test에서 factory·Restore, accepted queue·timer replay, Session route ACK, steady normalization과 source cleanup을 확인하고 JVM `M6-RUNTIME`을 통과하는 것이다. | 해결 |
2026-07-27 JVM 후속 구현은 Entry Spot standalone Actor를 실제 relocation control 경로에 연결했다.
Hidden target restore, source freeze, authority commit, authority가 선택한 journal replay, target publish,
source cleanup, Completed 기록과 steady authority normalization을 수행한다. Java core 591/591과
Kotlin 46/46이 통과했고 object generation 유지와 authority owner generation 증가를 확인했다.
남은 범위는 target inbound permit, accepted-journal reply의 durable completion·ACK, bound Session route
ACK, PerActor timer capture·복원, commit 이후 process recovery scanner와 실제 두 process E2E다.
따라서 `BLK-043`은 계속 `조치 중`이다.

2026-07-30 JVM production replay 후속 구현은 composition의 성공 no-op journal replayer를
fail-closed fallback으로 교체했다. Frozen Spot·Actor record가 source node RID·lifecycle generation,
source owner ID·lease generation, operation ID와 reply route를 canonical bytes에 보존하는 것도
확인했다. Durable envelope decoder는 participant·sequence prefix를 포함한 raw entry와 handler에
전달할 frozen operation을 분리한다. 따라서 hidden handler는 실제 operation bytes를 받는다.

Standalone Actor도 User Spot과 같은 `ZLinkAcceptedJournalReplayer`를 사용한다. Hidden Actor dispatch
terminal을 durable completion에 먼저 기록하고 command 33·46 ACK 또는 exact source lease expiry를
확인한 뒤 Completed authority normalization과 admission open을 진행한다. Steady normalizer는 production
composition에서 `normalizeCompletedAggregate`에 연결되어 있어 no-op이 아니다. Canonical reply capability
focused test, source·target owner relocation test, Java core 전체 test와 `M6-RUNTIME` 11/11,
`ROW-GATE` files 403·commands 11이 통과했다.

남은 blocker는 실제 두 process `ZLinkFrameworkRuntime.retire()`에서 accepted request reply와 bound
Session route ACK를 함께 관찰하는 process E2E, PerActor timer 복원과 commit 이후 restart recovery
scanner다. Component callback으로 성공을 반환하는 우회는 남기지 않았다. 이 조건 때문에
`BLK-043` 전체 상태는 `조치 중`을 유지한다.
Standalone Actor 후속 교정에서 identity key 변환, source cleanup lease proof, exact published attempt ACK-loss
reconciliation, command 35 pending state 거부, bounded target stage와 canonical Actor accepted journal을 연결했다.
Focused 6/6·3/3·13/13, Unit 999/999, Contract 65/65, build warning·error 0과 private JSON journal symbol
0이 통과했다. 이 결과는 `.NET` standalone Actor 경로의 교정 증거이며 hosted crash/restart,
mixed-language process와 final post-review가 남아 있으므로 `BLK-044`는 `조치 중`을 유지한다.

2026-07-30 Node Redis aggregate provider 후속 구현은 `BLK-042`에서 확인한
process-local Location authority의 하위 gap을 닫았다. Production
`ZLinkLocationStoreRepository`가 aggregate participant inventory를 최대 1,024개와 encoded
1 MiB로 제한한 immutable page에 저장하고, authority fence와 target capacity를
`Prepare` CAS로 고정한다. `Commit`은 aggregate authority와 source·target capacity를 한
provider batch로 공개하며, `Abort`는 해당 aggregate fence와 pending capacity만 되돌린다.
공개 SPI, service wire와 physical connection은 추가하지 않았다.

실제 Redis test 8/8에서 10,100 participant page, 동시 동일 `Prepare`의
`Prepared`·`AlreadyPrepared` 수렴과 다른 fingerprint 충돌을 확인했다. Abort
publication 직후 child process를 exit code 92로 종료한 뒤 다른 process가 participant
fence cleanup을 완료했다. Aggregate publication 직후에는 exit code 91로 종료하고 다른
process가 committed authority를 읽어 normalization을 완료했다. Inventory page checksum
손상은 commit 전에 검출했다. Fresh candidate
`.artifacts/v11/evidence/V11-M6A-NODE/candidate-provider-aggregate-20260730.json`과 result
`.artifacts/v11/evidence/V11-M6A-NODE/result-provider-aggregate-20260730.json`에서
`M6-RUNTIME` 7/7과 `ROW-GATE` files 336·commands 7이 통과했다.

따라서 `BLK-042`의 provider-backed aggregate paging·publication·abort·recovery 하위
조건은 해결했다. Active workload의 전체 host relocation과 mixed-language process
조건은 기존 `BLK-042`와 `BLK-044`가 계속 소유하므로 두 blocker의 전체 상태는
`조치 중`을 유지한다.

| `BLK-044` | `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE`, `V11-M6C-CPP`, `V11-M6-DN-REFERENCE`; relocation wire lanes | working tree(2026-07-26, base `01b13d41cb`) | 독립 .NET final delta review에서 maintenance relocation이 공통 service wire command 30~35·40~46 대신 `.stage.v1`·`.publish.v1`·`.abort.v1`·`.reply.v1` private typed packet을 사용해 mixed-language relocation이 성립하지 않는 critical finding이 나왔다. 교차 조사 결과 Java/Kotlin과 Node도 언어별 private control envelope를 사용하고 C++는 production canonical relocation command 연결이 완료되지 않았다. | 공통 schema는 command ID와 field 계약을 정의했지만 각 runtime의 production relocation transport가 그 codec과 infrastructure dispatch를 사용하도록 묶이지 않았다. Component test가 언어별 encoder·decoder끼리만 왕복해 wire drift를 검출하지 못했다. | 네 runtime의 private relocation packet·wrapper를 제거하고 `service-wire-v1.schema.json`의 field order·bounds를 그대로 구현한 codec을 raw RouteMesh infrastructure dispatch에 연결한다. stable relocation/coordinator identity, exact source owner·lease·node·generation fence와 terminal relay ACK를 같은 command 계약으로 보존한다. 공통 `golden/reply-relay-v1.json`을 기준으로 .NET·JVM·Node·C++ command 33·46 codec이 byte parity를 확보했다. C++은 raw transport dispatch와 exact request-source peer fence를 연결했고, .NET도 command 33·46을 managed raw infrastructure dispatch에 연결하고 `.reply.v1` packet을 제거했다. Pending relay는 ACK target RID·operation·relocation·coordinator로 분리하며 expected source owner·lease·node·generation과 admitted lifecycle을 exact 검증한다. ACK-loss retry·`AlreadyTerminal` duplicate·source collision·non-Ok payload negative를 포함한 focused 6/6, Unit 964/964, Contract 65/65와 protocol asset gate가 통과했다. JVM도 command 33과 command 46을 각각 독립 raw send로 전환하고 authenticated source RID·lifecycle, 중복 pending, ACK timeout과 stale generation을 검증했다. .NET은 command 30·31·32·34·35·40·41 exact codec, 공통 golden 7/7, full frozen-record kind 1~14 byte preservation과 relocation phase 0~9 검증을 완료했다. Unit 967/967, Contract 65/65와 protocol gate가 통과했다. 이 결과는 codec checkpoint이며 production packet 전환 완료 증거가 아니다. .NET target staging의 User Spot `stableType`은 exact Spot authority allocation에서, Actor identity·`stableType`·authority payload는 `zla1:a:` authority scan과 steady authority payload에서 얻을 수 있으므로 새 command 40 field는 필요하지 않다. 남은 .NET 선행 조건은 정식 command `40→30 offer→30 accept→41` state machine과 schema의 empty offer participant vector·공통 golden의 non-empty vector 불일치를 먼저 해소하는 일이다. JVM command 30~35·40~41 전체 전환, Node command 33·46과 네 runtime production wrapper 제거도 남아 있다. | 공통 byte golden fixture를 네 언어가 읽고 쓰는 test, 모든 방향의 mixed-language process relocation E2E, ACK-loss retry·source lease expiry·duplicate terminal replay가 통과해야 한다. 두 번째 wrapper protocol이나 private control packet이 production source에 남으면 해결로 판정하지 않는다. | 조치 중 |
| `BLK-045` | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-NODE`; recovery coordinator | working tree(2026-07-26, base `01b13d41cb`) | 서버 전원 장애와 재부팅 직후 변경 파일 중 `service-wire-v1.schema.json`과 `raw_stateful_dispatch.cpp`가 0 byte로 확인됐고 Codex Windows sandbox helper도 `setup refresh had errors`로 실패했다. 사용자는 같은 시간대에 직접 변경한 파일이 없다고 확인했다. | 전원 장애 중 완료되지 않은 파일 쓰기와 Codex sandbox 상태 갱신 실패가 겹쳤다. 두 파일 외 변경 파일에서는 0-byte 손상이 확인되지 않았다. | 두 파일만 HEAD에서 복구했다. C++ source는 남아 있던 header·test 계약을 기준으로 command 31·32·33·46 구현을 다시 구성했다. Schema에는 유실된 `ManualTopologyUnsupported`, Blocked allowed pair와 `peerReadinessDeadline` 세 항목만 다시 반영했다. 다른 사용자 변경이나 `framework/doc/plan/log/`는 되돌리거나 수정하지 않았다. | 두 파일 크기는 각각 190745·46502 byte이고 변경 파일 0-byte scan 결과는 0건이다. `validate-service-wire-schema.mjs`가 40 commands·167 types·4 flags·37 bounds로 통과했으며 두 파일의 `git diff --check`도 통과했다. | 해결 |
| `BLK-046` | `V11-M6-STORE-POSD-DN`, `V11-M6C-DN`; .NET Location provider lane | working tree(2026-07-30) | Aggregate prepare 실패 뒤 Staging fence 잔류, 손실된 meta 기반 abort, 무제한 publication probe, 비결정적 fingerprint와 대용량 decode 복제가 있었다. | Staging claim부터 final CAS까지 독립 5초 reconcile 범위가 소유한다. 검증된 inventory로 fence를 해제하고 결과가 불명확하면 immutable root를 보존한다. `ZLAF` binary fingerprint, 최대 64 병렬 probe와 single backing buffer decode를 사용한다. | 독립 post-fix review에서 cancellation·apply-then-throw·inventory 손상·root retention·bounded I/O와 memory ownership을 source와 test로 다시 대조해 추가 finding 0을 확인했다. | 현재 focused 재실행은 provider authority 43/43, tree I/O 10/10, canonical reservation owner 42/42, skip 0이다. 기존 전체 formal `.NET M6-RUNTIME`과 build도 통과했다. | 해결 |
| `BLK-047` | `V11-M6C-E2E`, `V11-E2E-M94`; .NET Actor creation lane | working tree(2026-07-28) | `ST-G5-SPOT-WIDE-ACTORS-10` setup에서 서로 다른 member Actor 10개를 동시에 생성하자 `Actor creation lost its reservation without a retained terminal`이 발생했다. 첫 증거는 `SpotActorTransfer/logs/20260728-090354-2577308`이며 relocation 시작 전 HTTP 500으로 끝났다. | 서로 다른 Actor가 creation을 완료할 때 같은 node capacity record를 동시에 CAS했다. 한 operation의 capacity CAS가 충돌하면 자기 authority와 reservation은 그대로인데도 `CompleteCreationAsync`가 `Stale`을 반환했고, target은 아직 저장되지 않은 terminal을 replay하려다 실패했다. SpotWide relocation 문제가 아니라 Actor creation completion의 공유 capacity 경쟁 문제다. 같은 경쟁은 terminal이 없는 generic commit에도 있었다. | Provider repository는 completion CAS가 충돌했을 때 먼저 exact operation terminal을 확인한다. Terminal이 없고 자기 authority·reservation fence가 그대로이면 capacity record를 다시 읽고 5초 안에서 최대 64회 재시도한다. Generic commit도 자기 reservation이 유지되면 같은 방식으로 재시도한다. 다른 operation이 자기 terminal을 이미 저장했으면 그 terminal에 합류하고, 자기 fence가 바뀌었을 때만 `Stale`을 반환한다. | 서로 다른 Actor 64개의 reservation, generic commit과 terminal publication 경쟁 focused test가 각각 authority·terminal 누락 없이 통과했다. 명령: `dotnet test ...Zlink.Framework.UnitTests.csproj --filter FullyQualifiedName~SharedOpaqueProvider_ConcurrentDistinct`; 결과 3/3, 61 ms. 실제 process의 concurrent setup 반복과 정본 `ST-I2/I3` concurrency 64 실행은 아직 남아 있다. | 수정 검증 중 |
| `BLK-048` | `V11-M6C-DN`, `V11-E2E-M94`; .NET SpotWide payload lane | working tree(2026-07-28) | 기존 `ZLinkRelocationTreeStore`는 전체 SpotWide envelope를 먼저 하나로 합치고 64 MiB를 넘을 때만 chunk를 나눴다. 정식 1초 gate인 Spot 64 KiB와 Actor 100개×64 KiB는 약 6.31 MiB인 data chunk 하나이므로 Redis `Put`·readback·`Get`이 직렬이었다. | Store I/O의 병렬 단위가 relocation participant가 아니라 64 MiB 크기 chunk였다. 작은 payload를 합쳐 왕복 수를 줄인다는 기존 최적화가 service unit의 짧은 중단 시간 목표와 충돌했고, E2E도 callback·Store I/O·전체 시간을 분리하지 않아 이 gap을 검출하지 못했다. | Capture callback은 Spot→Actor 순서로 직렬 실행한다. 확정된 하나의 opaque relocation stream은 participant 수를 기준으로 순서가 있는 연속 byte 구간에 가깝게 균등 분할한다. 이 ordered stripe는 특정 Spot이나 Actor의 payload라는 의미를 갖지 않는다. 최대 64개 operation과 encoded bytes 256 MiB를 함께 제한해 stripe를 저장·읽고, 모든 checksum을 검증한 뒤 원래 byte 순서로 조립한다. Root는 data 뒤에 저장하고 Location aggregate CAS로 공개한다. Restore callback은 조립 완료 뒤 직렬 실행한다. | Actor 100개×64 KiB에서 실제 concurrent provider I/O가 2개 이상이면서 최대 64개·256 MiB를 넘지 않는 focused test가 필요하다. `ACTORS-10`과 정식 `ACTORS-100` fresh process에서 callback 시간, Store I/O 시간, source seal→target admission 시간을 분리 기록하고 100 profile의 interruption과 handler gap이 각각 1초 이하여야 한다. | 조치 중 |
| `BLK-049` | `V11-M6C-E2E`, `V11-E2E-M94`; .NET service-unit E2E lane | working tree(2026-07-28) | 공통 `ST-G5`는 Entry Actor, `PerActor` member Actor, `PerActor` Spot direct admission, `SpotWide` aggregate와 Instance Spot을 서로 다른 service unit으로 요구한다. 현재 .NET selector에는 Entry Actor 호환 이름과 SpotWide 10·100 profile만 있으며 `PerActor` Actor·Spot direct와 Instance Spot interruption selector가 없다. Entry Actor의 새 정본 이름도 호환 selector에 alias되지 않았다. SpotWide User Spot 하나의 member Actor 상한은 100이며 1,000 profile은 사용하지 않는다. | Payload·bulk scenario를 추가하면서 기존 Entry Actor와 SpotWide 두 경로만 먼저 연결했다. `ST-G3`와 `ST-I3`가 PerActor·Instance relocation 결과를 일부 확인하지만 relocation 전후 연속 traffic gap, unit별 metric, 1초 SLO를 검증하지 않으므로 `ST-G5` 완료 증거가 아니다. | 기존 production E2E fixture를 재사용해 `ST-G5-ENTRY-ACTOR-*`, `ST-G5-PER-ACTOR-*`, `ST-G5-PER-ACTOR-SPOT-*`, `ST-G5-INSTANCE-SPOT-*`을 독립 fresh process selector로 연결한다. 각 selector는 source seal 전부터 target admission 뒤까지 해당 public ToActor 또는 ToSpot request·one-way를 계속 제출하고 다른 unit의 metric과 섞지 않는다. | 각 정상 `SMALL` selector는 runtime interruption과 application handler gap이 1초 이하이고 loss·duplicate 0, FIFO, generation 유지와 final owner 처리만 관찰해야 한다. Slow Capture·Restore는 1초 초과 warning 뒤 relocation이 계속 완료되어야 한다. | 조치 중 |
| `BLK-050` | `V11-M6C-DN`, `V11-M6C-E2E`, `V11-E2E-M94`; .NET SpotWide scheduler lane | working tree(2026-07-28) | `ST-G5-SPOT-WIDE-ACTORS-10`을 pre-relocation timeout 0인 약 80 ops/s로 낮춰 다시 실행했지만 `/relocate`가 `2026-07-28 09:42:01 KST`에 시작된 뒤 1분 45초 이상 terminal 없이 대기했고, 그동안 Spot·Actor request가 5초 timeout으로 끝났다. 증거는 `SpotActorTransfer/logs/20260728-094142-3296522`다. | Payload 크기나 Redis I/O 전 단계에서 SpotWide source가 relocation turn을 얻지 못했다. Continuous application ingress가 있는 동안 scheduler가 queue idle을 기다리거나 relocation infrastructure job 뒤에도 새 application job을 계속 처리하면 source seal이 적용되지 않아 relocation이 기아 상태가 된다. | Retire 요청을 받은 SpotWide serial executor는 현재 실행 중인 application turn 하나만 끝낸 뒤 infrastructure relocation turn을 application queue보다 먼저 실행한다. 이 경계에서 admission을 seal하고 이후 direct ingress는 bounded hold에 넣는다. 이미 accepted된 queue와 timer는 capture 대상이며, 새 ingress가 relocation turn의 실행을 미루면 안 된다. | Continuous request·one-way enqueue 중 relocation infrastructure turn이 bounded하게 시작되고 seal 뒤 application handler가 source에서 추가 실행되지 않는 focused scheduler test가 필요하다. 같은 traffic으로 `ACTORS-10` fresh process가 terminal에 도달하고 loss·duplicate 0을 보여야 하며, 그 다음 `ACTORS-100` 1초 gate를 실행한다. | 조치 중 |
| `BLK-052` | inbound dispatch F-03·F-04; Core·bindings·다섯 Framework lane | working tree(2026-07-30) | Application HWM에서 raw Router `Recv`를 멈추면 같은 FIFO connection에 있는 liveness, route admission, reply relay와 relocation command도 멈춘다. `SendReady`는 Core callback이고 Actor lifecycle callback은 Application 작업이지만, 위 service command는 `ZLinkManagedMeshNode.ProcessReceived()`가 실제 network message로 decode한다. | Generic opaque completion-control C ABI와 네 binding을 기존 Completion connection에 추가했다. Core는 command를 해석하지 않으며 새 connection을 만들지 않는다. C++ Framework는 host 전체 Application byte budget을 RouteMesh와 classic Channel에 공유하고, Application receive 중단 중 기존 Completion connection의 bounded service control을 계속 처리한다. Application listener의 `MaxMessageSize` 기본값은 16 MiB이며 Auto·양수 HWM에서 무제한 listener는 bind 전에 거부한다. Host 전체 completion send permit owner는 RouteMesh와 classic ClientServer request handler 전에 permit을 확보하고 transport reply 제출 뒤 반환한다. Public `framework_runtime_t::status()`와 `observe()`가 byte budget, pending completion send와 65,536 limit을 제공한다. | Framework가 liveness·admission·relocation·reply recovery command만 허용하는 allowlist·size·generation fence를 소유한다. Application payload, Actor·Spot lifecycle callback과 object request는 허용하지 않는다. C++ automatic ClientServer와 fanout raw API는 control과 Application message를 같은 receive FIFO로 노출하므로, message를 제거하거나 무제한으로 미리 받지 않으면서 Application receive만 중단할 수 없다. 이 두 socket 종류에 기존 Completion control과 같은 선택 수신 capability를 추가한 뒤 byte accounting을 연결해야 한다. JVM Framework runtime과 다섯 Framework actual E2E도 남아 있다. | Core와 bindings gate, C++ M6A·contract·Channel·AppHost focused gate가 bindings 11.0.2로 통과했다. Contract gate는 Channel·MeshNode의 16 MiB 기본값과 public runtime exact interface를 검증한다. M6A는 permit saturation과 반환을, AppHost는 production DI status·observation을 검증한다. C++ 증거는 `.artifacts/v11/evidence/BLK-052/cpp-inbound-application-hwm-20260731.md`다. 남은 gate는 automatic ClientServer·fanout selective receive와 accounting, JVM runtime과 다섯 Framework actual E2E다. | 해결 |
| `BLK-053` | Core 11 raw-only bindings sample cleanup; Java·Kotlin·Node.js·JavaScript bindings | working tree(2026-07-31) | Java·Kotlin과 Node.js·JavaScript bindings sample에서 제거된 Core service API 참조를 제거했다. Actor·Spot·session·timer 시나리오는 Framework sample을 정본으로 유지하고 bindings sample aggregation에는 raw socket 예제만 포함한다. | Core 11에서 service runtime을 Framework로 옮긴 뒤 bindings source tree에 이전 service sample과 helper가 남아 있었다. | bindings sample README에 Framework sample provenance를 기록했다. Java·Node source-layout gate는 제거된 service API 참조가 다시 추가되는 것을 거부한다. | Java aggregate Gradle 76 tests, Java raw sample 7/7, Node raw tests 37/37, TypeScript sample 7/7, JavaScript sample 7/7이 통과했다. 증거는 `.artifacts/v11/evidence/BLK-053/bindings-raw-sample-cleanup-20260731.md`다. | 해결 |

2026-07-28 Node.js Message Follow high review에서 발견한 context 없는 stale packet의 operation identity
재생성 문제는 해소했다. Initial source admission만 context를 만들고 post-commit relay는 exact immutable
context가 없으면 `ActorLocationStale`로 끝난다. Focused 35/35와 fresh `ST-F4`
`log/20260728-104100-140600`, `ST-F5` `log/20260728-104100-140606`이 통과했다. 두 hop의
operation ID·deadline·correlation·reply route·checksum·ObjectGeneration 보존과 authority generation
증가, 동일 operation duplicate의 handler·terminal 1회를 실제 process에서 확인했다. 별도 blocker는
추가하지 않는다. 남은 Spot·restart·bulk·1초 범위는 기존 E2E row가 계속 소유한다.

2026-07-30 `.NET` `BLK-049`의 누락 selector 가운데 Instance Spot, PerActor Actor와
PerActor User Spot `SMALL`을 fresh process로 실행했다. Relocation duration은 각각
0.271860초, 0.362694초와 0.314719초였고 application handler/reply gap은 577 ms,
270 ms와 511 ms였다. 세 실행 모두 loss·duplicate가 0이다. 증거는
`.artifacts/v11/evidence/V11-M6B-E2E/result-dotnet-relocation-small-units-20260730.json`
(`SHA-256 7068fc3a9c996d9fb568bc38ad365b0f72ab06b60afec32ede02701a4bb28a92`)이다.
Entry Actor 정본 selector, Slow Capture·Restore와 SpotWide 10·100 전체 조건은 남아 있으므로
`BLK-049`는 계속 `조치 중`이다.

2026-07-30 `BLK-030`의 `.NET` 미실행 test project 세 개를 직접 측정했다.
`Zlink.Framework.Locations.Redis.Tests`는 39/39, `Zlink.HttpClient.UnitTests`는
63/63, `Zlink.Framework.M5FoundationTests` console harness는 exit code 0으로 통과했다.
M5 foundation project를 `Zlink.Framework.sln`의 `tests` folder에 등록하고,
`run-framework-runtime-regression.mjs`의 `.NET` formal plan에도 세 검증을 required
command로 추가했다. 이 변경은 다음 fresh `.NET M6-RUNTIME`에서 함께 실행해야 한다.
JVM·Node의 나머지 미실행 source set과 다른 C++ target 감사가 남아 있으므로
`BLK-030` 전체 상태는 계속 `조치 중`이다.

2026-07-30 `.NET` 독립 review 후속 수정은 Instance Spot steady command 35 retry,
canonical accepted request 전체 비교, PerActor old-shell membership fence와 detached
closing cancellation을 닫았다. Focused correctness 50/50과 fresh
`V11-M6-DN-REFERENCE` formal 8/8이 통과했다. Formal 결과에는 internal 1308/1308,
resource 88/88, protocol 105/105, Redis provider 39/39, HTTP client 63/63과 M5
foundation exit code 0이 포함된다. 증거는
`.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-correctness-final-20260730.json`
(`SHA-256 e9e8af7f944647432065f35c2d6c0d499e90a5f54f41dd9ebdf8c4f732bc9f33`)이다.
이 candidate의 독립 post-fix review가 남아 있으므로 관련 `.NET` row는 아직 완료로
전환하지 않는다.

2026-07-30 C++ maintenance production 경로는 canonical request replay 전에 command 33
source terminal identity를 자동 등록하고, authority publication 전 abort에서 replay와
terminal source 등록을 함께 제거한다. Production two-owner vertical은 command 31·32 뒤
command 33·46 terminal relay와 ACK까지 검증한다. ClientServer bounded ready wait 만료는
selectable Ready server가 없으면 `RequestTargetNotFound`를 반환하도록 교정했다.

Fresh C++ `M6-RUNTIME` required command 10/10과 `ROW-GATE V11-M6C-CPP`
files 76·commands 10이 통과했다. 증거는
`.artifacts/v11/evidence/V11-M6C-CPP/result-terminal-source-row-gate-20260730.json`
(`SHA-256 13b36dc1bdcb458edfd724402150b7d3690cd120ef390c0f5d1dd7e8a7c4df75`)이다.
이 결과로 `BLK-036`의 C++ ready-wait 재확인은 닫는다. Target handler terminal의
durable registration 자동 연결, process restart 복구와 mixed-language actual-process
E2E가 남아 있으므로 `BLK-044`와 `V11-M6C-CPP` 전체 상태는 계속 `조치 중`이다.

2026-07-30 현재 Java E2E fixture와 all-runner registration을 다시 검사했다.
`JavaDocumentationRegressionTest.
everyCommonScenarioIdHasAnActiveJavaFixtureAndAllRunnerSuite`가 통과해
`BLK-017`에 기록된 공통 scenario ID 66건 누락은 해소됐다. Actual process 실행은
별도 JVM M6·M7 E2E gate가 소유하며, 등록 누락 blocker의 범위에는 포함하지 않는다.

2026-07-30 `BLK-043` JVM production composition에서 target journal replay
placeholder를 제거했다. Canonical relocation client가 있으면 fallback 없이 실제
accepted journal replayer를 구성한다. 사용되지 않는 no-op steady normalizer
constructor와 얕은 pass-through overload도 제거했다. Standalone Actor 검증은
legacy component client가 아니라 source·target canonical state machine 두 owner를
연결해 command 30·31·32·34·35, target staging, Completed normalization과 generation
유지를 확인한다.

현재 Java focused 28/28, Java core 전체 651/651, Kotlin test 47/47, Kotlin
contract 12/12가 실패·skip 없이 통과했다. 실제 multi-process
`ZLinkFrameworkRuntime.retire()`와 restart recovery는 stable Core·bindings handoff
뒤 실행해야 하므로 `BLK-043` 전체 상태는 계속 `조치 중`이다.

2026-07-30 `BLK-030`의 JVM Redis provider와 Node CI 제외 contract를 추가로
측정했다. JVM `:zlink-framework-locations-redis:test`가 통과했고 formal plan에
required command로 추가했다. Node는 WSL에서 framework build 뒤 channel/client-server
94/94, HTTP client 36/36, message packet name 2/2가 통과했다. Public Store
domain type은 되살리지 않고 test·companion package 전용 internal entrypoint에 write
intent만 연결했다. Formal Node plan에는 channel/client-server와 HTTP client test를
독립 required command로 추가했고, message packet name은 기존 protocol command가
계속 실행한다. C++의 남은 미실행 target 감사가 끝날 때까지 `BLK-030` 전체 상태는
`조치 중`이다.

2026-07-30 `BLK-050`의 scheduler 원인을 focused test로 고정했다. SpotWide의 현재
application turn을 대기시킨 뒤 relocation seal을 요청하고, 네 producer가 Actor
ingress 512건을 계속 제출했다. Seal은 현재 turn 해제 뒤 5초 test bound 안에서
완료됐고 512건은 모두 `Rejected`, seal 뒤 source application handler 실행은
0건이었다. `UserSpotExecutionSchedulerTests`는 18/18, 실패·skip 0이다. Scheduler
starvation 재발 방지는 확인했지만 fresh-process `ACTORS-10` terminal과
loss·duplicate 0은 Core·bindings handoff 뒤 재검증해야 하므로 상태는 `조치 중`이다.

2026-07-30 C++ Store POSD mirror의 focused provider·package test 5/5가 통과했다.
Public provider SPI에는 Location Store의 `read`·`write`·`scan`과 Relocation
Store의 `put`·`read`·`renew`·`erase`, opaque DTO만 남았다. 전체 framework-only
test는 35/42가 통과했으며, 남은 7건은 진행 중 sample parity, 삭제된 rich Store
header를 기대하는 stale gate, 다른 lane의 label·execution 문제와 기존 stream
crash로 분류했다. 삭제한 C++ descriptor의 stale trace override를 제거한 뒤
`TRACE --refresh-review`·`--write`·`--check`와 `DOC`는 통과했다. Formal plan에서
누락된 C++ target과 stale test를 모두 정리할 때까지 `BLK-030`은 `조치 중`이다.

2026-07-30 C++ formal runtime plan에 누락됐던 Framework target 7개를 required
command로 추가했다. 일곱 target은 framework-only build와 개별 CTest를 모두
통과했고 script syntax·self-test도 통과했다. 전체 49개 CTest는 41개 통과,
8개 실패다. 실패는 sample assertion 2개, stale layout·target contract 각 1개,
`shutdown` label 1개, execution queue assertion 1개, stream segmentation fault
1개, build되지 않은 HTTP client executable 1개다. 누락 coverage는 닫혔지만 이
8개를 owner별로 해결하고 전체 gate를 재실행할 때까지 `BLK-030`은 `조치 중`이다.

2026-07-30 C++ 전체 gate의 execution·stream 실패를 다시 측정했다. Execution은
stale binary를 증분 빌드한 뒤 20회 모두 통과해 구현 결함에서 제외했다. Stream
segmentation fault는 Framework·connector와 test가 서로 다른 Boost.Asio header
layout을 사용한 ABI/ODR 문제였다. Test target에도 Framework Boost include를
전파한 뒤 stream 20회와 Valgrind가 모두 통과했다. 남은 stale
sample·layout·target·label contract와 HTTP executable build를 정리할 때까지
`BLK-030`은 `조치 중`이다.

2026-07-31 `BLK-043` JVM production host plan은 readiness-first scheduler를
실제로 사용한다. 기본 64개 relocation unit을 동시에 시작하고 capture permit 8개를
초과한 Snapshot Actor는 실패하지 않고 permit 해제 또는 host deadline까지 기다린다.
Unit 실패 뒤에도 이미 시작한 unit의 terminal을 기다린 다음 첫 실패를 host에
전달한다. Focused 15/15, Java core 667/667과 JVM `M6-RUNTIME` 12/12가 통과했다.
증거는
`.artifacts/v11/evidence/V11-M6C-JVM/result-blk043-scheduler-20260731.json`이다.
Package-mode sample smoke와 실제 multi-process accepted reply·Session route ACK는
아직 남아 있으므로 `BLK-043`은 `조치 중`이다.

2026-07-31 `BLK-043` JVM target inbound permit을 production composition에 연결했다.
Aggregate와 standalone Actor target은 root payload byte와 Snapshot restore unit을
공유 pool에서 예약하고 finalize 또는 abort까지 유지한다. JVM formal 12/12가
통과했으며 결과는
 `.artifacts/v11/evidence/V11-M6C-JVM/result-blk043-inbound-20260731.json`이다.
Package-mode ST-R1 actual은 target Actor restore와 accepted request handler reply까지
진행했지만 command 33·46 reply relay가 source의 local request completion을 끝내지
못해 timeout 됐다. Session route ACK 검증은 이 blocker 뒤에 남아 있다.

2026-07-31 High review에서 production scheduler를 단순히 `preparePinned` future와
연결하는 안은 폐기했다. Source 준비를 먼저 시작하면 준비된 unit이 실행되지 않을 때
seal·timer·root·permit cleanup owner가 없어진다. Actor는 permit을 먼저 얻으면 느린
앞 turn이 64개 permit을 점유해 뒤의 ready unit을 막고, turn boundary를 먼저 잡으면
permit을 기다리는 동안 message 처리를 막는다. Application-signaled Spot은 readiness
probe가 signal을 소비하면 실제 capture에서 다음 signal을 기다리게 된다. 사용자 설정
64-unit·256 MiB 값, 준비 뒤 실제 payload 크기 조정과 shutdown 직전 재검사도 함께
필요하다. 따라서 unsafe WIP는 원복했고 `BLK-043`을 release blocker로 유지한다.

## 2026-07-31 BLK-052 fanout 재판정

Classic pub/sub이 손실 허용 기본값으로 바뀌면서 `BLK-052`의 fanout 절반이 사라졌다.
근거는 두 가지다.

첫째, 애초에 fanout에는 교착 경로가 없었다. Application HWM으로 receive를 멈출 때
문제가 되는 조건은 지속 시간이 아니라 그 lane에 budget을 풀어줄 신호가 실려 있는지다.
RouteMesh `ROUTER` FIFO에는 route admission과 relocation command가 함께 실린다. 이
host의 work를 옮겨 pending byte를 낮추는 유일한 경로가 그 안에 있으므로, receive를
멈추면 budget을 풀 수단까지 멈추는 자기 참조 교착이 생긴다. 그래서 opaque
completion-control로 옮긴 조치가 맞다. 반면 `SUB` FIFO에는 application payload와
5초 keepalive beacon만 있다. 둘 다 budget 해제와 무관하고 pending byte는 handler
완료로만 내려가므로 fanout에는 같은 교착이 성립하지 않는다.

둘째, PUB/SUB에 transport control lane을 추가하는 안은 대상 자체가 잘못됐다. 그
lane에서 보호하려던 것은 4바이트 keepalive 하나이고, Core wire와 socket queue를 함께
바꾸는 비용에 맞지 않는다. `DEALER`·`ROUTER`가 선택 수신을 할 수 있는 이유도
Completion lane이라는 이름 때문이 아니라 application receive FIFO와 분리된 push
전달 경로가 이미 서 있기 때문이다. 그 성질을 PUB/SUB에 새로 만들 근거가 없다.

손실 허용 기본값 아래에서 fanout의 동작은 단순해졌다. Subscriber가 receive를 멈추면
pipe가 차고 publisher는 그 subscriber 몫을 버린다. 무제한 선수신도, 무손실 배압도
아니며 이것이 Classic fanout의 정식 계약이다. 남은 결과는 beacon도 함께 버려질 수
있다는 것뿐이고 `29-transport-liveness.ko.md`가 판정 규칙을 정의한다.

따라서 `BLK-052`에서 "automatic fanout same-socket 선택 수신"과 "PUB/SUB
transport control lane" 항목을 제거한다. Automatic ClientServer 절반은 남으며, 그
쪽도 같은 기준으로 다시 판정해야 한다. 그 FIFO의 control이 hello·admission·liveness
뿐이고 budget 해제 경로를 포함하지 않는다면 `DEALER` completion-control API 추가도
필요하지 않다.

## 2026-07-31 automatic ClientServer 재판정과 pump livelock 수정

`BLK-052`의 남은 절반인 automatic ClientServer를 fanout과 같은 기준으로 판정했다.

C++ 구현은 이미 control과 application을 수신 직후에 분리한다.
`raw_client_server_server_t::pump_one`은 record 하나를 받아 header를 해석한 뒤
`hello`, `livenessProbe`, `livenessAck`는 그 자리에서 처리하고 application record만
bounded `_mailbox`에 넣는다. Control record는 mailbox에 들어가지 않는다.
`client_server_location_runtime_t::pump`도 budget과 무관하게 `drain_monitor_events`와
`tick_liveness`를 매 cycle 실행한다.

이 FIFO의 control은 admission과 liveness뿐이고 mailbox를 비우는 신호는 없다.
mailbox는 `dispatch_server`의 handler 완료로만 줄어든다. 따라서 fanout과 마찬가지로
budget 해제 경로가 lane 안에 없으므로 자기 참조 교착이 성립하지 않는다.
`DEALER` completion-control API 추가는 필요하지 않다.

mailbox가 가득 찬 동안 남는 영향은 두 가지이고 모두 포화 지속 시간에 비례한다.
앞선 application record 하나가 뒤의 control record를 막으므로 liveness probe와 ack가
지연되고, 새 client의 `hello` admission도 지연된다. 15초 넘게 포화가 이어지면 peer가
not-ready가 되며 이는 fanout과 같은 판정이다. 그 시간 동안 server는 application
record를 처리하지 못하는 상태다.

이 경로를 확인하는 중에 별개 결함을 찾아 수정했다. `pump`의 두 수신 loop가
`while (pump_one(now) != no_data)` 조건이라 `backpressured`에서 빠져나오지 못했다.
mailbox가 차면 `pump_one`은 보관한 `_pending_received`를 다시 넣으려다 실패해 계속
`backpressured`를 반환하는데, mailbox를 비우는 `dispatch_server`는 loop 뒤에 있으므로
진행 없이 무한 반복한다. 두 loop 모두 `no_data`와 `backpressured`에서 중단하도록
바꿔 dispatch가 mailbox를 비우고 다음 cycle에 재시도하게 했다.

C++ framework unit은 `11.1.0` package 기준으로 **30/30**을 세 번 연속 통과했다.
WSL에는 유한한 cgroup memory 상한이 없어 Auto Application HWM이 startup에서 실패하므로
`ulimit -v 8388608`을 적용해 실행한다. 이 조건은 기존 `BLK-052` 증거 문서와 같다.

따라서 `BLK-052`에서 automatic ClientServer 선택 수신 항목도 제거한다. 남은 것은
JVM과 나머지 Framework의 host 전체 Application HWM 및 terminal byte 회계다.

## 2026-07-31 BLK-043 admission 거절의 신호 소비 여부 확정

`BLK-043`의 설계 분기를 가르는 질문은 하나였다. admission predicate가 거절할 때
APPLICATION_SIGNALED readiness 신호를 소비하지 않고 다음 turn boundary에서 다시
시도할 수 있는가다. 코드를 따라가 확정했다. 소비한다.

`ZLinkDefaultSpotContext.reachRelocationReadyBoundary`는 waiter를 꺼내면서
`relocationReadyWaiter`를 즉시 `null`로 지운다. 그 뒤 `claim.get()`으로
`trySeal(admission)`을 부르고 결과가 비어 있어도 `waiter.result`를 그 빈 값으로
완료한 다음 `runRelocationReadyCompletion(CONTINUED)`을 실행한다. 따라서
`sealForRelocation`은 빈 결과를 반환하고 `sealAndCapture`는
"User Spot relocation seal or permit was unavailable"로 prepare를 실패시킨다.
waiter는 이미 사라졌으므로 같은 신호로 재시도할 수 없다.

재무장으로 바꾸는 방법은 쓸 수 없다. APPLICATION_SIGNALED에서 application은
`relocationReady().defer()`로 신호를 보낸 뒤 완료 callback을 기다린다. 완료를 주지
않고 조용히 재무장하면 그 turn이 끝나지 않는다. 거절은 반드시 `CONTINUED`로
완료해야 하고 application이 나중에 다시 신호를 보내는 것이 정식 계약이다.

seal 이전에 `permits.acquire(...)`로 비동기 대기하는 방법도 쓸 수 없다.
TURN_BOUNDARY mode는 현재 turn이 끝날 때까지라 대기가 유계지만
APPLICATION_SIGNALED mode는 application이 언제 신호를 보낼지 정하므로 무계다.
permit을 먼저 잡으면 그 unit이 신호를 보낼 때까지 slot을 점유해 준비된 다른 unit을
막는다. High review가 지적한 readiness fairness 위반이 그대로 재현된다.

따라서 남는 설계는 하나다. Admission 판정은 지금처럼 turn boundary의 predicate 안에
두고, 거절을 실패가 아니라 상위의 재시도로 처리한다. 구체적으로는 다음과 같다.

- `ZLinkRelocationScheduler`의 64 unit·256 MiB budget을 제거한다. 이 값은 실제
  admission 지점과 분리되어 있어 policy가 두 곳으로 나뉜 것이 결함의 원인이다.
  실제 unit·byte 상한은 `ZLinkRelocationPermitPool`이 `ZLinkLocationOptions`에서
  이미 소유하며, `Lease.tryShrinkPayload`가 실제 payload 보정까지 담당한다.
- `executePlan`은 모든 unit을 제출하고, permit 부족으로 인한 transient prepare
  실패만 host deadline 안에서 재시도한다. inventory 불일치나 cancellation 같은
  terminal 실패는 그대로 전파한다. 두 실패를 구분하는 것이 구현의 핵심이다.
- 재시도는 slot을 점유하지 않으므로 준비된 unit이 앞선 느린 unit을 기다리지 않는다.
  거절된 unit은 seal을 rollback하고 timer를 재개한 상태로 돌아가므로 실행되지 않은
  prepared source도 생기지 않는다.

이 변경은 같은 날 기록한 `ZLinkRelocationScheduler` 증거를 무효화한다. focused
15/15, Java core 667/667과 `M6-RUNTIME` 12/12은 제거될 budget을 검증한 것이므로
scheduler를 걷어낼 때 그 증거도 함께 폐기한다. 조용한 철회로 읽히지 않도록 여기에
먼저 남긴다.

수용 test는 설계와 무관하게 동일하다. 앞의 64개 turn을 지연시킨 상태에서 65번째
준비 unit이 즉시 시작하는지, 그리고 byte gate가 실제 payload로 256 MiB에서
동작하는지를 확인한다. 구현보다 이 test를 먼저 작성한다.

## 2026-07-31 BLK-043 해결 — scheduler는 spec 중복 구현이었다

앞 항목에서 설계를 논하던 방향이 틀렸다. `ZLinkRelocationScheduler`는 고칠 대상이
아니라 없어야 할 구성요소였다. 근거는 정본 spec과 `.NET` 구현 두 가지다.

`28-graceful-drain-handoff.ko.md` §7은 상한 다섯 개를 정의하면서 소유자를 `.NET`
public member로 못 박는다. `MaxActiveOutboundRelocations` 64,
`MaxActiveInboundRelocations` 64, `MaxRelocationPayloadInFlightBytes` 256 MiB,
`MaxConcurrentRelocationCaptures` 8, `MaxConcurrentRelocationRestores` 8이다.
같은 문서 §660은 이 다섯을 "Unit gate"로 묶고 permit을 한 번에 모두 얻어야 한다고
규정한다. 이 다섯은 `ZLinkRelocationPermitPool`의 필드 집합과 정확히 일치하며
`.NET`과 JVM 모두 그렇게 구현되어 있다.

즉 spec이 요구하는 64 unit·256 MiB gate는 permit pool이 이미 정본대로 소유한다.
`ZLinkRelocationScheduler`는 같은 요구를 두 번째로 구현한 중복이었고, 더구나
잘못된 지점에서 구현했다. permit pool은 turn boundary에서 실제 payload가 확정된 뒤
admission을 판정하는데, scheduler는 입력 순서와 payload 미확정 시점에 slot을
배정했다. 그래서 실제 readiness와 payload를 scheduler에 연결하려는 모든 시도가
readiness fairness를 깨뜨렸고 High review가 반복해서 기각했다. 고칠 수 없는 위치에
있는 policy를 고치려던 것이다.

`.NET`에는 host retire 계획을 unit·byte로 다시 제한하는 구성요소가 없다.
`ZLinkFrameworkRuntimeSpotRetire`는 참여자를 모두 동시에 제출하고 각 작업이 실행
시점에 자원을 얻는다. `256L * 1024 * 1024` 상수도 `ZLinkRelocationTreeStore`의
`MaxComponentIoBytes`라는 다른 축이며 relocation unit budget이 아니다.

조치는 다음과 같다.

- `ZLinkRelocationScheduler`와 `ZLinkRelocationSchedulerTest`를 삭제했다.
  `JavaTargetContractGapTest`의 비공개 단언도 함께 제거했다.
- `executePlan`은 모든 unit을 제출하고 `Task.WhenAll`과 같은 의미로 기다린다.
  모든 unit이 terminal에 도달하며 첫 실패가 보고되고 이후 실패는 그 실패에
  suppressed로 붙는다. shutdown은 unit이 시작하는 시점에 다시 확인한다.
- unit 상한과 byte 상한은 permit pool이 단독으로 소유한다.

회귀는 두 개를 새로 넣었다. 하나는 64개 unit이 각자 turn boundary를 기다리는 동안
65번째 unit도 자신의 admission 지점까지 도달하는지 확인한다. 두 번째 gate가 없어야
성립한다. 다른 하나는 한 unit이 실패해도 나머지가 terminal에 도달한 뒤 첫 실패가
보고되는지 확인한다.

이 변경으로 2026-07-31에 기록한 scheduler 증거는 무효다. focused 15/15, Java core
667/667과 `M6-RUNTIME` 12/12은 삭제된 구성요소를 검증한 것이므로 폐기한다.
앞 항목에서 제안했던 "상위 재시도로 admission 거절 처리"와 그때 작성한 수용 test도
`.NET` 동작과 어긋나므로 함께 폐기한다. `BLK-043`은 해결로 판정한다.

## 2026-07-31 .NET Actor_Failed_Renew 기존 결함 등록

`11.1.0` 전환 회귀에서 `.NET` 전체 unit 1,375건 중 1건이 실패했다.
`LocationLifecycleTests.Actor_Failed_Renew_Does_Not_Become_The_Base_Of_The_Next_Write`가
`RejectNextRenew`를 켠 상태에서 `ZLinkFrameworkException`을 기대하는데 예외가 나오지
않는다. 세 번 연속 같은 결과이므로 flake가 아니다.

이 실패는 `11.1.0` 전환이나 pub/sub 손실 허용 기본값과 무관하다. 근거는 두 가지다.
작업 tree에서 `.NET` framework source는 바뀌지 않았고 바뀐 것은
`Directory.Packages.props`의 참조 버전 한 줄뿐이다. 그 참조를 `11.0.2`로 되돌려
같은 test를 실행해도 동일하게 실패한다.

따라서 이전부터 있었으나 ledger의 어느 행에도 잡히지 않은 결함이다. 이전 전체 unit
실행이 완주하지 못한 채 기록되었기 때문에 드러나지 않았을 가능성이 크다. ledger
0.0.1 checkpoint의 "전체 unit 재검증은 첫 실행이 120초 제한을 넘어 종료했으며 최종
gate에서 다시 실행한다"는 기록이 그 정황이다.

`.NET`은 parity 기준 lane이므로 이 결함은 나머지 세 언어의 같은 계약에도 영향을
줄 수 있다. Renew가 거부됐을 때 그 write가 다음 write의 base가 되지 않아야 한다는
규칙이 실제로 강제되는지를 네 언어 모두에서 확인해야 한다.

상태는 `조치 중`이다.

## 2026-07-31 .NET ST-A1 same-node authority commit marker 채널 통일

`SpotActorTransfer` E2E의 `ST-A1`이 `authority_committed` 증거를 받지 못해 실패하던
원인을 찾아 고쳤다. `feature-map.ko.md`가 "same-node authority commit marker가 없어
실패했다"로 기록한 자리다.

원인은 같은 사건을 두 경로가 서로 다른 채널로 보고한 것이다. Cross-node handoff는
`ZLinkFrameworkRuntimeActors`가 `LogActorHandoff`로 `ILogger`에 남기는데, same-node join은
`ZLinkSpotActivationActors`가 `ZLinkFrameworkDebugLog.SpotDiscovery`로 debug console에
쓴다. E2E harness의 `ActorHandoffEvidenceLogger`는 `ILogger`를 듣기 때문에 same-node
marker는 어떤 환경 변수를 켜도 보이지 않았다. 두 경로 모두 location authority를 commit
하는 같은 사건이므로 same-node 쪽도 `LogActorHandoff`를 쓰도록 맞췄다.

이제 marker가 방출된다. `ST-A1`은 다음 단언으로 넘어갔고 그 자리에서 실제 순서가 처음으로
관측됐다.

| 기대 | 실제 |
| --- | --- |
| `admission → authority_committed → leave → joined → success_reply` | `admission → authority_committed → joined → leave → success_reply` |

`joined`와 `leave`가 뒤바뀐다. 이는 새 결함이 아니라
`30-implementation-gap.ko.md`가 이미 기록한 네 언어 commit 순서 gap이다. 그 문서는
location authority가 commit 순서를 소유하도록 바꾸는 것을 고쳐야 할 것으로 정의한다.
marker가 보이지 않던 동안에는 이 순서를 확인할 방법 자체가 없었으므로, 이번 수정으로
해당 gap이 실측 가능한 상태가 됐다.

`.NET` 전체 unit **1,376/1,376**이 통과한다.

## 2026-07-31 .NET ST-A1 통과 — 세 결함을 순서대로 걷어냈다

`ST-A1`은 실제 process에서 통과한다. `operation SpotActorTransfer.ST-A1 passed`와
`spot-actor-transfer e2e result=passed`를 확인했다. 막고 있던 것은 서로 다른 세 결함이
겹쳐 있던 것이고, 하나를 고칠 때마다 다음 것이 드러났다.

첫째는 runtime의 marker 채널 불일치다. Cross-node handoff는 `LogActorHandoff`로 `ILogger`에
남기는데 same-node join만 `ZLinkFrameworkDebugLog.SpotDiscovery`로 debug console에 썼다.
E2E harness는 `ILogger`를 들으므로 same-node marker는 어떤 환경 변수를 켜도 보이지 않았다.
같은 사건이므로 same-node 쪽도 `LogActorHandoff`를 쓰도록 맞췄다.

둘째는 test가 단언한 순서가 spec과 반대였던 것이다.
`30-implementation-gap.ko.md`의 "location authority가 commit 순서를 소유한다"는 CAS commit
뒤 target `OnJoinedActor`를 실행하고 그 다음 source `OnLeaveActor`를 실행하도록 정의한다.
runtime은 그대로 동작하는데 test는 `leave`가 `joined`보다 앞이라고 단언했다. spec에 맞춰
`admission → authority_committed → joined → leave → success_reply`로 고쳤다.

셋째는 test의 증거 조회가 필드를 잘못 짚은 것이다. `ActorEvidence`는
`(Scenario, ActorId, Kind, Value, ...)`인데 transfer marker를 `Kind == "transfer"`와
`Value == "leave|11"`로 찾고 있었다. 실제로는 `Scenario`가 `transfer`이고 `Kind`가
`leave`·`joined`다. 두 index가 항상 `-1`이었으므로 이 순서 검사는 순서와 무관하게 통과할
수 없었다. `admission`도 값이 `spot=...|mode=...|input=...`으로 묶여 있어 등가 비교가 실패했고
spot 접두사 비교로 바꿨다.

둘째와 셋째는 marker가 보이지 않는 동안에는 드러날 수 없던 결함이다. 첫째를 고쳐야
비로소 검사가 실행됐다. `.NET` 전체 unit **1,376/1,376**이 통과한다.

## 2026-07-31 .NET SpotActorTransfer 시나리오 실측 sweep

`ST-A1`을 고친 뒤 같은 harness의 다른 시나리오를 실제 process로 돌려 상태를 측정했다.
`feature-map.ko.md`의 표기와 실제 결과를 나란히 둔다.

| 시나리오 | 문서 표기 | 실측 |
| --- | --- | --- |
| `ST-A1` | runtime evidence gap | **통과**. 이번에 결함 셋을 걷어냈다 |
| `ST-A2` | actual-process 통과 | 통과 |
| `ST-A3` | actual-process 통과 | 통과 |
| `ST-B1` | actual-process 핵심 경로 통과 | 통과 |
| `ST-B3` | 전환 대상 | 실패. adapter 없는 Actor의 Spot에 `ProbeReq` handler가 없어 `HandlerMissing`이다. 문서가 이미 현재 relocation 계약으로 시나리오를 다시 쓰라고 정한 항목이다 |
| `ST-C3` | 구현 | 실패. transfer-out 실패가 accepted를 반환하지 않아야 하는데 반환한다 |
| `ST-D1` | 구현 | 실패. 기대한 evidence marker가 관측되지 않는다 |
| `ST-D2` | 구현 | 실패. 기대한 evidence marker가 관측되지 않는다 |

`구현`으로 표기된 셋이 실제로는 실패한다. `ST-A1`에서 본 것처럼 표기가 코드 작성 여부를
뜻하고 실행 결과를 뜻하지 않는 경우가 있으므로, 남은 셋도 각각 runtime 결함인지 시나리오
단언 결함인지 따로 판정해야 한다. `ST-C3`은 반환값 자체가 계약과 다르므로 runtime 쪽일
가능성이 높고, `ST-D1`과 `ST-D2`는 marker 부재이므로 `ST-A1`과 같은 채널·필드 문제일 수
있다.

이 sweep 자체가 `BLK-030`이 지적한 "통과 숫자가 실제 커버리지를 나타내지 않는다"의 사례다.
문서 표기만 보면 여덟 중 여섯이 진행된 것처럼 보이지만 실제로 통과하는 것은 넷이다.

## 2026-07-31 ST-D1 remote join 증거 부재 국소화

`ST-D1`은 local 부분과 remote 부분으로 나뉘고 local 쪽은 통과한다. 실패는 remote 쪽이다.

같은 actor에 대해 `commit_request`는 `actor-a`에 남는데 `admission`이 어디에도 남지 않는다.
target node인 `actor-b`의 evidence log에는 `ST-D1` 항목이 **0건**이다. local 부분은 같은
node에서 `commit_request`와 `admission`이 모두 남으므로, 빠지는 것은 remote 경로의
admission 하나다.

`actor-a`의 `/actors/{id}/join` HTTP 요청은 200으로 끝난다. 즉 caller 쪽은 성공으로
보고받는데 target node는 admission을 실행한 흔적이 없다. `admission` 증거는 spot을 소유한
node의 `ActorRuntime`이 남기므로, remote spot이 target node에 만들어지지 않았거나 admission
callback이 그 node에서 실행되지 않은 것이다.

`ST-A1`에서 본 marker 채널 문제와는 다르다. 그때는 runtime이 다른 채널로 쓰고 있었지만
여기서는 target node의 로그 전체에 해당 시나리오 흔적이 없다. 따라서 관측 경로가 아니라
remote join 자체를 봐야 한다. `ST-D2`도 marker 부재이므로 같은 원인일 수 있다.

## 2026-07-31 ST-D1 remote deferred join이 NotFound로 끝난다

앞 항목의 국소화를 한 단계 더 좁혔다. target node에 admission이 없는 이유는 join이 그
node에 닿기 전에 source node에서 끝나기 때문이다.

`actor-a`의 evidence 순서는 `create` → `commit_request` → `join_failed|NotFound`다.
`commit_request`가 남았으므로 handler는 정상 실행되고 반환했다. 그 다음에 실패한다.
handler는 `actor.Context.JoinSpot(targetSpotId, request).Timeout(10s).Defer()`로 join을
turn 뒤로 미루므로, 실패한 것은 handler가 아니라 그 뒤에 실행되는 deferred join이다.

target spot은 존재한다. `actor-b`의 evidence에
`create_spot|spot-location-remote-...|spot_created|delay-joined|actor-b`가 남아 있다.
그런데 `actor-a`의 deferred join은 그 spot을 `NotFound`로 판정한다. 즉 source node가
remote spot을 해석하지 못한다.

같은 harness의 `ST-B1`은 통과하고 그쪽도 `Defer()`를 쓴다. 따라서 deferred join 자체나
remote 자체가 깨진 것은 아니다. `ST-D1`의 remote 경로는 spot을 `delay-joined` mode로
target node에 만든 직후 source node에서 join을 건다. spot의 Location Store row가 source
node에 아직 보이지 않는 시점에 deferred join이 해석을 시도하는 가시성 문제일 수 있다.
`ST-D2`도 marker 부재이므로 같은 원인인지 함께 확인해야 한다.

다음 확인 순서는 이렇다. deferred join이 spot을 해석할 때 Location Store를 다시 읽는지,
읽는다면 그 시점이 target node의 spot 등록 publish보다 앞설 수 있는지, 앞설 수 있다면
`NotFound`를 종료로 볼지 재시도로 볼지가 계약 판단이다.

## 2026-07-31 ST-D1은 오래된 실패다

이번 변경이 만든 회귀가 아닌지 확인했다. 아니다.

같은 harness의 2026-07-10 실행 로그(`logs/20260710-074046-1007263`)도 `ST-D1`에서
실패한다. 그때는 **local 부분**에서 이미 멈췄다. `actor-a`의 `ST-D1` evidence가 한 건뿐이고
`actor-location-local-...`의 marker를 기다리다 끝났다. 오늘 실행은 local 부분이 일곱 건을
모두 남기고 통과한 뒤 remote 부분에서 멈춘다.

즉 `ST-D1`은 최소 3주 전부터 실패해 왔고, 오늘은 그때보다 더 진행한 지점에서 멈춘다.
`feature-map.ko.md`가 이 시나리오를 `구현`으로 표기한 것은 코드 작성 여부를 뜻하지
실행 결과를 뜻하지 않는다. 이번에 처음으로 실패 지점을 remote deferred join의 spot 해석
`NotFound`까지 좁혔다.

spec은 `NotFound`를 "요청한 User Spot을 찾을 수 없다"는 종료 오류로 정의한다
(`15-spot-actor.ko.md` §Failed.Kind 표). target node는 공개 API
`GetOrCreate(...).InMesh(...)`로 spot을 만들었고 `spot_created` 증거도 남겼으므로 spot은
존재한다. 존재하는 spot을 찾지 못하는 것이므로 계약대로 동작한 결과가 아니라 결함이다.

## 2026-07-31 ST-D1 시나리오의 spec 부합성 확인

고치기 전에 시나리오 자체가 계약에 맞는지 확인했다. 맞다. 따라서 결함은 runtime 쪽이다.

`ST-D1` remote는 target node에서 Spot을 만들고 그 다음 source node에서 join을 건다.
`21-location-runtime.ko.md` §생성 계약은 `GetOrCreate`가 `Ready` record에 대해 기존 ref를
돌려주고, 생성 결과로 `Ready`와 수용 공간과 최종 결과를 한 번에 기록한다고 정의한다. 즉
생성 call이 성공으로 반환한 시점에 Location Store record는 `Ready`다. 만든 직후 다른
node에서 조회하는 순서는 계약이 허용하는 순서이며 시나리오가 별도로 기다릴 의무는 없다.

Spot이 실제로 target node에 놓인 것도 확인했다. `spot_created` 증거는 Spot이 배치된
node의 생성 handler가 남기며 `actor-b`에 있다.

실패 지점은 `ZLinkActorRemoteJoiner.ResolveRemoteActorJoinTargetAsync`다.
`ZLinkLocationAddressResolvers.ResolveSpotHandleAsync`가 `null`을 돌려주면
`SPOT '{spotId}' has no live location row.`로 `NotFound`를 던진다. `Ready`로 기록된 Spot을
live location row 없음으로 판정하는 것이므로 계약대로 동작한 결과가 아니다.

`ST-B1`이 통과하는 이유도 함께 봤다. 그쪽은 Spot을 만든 뒤 세 node에 걸친
`ResetRelocationBlobMeasurementsAsync`를 실행하고 join한다. 그 사이 시간이 우연히 가림막이
되는 형태다. `ST-B1`은 Spot 배치를 명시적으로 단언하기까지 하므로, 두 시나리오의 차이는
계약이 아니라 생성과 join 사이의 간격뿐이다. 따라서 resolver가 방금 `Ready`가 된 row를
보지 못하는 조건을 봐야 한다.

## 2026-07-31 ST-D1 NotFound의 발생 지점

`NotFound`가 나오는 자리를 코드까지 좁혔다. Store에 record가 없어서가 아니라 owner
liveness 판정에서 걸러진다.

`ZLinkStoreLocationResolvers.ResolveSpotRowAsync`는 Spot authority를 읽은 뒤
`_liveRows.ResolveWithPresenceAsync(raw, row => row.OwnerId, ...)`로 owner lease가 살아
있는지 확인한다. 이 판정에서 `row`가 `null`이 되면 route를 무효화하고 `null`을 반환하며,
호출자인 `ZLinkLocationAddressResolvers.ResolveSpotHandleAsync`가 그대로 `null`을 돌려주고,
`ZLinkActorRemoteJoiner.ResolveRemoteActorJoinTargetAsync`가
`SPOT '{spotId}' has no live location row.`로 `NotFound`를 던진다.

주석이 밝히는 의도는 record가 없는 경우와 owner lease가 만료된 경우를 같게 다루는
것이다. `ST-D1`에서는 Spot을 만든 직후이므로 lease 만료는 아니다. 따라서 source node가
target node의 owner lease를 아직 관측하지 못한 상태에서 판정이 이뤄지는 쪽이다.

`ST-B1`이 통과하는 것과도 맞는다. 그쪽은 Spot 생성과 join 사이에 세 node에 걸친 별도
호출이 있어 그 사이에 관측이 갱신된다.

다음 확인 지점은 셋이다. `ResolveWithPresenceAsync`가 owner liveness를 어디서 읽는지,
그 값이 peer node의 lease 게시보다 늦게 갱신될 수 있는지, 그리고 방금 `Ready`가 된 row에
대해 관측이 없을 때 `null`로 끝내는 것이 계약에 맞는지다. 마지막 항목은 판단이 필요하다.
`21-location-runtime.ko.md`는 생성 call이 성공하면 record가 `Ready`라고 정의하므로,
`Ready`인데 조회가 실패하는 상태가 계약상 허용되는지부터 정해야 한다.

## 2026-07-31 ST-D1 NotFound의 메커니즘 — owner lease 음성 캐시

`IsOwnerLiveAsync`가 어떻게 판정하는지까지 따라갔다.

`ZLinkOwnerLeaseTracker.GetSnapshotAsync`는 owner lease 읽기 결과를
`_options.PollingInterval` 동안 캐시한다. 기본값은 1초다. 중요한 것은 **`Missing` 결과도
같은 캐시에 들어간다**는 점이다.

```
ZLinkOwnerLeaseReadResult.Missing => new Snapshot(null, MinValue, MinValue, fetchedAt)
```

`IsOwnerLiveAsync`는 `snapshot.Token is not null && IsUnexpired(snapshot)`이므로 캐시된
`Missing`은 그 1초 동안 계속 "owner가 살아 있지 않다"로 답한다. 그 사이에 들어온 Spot
조회는 record가 `Ready`여도 live row 없음으로 걸러진다.

여기에 두 번째 조건이 겹친다. spec은 `NotFound`를 `Failed`의 종료 kind로 정의하므로
deferred join은 재시도하지 않는다. 즉 **1초짜리 음성 캐시 한 번이 종료 실패로 굳는다.**
join call의 `Timeout(10s)`도 소용이 없다. 10초 안에 다시 시도하는 경로가 아니라 첫 판정이
그대로 결과가 되기 때문이다.

`ST-B1`이 통과하고 `ST-D1`이 실패하는 차이도 이것으로 설명된다. `ST-B1`은 Spot 생성과
join 사이에 세 node에 걸친 호출이 있어 그 사이 캐시가 만료되거나 성공 읽기로 덮인다.

판단이 필요한 지점은 하나다. 생성 call이 성공해 record가 `Ready`인데 owner lease 관측이
아직 없을 때, 이것을 종료 `NotFound`로 볼지 아니면 한 번 더 읽고 판정할지다. 후자라면
음성 결과를 캐시하지 않거나, live row 판정이 실패했을 때 owner lease를 강제로 다시 읽는
경로가 필요하다. 전자라면 spec이 "생성 성공 뒤에도 다른 node에서 잠시 조회되지 않을 수
있다"를 명시해야 하고, 그 경우 시나리오가 기다려야 한다.

## 2026-07-31 ST-D1 음성 캐시 가설은 반증됐다

앞 항목의 가설을 실제로 고쳐서 확인했다. 원인이 아니다.

`ZLinkOwnerLeaseTracker`가 종료 판정에 캐시된 음성 결과를 쓰지 않도록 바꿨다.
`IsOwnerLiveAsync`와 `IsOwnerTokenLiveAsync`는 캐시가 "lease 없음"이라고 답하면 store를
한 번 더 읽어 확인한 뒤에만 그 답을 확정한다. 양성 결과는 그대로 캐시를 쓰므로 store
부하는 늘지 않는다. 이 변경 자체는 유지한다. 캐시된 음성으로 재시도 없는 종료 실패를
만들지 않는 것은 그 자체로 옳다.

그런데 `ST-D1`은 여전히 같은 자리에서 `join_failed|NotFound`다. 따라서 owner lease 관측
지연이 방아쇠가 아니다.

다음 후보는 한 단계 앞이다. `ZLinkStoreLocationResolvers.ProjectSpot`은 authority record가
있어도 다음을 모두 만족해야 row를 만든다.

- payload가 User Spot으로 decode된다
- `State == Ready`
- `user.OwnerId == snapshot.OwnerId`
- `snapshot.OwnerLeaseGeneration > 0`
- `user.OwnerLeaseGeneration == snapshot.OwnerLeaseGeneration`

하나라도 어긋나면 owner liveness까지 가기 전에 `null`이 되고 결과는 같은 `NotFound`다.
`Ready` 여부만이 아니라 payload와 snapshot의 owner·lease generation이 일치하는지를 봐야
한다. 다음 확인은 실패 시점의 Redis authority record를 직접 읽어 이 다섯 조건 중 어느
것이 어긋나는지 특정하는 것이다.

`.NET` 전체 unit **1,376/1,376**이 통과하므로 이번 변경에 회귀는 없다.

## 2026-07-31 ST-D1 실패 시점의 Spot authority record

실행 중 Redis에서 해당 Spot의 authority meta를 직접 읽었다. Redis provider는 논리 키를
`opaque:map` hash로 opaque 키에 매핑하고, 각 opaque 키는 version별 zset이다. 최신 version을
읽은 결과는 다음과 같다.

```json
{
  "objectGeneration": 1,
  "authorityOwnerGeneration": 25,
  "ownerId": "8120f5d4c134478b99e8f942b151e2a5",
  "ownerLeaseGeneration": 2,
  "allocation": {
    "state": 2,
    "objectKind": 2,
    "stableType": "transfer-user-spot",
    "descriptor": { "meshName": "spot-actor-transfer", "rid": "actor-b-558d8beb-..." }
  }
}
```

확인된 사실은 셋이다. record가 존재한다. owner는 `actor-b`다. descriptor rid를 hex에서
풀면 `actor-b-...`다. `ownerLeaseGeneration`이 `2`이므로 `ProjectSpot`의
`snapshot.OwnerLeaseGeneration > 0` 조건은 만족한다.

따라서 남은 후보는 payload 쪽 세 조건이다. `user.State == Ready`,
`user.OwnerId == snapshot.OwnerId`, `user.OwnerLeaseGeneration == 2` 중 하나가 어긋난다.
payload는 별도 키에 binary로 저장되므로 다음 확인은 그 payload를
`ZLinkUserSpotAuthorityPayloadCodec`로 decode해 세 값을 직접 비교하는 것이다.

수집 방법도 남긴다. harness는 종료 시 Redis 컨테이너를 지우므로 실행 중에 읽어야 한다.
`redis_started name=`에서 컨테이너 이름을 얻고, `*opaque:map` hash의 `hkeys`에서
`authority:meta`와 Spot id로 논리 키를 찾은 뒤 `hget`으로 opaque 키를 얻어 `zrange -1 -1`로
최신 version을 읽는다. 40시간까지 살아 있는 고아 컨테이너가 있으므로 이름은 반드시 그
실행의 로그에서 얻어야 한다.

## 2026-07-31 ST-D1 근본원인 좁히기 — 응답 경로가 원인이다

진단을 코드에 심어 실행하며 좁혔다. 결론부터 쓰면 **조회는 전부 성공하고, actor
핸들러도 성공적으로 완료하는데, 그 응답이 호출자에게 돌아가지 못한다.**

먼저 앞선 두 가설이 증거로 반증됐다.

`ProjectSpot`의 다섯 조건이 어긋난다는 가설은 틀렸다. 진단이 찍은 값은
`state=Ready payload_owner=09dcbfd8... snapshot_owner=09dcbfd8... payload_lease=2
snapshot_lease=2`로 **다섯 조건이 전부 통과**한다. owner lease 생존 게이트가 row를
버린다는 가설도 틀렸다. `live_row_rejected` 추적이 한 건도 찍히지 않았다.
`resolve_spot_row`는 store에서 1회 성공했고 `project_spot_no_authority`도 없다.

또한 실패 지점을 `ResolveRemoteActorJoinTargetAsync`로 본 것도 틀렸다. 그 함수의
`"has no live location row"` 메시지는 로그에 아예 나타나지 않는다.

실제 경로는 이렇다. `ActorNodeEndpoints`의 `/actors/{actorId}/join`이
`actorClient.RequestToActor(...)`를 호출하고, 이것이 `ZLinkFrameworkException`
`NotFound`를 던져 `join_failed` 증거가 남는다. 그런데 actor-a의 flow 추적을 보면 그
요청은 `phase=received`와 `phase=replied`가 모두 찍힌다. 그리고 핸들러
(`ActorRuntime.ExecuteAsync`)는 `commit_request` 증거를 남긴 직후
`new JoinTargetRes(..., true, ...)`를 반환한다. `commit_request`는 성공 반환 직전의
마지막 줄이므로, **핸들러는 끝까지 성공했다.**

따라서 결함은 응답 전달 경로에 있다. actor가 원격 spot으로 이동하는 중이라 응답이
요청자에게 라우팅되지 못하고, 프레임워크가 이를 `NotFound`로 요청자에게 올린다.
in-flight 요청의 응답이 actor 재배치를 견디게 하는 장치가 message follow인데,
`ZLinkActorMessageFollower`의 추적(`actor_follow_*`, `message_follow_registered`)이
**한 건도 찍히지 않았다.** follow가 등록되지 않는 것이 다음 확인 지점이다.

시나리오 자체는 스펙에 부합한다. `delay-joined` 게이트로 커밋이
`OnJoinedActorAsync` 완료 후에만 일어나는지를 검증하며, 게이트 해제 전 source ref가
actor-a에 남아 있는지까지 확인한다. 결함은 런타임 쪽이다.

## 2026-07-31 ST-D1 근본원인 확정 — target 적격성 판정에서 실패한다

삼켜지던 예외를 드러내 실패 지점을 끝까지 따라갔다. 전체 사슬은 다음과 같다.

```
ZLinkDeferredActorJoin.RunAsync
  └ ZLinkActorRemoteJoiner.SubmitRoutedJoinActorTransactionAsync
      └ DecodeAdmissionReplyAndDispose  ← NodeB가 Error envelope로 응답
          └ "Actor '...' target became unavailable." (NotFound)
```

NodeB 쪽에서 그 Error를 만드는 곳은 `ZLinkFrameworkRuntimeActors`의 admission 처리이며,
`authorityStore.ReserveRelocationCapacityAsync`가
`ZLinkRelocationCapacityReserveResult.TargetUnavailable`을 반환한 경우다.

Redis provider(`ZLinkProviderLocationRepository.Authority`)에서 그 결과가 나오는 조건은
하나뿐이다. `ReadEligibleTargetAsync(TargetDescriptor, TargetNodeLifecycleGeneration,
TargetOwner, ObjectKind, StableType)`가 `null`을 반환하는 경우다. 용량 부족은
`PlacementCapacityExhausted`로 따로 분기하므로 용량 문제가 아니고, **target이 적격
대상으로 조회되지 않는 것**이 원인이다.

증거가 이 결론과 맞는다. actor-b의 evidence에는 `spot_created`만 있고 `admission`이
없다. 즉 앱 핸들러까지 가기 전에 런타임이 거부한다. 반면 spot 조회 자체는 성공하며
(`resolve_spot_row source=store` 1회 성공, `project_user_spot` 다섯 조건 통과), 앞서
읽은 authority meta도 `state=Ready`에 owner/lease가 일치했다.

다음 확인은 `ReadEligibleTargetAsync`의 인자 다섯 중 무엇이 어긋나는지다. 후보는
`TargetNodeLifecycleGeneration`과 `TargetOwner`이며, 앞서 읽은 record의
`descriptorLifecycleGeneration`과 대조하면 판별된다.

### 조사 과정에서 드러난 관측성 결함

`ZLinkActorJoinCompletion.Failed`는 `(OperationId, Kind, IsRetriable)`만 담고 원인
예외를 버린다. `ZLinkDeferredActorJoin`의 `catch (Exception exception)`이 예외를
`MapFailure`로 kind만 뽑고 그대로 삼키므로, 같은 kind로 매핑되는 수십 개 throw 지점이
바깥에서 완전히 동일해 보인다. 이번 조사가 오래 걸린 직접적 원인이다. 그 지점에
원인 예외를 남기는 추적을 넣었고, 추적 비용이 꺼진 상태에서 0이므로 상시 유지한다.
e2e의 join 실패 catch도 kind만 기록하고 메시지·스택을 버려 같은 문제가 있었다.

## 2026-07-31 ST-D1 실패 조건 확정 — target node의 placement weight가 0이다

`ReadEligibleTargetAsync`의 아홉 조건 중 어느 것이 어긋나는지 진단으로 찍었다.
실패 직전 actor-b의 마지막 검사가 정확히 이것이다.

```
eligible_target_check owner_live=True lifecycle=2994233909027177458/2994233909027177458
  owner=69d2d79e.../69d2d79e... lease=2/2 role=Server state=Serving
  weight=0 kind=Actor stable_type=transfer-stateful
```

`weight=0`이므로 `descriptor.PlacementWeight <= 0` 조건에 걸려 `null`이 반환되고,
`TargetUnavailable` → `NotFound`로 이어진다. 실행 전체에서 `weight=0`인 Actor 검사는
**이 한 건뿐**이고 나머지는 전부 `weight=100`이므로, 이 검사가 실패한 그 검사다.

weight가 0인 이유는 시나리오 하네스에 있다. `WithPlacementNodeAsync`는 생성 대상
node만 100으로 두고 나머지를 0으로 내려 배치를 고정한 뒤 `finally`에서 전부 100으로
되돌린다. ST-D1 remote는 `CreateSpotAsync(NodeB)` 다음에
`CreateActorAsync(NodeA)`를 부르므로 두 번째 단계에서 **NodeB가 0으로 내려간다.**
확정된 사실은 하나다. **join 시점의 store 조회가 weight 0을 봤다.** 복원은 동기 HTTP
호출이고 응답값까지 단언하므로 node의 in-memory 값은 100이다. 둘 사이의 간극을
설명하는 후보는 store descriptor 발행이 비동기라는 것이고,
`ZLinkAutoConnectReconciler._pendingPlacementWeight`가 그 발행 경로를 시사한다.
다만 이는 아직 가설이며 측정하지 않았다.

### 판단이 필요한 지점

스펙은 placement weight를 일관되게 **후보 중 target을 고르는** 수단으로 서술한다.
"Serving 상태와 capacity를 확인한 뒤 node-wide placement weight로 target 하나를
선택한다", "placement weight를 적용해 target을 결정한다"가 그 예다. ST-D1의 join은
target spot을 명시 지정하므로 선택이 일어나지 않는다.

코드에도 이 구분이 이미 있다. `ReadEligibleTargetAsync`는
`requireNewPlacementEligibility` 인자를 가지며 `CommitAsync`와 `CompleteCommitAsync`는
`false`를 넘긴다. 반면 relocation capacity 예약 경로는 기본값 `true`를 쓴다.

따라서 후보는 둘이다. 하나는 placement weight 변경이 store에 반영된 뒤 반환되게
하는 것이고, 다른 하나는 명시 지정된 target의 relocation 예약에 new-placement
적격성을 요구하지 않는 것이다. 스펙 근거는 후자를 지지하지만, 전자도 선택 경로의
정합성 문제로 따로 남아 있다. 어느 쪽을 고치든 다른 시나리오(배치 고정을 쓰는
ST 계열 전반)에 영향이 있으므로 확정 전에 영향 범위를 봐야 한다.

## 2026-07-31 ST-D1 수정 1차 — 명시 지정 target에 new-placement 적격성을 요구하지 않는다

### 측정으로 확인한 것

진단에 `rid`를 추가해 거부된 descriptor가 어느 노드인지 **추론이 아니라 측정**으로
확인했다. `rid=actor-b-...`로 target node가 맞았다.

`weight=0` 게이트를 없애도 되는지는 graceful drain이 그 값을 쓰는지에 달려 있었다.
확인 결과 **drain은 placement weight를 0으로 두지 않는다.** 코드의 모든 사용처가
후보 필터링(`OrderByDescending`, 선택 헬퍼의 `PlacementWeight > 0`)이고,
`28-graceful-drain-handoff` 스펙에는 placement weight 언급이 아예 없다. 따라서 이
게이트를 예약 경로에서 제거해도 drain 계약은 깨지지 않는다.

### 수정

`ZLinkProviderLocationRepository.Authority`의 relocation capacity 예약이
`ReadEligibleTargetAsync`에 `requireNewPlacementEligibility: false`를 넘기게 했다.
호출자가 target을 명시 지정했으므로 선택이 일어나지 않고, 선택을 지배하는
new-placement 규칙도 적용되지 않는다. capacity와 liveness는 아래 검사들이 그대로
막는다. 같은 파일의 `CommitAsync`와 `CompleteCommitAsync`가 이미 `false`를 넘기고
있었으므로 이 구분은 원래 설계에 있던 것이다.

### 결과

ST-D1 remote가 admission 단계까지 전진했다. 수정 전 actor-b의 evidence에는
`spot_created`뿐이었고 admission 자체가 일어나지 않았다. 수정 후에는
`admission|spot=...|mode=delay-joined|input=actor-id-only`가 기록된다. 즉
`TargetUnavailable`은 실제로 잘못된 거부였다.

### 남은 다음 층

시나리오는 아직 통과하지 않는다. `joined_wait`가 관측되지 않고 source가
`reject_reply`를 기록한다. 그런데 앱의 admission 핸들러는 `mode=delay-joined`이고
`ExpectedMode`도 reject가 아니므로 `ZLinkSpotActorJoinResult.Accept`를 반환한다.
앱이 Accept를 반환했는데 source가 Rejected 완료로 분류하는 것이며, admission과
`OnJoinedActorAsync` 사이에서 흐름이 끊긴다. 이것이 다음 조사 대상이다.

수정이 다른 시나리오를 깨뜨리지 않는지 전체 ST 스위트를 돌려 확인 중이다.

### 회귀 검증 결과

수정 적용 후 시나리오별 실행 결과는 다음과 같다.

| 시나리오 | 결과 | 판정 |
|---|---|---|
| ST-A1, ST-A2, ST-A3, ST-B1 | 통과 | 정상 |
| ST-F4, ST-F5 (message follow) | 통과 | 정상 |
| ST-B3 | 실패 | 기존 실패("전환 대상"), `ProbeAsync` HTTP 500 |
| ST-C1, ST-I4 | 실패 | **기존 실패** |

ST-C1과 ST-I4는 수정을 되돌린 상태에서도 동일하게 실패하는 것을 직접 확인했다.
따라서 이 수정으로 인한 회귀는 없다. relocation과 message follow의 핵심 경로
(ST-A 계열, ST-B1, ST-F4, ST-F5)는 모두 통과한다.

`Zlink.Framework.sln` 테스트에서 `SampleRegressionTests` 134건이 통과한다.
이전에 이 프로젝트는 필터된 게이트에서 실행되지 않아 실패가 가려져 있었다.

## 2026-07-31 ST-D1 2차 층 — admission은 수락되고 join reply가 거부된다

1차 수정 후 실패가 이동한 지점을 진단으로 특정했다.

```
admission_decision actor=... spot=... accepted=True user_spot=True entry_spot=False
source_rejected site=join_reply actor=... spot=... target_rid=actor-b-...
```

target의 admission 결정은 **수락**이다(`accepted=True`). 앱 핸들러가 도달해
`ZLinkSpotActorJoinResult.Accept`를 반환한 것과 일치한다. 그 다음 단계인 join 요청의
응답에서 `reply.Accepted`가 false가 되어 `ZLinkActorRemoteJoiner`가
`ZLinkActorJoinResult.Rejected`를 만든다. 이것이 source의 `reject_reply` 증거다.

actor-b의 evidence는 `admission`에서 멈추고 `joined_wait`가 없다. 즉 target이
`OnJoinedActorAsync`를 부르기 전에 join 요청을 거부한다. delay-joined 게이트는 그
콜백 안에 있으므로 시나리오가 기다리는 `joined_wait`는 나올 수 없다.

정리하면 사슬은 이렇다. admission 수락 → join 요청 → target이
`OnJoinedActorAsync` 이전에 거부 → source가 Rejected로 완료. 다음 조사는 target의
join 요청 처리에서 `OnJoinedActorAsync` 이전에 거부를 만드는 조건이다.

## 2026-07-31 ST-D1 2차 층 원인 — handoff commit의 compare-exchange 충돌

삼켜지던 예외를 또 드러내 원인을 찾았다. target의 join 요청 처리
(`JoinRoutedActorAsync`)가 authority commit에서 예외를 받아 `catch`에서
`CreateRejectedHandoffReply`로 바꾼다. 그 예외는 이것이다.

```
ZLinkFrameworkException: Actor '...' authority changed during handoff commit.
  at ZLinkActorOwnershipCoordinator.CommitTransferredActorAuthorityAsync (line 630)
  at ZLinkFrameworkRuntime.PublishTransferredActorAuthorityAsync
  at ZLinkFrameworkRuntime.JoinRoutedActorAsync (line 580)
```

발생 조건은 `ZLinkAuthorityCompareExchangeResult.Conflict`가 나오고
`TryResolveCommittedAuthority`가 현재 record를 이 handoff의 결과로 해석하지 못한
경우다. 즉 actor authority record에 대한 optimistic concurrency 충돌이다.

이것이 `OnJoinedActorAsync` 이전에 거부가 나오는 이유를 설명한다. commit이 콜백보다
먼저이고, commit이 실패하면 콜백은 아예 호출되지 않는다.

### 주목할 점

이 예외는 `ZLinkFrameworkErrorKind.Unavailable`에 `ZLinkRetryAdvice.RetryAfterBackoff`를
달고 있다. 즉 재시도 가능한 일시적 충돌로 **분류되어 있다**. (뒤의 3차 층 절에서
측정한 결과 실제로는 일시적 충돌이 아니었다. 분류 자체가 실상과 맞지 않는다.) 그런데 `catch`는 이를
재시도하지도, 재시도 가능성을 응답에 싣지도 않고 영구적인 Rejected 응답으로 바꾼다.
source는 그 응답을 받아 `ZLinkActorJoinResult.Rejected`를 만들고 시나리오는
`reject_reply`로 끝난다. 일시적 충돌이 영구 거부로 격하되는 셈이며, 이것이
다음 판단 지점이다.

충돌 자체의 원인(무엇이 record를 동시에 바꾸는지)은 아직 측정하지 않았다. 다음
확인은 conflict의 current snapshot과 기대값을 나란히 찍어 어느 축이 어긋나는지
보는 것이다.

## 2026-07-31 ST-D1 3차 층 — CAS 충돌은 동시 변경이 아니다

충돌 시점의 기대값과 현재값을 나란히 찍었다.

```
handoff_cas_conflict actor=...
  expected_version=9e1b217281fb4f31ac32189fbd81d513
  expected_target_gen=35
  current=Found current_detail=owner=dc4d1394... lease=1 object_gen=1
                  authority_gen=34 version=9e1b217281fb4f31ac32189fbd81d513
```

**store version이 기대값과 완전히 같다.** 즉 record는 읽은 뒤로 바뀌지 않았고,
이것은 optimistic concurrency 충돌이 아니다. 앞 절에서 이 실패를 "재시도 가능한
일시적 충돌"로 적었는데, 그 해석은 틀렸다. 재시도해도 같은 결과가 나온다.

어긋나는 축은 authority owner generation이다. 예약 단계가 target generation을 35로
계산했는데 record의 현재 generation은 34다. 즉 **capacity 예약이 record가 도달하지
않은 generation을 기대값으로 잡았다.** `ZLinkAuthorityGenerationTransition.NewOwner`가
34에서 35를 만들어야 하는데 CAS는 version이 일치하는데도 Conflict를 반환한다.

이 결함은 1차 층 수정으로 비로소 도달 가능해진 지점에 있다. 1차 수정 전에는 admission
자체가 일어나지 않아 이 경로가 실행되지 않았다.

다음 확인은 예약 단계에서 `expectedTargetAuthorityOwnerGeneration`이 어떻게
계산되는지와, provider의 CAS가 version 일치에도 Conflict를 반환하는 조건이다.
후자는 `ReserveRelocationCapacityAsync`가 읽은 counter와 commit 시점의 record
generation이 서로 다른 원천에서 온다는 뜻일 수 있다.

## 2026-07-31 ST-D1 해결 — 런타임 결함 2건 + 시나리오 스펙 위반 2건

ST-D1이 통과한다(`operation SpotActorTransfer.ST-D1 passed`). 원인은 네 겹이었다.

### 런타임 결함 (2건, 같은 뿌리)

명시 지정된 target의 relocation에 **선택(selection) 규칙인 new-placement 적격성을
적용**하던 것이 두 곳에 있었다. `ReadEligibleTargetAsync`의
`requireNewPlacementEligibility`는 `descriptor.PlacementWeight <= 0`이면 target을
탈락시키는데, placement weight는 후보 중 하나를 고를 때 쓰는 값이다. 호출자가 target을
직접 지정한 relocation에는 선택이 없다.

첫째, `ReserveRelocationCapacityAsync`의 예약 경로. 여기서 걸리면 `TargetUnavailable` →
`NotFound`가 되어 admission 자체가 일어나지 않았다.

둘째, `IsEligibleTargetAsync`(commit 경로에서만 쓰인다). 이미 예약까지 끝난 relocation을
commit할 때 적격성을 다시 확인하며, 여기서 걸리면 CAS가 Conflict를 반환하고
"authority changed during handoff commit"으로 포장되어 영구 거부가 됐다. 이 helper는
호출자가 하나뿐이고 그 호출자가 commit 경로이므로 helper 자체를 고쳤다.

두 곳 모두 `requireNewPlacementEligibility: false`로 바꿨다. 같은 파일의
`CommitAsync`와 `CompleteCommitAsync`가 이미 `false`를 넘기고 있었으므로 이 구분은
원래 설계에 있던 것이고, 이 둘이 누락돼 있었다.

drain 계약을 깨뜨리지 않는다는 것은 미리 확인했다. drain은 placement weight를 0으로
두지 않으며, `28-graceful-drain-handoff`에 그 값에 대한 언급이 없다.

### 시나리오의 스펙 위반 (2건)

런타임을 고치고 나니 시나리오 자체가 스펙과 어긋나 있었다. 둘 다
`15-spot-actor.ko.md`가 명시적으로 규정하는 사항이다.

하나. remote 절반이 `joined_wait` 시점에 source ref가 아직 `actor-a`여야 한다고
단언했다. 스펙은 "Application이 요청한 User Spot join은 target admission callback,
**commit 뒤 target joined**와 source leave notification을 사용한다"고 규정한다. commit이
joined보다 먼저이므로 그 시점의 ref는 정당하게 target을 가리킨다. 단언을 스펙 순서에
맞춰 뒤집었다.

둘. `success_reply`를 NodeA에서 기다렸다. 스펙은 "`Accepted`는 **target Actor**,
`Rejected`와 commit 전 `Failed`는 source Actor가 받는다"고 규정한다. source는 leave
notification을 받고 Accepted completion은 target의 것이므로 NodeB로 옮겼다.

이전 기록에서 "시나리오는 스펙에 부합한다"고 적었던 것은 **틀렸다.** 게이트 구조만
보고 판단했고 순서·수신자 조항을 확인하지 않았다. 런타임 결함을 걷어내고 나서야
드러났다.

## 2026-07-31 ST-C3 — 1/4 해결, 나머지는 스펙 해석 판단이 필요하다

### 해결한 부분

네 하위 케이스 모두 `!response.Accepted`를 단언하고 있었다. `.Defer()` 뒤 handler가
돌려주는 값은 join을 시작했다는 응답이지 join 결과가 아니다. 스펙은 "Join 결과는
0이 아닌 128-bit `OperationId`와 함께 Actor completion callback으로 전달한다"고
규정한다. 네 단언을 제거했고, 각 케이스가 이미 갖고 있던 completion evidence 단언이
실제 판정을 담당한다. transfer-out 케이스는 이것으로 통과한다. 런타임은 이미
스펙대로였다(`transfer_out_failed` → `join_failed|InternalFailure`가 source로).

### 판단이 필요한 부분 — source leave 실패

시나리오는 leave 실패 시 `join_failed`가 나오고 target에 `transfer_in`과 `joined`가
없어야 한다고 기대한다. 실제 런타임은 반대로 동작한다. target은
`transfer_in` → `authority_committed` → `joined`까지 완료하고, source만
`leave_failed`를 반복한다.

**런타임 쪽이 스펙에 맞다.** leave notification은 commit 뒤에 오고("commit 뒤 target
joined와 source leave notification"), 스펙은 "Commit 뒤 recovery는 source로
rollback하지 않고 target을 복구한 뒤 같은 `OperationId`의 `Accepted`를 전달한다"고
명시한다. 즉 leave 실패로 transfer를 되돌리지 않는 것이 계약이다.

그런데 여기서 막힌다. 스펙은 동시에 "Target joined callback, source leave
notification과 durable cleanup이 끝나기 전에 completion과 뒤 application payload를
실행하지 않는다"고 규정한다. application의 leave callback이 계속 실패하면 completion
자체가 영원히 나오지 않는다. 실제로 `leave_failed`가 무한히 반복되고 `success_reply`도
`join_failed`도 나오지 않는다.

따라서 두 조항이 이 경우에 서로 맞물리지 않는다. Accepted를 전달해야 하는데,
그 전제인 leave notification이 끝나지 않는다. 시나리오를 어느 쪽으로 고쳐 쓸지는
이 해석을 정하는 문제이므로 임의로 결정하지 않았다. 함께 볼 것은 두 가지다. 실패하는
application leave callback을 무한 재시도하는 것이 의도인지, 그리고 그 경우 completion을
어떤 시점에 어떤 값으로 내보낼지다.

나머지 두 케이스(transfer-in 실패, joined 실패)도 같은 판단에 걸려 있을 가능성이
높으므로 함께 정하는 것이 낫다.

## 2026-07-31 ST-D2 — fencing은 동작한다, 시나리오가 한 층 아래를 관측한다

`source_cleanup_attempt` 증거가 관측되지 않아 실패한다. 원인을 측정으로 확정했다.

시나리오는 source의 cleanup delete를 `CleanupGatedLocationStore`로 가로채 관측한다.
그 store는 `ZLinkStoreMutation.Delete`가 든 write를 볼 때만 게이트를 연다. 그런데
source의 release는 store write까지 가지 않는다.

`ReleaseTrackedActorAsync`는 `ZLinkAuthorityMutation.Delete()`를
`CompareExchangeAuthorityAsync`로 낸다. 그 CAS는 진입부에서 version을 대조하는데,
target이 이미 authority를 가져갔으므로 source가 들고 있던 version은 낡았다. actor-a에서
찍힌 진단이 이것을 그대로 보여준다.

```
cas_conflict_reason missing=False state=Active version_match=False fence=none
```

record는 존재하고 state도 Active인데 version만 어긋난다. 따라서 CAS는 store에 아무
mutation도 내지 않고 Conflict로 끝나고, 게이트가 걸릴 delete write 자체가 없다.

**이것은 결함이 아니라 의도된 동작이다.** `ZLinkActorSessionLocationOwnership`의 주석이
그대로 말한다. "A release racing the new owner's Takeover is ignored as stale by the
store, which is the intended fencing outcome." 즉 ST-D2가 검증하려는 fencing은 이미
동작하고 있으며, 다만 시나리오가 관측하려는 지점보다 한 층 위에서 끝난다.

따라서 고칠 대상은 시나리오다. stale release가 store delete로 도달한다고 전제하지 말고,
release가 stale로 거절되는 사실 자체를 관측해야 한다. 현재 하네스에는 그 신호가 없으므로
`CleanupGatedLocationStore`를 CAS 결과를 관측하는 형태로 바꾸는 등 하네스 확장이 필요하다.
이는 단순 단언 수정이 아니라 전환 작업이므로 ST-B3·ST-B4와 같은 범주로 둔다.

## 2026-07-31 ST-C1 해결 — 하네스 결함 1건 + 시나리오 결함 1건

ST-C1이 통과한다. 런타임은 처음부터 정상이었고 원인은 둘 다 테스트 쪽이었다.

### 하네스 — 기다리던 예외를 잡지 못했다

`WaitUnavailableAsync`는 node가 죽었다는 증거로 연결 실패를 기다린다. 그런데
`error.Kind == ZLinkFrameworkErrorKind.InternalFailure && HasConnectionFailure(error)`로
걸러서, 정작 그 예외가 filter를 통과하지 못하고 밖으로 새어 나갔다.

`ZLinkHttpClient`는 연결 실패를 **`Unavailable`**로 매핑한다. `RetryPolicy`의 주석이
"connection failures as `ZLinkFrameworkErrorKind.Unavailable`"이라고 명시한다. 즉
`InternalFailure`를 키로 잡은 것이 처음부터 틀렸다.

connection 실패라는 사실 자체가 이 loop가 기다리는 증거이므로, kind 조건을 없애고
`HasConnectionFailure(error)`만으로 판정하게 했다.

### 시나리오 — ST-C3와 같은 혼동

`response.Accepted`가 false여야 한다고 단언했다. `.Defer()` 뒤 handler가 돌려주는
값은 join을 시작했다는 응답이고, 이 시나리오에서는 NodeA를 죽이기 **전에** 이미
반환된다. 따라서 true인 것이 정상이다. 스펙대로 실제 판정은 target의
`pending_admission_expired` runtime marker와 `transfer_in` 부재가 담당하며, 그 단언은
이미 시나리오에 있었다.

ST-C3에서 고친 것과 정확히 같은 오해다. `.Defer()`를 쓰는 시나리오 전반에서
handler 반환값을 join 결과로 읽는 패턴을 함께 점검할 필요가 있다.

## 2026-07-31 ST-I4 현황 — follow는 등록되고 reply만 gate에 잡히지 않는다

`StI4ActorMessageFollowMatrixScenario`가 line 94
(`WaitExternalTransportDeliveryAsync(replyGate)`)에서 timeout으로 실패한다. 앞의 세
gate(baseline, oneWay, request)는 모두 정상 포착된다.

message follow 자체는 동작한다. 진단에 다음이 찍힌다.

```
message_follow_registered source_rid=actor-a-... target_rid=actor-b-... entries=1
```

즉 source에서 target으로 follow가 등록됐고, 시나리오의 relocation 단계
(`success_reply`를 target에서 기다리는 단언 포함)도 통과한다. 참고로 이 시나리오가
`success_reply`를 **target**에서 기다린다는 점은 ST-D1에 적용한 스펙 해석
("`Accepted`는 target Actor가 받는다")을 뒷받침한다.

남은 것은 reply가 external transport gate에 포착되지 않는 이유다. gate는
`afterGateId: requestGate`로 무장되어 request 다음의 전달을 잡도록 되어 있다. 가설은
relocation 뒤 reply가 gate가 계측하는 연결과 다른 경로로 나가서 gate가 보지 못한다는
것이다. 이것이 계약 변화인지 하네스 전제의 문제인지는 아직 판별하지 않았다.

다음 확인은 reply가 실제로 어느 연결로 나가는지다. gate의 서버측 구현이 어떤 지점에
붙어 있는지 먼저 보고, follow relay가 그 지점을 지나는지 대조하면 판별된다.

## 2026-07-31 ST-I4 진단 완료 — 하네스가 응답을 잘못된 flow에서 기다린다

원인을 코드로 확정했다. 런타임 결함이 아니다.

external transport gate는 각 node 앞의 TCP proxy(`Support/stream_marker_proxy.py`)가
바이트 마커를 매칭해 스트림을 붙잡는 방식이다. `replyGate`는 마커 없이
`afterGateId: requestGate`로 무장된다. proxy에서 after-gate는 다음 조건으로만 포착한다.

```python
gate.after_gate_id is not None and gate.target_flow is self
```

그리고 `target_flow`는 참조 gate가 매칭된 flow의 **peer**로 설정된다
(`child.target_flow = parent_flow.peer`). proxy는 방향마다 별도 `Flow`를 만들므로
(`left_to_right`, `right_to_left`), 결국 `replyGate`는 **request가 지나간 연결의 반대
방향**에서만 응답을 기다린다.

여기서 전제가 깨진다. request는 caller node → source node 방향에서 포착됐다. 그 뒤
actor가 target node로 재배치되므로, 응답은 source가 아니라 target에서 나온다. 즉
source → caller 방향으로는 아무것도 흐르지 않고, gate는 영원히 포착하지 못한다.

message follow 자체는 정상이다. 진단에
`message_follow_registered source_rid=actor-a-... target_rid=actor-b-... entries=1`이
찍히고, 앞의 세 gate(baseline, oneWay, request)와 relocation 단계는 모두 통과한다.

정리하면 하네스의 `afterGateId`가 "응답은 요청이 지나간 연결의 역방향으로 돌아온다"고
가정하는데, 이 시나리오가 검증하려는 상황(응답자가 이동한 뒤 응답)에서는 그 가정이
성립하지 않는다. gate를 target → caller flow에 걸 수 있도록 proxy의 after-gate 바인딩을
확장해야 한다. 단언 수정이 아니라 하네스 확장이므로 ST-D2·ST-B3·ST-B4와 같은 범주다.

## 2026-07-31 스펙 15-spot-actor 내부 불일치 — completion과 durable cleanup의 순서

ST-C3의 leave 실패를 판단하려고 스펙을 정독하다 조항 간 불일치를 찾았다.

산문(§276-279)은 이렇게 말한다.

> "Target joined callback, source leave notification과 **durable cleanup이 끝나기 전에**
> completion과 뒤 application payload를 실행하지 않는다."

즉 completion은 durable cleanup 이후다. 그런데 같은 문서의 순서 목록과 sequence
diagram은 반대로 적혀 있다. 단계 7은 "Target joined callback과 source leave
notification을 실행한다. **그다음** target Actor에서 `Accepted` completion callback과
accepted journal을 ... replay한다"이고, 단계 8이 그 뒤에 "남은 source resource의
durable cleanup을 끝낸 뒤 Completed authority CAS를 수행한다"이다. diagram도 같다.

```
SourceRuntime->>TargetSpot: target joined callback 실행
SourceRuntime->>SourceActor: source leave notification 실행
SourceRuntime->>TargetActor: Accepted completion과 journal replay
SourceRuntime->>TargetActor: 실행 전 queue와 hold message 전달
SourceRuntime->>SourceRuntime: durable source cleanup
```

산문은 completion이 durable cleanup을 기다린다고 하고, 단계 목록과 diagram은
completion이 durable cleanup보다 먼저라고 한다. 셋 중 둘이 한쪽이므로 산문 쪽 문장을
고치는 것이 유력하지만, 이는 스펙 소유자가 정할 문제다.

두 서술이 일치하는 부분은 **completion이 source leave notification 뒤**라는 점이다.
따라서 ST-C3의 미결 질문은 그대로 남는다. application의 leave callback이 계속
실패하면 그 전제가 끝나지 않아 completion이 나오지 않는다. 스펙은 실패하는 leave
callback을 어떻게 다룰지 규정하지 않는다. 이것은 불일치가 아니라 **미규정**이며,
ST-C3의 나머지 세 하위 케이스를 고치려면 먼저 정해야 한다.

## 2026-07-31 ST-B3·ST-B4 재분류 — "전환 대상"이 아니라 런타임 결함이었다

전체 시나리오를 개별 실행해 확정 현황을 만드는 과정에서 드러났다. **ST-B3와 ST-B4가
통과한다.** 두 시나리오는 그동안 "시나리오를 현재 relocation 계약에 맞게 다시 써야
하는 전환 대상"으로 분류돼 있었는데, 실제로는 런타임 결함이었고 ST-D1에서 고친
`requireNewPlacementEligibility` 2개소 수정으로 함께 해소됐다.

이 분류 오류는 전체 스위트가 첫 실패에서 멈추는 성질 때문에 오래 남아 있었다.
ST-B3가 앞쪽에서 실패하면 그 뒤는 실행되지 않고, 실패 사유만 보고 "시나리오가 낡았다"고
판단하기 쉬웠다. 개별 실행으로 확정 현황을 만들지 않으면 이런 오분류는 계속 남는다.

교훈은 두 가지다. 첫째, 스위트가 첫 실패에서 멈추는 하네스에서는 개별 실행 매트릭스가
주기적으로 필요하다. 둘째, 실패 사유만으로 "시나리오 결함"을 단정하지 말 것. ST-B3의
`ProbeAsync` HTTP 500은 시나리오가 낡아서가 아니라 런타임이 admission을 거부해서였다.

### ST-B5는 환경 문제다

ST-B5는 `inotify_add_watch failed` (`Errno 28`, ENOSPC)로 실패했다. `kill_on_file_marker.py`가
watch를 걸지 못해 죽고, 그 여파로 client가 config를 찾지 못했다. 확인 결과 다른
시나리오 로그에는 inotify 오류가 없고 현재 instance 사용량도 1024 중 21로 여유가 있다.
즉 일시적 소진이며 코드 결함이 아니다. 재실행으로 확인해야 한다.

## 2026-07-31 SpotActorTransfer 전체 시나리오 확정 현황 (29개 개별 실행)

스위트가 첫 실패에서 멈추므로 29개를 개별 실행해 확정 현황을 만들었다.

**통과 14**: ST-A1, ST-A2, ST-A3, ST-B1, ST-B3, ST-B4, ST-C1, ST-D1, ST-F1, ST-F4,
ST-F5, ST-F6, ST-G3, ST-H1, ST-I6

**실패 15**: ST-B2, ST-B5, ST-C2, ST-C3, ST-D2, ST-E1, ST-E1A, ST-E2, ST-F2, ST-F3,
ST-G6, ST-I1, ST-I4, ST-I5

이 세션에서 ST-B3, ST-B4, ST-C1, ST-D1이 실패에서 통과로 바뀌었다.

### 실패의 군집

bound session 경로가 가장 큰 군집이다. ST-E1(`actor-bound-sess` evidence 미관측),
ST-E1A(old binding에서 `ActorLocationStale` 대신 `Request timed out`), ST-E2(timeout),
ST-F3(`actor-bound-orde` evidence 미관측)가 모두 여기에 속한다. 네 개가 한 원인일
가능성이 높으므로 우선순위가 가장 높다.

external transport gate 군집이 다음이다. ST-I4와 ST-I5가 같은
`did not capture a delivery`로 실패한다. ST-I4는 원인을 확정했다(after-gate가 요청이
지나간 연결의 역방향에만 걸리는데, 응답자가 이동하면 그 방향으로 아무것도 흐르지
않는다). ST-I5도 같은 하네스 한계일 가능성이 높다.

나머지는 개별 원인이다. ST-B2(transport endpoint 미연결), ST-B5(환경, inotify),
ST-C2·ST-D2(evidence 미관측), ST-C3(leave 실패 미규정), ST-F2, ST-G6(HTTP timeout),
ST-I1(HTTP 500).

### 이번 세션 수정이 원인이 아님을 확인했다

E 계열이 이 세션의 수정 때문에 생긴 것인지 확인했다. `requireNewPlacementEligibility`
2개소를 임시로 되돌리고 ST-E1A를 실행한 결과 **완전히 동일한 실패**가 나왔다.

```
ST-E1A expected ActorLocationStale from the old binding, got 'Request timed out.'
```

따라서 E 계열은 기존 결함이며 이 세션의 수정과 무관하다. 수정은 원복했다.

## 2026-07-31 ST-E1A 조사 — node staleness는 검사하는데 actor generation staleness는 안 한다

먼저 시나리오의 성격을 바로잡는다. ST-E1A는 relocation이 아니라 **destroy + recreate**다.
actor를 파괴하고 같은 ID로 다시 만들어 새 `ObjectGeneration`(incarnation)을 얻은 뒤,
**낡은 binding**으로 request를 보내 `ActorLocationStale`이 오는지 본다. 실제로는 응답이
오지 않고 `Request timed out`이 난다.

런타임에서 `ActorLocationStale`을 만드는 지점들을 확인했다
(`ZLinkManagedMeshNode` 4400·4508·5005·5127 부근). 모두 다음 형태다.

```csharp
if (stateful.TargetNodeRid != _routingId
    || stateful.TargetNodeGeneration != _lifecycleGeneration)
    // ActorLocationStale
```

즉 **node 수준 staleness**만 본다. target node가 바뀌었거나 node lifecycle generation이
달라진 경우다. ST-E1A는 같은 node에서 actor의 `ObjectGeneration`만 바뀐 경우이므로 이
검사를 그대로 통과한다.

**(정정) 위 문단만으로 "actor generation staleness를 검사하지 않는다"고 적었던 것은
틀렸다.** `ProcessStateful`을 끝까지 읽으면 검사도 stale 응답도 존재한다.

```csharp
if (!TryGetActor(stateful.TargetActor, out var actor)
    || actor.AuthorityOwnerGeneration != stateful.AuthorityOwnerGeneration
    || stateful.OwnerLeaseGeneration != ...)
{
    // message follow를 먼저 시도하고
    if (messageFollowTarget?.TryFollow(...) == true) return;
    // 처리되지 않으면 stale로 응답한다
    if (request)
        Reply(RequestResult.Conflict, FrameworkErrorCode.ActorLocationStale, ...);
    return;
}
```

즉 4400 부근의 node 수준 검사와 별개로, actor 수준 staleness 판정이 이 지점에 있다.
따라서 "검사가 없어서 사라진다"는 설명은 성립하지 않는다.

남은 후보는 둘이다. 하나는 request가 `ProcessStateful`에 도달하기 전에 사라지는
경우다(`peer.Admitted`가 false이거나 앞쪽 protocol error 분기에서 `return`). 다른 하나는
`messageFollowTarget.TryFollow`가 `true`를 반환해 책임을 가져간 뒤 응답을 내지 않는
경우다. 후자라면 stale 응답 경로 자체가 실행되지 않으므로 timeout과 정확히 맞는다.
ST-E1A에는 relocation이 없어 follow 항목이 없을 텐데도 `TryFollow`가 true를 반환한다면
그것이 결함이다.

다음 확인은 이 두 갈래를 가르는 진단이다. `ProcessStateful` 진입 여부와 `TryFollow`
반환값을 찍으면 한 번의 실행으로 판별된다.

이 가설은 군집 전체를 설명할 여지가 있다. ST-E1·ST-E2·ST-F3도 bound session 경로의
timeout이거나 bound 관련 evidence 미관측이다.

다음 확인은 bound request가 actor를 찾는 지점에 진단을 넣어, 낡은 generation의 request가
어디서 사라지는지 특정하는 것이다. 이번 세션에서 여러 층에 심어 둔 진단과 같은 방식이면
한 번의 실행으로 판별된다.

## 2026-07-31 ST-E1A 측정 — 요청은 mesh wire 경로에 도달하지 않는다

앞 절의 두 후보를 가르는 진단을 넣고 실행했다. `ProcessStateful` 진입부의
`stateful_dropped`와 actor staleness 분기의 `actor_stale_path` 둘 다 **한 건도 찍히지
않았다.** 따라서 후보 둘 중 어느 쪽도 아니다. 낡은 binding으로 보낸 request는 애초에
node 간 mesh wire 경로(`ProcessStateful`)에 도달하지 않는다.

이유는 경로를 잘못 짚었기 때문이다. ST-E1A의 request는 외부 client가
`session.Request(...)`로 STREAM session에 보내는 것이므로, node 간 wire가 아니라 session
gateway 경로로 들어간다. 따라서 staleness 판정도 그쪽에서 일어나야 한다.

session 계층에는 별도의 stale 표현이 있다. `ZLinkSessionActorCoordinator`(391)와
`ZLinkSessionActor`(30)가 `ZLinkFrameworkException`을
`ZLinkFrameworkErrorKind.InvalidOperation`과 `"Actor '...' session binding is stale."`
메시지로 던진다. 반면 시나리오가 기대하는 것은 wire 계층 표현이다.

```csharp
stale?.Error.Code == ZlinkStreamErrorCode.RemoteError
&& stale.Error.Message.StartsWith("ActorLocationStale:", StringComparison.Ordinal)
```

즉 두 계층이 서로 다른 형태로 stale을 표현하며, 시나리오는 wire 형태를 기대한다.
다만 관측된 것은 **둘 중 어느 오류도 아닌 timeout**이므로, session 계층의 예외 경로도
실행되지 않았을 가능성이 크다.

다음 확인은 session gateway가 이 request를 어디까지 처리하는지다. 이번 진단이 경로를
잘못 짚었으므로, 다음에는 `ZLinkSessionActorCoordinator`의 stale 분기와 session gateway
진입부에 같은 방식으로 진단을 넣어 측정한다.

### 방법에 대한 기록

이번 회차에서 두 번 틀렸다. 처음에는 코드를 덜 읽고 "actor generation 검사가 없다"고
적었고, 그다음에는 검사를 찾은 뒤 경로를 mesh wire로 단정하고 진단을 그쪽에만 넣었다.
두 번 다 측정 전에 경로를 확정한 것이 원인이다. bound session처럼 계층이 여럿인
경로에서는 진단을 한 계층에만 넣지 말고 진입점부터 순서대로 넣어야 한 번에 갈린다.

## 2026-07-31 스펙 모순 해소 — completion은 durable cleanup을 기다리지 않는다

앞서 기록한 `15-spot-actor.ko.md`의 내부 모순을 사용자 판단으로 확정했다. 기준은
"이동이 이미 성공했으면 보고가 먼저이고, 정리는 따로 하면 된다"이다.

따라서 단계 목록(7·8)과 sequence diagram이 맞고 산문 문장이 틀린 것이었다. 산문을
고쳤다.

```
(before) Target joined callback, source leave notification과 durable cleanup이 끝나기
         전에 completion과 뒤 application payload를 실행하지 않는다.

(after)  Target joined callback과 source leave notification이 끝나기 전에 completion과
         뒤 application payload를 실행하지 않는다. 남은 source resource의 durable
         cleanup은 completion을 막지 않으며 그 뒤에 이어서 수행한다.
```

이제 세 서술이 모두 "joined → leave → completion → cleanup" 순서로 일치한다. 영문판
`15-spot-actor.md`는 존재하지 않아 동기화 대상이 없다.

### 남은 빈칸은 그대로다

이 판단은 completion과 **durable cleanup**의 순서를 정한 것이다. ST-C3가 걸려 있는
문제는 다른 것이다. completion은 여전히 **source leave notification** 뒤이고, 세 서술이
이 점에서는 원래부터 일치했다. application의 leave callback이 계속 실패할 때 어떻게
할지는 아직 규정되지 않았다.

## 2026-07-31 ST-E1A 원인 확정 — stale 응답이 있는 경로와 요청이 가는 경로가 다르다

진입점부터 순서대로 진단을 넣어 측정했다.

session gateway는 요청을 `ZLinkSessionActorCoordinator.RelayToActorAsync`로 넘긴다.
그 진입부에서 찍힌 값은 다음과 같다.

```
session_relay_entry actor=actor-new-incarnation-... context_live=True has_route=True
                    route_authority_gen=7 route_node_gen=6764754405549093013
```

`context_live=True`가 핵심이다. 그 지점의 guard는 **binding token**만 확인한다. actor를
파괴하고 같은 ID로 다시 만들어도 token은 그대로이므로, 낡은 incarnation을 향한 요청이
이 guard를 그대로 통과한다. route도 살아 있어 낡은 generation(7)을 들고 전달된다.

그다음이 결정적이다. 이 경로는 `ForwardToRemoteActorAsync`를 거치며
`ZLinkActorBoundSessionRelay`의 route 패킷으로 나간다. **node 간 stateful wire
경로(`ProcessStateful`)를 타지 않는다.** 실제로 그 함수에 심은 진단
(`stateful_dropped`, `actor_stale_path`)이 이번 실행에서 한 건도 찍히지 않았다.

그런데 시나리오가 기대하는 `ActorLocationStale` 응답은 바로 그
`ProcessStateful` 안에 있다.

```csharp
if (!TryGetActor(...) || actor.AuthorityOwnerGeneration != stateful.AuthorityOwnerGeneration ...)
{
    if (messageFollowTarget?.TryFollow(...) == true) return;
    if (request) Reply(RequestResult.Conflict, FrameworkErrorCode.ActorLocationStale, ...);
}
```

즉 **stale을 판정해 응답하는 코드와 이 요청이 실제로 가는 경로가 서로 만나지 않는다.**
bound session forward 경로에는 그에 상응하는 stale 판정이 없어 요청이 조용히 사라지고
호출자는 timeout을 본다. 시나리오가 기대하는 응답은 구조적으로 도달 불가능하다.

이것이 bound session 군집(ST-E1, ST-E1A, ST-E2, ST-F3)의 공통 원인일 가능성이 높다.
넷 모두 bound session 경로의 timeout이거나 관련 evidence 미관측이다.

수정 방향은 bound session forward 경로에도 actor generation staleness 판정을 두어
`ActorLocationStale`로 응답하게 하는 것이다. 다만 어느 계층에 둘지는
`ZLinkActorBoundSessionRelay` 수신측 구조를 더 본 뒤 정해야 한다.

### 방법 기록

이번 건은 경로를 세 번 잘못 짚은 뒤에야 잡혔다. 처음엔 검사 부재로 오판했고, 다음엔
mesh wire로 단정했고, 그다음엔 push relay(반대 방향)를 봤다. 세 번 다 측정 없이 경로를
확정한 탓이다. 진입점(`RelayActorRefAsync` → `RelayToActorAsync`)에 진단을 넣자 한 번에
갈렸다. 계층이 여럿인 경로는 반드시 진입점부터 측정한다.

## 2026-07-31 ST-E1A 결함 지점 확정 — bound 요청은 액터를 못 찾아도 응답하지 않는다

`ZLinkActorInboundPipeline`에서 액터를 찾지 못했을 때의 처리다.

```csharp
var actor = endpoint.ResolveActor(state);
if (actor is null)
{
    await ZLinkActorBoundSessionRelay.TryReplyMissingNoBindActorAsync(...);
    acknowledgeHandledFrame?.Invoke();
    return;
}
```

`TryReplyMissingNoBindActorAsync`는 첫 줄에서 이렇게 걸러 낸다.

```csharp
if (requestHeader.Kind != ZlinkStreamMessageKind.Request
    || requestHeader.RequestSeq is null
    || !IsNoBindRequest(requestId, flags))
    return false;
```

즉 **no-bind 요청일 때만** `NotFound`로 응답한다. ST-E1A처럼 session에 bound된 요청은
`false`가 반환되고, 그 반환값을 쓰지 않은 채 frame을 acknowledge하고 `return`한다.
**응답이 나가지 않으므로 호출자는 timeout을 본다.**

바로 위 분기와 대비하면 누락이 분명하다. "Actor is being destroyed" 경우에는 bound
여부와 무관하게 `NotFound` 응답을 보낸다. 즉 응답을 보내는 패턴은 이미 있고, **bound +
액터 없음** 조합에만 빠져 있다.

ST-E1A는 actor를 파괴하고 같은 ID로 재생성한 뒤 낡은 binding으로 요청을 보내는
시나리오다. session 계층의 guard는 binding token만 보므로 통과하고, route는 낡은
generation을 들고 전달되며, 도착지에서 `ResolveActor`가 실패해 이 분기에 들어온다.
그리고 조용히 사라진다.

### 수정 시 정해야 할 것

시나리오가 기대하는 형태는 `ZlinkStreamErrorCode.RemoteError`에 메시지가
`"ActorLocationStale:"`로 시작하는 것이다. 반면 이 지점의 기존 no-bind 응답은
`ZLinkFrameworkErrorKind.NotFound`에 `"Actor '...' is not available."`이다. 어느 형태로
응답할지는 spec을 기준으로 정해야 하며, 이 판정은 bound session 군집 4건
(ST-E1, ST-E1A, ST-E2, ST-F3)에 함께 영향을 준다. 시나리오 기대값을 그대로 계약으로
삼지 말고 spec에서 근거를 찾은 뒤 고치는 것이 맞다.

## 2026-07-31 ST-E1A 계약 근거 확보 — spec은 세 갈래를 규정하는데 .NET에는 하나뿐이다

시나리오 기대값을 계약으로 삼지 않고 spec에서 근거를 찾았다.
`server/languages/dotnet/interfaces/07-stream-session.ko.md`가 이렇게 규정한다.

> "Session binding은 `ActorRef.ActorId + ObjectGeneration`의 exact incarnation을 한 번
> 고정한다. ... **Mapping이 없으면 `ActorLocationStale`, current generation이 다르면
> `ActorGenerationStale`**, pre-commit seal 중이면 `ActorMoving`이다."

즉 spec은 세 갈래를 구분한다. ST-E1A는 actor를 파괴하고 같은 ID로 재생성한 뒤 낡은
binding으로 요청하는 경우이므로 **current generation이 다른 쪽**, 곧
`ActorGenerationStale`에 해당한다.

여기서 두 가지가 드러난다.

### 1. .NET에 `ActorGenerationStale`과 `ActorMoving`이 없다

`framework/languages/dotnet/src` 전체에서 두 이름의 출현이 **0건**이다.
`ZLinkFrameworkErrorKind`에도 없다. 즉 spec이 규정한 세 갈래 중 `ActorLocationStale`
하나만 구현되어 있다.

Node에는 있다. `packages/framework/src/contracts/Errors/ZLinkFrameworkException.ts`에
`ActorGenerationStale = 'actorGenerationStale'`(코드 27), `ActorMoving`(코드 28)이 정의되어
있다. 따라서 이것은 spec이 뒷받침하는 실제 .NET 구현 갭이다. 다른 언어에 있다는 사실만으로
계약을 만드는 경우가 아니라, **spec에 근거가 있고 .NET만 빠져 있는 경우**다.

### 2. 시나리오 기대값이 spec과 다르다

ST-E1A는 `"ActorLocationStale:"`로 시작하는 메시지를 기대한다. spec 기준으로 이 상황은
`ActorGenerationStale`이므로 시나리오 기대값이 틀렸다.

### 정리하면 세 겹이다

첫째, bound 요청이 액터를 찾지 못했을 때 아무 응답도 보내지 않는다
(`ZLinkActorInboundPipeline`의 `TryReplyMissingNoBindActorAsync`가 no-bind만 처리).
둘째, spec이 규정한 `ActorGenerationStale`·`ActorMoving`이 .NET에 없다.
셋째, ST-E1A가 기대하는 오류 종류가 spec과 다르다.

수정 순서는 둘째(오류 종류 추가) → 첫째(bound 경로에서 그 오류로 응답) → 셋째(시나리오
기대값을 spec에 맞춤)가 자연스럽다. 이 수정은 bound session 군집 4건에 함께 영향을 준다.

## 2026-07-31 ActorGenerationStale 갭의 실제 위치 — wire 프로토콜 스키마다

구현 위치를 확인하다 갭의 층이 예상과 달랐다.

먼저 .NET의 `ZLinkFrameworkErrorKind`와 Node의 것은 **서로 다른 체계**다. .NET은
`NotFound`부터 `InternalFailure`까지 13개(0~12)의 압축된 분류이고, Node는 32개 이상의
세분된 목록이다. 따라서 "Node에 27·28이 있으니 .NET에 같은 번호를 추가한다"는 접근은
성립하지 않는다.

spec이 말하는 세 이름의 실제 소재는 wire 오류 코드다. 이 코드들은
`framework/runtime/protocol/service-wire-v1.schema.json`에서 생성되며
(`generate-service-wire-assets.mjs`), 언어별 생성물이 따라 나온다. 스키마를 확인한 결과는
다음과 같다.

```
actorLocationStale  = 21   (있음)
SpotGenerationStale = 33   (있음)
SpotMoving          = 34   (있음)
ActorGenerationStale       (없음)
ActorMoving                (없음)
```

**Spot 쪽 세 갈래는 완비되어 있는데 Actor 쪽은 `actorLocationStale` 하나뿐이다.** spec
`07-stream-session.ko.md`가 Actor에 대해서도 세 갈래를 규정하므로, 이는 Spot 대비
누락으로 보인다.

### 따라서 이 수정은 프로토콜 변경이다

`ActorGenerationStale`과 `ActorMoving`을 추가하려면 wire schema에 코드를 넣고 전 언어
생성물을 다시 만들어야 한다. 한 언어의 내부 수정이 아니라 **언어 공통 프로토콜 표면
변경**이다. 이 세션에서 단독으로 밀어붙일 범위가 아니라고 판단해 멈췄다.

정리하면 ST-E1A를 제대로 고치는 데 필요한 것은 셋이다. 프로토콜에 두 오류 코드 추가,
`ZLinkActorInboundPipeline`의 bound 요청 무응답 수정, 그리고 ST-E1A 기대값을
`ActorGenerationStale`로 정정. 첫째가 나머지의 전제이므로 프로토콜 변경 승인이
선행되어야 한다.

한편 bound 요청에 **아무 응답도 보내지 않는 것** 자체는 오류 코드 선택과 무관한 결함이다.
어떤 코드로 응답할지와 별개로, 응답이 없어 호출자가 timeout까지 매달리는 동작은 그
자체로 고칠 값어치가 있다.

## 2026-07-31 ST-C3 미규정 해소 — leave notification은 one-way send다

앞서 미규정으로 남겨 둔 "실패하는 application leave callback"을 사용자 판단으로
확정했다. 설계는 다음과 같다. join이 성공(true)하면 요청한 쪽으로 **send로 통지**하여
`OnLeave`를 부르고, 이동한 쪽에서는 `OnJoined`를 부른다. 즉 leave 통지를 one-way send로
만들어 completion을 막지 않게 한다.

이로써 앞 절의 교착이 풀린다. 이동은 이미 성공했고 target도 정상인데 source의 leave
callback이 실패한다는 이유로 completion이 영원히 나오지 않던 문제가, leave가
completion을 막지 않게 되면서 사라진다.

`15-spot-actor.ko.md` 세 곳을 고쳤다.

산문(§277-279)은 이제 target joined callback만 completion을 막고, leave notification은
one-way send이며 그 실행이나 실패가 completion을 막지 않는다고 적는다. 단계 7은
"Target joined callback을 실행하고 source leave notification을 one-way send로 전달한다.
leave notification의 완료나 실패는 이 단계를 막지 않는다"로 바꿨다. sequence diagram의
해당 화살표도 `-)`(비동기)로 바꾸고 `(one-way send)`를 명시했다.

정리하면 순서는 이렇게 확정됐다.

```
target joined callback (completion을 막음)
  → Accepted completion
  → source leave notification (one-way send, 막지 않음)
  → durable cleanup (막지 않음)
```

joined만 completion을 막는 이유는 비대칭이 실제로 있기 때문이다. 보고를 받고 target에
접근했는데 membership이 아직 없으면 곤란하지만, source의 옛 membership 정리가 늦는 것은
아무도 곤란하게 하지 않는다.

### 남은 구현 작업

ST-C3의 네 하위 케이스 중 transfer-out은 이미 통과한다. source leave 실패 케이스는 이제
기대값이 정해졌으므로 시나리오를 그 기준으로 다시 쓸 수 있다. transfer-in 실패와 joined
실패 두 케이스도 같은 기준으로 함께 정리한다. 런타임이 leave notification을 실제로
one-way send로 보내는지도 확인해야 한다.

## 2026-07-31 (정정) ST-C1·ST-C3의 `!response.Accepted` 단언은 지우면 안 되는 것이었다

앞서 ST-C1과 ST-C3에서 `!response.Accepted` 단언을 "`.Defer()` 뒤 handler 반환값을 join
결과로 오인한 시나리오 결함"으로 보고 제거했다. **틀렸다.**

`Zlink.Framework.SampleRegressionTests`의
`SpotActorTransferSourceDownAssertionCannotBeCaughtAsTransportFailure`가 그 단언을
소스 텍스트로 고정하고 있다.

```csharp
Assert.Contains("if (response is not null)", scenario, StringComparison.Ordinal);
Assert.Contains("!response.Accepted", scenario, StringComparison.Ordinal);
```

즉 프로젝트는 이 단언을 의도적으로 동결했고, **`Accepted=true`가 돌아오는 것 자체를
결함으로 본다.** ST-C1의 이름이 "Source Down Before Commit"인 만큼, commit 전에 source가
죽었으면 join이 accepted로 보고되어서는 안 된다는 것이 계약이다.

따라서 진짜 문제는 시나리오가 아니라 **e2e server의 handler가 commit 전에
`Accepted=true`를 반환하는 것**일 가능성이 크다. `ActorJoinTargetUseCase.ExecuteAsync`는
`.Defer()` 직후 `commit_request` 증거를 남기고 `new JoinTargetRes(..., true, ...)`를
반환한다. `.Defer()`를 쓴 join의 결과는 completion callback으로 오는데, handler가 먼저
"수락됐다"고 답해 버리는 셈이다.

두 시나리오를 `git checkout`으로 원복했다. ST-C1은 다시 실패 상태가 되고, ST-C3도 앞서
통과했던 transfer-out 케이스가 되돌아간다. 이 회차에서 ST-C1을 "해결"로 적었던 것도
정정한다. 하네스의 `WaitUnavailableAsync` 수정(연결 실패를 kind 무관하게 인정)은 별개의
실제 결함 수정이므로 유지한다.

### 교훈

시나리오 단언을 지우기 전에 그 단언을 지키는 가드 테스트가 있는지 먼저 확인해야 한다.
이 저장소는 `SampleRegressionTests`에서 e2e 시나리오의 소스 텍스트를 직접 단언하는
방식으로 계약을 고정한다. 전체 솔루션 테스트를 돌리지 않았다면 이 회귀를 놓쳤을 것이다.

## 2026-07-31 (정정) .NET public error kind 추가는 스펙 위반이었다

`ZLinkFrameworkErrorKind`에 `ActorLocationStale`·`ActorGenerationStale`·`ActorMoving`을
추가했다가 원복했다. `10-monitoring-errors.ko.md`가 이를 명시적으로 금지한다.

> "Generation stale, object moving, worker queue 상태와 relocation 처리 단계는
> Framework가 retry·recovery를 판정할 때 사용하는 **내부 원인**이다. Application이 다른
> 대응을 선택할 수 없으면 **별도 public kind로 구분하지 않는다.**"

즉 .NET의 13값 압축 enum은 의도된 설계이고, 계약 테스트 3건
(`Default_retry_advice_...`, `Framework_exception_contract_matches_the_frozen_surface`,
`Fixed_spec_snapshot_...`)이 그 설계를 지키고 있었다. 테스트가 제 역할을 한 것이다.

### 두 스펙을 함께 읽어야 했다

`07-stream-session.ko.md`는 session binding 실패를 `ActorLocationStale`,
`ActorGenerationStale`, `ActorMoving`으로 구분한다고 적는다. 이를 public error kind
목록으로 읽은 것이 오독이었다. `10-monitoring-errors.ko.md`가 같은 개념을 public kind로
올리지 않는다고 못 박고 있으므로, 앞 문서가 말하는 세 이름은 **public kind가 아닌 다른
층위**(wire error code 또는 진단 문자열)의 구분으로 읽어야 한다.

### 현재 상태

wire schema의 `actorGenerationStale`(36)·`actorMoving`(37) 추가는 유지한다. wire 코드는
public application 계약이 아니라 node 간 프로토콜이며, spec이 금지하는 것은 public kind
승격이다. 다만 이 코드들을 실제로 사용하는 곳은 아직 없다.

`ZLinkActorInboundPipeline`의 수정(bound 요청이 액터를 못 찾을 때 무응답 대신 응답)은
유지하되 kind를 기존 `InvalidOperation`으로 되돌렸다. 다만 측정 결과 ST-E1A의 프레임은
이 지점에 도달조차 하지 않으므로, 이 수정이 ST-E1A를 고치지는 않는다.

### 남은 질문

ST-E1A가 기대하는 `"ActorLocationStale:"` 문자열이 어느 층에서 만들어지는지 아직
모른다. public kind가 아니라면 wire error code 이름이 메시지에 실려 오는 경로가 있을
것이다. 그 경로를 찾는 것이 다음 단계다.

## 2026-07-31 `ActorLocationStale` 문자열의 출처 — 스펙과 테스트가 정면으로 충돌한다

ST-E1A가 기대하는 문자열의 출처를 추적한 결과, 이 문제가 ST-E1A 하나에 국한되지 않는다.

`ToActorMessaging` e2e의 `TaB2StaleActorReferenceScenario`가 같은 것을 기대한다.

```csharp
await context.AssertCachedFailureAsync("TA-B2-stale-request", actorId, "ActorLocationStale");
```

그 helper는 `response.ErrorKind == expectedKind`를 비교하고, server는
`error.Kind.ToString()`으로 그 값을 만든다. 즉 **`ZLinkFrameworkErrorKind`에
`ActorLocationStale`이라는 값이 있어야만 통과하는 단언**이다.

그런데 `10-monitoring-errors.ko.md`는 그 값을 public kind로 올리지 않는다고 명시한다.
같은 파일의 enum 정의도 `NotFound`부터 `InternalFailure`까지 13개뿐이다. 계약 테스트
3건이 이 13개 목록을 동결하고 있다.

따라서 다음 셋이 동시에 성립할 수 없다.

1. `10-monitoring-errors.ko.md`: generation stale·object moving을 public kind로 올리지 않는다
2. `07-stream-session.ko.md`: session binding 실패를 `ActorLocationStale`/
   `ActorGenerationStale`/`ActorMoving`로 구분한다
3. e2e 시나리오 둘(ST-E1A, TA-B2): public kind 이름으로 `ActorLocationStale`을 기대한다

2와 3은 그 이름들이 public 표면에 있어야 성립하고, 1은 없어야 한다고 말한다.

### 이건 내가 판단할 문제가 아니다

이 세션에서 두 번 틀린 방식이 정확히 이것이다. 한 문서만 보고 다른 문서를 안 봤고,
시나리오 기대값을 계약으로 삼았다가 가드 테스트에 걸렸다. 이번에는 셋 중 무엇이
정본인지 스펙 소유자가 정해야 한다.

선택지는 셋이다. `10-monitoring-errors`의 금지 문장을 좁혀 session binding 실패만
public kind로 허용하거나, `07-stream-session`의 세 이름을 public kind가 아닌 진단
문자열로 다시 쓰고 시나리오 둘을 그에 맞추거나, 세 이름을 wire 층에만 두고 application에는
기존 kind(`InvalidOperation` 등)로 노출하며 시나리오를 고치는 것이다.

어느 쪽이든 ST-E1A와 TA-B2가 함께 움직인다. 결정 전에는 구현을 건드리지 않는다.

## 2026-07-31 (정정) 공통 spec 20이 이 동작을 이미 규정하고 있다

앞 절에서 이 문제를 "스펙 셋이 충돌하니 소유자가 정해야 한다"로 정리했는데, 언어별
spec만 보고 **공통 spec을 확인하지 않은** 탓이다. `20-session-actor-dispatch.ko.md`가
동작 자체를 직접 규정한다.

> "저장한 route가 더 이상 유효하지 않으면 active Message Follow route로 정확히 한 번
> 전달하거나 **typed stale error로 끝낸다.** Location Store에서 새 `ActorRef`를 찾아 같은
> message를 다른 owner에게 자동으로 다시 보내지 않는다."

같은 문서가 binding이 `ObjectGeneration`을 보관하는 이유도 명시한다.

> "| `ActorId`, `ObjectGeneration` | **같은 ID로 다시 만들어진 다른 Actor에게 보내지
> 않기 위해** 사용한다. |"

ST-E1A가 정확히 이 경우다. actor를 파괴하고 같은 ID로 재생성했으므로 저장된 route는
더 이상 유효하지 않다. relocation이 없어 message follow route도 없다. 따라서 스펙이
요구하는 결과는 **typed stale error**다.

관측된 동작은 응답 없는 timeout이다. **이것은 공통 spec 위반이며, 시나리오 기대값과
무관하게 런타임 결함이다.** `ZLinkActorInboundPipeline`에서 bound 요청에 응답하도록 한
수정은 이 조항이 근거다.

### 그래서 앞 절의 "3자 충돌" 정리를 좁힌다

충돌은 **동작**이 아니라 **오류 이름**에만 있다. 동작은 공통 spec 20이 확정한다.
typed stale error로 끝내야 하며 조용히 버리면 안 된다.

남은 것은 그 typed error를 application에 어떤 이름으로 노출하느냐다. 여기서
`07-stream-session.ko.md`의 세 이름과 `10-monitoring-errors.ko.md`의 public kind 금지가
부딪힌다. 이 부분만 결정이 필요하고, **응답을 보내야 한다는 요구는 이미 결정되어 있다.**

### 다음 확인

ST-E1A의 프레임이 `ZLinkActorInboundPipeline`에 도달하지 않는다는 측정 결과가 남아
있다. 공통 spec이 typed stale error를 요구하므로, 프레임이 어디서 사라지는지 찾아 그
지점에서 stale error를 내보내야 한다. 이름 결정과 무관하게 착수할 수 있다.

## 2026-07-31 ST-E1A 프레임 소실 구간 확정 — session-a에서 나가고 actor-a에 안 들어온다

경로 세 지점을 동시에 측정했다.

```
session_relay_entry actor=actor-new-incarnation-... context_live=True has_route=True
                    route_authority_gen=5
forward_part actor=actor-new-incarnation-... target_node=actor-a-457ded43-...
             local_node=session-a-94cc87df-... has_relay=True has_more=True
forward_part actor=actor-new-incarnation-... target_node=actor-a-457ded43-...
             local_node=session-a-94cc87df-... has_relay=True has_more=False
(inbound_resolve  — 0건)
```

`forward_part`가 header part와 body part로 두 번 찍히므로 프레임은 **session-a에서
actor-a로 정상 송신된다.** 그런데 actor-a의 `ZLinkActorInboundPipeline`
(`inbound_resolve`)에는 한 건도 도달하지 않는다.

따라서 소실 구간은 **session-a 송신과 actor-a inbound pipeline 사이**로 좁혀진다.
transport에서 버려지거나, actor-a 수신측이 pipeline 이전 단계에서 조용히 버린다.

공통 spec 20이 "저장한 route가 더 이상 유효하지 않으면 ... typed stale error로 끝낸다"고
요구하므로, 그 버리는 지점을 찾아 stale error를 내보내는 것이 수정 방향이다. 오류
이름 논쟁과 무관하게 진행할 수 있다.

다음 확인은 actor-a 수신측 진입점이다. `ForwardPart`의 relay가 어떤 packet으로 나가는지
따라가 그 수신 handler에 같은 방식으로 진단을 넣으면 한 번에 갈린다.

## 2026-07-31 ST-E1A 근본원인 확정 — one-way 핸들러 안에서 던진 예외가 삼켜진다

프레임이 사라지는 지점을 로그로 확정했다.

```
fail: zlink.framework.dispatch[0] phase=error surface=RouteMeshChannel kind=Send
      packet=$zlink.actor.frame-relay.v1 ...
```

`ZLinkFrameworkRuntimeActors.DispatchRemoteActorFrameAsync`가 bound session fence를
검사하는 큰 조건에서 예외를 던진다.

```csharp
else if (!hasBoundSessionFence
         || boundSessionFence.ActorGeneration != actorGeneration
         || session.ObjectGeneration != actorGeneration
         || ... )
{
    throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.Unavailable,
        $"Actor '{actorId}' session relay authority identity is stale.");
}
```

ST-E1A에서는 actor를 파괴하고 재생성했으므로 `session.ObjectGeneration`(새 incarnation)과
프레임이 실어 온 `actorGeneration`(낡은 binding의 값)이 어긋나 이 조건이 성립한다.

문제는 던지는 **위치**다. 이 함수는 `ZLinkRemoteActorFrameRelayHandler.HandleAsync`에서
호출되고, 그 handler는 `IZLinkRouteSendHandler<...>` 즉 **one-way send handler**다.
따라서 예외는 route dispatch의 error sink로 가서 로그 한 줄로 끝나고, 원래 요청자에게는
아무것도 돌아가지 않는다. 호출자는 deadline까지 기다리다 timeout을 본다.

앞선 측정들이 모두 이것으로 설명된다. `forward_part`는 정상 송신되고
(`remote_frame_dispatch`도 찍힌다), 그 직후 이 조건에서 던져지므로
`inbound_resolve`(더 뒤의 pipeline)에는 도달하지 않는다.

### 이것은 공통 spec 위반이다

`20-session-actor-dispatch.ko.md`는 "저장한 route가 더 이상 유효하지 않으면 active
Message Follow route로 정확히 한 번 전달하거나 **typed stale error로 끝낸다**"고 요구한다.
현재 동작은 typed stale error를 요청자에게 전달하지 않고 수신측에서 로그만 남긴다.

### 수정 방향

이 지점의 프레임은 `replyRequestId`, `replyFlags`, `replyCapability`를 이미 싣고 있다.
따라서 request인 경우 예외를 던지는 대신(또는 던지기 전에) 그 reply 경로로 typed stale
error를 보내면 된다. 같은 파일 안에 `ZLinkActorBoundSessionRelay.ReplyStaleActorAsync`가
이미 있으므로 재사용할 수 있다.

오류 **이름** 결정(07-stream-session vs 10-monitoring-errors)과는 독립이다. 어떤 kind를
싣든 "응답을 보낸다"는 요구는 공통 spec이 이미 확정했다. 다만 이름이 정해져야 ST-E1A와
TA-B2의 단언이 통과하므로, 시나리오 통과까지 가려면 두 결정이 모두 필요하다.

## 2026-07-31 ST-E1A 동작 수정 완료 — spec 20 위반 해소, 이름만 남았다

`DispatchRemoteActorFrameAsync`의 bound session fence 분기에서, 예외를 던지기 전에
`ZLinkActorBoundSessionRelay.ReplyStaleActorAsync`로 요청자에게 typed stale error를
보내도록 고쳤다. 프레임이 실어 온 `replyRequestId`·`replyFlags`·`replyCapability`를
그대로 쓴다.

결과가 바뀌었다. 수정 전에는 호출자가 deadline까지 기다리다
`Request timed out`을 받았고, 지금은 즉시 다음을 받는다.

```
Unavailable: Actor 'actor-new-incarnation-...' session relay authority identity is stale.
```

즉 `20-session-actor-dispatch.ko.md`가 요구하는 "typed stale error로 끝낸다"가
충족된다. 조용한 소실은 사라졌다.

### 이름 문제만 남았다

이 결과가 앞선 추정 하나를 확정해 준다. 오류 메시지의 접두사는
`ZLinkFrameworkErrorKind.ToString()`이다(`Unavailable:`). 따라서 ST-E1A와 TA-B2가
기대하는 `ActorLocationStale:`·`ActorGenerationStale:`은 **public kind 값이어야만**
나올 수 있고, 그것을 `10-monitoring-errors.ko.md`가 금지한다.

동작은 고쳤으므로 두 시나리오는 이제 timeout이 아니라 "이름이 다르다"로 실패한다.
ST-E1A의 기대값 수정은 원복했다. 이름이 정해지기 전에 시나리오를 한쪽으로 몰아 두면
결정이 반대로 났을 때 다시 되돌려야 하고, 이 세션에서 그런 성급한 수정으로 두 번
틀렸기 때문이다.

### 회귀 검증과 ST-D1 flake

stale reply 수정 뒤 회귀를 확인했다. ST-A1, ST-B1, ST-F4는 통과한다.

ST-D1이 한 번 실패했다가(`success_reply` 미관측) 연속 두 번 재실행에서 통과했다. 즉
회귀가 아니라 **flake**다. ST-D1은 `delay-joined` 게이트로 타이밍을 잡는 시나리오이므로
간헐 실패 여지가 있다. 3회 중 1회 실패는 무시할 수준이 아니므로 별도 항목으로 남긴다.
게이트 해제와 completion 전달 사이의 경합이 의심되며, 재현되면 그때 추적한다.

ST-E2와 ST-F3는 여전히 실패한다. ST-E2는 아직 `Request timed out`이므로 이번에 고친
경로(`DispatchRemoteActorFrameAsync`의 bound session fence)와는 다른 지점에서 사라진다.
bound session 군집이 한 원인일 것이라는 앞선 가설은 절반만 맞았다. ST-E1A는 이
경로였지만 ST-E2·ST-F3는 아니다.

## 2026-07-31 ST-E1A 해결 — 새 이름을 만들지 않고 기존 kind로 정리했다

오류 이름 문제를 사용자 승인으로 확정했다. **새 public kind를 만들지 않고 기존 13개 중
맞는 것을 고른다.** `NotFound`다.

근거는 `10-monitoring-errors.ko.md`의 kind 표다.

| kind | 의미 | retry advice |
|---|---|---|
| `Unavailable` | target·route·Store·worker가 **현재** 처리할 수 없다 | `RetryAfterBackoff` |
| `NotFound` | 요청한 Actor·Spot·route·target이 **존재하는지** 확인한다 | `DoNotRetry` |

파괴되고 다른 incarnation으로 대체된 actor는 다시 시도해도 돌아오지 않는다. 따라서
직전 수정에서 쓴 `Unavailable`(재시도 권장)은 부정확했고 `NotFound`(재시도 말 것)가
맞다. retry advice가 의미와 일치한다.

같은 문서가 "Application이 다른 대응을 선택할 수 없으면 별도 public kind로 구분하지
않는다"고 명시하므로, "actor가 아예 없음"과 "낡은 incarnation을 가리킴"을 하나의
`NotFound`로 합치는 것이 이 문서의 의도다. 진단이 필요하면 메시지 본문
(`session relay authority identity is stale`)이 구분해 준다.

### 적용 결과

`DispatchRemoteActorFrameAsync`의 stale 응답 kind를 `NotFound`로 바꾸고, ST-E1A와
`ToActorMessaging`의 TA-B2 기대값을 `NotFound`로 정정했다. **ST-E1A가 통과한다.**

`07-stream-session.ko.md`가 말하는 `ActorLocationStale`·`ActorGenerationStale`·
`ActorMoving`은 public kind가 아니라 다른 층위의 구분으로 남는다. 이 결정으로 public
표면에는 새 값이 하나도 추가되지 않았다.

앞서 wire schema에 넣었던 `actorGenerationStale`(36)·`actorMoving`(37)은 이 안에서
쓰이지 않으므로 **원복했다.** 사용처 없는 프로토콜 코드를 남기지 않는다.

## 2026-07-31 ST-E2 원인 — bound session actor의 relocation이 deadline을 넘긴다

ST-E2는 ST-E1A와 다른 문제다. 증거가 명확하다.

actor-a(source):
```
ST-E2|actor-bound-session-rebind-...|commit_request|spot-...
ST-E2|actor-bound-session-rebind-...|join_failed|DeadlineExceeded
```

actor-b(target):
```
phase=received packet=__zlink.actor.join_spot.admission        (21:09:39)
phase=received packet=__zlink.actor.join_spot.admission_abort  (21:09:42)
pending_admission_expired actor=actor-bound-session-rebind-...
```

즉 **relocation join이 DeadlineExceeded로 실패**하고 target의 admission이 만료된다.
ST-E2는 bound session이 붙은 actor를 이동시키는 시나리오이므로, spec 15 단계 9의 session
binding route 갱신(`command 44·45`)이 완료되지 않아 join이 deadline을 넘는 것으로 보인다.

### 실패가 늦게 드러나는 이유

시나리오는 `join.Accepted`를 확인하고 통과시킨 뒤 다음 단계로 넘어간다. 실제로는 join이
실패했는데 `.Defer()` 뒤 handler가 이미 `Accepted=true`를 반환했기 때문에 그 단언이
통과한다. 그래서 실패가 line 26(join)이 아니라 line 29(새 node에 bind)에서
`Request timed out`으로 나타난다.

이것은 앞서 ST-C1·ST-C3에서 본 것과 **같은 구조**다. `.Defer()`를 쓴 join에서 handler
반환값이 join 결과보다 먼저 나가고, 그 값이 `Accepted=true`라서 실제 실패를 가린다.
ST-C1의 가드 테스트가 `!response.Accepted`를 동결한 이유도 이 때문으로 보인다.

### 정리

ST-E2·ST-F3는 bound session actor의 relocation 자체가 완료되지 않는 문제이고,
ST-C1·ST-C3·ST-E2에 공통으로 걸린 것은 조기 `Accepted` 반환이다. 두 축을 분리해
추적해야 한다. bound session relocation이 왜 deadline을 넘는지가 먼저다.

### ST-E2 추가 측정 — seal 이전, admission 결정 이전에 멈춘다

`SealBoundSessionRouteAsync` 진입부에 진단(`bound_seal_begin`)을 넣고 실행했다.
**한 건도 찍히지 않는다.** 즉 relocation은 bound session route seal 단계에 도달하기 전에
deadline을 넘긴다. 앞 절에서 "command 44·45 갱신이 늦어지는 것으로 보인다"고 적은 추정은
틀렸다.

같은 실행에서 target(actor-b)의 `admission_decision` 진단도 찍히지 않는다. actor-b는
`__zlink.actor.join_spot.admission` 패킷을 **수신했는데**(flow 로그에 있음) 앱 admission
결정 지점까지 가지 못한다.

정리하면 멈추는 구간은 **target이 admission 요청을 수신한 뒤 admission 결정에 이르기
전**이다. ST-D1에서 고쳤던 구간과 같은 위치이지만 그때와 달리 오류가 아니라 지연이다.
3초 뒤 source가 abort를 보내고 target은 `pending_admission_expired`를 남긴다.

bound session이 붙은 actor라는 점이 유일한 차이이므로, admission 처리 경로에서 bound
session 상태를 확인하는 지점이 블로킹되는지 확인하는 것이 다음 단계다.

### ST-E2 구간 재확정 — target은 수락하는데 source가 그 응답을 못 받는다

admission 수신 진입부에 진단(`admit_entry`)을 추가하고 다시 측정했다. 앞 절에서 "target이
admission 결정에 이르지 못한다"고 적었는데 **그것도 틀렸다.** 이번 실행에서는 둘 다 찍힌다.

```
admit_entry        actor=actor-bound-session-rebind-... draining=False
admission_decision actor=actor-bound-session-rebind-... accepted=True user_spot=True
```

즉 target은 admission을 받아 앱 handler까지 가고 **수락한다.** 그런데 source는 여전히
`join_failed|DeadlineExceeded`로 끝나고, `bound_seal_begin`은 찍히지 않는다.

따라서 멈추는 구간은 **target의 수락 결정과 source의 다음 단계(seal) 사이**다. source가
admission reply를 받지 못해 deadline까지 기다리는 것으로 보인다. 거부 경로
(`source_rejected`)도 찍히지 않으므로 reply 자체가 도달하지 않는다.

형태가 ST-E1A와 닮았다. 그쪽도 수신측이 처리를 마쳤는데 응답이 요청자에게 돌아가지
않았다. 다만 ST-E1A는 one-way handler에서 예외가 삼켜진 경우였고, 이쪽은 정상 수락
경로이므로 원인은 다르다.

다음 확인은 target이 admission reply를 실제로 송신하는지, 송신한다면 source가 어디서
그것을 기다리는지다. `admission_decision` 직후부터 reply 송신까지 사이에 진단을 넣으면
갈린다.

### 이 회차의 측정 원칙 위반 기록

이번 건에서 추정이 두 번 틀렸다. 처음에는 "command 44·45 갱신이 늦다"고 적었으나 seal
단계에 도달조차 하지 않았고, 다음에는 "target이 admission 결정에 이르지 못한다"고
적었으나 결정까지 정상 도달했다. 두 번 다 **한 번의 실행에서 특정 진단이 없는 것을 보고
구조를 단정**한 결과다. 진단이 없다는 것은 그 지점에 도달하지 않았다는 뜻일 뿐,
그 앞이 어디까지 갔는지는 말해 주지 않는다. 구간을 좁힐 때는 양 끝에 진단을 함께 넣어야
한다.

## 2026-07-31 ST-E2 근본원인 위치 확정 — 원격 session owner로의 route seal이 응답하지 않는다

양 끝에 진단을 넣어 구간을 끝까지 좁혔다. admission 왕복은 완전히 성공한다.

```
admit_request_sent  → admit_entry → admission_decision(accepted=True)
                    → admit_reply_built → admit_reply_received
preflight_done      has_bound_session=True
bound_seal_begin    session_node=session-b-... local=False
(그 뒤 진행 없음)
deferred_join_failed  DeadlineExceeded
```

즉 relocation은 bound session route seal 단계까지 정상 도달하고, 그 seal이 **원격**
session owner(session-b)를 향한다(`local=False`). 그 왕복이 끝나지 않아 join 전체가
deadline을 넘긴다.

따라서 ST-E2의 결함은 `SealBoundSessionRouteAsync`의 **remote 분기**에 있다. 같은 함수의
local 분기는 이 시나리오에서 실행되지 않는다.

### 진단 배치에서 같은 실수를 세 번 했다

`bound_seal_begin`을 처음에는 local 분기 **안**에 넣었다. ST-E2는 remote 분기를 타므로
진단이 찍히지 않았고, 그것을 "seal 단계에 도달하지 못한다"로 읽어 두 회차를 잘못
좁혔다. 진단을 분기 앞으로 옮기자마자 실제로는 도달하고 있었음이 드러났다.

이 세션에서 같은 유형의 오독이 세 번 있었다. 진단이 없다는 사실은 세 가지를 뜻할 수
있다. 그 지점에 도달하지 않았거나, 다른 분기를 탔거나, 조건문에 걸려 건너뛰었다.
**분기 안에 진단을 넣으면 그 셋을 구분할 수 없다.** 조건 분기를 측정할 때는 분기 앞에
조건값과 함께 찍어야 한다.

### 다음 단계

`SealBoundSessionRouteAsync`의 remote 분기가 무엇을 보내고 session-b가 그것을 어떻게
처리하는지 본다. 이번에도 양 끝(송신 직전, 수신측 진입, 응답 송신)에 함께 넣는다.
ST-F3도 bound session 경로이므로 같은 원인일 가능성이 있다.

## 2026-07-31 ST-E2 결함 지점 확정 — 원격 route seal이 응답하지 않는다

송신측과 수신측에 진단을 나눠 넣어 확정했다. 프로세스별로 보면 이렇다.

actor-a(source):
```
bound_seal_begin      session_node=session-b-... local=False
route_control_sent    target=session-b-... type=ZLinkSessionRouteSealRequest
route_control_sent    target=session-b-... type=ZLinkSessionRouteAbortRequest
```

session-b(session owner):
```
route_seal_received   actor=actor-bound-session-rebind-...
```

즉 seal 요청은 session-b에 **정상 도달하고 처리에 들어간다.** 그런데 actor-a는 응답을
받지 못해 deadline이 지난 뒤 abort를 보낸다. 그 결과 join 전체가 `DeadlineExceeded`가
된다.

따라서 결함은 `ZLinkSessionActorBindingTable.SealRouteAsync` 안에 있다. 이 함수는
`Task? drain`을 두고 `_entries` lock 안에서 상태를 정리한 뒤 그 drain을 기다리는 구조다.
그 대기가 끝나지 않아 응답이 나가지 않는 것으로 보인다.

ST-E2는 relocation 직전에 bound push를 한 번 주고받는다(`before-rebind-transfer`).
in-flight session frame이 있는 상태에서 seal이 그 drain을 기다리므로, 그 drain이 완료되지
않는 조건이 있는지가 다음 확인 지점이다.

ST-F3도 bound session 경로이므로 같은 원인일 가능성이 있다. 이 지점을 고치면 두 개가
함께 움직일 수 있다.

## 2026-07-31 ST-E2 결함 확정 — seal이 엉뚱한 session owner로 간다

`SealRouteAsync` 안쪽에 진단을 더 넣어 확정했다. session-b에서 `route_seal_received`는
찍히는데 그 다음 줄의 `route_seal_drain`은 찍히지 않는다. 즉 함수 진입 직후의 guard에서
조기 return한다. 첫 guard가 `!_entries.TryGetValue(key, out var entry)`이므로 **그 노드에
해당 binding 자체가 없다.**

이유는 시나리오를 보면 분명하다.

```csharp
await using var oldSession = await context.ConnectAndBindAsync(
    context.Options.NodeAStreamEndpoint, "ST-E2", sourceRef);
```

binding은 **NodeA의 stream endpoint**, 즉 **session-a**에 만들어진다. 그런데 source가
보내는 seal의 대상은 session-b다.

```
bound_seal_begin  session_node=session-b-...  local=False
route_control_sent target=session-b-...       type=ZLinkSessionRouteSealRequest
```

**seal이 binding이 없는 노드로 간다.** 그 노드는 첫 guard에서 `Acknowledged=false`를
반환하지만, source는 그 응답을 받지 못하고 deadline까지 기다린 뒤 abort를 보낸다.
그래서 join이 `DeadlineExceeded`로 끝난다.

즉 결함은 둘이 겹쳐 있다. 하나는 **source가 bound session의 owner node를 잘못
계산하는 것**이고, 다른 하나는 **그 조기 return의 응답이 요청자에게 돌아가지 않는
것**이다. 후자는 ST-E1A에서 고친 것과 같은 유형(수신측이 처리를 마쳤는데 응답이 유실)이다.

`SessionNodeRid`가 어디서 session-b로 채워지는지가 다음 확인 지점이다. preflight에는
`boundSession.SessionNodeRid is null`이면 `actorRef.NodeRid`로 채우는 경로가 있는데,
그 값은 actor-a나 actor-b이지 session-b가 아니므로 다른 경로에서 온 값이다.

### 전제 검증 — NodeAStreamEndpoint는 session-a가 맞다

위 결론이 "binding은 session-a에 있는데 seal은 session-b로 간다"에 의존하므로 endpoint
매핑을 직접 확인했다. `run_e2e.sh`에서 다음과 같다.

```
SESSION_A_STREAM="tcp://127.0.0.1:$SESSION_A_STREAM_PORT"
--node-a-stream-endpoint "$SESSION_A_STREAM"
```

즉 `context.Options.NodeAStreamEndpoint`는 session-a의 stream port다. ST-E2의
`oldSession`은 그 endpoint로 bind하므로 binding은 session-a에 있다. 반면 seal은
`session_node=session-b-...`로 나간다. 불일치가 실재한다.

`ZLinkActorBoundSession`은 `sessionNodeRid`를 생성 인자로 받으므로, 그 값을 넘기는
호출부가 다음 확인 지점이다. bind 시점에 session owner node를 기록하는 경로와,
relocation preflight에서 그 값을 다시 계산하는 경로 중 어디가 session-b를 넣는지 본다.

### bind 시점과 seal 시점의 session owner가 다르다 (측정 확인)

`BindRemoteBoundSessionRouteAsync`에 진단을 넣어 bind 시점 값을 찍었다. 최초 bind는
올바른 노드를 기록한다.

```
bind_session      actor=... session_node=session-a-761c3c42-...   ← oldSession, 정상
bound_seal_begin  actor=... session_node=session-b-692cfee1-...   ← seal은 다른 노드로
```

즉 binding은 session-a에 만들어지는데 relocation의 route seal은 session-b로 나간다.
session-b에는 그 binding entry가 없으므로 `SealRouteAsync`의 첫 guard에서 조기
return하고, 그 응답이 요청자에게 돌아가지 않아 join이 `DeadlineExceeded`로 끝난다.

같은 실행에서 `bind_session`이 수백 번 반복되는데, 이는 join 실패 뒤 시나리오 line 29의
`ConnectAndBindAsync(NodeBStreamEndpoint)`가 timeout까지 재시도하는 것이다. 결함의
증상이지 원인이 아니다.

다음은 relocation preflight가 `boundSession.SessionNodeRid`를 어떻게 얻는지다. bind가
기록한 session-a가 어디서 session-b로 바뀌는지 그 사이 경로를 본다.

## 2026-07-31 ST-E2 근본원인 — 조기 Accepted가 relocation 중에 재bind를 허용한다

actor-a 로그를 줄 번호 순으로 읽어 사건 순서를 확정했다.

```
20  bind_session      session_node=session-a-...      ← oldSession bind (정상)
    BoundPushReq  received/replied                    ← before-rebind push
    JoinTargetReq received/replied                    ← join 요청 처리, 핸들러가 즉시 반환
    resolve_spot_row / project_user_spot
    admit_request_sent                                ← relocation admission 송신
32  bind_session      session_node=session-b-...      ← relocation 진행 중에 재bind
36  bound_seal_begin  session_node=session-b-...      ← seal이 새 binding을 따라감
```

즉 **relocation이 아직 진행 중인데 actor의 bound session이 session-b로 다시 묶인다.**
그 결과 뒤이은 route seal이 session-a가 아니라 session-b를 향하고, session-b에는 그
fence에 맞는 entry가 없어 조기 return하며, 그 응답이 돌아가지 않아 join이
`DeadlineExceeded`로 끝난다.

왜 relocation 도중에 재bind가 일어나는가. 시나리오는 join을 await한 뒤에야 새 session에
bind한다.

```csharp
var join = await context.JoinAsync(context.NodeA, actorId, ...);   // line 26
ZlinkStreamAssert.Ensure(join.Accepted, ...);
var targetRef = await context.GetActorRefAsync(context.NodeB, actorId);
await using var newSession = await context.ConnectAndBindAsync(NodeBStreamEndpoint, ...); // line 29
```

문제는 line 26이 **relocation이 끝나기 전에 반환된다**는 것이다. `.Defer()` 뒤 handler가
`Accepted=true`를 먼저 돌려주므로 `join.Accepted` 단언이 통과하고, 시나리오는 아직
이동 중인 actor에 대해 line 29의 재bind를 실행한다.

### 세 시나리오가 한 원인으로 모인다

ST-C1, ST-C3, ST-E2가 모두 이 조기 `Accepted`에 걸려 있다. ST-C1은 source가 죽었는데
accepted를 받고, ST-C3은 transfer-out이 실패했는데 accepted를 받으며, ST-E2는 relocation이
끝나기 전에 다음 단계로 진행해 스스로를 깨뜨린다.

`SampleRegressionTests`가 ST-C1의 `!response.Accepted` 단언을 소스 텍스트로 동결해 둔
이유도 이것이다. 이 단언은 조기 accepted를 **회귀로 잡기 위한 장치**였고, 이 세션 초반에
그것을 "시나리오의 혼동"으로 보고 제거했던 판단은 틀렸다(이미 원복).

### 다음

`.Defer()`를 쓴 join에서 handler 반환값이 join 결과보다 먼저 나가는 것이 계약상 맞는지
정해야 한다. 스펙 15는 "Join 결과는 completion callback으로 전달한다"고 하므로, handler의
동기 반환값이 `Accepted=true`를 주장하는 것은 e2e server의 `ActorJoinTargetUseCase`
설계 문제일 가능성이 크다. 런타임이 아니라 e2e server 쪽 수정으로 세 시나리오가 함께
움직일 수 있다.

## 2026-07-31 조기 Accepted의 정체와 통합 수정안

스펙이 이 문제를 직접 규정한다. `15-spot-actor.ko.md` §183:

> "Actor Join call은 execution mode와 관계없이 동기 `Defer()`만 제공한다. 현재
> handler에서는 **intent와 barrier registration만 완료하고 handler의 마지막
> continuation이 정상적으로 끝난 뒤 Join을 실행한다.**"

즉 handler는 join이 **실행되기도 전에** 반환된다. 설계상 결과를 알 수 없다. 결과는
같은 문서 §275대로 completion callback으로 온다.

그런데 e2e server의 `ActorJoinTargetUseCase`는 `.Defer()` 직후
`new JoinTargetRes(..., true, ...)`를 반환하고, 하네스의 `JoinAsync`는
`JoinRawAsync`의 얇은 래퍼여서 그 값을 그대로 시나리오에 준다.

```csharp
public async Task<JoinTargetRes> JoinAsync(...)
    => (await JoinRawAsync(client, actorId, request)).ToJoinTargetRes();
```

**completion을 기다리는 곳이 아무 데도 없다.** 그래서 시나리오가 "join이 끝났다"고
믿는 시점이 실제 완료보다 훨씬 이르다.

### 통합 수정안

`/actors/{actorId}/join` 엔드포인트가 handler의 즉시 응답이 아니라 **join completion을
기다려 그 결과를 반환**하게 한다. e2e server에는 이미 `OnJoinCompletedAsync`와
`_pendingJoins`가 있으므로, actor별 `TaskCompletionSource`로 HTTP 요청과 completion을
연결하면 된다. 런타임 변경이 아니라 e2e server 변경이다.

이 수정이 닿는 범위는 다음과 같다.

| 시나리오 | 현재 | 수정 후 기대 |
|---|---|---|
| ST-D1 | `join.Accepted`가 무조건 true여서 단언이 무의미 | 실제 성공을 검증 |
| ST-C1 | source가 죽었는데 accepted → 가드 테스트가 잡는 실패 | completion이 Failed이므로 `!Accepted` 성립 |
| ST-C3 | transfer-out 실패인데 accepted | completion 기준으로 정확해짐 |
| ST-E2 | relocation 중에 재bind해 자멸 | 완료 후 재bind하므로 seal이 올바른 노드로 |

`SampleRegressionTests`가 ST-C1의 `!response.Accepted`를 동결한 것은 이 조기 accepted를
회귀로 잡기 위한 장치였다. 수정 후에는 그 단언이 자연스럽게 성립한다.

주의할 점은 completion 대기에 timeout이 필요하다는 것이다. ST-C1처럼 source가 죽어
completion이 영원히 오지 않는 경우가 있으므로, 엔드포인트는 유한한 deadline을 두고
그때는 현재처럼 예외 경로로 떨어져야 한다.

### (정정) 통합 수정안은 그대로는 성립하지 않는다

"엔드포인트가 join completion을 기다리게 한다"는 앞 절의 안은 cross-node join에서
동작하지 않는다. 스펙 §275가 `Accepted`는 **target Actor**가 받는다고 규정하고, 실측도
같다. ST-D1의 remote 절반에서 `success_reply`는 actor-b에만 남고 actor-a에는 없다.

```
actor-a: 0   actor-b: 1     (remote actor 의 success_reply)
```

HTTP 요청은 source(actor-a)로 들어가는데 성공 completion은 target(actor-b)에 도착하므로,
source의 엔드포인트가 그것을 기다릴 방법이 없다. source가 로컬에서 볼 수 있는 것은
`Rejected`와 commit 전 `Failed`뿐이다.

### 수정 방향을 바꾼다

시나리오가 `join.Accepted`를 "relocation이 끝났다"는 신호로 쓰는 것을 그만두고,
**target 쪽 completion evidence를 기다린 뒤** 다음 단계로 넘어가게 한다. ST-D1은 이미
그렇게 되어 있다(이 세션에서 `success_reply` 대기를 NodeB로 옮겼다).

ST-E2에 적용하면 line 26의 join 뒤에 NodeB의 `success_reply` evidence를 기다린 다음
line 29의 재bind를 하게 된다. 그러면 relocation이 끝난 뒤 재bind하므로 seal이 올바른
노드(session-a)를 향하고, 자멸 구조가 사라진다.

ST-C1과 ST-C3은 성격이 다르다. 둘은 실패를 검증하는 시나리오이고 실패 completion은
source에 도착하므로, 그쪽은 엔드포인트가 로컬 completion을 기다리는 방식이 유효할 수
있다. 다만 ST-C1의 가드가 `!response.Accepted`를 동결하고 있으므로 handler가
`Accepted=true`를 반환하는 것 자체를 고치는 편이 정합적이다.

즉 하나의 통합 수정이 아니라 두 갈래다. cross-node 성공 검증은 target evidence 대기로,
실패 검증은 handler 반환값 교정으로 간다.

## 2026-07-31 ST-E2 1차 수정 적용 — seal 대상은 고쳐졌고 응답 유실이 남았다

ST-E2에 target completion 대기를 넣었다. join 뒤 곧바로 재bind하지 않고 NodeB의
`success_reply` evidence를 먼저 기다리게 했다. 근거는 스펙 §183(`.Defer()`는 Join 실행
전에 반환)과 §275(`Accepted`는 target Actor가 받는다)이다.

가설이 확인됐다. 수정 전에는 relocation 중에 session-b로 재bind가 일어나 seal이 엉뚱한
노드를 향했는데, 수정 후에는 그 재bind가 사라지고 seal이 올바른 노드로 간다.

```
(before) bind_session session-a → bind_session session-b → bound_seal_begin session-b
(after)  bind_session session-a                          → bound_seal_begin session-a
```

그런데 join은 여전히 `DeadlineExceeded`다. 다음 층이 드러났다.

```
session-a: route_seal_received
           route_seal_drain active_frames=0 waits=False   ← 대기 없이 즉시 처리
actor-a:   route_control_sent type=ZLinkSessionRouteSealRequest
           route_control_sent type=ZLinkSessionRouteAbortRequest   ← 응답이 없어 포기
```

session-a는 seal을 **대기 없이 즉시 처리**하고 결과를 반환한다(`active_frames=0`이므로
drain도 기다리지 않는다). 그런데 actor-a는 그 응답을 받지 못하고 deadline 뒤 abort를
보낸다.

### 같은 패턴이 세 번째다

이 세션에서 "수신측은 처리를 끝냈는데 응답이 요청자에게 돌아가지 않는다"를 세 번 만났다.
ST-E1A의 frame relay(one-way handler에서 예외가 삼켜짐), seal의 조기 return, 그리고 이번
seal의 정상 처리 경로다. 앞의 둘은 각각 다른 원인이었지만 세 번째는 정상 성공 경로에서
일어나므로, node 간 route control request/reply 전달 자체에 문제가 있을 가능성이 있다.

특히 대상이 **session gateway node**(session-a)라는 점이 공통이다. actor node 간
request/reply는 admission 왕복에서 정상 동작하는 것을 이미 확인했다
(`admit_request_sent` → `admit_reply_received`). 따라서 session gateway를 향한 route
control의 응답 경로를 먼저 본다.

### 응답 유실 지점 확정 — 핸들러는 ack=True를 반환한다

session gateway 쪽 핸들러의 반환 직전에 진단을 넣었다.

```
session-a: route_seal_drain     active_frames=0 waits=False
           route_seal_replying  ack=True          ← 응답을 만들어 반환한다
actor-a:   route_control_sent   SealRequest
           route_control_sent   AbortRequest       ← 그 응답을 받지 못한다
```

`ZLinkSessionRouteSealHandler`는 `IZLinkRouteRequestHandler<Request, Reply>`이고
`ack=True`인 reply를 정상 반환한다. 그런데 요청자는 deadline까지 아무것도 받지 못한다.
따라서 결함은 핸들러나 seal 로직이 아니라 **route request의 응답 전송**에 있다.

actor node 사이의 request/reply는 정상이다. admission 왕복이
`admit_request_sent` → `admit_reply_received`로 완결되는 것을 이미 확인했다. 차이는
대상이 **session gateway node**라는 점뿐이다.

정리하면 ST-E2의 남은 결함은 "session gateway를 대상으로 한 route request의 응답이
요청자에게 돌아오지 않는다"이다. 이는 seal에 국한되지 않을 수 있으므로, 같은 경로를
쓰는 다른 route control(abort, commit)도 함께 확인해야 한다.

참고로 session gateway 프로세스에는 flow 로깅이 켜져 있지 않아(로그 5줄) 기존 추적으로는
이 구간이 보이지 않았다. 이번 진단이 없었다면 "seal이 처리되지 않는다"로 오판할 수
있었다.

## 2026-07-31 ST-E2 잔여 결함 확정 — mesh 계층에서 응답이 오지 않는다

요청자 쪽 mesh 콜백에 진단을 넣어 확정했다.

```
actor-a:   route_control_sent   target=session-a-... type=ZLinkSessionRouteSealRequest
           route_control_sent   target=session-a-... type=ZLinkSessionRouteAbortRequest
           node_request_result  target=session-a-... result=TimedOut
           node_request_result  target=session-a-... result=TimedOut
session-a: route_seal_received
           route_seal_drain     active_frames=0 waits=False
           route_seal_replying  ack=True
```

session-a의 핸들러는 `ack=True`인 reply를 반환하는데, actor-a의 mesh 계층은 두 요청 모두
`TimedOut`으로 끝난다. 즉 **응답이 요청자 노드에 도달하지 않는다.** 프레임워크 상위
계층이 아니라 node 간 request/reply 전달의 문제다.

같은 프로세스 쌍에서 요청은 정상적으로 간다(session-a가 받았다). 응답 방향만 유실된다.

actor node 사이의 request/reply는 정상이다. admission 왕복이
`admit_request_sent` → `admit_reply_received`로 완결된다. 차이는 대상이 **session gateway
node**라는 점이다. session gateway는 다른 역할로 기동하므로, 그 노드가 응답을 되돌릴
경로를 갖추고 있는지가 다음 확인 지점이다.

확인할 것은 셋이다. session-a의 mesh peer 집합에 actor-a가 admitted 상태로 있는지,
reply 송신이 실제로 일어나는지(session gateway에는 flow 로깅이 꺼져 있어 보이지 않는다),
그리고 요청에 쓰인 `SendFlags.DontWait`가 응답 경로에도 적용되어 혼잡 시 버려지는지다.

### 응답 주소는 정확하다

세 후보 중 첫째를 확인했다. seal 핸들러가 받은 `context.SourceNodeRid`는 요청자의 실제
rid와 정확히 일치한다.

```
session-a: route_seal_replying ack=True source_node=actor-a-bc77f9cc-a092-413f-a36d-64445ecf3d9d
actor-a  : (실제 rid)          actor-a-bc77f9cc-a092-413f-a36d-64445ecf3d9d
```

따라서 "응답을 어디로 보낼지 모른다"는 원인은 배제된다. 핸들러는 올바른 요청자 주소를
알고 `ack=True`를 반환한다. 그럼에도 요청자의 mesh 계층은 `TimedOut`을 받는다.

남은 후보는 둘이다. 프레임워크가 route request의 응답을 실제로 송신하지 않거나, 송신은
되는데 요청자 쪽에서 correlation이 맞지 않아 대기 중인 요청과 연결되지 못하는 경우다.
전자와 후자는 증상이 같으므로 송신 측에 진단을 넣어야 갈린다.

참고로 이 핸들러는 `context`를 `_ = context;`로 버리고 있었다. 진단을 넣으며 그 폐기를
제거했다. 응답 경로를 조사하는 데 필요한 정보가 코드에 있으면서도 쓰이지 않고 있었다.

### 응답은 송신된다 — 전송 또는 correlation 문제로 좁혀진다

남은 두 후보 중 "송신하지 않는다"를 배제했다. `ZLinkMeshNodeRouteDispatcher`의 응답 송신
직전에 진단을 넣은 결과다.

```
session-a: route_seal_replying  ack=True
           route_reply_send     source=actor-a-7422671e-7a2c-4ff1-86d0-6a5c5cd8649e
           route_reply_send     source=actor-a-7422671e-...        (abort 응답)
actor-a  : node_request_result  target=session-a-... result=TimedOut
           node_request_result  target=session-a-... result=TimedOut
```

session-a는 seal과 abort 두 요청 모두에 대해 **올바른 요청자 주소로 응답을 송신한다.**
그런데 actor-a의 mesh 콜백은 두 번 다 `TimedOut`을 받는다.

따라서 결함은 다음 둘 중 하나다. 응답이 전송 계층에서 유실되거나, 도착은 하는데
correlation이 맞지 않아 대기 중인 요청과 연결되지 못한다. 프레임워크 상위 계층은 양쪽
끝에서 정상 동작하는 것이 확인됐으므로, 다음 조사는 mesh 전송 계층으로 내려가야 한다.

조사 범위가 프레임워크에서 core transport로 넘어가는 지점이므로 여기서 구간을 끊는다.
정리하면 ST-E2의 최종 미해결 항목은 "session gateway node가 보낸 route request 응답이
요청자 node에 도달하지 않거나 correlation되지 않는다"이다.

## 2026-07-31 ST-F3도 같은 원인이다 — 하나를 고치면 둘이 움직인다

ST-F3를 같은 진단으로 실행해 대조했다. ST-E2의 잔여 결함과 패턴이 정확히 같다.

```
actor-a  : bound_seal_begin     session_node=session-a-...
           node_request_result  target=session-a-... result=TimedOut
           node_request_result  target=session-a-... result=TimedOut
session-a: route_reply_send     source=actor-a-986c9a73-...
           route_reply_send     source=actor-a-986c9a73-...
```

ST-F3는 ST-E2와 달리 relocation 중 재bind를 하지 않는다. 그런데도 같은 지점에서 멈춘다.
따라서 두 시나리오는 **잔여 결함 하나를 공유한다.** session gateway가 보낸 route request
응답이 요청자 node에 도달하지 않는 문제다.

이로써 bound session 군집의 구조가 정리된다.

| 시나리오 | 원인 |
|---|---|
| ST-E1A | one-way handler에서 예외가 삼켜져 응답 없음 (**해결**) |
| ST-E2 | 조기 Accepted로 인한 재bind (**해결**) + 아래 공통 결함 |
| ST-F3 | 아래 공통 결함 |
| ST-E1 | 미조사 |

공통 결함은 "session gateway node가 보낸 route request 응답이 요청자 node에 도달하지
않거나 correlation되지 않는다"이며, 이것을 고치면 ST-E2와 ST-F3가 함께 움직인다.
ST-E1도 bound session 경로이므로 같은 원인일 가능성이 있다.

## 2026-07-31 ST-B2 — commit 뒤 recovery가 Accepted를 전달하지 않는다

ST-B2("Source Cleanup Failure After Success")는 commit이 끝난 뒤 source가 죽는 상황을
검증한다. 시나리오는 `CrashNodeAAndWaitUnavailableAsync()` 뒤에 **target**에서
`success_reply`를 기다린다.

```csharp
await context.CrashNodeAAndWaitUnavailableAsync();
await context.WaitEvidenceAsync(target, [
    $"{scenario}|{actorId}|success_reply|{spotId}"
], timeoutMilliseconds: 20_000);
```

이 대기는 스펙과 일치한다. `15-spot-actor.ko.md` §276이 이렇게 규정한다.

> "Commit 뒤 recovery는 source로 rollback하지 않고 **target을 복구한 뒤 같은
> `OperationId`의 `Accepted`를 전달한다.**"

즉 commit 뒤 source가 죽어도 target이 복구되어 Accepted completion이 나와야 한다.
그런데 20초를 기다려도 `success_reply`가 관측되지 않는다.

시나리오는 올바르므로 **런타임이 commit 뒤 recovery에서 Accepted를 전달하지 않는
것**이 결함이다. ST-D1에서 고친 "success_reply를 어느 노드에서 기다리는가" 문제와는
다르다. ST-B2는 처음부터 target에서 기다리고 있었다.

앞선 매트릭스에서 ST-B2의 실패 사유는 `Transport endpoint is not connected`였는데 이번에는
`Expected evidence marker`로 바뀌었다. 이 세션의 수정으로 더 뒤 단계까지 진행하게 된
것으로 보이며, 그만큼 원인이 더 정확히 드러났다.

## 2026-07-31 ST-E1도 같은 구조다 — bound session 군집이 셋으로 확인된다

ST-E1(`StE1BoundSessionPushAfterTransferScenario`)의 대기 지점을 확인했다.

```csharp
ZlinkStreamAssert.Ensure(join.Accepted, "ST-E1 join was rejected.");
await context.WaitEvidenceAsync(context.NodeB, [
    $"ST-E1|{actorId}|success_reply|{spotId}"
]);
```

이미 **target(NodeB)**에서 기다린다. 스펙 §276("`Accepted`는 target Actor가 받는다")과
일치하므로 시나리오는 옳다. 그리고 이름 그대로 bound session이 붙은 actor의 transfer를
검증한다.

즉 ST-E1, ST-E2, ST-F3가 모두 "bound session actor의 relocation이 완료되지 않는다"는
같은 증상을 보인다. ST-E2와 ST-F3는 진단으로 원인이
"session gateway가 보낸 route request 응답이 요청자에게 도달하지 않는다"임을 확인했고,
ST-E1은 구조가 같으므로 같은 원인일 가능성이 높다(진단 실행은 매트릭스 종료 후로 미룸).

정리하면 이 하나의 결함이 **세 시나리오**를 잡고 있다.

| 시나리오 | 검증 대상 | 확인 방법 |
|---|---|---|
| ST-E1 | transfer 뒤 bound session push | 구조 대조 |
| ST-E2 | bound session rebind isolation | 진단 실측 |
| ST-F3 | bound session cross-move order | 진단 실측 |

우선순위가 높다. 다만 원인 지점이 framework를 벗어나 mesh transport에 있으므로,
framework 범위에서 고칠 수 있는지부터 판단해야 한다.

## 2026-07-31 최종 확정 매트릭스 (수정 반영 후 29개 재실행)

**통과 15**: ST-A1, ST-A2, ST-A3, ST-B1, ST-B3, ST-B4, ST-D1, ST-E1A, ST-F1, ST-F4,
ST-F5, ST-F6, ST-G3, ST-H1, ST-I6

**실패 14**: ST-B2, ST-B5, ST-C1, ST-C2, ST-C3, ST-D2, ST-E1, ST-E2, ST-F2, ST-F3,
ST-G6, ST-I1, ST-I4, ST-I5

세션 초반 매트릭스와 개수는 같지만 구성이 다르다. ST-B3·ST-B4·ST-D1·ST-E1A가 실패에서
통과로 바뀌었고, ST-C1은 통과에서 실패로 돌아갔다. **ST-C1의 통과는 유효하지 않았다.**
이 세션 초반에 가드 테스트가 지키는 `!response.Accepted` 단언을 잘못 제거해 통과한
것이었고, 원복하면서 원래대로 실패 상태가 됐다. 즉 실제 순증은 네 개다.

### 확정된 원인별 파급

| 결함 | 잡고 있는 시나리오 | 범위 |
|---|---|---|
| session gateway route 응답 유실 | ST-E1, ST-E2, ST-F3 | mesh transport |
| commit 뒤 recovery가 Accepted 미전달 | ST-B2 | framework |
| 조기 `Accepted` 반환 | ST-C1, ST-C3 | e2e join 의미론 |
| 하네스 관측 지점 전환 | ST-D2, ST-I4, ST-I5 | 하네스 |
| 환경 (inotify 한도) | ST-B5 | 환경 |
| 미조사 | ST-C2, ST-F2, ST-G6, ST-I1 | - |

다음 착수는 ST-B2가 적합하다. framework 범위이고 스펙 §276이 명확한 근거이며, recovery
경로(`SchedulePublishedActorRelocationRecovery` → `CompleteRoutedActorHandoffAsync`)가
이미 존재하므로 왜 동작하지 않는지만 밝히면 된다. 그 경로가 `TryRunDetached`로 분리
실행되는 점이 눈에 띈다. 이 세션에서 네 번 만난 "예외가 삼켜져 아무 일도 일어나지 않는"
패턴과 모양이 같다.

## 2026-07-31 dotnet e2e 12개 스위트가 기동조차 못 하던 원인과 baseline

SpotActorTransfer 외의 dotnet e2e 스위트 12개를 처음으로 돌렸더니 전부 기동 단계에서
실패했다. 원인은 하나였다.

```
ZLinkConfigurationException: ApplicationHwmBytes uses Auto sizing,
but no finite process memory limit was ...
```

`06-framework-api.ko.md`가 이 동작을 규정한다.

> "Auto mode에서 유한한 process memory 상한을 확인하지 못하거나 계산 결과가 양수가
> 아니면 socket bind 전에 configuration error로 실패한다."

즉 런타임은 스펙대로다. 문제는 e2e 호스트가 컨테이너 밖에서 돌고 이 머신의 cgroup에
메모리 한도가 없다는 것이다.

### baseline

git 이력으로 확인했다. Auto HWM 계약은 커밋 `00959010f4`
("advance v11 runtime contract migration", 2026-07-31)에서 들어왔고, 같은 커밋이
SpotActorTransfer의 host factory에만 `ProcessMemoryLimitBytes = 1 GiB`를 추가했다.
나머지 스위트는 갱신되지 않았다. 이 커밋은 이번 세션 작업보다 앞선다.

따라서 **12개 스위트는 이번 세션 작업 이전부터 깨져 있었다.** 이번에 32개 host에 같은
설정을 추가한 것은 그 커밋의 누락을 메운 것이지 회귀 수정이 아니다.

### 남은 문제

기동은 풀렸다. 프로세스가 정상 기동하고 정상 종료하며 stderr도 비어 있다. 그런데
연결·라우트 준비 증거가 나오지 않아 다음 단계에서 실패한다.

```
AutomaticTurnDispatch : play-a → delay-a route readiness 3초 타임아웃
LocationMessaging     : backpressure-consumer route readiness 3초 타임아웃
PubSub                : sub-1 publisher ConnectionReady 3초 타임아웃
```

이것이 `00959010f4`의 또 다른 후속 누락인지 더 오래된 상태인지는 아직 모른다. 확인하려면
`00959010f4^`에서 PubSub 하나를 돌려 비교해야 한다. baseline 없이 고치기 시작하면 무엇을
고치는지 모르는 채 진행하게 되므로, 그 비교를 먼저 하는 것이 낫다.

`ChannelEgressRouting`은 별개다. 인자 없이 호출하면 selector를 명시하라는 사용법 안내를
출력한다. 집계 러너가 등록하지 않는 config가 있어 명시 호출이 필요하다.

## 2026-07-31 ST-B2 — recovery는 스케줄되지만 완료 전달까지 가지 않는다

`SchedulePublishedActorRelocationRecovery` 진입부와 완료 전달 직전에 진단을 넣고 ST-B2를
실행했다.

```
recovery_scheduled  aggregate=804b11a9-... already_watched=False
(recovery_completing — 없음)
```

recovery는 중복 감시로 건너뛴 것이 아니라 **정상적으로 스케줄된다**
(`already_watched=False`). 그런데 `CompleteRoutedActorHandoffAsync` 직전의
`recovery_completing`은 찍히지 않는다. 즉 detached task가 완료 전달까지 도달하지 않는다.

주의할 점이 있다. 이 세션에서 "진단이 없다"를 "도달하지 않았다"로 읽어 세 번 틀렸다.
`recovery_completing`이 있는 경로에도 앞선 조건 분기가 있을 수 있으므로, 다음 단계는
detached task 시작 직후와 그 안의 분기마다 진단을 넣어 어디서 멈추는지 특정하는 것이다.
분기 안이 아니라 분기 앞에 조건값과 함께 찍어야 한다.

현재까지 확실한 것은 둘이다. 스펙 §276이 요구하는 "commit 뒤 target 복구 후 Accepted
전달"이 일어나지 않는다. 그리고 그 경로의 시작점인 recovery 스케줄은 정상 수행된다.

### baseline 시도는 접었다

`00959010f4^`에 worktree를 만들어 PubSub을 돌려 비교하려 했으나 그 시점 소스가 현재
로컬 패키지·네이티브 상태와 맞지 않아 빌드부터 실패했다. 비용 대비 효과가 없어 접었다.

따라서 12개 스위트의 연결 실패가 언제부터인지는 미확정으로 남는다. 다만 다음 둘은
확실하다. 기동 실패는 `00959010f4`의 후속 누락이었고 이번에 해소했다. 그리고 그 커밋은
이번 세션 작업보다 앞서므로 12개 스위트의 실패는 이번 세션이 만든 것이 아니다.

### ST-B2 정지 지점 분리 완료

분기 앞에 조건값과 함께 진단을 넣어 구간을 하나로 좁혔다.

```
recovery_scheduled   already_watched=False
recovery_metadata    operation_empty=False legacy_empty=True   ← 조기 return 아님
recovery_validating  actor=actor-cleanup-after-success-... handoff=...
(recovery_completing — 없음)
```

`recovery_metadata`와 완료 전달 사이에는 호출이 하나뿐이다.

```csharp
await ValidateCanonicalRemoteJoinRecoveryAsync(candidate, participant, canonical,
                                               sourceFence, recovery, cancellationToken);
ZLinkFrameworkDebugLog.SpotDiscovery($"recovery_completing ...");
await CompleteRoutedActorHandoffAsync(...);
```

`recovery_validating`은 찍히고 `recovery_completing`은 찍히지 않으므로, **정지 지점은
`ValidateCanonicalRemoteJoinRecoveryAsync`**다. 던지거나 끝나지 않는다. 재시도·실패
추적이 로그에 없으므로 예외가 밖으로 나가 조용히 사라지거나 그 안에서 대기하는 것으로
보인다.

이로써 ST-B2의 결함 위치가 함수 하나로 확정됐다. 다음은 그 함수 안에서 어느 검증이
걸리는지 보는 것이다.

## 2026-07-31 ST-B2 근본원인 확정 — recovery가 identity 검증에서 거부되고 그 실패가 사라진다

삼켜지던 예외를 포착했다.

```
ZLinkFrameworkException: Actor 'actor-cleanup-after-success-...' canonical Join
recovery mismatches its participant or aggregate identity.
  at ValidateCanonicalRemoteJoinRecoveryIdentity
  at ValidateCanonicalRemoteJoinRecoveryAsync
```

전체 사슬은 이렇다.

```
commit 성공 → recovery 스케줄(정상, already_watched=False)
  → recovery metadata 존재(operation_empty=False)
  → identity 검증에서 mismatch → 예외가 detached task에서 소실
  → Accepted completion 미전달 → ST-B2가 20초 대기 후 실패
```

스펙 §276이 "commit 뒤 recovery는 target을 복구한 뒤 같은 `OperationId`의 `Accepted`를
전달한다"고 요구하므로 이는 스펙 위반이다.

### 두 갈래로 나뉜다

하나는 왜 identity가 어긋나느냐다. recovery record가 participant 또는 aggregate identity와
맞지 않는다. 어느 축이 어긋나는지는 아직 모른다.

다른 하나는 왜 그 실패가 조용하냐다. detached recovery task 안에서 던져진 예외가 어디에도
보고되지 않는다. 이 세션에서 같은 패턴을 다섯 번 만났다. ST-E1A의 frame relay, seal의 조기
return, seal의 정상 경로, deferred join, 그리고 이번 recovery다. **recovery가 실패하면
최소한 로그에는 남아야 한다.** 이번에 넣은 진단이 그 역할을 하지만 임시 진단이 아니라
정식 오류 보고가 필요하다.

두 번째는 첫 번째와 무관하게 그 자체로 고칠 값어치가 있다.

### 어긋나는 축을 특정했다 — envelope과 reference의 InventoryDigest

identity 검증의 큰 복합 조건을 축별로 찍었다. 열세 축 중 열둘이 일치한다.

```
handoff_matches=True  key=True  kind=Actor  obj_gen=1/1  auth_gen=7/7
canon_key=True  canon_obj_gen=1/1  canon_auth_gen=6/6
stable_type=transfer-stateful/transfer-stateful
reloc_ref=pending  crc=0  agg_id=True  agg_gen=1/1
digest_len=32  digest_zero=True  src_rid=True
env_ref_id=True  env_ref_gen=True
env_ref_digest=False          ← 유일하게 어긋나는 축
```

즉 마지막 조건 하나만 실패한다.

```csharp
|| !candidate.Envelope.InventoryDigest.Span.SequenceEqual(
       candidate.Reference.InventoryDigest.Span)
```

**relocation envelope과 그 reference가 InventoryDigest에서 불일치한다.** aggregate id와
generation은 같은데 digest만 다르다. 같은 aggregate를 가리키는 두 기록이 inventory에 대해
서로 다른 값을 들고 있다는 뜻이므로, 둘 중 하나가 갱신되지 않았거나 서로 다른 시점의
inventory로 계산됐을 가능성이 있다.

ST-B2의 결함이 이 한 축으로 좁혀졌다. 다음은 envelope과 reference의 digest가 각각 언제
어떤 inventory로 계산되는지 확인해 어느 쪽이 낡았는지 정하는 것이다.

### 두 digest의 실제 값 — reference가 전부 0이다

두 값을 직접 찍었다.

```
env_digest=BDAE29D54153F3A0...   (실제 digest)
ref_digest=0000000000000000...   (전부 0)
env_len=32  ref_len=32
```

길이는 둘 다 32로 정상인데 **reference의 InventoryDigest가 채워지지 않았다.** envelope만
실제 digest를 들고 있다.

여기서 눈여겨볼 점이 있다. 같은 검증에서 `wire.RelocationInventoryDigest`는 **전부 0이어야
통과한다**(`digest_zero=True`가 통과 조건이다). 그리고 이 relocation은
`reloc_ref=pending`, `crc=0`으로 **payload가 아직 publish되지 않은 pending 상태**다.

즉 pending relocation에서는 inventory digest를 0으로 두는 규약이 있는 것으로 보이고,
wire record와 reference는 그 규약을 따르는데 **envelope만 실제 digest를 계산해 넣는다.**
그래서 envelope과 reference를 그대로 비교하는 마지막 조건이 실패한다.

`ZLinkAggregateInventoryDigest.Compute` 호출 지점이 다섯 곳이므로, envelope을 만드는
경로가 pending 여부와 무관하게 digest를 계산하는지 확인하면 어느 쪽이 규약을 어기는지
정해진다. 판단할 것은 둘이다. envelope이 pending일 때 0을 넣어야 하는지, 아니면 검증이
pending relocation에서 이 비교를 건너뛰어야 하는지다.

### 두 값의 출처가 다르다 — 하나는 계산값, 하나는 미publish 상태

`ZLinkRelocationRecoveryCandidate`는 세 가지를 들고 있다.

```csharp
internal sealed record ZLinkRelocationRecoveryCandidate(
    ZLinkRelocationManifestReference Reference,   // authority에 publish된 manifest 참조
    ZLinkRelocationEnvelope Envelope,             // 로컬에서 만든 envelope
    IReadOnlyList<ZLinkAuthorityEntry> Authorities);
```

`Envelope`의 digest는 계산값이다. standalone actor relocation 경로가 참가자 하나로
직접 계산한다.

```csharp
var digest = ZLinkAggregateInventoryDigest.Compute([publicationParticipant]);
return new ZLinkRelocationEnvelope(relocationId, 1, digest, [participant]);
```

반면 `Reference`는 authority에 publish된 manifest를 가리키는데, ST-B2의 relocation은
`reloc_ref=pending`, `crc=0`, digest 전부 0이다. 즉 **manifest가 publish되지 않은
상태**다.

그런데 `ZLinkRelocationStartupRecovery`의 주석은 이렇게 말한다.

> "Finds durable Actor, User Spot and Instance Spot relocations whose authority
> **already publishes an immutable root**."

recovery는 root가 이미 publish된 relocation을 대상으로 한다. ST-B2의 relocation은 그
조건을 만족하지 않는데도 candidate로 올라왔고, identity 검증이 그것을 거부한다.

### 두 갈래 해석

하나는 recovery가 애초에 이 relocation을 candidate로 올리지 말았어야 한다는 것이다.
그렇다면 검증은 제 역할을 한 것이고 결함은 candidate 선별에 있다.

다른 하나는 commit이 성공했으면 manifest도 publish되어야 하는데 그러지 않았다는
것이다. ST-B2는 "Source Cleanup Failure After Success"이므로 commit은 성공했다.

어느 쪽이든 스펙 §276이 요구하는 "commit 뒤 target 복구 후 Accepted 전달"은 일어나지
않는다. 다만 고칠 지점이 다르다. 전자는 선별 조건, 후자는 publish 경로다. 이 판단은
relocation의 pending·published 계약을 확인한 뒤에 해야 한다.

## 2026-07-31 ST-B2 결론 — sentinel이 published root 자리에 들어와 있다

`"pending"` reference의 정체를 코드가 직접 설명한다.

```csharp
// The immutable root cannot contain its own reference, checksum, or
// digest. Recovery persists this exact sentinel; startup verifies the
// real root against the published authority instead.
var pendingReference = new ZLinkRelocationManifestReference(
    "pending", 0, relocationId, 1, new byte[32]);
```

즉 `"pending"` + checksum 0 + digest 32바이트 0은 **의도된 sentinel**이다. immutable root는
자기 자신의 reference·checksum·digest를 담을 수 없으므로 recovery record에는 이 sentinel을
저장하고, **startup은 실제 root를 published authority에서 따로 확인한다**는 설계다.

여기에 비추면 문제가 분명해진다. identity 검증은 두 가지를 한다. 하나는 wire record가
sentinel 규약을 지키는지 보는 것이고(`reloc_ref=pending`, `crc=0`, digest 전부 0 —
이번 실행에서 모두 통과), 다른 하나는 `candidate.Envelope.InventoryDigest`와
`candidate.Reference.InventoryDigest`를 비교하는 것이다.

그런데 관측된 `candidate.Reference`는 **sentinel 그 자체**다(digest 전부 0). 주석대로라면
startup recovery는 published authority에서 **실제 root**를 읽어 `Reference`에 넣어야
하는데, sentinel이 그 자리에 들어와 있다. 실제 digest를 계산해 넣은 envelope과 비교하니
당연히 어긋난다.

따라서 결함은 검증이 아니라 **candidate의 `Reference`를 채우는 쪽**이다. sentinel을
published root로 착각해 그대로 넘긴다.

이 결론은 코드가 스스로 밝힌 의도("startup verifies the real root against the published
authority instead")에 근거한다. 다음은 `ZLinkRelocationStartupRecovery`가 `Reference`를
어디서 읽는지 확인해, published authority 대신 recovery record의 sentinel을 읽고 있는지
보는 것이다.

## 2026-07-31 ST-B2 — sentinel이 published로 분류된다

`ZLinkRelocationStartupRecovery`가 published와 unpublished를 가르는 조건이다.

```csharp
if (ZLinkCanonicalRelocationAuthorityStateCodec.TryRead(...)
    && canonical.Phase == 1
    && string.IsNullOrEmpty(canonical.RelocationReference))
{
    // Preparing has no immutable root yet and therefore cannot be
    // reconciled as a published relocation tree.
    unpublished++;
    continue;
}
AddPublished(entry, linked);
```

unpublished 판정은 `RelocationReference`가 **비어 있는지**만 본다. 그런데 sentinel은 빈
문자열이 아니라 `"pending"`이다.

```csharp
var pendingReference = new ZLinkRelocationManifestReference(
    "pending", 0, relocationId, 1, new byte[32]);
```

따라서 sentinel을 가진 relocation은 `IsNullOrEmpty`를 통과하지 못해 **published로
분류되고**, sentinel이 그대로 `group.Reference`가 되어 candidate의 `Reference`에 실린다.
그 뒤 identity 검증이 실제 digest를 가진 envelope과 비교하니 어긋난다. ST-B2에서 관측한
`ref_digest=0000...`이 바로 이 sentinel이다.

### 다만 이것만 고치면 ST-B2는 통과하지 않는다

조건에 `"pending"`을 더해 unpublished로 분류하면, `unpublished == participants.Count`가
되어 recovery가 `null`을 반환하고 candidate 자체가 생기지 않는다. 그러면 identity
mismatch는 사라지지만 **Accepted completion은 여전히 전달되지 않는다.**

즉 질문이 하나 남는다. ST-B2는 commit이 성공한 뒤 source가 죽는 시나리오인데, commit이
성공했다면 manifest도 publish되어 sentinel이 실제 root로 대체되었어야 하는 것 아닌가.
관측된 상태는 commit 뒤에도 reference가 sentinel로 남아 있다.

따라서 결함은 둘 중 하나이거나 둘 다다. 분류가 sentinel을 published로 잘못 보거나,
commit이 성공했는데 publish가 sentinel을 실제 root로 갱신하지 않는다. 후자라면 스펙
§276이 요구하는 recovery의 전제 자체가 성립하지 않는다.

## 2026-07-31 ST-B2 최종 정리 — sentinel 설계와 digest 비교가 서로 맞지 않는다

authority 쪽도 확인했다. `ValidateCanonicalRemoteJoinRecoveryAsync`의 다른 검증은
authority의 publication reference를 candidate의 reference와 비교한다.

```csharp
|| publication.RelocationReference != candidate.Reference.Reference
|| publication.RelocationChecksumCrc32c != candidate.Reference.ChecksumCrc32c
```

이 비교는 통과한다. 즉 authority에 저장된 `RelocationReference`도 `"pending"` sentinel이다.
코드 주석이 말한 대로 "Recovery persists this exact sentinel"이 실제로 그렇게 되어 있다.

그런데 같은 함수의 마지막 조건은 이렇다.

```csharp
|| !candidate.Envelope.InventoryDigest.Span.SequenceEqual(
       candidate.Reference.InventoryDigest.Span)
```

`Reference`는 sentinel이므로 digest가 32바이트 0이고, `Envelope`은
`ZLinkStandaloneActorRelocationRuntime`이 실제로 계산해 넣는다.

```csharp
var digest = ZLinkAggregateInventoryDigest.Compute([publicationParticipant]);
return new ZLinkRelocationEnvelope(relocationId, 1, digest, [participant]);
```

**따라서 이 조건은 standalone actor relocation에서 통과할 수 없다.** sentinel을 persist하는
설계와, envelope·reference의 digest가 같기를 요구하는 검증이 서로 맞지 않는다.

### 정리

ST-B2의 실패는 다음 사슬이다. commit 성공 → recovery 스케줄 → sentinel을 가진
relocation이 published로 분류됨(`IsNullOrEmpty`만 검사하므로) → identity 검증이
envelope의 실제 digest와 sentinel의 0 digest를 비교 → 불일치 → 예외가 detached task에서
소실 → Accepted completion 미전달 → 시나리오 20초 대기 후 실패.

고칠 후보는 셋이고 각각 다른 층이다. 분류가 `"pending"`을 unpublished로 볼 것인가.
envelope이 sentinel과 짝을 이룰 때 digest를 0으로 둘 것인가. 아니면 검증이 sentinel
reference에 대해 이 비교를 건너뛸 것인가. 어느 것이 계약상 옳은지는 sentinel 설계의
의도를 아는 쪽이 정해야 한다. 셋 다 증상은 없애지만 의미가 다르다.

한 가지는 분명하다. detached recovery task에서 예외가 소실되는 것은 어느 선택과도
무관하게 고칠 값어치가 있다. 이 세션에서 같은 패턴을 다섯 번 만났다.

## 2026-07-31 (정정) recovery 실패는 이미 보고되고 있다

앞 절들에서 "detached recovery task의 예외가 어디에도 보고되지 않는다"고 여러 번 적었다.
**틀렸다.** `ZLinkRuntimeTaskRunner`가 이미 보고한다.

```
[zlink-framework] task 'actor-published-relocation-recovery' failed:
  ZLinkFrameworkException: Actor 'actor-cleanup-after-success-...' ...
```

다만 `ZLinkFrameworkDebugLog.TaskFailure`는 `ZLINK_DEBUG_FRAMEWORK_TASKS` 뒤에 있고,
이번 조사 내내 `ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY`만 켜고 돌렸다. 그래서 보고가
없다고 판단했다.

따라서 "예외 소실"을 고칠 필요는 없다. 이 세션에서 같은 패턴을 다섯 번 만났다고 적은 것
중 이 건은 빼야 한다. 나머지 넷(ST-E1A의 one-way frame relay, seal의 조기 return,
seal의 정상 경로, deferred join)은 실제로 응답이 요청자에게 나가지 않는 문제이므로
성격이 다르다.

### 교훈

진단 플래그가 여럿이면 조사 시작 시 전부 켠다. 하나만 켜고 "보고가 없다"고 판단하면
코드에 없는 결함을 만들어낸다. 이 저장소의 framework debug 플래그는 최소 셋이다.
`ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY`, `ZLINK_DEBUG_FRAMEWORK_TASKS`,
`ZLINK_DEBUG_FRAMEWORK_STARTUP`.

ST-B2의 실제 결함(sentinel과 digest 비교의 불일치)과 세 가지 수정 후보는 그대로 유효하다.

## 2026-07-31 ST-I1 첫 관측 — destroy 요청에서 500이 난다

진단 플래그를 모두 켜고(`SPOT_DISCOVERY`, `TASKS`, `STARTUP`) ST-I1을 처음 돌렸다.
클라이언트는 `HTTP request failed with status 500`으로
`StI1RelocationPayloadMeasurementScenario.cs:223`에서 실패한다.

서버(actor-b) 로그에서 그 500의 출처가 보인다.

```
fail: Microsoft.AspNetCore.Server.Kestrel[13] ... An unhandled exception was thrown
      by the application.
info: Request finished HTTP/1.1 POST http://127.0.0.1:39049/actors/actor-payload-small-.../des...
```

즉 **actor destroy 요청 처리 중 예외가 나서 500이 된다.** 예외 본문은 Kestrel의 한 줄
로그에만 있고 stack이 남지 않아 아직 종류를 모른다.

다음은 e2e server의 destroy 엔드포인트가 어떤 예외를 던지는지 확인하는 것이다.
ST-I1은 relocation payload 크기를 측정하는 시나리오이므로, 측정 대상 actor를 정리하는
단계에서 걸리는 것으로 보인다.

### ST-I1 원인 확정 — 시나리오가 user Spot에 있는 actor를 그대로 destroy한다

Kestrel 로그의 예외 본문을 끝까지 읽었다.

```
ZLinkFrameworkException: Actor 'actor-payload-small-...' must leave its current
SPOT before destroy.
  at ZLinkActorSessionManager...DestroyActorAsync
```

스펙 `14-actor-model.ko.md` §525가 이 계약을 규정한다.

> "Actor destroy는 exact `ActorRef`를 받는다. Actor가 user Spot에 있으면 **먼저 leave
> 또는 Entry Spot join을 완료해야 한다.**"

따라서 런타임이 옳고 **시나리오가 계약을 지키지 않는다.** ST-I1은 relocation payload
크기를 측정하는 시나리오이므로 actor를 user Spot으로 이동시킨 뒤 정리 단계에서 그대로
destroy를 호출한다. 스펙대로면 leave 또는 Entry Spot join을 먼저 끝내야 한다.

수정은 시나리오 쪽이다. destroy 전에 Entry Spot으로 되돌리거나 leave를 완료하면 된다.
이 판단은 스펙 문장이 명시적이므로 추가 확인이 필요하지 않다.

### ST-I1 수정 방향과 그 전제

스펙이 요구하는 "leave 또는 Entry Spot join"의 수단은 계약에 있다.
`IZLinkActorContext.JoinEntrySpot(...)`이다. e2e server에는 이를 노출하는 엔드포인트가
없다. 현재 actor node의 엔드포인트는 다음뿐이다.

```
/actors  /actors/{id}/bound-push  /actors/{id}/destroy  /actors/{id}/join
/actors/{id}/probe  /actors/{id}/probe-from-node  /actors/{id}/ref
/actors/{id}/send-from-node  /drain  /evidence  /health
/placement-weight  /process-memory  /relocate  /relocation-blobs  /shutdown  /spots
```

따라서 수정은 e2e server에 Entry Spot 복귀 엔드포인트를 추가하고 ST-I1이 destroy 전에
그것을 호출하는 형태가 된다.

여기서 이미 열려 있는 문제와 만난다. `JoinEntrySpot`도 `.Defer()`를 쓰는 join이므로
handler는 join이 실행되기 전에 반환한다(스펙 §183). 따라서 시나리오가 "복귀가 끝났다"고
믿을 시점을 정해야 하고, 그것은 ST-C1·ST-C3·ST-E2를 막고 있는 조기 `Accepted` 문제와
같은 판단이다. ST-E2에서는 target의 completion evidence를 기다리는 방식으로 풀었다.

정리하면 ST-I1의 수정 자체는 명확하지만, 그 안에서 완료를 어떻게 확인할지는 미결
항목에 의존한다. 같은 판단이 네 시나리오(C1, C3, E2, I1)에 걸쳐 있다.

## 2026-07-31 실패 시나리오의 성격 분류 — 신규 미완성 3건, 기존 실패 11건

커밋 `00959010f4`가 손댄 SpotActorTransfer 시나리오 파일을 현재 실패 목록과 대조했다.

그 커밋이 추가·수정한 시나리오는 일곱이다.

```
StG5SpotWideRelocationInterruption  StG6ApplicationSignaledRelocation
StI2BulkActorRelocation             StI4ActorMessageFollowMatrix
StI4RelocationAuthorityBoundary     StI4SpotMessageFollowMatrix
StI5MessageFollowSafety
```

이 중 현재 실패하는 것은 셋이다. **ST-G6, ST-I4, ST-I5.** ST-G5와 ST-I2는 통과한다.

나머지 열하나(ST-B2, ST-B5, ST-C1, ST-C2, ST-C3, ST-D2, ST-E1, ST-E2, ST-F2, ST-F3,
ST-I1)는 그 커밋이 건드리지 않았으므로 더 오래된 실패다.

### 이 분류가 작업 성격을 가른다

신규 셋은 **아직 통과한 적이 없는 시나리오**다. 회귀가 아니라 마이그레이션이 끝나지
않은 것이므로, 원저자가 의도한 계약이 무엇인지 확인해야 한다. ST-G6는 application이
ready를 신호해도 relocation이 30초 안에 끝나지 않는다.

나머지 열하나는 기존 실패이고, 이 세션에서 그중 여덟의 원인을 확정했다. 앞서 "실패의
상당수가 미완성 마이그레이션일 것"이라고 추정했는데, 대조해 보니 셋뿐이다. 추정을
숫자로 바꾼다.

## 2026-07-31 ST-C2도 bound session 군집이다 — 네 시나리오가 한 원인

미조사였던 ST-C2를 모든 진단 플래그를 켜고 돌렸다. 실패는
`StC2SourceDownAfterTargetCommitScenario.cs:30`에서 `transfer_in`·`joined` evidence
미관측이다. 이 시나리오도 bound session을 쓴다.

```csharp
var beforeTransferReply = await bound.Request(new BoundPushReq("ST-C2", "bound-before-transfer"))
```

진단이 앞선 셋과 같은 모양을 보인다.

```
actor-a: bound_seal_begin      actor=actor-source-down-after-commit-...
         node_request_result   target=session-b-... result=TimedOut
         node_request_result   target=session-b-... result=TimedOut
session: route_reply_send      2건
```

session이 응답을 송신하는데 요청자는 두 번 다 `TimedOut`을 받는다. ST-E2·ST-F3에서 확정한
"session gateway를 대상으로 한 route request의 응답이 요청자에게 도달하지 않는다"와
같다.

따라서 이 하나의 결함이 잡고 있는 시나리오는 넷이다.

| 시나리오 | 검증 대상 |
|---|---|
| ST-C2 | target commit 뒤 source 종료 |
| ST-E1 | transfer 뒤 bound session push |
| ST-E2 | bound session rebind isolation |
| ST-F3 | bound session cross-move order |

기존 실패 열하나 중 넷이 여기에 묶인다. 우선순위가 가장 높은 단일 결함이다.

## 2026-07-31 ST-F2 — marker 순서가 뒤집혀 있다

마지막 미조사 항목이었다. ST-F2는 클라이언트 단언이 아니라 **러너의 사후 검증**에서
실패한다.

```
Marker order failed for actor-inflight-overtake-...:
  backlog_enqueued=238, location_committed=207
```

`run_e2e.sh`의 다음 검증이다.

```bash
require_marker_order actor-inflight-overtake- backlog_enqueued location_committed
```

`backlog_enqueued`가 `location_committed`보다 먼저 나와야 하는데 실제로는 반대다
(207 < 238). 즉 **in-flight message가 backlog에 들어가기 전에 location commit이
먼저 일어난다.**

ST-F2가 검증하려는 것이 "in-flight overtake"이므로, 이 순서는 시나리오의 핵심 계약이다.
commit이 먼저 일어나면 그 사이에 도착한 message가 추월당할 수 있다.

이것은 bound session 군집과 다른 별개의 원인이다. 순서 자체가 뒤집혔는지, 아니면 marker
검증이 보는 지점이 실제 순서를 반영하지 못하는지는 다음 확인 대상이다.

### 이로써 실패 14개의 성격이 모두 파악됐다

| 원인 | 시나리오 | 수 |
|---|---|---|
| session gateway route 응답 유실 | C2, E1, E2, F3 | 4 |
| `.Defer()` join의 완료 확인 방식(판단 대기) | C1, C3, I1 | 3 |
| sentinel과 digest 비교 불일치(판단 대기) | B2 | 1 |
| 신규 미완성 마이그레이션 | G6, I4, I5 | 3 |
| marker 순서 역전 | F2 | 1 |
| 하네스 관측 지점 전환 | D2 | 1 |
| 환경(inotify) | B5 | 1 |

미조사는 0이다.

## 2026-07-31 bound session 군집 — 응답 제출이 반환하지 않는다

session gateway 쪽 응답 경로를 단계별로 좁혔다.

```
route_reply_send       2건    ← ReplyResponseAsync 호출 직전
route_reply_dropped    0건    ← CanReply 는 true, 조용한 drop 아님
route_reply_submitted  0건    ← 호출이 반환하지 않는다
```

즉 `ReplyResponseAsync`에 진입은 하는데 완료되지 않는다. 던지거나 끝나지 않는다.
session 프로세스에는 task 실패 로그도 없다.

경로를 따라가면 `SubmitEnvelopeAsync` → `ZLinkSpotReplySubmitter.SubmitAsync`이고,
그 안은 이렇다.

```csharp
var result = await submitter.SubmitAsync(
        replyParts,
        pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
            received.Reply(pending, SendFlags.DontWait), nameof(SubmitAsync)),
        cancellationToken);
if (result.Status != ZLinkOneWaySubmitStatus.Submitted)
    throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.Terminated);
```

`SendFlags.DontWait`으로 보내므로 상대 경로가 없거나 혼잡하면 즉시 실패하고, 그 결과가
`AcceptOrThrow` 또는 `result.Status` 검사에서 예외가 된다. 그 예외가 어디로 가는지가
다음 확인 지점이다. session 프로세스의 task 실패 로그에 나타나지 않으므로 중간에서
잡히는 것으로 보인다.

이로써 네 시나리오(ST-C2, ST-E1, ST-E2, ST-F3)를 잡고 있는 결함이 함수 하나로 좁혀졌다.
handler는 응답을 만들고, dispatcher는 제출을 시작하지만, 제출이 끝나지 않는다.

## 2026-07-31 bound session 군집 근본원인 — 응답 제출에서 NullReferenceException

응답 호출을 감싸 예외를 포착했다.

```
route_reply_failed source=actor-a-70705cab-...
System.NullReferenceException: Object reference not set to an instance of an object.
  at ZLinkSpotReplySubmitter.SubmitAsync(ZLinkAsyncSubmitter submitter,
       ZLinkBackendRouteReceived received, IReadOnlyList`1 replyParts, ...)
  at ZLinkMeshNodeRouteDispatcher.DispatchNodeRouteAsync(...)
```

**session gateway가 route request에 응답하려 할 때 `SubmitAsync`에서 null 참조가
발생한다.** 그래서 응답이 나가지 못하고 요청자는 deadline까지 기다린다.

해당 함수는 다음과 같다.

```csharp
try { await completionLease.ReserveReplyAsync(...); }
catch { ...; throw; }
var result = await submitter.SubmitAsync(
        replyParts,
        pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
            received.Reply(pending, SendFlags.DontWait), nameof(SubmitAsync)),
        cancellationToken);
if (result.Status != ZLinkOneWaySubmitStatus.Submitted)
    throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.Terminated);
completionLease.TransferToCore();
```

dispatcher가 `_replySubmitter is null`이면 `SubmitDirectAsync`로 가므로 `submitter`는
null이 아니다. 따라서 `completionLease`나 `received`의 내부 값이 유력하다.
`completionLease`는 `ResponderLease` 구조체이므로 default 값이 넘어오면 그 안의 참조가
null일 수 있다.

이 하나가 네 시나리오(ST-C2, ST-E1, ST-E2, ST-F3)를 잡고 있다. NRE는 계약 해석의 여지가
없는 명백한 결함이므로, 판단 대기 없이 고칠 수 있는 항목이다.

다음은 `SubmitAsync` 안에서 어느 참조가 null인지 특정하는 것이다.

### null 참조의 정체 — nullable permit을 검사 없이 역참조한다

`ResponderLease`는 struct가 아니라 **sealed class**다(앞선 커밋 메시지에서 struct로
추정했던 것을 정정한다).

```csharp
internal sealed class ResponderLease : IDisposable
{
    private readonly ZLinkCompletionAdmissionOwner _owner;
    internal ValueTask ReserveReplyAsync(...) => _owner.ReserveReplyAsync(this, ...);
}
```

그리고 dispatcher의 파라미터는 **nullable**이다.

```csharp
private async ValueTask DispatchNodeRouteAsync(
    ZLinkBackendRouteReceived received,
    ZLinkEnvelopeHeader header,
    ZLinkCompletionAdmissionOwner.ResponderLease? completionPermit,   // ← nullable
    CancellationToken cancellationToken)
```

그런데 그 값이 흘러가는 `ReplyResponseAsync`와 `ZLinkSpotReplySubmitter.SubmitAsync`의
파라미터는 non-nullable이고, `SubmitAsync`는 첫 줄에서 바로 역참조한다.

```csharp
await completionLease.ReserveReplyAsync(Measure(replyParts), cancellationToken);
```

즉 **completion permit이 null인 경로에서 응답을 보내려 하면 NullReferenceException이
난다.** 타입이 null을 허용한다고 선언해 놓고 사용처가 그 가능성을 다루지 않는다.

이것이 ST-C2·ST-E1·ST-E2·ST-F3 네 시나리오를 잡고 있는 결함이다. 수정은 permit이 null일
때의 동작을 정하는 것이다. 예약을 건너뛰고 그대로 제출할 것인지, 아니면 애초에 null이
오지 않아야 하는데 오는 것인지 확인해야 한다. 후자라면 permit을 만드는 쪽이 결함이다.

## 2026-07-31 bound session 군집 근본원인 완결 — infrastructure request는 permit 없이 응답할 수 없다

permit이 null인 이유를 찾았다. **의도적으로 null이다.**

```csharp
using (var completionPermit = received.CanReply
           && !IsInfrastructureRelay(received)
           ? await _completionAdmission.AcquireResponderAsync(cancellationToken)
           : null)
```

infrastructure relay는 completion admission을 잡지 않는다. 그리고 무엇이
infrastructure인지 보면 seal이 명시되어 있다.

```csharp
private static bool IsInfrastructureRelay(...) =>
    received.ChannelName is null
    && ((header.Kind == Command && header.MessageName is
            RemoteSessionPushProtocol.PacketName
            or RemoteActorFrameProtocol.PacketName
            or RemoteActorReplyProtocol.PacketName)
        || (header.Kind == Request && header.MessageName is
            SessionRouteCommitProtocol.PacketName
            or SessionRouteCommitProtocol.SealPacketName        // ← seal
            or SessionRouteCommitProtocol.AbortPacketName
            or SessionRouteCommitProtocol.UnsealPacketName));
```

Command 세 개는 one-way라 응답이 없으니 permit이 없어도 된다. 그런데 **Request 네 개는
응답이 필요한데도 같은 분류에 들어가 permit이 null이 된다.** 그리고 응답 경로는 그 null을
검사 없이 역참조한다.

```csharp
await completionLease.ReserveReplyAsync(Measure(replyParts), cancellationToken);
```

따라서 seal·abort·unseal·commit 네 종류의 infrastructure **request**는 **응답을 보내려는
순간 반드시 NullReferenceException이 난다.** 이 경로는 구조적으로 성공할 수 없다.

같은 파일의 오류 응답 두 곳은 `completionPermit!`로 null 허용을 억제해 두었는데, 그 역시
infrastructure request에서 실행되면 같은 NRE가 난다.

### 수정 방향

infrastructure request도 응답이 필요하므로 다음 중 하나다. 응답 경로가 permit이 null일 때
예약을 건너뛰도록 하거나, infrastructure 분류를 one-way command에만 적용하고 request 네
개는 permit을 잡도록 하거나.

전자가 최소 변경이고 후자가 의미상 정확하다. completion admission이 응답 바이트를
회계하는 장치라면, 응답을 실제로 보내는 request는 회계 대상이어야 자연스럽다. 다만
infrastructure를 회계에서 빼려는 원래 의도가 있었다면 전자가 맞다.

이 결함 하나가 ST-C2, ST-E1, ST-E2, ST-F3 네 시나리오를 잡고 있다.

## 2026-08-01 bound session 군집 수정 — ST-C2와 ST-E1 해결

응답 경로가 null responder lease를 다루도록 고쳤다.

```csharp
//  Infrastructure requests - session route commit, seal, abort and unseal -
//  carry no responder lease on purpose: their replies must not queue behind
//  application completion admission, or the relocation control plane would
//  stall whenever application traffic filled it. They still need an answer,
//  so send without reserving.
if (completionLease is not null)
    try { await completionLease.ReserveReplyAsync(...); }
    catch { ZLinkMessageParts.DisposeAll(replyParts); throw; }
...
completionLease?.TransferToCore();
```

근거는 `ZLinkCompletionAdmissionOwner`가 pending request·send·byte 상한을 두는
backpressure 장치라는 점이다. infrastructure를 그 회계에서 제외한 것은 제어 평면이
application 혼잡에 막히지 않게 하려는 의도다. 따라서 permit을 잡게 만드는 대신 응답
경로가 없는 permit을 견디게 하는 쪽이 원래 의도를 지킨다.

결과는 다음과 같다.

| 시나리오 | 결과 |
|---|---|
| ST-C2 | **통과** |
| ST-E1 | **통과** |
| ST-F3 | 실패 지점 이동(runtime evidence marker 미관측) |
| ST-E2 | 실패 지점 이동 |

회귀는 없다. ST-D1·A1·B1·E1A·F4 통과, `Zlink.Framework.sln` 1831건 전량 통과.

### 같은 실수를 한 번 더 했다

첫 수정은 효과가 없었다. `ZLinkSpotReplySubmitter`에 본문이 거의 같은 메서드가 둘 있는데
(`SubmitDirectAsync`와 `SubmitAsync`), 텍스트로 앵커해 치환하면서 앞의 것을 잡았다.
stack이 `SubmitAsync`를 가리키는데 `SubmitDirectAsync`를 고쳐 놓고 "고쳤는데 안 된다"고
판단할 뻔했다. 진단을 넣어 위치를 확인하고서야 드러났다.

교훈은 앞선 것들과 같다. 비슷한 코드가 여럿일 때 텍스트 앵커는 위험하다. 수정 뒤에는
그 수정이 실제로 실행되는 경로에 있는지 확인해야 한다.

### ST-E2 다음 층 — relocation은 끝나고 새 session bind가 반복 실패한다

수정 후 ST-E2의 실패 지점이 line 29에서 line 37로 이동했다. 사이에 있는
`success_reply` 대기(line 33-35)를 통과하므로 **relocation 자체는 완료된다.**

```csharp
await context.WaitEvidenceAsync(context.NodeB, [
    $"ST-E2|{actorId}|success_reply|{spotId}"]);          // ← 통과
var targetRef = await context.GetActorRefAsync(context.NodeB, actorId);
await using var newSession = await context.ConnectAndBindAsync(
    context.Options.NodeBStreamEndpoint, "ST-E2", targetRef);   // ← line 37, timeout
```

진단을 보면 이 단계의 성격이 드러난다.

```
bind_session session_node=session-a-...      1건   (최초 bind, 정상)
bind_session session_node=session-b-...    211건   (재시도)
```

`route_reply_failed`는 한 건도 없다. 즉 앞서 고친 NRE는 이 경로에 없다. 대신 bind 요청이
actor node에 **211번 도달하는데** 클라이언트는 끝내 성공을 받지 못한다. 매번 처리되지만
결과가 돌아오지 않거나 거절되어 재시도가 반복된다.

다음은 bind 응답이 어떻게 되는지다. `ZLinkRemoteSessionBindingHandler`가
`ZLinkActorBoundSessionRelay.SendReplyAsync`로 응답하므로 그 경로를 본다.

### ST-E2 bind는 예외가 아니라 hang이다

bind 경로에 진단을 넣어 두 가지를 확인했다.

```
bind_session  session-b-...   211건   ← BindRemoteBoundSessionRouteAsync 진입
bind_reply                      0건   ← 그 호출이 반환하지 않는다
bind_failed                     0건   ← 예외도 아니다
```

즉 `BindRemoteBoundSessionRouteAsync`가 **진입만 하고 반환도 예외도 없이 멈춘다.**
클라이언트는 재시도하고 그때마다 같은 일이 반복되어 211건이 쌓인다.

예외 경로가 아니므로 앞서 고친 NRE와는 다르다. 그 함수 안에서 대기하는 지점을 찾아야
한다. `bind_session` 진단 바로 뒤가 `BeginActorSessionReplacement`이므로 거기서부터
본다. 이름이 replacement이므로 기존 binding을 교체하며 무언가를 기다릴 가능성이 있고,
ST-E2는 이전 session이 아직 살아 있는 상태에서 새 session을 bind하는 시나리오이므로
그 교체가 이전 binding의 해제를 기다린다면 설명이 된다.

### (정정) 이전 binding 해제 대기가 아니다

앞 절에서 "replacement가 이전 binding의 해제를 기다린다면 설명이 된다"고 추정했다.
측정 결과 **틀렸다.**

```
bind_replacement owns=True   212건
```

212번의 시도가 **전부 실행권을 가진다.** 따라서 `!replacement.OwnsExecution` 분기의
`await replacement.Completion.WaitAsync(...)`에 걸리는 경우는 하나도 없다. 이전 시도를
기다리다 막히는 구조가 아니다.

매 재시도가 새로 실행권을 얻고 각자 멈춘다는 뜻이므로, 정지 지점은 실행권을 가진 경로
안쪽이다. 다음은 그 경로를 단계별로 짚는 것이다.

이 세션에서 추정이 빗나간 것이 여러 번인데, 공통점은 "그럴듯한 설명"을 먼저 세우고
측정으로 확인한 순서다. 순서를 뒤집으면 회차가 줄어든다.

## 2026-08-01 ST-E2 정지 지점 확정 — TombstoneReplacedSessionOwnerAsync

실행권 경로를 단계별로 찍었다.

```
bind_replacement       211건   owns=True
bind_tombstone_begin   211건   ← 전부 tombstone 호출에 진입한다
bind_done                1건   ← 하나만 끝난다
```

즉 **210건이 `TombstoneReplacedSessionOwnerAsync` 안에서 멈춘다.** 예외도 아니고
반환도 하지 않는다.

이 함수는 교체된 이전 session owner를 tombstone 처리한다. ST-E2는 이전 session이 아직
살아 있는 상태에서 새 session을 bind하므로 이 경로가 반드시 실행된다. 하나만 완료된다는
점을 보면, 첫 시도가 무언가를 붙잡은 뒤 놓지 않고 이후 시도들이 거기에 쌓이는 형태로
보인다. 다만 `OwnsExecution`이 전부 true이므로 replacement 수준의 상호배제는 아니다.
그보다 아래, tombstone 처리 자체가 쓰는 자원에서 막히는 것이다.

다음은 그 함수 내부를 같은 방식으로 짚는 것이다.

### 정지 원인 — 이전 session owner로 보낸 tombstone 요청이 응답받지 못한다

tombstone 함수 안의 원격 왕복을 찍었다.

```
tombstone_request_sent   211건
tombstone_response         0건
```

`TombstoneReplacedSessionOwnerAsync`는 이전 session owner(session-a)에게
`RequestToNode`로 tombstone을 요청하고 응답을 기다린다.

```csharp
response = await Services.GetRequiredService<IZLinkRouteClient>()
    .RequestToNode(previous.MeshName, sessionNodeRid, request)
    .Timeout(Registration.DefaultRequestTimeout)
    .Async<ZLinkRemoteSessionOwnerTombstoneResponse>(cancellationToken);
```

**211번 보내고 한 번도 응답받지 못한다.** 이번 세션에서 고친 결함
(infrastructure route request의 응답이 permit 없이 NRE로 죽는 문제)과 **형태가 같다.**
다만 tombstone packet은 `IsInfrastructureRelay` 목록에 없으므로 permit은 잡힌다.
따라서 원인이 같지는 않다.

확인할 것은 셋이다. session gateway에 이 packet의 handler가 등록되어 있는지, 요청이
실제로 그 노드에 도달하는지, 도달한다면 응답이 어디서 사라지는지다. 앞서 같은 종류의
문제를 풀 때 송신·수신·응답 세 지점에 진단을 나눠 넣어 한 번에 갈렸으므로 같은 방법을
쓴다.

### ST-E2는 비결정적이다

tombstone 수신 진단을 넣고 다시 돌렸더니 송신도 수신도 0건이었다. 실패 지점이
line 37(새 session bind)이 아니라 line 33(relocation의 `success_reply` 대기)이었기
때문이다. 직전 실행에서는 line 33을 통과하고 line 37에서 멈췄다.

즉 **같은 시나리오가 실행마다 다른 지점에서 실패한다.** relocation 자체가 때로는
완료되고 때로는 완료되지 않는다.

이것은 조사 방법에 영향을 준다. 한 번의 실행에서 특정 진단이 없다는 사실을 "그 경로에
도달하지 않는다"로 읽으면 안 되고, 이번처럼 "이번 실행은 거기까지 가지 않았다"일 수
있다. 이 세션에서 같은 오독을 여러 번 했는데, 비결정적 시나리오에서는 그 위험이 더 크다.

따라서 ST-E2의 tombstone 무응답(211건 송신, 0건 응답)은 유효한 관측이지만, 그것이
유일한 정지 원인인지는 확실하지 않다. relocation이 완료되지 않는 경우가 별도로 있고,
그쪽은 아직 원인을 모른다. 두 층을 각각 다뤄야 한다.

## 2026-08-01 ST-E2 flake 양상과 tombstone 미도달

먼저 비결정성을 정량화했다. 세 번 연속 실행에서 **3/3이 line 37**에서 실패한다. 앞서 한
번 관측한 line 33(relocation 미완료)은 드문 경우다. 따라서 공략 대상은 line 37,
즉 새 session bind의 tombstone 층이다.

그 층을 송신·수신·응답 세 지점으로 나눠 측정했다.

```
tombstone_request_sent (actor-a)   212건
tombstone_received     (전체)        0건
tombstone_response                   0건
```

**요청이 대상 노드에 도달하지 않는다.** 수신 handler가 한 번도 실행되지 않으므로 응답이
없는 것도 당연하다. 이는 앞서 고친 결함(수신은 되는데 응답이 유실)과 **다른 성격**이다.
그쪽은 수신측이 처리를 마치고도 응답이 못 돌아온 경우였고, 이쪽은 애초에 도달하지 않는다.

`TombstoneReplacedSessionOwnerAsync`는 `IZLinkRouteClient.RequestToNode(previous.MeshName,
sessionNodeRid, request)`로 보낸다. 확인할 것은 셋이다. `previous.MeshName`과
`sessionNodeRid`가 실제 이전 session owner를 가리키는지, 그 노드가 해당 mesh의 peer로
연결되어 있는지, route client가 그 조합을 해석하지 못하고 조용히 버리는지다.

`bind_session` 진단에서 이전 owner가 session-a였고 새 bind 대상이 session-b였다는 점을
감안하면, 첫 번째(주소가 실제 이전 owner를 가리키는가)부터 보는 것이 순서다.

### 주소와 연결은 정상이다 — 남은 것은 route 해석

세 후보 중 둘을 배제했다.

```
tombstone_request_sent
  target=session-a-df1ae3e5-...      ← session-a 의 실제 rid 와 일치
  local=actor-b-...                   ← 송신자는 재배치 후 새 owner
  mesh=spot-actor-transfer
  peers=[actor-c:Admitted, actor-d:Admitted, actor-a:Admitted,
         session-a:Admitted, session-b:Admitted]
```

주소가 실제 이전 session owner를 정확히 가리키고, **session-a는 송신 노드의 admitted
peer다.** 그런데도 수신측 handler는 한 번도 실행되지 않는다.

따라서 남은 후보는 셋째, route client 또는 그 아래 계층이 이 packet을 해석하지 못하고
버리는 경우다. handler 자체는 `ZLinkMeshNodeRouteDispatcher`의 descriptor 목록에
등록되어 있고 DI에도 `TryAddScoped<ZLinkRemoteSessionOwnerTombstoneRouteHandler>()`로
등록되어 있다. 다만 그 등록이 **session gateway 프로세스의 mesh node에도 적용되는지**는
확인하지 않았다. gateway는 actor node와 다른 역할로 기동하므로 route 등록 집합이 다를 수
있다.

다음은 session gateway가 이 packet의 route를 등록하는지 확인하는 것이다.

### 세 후보 모두 배제 — 유실은 framework 밖이다

마지막 후보였던 route 미등록도 배제됐다.

```
"No node route request handler" 오류 :  0건
session-a stdout                     : 26줄 (tombstone 흔적 없음)
```

handler가 등록되지 않았다면 dispatcher가 Error 수준 로그를 남기고 오류 응답을 보낸다.
그런 로그가 한 건도 없다. 즉 handler가 없는 것이 아니라 **요청이 session-a의 dispatcher에
도달하지 않는다.**

정리하면 tombstone 층의 세 후보가 모두 배제됐다. 주소는 실제 rid와 일치하고, 대상은
admitted peer이며, handler는 등록되어 있다. 그런데 212번 보낸 요청을 상대 노드가 하나도
받지 못한다. framework 계층은 모두 정상이므로 유실은 **mesh 전송 계층**이다.

ST-E2는 이 세션에서 다섯 겹을 벗겼다. 조기 Accepted, seal 대상 오류, 응답 유실(NRE),
tombstone 무응답, 그리고 요청 미도달이다. 앞의 셋은 framework 안에서 고쳤거나 원인을
확정했고, 마지막 둘은 framework 밖으로 나간다. 여기서 framework 범위의 조사는 한계에
닿았고, 다음은 core transport 추적이 필요하다.

## 2026-08-01 ST-F3 다음 층 — handoff_backlog는 런타임 진단 마커다

이번 세션의 응답 수정으로 ST-F3의 실패가 다음으로 넘어갔다.

```
Expected runtime evidence marker was not observed:
  handoff_backlog actor=actor-bound-order-...
```

이 마커는 애플리케이션 evidence가 아니라 **런타임 진단**이다.

```csharp
diagnostic?.Invoke(
    $"handoff_backlog actor={actorId} arrival={...} kind={...} request_id={...} flags={...}");
```

`diagnostic`이 nullable callback이므로 그것이 연결되지 않으면 아무것도 나오지 않는다.
러너 헤더도 "Some ST-F handoff markers are runtime diagnostics behind this gate"라고
적고 있다.

따라서 ST-F3의 현재 실패는 둘 중 하나다. 진단 callback이 연결되지 않아 마커가 나오지
않거나, backlog 자체가 일어나지 않거나. e2e host는
`ConfigureDispatch().Diagnostics.SetLevel(ZLinkDiagnosticsLevel.Normal)`을 설정하는데,
이 마커가 그보다 높은 수준을 요구하는지 확인해야 한다.

구분 방법은 간단하다. 진단 수준을 올려 마커가 나오는지 보면 된다. 나오면 설정 문제이고,
안 나오면 backlog가 실제로 일어나지 않는 것이다.

### handoff_backlog는 debug 플래그 뒤가 아니다 — backlog 자체가 없다

진단 callback의 출처를 따라갔다.

```csharp
private readonly ZLinkActorSessionRegistry _actorSessions = new(
    services,
    runtime.LogActorHandoff,      // ← handoff 진단
    ...);
```

`LogActorHandoff`는 `ILogger` 기반이고 category는 `Zlink.Framework.ActorHandoff`다. 이
category의 Information 로그는 이 세션의 다른 실행에서 실제로 관측됐다
(`message_follow_registered ... entries=1`). 즉 **debug 환경변수 뒤에 숨은 것이 아니라
평범한 Information 로그**이며, 러너는 그 로그를 grep한다.

따라서 앞 절의 두 가능성 중 첫째("진단이 연결되지 않았다")는 배제된다. 남는 것은
**backlog 자체가 일어나지 않는다**는 것이다.

ST-F3는 bound session actor가 이동하는 동안 in-flight frame이 backlog에 담기는지를
검증한다. 그 frame이 담기지 않는다는 뜻이므로, 이동 중 도착한 message가 어떻게 처리되는지
확인해야 한다. 참고로 ST-F2도 backlog 관련이며 `backlog_enqueued`와
`location_committed`의 순서가 뒤집혀 실패한다. 두 시나리오가 같은 영역을 다루므로 함께
볼 만하다.

### ST-F3 backlog 미발생의 조건 — 포착 단계가 아니다

capture 진입부에 그 조건이 검사하는 값들을 찍었다.

```
capture_entry  src_ingress=False  tgt_ingress=False
               direct=True  flags=1  bound_route=False      (2건)
```

두 건뿐이고 둘 다 첫 조건에서 곧바로 빠진다.

```csharp
if (!capturesSourceIngress && !capturesTargetIngress)
    return ZLinkActorHandoffCaptureResult.NotSealed;
```

`capturesSourceIngress`와 `capturesTargetIngress`는 handoff의 현재 phase로 결정된다.
둘 다 false라는 것은 **frame이 도착한 시점에 actor가 포착하는 phase에 있지 않다**는
뜻이다. 그래서 backlog가 생기지 않고 `handoff_backlog` 마커도 나오지 않는다.

`bound_route=False`도 눈에 띈다. ST-F3는 bound session 시나리오인데 포착 후보로 들어온
frame이 bound session route가 아니다. `direct=True`, `flags=1`(request)과 함께 보면,
이 frame들은 시나리오가 기대하는 in-flight bound session message가 아닐 수 있다.

따라서 확인할 것은 둘이다. 시나리오가 보내는 in-flight message가 애초에 이 경로로
오는지, 그리고 온다면 그 시점에 handoff phase가 포착 단계인지다. 전자가 아니라면
frame이 다른 경로로 처리되고 있는 것이고, 후자라면 타이밍 문제다.

### 시나리오가 보내는 in-flight message는 capture 경로로 오지 않는다

ST-F3가 보내는 것은 bound session 위의 **one-way send**다.

```csharp
await context.WaitEvidenceAsync(context.NodeB, [$"ST-F3|{actorId}|joined_wait|{spotId}"]);
await bound.Send(new HandoffPacket("ST-F3", "S1")).Async();
await bound.Send(new HandoffPacket("ST-F3", "S2")).Async();
await context.WaitRuntimeEvidenceAsync(context.NodeA,
    $"handoff_backlog actor={actorId} arrival=1");
```

`joined_wait` 게이트로 handoff를 멈춘 상태에서 S1·S2를 보내고, 그것들이 backlog에
담기기를 기다린다.

그런데 capture 진입 진단에 잡힌 frame은 둘뿐이고 **`flags=1`, 즉 request**다. 같은 코드의
주석이 그 비트를 request로 설명한다("A request keeps its live reply route"). 또 둘 다
`bound_route=False`다.

즉 **S1·S2 one-way send는 capture 경로에 도달하지 않는다.** 잡힌 두 건은 시나리오가
기대하는 그 message가 아니다.

bound session send는 session-a에서 actor-a로 relay되는 경로를 탄다. 이 세션에서 확인한
대로 그 경로에는 이미 결함이 있었고(응답 유실은 고쳤지만 tombstone 요청 미도달은
mesh 계층으로 남았다), S1·S2가 그 경로에서 사라지는지 확인해야 한다. ST-F3의 backlog
미발생이 별개 결함인지, 아니면 bound session relay 문제의 또 다른 증상인지가 갈린다.

### 계수만으로는 갈리지 않는다 — 프레임 단위 대조가 필요하다

S1·S2가 actor node에 도달하는지 기존 진단으로 세어 봤다.

```
session forward_part      2건
actor   inbound_resolve   1건
        capture_entry     2건
```

`forward_part`는 앞선 조사에서 프레임 하나당 두 번 찍혔다(header part와 body part).
그렇게 보면 session이 내보낸 것은 **프레임 하나**인데 시나리오는 S1과 S2 **둘**을 보낸다.
한편 actor 쪽 `inbound_resolve`는 한 건이고 `capture_entry`는 두 건이라 서로 맞지 않는다.

즉 계수만으로는 어느 프레임이 어디서 사라졌는지 특정할 수 없다. 진단이 프레임을 구분하지
않기 때문이다. 다음에는 marker(S1·S2)나 arrival index를 진단에 실어 **프레임 단위로
대조**해야 한다. 지금 상태에서 "하나가 유실됐다"고 단정하면 앞서 여러 번 그랬듯 틀릴 수
있다.

### 프레임 식별자로 확정 — one-way send는 capture 경로에 오지 않는다

진단에 arrival index와 request id를 실어 다시 측정했다.

```
capture_entry ... flags=1 bound_route=False arrival=0 kind=Request request_id=21
capture_entry ... flags=1 bound_route=False arrival=0 kind=Request request_id=21
```

두 건은 서로 다른 프레임이 아니라 **같은 프레임(`request_id=21`)이 두 번 평가된 것**이다.
앞 절에서 계수만 보고 "둘 중 하나가 유실됐다"고 읽을 뻔했는데, 식별자를 실으니 애초에
프레임이 하나뿐이었다.

그리고 그 하나는 `kind=Request`다. ST-F3가 backlog에 담기기를 기대하는 S1·S2는
`bound.Send(...)`로 보내는 **one-way**다. 따라서 **one-way send는 capture 경로에 전혀
도달하지 않는다.** 이것이 `handoff_backlog` 마커가 나오지 않는 직접 원인이다.

남은 질문은 그 one-way send들이 어디까지 가느냐다. session에서 relay되기는 하는지,
actor node에 도착하는지, 도착한다면 capture 이전 어느 단계에서 다른 경로로 빠지는지다.
계수가 아니라 marker(S1·S2)를 진단에 실어야 답이 나온다.

### S1·S4 증거는 어느 actor에도 없다 — 다만 이는 예상된 상태다

actor 양쪽의 evidence를 확인했다.

```
actor-a : S1~S4 증거 0건
actor-b : S1~S4 증거 0건
```

다만 이것만으로는 유실이라고 볼 수 없다. 시나리오는 `joined_wait` 게이트를 잡은 상태에서
S1·S2를 보내고 **backlog에 담기기를** 기다린다. 게이트가 아직 풀리지 않았으므로 이
시점에 handler까지 배달되지 않는 것이 정상이다. 즉 "증거가 없다"는 "backlog에 있다"와
"사라졌다"를 구분하지 못한다.

구분하려면 send가 session에서 actor node로 relay되는 구간에 marker를 실은 진단이
필요하다. 현재 `forward_part`는 프레임을 구분하지 못하고 payload도 해석하지 않으므로
S1·S2를 식별할 수 없다.

이 세션에서 확인한 사실만 정리하면 이렇다. one-way send는 handoff capture 경로에
도달하지 않는다. 그리고 그 send가 session에서 나갔는지, actor node에 도착했는지는 아직
모른다. 계수는 프레임 하나(Request)로 설명되므로 send가 relay된 흔적은 관측되지 않았다.

## 2026-08-01 ST-F3 원인 — 게이트웨이가 HandoffPacket send를 relay하지 않는다

send가 어디서 멈추는지 찾았다. 이미 심어 둔 `session_relay_entry` 진단이 답을 준다.

```
session_relay_entry : 0건
session-a 로그      : 26줄, 오류 없음
```

one-way send가 `RelayToActorAsync`에 **진입조차 하지 않는다.** 그리고 게이트웨이는 오류도
남기지 않는다.

이유는 게이트웨이의 세션 dispatch에 있다.

```csharp
if (await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken)) return;
var targetActorId = payload.Decode<BoundPushReq>().ActorId;
```

등록된 handler는 `BindActorSessionHandler`와 `SessionBindingsHandler` 둘뿐인데 ST-F3가
보내는 것은 `HandoffPacket`이다. 어느 handler도 처리하지 않으므로 `BoundPushReq`로
디코드하는 fall-through로 가고 거기서 조용히 끝난다.

즉 **e2e 게이트웨이가 `HandoffPacket` send를 relay할 준비가 되어 있지 않다.** ST-F3는 그
packet이 backlog에 담기기를 기대하지만 애초에 actor node로 보내지지 않는다. 런타임 결함이
아니라 e2e 하네스의 갭이다.

다만 고칠 방향은 확인이 더 필요하다. 게이트웨이가 `HandoffPacket`도 relay하도록 하는 것이
맞는지, 아니면 시나리오가 다른 방식으로 보내야 하는지다. `HandoffPacket`을 쓰는 다른
시나리오(ST-I4 등)가 어떻게 동작하는지 보면 갈린다.

### 통과하는 시나리오와의 차이 — 전송 경로가 다르다

`HandoffPacket`을 쓰는 시나리오 아홉 중 여섯(ST-A2, ST-B1, ST-F1, ST-F4, ST-F5, ST-H1)이
통과하고 셋(ST-F2, ST-F3, ST-I4)이 실패한다. 통과하는 ST-F1과 실패하는 ST-F3를 나란히
놓으면 차이가 분명하다.

```csharp
// ST-F1 (통과) — node 를 통해 보낸다
await context.SendFromNodeAsync(context.NodeA, actorId, new HandoffPacket("ST-F1", marker));
await context.WaitRuntimeEvidenceAsync(context.NodeA, $"handoff_backlog actor={actorId} arrival=2");

// ST-F3 (실패) — bound session 으로 보낸다
await bound.Send(new HandoffPacket("ST-F3", "S1")).Async();
await context.WaitRuntimeEvidenceAsync(context.NodeA, $"handoff_backlog actor={actorId} arrival=1");
```

둘 다 같은 packet을 보내고 같은 `handoff_backlog` 마커를 기다리는데, **경로가 다르다.**
ST-F1은 `/actors/{id}/send-from-node` 엔드포인트로 actor node에서 직접 보내므로 게이트웨이를
거치지 않는다. ST-F3는 bound session으로 보내므로 게이트웨이의 세션 dispatch를 지나야
하고, 거기서 `BoundPushReq` fall-through에 걸려 사라진다.

즉 backlog capture 자체는 정상 동작한다(ST-F1이 그것을 증명한다). ST-F3가 실패하는 것은
**bound session을 통한 send가 게이트웨이에서 relay되지 않기 때문**이다. 결함의 위치가
런타임의 handoff 처리가 아니라 e2e 게이트웨이의 session dispatch로 확정된다.

ST-F3를 통과시키려면 게이트웨이가 bound session send를 대상 actor로 relay해야 한다. 현재
fall-through는 `BoundPushReq`만 가정하므로, 임의의 application packet을 bound actor에게
전달하는 경로가 필요하다.

### (정정) ST-F2·ST-I4는 같은 계열이 아니다

앞 절에서 "ST-F2·ST-I4도 같은 계열일 가능성이 높아 함께 보는 게 낫다"고 적었다. 확인해
보니 **틀렸다.** 둘 다 `SendFromNodeAsync`를 쓴다.

```csharp
// ST-F2
await context.SendFromNodeAsync(context.NodeA, actorId, new HandoffPacket("ST-F2", marker));
// ST-I4
var oneWay = context.SendFromNodeAsync(caller, actorId, new HandoffPacket(scenario, oneWayMarker));
```

이는 통과하는 ST-F1과 **같은 경로**이며 게이트웨이를 거치지 않는다. 따라서 ST-F3의
게이트웨이 relay 갭과는 무관하다.

세 시나리오의 원인이 각각 다르다.

| 시나리오 | 전송 경로 | 원인 |
|---|---|---|
| ST-F3 | bound session send | 게이트웨이가 relay하지 않음 |
| ST-F2 | SendFromNode | `backlog_enqueued`와 `location_committed` 순서 역전 |
| ST-I4 | SendFromNode | external transport gate가 응답 방향을 잘못 감시 |

묶어서 고칠 수 있는 관계가 아니므로 각각 다뤄야 한다. 전송 경로를 확인하지 않고 packet
종류만 보고 묶으려 했던 것이 오류였다.

## 2026-08-01 ST-B5는 이 머신에서 실행할 수 없다 — inotify가 동작하지 않는다

ST-B5를 재실행했으나 여전히 `inotify_add_watch failed` (`Errno 28`, ENOSPC)로 실패한다.
앞서 "일시적 소진"으로 적었는데 그것도 틀렸다. 최소 재현으로 확인했다.

```python
libc.inotify_init1.argtypes=[ctypes.c_int]
libc.inotify_add_watch.argtypes=[ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
fd = libc.inotify_init1(os.O_CLOEXEC)      # 성공, fd=3
libc.inotify_add_watch(fd, b'/tmp', 2)     # -1, errno=28
```

`/tmp`에 대한 watch조차 실패하며, `argtypes`를 명시해도 같다. 따라서 ctypes 마샬링
문제도 `kill_on_file_marker.py`의 버그도 아니다. 자원 여유도 충분하다.

```
inotify instance 사용량 : 20
max_user_instances      : 1024
max_user_watches        : 524288
```

즉 **이 머신(WSL2)에서 inotify watch를 추가할 수 없다.** ST-B5는 `kill_on_file_marker.py`가
로그 파일에 marker가 나타나는 순간 node를 죽이는 방식이라 inotify에 의존한다. 따라서
이 시나리오는 코드 상태와 무관하게 여기서 실행할 수 없다.

이는 프레임워크 결함이 아니며 고칠 대상도 아니다. 다른 환경에서 실행하거나, marker 감시를
polling으로 바꾸면 이 제약을 벗어난다. 후자는 하네스 변경이므로 필요하면 별도로 판단한다.

## 2026-08-01 ST-F2 실제 순서 — commit이 먼저이고 backlog가 뒤따른다

러너가 요구하는 순서는 `backlog_enqueued`가 `location_committed`보다 먼저다. 로그의 실제
순서는 반대다.

```
location_committed  actor=actor-inflight-overtake-... spot=spot-inflight-overtake-...
backlog_enqueued    actor=actor-inflight-overtake-... arrival=0
backlog_enqueued    ... arrival=1
backlog_enqueued    ... arrival=2
```

세 건의 backlog는 시나리오가 보낸 P1·P2·P3이고, 모두 **location commit 뒤에** 담긴다.
ST-F2가 "DirectOvertakePrevention"으로 막으려는 것이 바로 이 상황이다. commit이 먼저
일어나면 그 사이에 도착한 직접 전달 packet이 backlog에 담긴 packet을 추월할 수 있다.

`location_committed`는 두 곳에서 나오는데, 로그의 `spot=` 필드로 보아
`ZLinkSpotActivationActors`의 것이다.

확인할 것은 둘이다. 런타임이 실제로 commit을 먼저 하는 것이 계약 위반인지, 아니면
시나리오가 packet을 보내는 시점이 commit 이후여서 순서가 그렇게 보이는지다. 전자라면
런타임 결함이고 후자라면 시나리오 타이밍 문제다. 두 marker의 시각을 packet 전송 시각과
대조하면 갈린다.

## 2026-08-01 ST-F2는 시나리오가 통과한다 — 러너의 사후 검사만 실패한다

시각과 클라이언트 결과를 함께 보니 성격이 달라진다.

```
client.stdout : operation SpotActorTransfer.ST-F2 passed
client.stderr : 0 바이트
러너          : Marker order failed ... backlog_enqueued=238, location_committed=207
```

즉 **시나리오의 모든 단언은 통과**하고, 러너가 실행 뒤에 하는 marker 순서 검사만 실패한다.

두 marker가 서로 다르다는 점이 열쇠다. 시나리오가 기다리는 것은 `handoff_backlog`이고,
러너가 검사하는 것은 `backlog_enqueued`다.

```csharp
// 시나리오: 게이트를 풀기 전에 capture 를 확인한다
await context.WaitRuntimeEvidenceAsync(context.NodeA,
    $"handoff_backlog actor={actorId} arrival=1");
await context.ReleaseJoinedGateAsync(context.NodeB, spotId);
```

```bash
# 러너: 다른 marker 로 순서를 검사한다
require_marker_order actor-inflight-overtake- backlog_enqueued location_committed
```

시각도 이를 뒷받침한다. `location_committed`가 23:43:43.412이고 세 건의
`backlog_enqueued`가 23:43:43.609에 **동시에** 찍힌다. 즉 `backlog_enqueued`는 handoff
중의 capture가 아니라 commit 뒤 target 쪽에서 프레임을 actor queue에 넣는 시점의
marker로 보인다. 그렇다면 commit 뒤에 오는 것이 자연스럽다.

따라서 러너의 검사가 요구하는 순서(`backlog_enqueued` → `location_committed`)가 실제
설계와 맞지 않을 가능성이 있다. 확인할 것은 `backlog_enqueued`가 두 곳
(`ZLinkSpotActivationActors`와 `ZLinkActorHandoffState`)에서 나오는데 러너가 어느 쪽을
의도했는지다. capture 쪽을 의도했다면 marker 이름을 `handoff_backlog`으로 바꿔야 하고,
enqueue 쪽을 의도했다면 순서 기대가 뒤집혀야 한다.

시나리오가 통과하므로 런타임 동작 자체에는 문제가 없어 보인다.

### 두 marker는 같은 파일에서 나온다 — 순서 기대의 근거는 아직 불명

로그의 `backlog_enqueued`에는 `trailing=true`가 없다. 따라서
`ZLinkSpotActivationActors`(trailing 표시를 붙이는 쪽)가 아니라
`ZLinkActorHandoffState`(388행)에서 나온 것이다. 시나리오가 기다리는 `handoff_backlog`도
같은 파일(320행)에 있다.

즉 두 marker가 handoff 상태 관리라는 같은 영역에서 나오는데도, 하나는 commit보다 먼저
(시나리오가 그것을 기다린 뒤 게이트를 푸므로) 나오고 다른 하나는 commit보다 200ms 뒤에
나온다. 두 지점이 각각 handoff 수명주기의 어느 단계인지 확인해야 러너의 순서 기대가
타당한지 판단할 수 있다.

현재까지 확실한 것만 정리하면 이렇다. ST-F2의 시나리오 단언은 모두 통과한다. 러너의
사후 검사만 실패한다. 그 검사가 비교하는 두 marker는 실제로 commit을 사이에 두고
나뉘어 나온다. 이것이 런타임 결함인지 검사의 기대가 잘못된 것인지는 388행이 속한
단계를 확인해야 갈린다.

### 두 marker의 단계가 다르다 — 러너의 기대가 성립하지 않는다

388행이 속한 메서드를 확정했다.

```
227행 : TryCapture(...)   → handoff_backlog   (320행)
332행 : Import(...)       → backlog_enqueued  (388행)
```

`TryCapture`는 source에서 handoff 중 도착한 프레임을 붙잡는 단계이고, `Import`는 target이
그 프레임들을 받아들이는 단계다. 이름 그대로 **capture가 먼저이고 import가 나중**이며,
그 사이에 authority commit이 있다.

시나리오가 `handoff_backlog`(capture)을 기다린 뒤 게이트를 푸는 것도 이 순서와 맞는다.
capture는 commit 전에 끝나야 하고, import는 commit 뒤 target에서 일어난다.

따라서 러너의 검사는 성립하지 않는다.

```bash
require_marker_order actor-inflight-overtake- backlog_enqueued location_committed
```

`backlog_enqueued`는 `Import` 단계의 marker이므로 `location_committed`보다 **뒤에 오는
것이 정상**이다. 이 검사는 capture 단계의 marker(`handoff_backlog`)를 의도했을
가능성이 크다. ST-F1에 대해서는 두 marker의 존재만 요구하고 순서는 보지 않는데
(`require_runtime_marker`), ST-F2에만 순서를 요구하면서 marker를 잘못 지목한 것으로 보인다.

정리하면 ST-F2는 런타임 결함이 아니다. 시나리오 단언은 모두 통과하고, 러너의 사후 검사가
capture 대신 import marker를 commit과 비교한다. 수정은 러너의 검사에서 marker 이름을
`handoff_backlog`으로 바꾸는 것이다. 다만 러너 검사의 원래 의도를 확인한 뒤 고치는 것이
안전하다.

### (정정) marker 이름을 바꿔도 순서는 뒤집히지 않는다

앞 절에서 "러너가 capture marker를 의도했을 것"이라 보고 검사를 `handoff_backlog`으로
바꿔 시험했다. **여전히 실패한다.**

```
handoff_backlog=130, location_committed=108
```

capture marker조차 `location_committed`보다 뒤에 나온다. 따라서 "capture는 commit
앞, import는 commit 뒤"라는 앞 절의 정리는 이 실행을 설명하지 못한다. 러너 수정은
원복했다.

가능한 설명이 하나 있다. `location_committed`는 transfer뿐 아니라 actor 생성이나 최초
membership 확정에서도 나올 수 있다. 러너의 `require_marker_order`는 각 marker의 **첫
출현**을 비교하므로, 첫 `location_committed`가 setup 단계의 것이라면 어떤 handoff marker와
비교해도 항상 앞선다. 그렇다면 marker 이름을 무엇으로 바꾸든 이 검사는 통과할 수 없다.

확인하려면 `location_committed`가 그 실행에서 몇 번 나오는지, 그리고 각각 어느 단계인지
봐야 한다. 지금 상태에서 러너를 고치면 또 헛수고가 되므로 여기서 멈춘다.

이 세션에서 같은 실수를 반복하고 있다. 그럴듯한 설명을 세우고 그에 맞춰 고친 뒤 실패로
확인하는 순서다. 고치기 전에 그 설명이 예측하는 바를 먼저 측정했다면 한 회차를 아꼈다.

### (측정) setup commit 설명도 틀렸다 — commit은 한 번뿐이고 capture보다 먼저다

앞 절의 "첫 `location_committed`가 setup 단계의 것일 수 있다"는 설명을 고치기 전에
측정했다. **틀렸다.**

```
location_committed  01:23:18.995   (전체 실행에서 단 1건)
handoff_backlog     01:23:19.084   (첫 건, 89ms 뒤)
```

`location_committed`는 그 실행에서 한 번만 나오고, capture marker보다 89ms 먼저다.
따라서 setup 단계의 commit이 섞인 것이 아니라 **transfer의 commit이 실제로 capture보다
먼저 일어난다.**

이는 ST-F2("DirectOvertakePrevention")가 잡으려는 바로 그 상황이고, 러너의 검사가 옳다.
앞서 "러너가 marker를 잘못 지목했다"고 적은 것도 함께 정정한다. 지목은 옳았고 순서 위반이
실재한다.

다만 한 가지가 남는다. 시나리오의 단언은 모두 통과하며, 그중에는 관측 가능한 순서 검증도
있다.

```csharp
await context.AssertEvidenceOrderAsync(context.NodeB, actorId, "handoff_packet", ["B1", "B2", "D1"]);
```

즉 **내부 marker 순서는 어긋나는데 외부로 관측되는 순서는 맞다.** 이 실행에서는 추월이
실제로 일어나지 않았다는 뜻이다. 러너의 검사는 외부 결과가 우연히 맞는 경우까지 걸러내는
더 엄격한 내부 불변식이고, 그 불변식이 깨져 있다.

정리하면 ST-F2는 런타임의 순서 문제이며 검사가 그것을 정확히 잡아낸다. 이번 세션에서
이 항목에 대해 두 번 잘못된 설명을 세웠고 두 번 다 측정이 뒤집었다. 세 번째 설명도
세우기 전에 측정한 것이 이번의 유일한 개선이다.

### 측정한 시간 순서

ST-F2 실행의 관련 marker를 시각순으로 정리하면 이렇다.

```
01:23:18.995  location_committed
01:23:19.084  handoff_backlog   (1)
01:23:19.085  handoff_backlog   (2)
01:23:19.169  handoff_backlog   (3)
01:23:19.234  backlog_enqueued  ×3
```

commit → capture(3건) → enqueue(3건) 순서다. capture는 commit 89ms 뒤에 시작한다.

여기서 설명이 필요한 지점이 하나 있다. 시나리오는 `joined_wait` 게이트를 잡은 상태에서
B1·B2를 보내고 `handoff_backlog arrival=1`을 기다린 **뒤에** 게이트를 푼다. 즉 게이트가
풀리기 전에 capture가 일어나야 하고, commit은 게이트가 풀린 뒤에 와야 자연스럽다. 그런데
commit이 capture보다 앞선다.

따라서 `location_committed`가 실제로 무엇을 표시하는지 확인해야 한다. transfer의 authority
commit이라면 게이트보다 앞설 수 없고, 다른 것(예: 이전 membership 확정이나 target 예약)을
표시한다면 앞서는 것이 자연스럽다. 이 세션에서 이 항목에 대해 설명을 먼저 세워 두 번
틀렸으므로, 이번에는 `location_committed` 방출 지점의 코드를 읽어 무엇을 표시하는지
확인한 뒤에 판단한다.

### location_committed의 정체 — same-node join의 commit이다

코드를 읽으니 그 marker가 무엇을 표시하는지 분명하다.

```csharp
//  Same-node join commits the location authority just as
//  a cross-node handoff does, so it reports the commit on
//  the same channel. Using the debug console here left
//  the marker invisible to anything reading ILogger.
_runtime.LogActorHandoff(
    $"location_committed actor={actor.Context.ActorId} spot={SpotId}");
```

이것은 `ZLinkSpotActivationActors`의 **same-node join** 경로이고, 주석이 스스로
"cross-node handoff와 마찬가지로 같은 채널에 commit을 보고한다"고 밝힌다. 즉 이 marker는
transfer 전용이 아니라 **같은 노드 안에서의 join commit도 함께 찍는다.**

이것이 시간 순서를 설명한다. ST-F2는 actor를 만들고 entry spot에 join시키는 setup을
거치는데, 그 same-node join이 `location_committed`를 남긴다. 그 뒤에 transfer가 시작되고
capture가 일어난다. 따라서 commit(setup) → capture(transfer) 순서가 관측된다.

앞 절에서 "location_committed는 그 실행에서 한 번뿐이므로 setup 것이 섞인 게 아니다"라고
적었는데, **한 번뿐이어도 그 한 번이 setup의 것**일 수 있다. 횟수만 보고 단계를 판단한
것이 오류였다.

정리하면 러너의 검사는 transfer의 commit을 의도하지만 marker가 same-node join에서도
나오므로, 첫 출현을 비교하는 방식으로는 transfer 순서를 검증할 수 없다. ST-F2의 실패는
런타임의 순서 위반이 아니라 **marker가 두 단계를 구분하지 못하는 것**이다. 시나리오
단언이 모두 통과하는 것도 이와 맞는다.

### (재정정) same-node join 설명도 확실하지 않다

앞 절에서 "그 `location_committed`는 setup의 same-node join 것"이라고 적었다. 확인해 보니
그렇게 단정할 수 없다.

```
로그의 값 : spot=spot-inflight-overtake-8b4beebdc01348c788d8eac934c0a144
시나리오  : CreateSpotAsync(NodeB, spotId, "delay-joined")   ← transfer 대상 user spot
            CreateActorAsync(NodeA, actorId, ...)            ← actor 는 NodeA
```

logged spot id는 setup에서 만든 entry spot이 아니라 **transfer의 target user spot**이다.
따라서 이 marker는 setup이 아니라 **transfer 과정에서 target이 그 user spot에 대해 남긴
commit**일 가능성이 크다. 방출 지점의 주석("Same-node join commits ... just as a
cross-node handoff does")은 코드 경로가 공유된다는 뜻이지 이 실행이 same-node라는 뜻이
아니다.

이 항목에 대해 지금까지 네 번 설명을 세웠고 네 번 다 뒤집혔다. 러너가 marker를 오지목했다,
setup commit이 섞였다, 런타임 순서 위반이다, same-node join의 것이다. 매번 다음 측정이
반증했다.

따라서 여기서 설명을 더 만들지 않는다. 확실한 것만 남긴다.

- `location_committed`는 그 실행에서 한 번 나오고 값은 transfer의 target user spot이다.
- 그것이 첫 `handoff_backlog`보다 89ms 먼저다.
- 시나리오의 단언은 전부 통과하며 관측 가능한 packet 순서도 맞다.
- 러너의 사후 검사만 실패한다.

`location_committed`가 transfer 수명주기의 정확히 어느 시점을 표시하는지, 그리고 러너가
그 marker로 무엇을 보장하려 했는지는 이 marker와 검사를 설계한 쪽이 답해야 한다.
로그만으로 추론하는 것은 이 항목에서 네 번 실패했다.

## 2026-08-01 dotnet 나머지 스위트 — 기동은 하는데 연결이 성립하지 않는다

메모리 한도 수정으로 기동 장벽이 사라진 뒤 PubSub을 다시 조사했다.

```
sub-1 : 01:37:42.606  Now listening on http://127.0.0.1:38789
        01:37:42.615  Application started
pub-a : 01:37:45.255  Now listening
        01:37:45.263  Application started
러너  : Timed out waiting 3s for sub-1 publisher connection evidence: event=ConnectionReady
```

노드는 모두 정상 기동하고 각각 0.9초 간격으로 뜬다. 대기가 3초이므로 처음에는 기동이
느려 시간이 모자란 것으로 보였다. 그러나 `sub-1`의 evidence 파일은 **0줄**이다. 즉
`ConnectionReady`가 늦게 오는 것이 아니라 **끝내 오지 않는다.**

따라서 타이밍 문제가 아니라 연결 자체가 성립하지 않는다. 12개 스위트가 같은 모양
("route readiness"/"connection evidence" 3초 타임아웃)으로 실패하므로 공통 원인일
가능성이 있다.

이 스위트들은 이번 세션 이전부터 실패해 왔고(`00959010f4`가 기동 장벽을 남긴 시점부터
확인 불가였다), 기동이 풀린 지금 처음으로 다음 단계를 볼 수 있게 됐다. 연결이 왜
성립하지 않는지는 아직 모른다. `SpotActorTransfer`는 같은 머신에서 정상 연결되므로
머신이나 transport 전반의 문제는 아니고, 이 스위트들의 구성에서 갈리는 지점이 있을
것이다.

### PubSub 연결 실패의 정황 — 구독자가 발행자보다 먼저 뜬다

`sub-1`의 stdout은 13줄뿐이고 mesh·peer·connect 관련 기록이 하나도 없다. 기동 후 6초 뒤
drain으로 끝난다.

구독자는 발행자 endpoint로 연결한다.

```csharp
var subscriber = framework.AddFanoutChannel(PubSubNames.Channel)
    .Connect(options.PublisherEndpoint);
```

그런데 러너의 기동 순서는 구독자가 먼저다.

```bash
173:  start_server "sub-$sub" "$SUBSCRIBER_PROJECT" ...
185:  start_server pub-a "$PUBLISHER_PROJECT" ...
```

시각도 이를 확인한다. 구독자 셋이 01:37:42.6, 43.5, 44.4에 뜨고 발행자가 45.3에 뜬다.
즉 **구독자가 연결하려는 시점에 발행자가 아직 listen하지 않는다.**

여기서 갈린다. connect가 재시도하는 계약이면 발행자가 뜬 뒤 연결이 성립해야 하고, 그렇지
않다면 최초 실패로 끝난다. 관측된 결과는 후자에 가깝지만, 연결 시도 자체가 로그에 없어
재시도 여부를 확인할 수 없다. 다음은 connect의 재시도 계약을 스펙에서 확인하고, 필요하면
기동 순서를 바꿔 대조하는 것이다.

`SpotActorTransfer`가 같은 머신에서 정상 연결되는 것은 그쪽이 auto-connect와 mesh peer
admission을 쓰기 때문일 수 있다. 두 스위트가 연결을 맺는 방식이 다르므로 단순 비교는
성립하지 않는다.

### 스펙에 부합하는 후보 — NotRequired 종료라면 재시도하지 않는다

`07-channel-topology.ko.md`에 조용한 연결 생략을 규정한 조항이 있다.

> "Manual peer 양쪽이 Object Client이고 RouteMesh Channel Server membership도 없으면
> 설정 오류로 host 전체를 중단하지 않는다. 해당 connection intent만 `NotRequired`
> terminal로 끝내고 ready peer와 liveness 대상에서 제외한다. **같은 설정을 계속
> 재시도하지 않는다.** Monitoring에는 peer를 `NotRequired`로 남겨 정상적인 연결
> 생략임을 보여 준다. 연결이 필요하지만 ready connection이 없는 `NotConnected`와 같은
> 장애로 집계하지 않는다."

관측된 것과 잘 맞는다. 연결이 성립하지 않고, 재시도 흔적이 없고, 오류 로그도 없고,
`ConnectionReady`가 끝내 오지 않는다. 조항이 이를 **정상적인 생략**으로 규정하므로 조용한
것도 설명된다.

다만 이것은 아직 **가설**이다. 이 세션에서 그럴듯한 설명을 세우고 그에 맞춰 고쳤다가
되돌린 일이 여러 번 있었으므로, 검증 방법을 함께 적는다. 이 가설이 참이라면 monitoring에
해당 peer가 `NotRequired`로 남아 있어야 한다. 그것을 확인하기 전에는 코드를 고치지 않는다.

가설이 맞다면 PubSub e2e host들이 Object Client로만 구성되고 RouteMesh Channel Server
membership이 없어서, v11에서 이 조항이 도입·강화되며 연결이 생략되기 시작한 것이 된다.
그렇다면 수정은 e2e host 구성이지 런타임이 아니다.

### 가설을 뒷받침하는 구조 차이

두 스위트의 host 구성을 대조했다.

```
SpotActorTransfer : mesh28.Objects().Server()      ← Server role 선언
PubSub            : (role 선언 없음)
```

`SpotActorTransfer`는 mesh object role을 `Server`로 선언하고, PubSub의 publisher와
subscriber는 어떤 role도 선언하지 않는다. 앞 절의 스펙 조항이 요구하는 조건("양쪽이
Object Client이고 RouteMesh Channel Server membership도 없으면")과 방향이 맞는다. 같은
머신에서 `SpotActorTransfer`만 연결되는 것도 이것으로 설명된다.

다만 role을 선언하지 않았을 때의 기본값이 무엇인지는 아직 확인하지 않았다. 기본이
`Client`라면 조항의 조건이 성립하고, 다른 값이라면 성립하지 않는다. 따라서 이 대조는
가설을 **뒷받침**하지만 확정하지는 않는다.

확정하려면 둘 중 하나다. role 기본값을 spec이나 구현에서 확인하거나, monitoring에서 peer
상태가 `NotRequired`인지 보는 것이다. 전자가 더 값싸므로 그쪽을 먼저 본다.

### 가설 기각: PubSub은 mesh peer를 쓰지 않는다

앞 두 절의 `not_required` 가설은 틀렸다. PubSub e2e는 classic fanout channel을 쓴다.
Host factory가 `framework.AddFanoutChannel(...).Connect(endpoint)`로 구성하고 주석에도
"Classic fanout uses no location store (config-3)"라고 적혀 있다. MeshNode도 peer도
등장하지 않으므로 peer state 자체가 성립하지 않는다. `Objects().Server()` 유무를 비교한
것은 애초에 이 스위트와 무관한 축이었다.

실제 원인은 다른 것이었다. `f14abf3c37f`가 subscriber host에서
`AddZLinkMonitoring(monitor => monitor.AddSocketEvents(..., ConnectionReady, ...))`
등록을 지웠는데 `run_e2e.sh`는 여전히 `event=ConnectionReady` 증거를 기다렸다. 그 API는
지금 framework 어디에도 없다. 삭제는 의도된 것이다. spec 24 §3이
"변화 stream의 각 항목은 일부 field만 담은 event가 아니라 완전한 status다. Nullable
field를 조합하는 범용 event DTO는 제공하지 않는다"고 정한다. 즉 낡은 쪽은 runner다.

대체 barrier로 status 조회를 쓸 수는 없었다. spec 24 §2.2는 topology status의 범위를
RouteMesh·ClientServer·automatic fanout으로 한정하고, `ZLinkFanoutRuntimeService`도
subscriber가 automatic discovery인 channel만 등록한다. Manual channel을 물으면
"not ready"가 아니라 오류다. 그래서 spec 29 §170이 정의한 classic fanout readiness,
곧 "첫 정상 application record 수신"을 그대로 barrier로 삼았다.

이 조사가 뒤집은 것이 하나 더 있다. "12개 스위트가 하나의 공통 원인으로 막혀 있다"는
추정은 근거가 없다. `event=ConnectionReady`를 기다리는 runner는 PubSub 하나뿐이다
(SubmitAdmission의 `event=` 표지는 application 수준이라 무관하다). 스위트별로 개별
확인해야 한다.

### classic fanout subscriber가 liveness beacon을 application record로 처리했다

barrier를 고치자 PS-A1~A3이 통과하고 PS-A4에서 다음 증거가 나왔다.

```
dispatch-error|surface=Channel|kind=Publish|reason=InvalidFrame|action=Drop|channel=events|topic=ZLF1
```

`ZLF1`은 `ZLinkFanoutLivenessProtocol.Topic`의 예약 topic이다. Publisher는 5초마다 같은
PUB socket으로 beacon을 보내므로 manual subscriber도 이것을 받는다.
`ZLinkChannelReceiveLoop.RunFanoutConnectionLoopAsync`(automatic)는 예약 topic을
걸러내지만 `RunSubscriberLoopAsync`(manual)는 걸러내지 않아 beacon이 application
dispatch로 흘러갔다.

spec 29 §4는 Classic fanout 전체(automatic·manual 양쪽)에 적용되며 "Beacon은
application event가 아니다. Application queue나 fanout handler에 전달하지 않는다.
Application message trace를 만들지 않는다"고 정한다. Manual loop는 세 항목을 모두
어겼다. 예약 topic 처리를 automatic loop와 같게 맞췄다.

남은 gap이 하나 있다. Manual mode에는 spec 29 §4의 15초 inbound timeout과 재연결이
없다. Automatic mode에만 watchdog이 있다. 이번 수정 범위 밖이므로 별도로 남긴다.

### PS-A4는 native library가 낡아서 막혀 있다

Beacon 수정 뒤 PS-A4는 다른 지점에서 멈춘다. 한 subscriber의 transport를 proxy로 막은
직후의 publish가 실패한다.

```
Fanout publish timed out before local admission completed.  (1003ms)
```

Classic fanout은 손실 허용이므로 느린 subscriber 하나 때문에 publish가 실패해서는 안
된다. 원인은 framework가 아니다.

```
core/src/runtime/sockets/pubsub/xpub.cpp:  -_lossy (false),  +_lossy (true),   (2026-07-31 23:17 수정)
bindings/dotnet/native/linux-x64/libzlink.so.11.1.0                            (2026-07-31 14:52 빌드)
e2e/PubSub/Server/Publisher/bin/.../libzlink.so.11.1.0                         (2026-07-31 14:52 빌드)
```

로드되는 native library가 lossy 기본값 변경보다 먼저 빌드됐다. nodrop 상태에서는 pipe가
차면 `EAGAIN`이 나고, submitter가 writability를 기다리다 send timeout에 걸린다. 관측된
증상과 정확히 맞는다. Core를 다시 빌드해 native library를 갱신해야 PS-A4를 판정할 수
있다.

### PS-A4는 core lane 결함이다: 죽은 pipe 하나가 fanout 전체를 막는다

Native library를 core 최신 소스로 다시 빌드해 `_lossy = true`를 반영하고 nupkg
캐시까지 교체한 뒤에도 PS-A4는 같은 곳에서 실패한다.

```
Fanout publish timed out before local admission completed.  (1003ms)
```

`libzlink.so.11.1.0` 02:02 빌드가 실제로 로드된 것을 publisher bin에서 확인했다. 즉
lossy 기본값은 반영됐고 그것만으로는 부족하다.

코드 경로는 다음과 같다.

```
xpub_t::xsend            admitted = (admission == ready) || (_lossy && admission == hwm_full)
dist_t::check_hwm        matching pipe 중 하나라도 ready가 아니면 그 상태를 결과로 올린다
pipe_t::check_hwm_for_message
                         _state != active  -> pipe_message_admission_inactive
                         그 외 포화 경로   -> pipe_message_admission_hwm_full
```

`lossy`가 용서하는 것은 `hwm_full` 하나뿐이다. PS-A4의 `NetworkFaultProxy.Block()`은
양쪽 socket을 `Dispose()`로 끊으므로 publisher 쪽 pipe가 active를 벗어나고
`inactive`가 된다. `dist_t::check_hwm`은 matching pipe 전체에서 가장 나쁜 상태를
돌려주므로, 끊긴 구독자 하나가 나머지 모든 구독자에 대한 publish까지 `EAGAIN`으로
만든다. Submitter는 writability를 기다리다 send timeout에 걸린다.

이것은 spec 29 §4의 손실 허용 계약과 어긋난다. 스펙은 포화된 subscriber에 대해 record를
버린다고 정하지, 죽은 subscriber 하나가 channel 전체의 publish를 실패시킨다고 정하지
않는다. 받을 수 없는 pipe는 건너뛰어야 할 대상이지 전체를 막을 근거가 아니다.

수정 위치는 `xpub_t::xsend`와 `dist_t::check_hwm`으로 core lane이다. 이 세션의 커밋은
`framework/`로 한정해 왔고 core working tree에는 v11 lossy 작업이 진행 중이므로 여기서는
고치지 않고 남긴다. PS-A1~A3은 통과하고 PS-A4만 이 결함에 걸려 있다.

### dotnet e2e 스위트 전수 1차 실패 목록

"12개 스위트가 하나의 원인으로 막혀 있다"는 추정이 무너진 뒤, 남은 스위트를 직렬로 한 번씩
돌려 각각의 첫 실패를 확보했다.

| 스위트 | 첫 실패 |
|---|---|
| PubSub | PS-A1~A3 통과, PS-A4는 core lossy admission 결함 |
| StoreFailure | SF-A1·SF-A2 통과, SF-B1이 outage 중 정지 |
| AutomaticTurnDispatch | `play-a to delay-a route readiness` 3초 timeout |
| LocationMessaging | `backpressure-consumer route readiness` 3초 timeout |
| ChannelEgressRouting | runner가 미구현 selector 4건(CH-E2E-03/08, CH-REG-02/05)으로 거부 |
| ObservabilityOps | ObsA1FlowCorrelation |
| RegistrationCodec | evidence 대기 HTTP timeout |
| ResilienceLifecycle | 미분류 |
| RuntimeMonitoring | MonA4AvailabilityTransition |
| SpotService | 미분류 |
| SubmitAdmission | HTTP 500 |
| ToActorMessaging | 미분류 |
| SpotActorTransfer | 15/29 통과 |

### backpressure-consumer는 send HWM 4가 handshake를 굶겨서 막힌다

LocationMessaging의 `backpressure-consumer`만 route readiness에 도달하지 못한다. 같은
manual 구성인 `single-consumer`는 통과한다. 두 host의 차이는 한 줄뿐이다.

```csharp
if (options.TraceLabel == "backpressure-consumer")
    profileMesh.ConfigureRouterSocket().SendHighWaterMark = 4;
```

측정으로 확인했다. 이 값만 4000으로 올리면 readiness barrier를 통과하고 시나리오 단계까지
진행한다. 되돌리면 다시 막힌다. 즉 message 4개짜리 send HWM이 mesh handshake와 liveness
frame까지 함께 제한해서 peer가 Ready로 가지 못한다.

`ConfigureRouterSocket().SendHighWaterMark`는 Core socket 옵션을 그대로 노출하는
knob이고, spec 06이 정의하는 byte 기반 Application HWM과는 다른 축이다. 공통 spec에는
이 knob에 대한 조항도, handshake·liveness frame을 application send HWM에서 제외한다는
조항도 없다. 따라서 다음 둘 중 어느 쪽이 옳은지 스펙만으로는 판정할 수 없다.

- e2e가 backpressure를 만들려고 고른 값이 너무 낮다. Framework의 admission 계층으로
  backpressure를 만들어야 한다.
- Handshake와 liveness frame은 application이 설정한 send HWM의 영향을 받지 않아야 한다.

측정 편집은 되돌렸다. 판정 근거를 만들기 전에는 고치지 않는다.

### 세 스위트는 컴파일이 되지 않아 시나리오에 도달한 적이 없다

ResilienceLifecycle·SpotService·ToActorMessaging은 host project가 v11 대상으로 빌드되지
않는다. runner가 `dotnet build ... >/dev/null`로 빌드 출력을 버려서 아무 메시지 없이
실패했다. 드리프트는 넷이다.

| 증상 | v11 |
|---|---|
| `SendHighWaterMark = int` | `ulong` |
| `Zlink.Framework.Contracts.Eventing` | 네임스페이스 삭제 |
| `EnableActorDispatch("mesh")` | 인자 없음 |
| `ActorRef.Generation` | `ActorRef.ObjectGeneration` |

### fixed RID + automatic discovery는 세 스위트에 걸친 공통 결함이다

spec 13 §3.3은 fixed RID를 Location Store와 automatic discovery를 쓰지 않는 explicit
manual topology에서만 허용한다. StoreFailure·ResilienceLifecycle·ToActorMessaging이
이를 어겨 host가 기동 즉시 죽는다. 나머지 스위트의 `SetRoutingId` 호출은 실제 manual
topology라 정상이다(로그에 해당 오류 없음).

ToActorMessaging은 같은 편집으로 끝나지 않는다. Caller가
`PeerConnections.Connect(RoutingId.From(options.ActorRid), endpoint)`로 actor peer를
**RID로 지정해** 연결한다. Actor host가 automatic RID를 받으면 caller가 그 값을 미리 알
수 없다. Topology 자체를 어떻게 바꿀지 결정이 필요하다.

### RL-A1: 선택 가능한 target이 없을 때 `Unavailable`이 온다

Provider가 내려간 창에서 select-one 호출이 `Unavailable`로 끝난다. spec 08 §7 표는
"ChannelName의 선택 가능한 target이 없다 → `NotFound`로 끝난다"로 정한다. Framework의
매핑 자체는 충실하다. Core가 `NotConnected`를 돌려주고
`ZLinkRequestFailureMapper`가 그것을 `Unavailable`로 옮긴다. 즉 Core member 집합에 죽은
provider가 남아 있다는 뜻이다.

시나리오는 **registry** host의 topology에서 api-b row가 사라지기를 기다린 뒤 consumer에
요청한다. Consumer 자신의 수렴은 기다리지 않는다. 따라서 다음 둘 중 하나다.

- Consumer가 아직 수렴하지 않은 시점을 시나리오가 관측한다(시나리오 문제).
- Row가 사라져도 Framework가 Core member를 회수하지 않는다(런타임 문제).

Consumer 자신의 topology를 기다리도록 바꿔 보면 갈린다. 아직 하지 않았다.

### AutomaticTurnDispatch: play → delay 요청이 나가서 도착하지 않는다

`play-a`가 `await.delay` channel로 보내는 readiness 요청이 submit된 뒤 timeout으로
끝난다.

```
System.TimeoutException: ZLink request timed out before completion.
   at ZLinkAsyncSubmitter.SubmitRequestCoreAsync
```

`delay-a`는 `/health` 외에 아무것도 받지 못한다. 다음을 배제했다.

- Barrier 시간 부족이 아니다. 3초를 30초로 늘려도 같다.
- 기동 오류가 아니다. 양쪽 host stderr가 비어 있다.
- `NotRequired` 정책이 아니다. play의 delay mesh는 Object role이 `None`이고
  delay-a는 Channel Server membership을 가지므로 `IsNotRequired`가 거짓이다.

즉 manual peer 연결이 성립하는데도 request가 상대에 도달하지 않는다. LocationMessaging의
`backpressure-consumer`와 증상 계열이 같지만 그쪽 원인인 send HWM 설정은 여기에 없다.

### RL-A1 판정: barrier가 부족했고, 그 아래에 계약 격차가 하나 더 있다

앞 절에서 갈리지 않았던 두 갈래 중 barrier 쪽이 먼저 참으로 드러났다. `registry`와
`consumer`는 같은 host였으므로 "consumer가 아직 수렴하지 않았다"는 표현은 정확하지
않았다. 실제 문제는 `/topology/wait`의 `ExpectedCount == 0` 분기가 **Location Store row가
사라진 것만** 확인하고 mesh ready set은 확인하지 않은 것이다. Row가 사라져도 ready
후보에는 남아 있을 수 있다. Barrier가 다음 줄이 필요로 하는 것을 증명하지 못했다.

`ExpectedCount == 0`일 때 해당 role이 ready set에서도 빠졌는지 함께 확인하도록 고쳤다.

그러자 결과가 `Unavailable`에서 `TimeoutException`으로 바뀐다. 이것이 남은 격차다.

```
api-b : ready set에서 제거됨 (barrier가 보장)
api-a : weight 0        (RL-A1이 의도적으로 배제)
=> 선택 가능한 후보 0개
```

후보가 0개인데 submit이 수락되고 1초 operation timeout까지 기다린다. spec 08 §7은
"ChannelName의 선택 가능한 target이 없다 → `NotFound`로 끝난다"로 정하고, timeout을
허용하는 것은 그 다음 행 "알려진 target의 연결이 제한 시간까지 ready가 되지 않는다"
뿐이다. 두 경우는 선택 시점에 구분되어야 한다.

api-a가 살아 있는데도 요청이 처리되지 않은 것은 §3.2 step 4의 weight 0 배제가 동작한다는
증거다. 즉 후보 집합은 비어 있고, 비어 있는데도 실패하지 않고 기다린다.

이 대기는 `ZLinkAsyncSubmitter`의 `failFastNotConnected` 주석이 말하는 connect-window
buffering이다. Rid-addressed router 경로만 fail-fast를 선택하고 channel select-one은
선택하지 않는다. 후보 0개를 즉시 `NotFound`로 끝내려면 선택 시점의 후보 수를 알아야 하며
그 선택은 Core의 `RequestToChannel` 안에서 일어난다. PS-A4와 마찬가지로 core lane이므로
기록만 한다.

### AutomaticTurnDispatch 판정: manual-only RouteMesh에는 select-one 후보가 없다

`/topology/ready`가 실패할 때 mesh status를 함께 돌려주도록 측정을 붙였다.

```
{"ready":false,"diag":"delay-a|Ready ready=True"}
```

Peer는 `Ready`이고 MeshNode도 ready다. 그런데 `RequestToChannel("await.delay", ...)`는
submit된 뒤 timeout으로 끝나고 `delay-a`는 아무것도 받지 못한다. 연결 문제가 아니라
**선택할 후보가 없는** 문제다. RL-A1에서 확인한 것과 같은 증상이다. 후보가 0개일 때
`NotFound`로 끝나지 않고 connect-window buffering으로 기다린다.

후보가 왜 0개인가. spec 08 §3.2의 각주가 답한다.

> RouteMesh 후보는 07 Channel topology §4.2가 **descriptor에 게시하는** Server
> membership이고, 그 set은 remote target이 될 수 있는 membership만 나타내기 때문이다.

Descriptor는 Location Store에 게시하는 정보다(spec 29 §5). `play-a`는 Location Store를
등록하지만 `delay-a`는 등록하지 않는다. 따라서 `delay-a`의 `await.delay` Server
membership은 어디에도 게시되지 않고, play는 peer가 ready인 것만 알 뿐 그 peer가 이
channel의 Server라는 사실을 알 수 없다.

spec 07 §7은 "ChannelName Server는 MeshNode가 ready이고 자신의 weight가 0보다 클 때만 새
select-one target이 된다"고만 적어 descriptor를 언급하지 않는다. 두 조항이 다르게
읽히지만, 관측은 spec 08 각주 쪽과 일치한다.

Delay host에 Location Store를 붙여 확인했다. 그러면 `delay-a`가 후보가 되지만 동시에
`play-a`가 같은 MeshName의 `delay-b`까지 발견하고 `NotConnected`로 남긴다.

```
{"ready":false,"diag":"delay-a-63f6e664|Ready;delay-b-4a84f7f7|NotConnected ready=False"}
```

이 스위트는 `play-a ↔ delay-a`와 `play-b ↔ delay-b`를 **같은 MeshName의 별개 manual
pair**로 쓰려 한다. Store를 붙이면 두 pair가 한 mesh로 합쳐져 의도가 깨진다. 즉 store
추가는 해답이 아니다.

여기서 확정할 수 있는 것과 없는 것을 구분해 둔다. 확정된 것은 관측이다. Peer가 ready여도
descriptor가 없으면 select-one이 target을 찾지 못한다. 확정되지 않은 것은 그것이 옳은
동작인지다. spec 07 §7은 "ready이고 weight가 0보다 크면 select-one target이 된다"고만 적고
descriptor를 조건으로 걸지 않는데, spec 08 §3.2 각주는 후보를 descriptor 게시분으로
한정한다. 관측은 각주 쪽과 일치하지만 두 조항이 다르게 읽히는 것 자체가 spec 정합성
문제다. 스펙 소유자가 어느 쪽이 정본인지 정해야 이 스위트를 어떻게 고칠지 결정할 수 있다.

각주가 정본이면 이 스위트의 구성은 성립하지 않고 설계를 바꿔야 한다(pair마다 MeshName을
나누고 store를 쓰거나, select-one 대신 RID direct로 주소를 지정한다). §7이 정본이면
manual peer의 Server membership을 후보로 잡지 못하는 런타임 쪽이 격차다.

측정 편집(store 주입, prefix 전환, runner 인자, diag 응답)은 모두 되돌렸다.

### 후보 0개 격차는 RL-A1과 AutomaticTurnDispatch에 공통이며 아직 고칠 준비가 안 됐다

앞의 두 절은 같은 결함의 두 사례다. 하나로 묶어 둔다.

메커니즘까지는 확인했다. Core의 `RequestToChannel` submit이
`ZlinkSubmitException(NotConnected)`을 던지고, `ZLinkAsyncSubmitter.IsRetryableSubmitFailure`가
이것을 재시도 대상으로 판단한다. 이 경로에는 `_failFastNotConnected`가 지정되어 있지
않기 때문이다. 그래서 후보가 0개여도 실패하지 않고 operation timeout까지 기다린다.

그런데 여기서 바로 고칠 수 없는 이유가 둘이다.

첫째, fail-fast만으로는 계약을 만족하지 못한다. `failFastNotConnected`가 참이면 submit
실패가 그대로 올라오고 `TryMapSubmitFailure`가 `RouteNotConnected` → `Unavailable`로
옮긴다. spec 08 §7이 요구하는 것은 `NotFound`다. 즉 fail-fast와 매핑을 함께 바꿔야 한다.

둘째, 판별 기준을 어디에 둘지 정해지지 않았다. Framework 쪽 사전 검사(`MeshPeerChannels`로
후보 수를 세고 0이면 즉시 `NotFound`)를 검토했지만, ATD 측정에서 `delay-a`는 `Ready`였다.
그 peer의 channel table이 `await.delay`를 포함하면 사전 검사는 후보가 있다고 판단해
아무것도 바뀌지 않는다. 포함하지 않으면 Core는 이미 후보가 없음을 아는데도 구분 가능한
결과 대신 재시도 가능한 `NotConnected`를 돌려주는 것이 된다. 어느 쪽인지 확인하기 전에는
판별을 framework에 둘 수 있는지조차 알 수 없다.

Mesh outbound submit 전체에 걸리는 gate를 시나리오 하나의 assertion을 위해 두 곳 동시에
바꾸는 것은 지금 근거로 할 일이 아니다. PS-A4와 같이 core lane 소유자와 함께 정할 문제로
남긴다.

### SubmitAdmission: manual peer가 `Connecting`에서 벗어나지 못한다

Runner의 마지막 readiness 호출이 500으로 끝난다.

```
ZLinkFrameworkException: Route channel 'submit-admission.mesh' is not connected
to node 'submit-target' for packet 'RouteReadyRequest'.
  at ZLinkFrameworkRuntime.EnsureKnownRouteMeshPeer
```

`EnsureKnownRouteMeshPeer`가 manual 경로에서 `RequiredNotConnected`로 분류한 결과다. 즉
peer가 등록되어 있으나 `Admitted`가 아니다. 양쪽 view를 함께 찍어 확인했다.

```
{"state":"Connecting","diag":"submit-target|reason=NoReadyPeer"}   <- caller view
(404, 항목 없음)                                                    <- target view
```

Caller는 상대를 `Connecting`으로 잡고 있고 target에는 peer 항목 자체가 없다. 연결이
admission까지 도달하지 못한다.

세 가지를 배제했다.

- Receiver gate가 아니다. Caller를 gate 대신 target endpoint로 직접 연결해도 같다.
- Router socket HWM이 아니다. 이 스위트는 `SendHighWaterMark`와 `ReceiveHighWaterMark`를
  모두 `1`로 두는데, 1000으로 올려도 `Connecting` 그대로다. LocationMessaging의
  `backpressure-consumer`는 같은 증상이 HWM으로 풀렸으므로 두 스위트는 원인이 다르다.
- 시간 부족이 아니다. Readiness 시도를 30회에서 300회로 늘려도(약 90초) 그대로다.

증상 문자열(`Connecting` + `NoReadyPeer`)은 LocationMessaging과 같지만 원인은 공유하지
않는다. Manual peer handshake가 target에 peer 항목조차 만들지 못하는 이유가 남은 질문이다.

측정 편집(peer view 출력, HWM 상향, gate 우회, 시도 횟수)은 모두 되돌렸다.

### ChannelEgressRouting: 컴파일 수정 + 미등록 channel 오류 kind 수정으로 16/18

이 스위트도 빌드되지 않고 있었다. `Shared` project가 `Zlink.Framework.Contracts`만
참조하는데 `Zlink.Framework.Contracts.Handlers`와 `ZLinkPacketAttribute`는
`Zlink.Framework` assembly에 있다. 같은 attribute를 쓰는 RegistrationCodec의 Shared는
`Zlink.Framework`를 참조한다. 참조를 맞추자 전량 빌드된다.

Runner는 `all`을 거부한다. 미구현 selector 4건(CH-E2E-03·08, CH-REG-02·05) 때문이며
이것은 의도된 gate다. 구현된 18건을 개별 실행한 결과가 다음이다.

```
1차: pass=15 fail=3   (CH-E2E-07, CH-E2E-09, CH-REG-03)
```

CH-E2E-07은 런타임 결함이었다. 시나리오는 등록되지 않은 ChannelName 호출이 `NotFound`로
끝나기를 요구한다.

```csharp
var missing = await InvokeRequestAsync("session", "not.registered", "missing");
Require(!missing.Succeeded && missing.Error == "NotFound", ...);
```

spec 08 §7이 정확히 그렇게 정한다. "ChannelName이 현재 process에 등록되지 않았다 →
`NotFound`로 끝나며 다른 송신 경로로 보내지 않는다." 그런데
`ZLinkFrameworkRuntime.ResolveRouteMeshForChannel`은 후보 0개일 때
`ZLinkConfigurationException`을 던졌다. Host가 이것을 잡지 못해 500이 됐다. 이 호출
시점의 판단은 startup 결함이 아니라 caller가 대응할 수 있는 routing 결과이므로
`ZLinkFrameworkErrorKind.NotFound`로 바꿨다. 후보가 2개 이상인 경우는 실제 등록 모호성이
맞으므로 `ZLinkConfigurationException`을 유지한다.

```
2차: pass=16 fail=2   (CH-E2E-09, CH-REG-03)
```

남은 둘은 같은 지점이다. `wait_json .../fanout-status`가 automatic fanout publisher를
기다리다 timeout한다. 아직 조사하지 않았다.

CH-REG-01은 1차 개별 실행에서 `workflow-client health` timeout으로 실패했다가 전량
실행에서는 통과했다. 재현되지 않는 기동 경합으로 보이며 별도로 다시 확인해야 한다.

### CH-E2E-09·CH-REG-03: fanout barrier가 beacon 주기보다 짧았다

두 selector가 같은 곳에서 멈췄다.

```
Timed out waiting for fanout publisher at .../fanout-status
{"state":"Degraded","isReady":false,"readyPublisherCount":0}
```

spec 24 §2.2는 automatic fanout publisher가 "application record 또는 liveness beacon을
받으면 ready가 된다"고 정한다. 이 barrier 앞에서는 아무것도 발행하지 않으므로 첫 beacon이
유일한 신호이고, beacon 주기는 5초다(`ZLinkFanoutLivenessProtocol.BeaconInterval`).
Runner의 공용 예산은 3초여서 beacon 하나를 담을 수 없다. 구조적으로 통과할 수 없는
barrier였다.

`wait_json`에 예산 인자를 추가하고 이 호출에만 20초를 준다. 다른 barrier는 그대로 3초다.

두 selector 모두 barrier를 통과하고 같은 다음 지점에서 멈춘다.

```
InvalidOperationException: expected topology descriptors, got 5.
```

### `/locations` 열거 범위와 시나리오 기대가 어긋난다

`AssertAutomaticEndpointsAsync`는 row가 7개 이상이고 meshName 집합에 `game`, `audit`,
`workflow.command`, `config12.fanout`이 모두 있기를 요구한다. 실제로는 5개이고 전부
`game`이다.

`IZLinkLocationRuntimeQuery.ListTopologyAsync`가 돌려주는 `ZLinkLocationTopologyEntry`의
주석은 범위를 명시한다.

> One MeshNode descriptor projected with liveness. Spot and Actor rows are
> resolve-only store records and never enumerate into topology.

즉 MeshNode descriptor만 열거한다. ClientServer channel과 fanout channel은 MeshNode가
아니므로 나오지 않는다. 이 스위트는 role마다 mesh를 하나만 등록하고 `audit.record`는 그
mesh의 ChannelName이므로 `audit`이라는 mesh도 없다. 나머지 public 조회인
`ListServiceSummariesAsync`도 MeshName 단위라 마찬가지다.

한편 fanout은 실제로 ready가 됐다. Automatic subscriber가 publisher descriptor를 찾았다는
뜻이므로 store에는 무언가 게시되어 있고 public 조회에 나오지 않을 뿐이다.

공통 spec에서 이 열거 범위를 정하는 조항은 찾지 못했다. 21 §6.4는 ActorId·SpotId 기준
object 위치 조회를 정할 뿐 MeshNode topology 열거를 다루지 않는다. 따라서 둘 중 하나다.

- 공개 조회가 ClientServer·fanout descriptor도 열거해야 한다(런타임 격차).
- 시나리오가 MeshNode descriptor만 기대해야 한다(시나리오 낡음).

코드 주석은 후자를 시사하지만 근거가 주석 하나뿐이라 assertion을 임의로 낮추지 않는다.
