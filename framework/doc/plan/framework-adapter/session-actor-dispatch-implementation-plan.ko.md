[계획 목록](./README.ko.md) | [Session Actor Dispatch Usability](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md) | [POSD 리뷰](./worklog/posd-review.md) | [Sample POSD 리뷰](./worklog/sample-posd-review.md)

# Session Actor Dispatch 구현 및 샘플 전환 계획

이 문서는 `session-gateway-usability.ko.md` 초안을 사람의 추가 판단 없이 코드와 sample에
반영하기 위한 실행 계획이다. 이 문서에는 draft의 API 계약을 복사하지 않는다. 작업자는
각 단계에 적힌 draft section 링크를 직접 열어 읽고, 그 내용을 기준으로 구현한다.

이 문서는 공개 API 계약이 아니다. 공개 계약의 기준은 항상 아래 draft다.

- [session-gateway-usability.ko.md](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md)

## 1. 사용 방법

새 세션에서는 아래 짧은 프롬프트만 사용한다.

```text
/home/hep7/project/kairos/zlink 저장소에서 작업한다.
AGENTS.md를 먼저 읽고 규칙을 따른다.
framework/doc/plan/framework-adapter/session-actor-dispatch-implementation-plan.ko.md
문서를 단일 실행 계획으로 삼아, 사용자 추가 판단 없이 Phase 0부터 마지막 완료 조건까지
끝까지 진행한다.
```

작업자는 각 phase에서 아래 순서를 반복한다.

1. phase의 `읽을 draft 위치` 링크를 모두 연다.
2. 해당 section의 API, behavior, failure, test, sample 요구를 작업 로그에 항목화한다.
3. 코드를 수정한다.
4. 문서 반영 누락 리뷰를 실행한다.
5. POSD 기준으로 리팩토링한다.
6. 테스트와 smoke를 실행한다.
7. worklog 상태를 `pending`, `implemented`, `verified`, `not-applicable` 중 하나로 갱신한다.

모호한 점이 있어도 사람에게 질문하지 않는다. draft, 더 구체적인 `.NET` 문서, 기존 코드,
POSD 원칙 순서로 판단하고 계속 진행한다.

### 1.1 자율 실행 계약

이 계획은 새 세션 작업자가 중간에 사용자 결정을 요청하지 않고 끝까지 진행할 수 있어야
한다. 작업자는 아래 규칙을 따른다.

- 이 문서를 작업 queue의 단일 진입점으로 삼는다.
- draft의 내용을 프롬프트에 다시 복사하지 않는다. 각 phase의 링크를 직접 열어 읽는다.
- 어떤 API 이름이나 sample 구조가 충돌하면 더 최신 기준은
  [session-gateway-usability.ko.md](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md)로 둔다.
- `session-gateway.ko.md`는 superseded 초안이므로 새 구현 기준으로 쓰지 않는다.
- worklog의 과거 `SessionGateway` 구현 기록은 이력으로 보존하되, 새 구현 기준으로 쓰지 않는다.
- 판단이 필요한 경우에는 `draft -> .NET binding draft -> 기존 코드 -> POSD 원칙`
  순서로 근거를 찾고, 그 근거를 worklog에 남긴 뒤 진행한다.
- 구현이 막히면 placeholder나 smoke marker로 완료 처리하지 않는다. 실패 원인을 test나
  검색 결과로 좁히고 수정한다.
- `rg`가 없는 환경에서는 같은 의미의 `grep -RIn` 검색으로 대체한다.
- 각 phase가 끝날 때 관련 worklog를 갱신한다. 아직 남은 항목은 `pending`으로 남기고
  다음 phase로 넘기지 않는다.

### 1.2 전체 종료 조건

전체 작업은 아래 조건이 모두 만족될 때만 완료로 본다.

- Phase 0부터 Phase 9까지 모든 완료 조건을 만족한다.
- `implementation-checklist.md`, `posd-review.md`, `sample-posd-review.md`에 이번 작업의
  최종 상태가 기록되어 있다.
- framework와 sample의 `pending` 항목이 없다.
- POSD 반복 리뷰의 마지막 기록에 framework와 sample red flag가 없다고 적혀 있다.
- 필수 build/test/smoke 명령이 통과했다.
- 필수 검색에서 old public API, fake transport, retry/warmup workaround, sample serializer
  helper가 새 code와 새 sample 표면에 남지 않는다.
- draft 내용을 정식 spec에 반영한 뒤 전체 spec/draft 충돌 리뷰까지 끝났다.

## 2. Draft Section Map

아래 표는 구현자가 반드시 직접 읽어야 하는 위치다. 구현 세부는 이 계획서가 아니라
링크된 draft section을 기준으로 한다.

| 주제 | 읽을 draft 위치 |
|------|-----------------|
| 문제와 목표 | [§1 목적](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#1-목적), [§2 현재 샘플에서 드러난 문제](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#2-현재-샘플에서-드러난-문제), [§3 설계 목표](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#3-설계-목표), [§4 비목표](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#4-비목표) |
| POSD 판단 기준 | [§5 POSD 기준 문제 정리](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#5-posd-기준-문제-정리), [§6 대안 검토](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#6-대안-검토), [§22 POSD Review Result](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#22-posd-review-result) |
| 표면 개요 | [§7 제안 표면 개요](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#7-제안-표면-개요) |
| handler dispatch | [§8 Actor/Node/Spot Handler Dispatch](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#8-actornodespot-handler-dispatch), [§8.2 제안 handler 모양](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#82-제안-handler-모양), [§8.4 등록 API](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#84-등록-api), [§8.5 낮은 수준 handler와의 관계](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#85-낮은-수준-handler와의-관계) |
| SessionProxy naming | [§8.2.1 SessionProxy naming](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#821-sessionproxy-naming), [§11 SessionProxy 호출 표면](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#11-sessionproxy-호출-표면), [§11.1 이름 변경 계획](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#111-이름-변경-계획) |
| metadata | [§8.3 Header Metadata 전달](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#83-header-metadata-전달), [§12 Codec 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#12-codec-정책) |
| route resolver | [§9 Actor Route Resolvers](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#9-actor-route-resolvers), [§9.2 제안 interface](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#92-제안-interface), [§9.3 resolver가 숨기는 정보](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#93-resolver가-숨기는-정보), [§9.4 실패 의미](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#94-실패-의미) |
| session location writer | [§9.5 위치 소유와 갱신 책임](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#95-위치-소유와-갱신-책임), [§9.6 registry discovery metadata sample](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#96-registry-discovery-metadata-sample) |
| session actor helper | [§10 Session Actor Helpers](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#10-session-actor-helpers), [§10.2 제안 session context API](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#102-제안-session-context-api), [§10.2.1 actor create lifecycle](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#1021-actor-create-lifecycle), [§10.3 직접 dispatch 예시](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#103-직접-dispatch-예시) |
| discovery | [§13 Discovery 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#13-discovery-정책) |
| spot route | [§14 Spot Route Resolver와 Direct Target API 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#14-spot-route-resolver와-direct-target-api-정책) |
| timeout/retry | [§15 Timeout과 retry 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#15-timeout과-retry-정책) |
| before/after | [§16 Before / After](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#16-before--after) |
| error | [§17 Error 의미](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#17-error-의미) |
| breaking change | [§18 Breaking Change](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#18-breaking-change) |
| test 기준 | [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준) |
| 구현 순서와 완료 기준 | [§20 구현 순서](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#20-구현-순서), [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준) |

보조 문서는 필요할 때만 직접 읽는다.

- [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md)
- [interaction-model.ko.md](../../spec/draft/framework-adapter/policy/interaction-model.ko.md)
- [session-gateway.ko.md](../../spec/draft/framework-adapter/policy/session-gateway.ko.md)
- [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md)
- [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md)
- [streaming-client.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/streaming-client.ko.md)
- [tictactoe-game-sample.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md)

## 3. 공통 완료 규칙

- 기존 TicTacToe sample은 보존한다.
- 새 sample은 기존 sample과 project/file을 공유하지 않는다.
- sample 내부 fake transport로 성공시키지 않는다.
- discovery 문제를 retry나 warmup sleep으로 숨기지 않는다.
- framework는 dotnet zlink public API만 사용한다.
- old public API 이름은 제거 설명 문맥을 제외하고 code와 sample에 남기지 않는다.
- request/reply matching은 request sequence 기준으로 검증한다.
- 각 phase 뒤 `implementation-checklist.md`, `posd-review.md`,
  `sample-posd-review.md` 중 필요한 파일을 갱신한다.

## 4. Phase 0: 기준선 수집

읽을 draft 위치:

- [§18 Breaking Change](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#18-breaking-change)
- [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

작업:

1. 현재 code와 sample에서 old API, sample shortcut, fake transport, direct target API를 검색한다.
2. 검색 결과를 `implementation-checklist.md`에 추적표로 기록한다.
3. 각 항목을 `pending`, `verified`, `not-applicable` 중 하나로 표시한다.

필수 검색:

```bash
rg -n "SessionGateway|IZLinkSessionGateway|EnableSessionGateway|AddSessionProxyHandler|OpenActorRelay|DispatchAsync|SendToActor|RequestActor|IZLinkSessionProxyHandler" framework/languages/dotnet
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/languages/dotnet framework/doc/spec/draft/framework-adapter
rg -n "InMemoryRoutedChannel|UseManualConnections|Retry|Warmup|Task\\.Delay|SampleJson|System.Text.Json" framework/languages/dotnet/samples
```

## 5. Phase 1: Framework API 구현

읽을 draft 위치:

- [§7 제안 표면 개요](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#7-제안-표면-개요)
- [§8 Actor/Node/Spot Handler Dispatch](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#8-actornodespot-handler-dispatch)
- [§9 Actor Route Resolvers](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#9-actor-route-resolvers)
- [§10 Session Actor Helpers](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#10-session-actor-helpers)
- [§11 SessionProxy 호출 표면](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#11-sessionproxy-호출-표면)
- [§14 Spot Route Resolver와 Direct Target API 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#14-spot-route-resolver와-direct-target-api-정책)
- [§17 Error 의미](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#17-error-의미)

작업:

1. 링크된 draft section을 읽고 public API 변경 항목을 추적표에 옮긴다.
2. framework code에 public API를 구현한다.
3. old public API를 제거하거나 internal 경로로 숨긴다.
4. route resolver, session location writer, metadata policy, error surface를 구현한다.
5. registry 기본 구현을 framework에 넣지 않았는지 확인한다.

완료 조건:

- draft section별 `API`, `Behavior`, `Failure` 항목이 추적표에서 `implemented` 이상이다.
- code에서 old public API가 제거 설명 문맥 외에 남지 않는다.
- 관련 unit/integration test가 추가되었거나 기존 test가 새 API 기준으로 바뀌었다.

## 6. Phase 2: Framework 문서 반영 누락 반복 리뷰

읽을 draft 위치:

- [§20 구현 순서](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#20-구현-순서)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

반복 절차:

1. [Draft Section Map](#2-draft-section-map)의 모든 링크를 다시 연다.
2. 각 section의 요구가 code/test에 반영되었는지 확인한다.
3. 누락이 있으면 Phase 1로 돌아가 구현한다.
4. 구현 후 같은 section을 다시 리뷰한다.
5. 모든 항목이 `verified` 또는 이유 있는 `not-applicable`이 될 때까지 반복한다.

완료 조건:

- `implementation-checklist.md`에 framework 관련 `pending`이 없다.
- 누락이 없다는 리뷰 결과가 worklog에 기록되어 있다.

## 7. Phase 3: Framework POSD 반복 리팩토링

읽을 draft 위치:

- [§5 POSD 기준 문제 정리](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#5-posd-기준-문제-정리)
- [§6 대안 검토](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#6-대안-검토)
- [§22 POSD Review Result](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#22-posd-review-result)

반복 절차:

1. framework code를 POSD 기준으로 리뷰한다.
2. red flag를 `posd-review.md`에 기록한다.
3. 각 red flag마다 두 가지 이상 대안을 비교한다.
4. 선택한 대안을 구현한다.
5. 관련 test를 실행한다.
6. 새 red flag가 없을 때까지 반복한다.

완료 조건:

- `posd-review.md` 마지막 iteration에 framework red flag 없음이 기록되어 있다.
- 리팩토링 뒤 framework test가 통과한다.

## 8. Phase 4: Sample 수정

읽을 draft 위치:

- [§10.3 직접 dispatch 예시](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#103-직접-dispatch-예시)
- [§11 SessionProxy 호출 표면](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#11-sessionproxy-호출-표면)
- [§13 Discovery 정책](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#13-discovery-정책)
- [§16 Before / After](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#16-before--after)
- [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

작업:

1. 기존 `framework/languages/dotnet/samples/TicTacToe` 구조를 보존한다.
2. 새 sample 경로는 기존 계획의
   `framework/languages/dotnet/samples/TicTacToe(session-gateway)`를 유지한다.
3. 새 sample의 설명과 code는 draft의 새 public 모델을 따른다.
4. registry discovery metadata 기반 writer/resolver sample은
   [§9.6](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#96-registry-discovery-metadata-sample)을 직접 읽고 구현한다.
5. sample smoke는 실제 stream connector, routed channel, discovery를 사용한다.

완료 조건:

- 기존 TicTacToe sample build가 통과한다.
- 새 sample build와 smoke가 통과한다.
- sample 내부 fake transport, retry/warmup sleep, sample serializer helper가 없다.

## 9. Phase 5: Sample 문서 반영 누락 반복 리뷰

읽을 draft 위치:

- [§2 현재 샘플에서 드러난 문제](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#2-현재-샘플에서-드러난-문제)
- [§16 Before / After](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#16-before--after)
- [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

반복 절차:

1. sample 관련 draft section을 다시 읽는다.
2. sample code가 요구를 충족하는지 확인한다.
3. 누락을 `sample-posd-review.md`에 기록한다.
4. sample을 수정한다.
5. sample build와 smoke를 실행한다.
6. 모든 sample 항목이 `verified` 또는 이유 있는 `not-applicable`이 될 때까지 반복한다.

필수 검색:

```bash
rg -n "SessionGateway|IZLinkSessionGateway|EnableSessionGateway|AddSessionProxyHandler|OpenActorRelay|SendToActor|RequestActor|IZLinkSessionProxyHandler|InMemoryRoutedChannel|UseManualConnections|Retry|Warmup|Task\\.Delay|System.Text.Json|SampleJson" "framework/languages/dotnet/samples/TicTacToe(session-gateway)"
rg -n "RoutingId|\\.SendAsync\\(|ExecAsync|WithDontWait|\\.Sync\\(" "framework/languages/dotnet/samples/TicTacToe(session-gateway)"
```

남은 검색 결과는 제거 대상인지, draft의 제거 설명 문맥인지, resolver/placement 내부인지
분류해서 worklog에 남긴다.

## 10. Phase 6: Sample POSD 반복 리팩토링

읽을 draft 위치:

- [§5 POSD 기준 문제 정리](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#5-posd-기준-문제-정리)
- [§22 POSD Review Result](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#22-posd-review-result)

반복 절차:

1. sample code를 POSD 기준으로 리뷰한다.
2. red flag를 `sample-posd-review.md`에 기록한다.
3. 각 red flag마다 두 가지 이상 대안을 비교한다.
4. 선택한 대안을 구현한다.
5. sample build와 smoke를 실행한다.
6. 새 red flag가 없을 때까지 반복한다.

완료 조건:

- `sample-posd-review.md` 마지막 iteration에 sample red flag 없음이 기록되어 있다.
- sample smoke가 실제 topology로 통과한다.

## 11. Phase 7: 전체 검증

읽을 draft 위치:

- [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

필수 명령:

```bash
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug -f net8.0 --no-build
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.RuntimeTests/Zlink.Framework.RuntimeTests.csproj -c Release -f net8.0 --no-build
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MonitoringRuntimeTests/Zlink.Framework.MonitoringRuntimeTests.csproj -c Release -f net8.0 --no-build
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0 --no-build
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj -c Release -f net8.0 --no-build
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln -c Debug
/home/hep7/.dotnet/dotnet build "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Release
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Release --no-build
```

완료 조건:

- 모든 명령이 통과한다.
- `implementation-checklist.md`에 이번 계획의 `pending`이 없다.
- `posd-review.md`와 `sample-posd-review.md` 마지막 iteration에 red flag 없음이 기록되어 있다.

## 12. Phase 8: Draft 내용을 정식 Spec에 반영

Phase 7까지 완료된 뒤에만 수행한다. 구현 전 초안의 내용을 그대로 복사하지 않고,
실제 구현과 테스트로 확정된 계약만 정식 spec 문서에 나누어 반영한다.

읽을 draft 위치:

- [Session Actor Dispatch Usability](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md)
- [§18 Breaking Change](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#18-breaking-change)
- [§19 테스트 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#19-테스트-기준)
- [§21 완료 기준](../../spec/draft/framework-adapter/policy/session-gateway-usability.ko.md#21-완료-기준)

반영 대상 후보:

| 정식 또는 기존 spec 문서 | 반영 내용 |
|--------------------------|-----------|
| [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md) | public builder, 제거된 old API, error surface |
| [interaction-model.ko.md](../../spec/draft/framework-adapter/policy/interaction-model.ko.md) | request sequence, timeout, retry 금지 의미 |
| [session-gateway.ko.md](../../spec/draft/framework-adapter/policy/session-gateway.ko.md) | 새 session actor dispatch와 `SessionProxy` 모델로 갱신 또는 대체 |
| [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md) | typed handler, actor context, call builder |
| [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md) | actor create, writer bind/unbind, stale binding, framework exception |
| [regression-test-matrix.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/regression-test-matrix.ko.md) | 확정된 회귀 테스트 항목 |
| [tictactoe-game-sample.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md) | 최종 sample 구조와 smoke 기준 |

작업:

1. 구현 결과와 test 이름을 기준으로 draft section별 확정 계약을 추린다.
2. 대상 독자에 맞게 spec, binding 문서, sample 문서에 나누어 반영한다.
3. 구현되지 않은 아이디어나 보류된 대안은 정식 spec에 넣지 않는다.
4. 정식 spec에 반영한 뒤 draft 문서에는 유지 이유를 적거나, 더 이상 필요 없으면
   README에서 상태를 정리한다.
5. 정식 spec 문서와 code/test를 다시 대조한다.

완료 조건:

- draft의 구현 완료 내용이 정식 spec 또는 binding/sample 문서에 빠짐없이 반영되어 있다.
- 정식 spec에 구현되지 않은 계약이 섞이지 않았다.
- `session-gateway-usability.ko.md`의 남은 역할이 명확하다.
- spec 반영 뒤 금지 표현과 오래된 public API 예시 검색을 다시 통과한다.
- Phase 9의 전체 spec/draft 개념 충돌 리뷰로 넘어갈 수 있다.

## 13. Phase 9: 전체 Spec/Draft 개념 충돌 리뷰

Phase 8에서 정식 spec 반영을 끝낸 뒤 수행한다. 이 단계의 목적은 새 public 모델이
반영된 뒤에도 다른 spec 또는 draft 문서에 이전 개념이 남아 사용자를 혼동시키지 않는지
확인하는 것이다.

읽을 문서 범위:

- `framework/doc/spec/` 전체
- `framework/doc/spec/draft/framework-adapter/` 전체
- `framework/doc/spec/sample/` 전체
- 이번 계획의 worklog 문서

작업:

1. 전체 spec과 draft에서 같은 개념을 서로 다른 이름으로 설명하는 부분을 찾는다.
2. 이전 `SessionGateway` public API, `BindActorAsync`, `OpenActorRelay`,
   `AddSessionProxyHandler(...)`, direct target send/request 설명이 새 public 계약과
   충돌하는지 확인한다.
3. `AttachActorAsync(...)`, `BindActorAsync(...)`, `CreateActorAsync(...)`,
   `CreateRemoteActorAsync(...)`, `DispatchToActorAsync(...)`의 의미가 문서별로 일치하는지
   확인한다.
4. resolver 입력이 `actorId` 또는 `spotId`로 좁게 유지되는지 확인한다.
5. registry metadata sample이 framework 기본 구현처럼 설명된 문서가 없는지 확인한다.
6. sample 문서가 기존 sample 보존, 새 sample 독립 project, discovery, no retry,
   no fake transport 원칙과 맞는지 확인한다.
7. 충돌을 발견하면 대상 독자에 맞는 문서 위치로 수정한다.
8. 수정 뒤 같은 검색을 다시 실행한다.

필수 검색:

```bash
rg -n "EnableSessionGateway|AddSessionProxyHandler|IZLinkSessionGateway|SendToActor|RequestActor|OpenActorRelay|IZLinkSessionProxyHandler|BindActorAsync|AttachActorAsync|CreateRemoteActorAsync|DispatchToActorAsync|IZLinkActorRef|SessionProxy" framework/doc/spec framework/doc/plan/framework-adapter
rg -n "route key|RouteKey|ZLinkSpotRouteRequest|ZLinkActor.*RouteRequest|metadata.*resolver|resolver.*metadata|SpotNodeId|direct target" framework/doc/spec framework/doc/plan/framework-adapter
rg -n "InMemoryRoutedChannel|UseManualConnections|Retry|Warmup|Task\\.Delay|SampleJson|System.Text.Json" framework/doc/spec framework/doc/plan/framework-adapter
```

현재 문서 기준으로 이미 확인된 충돌 후보는 아래와 같다. Phase 9에서는 이 후보를
그대로 믿지 말고, 구현 완료 상태와 정식 spec 반영 결과를 기준으로 다시 검토한다.

| 후보 문서 | 확인할 충돌 |
|-----------|-------------|
| `policy/session-gateway.ko.md` | 기존 `EnableSessionGateway`, `AddSessionProxyHandler`, `BindActorAsync`, `OpenActorRelay`, `IZLinkSessionGateway` 중심 설명 |
| `bindings/dotnet/handler-interfaces.ko.md` | session context actor API가 새 `CreateActorAsync(...)`/`CreateRemoteActorAsync(...)` 모델과 맞는지 |
| `bindings/dotnet/aspnet-core-stream.ko.md` | stream session API가 새 actor create/dispatch helper와 맞는지 |
| `bindings/dotnet/stream-samples.ko.md` | sample code가 이전 attach/bind 용어를 쓰는지 |
| `bindings/dotnet/spot-samples.ko.md` | actor/spot 연결 예시가 새 session actor dispatch 모델과 충돌하는지 |
| `bindings/dotnet/regression-test-matrix.ko.md` | 회귀 테스트 항목이 새 resolver, writer, `SessionProxy`, metadata 정책을 포함하는지 |
| `spec/sample/tictactoe/session-gateway.ko.md` | 기존 `BindActorAsync`, `OpenActorRelay`, `SendToActor` sample 흐름을 새 sample 흐름으로 바꿔야 하는지 |
| worklog 문서 | 과거 구현 기록과 현재 목표가 구분되어 있는지 |

검색 결과는 아래처럼 분류한다.

| 분류 | 처리 |
|------|------|
| 새 public 계약과 충돌 | 문서 수정 |
| 이전 API 제거 설명 | 유지 가능하지만 제거 문맥임을 명확히 함 |
| 구현 전 draft의 historical context | 유지 가능하지만 새 기준 문서 링크를 추가 |
| sample 금지 항목 | sample 문서와 계획에서 금지 기준으로만 유지 |
| worklog 과거 기록 | 유지 가능하지만 현재 상태와 구분되게 표시 |

완료 조건:

- 새 public 모델과 충돌하는 spec/draft 설명이 없다.
- 이전 API는 제거 설명 또는 과거 worklog 문맥으로만 남는다.
- 정식 spec, draft, sample 문서의 완료 기준이 서로 같은 방향을 가리킨다.
- 충돌 리뷰 결과와 수정 내역이 worklog에 기록되어 있다.
- 최종 응답에는 변경 요약, 통과한 검증 명령, 남은 이슈가 없다는 판단 근거를 적는다.
