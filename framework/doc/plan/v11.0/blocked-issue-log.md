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

| 항목 | 담당 ID·lane | 기준 revision | 증상과 실행 command | 근본 원인 | 수정 내용과 변경 path | 재검증 command와 결과 | 상태 |
|---|---|---|---|---|---|---|---|
| `BLK-001` | `V11-M6A-DN`, `V11-M6B-DN`; .NET lane | working tree(2026-07-25, base `a67bc4419d`) | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj`에서 `EntrySpotActorDispatchTests.Current_Spot_Publish_Emits_Sent_With_Spot_Rid_And_Current_Flow`와 `..External_Spot_Publish_Emits_Internal_Publisher_Rid_Without_Correlation`이 flow log 파일 부재로 실패했다. | v11 정식 spec `52-message-flow-tracing.ko.md` §3·§9는 Logical Multicast·classic fanout publish가 message-flow event를 만들지 않도록 못박는다. Runtime은 이 규칙을 이미 지키고 있고, 두 unit test가 v10 시절의 `phase=sent` 기대를 그대로 들고 있었다. C++ reference에도 publish `sent` 기대는 없다. | Runtime 동작은 그대로 두고 두 test의 flow-log 기대만 spec 규칙(“publish는 event를 만들지 않는다”)으로 교체했다. Envelope header의 flow·correlation·publisher identity 검증은 유지했다. `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/EntrySpotActorDispatchTests.cs` | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --filter "FullyQualifiedName~EntrySpotActorDispatchTests"` → 71 passed / 1 failed(남은 1건은 이 항목과 무관한 `Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply` stream ingress gap). | 해결 |
| `BLK-002` | `V11-M6A-DN`; .NET lane | working tree(2026-07-25, base `a67bc4419d`) | `HttpExecutionSchedulerTests.Captured_http_callback_is_posted_as_a_new_serial_turn`이 5초 timeout으로 실패했다. | `859fcf07fe`가 `ZLinkSpotHttpExecutionScheduler.Capture()`를 `ZLinkApplicationExecutionContext.Current is { YieldAllowed: true }` 조건으로 강화했다(`04-async-execution-policy.ko.md`의 “Yield는 SpotWide User Spot·Instance Spot application callback에서만 유효” 규칙). Test는 bare serial queue turn만 만들어 capture가 `null`을 반환했고, 그 결과 delegate가 예외로 끝나 신호가 오지 않았다. | Runtime 강화는 유지하고 test가 yield 허용 application scope를 push하도록 맞췄다. `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/HttpExecutionSchedulerTests.cs` | `dotnet test --filter "FullyQualifiedName~HttpExecutionSchedulerTests"` → 1/1 통과. | 해결 |
