<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework .NET Behavior Matrix](behavior-matrix.ko.md) | [다음: ZLink Framework .NET Lifecycle And Failure Semantics](lifecycle-and-failure-semantics.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md) | [use case validation](../../usecase-validation.ko.md)

# Draft -- ZLink Framework .NET Regression Test Matrix

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework 구현에서 항상 유지해야 할 회귀 테스트
> 기준을 정리한다.

## 1. 목적

use case validation은 설계 설명 범위를 보는 문서다. 반면 이 문서는 구현이 바뀌어도
"무엇이 깨지면 회귀인가"를 테스트 항목으로 고정한다.

## 2. CI 계층

회귀 테스트는 아래 세 계층으로 나눈다.

| 계층 | 목적 | 예시 |
|------|------|------|
| `unit` | registration validation, dispatch lookup, option parsing | duplicate registration, builder validation |
| `integration-single-process` | same-host runtime 조합 확인 | channel request/send, embedded registry, monitoring attach |
| `integration-multi-process` | 실제 topology와 reconnect 확인 | remote registry query, discovery change, spot peer 변화 |

## 3. 최소 CI 매트릭스

| 항목 | 기준 |
|------|------|
| target framework | `net8.0`, `net10.0` |
| runtime RID | `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`, `osx-arm64` |
| test mode | debug, release |

최소 지원 버전이 `net8.0`이므로, 회귀 테스트도 최소 이 두 target framework를 같이
돌려야 한다. 현재 저장소의 기본 빌드는 `net8.0` 단일 TFM이며, `net10.0`은 회귀
matrix 보고용 multi-target 빌드에서 추가로 컴파일·실행하는 형태로 다룬다. 또한
현재 저장소의 `bindings/dotnet/runtimes/` 패키징 대상과
`.github/workflows/build.yml`이 만드는 native artifact 조합이 위 여섯 runtime RID를
기준으로 하고 있으므로, framework CI gate도 같은 범위를 기본으로 본다.

즉 `.NET` framework 회귀 테스트는 특정 OS 하나만 대표로 돌리고 끝내지 않는다.
현재 계획 기준의 필수 플랫폼 범위는 아래와 같다.

- Windows x64
- Windows ARM64
- Linux x64
- Linux ARM64
- macOS x64
- macOS ARM64

## 4. Channel Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate channel 이름 등록 (`AddClientServerChannel`, `AddFanoutChannel`) | `unit` | startup validation 예외 |
| 같은 channel 이름을 client-server와 fanout capability로 동시에 등록 | `unit` | startup validation 예외 |
| server capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| `AddClientServerChannel(...).EnableClient(c => c.UseManualConnections(...))` | `integration-single-process` | manual request/send 성공 |
| `AddClientServerChannel(...).EnableClient(...)` + 전역 `UseDiscovery(...)` | `integration-single-process` | discovery request/send 성공 |
| `AddFanoutChannel(...).EnableSubscriber(s => s.UseManualConnections(...))` | `integration-single-process` | manual subscribe 성공 |
| client capability에 peer acquisition 경로 없음 | `unit` | startup validation 예외 |
| same capability에 discovery/manual 혼용 | `unit` | startup validation 예외 |
| publisher capability에 bind endpoint 없음 | `unit` | startup validation 예외 |
| publisher-only channel | `integration-single-process` | publish submit 성공 |
| subscriber discovery attach | `integration-multi-process` | remote publish 수신 |
| handler group mapping | `unit` | `AddZLinkHandlers...()`만으로는 전역 dispatch 대상이 되지 않고, `channel.MapHandlerGroup("...")`로 매핑한 그룹의 handler만 그 채널에서 dispatch |
| 같은 channel server의 handler 중복 | `unit` | 같은 `kind + packetName`이 둘 이상이면 startup validation 예외 |
| 다른 channel server의 같은 packet handler | `integration-single-process` | 같은 `kind + packetName`을 서로 다른 channel에 매핑해도 각 channel에서 독립 dispatch |
| 같은 그룹을 여러 채널에 매핑 | `integration-single-process` | 같은 `[ZLinkHandlerGroup("api")]`를 두 채널에 `MapHandlerGroup`으로 노출해도 채널마다 독립 dispatch namespace |
| `MapHandlerGroup`이 가리키는 그룹 없음 | `unit` | 매핑한 그룹에 해당 handler가 하나도 없으면 startup validation 경고/오류 |
| event handler group mapping | `unit` | `channel.MapHandlerGroup("...")`로 매핑한 그룹의 publish handler만 그 subscriber channel에서 dispatch |
| HTTP handler에서 `IZLinkClient` 사용 | `integration-single-process` | route handler와 same DI container에서 정상 동작 |
| send async submit backpressure | `integration-single-process` | HWM 도달 시 caller thread를 block하지 않고 ready 이후 완료 |
| publish async submit backpressure | `integration-single-process` | `NoDrop` 또는 HWM 조건에서 thread를 block하지 않고 `SendTimeout` 정책으로 완료 또는 실패 |
| request submit/reply timeout 분리 | `integration-single-process` | request packet submit 지연은 `SendTimeout`, reply 대기는 `WithTimeout(...)`으로 판정 |
| pending request cleanup | `unit` | submit 실패, timeout, cancellation, runtime stop 때 request sequence가 pending map에서 제거 |
| ready callback batch drain | `integration-single-process` | socket ready 이후 pending send/publish를 batch로 처리하고 frame 중복 전송 없음 |

## 5. Spot Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| duplicate `spotName` factory | `unit` | startup validation 예외 |
| duplicate `AddEntrySpot<TEntrySpot>()` | `unit` | 같은 `SpotNode` 안에서 Entry Spot registry 중복 등록 시 startup validation 예외 |
| `AddSpotMesh(...)`에 `UseDiscovery(...)` 없음 | `unit` | discovery 기반 mesh를 만들 수 없으므로 startup validation 예외 |
| `AddSpotMesh(channel, configureMesh)` | `integration-single-process` | mesh 빌더 한 호출로 discovery + node + spot factory 등록 |
| standalone `AddSpotNode(...)` + local-only spot factory | `integration-single-process` | discovery mesh 없이 단일 local SpotNode runtime 시작 |
| `AddSpotNode(...)` + 별도 `UseSpotDiscovery(channel, ...)` 분리 호출 | `integration-single-process` | 호환 경로로만 유지한다. 새 샘플은 `AddSpotMesh(...)`를 사용한다 |
| `CreateAsync(spotName)` | `integration-single-process` | `SpotId`, `SpotName`, `Created` 일관성 확인 |
| `GetAsync(...)`, `ListAsync(...)` | `integration-single-process` | manager 조회 결과 일관성 확인 |
| `Configure()` handler registration | `integration-single-process` | `Context.AddPacket(...)`, `Context.AddActorPacket(...)`, `Context.AddActorJoined(...)`, `Context.AddActorLeft(...)`, `Context.AddSubscribe(...)`, `Context.AddActorJoin(...)` 등록이 descriptor에 반영 |
| Entry Spot handler registration | `integration-single-process` | `AddEntrySpot<TEntrySpot>()`로 등록한 `Context.AddActorPacket(...)`, `AddActorJoined(...)`, `AddActorLeft(...)`가 Entry Spot registry에 반영 |
| `OnInitializeAsync(...)` handler resolve | `integration-single-process` | per-spot scope DI 정상 동작 |
| `OnClosingAsync(...)` normal remove callback | `integration-single-process` | `RemoveAsync(...)` 호출 때 spot 실행 문맥에서 한 번 호출 |
| local spot publish | `integration-single-process` | subscriber 수신 |
| outbound-only 외부 publish client | `integration-multi-process` | target SPOT channel publish 성공 |
| spot 제거 후 scope 정리 | `integration-single-process` | 이후 callback 미발생, dispose 완료 |
| actor join 이후 dispatch 문맥 | `integration-single-process` | `IZLinkSpotContext.AddActorPacket(...)`으로 등록한 handler가 join된 `Spot` 실행 문맥에서 실행 |
| spot route resolver path | `integration-single-process` | spot name/id 기반 호출이 `IZLinkSpotRouteResolver` 결과로 target node와 spot id를 찾아 routed message를 보냄 |
| session actor create/dispatch bridge | `integration-single-process` | `CreateAndBindActorAsync(...)`, `BindActorHandleAsync(...)`, `DispatchToActorAsync(IZLinkActorRef, ...)`가 public session 표면에서 동작 |
| session actor binding rollback | `integration-single-process` | actor-session binding 갱신 실패 때 helper가 실패하고 local binding table의 같은 token entry를 제거 |
| stale session binding token guard | `integration-single-process` | 이전 stream의 늦은 unbind나 stale `SessionProxy` message가 새 binding을 지우거나 사용하지 못함 |
| session context close | `integration-single-process` | `IZLinkSessionContext.CloseAsync(...)`가 현재 stream client 연결을 서버 쪽에서 끊고 disconnect callback으로 이어짐 |
| actor join 직후 packet dispatch | `integration-single-process` | join 완료 뒤 들어온 packet이 새 `Spot` 실행 문맥에서 실행 |
| actor spot 이동 직후 packet dispatch | `integration-single-process` | 이전 `Spot` 문맥으로 stale dispatch 되지 않음 |
| actor context channel request 경로 | `integration-single-process` | join 전 `Context.RequestChannel(...)`은 일반 channel client 경로를, join 후에는 현재 `Spot` channel client 경로를 사용 |
| actor context stream API | `integration-single-process` | actor는 `Context.Send(...)`, `Context.Reply(...)`로 client stream에 쓰고 `IZLinkStream` 직접 노출에 의존하지 않음 |

## 6. Stream Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| 같은 node에 session 중복 등록 | `unit` | startup validation 예외 |
| header session node | `integration-single-process` | `OnDispatchAsync(...)` 호출 |
| `OnConnectedAsync(...)` | `integration-multi-process` | `ConnectionReady` 이후 1회 호출 |
| `OnErrorAsync(...)` 범위 | `integration-multi-process` | transport error만 session callback으로 전달 |
| peer metadata 표면 | `integration-single-process` | `SessionId`, `RoutingId`, `LocalAddr`, `RemoteAddr` 값 확인 |
| session callback task dispatch | `integration-single-process` | transport callback에서 user callback을 직접 호출하지 않고 managed task 경로로 호출 |
| session callback 직렬성 | `integration-single-process` | 같은 session의 lifecycle/packet callback이 서로 병렬 실행되지 않음 |
| session callback 직접 호출 우회 방지 | `unit` | runtime 내부 transport 진입점은 enqueue API만 사용 |

## 7. Registry / Monitoring Regression 항목

| 항목 | 계층 | 통과 기준 |
|------|------|-----------|
| embedded registry startup 순서 | `integration-single-process` | framework discovery가 registry bind 뒤 시작 |
| registry query DI | `integration-single-process` | `IZLinkRegistryQuery` resolve 및 snapshot 조회 성공 |
| remote query client | `integration-multi-process` | topology snapshot 조회 성공 |
| monitoring source name mismatch | `unit` | startup validation 예외 |
| registry polling diff | `integration-multi-process` | topology/status/service summary event 발생 |
| spot polling diff | `integration-multi-process` | status/peers/subjects event 발생 |

## 8. Release Gate

아래 조건을 모두 만족해야 구현 완료로 본다.

1. `unit`, `integration-single-process`, `integration-multi-process` 전부 통과
2. `net8.0`, `net10.0` 둘 다 통과
3. 위 여섯 runtime RID 전체에서 CI gate가 통과
4. happy-path 샘플과 대표 failure-path가 각각 한 번 이상 커버됨
5. `behavior-matrix.ko.md`에 있는 비허용 조합이 모두 테스트로 고정됨

즉 샘플이 한 번 실행되는 것만으로는 충분하지 않고, startup validation과 runtime
failure semantics가 테스트로 같이 고정돼야 한다. native backend가 이미 해당
platform을 지원하더라도, framework는 그 위에서 registration, lifecycle, DI,
monitoring 계층이 추가되므로 platform gate를 별도로 유지해야 한다.

## 9. 문서별 회귀 테스트 단락

이 디렉토리의 각 draft 문서는 자기 항목이 어떤 테스트로 고정되는지 짧은 `회귀 테스트`
단락을 가져야 한다. 중앙 matrix만 갱신하면 세부 문서 독자가 어떤 테스트를 봐야 하는지
놓치기 쉽기 때문이다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `DocumentationRegressionTests.DotNetDraftDocuments_AllExposeRegressionTestSection` | 아래 문서들이 모두 `회귀 테스트` 단락을 가진다. |
| `DocumentationRegressionTests.DotNetRegressionMatrix_References_AllDraftDocuments` | 이 matrix가 아래 문서 파일명을 모두 참조한다. |

대상 문서는 다음과 같다.

- `README.ko.md`
- `handler-interfaces.ko.md`
- `aspnet-core-channel-messaging.ko.md`
- `aspnet-core-spot.ko.md`
- `stage-wrapper-on-spot.ko.md`
- `aspnet-core-stream.ko.md`
- `aspnet-core-actor.ko.md`
- `session-actor-dispatch.ko.md`
- `streaming-client.ko.md`
- `unity-stream-connector.ko.md`
- `stream-open-items.ko.md`
- `aspnet-core-monitoring.ko.md`
- `aspnet-core-registry.ko.md`
- `behavior-matrix.ko.md`
- `regression-test-matrix.ko.md`
- `lifecycle-and-failure-semantics.ko.md`
- `implementation-scope-and-nongoals.ko.md`
- `backend-dependency-policy.ko.md`
- `channel-messaging-samples.ko.md`
- `spot-samples.ko.md`
- `stream-samples.ko.md`
- `tictactoe-game-sample.ko.md`
