# RouteMesh 11.0.0 통합 execution ledger

## 0. 문서 역할

이 문서는 RouteMesh 11.0.0 작업의 **단일 실행 상태 기준**이다. Core service 계약 이관, C++·.NET·JVM·
Node.js Framework runtime, stateful maintenance, socket liveness 정책, Core·bindings 제거, package와 smoke를
하나의 선행 조건 구조로 관리한다.

작업 상태, 담당 lane, 기준 revision, 이후 변경 목록, artifact hash, test와 review 증거는 이 ledger의 담당
행에만 기록한다. 다른 문서에 진행표나 완료 증거를 만들지 않는다. 예외는 §2.2가 정의한
[blocked issue log](blocked-issue-log.md) 하나이며, 이 log는 진행 상태가 아니라 blocker의 원인과 수정
이력만 기록한다. 정식 spec, source, test와 package 자체는
각 소유 디렉터리에 두되 이 ledger에서 정확한 revision과 결과 위치를 참조한다.

외부 registry에는 배포하지 않는다. Package gate는 local/internal package 생성, clean consumer 설치와
artifact provenance만 검증한다.

### 0.0.1 완료 목표 checkpoint

최종 마감은 시스템 시간 기준 **2026-07-29 09:00 KST**다. 이 시간은 계약이나 완료 gate를 줄이는
근거가 아니다. Critical path는 `.NET`의 canonical relocation reservation과 production wire 연결,
전체 회귀와 POSD·DDD review, 나머지 세 runtime의 동형 수렴, E2E·sample 활성화, 최종 package와 smoke
순서다. 서로 독립적인 언어 lane은 계속 병렬로 실행한다.

현재 병렬 checkpoint는 다음과 같다.

- 2026-07-28 RouteMesh Object Client 연결 계약을 갱신했다. 양쪽 모두 `Objects().Client()`이고
  RouteMesh Channel Server membership도 없는 pair만 Automatic discovery에서 connection intent를 만들지
  않는다. Manual endpoint가 이 조합을 만들면 handshake에서 확인한 뒤 socket을 종료하고 같은
  configuration generation에서 admission을 다시 시도하지 않는다. Object Client와 RouteMesh Channel
  Server의 동시 등록은 허용하며, 어느 한쪽에 Server membership이 있으면 weight가 `0`이어도 연결이
  필요하다. Channel Client, ClientServer와 classic fanout role은 이 판정에 포함하지 않는다. 운영
  상태는 연결이 필요하지만 준비되지 않은 `NotConnected`와 연결 자체가 필요 없는 `NotRequired`를
  구분한다. `NotRequired`는 peer 목록에는 남지만 ready count, liveness와 health failure 판정에서는
  제외한다. 공통 topology·monitoring·liveness spec, 공통 E2E와 다섯 언어 exact interface를
  정렬 중이다. `.NET`은 descriptor의 Object role과 RouteMesh Server membership을 planner와 handshake에
  함께 연결했다. Object Client와 application Node direct handler의 동시 등록은 startup에서 거부하고,
  Object Client RID를 Node direct target으로 사용하면 새 연결이나 reroute 없이 `NotFound`로 끝낸다.
  이전 규칙의 focused unit 46/46, targeted contract 7/7, 전체 unit
  1,191/1,191과 AutomaticTurnDispatch Session build가 통과했다. 후속 `gpt-5.6-sol high`
  review는 public automatic·manual Node direct 분류가 Client pair의 `NotRequired`를 잃는 P1,
  admission 전 expected RID를 사용하지 않아 peer가 중복 표시될 수 있는 P2, backend Object role
  설정의 기본 no-op P2를 찾았다. `.NET`은 target을 `Unknown`, `ObjectClientTarget`,
  `RequiredNotConnected`, `ReadyEligible`로 닫힌 집합 안에서 분류하고, admission 전 peer도
  expected RID로 표시하며 backend의 `SetObjectRole` 구현을 필수로 바꿔 세 finding을 닫았다.
  Public automatic·manual Node direct, Connecting peer 단일 표시, `NotConnected`와
  `NotRequired` 분리 회귀 7/7, auto-connect 36/36, raw service runtime 23/23, startup
  validation 56/56이 통과했다. 전체 unit은 1,196건 중 1,194건이 통과했다. 이 작업과 관계없는
  Message Follow deadline timing 1건이 실패했고, 수정한 startup ordering expectation 1건은
  단독 재실행에서 통과했다. 후속 검토에서 Object Client와 RouteMesh Channel Server의 동시 등록 및
  weight `0` Server capability가 빠진 것을 확인했다. Startup 금지를 제거하고 automatic·manual
  판정이 Server membership의 존재 자체를 사용하도록 수정했다. Linux focused startup·ObjectClient
  6/6, automatic planner 5/5, manual admission 2/2와 repeated Hello dedupe 1/1이 통과했다.
  다섯 언어 exact interface와 RM-A3 matrix도 같은 조건으로 갱신했다. 갱신한 Config 1 `RM-A3`
  actual-process는 Automatic·Manual Object Client pair의 `NotRequired`, Ready peer 0와 20초 reconnect
  억제를 확인했다. 한쪽에 RouteMesh Channel Server가 있으면 weight `100`과 `0`에서 모두 Ready peer
  1이 됐다. ClientServer와 classic fanout만 등록한 pair는 `NotRequired`를 유지했다. 연결이 필요한
  descriptor-only pair는 `NotConnected`·`Degraded`, Node direct Send·Request는 `NotFound`와 peer 수
  불변, Server↔Client·Server↔Server는 Ready를 확인했다. 실제 실행 중 Client membership을 backend
  Server admission으로 잘못 광고하던 문제와 Manual 양방향 연결의 동일 endpoint 중복 판정을 수정했다.
  Raw service runtime 23/23, Consumer build warning·error 0과 actual-process가 통과했다. 증거는
  `framework/languages/dotnet/e2e/LocationMessaging/logs/20260728-215606-1356039/rm-a3.evidence.log`다.
  Config 13 `SA-E2E-08` 증거는
  `framework/languages/dotnet/e2e/SubmitAdmission/logs/20260728-154231-1164570/`에 있다.
  `Zlink.HttpClient` source와 local package ABI를 맞추기 위해 package version을 `0.5.1`로 올렸고,
  WSL package SHA-256은
  `0c96944206471dc76485d0acf11206a9cd706ab757d3c20e5b073cd77ec78507`이다.
  다른 runtime의 동형 구현은 남아 있다.
- `.NET`은 command `40→30 offer→30 accept→41`을 raw production ingress에 연결했다. Command 40과 target
  offer는 상태를 변경하지 않으며 offer의 participant vector는 비어 있다. Exact source accept 뒤 User Spot은
  seal 시점의 단일 root와 participant 전체 typed bundle로 `PrepareAggregate`를 한 번 실행한다. Standalone
  Actor와 Instance Spot만 단일 capacity fence를 사용한다. 같은 accept retry는 single-flight 결과에 합류하고
  participant가 달라진 retry는 `InvalidDataException`으로 거부한다. Focused User Spot 1/1과 canonical reservation
  owner 28/28이 통과했으며 production source의 `.stage.v1`·`.publish.v1`·`.abort.v1`·`.held-relay.v1`
  packet symbol은 0개다. Mixed-language process E2E와 독립 final review는 남아 있다.
- Node.js는 independent command 33·46 durable reply relay와 ACK, side-effect-free offer를 유지하면서 command
  30 accept 시점에 실제 64-unit·256 MiB permit과 Location reservation을 함께 획득하도록 고쳤다. User Spot은
  participant 수와 무관하게 `PrepareAggregate`를 사용하고 standalone Actor·Instance Spot만 단일 capacity fence를
  사용한다. Offer는 capacity를 소비하지 않으며 success·abort·shutdown에서 permit과 timer를 해제한다. Unknown Store
  response는 처음 고정한 exact request·plan만 재실행하고 authority를 다시 읽어 새 reservation을 만들지 않는다.
  Target live record와 terminal tombstone은 각각 1,024개로 제한했다. `npm run lint`·`build`·`typecheck`, focused
  host relocation 2/2와 M6C 62/62가 통과했다. Active process의 Session route ACK와 mixed-language E2E는 남아 있다.
- Node.js Config 2 `SM-F6`은 같은 MeshName의 Object Server 두 process에서 실제 Redis Location Store와
  Relocation Store를 사용한다. Runtime placement weight로 source와 target을 분리한 뒤 source User Spot의
  global SpotId request·send와 Entry Spot Actor의 deferred join을 실행했다. Request·send handler,
  target admission·membership와 join completion evidence는 target process에만 남았다. Actor factory는
  Snapshot relocation policy와 adapter를 등록하며 호출자는 owner RID, endpoint, generation과 route frame을
  만들지 않는다. MultiNode·Client focused build와 actual process가 통과했고 증거는
  `framework/languages/node/e2e/SpotService/log/20260729-050702-1863756/`에 있다.
- Node.js Config 2 `SM-C4`는 local Spot factory가 없는 Gateway에서 같은 RouteMesh의 public node
  publisher를 사용한다. Gateway는 Spot channel의 Client role만 등록하고 Play-A는 Server role을
  등록한다. 별도 publish transport endpoint 없이 기존 Router 연결로 Logical Multicast를 제출했으며,
  topic을 구독한 User Spot은 marker를 한 번 받고 다른 stable type의 미구독 Spot은 받지 않았다.
  Gateway·Play·Client focused build와 actual process가 통과했고 증거는
  `framework/languages/node/e2e/SpotService/log/20260729-051542-2246502/`에 있다.
- .NET Config 12는 public `IZLinkRouteMeshRuntimeOptions`로 ClientServer Server weight를
  runtime에 `0`으로 변경하고 discovery 수렴 뒤 신규 선택에서 제외했다. 독립 Client는
  `100:300` 비율을 확인한 뒤 weight `0` Server를 한 번도 선택하지 않았고, 같은 process의
  Client+Server도 local weight가 `0`인 동안 remote Server만 선택했다. Weight를 복원하면
  Ready target 수가 다시 2로 수렴했다. 증거는
  `ChannelEgressRouting/logs/20260729-051159-2058781`,
  `logs/20260729-051215-2078936`, `logs/20260729-051223-2078916`이다.
  Accepted request drain·shutdown과 새 lifecycle generation 재시작 경쟁은 아직 남아 있다.
- C++은 application ingress에서 exact owner lease와 128-bit operation ID를 mailbox·accepted journal까지
  보존하고 command 31 typed capture를 production path에 연결 중이다. Lifecycle generation과 correlation으로
  operation ID를 다시 만드는 기존 dispatch 코드는 제거 대상이다.
- 공통 service wire 검증은 40 commands, 167 types, 4 flags와 37 bounds 기준으로 통과했다.
- `.NET` Config 12 Channel egress actual은 port `0` bind 뒤 실제 endpoint 게시, Node direct
  context의 실제 MeshName 전달, remote Logical Multicast와 ClientServer 교체 종료 순서를
  production 경로에서 보강했다. Remote Logical Multicast는 service wire schema의 command `23`과
  ChannelName·topic·source SpotId를 사용한다. Graceful drain은 auto-connect discovery descriptor를
  현재 owner fence로 제거한 뒤 Location owner lease를 해제한다. Node direct context focused 1/1,
  remote multicast·drain ordering focused 2/2가 통과했다. Actual은 `CH-REG-02`,
  `CH-REG-03`과 `CH-REG-05`의 현재 구현 범위를 통과했으며 최종 증거는
  `framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-004432-4150890/`다.
  이 실행의 모든 role stderr는 비어 있고 `ForceStopping`과 owner lease 오류가 없다. `CH-REG-05`의
  lifecycle generation 공개 증거와 이전 generation late reply 경쟁 variant는 아직 남아 있다.
- `.NET` Location provider close 회귀는 provider authority 31/31, participant 단위 parallel tree I/O
  10/10, canonical reservation owner 28/28, relocation runtime 144/144와 Framework build warning·error
  0이 통과했다. Actual `ReconcilePublishedStageAsync`의 in-process exact-publication retry도 canonical decode
  뒤 비어 있는 `CompletionPayload`를 authority `SourceCleanupState`에서 Pending·Completed로 각각 복원한다.
  수정 구현은 독립 high 재검토를 다시 시작할 수 있는 상태다.
- `.NET` solution contract migration은 현재 public API에 맞춰 sample·E2E·Stream Connector 호출부와
  regression assertion을 정렬했다. TicTacToe 실행 project를 `Server/Api`와 `Server/Play` 아래로
  옮겼고 Bingo Matchmaking project를 sample solution과 global solution에 등록했다. Bingo는
  matchmaking Instance Spot과 play User Spot을 별도 Mesh로 분리한다. ZoneWorld는 mutable Actor instance
  보관을 제거하고 global ActorId direct route를 사용한다. ZoneWorld 6개 project와 TicTacToe solution,
  Bingo·SupportChat·GameQuest·ShoppingMall·DeliveryDispatch build는 warning·error 0이다. 전체 Sample
  regression 125/125가 통과했다. Object Client가 소유한 bound Session으로
  보내는 Framework 내부 relay는 application용 Node direct 검사를 거치지 않고 정해진 Session route
  control packet만 내부 transport로 전달하도록 분리했다. 내부 relay와 public Object Client target
  거부의 focused 검증은 통과했다. Bingo actual process에서 확인한 Framework gap도 닫았다.
  Spot 내부 Channel 호출은 등록된 ClientServer Client runtime을 사용하고 RouteMesh Channel만 기존
  native Spot 경로를 사용한다. 호출별 timeout은 server discovery, connection admission과 request
  completion이 같은 deadline을 사용한다. Cross-node Actor join은 authority commit 뒤 sealed Session
  route에서 target의 membership·completion callback outbound를 허용하며 route commit 뒤 unseal한다.
  Relocation publication을 제거한 Store snapshot은 target Actor tracking에도 즉시 반영한다. User Spot을
  떠날 때는 최초 Entry Spot이 아니라 현재 node의 Entry Spot id와 generation을 authority에 기록한다.
  ClientServer focused 20/20, Actor handoff 61/61, Location lifecycle 17/17과 submit admission deadline
  focused 1/1이 통과했다. 최신 actual process는 build warning·error 0과 exit 0으로 끝났고 runner가
  `bingo-placement=completed`를 출력했다. 증거는 `/tmp/tmp.CiZnWKyBSK/logs`다. Client completion은
  `client.log:28`, matchmaking Instance Spot request·reply는 `matchmaking.log:4-7`, player1·observer·
  player2 join completion 각 1회는 `play-a.log:47,67,93`, 두 player의 결과 반영은
  `play-a.log:155,173`에서 확인했다. 이 변경을 포함한 전체 .NET UnitTests 1,242/1,242도
  통과했다. TRX는 `/tmp/zlink-unit-bingo/_ulalax-home_2026-07-29_00_16_30.trx`다.

- `.NET` Session binding은 replacement 상태를 `Prepared → Published → Completed` aggregate로 모았다.
  첫 previous-binding tombstone 성공 뒤 실패한 retry는 rollback하지 않고 같은 token으로 forward
  completion하며, publish 직전에 Actor authority·owner lease와 Session peer RID·lifecycle generation을
  다시 확인한다. Exact source fence, retired token과 generation 회귀를 포함한 focused 47/47이 통과했다.
  Store POSD 후속은 provider authority 43/43, provider relocation 4/4, Redis lifecycle 12/12,
  Redis opaque 10/10과 Redis build warning·error 0이 통과했다.
- `.NET` relocation timer는 admission 또는 owner fence가 먼저 닫혀도 handler failure를 만들지 않고
  pending tick을 logical snapshot에 남긴다. Target 복원 뒤 같은 tick을 정확히 한 번 처리한다.
  Timer lifecycle 11/11과 relocation focused 152/152가 통과했다. Actual `OBS-C11`은 Client-only Channel
  weight를 server처럼 낮추던 drain 오류와 target의 필수 RouteMesh peer가 Ready가 되기 전에 relocation
  offer를 승인하던 오류를 수정했다. 최신 실행
  `framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-223318-1927512/`에서는 source와 target이
  모두 필수 peer 두 개를 Ready로 확인하고 User Spot relocation을 완료했다. 동시에 이동한 Instance
  Spot의 cold-activation authority 검증과 command 35 publication 합류도 보강했다. Framework build는
  warning·error 0, canonical reservation focused 38/38이 통과했다. User Spot의 staging permit을
  인출할 때 reservation slot을 먼저 제거하던 경합도 수정했다. Slot은 durable terminal을 기록할
  때까지 유지한다. 같은 command 34 response가 겹치면 첫 요청 하나만 staging과 publication을
  수행하고, 나머지 요청은 같은 완료 결과를 기다린다. 따라서 target은 publication이 끝나기 전에
  ACK하지 않고 같은 payload를 중복 복원하지 않는다. Canonical reservation owner 38/38과 추가
  focused relocation·session·timer 회귀 98/98, 전체 unit test 1,233/1,233이 통과했다. 최신 actual 실행
  `framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-231144-2850416/`은 aggregate 두 개를
  commit하고 target PlayB에서 복원한 User Spot logical timer를 확인했다. 같은 intent 두 호출은 같은
  `Relocated` 결과로 끝났고 완료 뒤 재호출은 새 operation으로 처리됐다. Runner는
  `scenario OBS-C11 passed`와 `observability-ops client result=passed`를 출력했다. 로그에는 task failure,
  command 34 reservation 오류, relocation failure와 예외가 없다.
- `.NET` Spot Message Follow one-way는 `DontWait` 제출의 순간 backpressure 결과를 버리던 문제를
  수정했다. 기존 bounded send-ready queue가 같은 128-bit operation ID, generation·owner fence,
  hop과 metadata로 재제출하고 admission lease를 terminal까지 유지한다. Focused 1/1과 Actor handoff
  60/60이 통과했다. Trace가 native routing ID를 SpotId로 기록해 source Actor request를 collector가
  제외하던 문제를 수정했고 Spot route dispatcher와 exact flow focused test 6/6이 통과했다.
  Command 34 single-flight 수정 뒤 ACTORS-10 actual process를 연속 두 번 실행했다. 증거는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-233944-3394603/`과
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-234157-3450332/`이다. 두 실행에서
  Spot과 Actor는 각각 request 143/143과 one-way 159/159을 처리했다. 실행별 총 request 286건과
  one-way 318건에서 loss·duplicate는 0이고 Actor FIFO, global SpotId trace, request의
  flow·correlation pair를 확인했다. Spot과 Actor 10개는 각각 64 KiB application state를
  Capture·Restore했다. Relocation interruption은 각각 0.597029초와 0.569657초, source 마지막
  handler에서 target 첫 handler까지의 application gap은 각각 896 ms와 876 ms였다. Store I/O는 두
  실행 모두 최대 11개가 병렬로 실행됐다. 두 실행 모두 `required_gate=spotwide_service_continuity
  status=passed`와 runner PASS를 출력했다. 이 public continuity gate는 통과다. Exact stale-route Spot
  Message Follow는 이전 physical route를 public API로 고정해 release할 수 없어 별도 required gap으로
  남긴다. SpotWide atomic publication도 commit 전 participant 0개와 commit 뒤 전체를 함께 읽는 public
  observer가 없어 별도 gap으로 남긴다. Test 전용 raw route나 private probe는 추가하지 않았다.
- Public contract trace에서 확인한 3,062→3,088 member 변화는 신규 API 26개가 아니었다. 실제
  변화는 peer monitoring 계약 28개 추가와 stale member 2개 제거이며 공통 spec과 다섯 언어 exact
  interface에 부합한다. Baseline과 generated trace를 갱신한 뒤 문서 gate는 documents 51,
  blocks 172, owners 1,256, members 4,457과 unclassified·ambiguous·unknown 0으로 통과했다.
- `.NET` exact interface에만 남아 있던 ChannelName 생략 subscription attribute overload 두 개를
  제거했다. Logical Multicast subscription은 공통 계약대로 항상 `(ChannelName, topic)`으로
  식별하며, 다중 ChannelName 환경에서 임의의 기본 Channel을 선택하지 않는다. Source는 이미
  명시적인 ChannelName만 받으므로 구현 변경은 없다. Review baseline과 generated trace를
  의도적인 2-member 축소에 맞춰 갱신했다. 문서 gate는 documents 51, blocks 172, owners 1,256,
  members 4,455와 unclassified·ambiguous·unknown 0으로 통과했고 `.NET` contract test 70/70도
  통과했다.
- `.NET` TicTacToe Getting Started는 삭제된 `CreateGameHandler`와 Play channel 기반 room 생성 설명을
  제거했다. 현재 sample처럼 API가 `IZLinkSpotManager.Create`에 stable type과 최초 요청만 넘기고,
  Framework가 eligible Play Object Server와 `SpotId`를 결정하는 흐름으로 교체했다. 특정 `NodeRid`나
  endpoint로 Object를 배치하는 예제는 두지 않는다.
- `.NET` guide 05~14는 현재 exact interface와 sample 사용 흐름으로 다시 맞췄다. `SpotHandle`,
  `SpotRid`, `GetOrCreateAsync`, `ResolveAsync`, `SetEntrySpotRoutingId`, `RetireAsync`와 channel 기반
  Object 생성 예제를 제거했다. SpotId 기반 manager·client, deferred Join, Session binding,
  Location·Relocation Store 분리, `Relocate`와 `Shutdown`, 독립 Stream Connector를 설명한다.
  Node direct 예제는 Admin/Ops 조회에만 남겼다. 상대 링크 누락과 제거 surface 잔존은 0이며 문서
  contract gate와 `git diff --check`가 통과했다. Entry Spot만 소유하는 Actor destroy 설명과
  sample scenario inventory gate를 다시 연결한 뒤 sample regression 126/126도 통과했다.
- `.NET` HTTP client와 Stream Connector guide는 one-way terminal을 결과 없는 `Async()`로 통일하고
  내부 transport 설명을 제거했다. Exact contract에서 금지하지만 구현에 남아 있던 public
  `ZLinkHttpServerRequestBuilder.Yield<T>`도 source와 API snapshot에서 제거했다. HTTP client build는
  warning·error 0, Unit 63/63과 AutomaticTurnDispatch client build가 통과했다.
- `.NET` README와 internals도 같은 public boundary로 정리했다. Global SpotId·ActorId, manager 기반
  placement, Admin/Ops 전용 Node direct, `RelocateAsync(options)`와 `ShutdownAsync`, 두 Store SPI와
  실제 Message Follow·Instance cold activation test만 남겼다. 제거된 10.x surface 검색과 상대 링크
  검사가 통과했고, 문서 contract gate는 service wire 40 commands·167 types·234 negative tests,
  documents 51, blocks 172, members 4,457과 미분류 0으로 다시 통과했다.
- `.NET` solution에서 Bingo, TicTacToe와 ZoneWorld의 모든 sample project가 sample별 category와
  Server 하위 category에 등록된 것을 `dotnet sln ... list`와 `NestedProjects`로 확인했다. Bingo에는
  별도 Matchmaking Object Server가 등록되어 level별 Instance Spot을 실행하고, API는 matchmaking
  Mesh와 Play Mesh를 각각 Object Client로 사용한다.
- `.NET` sample scenario marker를 common sample spec과 다시 대조했다. Bingo의
  `BINGO-PLACEMENT-4`는 문서에만 있고 reverse Play startup을 실행하지 않던 gap을 찾아
  play-b→play-a 순서와 여섯 placement assertion 뒤의 phase marker로 연결했다. ZoneWorld
  `ZW-B6` Message Follow는 이전 owner route를 주입할 public 운영 probe가 없어 실제 구현이
  남아 있으며, runner가 해당 ID를 기록하지 않아 relocation·전체 완료 marker도 의도대로
  차단된다. Test 전용 raw route 우회나 fake pass는 추가하지 않았다. Linux에서 Bingo 8,
  TicTacToe 5, ZoneWorld 6, ShoppingMall 7, SupportChat 7개 sample project를 순차 build해
  총 33/33이 warning·error 0으로 통과했다.
- `.NET` E2E registry를 Config 1~13의 공통 selector 340개와 다시 대조했다. 모든 selector를
  feature-map에 등록하고 inventory regression 1/1을 통과시켰다. Config 12에는 공통
  Shared·Server·Client와 실제 process runner를 추가했다. RouteMesh 양방향, handler의
  `audit.record`·`workflow.command` 연속 호출, ClientServer weighted selection, 등록 충돌,
  미등록 ChannelName의 즉시 `NotFound`, result-free send, remote `ToChannel`, 같은 process의
  Client+Server, terminal uniqueness, classic fanout, port 0 endpoint와 일곱 sample fixture를
  검증하는 selector source가 warning·error 0으로 build된다. `CH-E2E-03`·`CH-E2E-08`·
  `CH-REG-02`·`CH-REG-05`와 일부 selector의 drain·protocol injection·STREAM variant는 남아
  있다. 따라서 `all`은 누락 항목을 출력하고 실패하며 aggregate runner에도 아직 등록하지
  않았다. Actual 과정에서 automatic fanout publisher가 최신 binding에서 지원하지 않는 PUB
  socket `RoutingId` option을 설정해 startup이 실패하는 production gap을 발견했다. Publisher
  RID는 Location descriptor identity로만 유지하고 route-addressed socket이 아닌 PUB에는 설정하지
  않도록 bundle과 internal backend surface를 정리했다. Focused regression 1/1과 Config 12
  Server build warning·error 0이 통과했다. 이어서 RouteMesh가 `NotConnected`로 남은 원인을
  확인했다. `ZLinkManagedMeshNode`가 port 0 bind 뒤 실제 endpoint를 저장하지 않아 모든 Location
  descriptor가 `tcp://127.0.0.1:0`을 게시했고, planner가 endpoint equality로 모든 remote row를
  self로 제외했다. Bind 직후 socket의 actual endpoint를 저장하도록 production runtime을 수정하고
  port 0·distinct peer planner 회귀 2/2를 추가했다. `CH-E2E-01` actual은 session·play의 실제
  Ready peer를 기다린 뒤 통과했다. Location evidence의 api `:2037`, play `:8177`, session
  `:29187` endpoint가 서로 다르고 session·play topology의 ready peer는 각각 2개다. 증거:
  `framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-000549-3638572/`.
  다음 static checkpoint에서 Config 12의 Session·Play에 Redis Relocation Store와 stable
  Snapshot Actor·User Spot factory를 등록하고 WorkflowServer를 `game` Object Client로 연결했다.
  Play Entry Spot Actor handler의 ClientServer `Async` 후 state mutation, timer의 `Yield` 후 resume,
  WorkflowServer handler의 global SpotId→ActorId 연속 request, Node·Spot·Actor direct와
  Session Entry→Play User Spot deferred Snapshot join source가 build 대기 상태다. Runner는 같은
  ClientServer endpoint에서 WorkflowServer를 교체해 새 RID 선택과 이전 RID 제거를 확인하도록
  확장했다. Logical Multicast remote Spot과 classic fanout은 별도 exactly-once evidence로
  검증한다. `CH-REG-02`의 bound-session push, `CH-REG-05`의 lifecycle generation public evidence와
  이전 generation late reply 경쟁은 아직 남아 있으므로 aggregate는 등록하지 않았다.
  첫 통합 actual 두 번은 object role이 없는 process의 endpoint DI 등록과 Object Client의
  Node-direct handler 등록을 찾아 startup 전에 수정했다. 세 번째 실행
  `framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-002803-3948595/`에서는
  Entry Spot Actor handler가 ClientServer reply 뒤 state를 1에서 2로 변경했지만 timer가
  `Yield` 시작 뒤 resume하지 않았다. 공통 Config 12 문구가 Entry Spot·`PerActor`에서 `Yield`를
  금지하고 automatic continuation을 사용하는 정식 execution contract
  (`06-framework-api.ko.md` §8.3, `12-spot-messaging.ko.md` §9.3)와 충돌한 것이 원인이다.
  Config 12와 .NET timer를 `Async`로 수정했다. `Yield` 검증은 `SpotWide` User Spot 시나리오가
  계속 소유한다. 이 실패 뒤의 E2E08·REG02·REG03·REG05는 실행되지 않았으므로 통과로 계산하지
  않았다.
  Cross-node `ToChannel`
  `RM-C1`은 local Server weight를 0으로 설정한 뒤 ChannelName만 사용해 remote
  request·send를 처리했고
  `framework/languages/dotnet/e2e/LocationMessaging/logs/20260728-232700-3148011/`에서
  통과했다. Cross-node `ToSpot` `SM-F2`도 source weight 0·target weight 100에서 global
  SpotId만 사용해 remote request·send를 처리했으며
  `framework/languages/dotnet/e2e/SpotService/logs/20260728-233321-3170089/`에서
  통과했다. One-way E2E endpoint는 source-local admission 뒤 local handler evidence를
  기다리지 않도록 계약에 맞췄다.
- `.NET` Framework UnitTests는 60초 안에 끝나지 않았지만 정지한 것이 아니었다. 병렬 실행을 끈
  기존 assembly 설정과 `--blame-hang-timeout 45s`를 유지한 full run은 약 2분 19초에 정상
  종료한다. 실제 failure 여섯 건도 닫았다. Message flow test는 제거된 전용 file sink 대신
  명시적으로 diagnostics를 켜고 `ILogger` 출력을 검증한다. Sample rate 범위 오류는 exact
  contract의 `ArgumentOutOfRangeException`과 맞췄다. Framework assembly의 internal telemetry
  type은 public contract 디렉터리에서 `Runtime/Diagnostics`로 옮겼다. Actor Join과 User Spot
  Close가 경쟁할 때 Join이 먼저 commit되면 local activation 존재를 확인하고 close는 deadline까지
  반복하지 않고 `false`로 끝난다. UnitTests build는 warning·error 0이며 focused 20/20과 full
  1,233/1,233이 통과했다. Full TRX는
  `/tmp/zlink-unit-clean/_ulalax-home_2026-07-28_23_33_11.trx`다.

2026-07-28 11:30 KST 기준 실행 행 205개의 상태는 완료 135개, 진행 19개, 수정 진행 14개,
대기 37개이며 차단 0개다. `.NET` public boundary는 internal-only Location record를 runtime으로
옮기고 source·package snapshot을 현재 public assembly와 맞췄다. Packaged consumer와 standalone
HTTP 검증, Scaffold 7/7, Contract 69/69, Unit 1,191/1,191과 `git diff --check`가 통과했다.
API·package snapshot의 SHA-256은
`73bda45f50eeaf1f81c9feee5de434a9ecc48de48e26719bceffd086add5da58`이며 재생성 결과와 차이가
없다. StoreFailure E2E의 Location Store
wrapper도 domain-specific authority operation을 제거하고 opaque `Read`·`Write`·`Scan`만 사용하도록
전환했으며 Consumer Release build warning·error 0이 통과했다. 같은 revision의 문서 gate는
40 commands, 167 types, 4 flags, 37 bounds, exact document
51개, declaration owner 1,251개, member 4,435개와 unclassified·ambiguous 0으로 통과한다.

2026-07-26 서버 전원 장애 뒤 변경 파일 두 개가 0 byte가 된 상태를 확인했다. 사용자 변경이 없음을 확인한
뒤 해당 두 파일만 복구했고, 변경 파일 전체에서 추가 0-byte 손상은 발견되지 않았다. Schema validator와
`git diff --check`도 다시 통과했다. 복구 범위와 증거는 `BLK-045`에 기록했다.

안정 checkpoint마다 이 문서와 blocked issue log를 먼저 갱신한 뒤 전체 언어 gate에 방해되지 않는 단위로
commit·push한다. `framework/doc/plan/log/`는 이 작업에서 수정하지 않는다.

### 0.0.3 2026-07-27 relocation recovery checkpoint

현재 상태 기준 revision은 `17d7280ea7`이다.

- 공통 문서 contract gate가 통과한다. Service wire는 40 commands, 167 types, 37 bounds이며 negative
  self-test 233개가 통과했다. Public contract trace는 document 51개, declaration owner 1,252개,
  member 4,389개이며 unclassified·ambiguous 항목은 0이다.
- `.NET`은 command `40 → 30 offer → 30 accept → 41`의 역할별 payload 모양을 회귀 test로 고정했다.
  Codec focused test 6/6, reservation owner test 16/16, documentation regression 15/15와 전체 Unit
  1,072/1,072가 통과했다. Mixed-language process relocation E2E가 남아 있으므로
  `V11-M6-DN-REFERENCE`와 `BLK-044`는 계속 진행 상태다.
- C++은 Store에서 만료 descriptor를 제외한 live view만 읽고, Store 복구 뒤 owner lease 확인과
  descriptor 재등록이 끝나기 전에는 기존 automatic connection을 제거하지 않는다. Target contract,
  sample parity 54/54와 live-row focused test가 통과했다.
- C++ deferred Actor Join은 non-zero operation ID, Actor queue barrier 예약, `Rejected`·`Failed`
  callback 전달과 callback 실패 재시도를 구현했다. 미분류 실패는 `RequestFailed`,
  `retryable=false`로 통일했으며 execution test가 통과했다.
- 실제 C++·JVM·Node host lifecycle source에는 새 `Relocated`·`Relocate`·`Shutdown` public contract가
  아직 모두 반영되지 않았다. 언어별 차이는 implementation gap §12.51이 소유한다.

Impact manifest quarantine과 mixed-language process relocation E2E는 아직 통과하지 않는다. 이
checkpoint는 해당 두 gate를 승인하거나 완료로 바꾸지 않는다.

### 0.0.4 2026-07-27 M6 runtime 병렬 구현 checkpoint

현재 변경은 `115d34281f` 이후의 C++·JVM·Node.js M6 runtime 작업을 묶는다.

- C++은 host lifecycle을 `Relocate`와 `Shutdown`으로 분리했다. Relocation 성공 뒤 runtime은
  `Relocated` 상태로 유지되며 Shutdown이 별도로 resource를 정리한다. 이전 `retire`·`drain`·
  `await_drained`와 per-Mesh drain 공개 API는 제거했다. Framework build, host lifecycle,
  contract header와 target contract는 각각 통과했고 Store resolver 37/37, Redis 16/16도 통과했다.
  이전 Store SPI와 Spot factory를 사용하는 일부 broad E2E compile gap은 계속 진행 상태다.
- JVM은 Entry Spot Actor relocation의 source capture와 target staging owner를 production runtime에
  연결했다. Target은 restore와 accepted journal replay를 live registry 밖에서 수행한 뒤 publish한다.
  Java core 587/587과 Kotlin 46/46가 통과했다. 실제 host endpoint, aggregate finalize와 two-owner
  recovery E2E는 `BLK-043`의 남은 조건이다.
- Node.js는 공개 Location Store를 opaque `read`·`write`·`scan`·`dispose` SPI로 줄이고 domain operation을
  Framework private repository로 옮겼다. Owner lease와 MeshNode descriptor의 production vertical
  slice는 이 SPI만 사용한다. NestJS HTTP client runtime도 scheduler error가 발생할 때만 lazy resolve한다.
  Typecheck와 location·NestJS focused regression 81/81이 통과했다.
- Public contract trace는 document 51개, declaration owner 1,248개, member 4,383개이며 unclassified와
  ambiguous 항목은 0이다. Impact manifest는 6,381개를 생성했고 quarantine은 pending 5,649,
  executed 0, skipped 0으로 통과했다. 삭제된 regression과 변경된 runner는 검토한 대체 파일과 exact
  hash로 고정했다. Framework document contract gate도 통과했다.

이 checkpoint는 C++ broad E2E, JVM two-owner recovery, Node production host 전체 연결과
mixed-language process relocation을 완료로 판정하지 않는다. 해당 작업은 기존 M6 행과
`BLK-043`·`BLK-044`에서 계속 관리한다.

### 0.0.5 2026-07-27 M6 runtime·E2E 정합성 checkpoint

현재 commit 기준 revision은 `c719dea69d8a9e99946784fe7c2c202242cec4fb`이다.

- `.NET`은 이 revision으로 새 `M6-RUNTIME` candidate와 result를 만들었다. Compile warning·error 0,
  public contract 65/65, runtime 1,057/1,057, resource 89/89와 protocol 91/91이 통과했다.
  제거 증거도 symbol·file·artifact 잔여 0으로 통과했다. 다만 공개 Location Store가 정식 exact
  interface의 작은 opaque SPI가 아니라 domain operation을 직접 노출하므로
  `V11-M6-STORE-POSD-DN`과 `.NET` reference 행은 완료로 바꾸지 않는다.
- `.NET` host lifecycle public source는 `RelocateAsync(options)`와 별도
  `ShutdownAsync`, `Relocating → Relocated` 상태, 분리된 relocation·termination result로
  전환했다. Relocation 성공은 stateful workload만 source dispatch에서 분리하고
  infrastructure를 종료하지 않는다. Shutdown을 별도로 호출해야 `Draining → Stopped`로
  전이한다. AspNetCore build는 warning·error 0, public contract 67/67과 lifecycle focused
  unit test 40/40이 통과했다. Actor·Spot target scheduler도 PlannedMaintenance의
  same-version과 RollingUpdate의 caller 지정 exact-version 조건을 preflight와 실제 후보
  선택에 동일하게 적용한다. ST-I2·I3 process E2E와 최신 전체 unit regression 완료 전까지
  lifecycle 행은 진행 상태로 유지한다.
- JVM은 Entry Spot standalone Actor를 production relocation control 경로에 연결했다. Hidden target
  restore, source freeze, authority commit, accepted journal replay, source cleanup, steady authority
  normalization과 target admission 개방을 순서대로 수행한다. Relocation 전후 object generation은
  유지하고 authority owner generation만 1 증가한다. Java core 591/591과 Kotlin 46/46이 통과했다.
  Target inbound permit, durable reply ACK, Session route ACK, PerActor timer와 process recovery는
  `BLK-043`에서 계속 진행한다.
- C++은 이전 Store SPI를 사용하던 DiscoveryRegistryHa와 ObservabilityOps의 broad compile gap을
  현재 public surface에 맞췄다. MeshNode vertical, Discovery provider·consumer·client,
  Observability session·play·workflow·client와 message-flow focused test가 통과했다. Scenario를
  삭제하거나 비활성화하지 않았다. 다만 Observability C track은 Config 11의
  `Relocate → Shutdown` 대신 이전 drain 동작과 C1~C5만 사용하고, public header에도 제거 대상
  `drain_state_t`·`drain_event_t`가 남아 있으므로 M6 또는 E2E 완료 증거가 아니다.
- 공통 E2E 문서의 상대 link와 이전 계약 용어를 검사했다. 깨진 link와
  `SpotRid`·`Checkpoint`·`PlacementProfile`·`AffinityKey`·`Retire` 잔여는 0이다. Config 10과
  Config 11은 현재 relocation·maintenance 계약과 일치한다.
- 실제 언어별 E2E는 아직 정식 spec과 일치하지 않는다. `.NET` SpotActorTransfer는
  `ST-F3A`·`ST-G1~G6`·`ST-H1~H5`, ObservabilityOps는 `OBS-C6~C11`, SpotService는
  `SM-A9~A13`·`SM-B0`·`SM-B0A`·`SM-B10`·`SM-B11`·`SM-G5`가 없다. 일부 source에는 제거된
  handler context, `SpotRid`, automatic topology의 fixed Node RID와 이전 drain API도 남아 있다.
  Feature map은 이 항목을 `미구현` 또는 `전환 대상`으로 바로잡았으며, scenario를 삭제하거나
  assertion을 약화하지 않고 현재 spec에 맞게 구현한다.
- Java SpotActorTransfer도 component test를 process E2E 완료 증거와 분리했다.
  `ST-F3A`·`ST-G1~G6`·`ST-H1~H5`는 현재 runner가 실행하지 않으므로 `미구현`으로 기록했다.
- C++ SpotActorTransfer의 `all` selector도 A~F만 실행하면서 공통 시나리오 전체를 실행한다고
  설명하던 오류를 바로잡았다. `ST-F3A`·G track·H track의 process gap을 명시했다.
  C++·Java Observability의 이전 drain 기반 C1~C5 결과는 현행 Config 11 PASS가 아니며,
  C1~C5는 `전환 대상`, C6~C11은 `미구현`으로 기록했다.
- C++ E2E의 Spot 주소는 string `SpotId`로 전환했고 Spot ID를 `RoutingId`로 변환하는 호출을
  제거했다. Message Follow marker도 `message_follow_relay`,
  `message_follow_route_removed`, `message_follow_expired`로 맞췄다. Framework와
  M6B runtime·Actor gateway·service wire focused test, SpotActorTransfer·SpotService client와
  Observability target build는 통과했다. 그러나 SpotActorTransfer server는 이전 factory·handler
  context 등 public API migration gap 때문에 compile되지 않으며 `ST-F4/F5`도 old `ActorRef`
  직접 주입의 일부 검증뿐이므로 `partial`이다. Track I은 모두 `미구현`을 유지한다.
- `.NET` SpotActorTransfer는 제거된 handler context, fixed route `ActorRef`,
  `SpotRid`, MeshNode ChannelName과 Actor dispatch option을 현행 MessageContext,
  global ActorId, `SpotId`와 object server registration으로 전환했다. Shared와 ActorNode,
  SessionGateway, Client가 warning 0으로 build됐다. `ST-F4`·`ST-F5`의
  old ActorRef HTTP payload와 사용하지 않는 NodeRid·Generation을 제거했다. HTTP target은
  submitting process만 선택하고 Framework에는 global Actor ID만 전달한다. Bounded route
  cache가 있는 process에서 resolver 뒤 transport delivery를 operation ID별로 지연한다.
  두 행은 process 재검증 전이므로 `부분 구현`이다.
- Node.js는 opaque Location Store repository를 ClientServer, fanout, Spot·Actor route와 owner cleanup에
  연결하고 public lifecycle을 `relocate(options)`와 `shutdown(options)`로 분리했다. Exact version
  filtering과 `Relocated` 상태를 포함하며 typecheck·lint, location·topology 34/34, M6A 9/9,
  M6B 39/39과 M6C 64/64가 통과했다. Public export 7개, concurrent same-option join,
  relocation 중 Shutdown의 current-unit barrier와 owner descriptor cleanup 정렬은 남아 있다.
- 공통 E2E 1~14와 다섯 언어 runner·feature map을 다시 대조했다. 사용자 요구를 반영해 Config 10은
  payload·대량 relocation·서비스 연속성·Message Follow `ST-I1~I6`을 추가한 41개가 정본이다.
  실제 selector 등록은 C++ 20개, .NET 34개, Java 20개, Kotlin 20개, Node.js 20개다.
  `.NET all`은 process 검증 전 F4/F5와 부분 Track I을 제외해 19개만 실행한다. `.NET`은
  `ST-I1~I6` selector와 fixture를 연결했다. Instance Spot payload 네 profile의
  activation-only 진단과 SpotWide 64 KiB는 관찰했지만 relocation 완료 증거는 아니다.
  Actor Message Follow도 과거 runtime marker 진단만 관찰했으며 public black-box
  process 재검증은 통과하지 않았다.
  `ST-I2/I3`의 정본 규모 실행은 아직 없으므로 `diagnostic_only`다. `ST-I2` 1+1 축소
  실행은 target admission과 relocation 중 control·이동 대상 traffic을 통과했지만
  제거 대상 callback을 terminal로 사용한 판정이 남아 완료 증거로 사용하지 않는다.
  정본 규모, 모든 per-unit terminal, SpotWide member
  aggregate와 Spot Message Follow 전체 조합은 남아 있다. 다른 네 언어의 I track은 아직 없다.
  Java/Kotlin·Node.js·C++의 공식 `all`은 old route 직접 주입에 의존하는 전환 대상
  F4/F5도 실행하므로 해당 결과를 현행 F4/F5 완료 증거로 사용하지 않는다.
  Config 11 정본 19개 중 다섯 언어 모두
  A1~C5만 등록하고 C6~C11은 없다. Config 12는 다섯 언어 모두 aggregate runner와 project가 없고,
  Config 14는 다섯 언어 모두 process E2E가 0이다. Config 14의 완료 조건은 현재
  `IS-E2E-01~36`으로 표와 일치한다.
- Config 1~8에서도 공통 scenario ID 누락을 확인했다. Config 1은 `RM-A7`·`RM-C10`, Config 2는
  C++·Java·Node의 신규 Spot scenario 10개, Config 3은 `PS-F1~F5`, Config 4는 `RC-B6`,
  Config 5는 `RL-E1~E5`·`RL-F1~F14`, Config 6은 `SF-B3`·`SF-C3~C5`·`SF-F1~F11`·
  `SF-G1~G3`, Config 7은 `MON-A6`, Config 8은 Java 외 세 언어의
  `TD-D4~D6`·`TD-E2A`·`TD-F5A`가 빠져 있다. 이 수치는 완료 증거가 아니라 scenario·runner·
  feature map을 현행 spec에 맞추는 작업 목록이다.
- Message Follow 구현도 공통 spec과 직접 대조했다. `.NET`은 Actor와 Spot route를 모두
  구현했고 Spot request의 absolute deadline을 wire·inbound record·follow hop에 보존한다.
  만료된 ingress는 handler queue 전에 끝내며 late reply를 폐기한다. Focused runtime 26/26,
  relocation 125/125와 Spot Message Follow 6/6이 통과했다. Java/Kotlin·Node.js·C++는
  Actor route의 일부만 구현했고 Spot route가 없다. 네 언어
  모두 Actor·Spot × one-way·request × commit 전·후, duplicate·expiry·deadline·generation·loop·
  hop·record·byte 제한과 multi-hop cleanup을 process E2E로 검증하지 않는다. 이 차이는
  `30-implementation-gap.ko.md` §12.52와 Config 10 `ST-I4~I6`이 소유한다.
  후속 production source 감사에서 Java/Kotlin과 Node의 Actor relay request가 original
  absolute deadline 대신 기본 timeout을 다시 적용함을 확인했다. Node는 이후 absolute
  deadline을 internal stream metadata, accepted handoff와 relay envelope에 보존하고
  handler admission 전 expiry와 late reply 폐기를 구현했다. Build·lint와 Actor
  handoff/client focused 25/25가 통과했다. Java/Kotlin의 gap은 남아 있다.
  C++ production relay는
  target route를 사용하지만 message 수·byte·hop 제한을 적용하는 admission API가 unit
  test에서만 호출되고, 새 30초 request envelope를 만들어 original operation·deadline·
  correlation을 보존하지 않는다. Actor client의 fresh-owner 재시도도 남아 있다.
  세 runtime 모두 Spot Message Follow owner, duplicate·loop 전체 의미,
  durable multi-hop recovery와 Track I process workload 증거가 없다.
- `.NET` `ST-F4/F5`와 `ST-I4/I5/I6`의 완료 판정에서
  `message_follow_*` runtime log marker 의존을 제거했다. Relocation 전에 resolve된
  delivery를 operation ID별로 지연한 뒤 public terminal과 application handler evidence를
  사용한다. Final owner handler는 정확히 한 번, 이전 owner handler는 0회여야 한다.
  Request는 reply marker와 original timeout을 보존하며 duration 뒤 release는
  `ActorLocationStale`, 모든 application handler 0회여야 한다. Route entry 수와 next-hop
  내부값은 public black-box로 관찰할 수 없으므로 process 성공으로 계산하지 않고
  provider/runtime contract test blocker로 남겼다. Client와 ActorNode build는 warning·error
  0이다. `ST-F4` 재실행 `logs/20260728-025621-573976`은 target `transfer_in` 뒤에도
  deferred Join completion이 source에서 `reject_reply`였고, duration 안 release한 `G1`을
  source handler가 처리해 실패했다. 기존 marker 판정이 가리던 production gap이므로
  `ST-F4/F5`와 Track I Message Follow를 통과로 표시하지 않는다.
- Message Follow 기본 duration도 언어별 registration에서 직접 확인했다. 공통 계약과 `.NET`·C++의
  기본값은 30초인데 Java host registration과 Node.js registration만 5초를 사용했다. 두 기본값을
  30초로 맞추고 Java focused test 1/1, Node.js focused test 1/1을 추가해 통과했다. Node.js 전체
  `drain-control` 14개 중 이 변경과 무관한 exact peer readiness 1건은 계속 실패하므로 broad
  regression 완료 증거로 사용하지 않는다.
- `.NET` 내부에서도 같은 기능을 `ForwardingWindow`, `ForwardingMapping`, `StragglerForwarder`로
  부르던 이름을 Actor·Spot `MessageFollow` route·lease·dispatcher와
  `MessageFollowHopCount`로 정리했다. Wire field 순서는 바꾸지 않았다. Relay, duration 종료,
  종료 뒤 old route와 generation·bound 거부 marker도 서로 구분했다. Framework build warning·
  error 0, focused 49+90+124+26과 전체 Unit 1,074/1,074가 통과했다. Actor ingress의 남은
  `ActorSessionForwarder.TryForward` 이름도 실제 책임에 맞춰
  `ActorMessageFollowDispatcher.TryFollow`로 바꾸고 해당 dispatch test 90/90을 다시 통과했다.
  공통 spec·E2E, 다섯 exact interface와 네 runtime source에서 이전
  `ForwardingWindow`·`ForwardingMapping`·`StragglerForwarder`·`MessageFollowWindow`
  식별자를 다시 검사한 결과는 0이다. 공개 설정 이름은 `.NET`
  `MessageFollowDuration`, Java/Kotlin `messageFollowDuration`, Node.js
  `messageFollowDurationMs`, C++ `message_follow_duration`으로 고정한다.
  Route lifecycle marker도 `message_follow_registered`, `message_follow_relay`,
  `message_follow_route_removed`, `message_follow_expired`, `message_follow_rejected`로
  고정했다. `.NET` Actor의 `message_follow_route_installed`와 Java Actor의 실제 route
  등록 시점에 쓰던 `source_cleanup`을 `message_follow_registered`로 바꿨다.
  Java/Kotlin Actor에는 relay·제거·만료·거부 marker, C++ Actor에는 거부 marker가
  남아 있으며 Java/Kotlin·Node.js·C++에는 Spot route 자체가 없다. `.NET` Actor
  handoff·dispatch focused 139/139, Java Actor handoff·registration·Location option
  focused test와 Java SpotActorTransfer Shared·ActorNode·Client compile이 통과했다.
  Framework document contract gate도 40 commands, 167 types, 37 bounds와 exact
  document 51개 기준으로 통과했다.
- `.NET` opaque Store SPI와 Redis generic storage contract는 public boundary·Unit·Redis focused
  test를 통과했다. Production repository도 process-local fallback 상속을 제거하고 authority,
  creation reservation, relocation capacity와 aggregate operation을 public opaque Store의
  conditional batch에 저장한다. 실제 Redis 2-process focused test와 Redis↔in-memory
  authority·generation parity가 통과했다. AutomaticTurnDispatch의 이전 개별 selector 실행은
  scenario 0개인 상태에서도 source gate만으로 성공할 수 있어 완료 증거에서 제외했다. Runner는
  canonical selector 검증과 실제 실행 수 대조를 추가했고, `20260728-035646-2274534`에서
  `TD-A2` 1개를 실제 실행해 통과했다.
  이전 `Conflict(Missing)` Store blocker는 해소됐다. Redis package의 중복
  `IZLinkLocationRepository`·domain Lua·key codec과 물리 schema 전용 test도 제거했다.
  Public package는 opaque `IZLinkLocationStore`의 read·conditional write·scan과
  connection·options·key namespace만 유지한다. 두 독립 Redis provider instance를 Framework
  private repository로 연결한 authority test와 전체 Redis test 25/25가 통과했다.
- `.NET` SpotActorTransfer는 Track I payload 관측을 위해 public
  `IZLinkRelocationStore` wrapper를 추가했다. Wrapper는 private envelope와 Store key를
  해석하지 않고 opaque blob의 byte 수, payload SHA-256과 opaque reference SHA-256만
  기록한다. `ST-I1`은 4 KiB·64 KiB·8 MiB·64 MiB payload profile, `ST-I4`는 commit
  뒤 Actor one-way·request, `ST-I5`는 correlation·expiry,
  `ST-I6`은 Actor multi-hop과 route cleanup scaffold를 실제 public API로 연결한다.
  I1은 profile마다 Store 측정을 reset하고 expected application·encoded Capture·restore
  byte SHA를 독립 계산해 대조한다. I4~I6은 resolver가 선택한 delivery를 actual submit
  직전에 operation ID별로 지연하므로 relocation 뒤 fresh resolve로 바뀌지 않는다.
  `ST-I2`와 `ST-I3`에는 Actor 10,000+1,000개, Instance Spot 1,000개,
  SpotWide 100개×Actor 100개의 정본 workload와 control traffic 측정을 연결했다.
  다만 I4의 source baseline은 seal 직전 queue 경계가 아니고, I5 absolute deadline과
  I6 handler operation identity 증거는 아직 없다. ActorNode와 Client build는 warning 0,
  error 0이다. Mode별 exact-version target selection은 Actor·Spot preflight와 실제
  scheduler까지 연결했다. PlannedMaintenance는 source와 같은 version,
  RollingUpdate는 caller가 지정한 더 높은 exact version만 허용하고 `>=` fallback은
  제거했다. 관련 relocation test 159/159와 최신 전체 Unit test 1,077/1,077이 통과했다.
  Track I는 process terminal과 전체 matrix가 남아 계속 `부분 구현`이다.
  `ProbeRefAsync`·`SendRefAsync`와 route-bearing HTTP DTO는 제거했다. Node별 endpoint는
  submitting process를 선택할 뿐이며 public Actor call에는 global Actor ID, message와
  timeout만 전달한다. Message Follow duration은 fixture에서 7초, stale route cache는 2초로
  두 값의 최소 5초 차이를 지킨다. Expiry와 이전 generation delivery는 commit 전에
  선택된 route delivery를 보존하는 transport fixture가 생길 때까지 완료로 판정하지 않는다.
  I1 Instance Spot 4 KiB·64 KiB·8 MiB·64 MiB는
  `logs/20260727-232318-914304`에서 자동 remote placement, byte 수와 SHA-256을 통과했다.
  SpotWide
  64 KiB·1 MiB·32 MiB·64 MiB adapter, opaque Store 총 byte·checksum과 peak RSS까지
  확장했다. Remote `ST-B1`은 identity·phase·source release·unbound completion fence를
  고친 뒤 `logs/20260727-235500-1282505`,
  `logs/20260727-235556-1284429`에서 연속 통과했다.
  SpotWide commit 직후 public lookup이 canonical target route를 해석하도록 고쳤다.
  `logs/20260728-000859-1518859`는 65,536 B application state, 66,247 B Store put,
  711 B envelope overhead와 restore checksum을 통과했다.
  Actor adapter가 정확히 64 MiB를 반환하는 독립 selector도 추가했다. 첫 실행
  `logs/20260728-004839-2032371`은 fixture header 74 bytes를 잘못 더해 adapter
  limit을 넘긴 문제를 찾았고 fixture를 수정했다. 수정 뒤 process evidence는 아직 없다.
  Instance Spot profile은 정식 64 KiB·1 MiB·32 MiB·64 MiB로 고치고
  activation-only selector를 실제 host relocation·target Restore·final location·
  Store read-back 검증으로 바꿨다. `logs/20260728-022204-146992`는 네 fixture를
  생성했지만 source Capture 3/4, target Restore 0에서 5분 deadline과 HTTP 499로
  끝났으므로 완료 증거가 아니다. Queue·journal·timer, permit contention과
  320 MiB·5-participant aggregate가 없어 ST-I1은 `diagnostic_only`다.
  SpotWide 64 MiB 실행 `logs/20260728-003937-1922103`은 typed failure 없이 relocation
  진행 중 외부 5분 execution limit에 도달했으므로 완료 증거가 아니다.
  I4 source dispatch와 request reply relay를 고친 뒤 과거 diagnostic 실행
  `logs/20260728-001043-1662047`에서 Actor held one-way·request,
  Message Follow route 등록·relay·제거 marker를 관찰했다. I5도 과거 diagnostic 실행
  `logs/20260728-002118-1750005`에서 correlation A/B 역순 release,
  original deadline, late reply 폐기와 expiry stale rejection marker를 관찰했다.
  두 실행은 현행 public black-box 완료 증거가 아니다.
  I6 첫 hop의 benign lease-renewal StoreVersion conflict는 latest snapshot으로
  commit을 재시도하도록 고쳤다. 단독 `logs/20260728-003822-1911426`은 두 번째
  target restore·location commit 뒤 source cleanup completion이 끝나지 않아
  multi-hop 완료 증거가 아니다.
  축소 I2 `logs/20260728-000226-1408117`과 I3 Instance
  `logs/20260728-000501-1448286`은 control baseline을 오류 없이 처리했지만 relocation
  terminal과 target restore에 도달하지 못했다. 정본 규모의 시간·연속성 증거로 사용하지
  않으며 Track I 전체를 `all`에서 제외한다.
  I2 최소 1 Recreate+1 Snapshot 반복 `logs/20260728-005525-2344250`과 30초 진단
  `logs/20260728-010329-2556033`도 target preflight 뒤 source relocation 내부에서
  nonterminal이었다. Target Restore·admission은 0이며 remote workload reply는
  reply capability 누락으로 retry됐다. 두 현상의 인과를 분리해 수정해야 한다.
  이후 command 40의 Actor stable type 정규화, canonical decode 우선순위,
  target replay 완료 전환과 relocation journal request의 opaque reply capability를
  수정했다. 이후 callback removal, deadline normalization과 Message Follow 회귀까지
  포함한 전체 `.NET` Unit 1,090/1,090와 public Contract 67/67이 통과했다.
  `logs/20260728-020553-3911921`의 1+1 축소 실행은 Recreate·Snapshot Actor의
  target admission과 이동 중 request·one-way 3/3, 오류 0을 확인했다. 다만 당시
  제거 대상 `OnActorRelocatedAsync`와 commit 전 `RestoreAsync`를 per-unit terminal로
  사용했으므로 이 실행도 ST-I2 완료 증거로 승격하지 않는다.
  Terminal store를 제거하고 host terminal·final location·post-terminal handler로
  바꾼 `logs/20260728-023349-253573`은 Relocate 298 ms와 control traffic
  14/14를 통과했지만 Core의 unbounded deadline `ulong.MaxValue`를 Framework가
  `long`으로 cast해 relay frame을 drop하는 실제 gap을 찾았다. Deadline adapter와
  dispatch pump에서 unbounded를 Framework `0`으로 정규화하고 finite deadline은
  보존했다. `logs/20260728-024755-386483`에서는 relay overflow가 0건이었지만
  별도 relocation unit 실패로 host가 `Blocked/RelocationFailed`가 되어 ST-I2
  완료 증거는 아직 없다.
  E2E 교차 리뷰 뒤 scale 실행은 `diagnostic_only`로 분리하고 request·one-way를
  각각 독립 open-loop pacer로 바꿨다. 생성 count를 완료 수로 출력하던 부분도
  제거했다. Operation ID·absolute deadline·request correlation, relocation 전후
  public 위치, host terminal과 terminal 뒤 handler 결과를 대조하며 SpotWide member
  전체의 최종 owner도 확인한다. Spot request correlation은 public
  `IZLinkRuntimeMessageFlowObserver`의 `received`·`replied` event를 대조한다.
  Public final location, `ObjectGeneration`, host `RelocateAsync` terminal과
  terminal 뒤 handler 결과로 observable barrier를 검증한다. 내부 authority owner generation 숫자와
  aggregate 단일 CAS 호출 자체는 provider contract test가 검증한다. 이를 process
  E2E에서 읽기 위한 public API는 추가하지 않는다. Internal metadata와 E2E friend
  assembly를 사용하는 Actor delivery gate도 public-only 완료 증거가 아니다.
  추가 감사에서 E2E `RelocationUnitTerminalStore`가 실제 terminal이 아니라 제거
  대상 `OnActorRelocatedAsync`와 commit 전 `RestoreAsync`를 기록함을 확인했다.
  Host `RelocateAsync` terminal, final `Find`, terminal 뒤 handler·message-flow
  evidence로 교체한다.
  Spot flow correlation은 relocation traffic 직전 flow ID watermark 이후 event만
  사용한다. Moving target request·one-way가 0건이면 실패한다. Actor request는
  original operation과 connection-scoped correlation을 일대일로 대조하고 duplicate
  0건을 확인한다. Queue·journal evidence도 sequence 집합이 아니라 같은 sender
  connection의 handler 도착 순서로 검증한다.
  `.NET`·Java/Kotlin·Node의 public callback과 runtime invocation은 제거했고 C++에는
  해당 callback이 없었다. `.NET` Release build와 lifecycle focused 2/2,
  Java/Kotlin compile과 Java handler contract, Node build와 public-surface
  focused 1/1이 통과했다.
  Authority owner generation 비교의 exact `source + 1` 가정은 제거하고
  `1..long.MaxValue` 범위와 `target > source`만 검사하도록 바꿨다. Source는 commit 뒤
  Store에서 읽은 실제 published generation으로 Message Follow를 시작한다. Target
  object를 publication 전에 staging하는 여섯 경로도 capacity reservation 또는 aggregate
  prepare가 먼저 예약한 participant generation을 admission·factory·context와 commit까지
  전달한다. Abort된 예약값은 재사용하지 않으며 `source + 2` non-contiguous focused test가
  통과했다. Opaque provider adapter는 provider namespace 전체에서 공유하는 authority
  owner generation counter를 Store transaction에서 함께 올린다. Standalone 예약은 값
  하나를 발급하고 aggregate prepare는 participant 수만큼 연속된 범위를 한 번 예약한다.
  Abort 뒤에도 발급값을 재사용하지 않는다. Participant lock은 별도 key가 아니라 authority
  metadata에 기록하므로 1,024-participant prepare도 provider의 2,048-key 제한 안에서
  완료된다. Commit은 participant별 metadata·payload·generation을 같은 transaction에서
  모두 갱신하지 않는다. Aggregate 상태와 source·target capacity만 2,048-key 제한 안에서
  한 번에 commit하고, authority read는 committed aggregate에 든 target owner·generation과
  payload를 즉시 공식 상태로 사용한다. Framework는 그 뒤 participant별 물리 record를
  retry-safe하게 정리한다. 정리 도중 process가 종료되어도 aggregate record가 visibility
  기준이므로 일부 participant만 이전 owner로 보이지 않는다. Provider 응답 유실 뒤
  정리 write까지 실패하는 test와 1,024-participant 실제 commit test를 포함한 opaque
  authority focused test 7/7이 통과했다. Provider 제한을 높이거나 participant 계약을
  낮추지 않았다. Reserve와 aggregate
  prepare의 write가 적용된 뒤 응답만 유실되면 exact request record를 다시 읽어 같은
  generation으로 수렴한다. Published Actor recovery는 이미 commit된 authority와 relocation
  reference를 먼저 확인하므로 process-local capacity fence가 없어도 재commit하지 않는다.
  Admission decode 실패와 expiry·shutdown은 permit과 durable capacity reservation cleanup을
  완료한 뒤 registry entry를 제거한다. Store가 `Stale`을 반환하거나 실패하면 bounded retry
  뒤 registry가 recovery ownership을 유지한다. Callback이 deadline 뒤 끝난 경우와 permit
  부족·application 거부·callback exception도 capacity fence를 registry에 먼저 등록한 뒤
  정리한다. Shutdown cleanup은 모든 entry를 bounded하게 시도하며 하나라도 남으면
  drain-safe를 완료하지 않고 successor retry owner를 유지한다. Command 35 completion은
  target ACK를 받을 때까지 100ms 간격으로 같은 record를 재전송하고 ACK 뒤에만 source
  attempt를 제거한다. ACK는 exact pending attempt가 보관한 command 35 fingerprint와
  대조하므로 relocation ID, attempt generation, coordinator와 source owner fence가
  달라진 stale ACK는 다른 attempt를 끝내지 못한다. Pending attempt는 prepare의 object
  generation과 target candidate generation에 묶여 있다. Target은 terminal prepare,
  최초 command 35 fingerprint와 completion-applied marker를 deterministic Relocation
  Store reference에 24시간 보관한다. Receipt에는 terminal prepare 외에 target lifecycle
  generation과 commit된 authority owner generation을 보존한다. 같은 lifecycle에서
  5분·1,024개 in-memory terminal 제한으로 항목이 제거되어도 exact retry는 role 2 ACK로
  수렴한다. Process restart로 lifecycle generation이 바뀌거나 더 높은 generation에서
  이전 attempt가 도착하면 stale command 35로 거부한다. Fingerprint가 다른 retry는
  durable reference conflict로 거부한다. Receipt 복구 때 relocation root CRC32C와
  published authority의 ObjectGeneration, target owner lease, target descriptor lifecycle
  generation과 root reference를 다시 확인한다. Command 35 fingerprint는 side effect 전에
  durable intent로 저장한다. Side effect 뒤 applied marker 저장이 실패하면 retry는 authority의
  source cleanup state를 완료 증거로 사용하지 않는다. Exact publication이면 target completion을
  실행하고, 같은 target authority generation의 Actor·Spot steady payload가 확인된 경우에만
  이미 완료된 것으로 처리해 marker 저장만 수렴한다. Standalone Actor target은 session route
  commit ACK, remote unseal과 local route 완료를 steady normalization보다 먼저 수행한다.
  Unseal이 실패하면 publication을 유지해 command 35 retry가 같은 fence로 재실행하며, steady
  normalization 뒤에는 await 없이 handoff admission을 연다. Receipt와 root가 24시간 뒤 만료되면
  ACK를 추측하지 않고 stale retry를 거부한다. Process 종료로 source의 in-memory retry가
  중단되면 published authority와 Relocation Store root가 recovery owner가 된다.
  SpotWide target은 Spot generation을 member Actor에
  재사용하지 않고 prepare가 각 Actor에 발급한 값을 사용한다. In-memory와 opaque provider
  adapter를 함께 검증했고 `.NET` unit 1,128/1,128, contract 67/67, Redis 25/25가 통과했다.
  `ST-F4` process 재실행 `logs/20260728-033031-1716434`도 통과하여 이전
  `reject_reply`와 source handler 오처리가 재현되지 않았다. Async cleanup과 global
  generation counter 반영 뒤 재실행한
  `SpotActorTransfer/logs/20260728-041249-2584620`도 통과했다.
  `.NET` Message Follow request reply는 source reply queue admission 전에 pending entry를
  제거하지 않도록 수정했다. Local direct reply와 remote node relay는 private reply capability에
  보존한 original absolute deadline까지만 bounded retry한다. 동시·중복 reply는 terminal claim
  하나만 실행하고 cancellation은 claim을 반환해 deadline 안의 재전달을 허용한다. Focused
  Message Follow 26/26이 backpressure 뒤 성공, deadline·deadline 0 fallback, cancellation 뒤
  단일 재전달, local·remote admission 재시도, duplicate terminal과 deadline 뒤 remote 전송
  0회를 검증했다. 변경 뒤 전체 `.NET` Unit 1,097/1,097도 통과했다. 실제 process E2E의
  source reply queue backpressure fixture와 `ST-I4`·`ST-I5` assertion을 연결했다. Fixture는
  route·pending request·reply capability를 바꾸지 않고 reply submit 직전에
  `Backpressured`를 반환한다. Runtime retry loop가 이를 original deadline까지 재시도하도록
  internal hook을 연결했고, `20260728-035609-2262342`에서 ST-I4·I5 focused process가
  통과했다. 두 selector는 Actor commit 직후 Message Follow와 reply deadline만 검증하므로
  Spot 조합·queue 경계·duplicate·generation·loop·hop·bound가 남아 있어 diagnostic 상태를 유지한다.
  Spot public call은 terminal 실행 안에서 route를 찾고 pre-resolved opaque route를
  보존하지 않는다. 만료 뒤 global-ID call도 current owner를 다시 찾으므로 commit 뒤
  이전 physical route와 expiry를 public caller만으로 결정적으로 만들 수 없다.
  Process 밖 transport harness가 runtime frame을 resolve 뒤 지연·복제해야 한다.
  Scale 진단 terminal, 독립 open-loop 부하, 실제 relocation evidence와 외부 transport
  harness로 교체한 뒤 정본 gate를 실행한다.
  I2의 혼합 실행은 `ST-I2-RECREATE`와 `ST-I2-SNAPSHOT`으로 분리했다. 두 selector는
  각각 fresh host process에서 10,000/180초와 1,000/90초 목표를 독립 계산한다.
  아직 정본 process 실행 증거는 없으며 creation-reservation blocker를 우회하지 않는다.
  1초 interruption·encoded byte 처리량·payload latency·CPU/RSS·Store byte 누락과
  I3의 pre/post aggregate visibility 누락은 diagnostic blocker다. SpotWide final
  owner equality는 atomic publication 완료 증거로 사용하지 않는다. 이 후속 항목은
  `30-implementation-gap.ko.md` §12.52가 소유한다. I4~I6의 internal Information log marker 판정도
  application evidence와 public message-flow observer로 교체한다.
  최신 회귀는 `.NET` Unit 1,090/1,090, public Contract 67/67, Redis provider
  25/25가 통과했다. SpotActorTransfer ActorNode·Client·SessionGateway build도
  warning·error 0이며 `verify-framework-doc-contracts.sh`와 `git diff --check`가
  통과했다.
- E2E source의 Spot identity도 다시 조사했다. Production framework는 `SpotId`로 바뀌었지만
  E2E source·fixture·DTO에는 이전 `SpotRid` 계열 이름이 C++ 107개 파일, .NET 84개,
  Java/Kotlin 57개, Node.js 164개 파일에 남아 있다. 일부 Java fixture는 Spot ID를
  `RoutingId`로 변환한다. 이 상태의 기존 E2E build·pass는 현행 contract 증거가 아니며,
  `30-implementation-gap.ko.md` §12.53 기준으로 네 언어 fixture를 `SpotId` 문자열 표면에
  맞춘 뒤 다시 실행한다.
- `.NET` AutomaticTurnDispatch의 제거된 handler context·Spot handle resolver·이전 builder를
  current MessageContext, global Spot ID와 direct Spot messaging으로 전환했다. Delay·ExternalApi·
  Play·Session·Client 다섯 project가 warning·error 0으로 build된다. 이전 runner는 알 수 없는
  selector도 scenario 0개로 통과시키므로 개별 실행 증거를 무효화했다. Canonical selector
  fail-fast와 실제 실행 수 gate를 추가한 뒤 `20260728-035646-2274534`에서 `TD-A2` 1개가
  실제 실행되어 통과했다.
  `SpotActorTransfer.Client`도 warning 0, error 0으로 build된다.
  그러나 Linux에서 전체 `Zlink.Framework.sln`을 다시 build하면 sample과 다른 E2E의
  이전 public surface 때문에 260 errors로 실패한다. 대표 원인은 제거된
  `ActorRefSnapshot`, `IZLinkDrainControl`, `ZLinkRequestContext`·`ZLinkSendContext`,
  이전 Spot manager terminal과 provider abstraction project reference 누락이다.
  Framework·Contract·Unit focused PASS를 전체 solution 완료 증거로 확대하지 않는다.
- Java SpotActorTransfer는 제거된 `ActorRef` 생성자·`generation()`과 route-bearing Actor request,
  이전 Message Follow 설정 위치 때문에 ActorNode compile error 10개가 있었다. Global Actor ID,
  `ObjectGeneration`과 `configureLocations()`로 전환한 뒤 Client·Shared·ActorNode compile이
  통과했다. 다만 `ST-F4/F5`의 old route 직접 주입은 public contract에 맞는 검증이 아니므로
  PASS 경로로 바꾸지 않았다. Resolver가 선택한 transport delivery를 지연하는 fixture가 생길
  때까지 Java·Kotlin·Node.js feature map도 `전환 대상`으로 유지한다.
- Kotlin SpotActorTransfer ActorNode는 같은 전환 외에도 이전 Spot factory·Entry Spot builder,
  handler context·Actor Join과 `SpotRid`가 남아 56 compile errors였다. 이를 global
  `SpotId`·Actor ID, `ObjectGeneration`, `MessageContext`, deferred Actor Join과 byte-array
  relocation adapter로 전환했다. Client·Shared·JavaClient·ActorNode compile은 통과한다.
  Focused `ST-A1`은 Actor가 다른 owner에 배치된 뒤 public request가 `actor is not local`로
  실패한다. 특정 Node RID 배치나 raw route로 우회하지 않고 Java runtime의 cross-node Actor
  messaging gap으로 유지한다. Kotlin feature map에도 이 runtime blocker와
  `ST-F3A`·G·H·I track 미구현 상태를 추가했다.
- Node.js E2E 47개 TypeScript project를 현재 framework source와 직접 build했다. 42개가
  compile에 실패했다. `SpotRid` 계열 이름은 `SpotId`로 전환했지만 제거된 `SpotHandle`·
  `ZLinkHandlerContext`·Actor join request/context, 이전 builder와 rich Redis Store 표면,
  E2E 공통 `rootDir` 설정이 함께 남아 있다. Root package `typecheck`는 통과했으나 E2E를
  포함하지 않으므로 process scenario 완료 증거가 아니다.
- Node.js Redis provider는 exact opaque Store SPI로 전환했다.
  `ZLinkRedisLocationStore`는 `read/write/scan`, `ZLinkRedisRelocationStore`는 Framework가
  발급한 reference의 `put/read/renew/delete`를 구현한다. 이전 provider-generated
  reference 표면은 public declaration과 runtime caller에서 제거했다. Package build·typecheck,
  Redis focused 5/5, M6B 39/39와 M6C 64/64가 통과했으므로 이전 implementation gap
  §12.55는 제거했다.
- Node.js public `ActorRef`도 exact interface와 다르다. 현재 source는 `actorId`, `nodeRid`,
  `generation` 세 필드와 별도 snapshot 변환 함수를 공개하지만 target은 `actorId`,
  `objectGeneration`, `meshName`, `nodeRid` 네 필드이며 snapshot helper를 제공하지 않는다.
  E2E만 이름을 바꾸지 않고 runtime·Session binding·manager·declaration을 함께 전환해야 한다.
- `.NET SpotActorTransfer`의 cleanup fault fixture를 rich Location Store operation에서 opaque
  conditional delete batch decorator로 바꿨다. ActorNode·SessionGateway·Client는 warning·error
  0으로 build된다. ActorNode와 SessionGateway도 fixed RID·manual peer를 제거하고 Framework가
  full RID를 발급하는 automatic topology로 전환했다. Readiness는 실제 peer RID를 관측하고,
  scenario의 owner assertion은 진단 prefix와 Framework 발급 suffix를 함께 확인한다. 다섯 process의
  automatic peer 연결 뒤 User Spot·Actor 생성과 join까지 진행된다. User Spot의 actor handler는
  sample과 같은 `Configure()` public registration으로 고쳤다. Reservation 기반 Actor
  creation commit의 authority snapshot을 local ownership coordinator에 연결하고 Actor
  publish를 그 뒤로 옮겨 첫 deferred Join의 `ActorRouteNotFound`를 수정했다.
  Fixture는 세 node의 동일 stable type, caller 비지정 create와 public node-wide
  placement weight로 전환하고 deferred completion callback을 기다리도록 고쳤다.
  `ST-A1`은 `logs/20260727-230711-505861`에서 통과했다. Remote `ST-B1`은
  actor-a와 actor-b에 Actor·Spot을 나눠 배치했지만 completion이 `RequestFailed`로
  끝났다(`logs/20260727-230926-521014`). 배치 차이는 닫혔고 remote Join runtime gap이 남았다.

이 checkpoint는 현재 E2E spec의 완료 승인이 아니다. 실제 process scenario와 언어별 feature map이
일치하고 Codex `gpt-5.6-sol high` 독립 review를 통과한 뒤 `V11-E2E-SPEC-FINAL`을 완료로 바꾼다.

### 0.0.2 2026-07-27 전체 진행 상태 checkpoint

현재 상태 기준 revision은 `f26ff5945b1dde46cacfd632c588cfcdfbdcccd5`다. 아래 수치는 이 revision을
직접 검증한 결과이며, §0.1~§0.1.2의 이전 handoff 수치보다 우선한다. 실행 행 198개의 현재 집계는 완료
132개, 진행 19개, 수정 진행 13개, 대기 34개다.

공통 계약 검증은 통과한다. `scripts/verify-framework-doc-contracts.sh` 결과는 service wire 40 commands,
167 types, 4 flags, 37 bounds, exact document 52개, declaration owner 1,385개, member 4,965개다.
Public contract trace의 unclassified·ambiguous·unknown owner는 모두 0이고 formal document는 135개다.
Core service migration inventory도 total 1,860, file 1,233, Core public symbol 523, export 98 기준으로
`--check`를 통과했다. Contract amendment impact manifest generator도 5,957 entries 기준으로 `--check`를
통과한다.

다만 impact manifest 자체의 quarantine 검증은 현재 통과하지 않는다. 변경된 E2E·sample hash 28건과
기존 경로가 없어 확인할 수 없는 항목 13건, 합계 41건을 검출했다. 이 결과는 검증을 약화하거나 현재
source hash를 자동 승인할 근거가 아니다. `V11-CA-IMPACT`의 승인 revision 완료 이력은 유지하되, 현재
drift는 `V11-M6-SCAFFOLD-ZERO`, `V11-E2E-SPEC-FINAL`과 `V11-SAMPLE-SPEC-FINAL`의 미완료 증거로
관리한다. 각 변경이 승인한 contract amendment인지 확인한 뒤 baseline·approved hash와 disposition을
다시 고정한다. 이전 `SPEC-01~06` JSON과 `.NET` reference artifact는 이전 revision의 유효한 이력이지만
현재 HEAD 완료 증거로 재사용하지 않는다.

언어별 실측 상태는 다음과 같다.

- `.NET`은 Unit project build warning·error 0, Unit 1,073/1,073, Contract 65/65가 통과했다. Redis
  전체 실행은 첫 시도에서 lease expiry timing 1건이 실패했지만 같은 suite 즉시 재실행은
  69 pass, 0 fail, environment skip 2다. `BLK-003`·`BLK-004`·`BLK-006`은 이미 해결됐고 private
  `.stage.v1`·`.publish.v1`·`.abort.v1`·`.held-relay.v1`·`.reply.v1` symbol도 source·test에서 0이다.
  현재 HEAD의 immutable candidate, `M6-RUNTIME`, `REMOVE`, `ROW-GATE`와 Codex high finding 0 review는
  아직 새로 만들지 않았다. Hosted crash/restart와 모든 방향 mixed-language relocation E2E가 없는
  `BLK-044`도 계속 조치 중이다. 따라서 M6A·M6B·M6C, deferred Join, session binding, weight, design
  review와 reference 행은 수정 진행으로 관리한다.
- JVM은 `:zlink-framework-core:test` 576/576, `JavaTargetContractGapTest` 16/16, Kotlin test
  46/46가 통과했고 Spring·testkit·Kotlin compile도 성공했다. Runtime service 구현은
  `runtime.internal.service` 아래 main 17개·test 15개로 이동했으며 이전 public package와
  다른 module의 internal 직접 참조는 0이다. 이 결과는 public boundary를 닫지만 active workload
  maintenance, production aggregate coordinator, process recovery와 네 언어 동형 수렴을 완료하지는 않는다.
  `V11-M6C-JVM`의 현재 blocker는 이전 `BLK-036`이 아니라 `BLK-043`의 standalone Actor source
  builder·target endpoint와 실제 two-owner recovery다. `V11-M6A-JVM`의 `BLK-006`은 해결됐고
  현재 남은 항목은 `BLK-016`·`BLK-017`·`BLK-018`이다.
- Node.js는 production·browser build와 typecheck가 통과한다. Public boundary를 포함한 broad contract
  117건을 다시 실행한 결과 104 pass, 13 fail이다. 실패는 `location-runtime`의 제거된 split
  owner/authority seam과 RoutingId Spot fixture 12건, NestJS DI fixture 1건이다. 이전 focused
  73/73·56/56과 M6-RUNTIME artifact는 해당 revision의 좁은 증거로 유지하지만 현재 HEAD 전체 완료
  증거로 확대하지 않는다. Public boundary focused subset 자체는 94/94다. M6A의 `BLK-006`·`BLK-009`와
  M6B의 `BLK-005`·`BLK-008`·`BLK-009`·`BLK-014`는 이미 해결됐으며, 현재 gap은 이 13건과
  listener identity·command 48 terminal replay·membership seal, production host 연결이다.
- C++은 contract header, target contract, sample parity와 Store·Location resolver target이 모두
  compile된다. Exact factory Context와 teardown focused test 2/2는 통과하고 host destructor count도
  1이다. Target contract는 descriptor discovery, typed draining, heartbeat와 live-row fence,
  common Mesh hash 등 9건이 실패하며 sample parity는 SupportChat의 이전 ActorRef 변환 1건 때문에
  53/54다. 제거된 no-context factory를 사용하는 E2E·sample·stale test caller도 M7 migration 범위에
  남아 있다.

현재 critical path는 impact manifest 재승인, `.NET` HEAD reference evidence와 Codex high review,
C++ target contract·caller migration, JVM·Node production host 연결 순서다. 그 뒤 네 M6C candidate를
합쳐 `V11-R5C`, scaffold zero, 최종 E2E·sample spec, 활성화와 M7 correctness gate로 진행한다.
M8 cleanup과 M9 package·smoke는 해당 선행 조건을 건너뛰지 않는다.

`.NET` public boundary의 후속 POSD 설계는 domain-specific Store operation을 외부 provider에 노출하지
않는 두 번째 대안으로 승인했다. Location Store는 opaque key·value와 version을 사용하는 atomic batch,
TTL과 bounded scan만 제공하고, Relocation Store는 Framework가 먼저 발급한 reference에 immutable blob을
저장한다. Host lifecycle은 relocation과 종료를 별도 command로 제공한다. `RelocateAsync`는 신규
application admission과 placement를 닫고 현재 workload를 이전한 뒤 `Relocated`에서 완료하며,
`ShutdownAsync`는 relocation을 시작하지 않고 `Serving` 또는 `Relocated` host를 종료한다.
`PlannedMaintenance`는 source와 같은 application version만 target으로 사용한다. `RollingUpdate`는
호출자가 지정한 더 높은 application version과 정확히 일치하는 target만 사용한다. 두 mode 모두
source와 같은 maintenance wave를 제외하며 mode와 target version이 같은 concurrent waiter만 합류한다.
`AddLocationStore`·`AddRelocationStore`는 유지하고 `Use*Store` 또는 Redis 전용 등록 helper는 추가하지
않는다. 공식 Redis extension은 두 Store 구현 class와 필요한 최소 constructor·options만 공개한다.

승인 입력의 Store 경계는 공통 정식 spec과 다섯 언어 exact interface에 흡수했다. 임시 설계 문서 세 개는
`V11-CA-STORE-POSD-DRAFT-RETIRE`에서 정식 owner, public trace와 repository link 누락이 없음을 확인한 뒤
제거했다. 이후 구현과 검증은 정식 spec과 exact interface만 입력으로 사용한다.

### 0.1 2026-07-25 handoff snapshot

`framework/doc/framework/common/spec/`을 이후 구현의 정식 contract baseline으로 사용한다.
`scripts/verify-framework-doc-contracts.sh`는 service wire schema, public contract trace와 formal document
검증을 모두 통과했다. 결과는 formal document 137개, exact member 1,397개, canonical member 176개,
unclassified·ambiguous·unknown owner 0개다.

실행 상태 행 200개의 2026-07-26 현재 집계는 완료 132개, 진행 22개, 수정 진행 12개,
검토 준비 완료 0개, 대기 34개다. 다음 작업자는
완료 행을 다시 실행하지 않고 아래 진행 행의 남은 조건부터 이어서 처리한다.

- .NET MessageContext는 production build까지 통과했지만 이전 handler signature를 사용하는 UnitTests
  compile 오류 59건과 exact contract evidence가 남아 있다. ObjectContext와 Entry Spot identity는 시작하지 않았다.
- C++ deferred Join은 Entry Spot Actor handler에 open serial turn이 없어 result-free `defer()`가
  `FrameworkError:16`으로 거부된다. 이 gate를 복구한 뒤 remote reply `errno=113`을 다시 검증한다.
- JVM stored session route는 relocation과 post-relocation request/reply까지 통과했다. Target에서 source의
  bound session으로 보내는 `BoundPushNotify`가 8초 안에 도착하지 않는 relay gap이 남아 있다.
- Node ObjectContext와 MessageContext는 완료했다. 전체 root typecheck의 남은 2건은 별도 M6C
  `ZLinkAuthoritySnapshot` fixture 오류다.
- Node contract test 정리는 인수인계 상태다(2026-07-25, 커밋 `73fd7bf041`..`2a362e0b56`). 다음 담당자는
  §9.1의 "Node contract test suite 정리 checkpoint" 문단과 아래 lane 표의 Node.js 행에서 이어서 진행한다.
- `V11-M6A-WEIGHT-DN` focused test 7/7은 통과했지만 candidate artifact와 `ROW-GATE` 증거가 없어 상태를
  올리지 않는다. M6C의 exact owner lease·capacity fence와 publish monitoring 제거 뒤 evidence 재생성도 남아 있다.

2026-07-25 자율 실행에서 네 lane의 상태를 같은 시점에 실측했다. 위 목록 중 .NET UnitTests compile 오류와
C++ `FrameworkError:16` 항목은 이미 해소되어 더 이상 유효하지 않다.

| lane | 명령 | 실측 결과 |
|---|---|---|
| .NET | `dotnet test .../Zlink.Framework.UnitTests.csproj` | 789 pass / 2 fail. 남은 2건은 `BLK-004`의 unsolicited ROUTER→DEALER 전달이다 |
| JVM | `./gradlew :zlink-framework-core:test` | 526 pass / 0 fail |
| Node.js | `npm run build`뒤 `node --test test/contract/<file>`을 파일 단위로 실행(전체 `npm test`는 10분 초과) | 2026-07-25 파일별 정리로 갱신됨: `actor-manager`(69/69)·`actor-transfer-authority`(3/3)·`spot-manager`(58/58)·`spot-activation-state`(4/4)·`entry-spot-serial-dispatch`(22/22)·`location-key-codec`(2/2) 완료. `stream-runtime`은 71/84로 미완료(중간 커밋 메시지가 84/84로 잘못 적은 적 있음, 무시할 것). `backend-contract`는 30/34이며 남은 4건은 `BLK-006`(자기 ChannelName 전송, 계약 판정 대기)로 의도적으로 미해결. `location-{store,host,runtime,autoconnect}`·`nestjs-module`·`message-flow`·`contract-surface`·`deferred-actor-join`·`object-message-context`·`routing-id-allocation`·`runtime-execution`·`tictactoe-session-dispatch`와 `sample-*`·`e2e-*` gate 다수는 아직 미착수. 5개 파일(`channel-client`·`client-server-location-runtime`·`stream-connector`·`stream-connector-codecs`·`stream-session-runtime`)은 hang한다(`BLK-009`). 세부는 §9.1 뒤 checkpoint(커밋 `73fd7bf041`..`2a362e0b56`)와 `blocked-issue-log.md`의 `BLK-008`·`BLK-009` 참고 |
| C++ | `.artifacts/v11/build/framework-runtime-regression/cpp`에서 `ctest -R "^test_cpp_framework_m6[abc]_runtime$"` | m6a·m6b·m6c 모두 통과. `zlink_cpp_framework_mesh_node_vertical_test`는 compile 실패로 `Not Run`이다 |

C++ vertical test는 generated protocol include 누락(수정함)과 제거된 `zlink::service` type 참조 22곳이
겹쳐 있었다. Machine inventory에서 이 파일의 disposition은 `retain-framework-owned-service-runtime`이고
removal gate는 `V11-M8-CLEAN-CPP`이므로 삭제하지 않고 Framework 소유 service runtime API로 이관한다.

#### 0.1.1 검증 층 정리 (같은 날 후속 실행)

위 표의 수치 일부는 그 시점의 측정이며 이후 갱신됐다. 현재 상태는 §0.0.2가 소유한다.

가장 중요한 발견은 개별 결함이 아니라 **측정 자체가 신뢰할 수 없었다**는 점이다. 세 형태가 반복됐다.

1. **검증이 실행되지 않음.** `Zlink.Framework.ContractTests`가 컴파일되지 않아 `M6-RUNTIME` 게이트가
   `dotnet-public-declarations` 단계에서 중단됐고 test를 0개 실행했다. JVM `--tests` glob이 98개 test
   클래스 중 35개만 선택했고, 누락된 63개에 오류 kind 표를 고정하는 유일한 test가 있었다. C++
   `test_cpp_framework_contract_headers`와 3363줄짜리 `test_cpp_framework_channel_messaging.cpp`가
   어떤 게이트에도 없었다. Node CI 게이트의 skip 목록에 hang하는 파일 두 개가 들어 있었다.
2. **검증이 자기 자신과 비교.** Node 오류 kind 검사가 낡은 기대값 37과 실제 37을 비교해 초록이었다.
   `pickEnumValues`가 실제 enum을 기대 key set으로 투영해 추가된 멤버가 비교에 참여하지 못했다.
3. **검증이 권위의 부정을 단언.** 커밋 `859fcf07fe`가 completion union을 C++ exact interface에
   추가하면서 같은 커밋에서 게이트 검사를 "존재 금지"로 뒤집었다.

JVM spring-boot starter는 SIGABRT로 31개 중 9개에서 잘린 상태를 "14 tests"로 보고하고 있었다. 원인은
admission monitor를 자기 DEALER보다 나중에 닫은 실제 런타임 결함이었다(`29-transport-liveness.ko.md` §6).

게이트에 편입한 것은 C++ public header contract, JVM `core:test` 전체(35/98 → 98/98 클래스), JVM codec
모듈 2개, JVM http-client 2개다. 편입 원칙은 `BLK-030`에 있다 — **넣기 전에 먼저 실측한다.** 통과하지
않는 스위트를 넣으면 row를 보호하는 게 아니라 막는다. 그래서 JVM testkit(4 실패·컴파일 오류 100),
spring-boot starter(4 실패), C++ channel messaging(게이트 83 발화)은 편입하지 않았다.

편입 직후 실제 결함이 드러났다. C++ header contract에서 3건, spring-boot에서 3건, 그리고 한 번도
실행된 적 없던 C++ channel messaging의 첫 실행이 `BLK-033`을 잡았다.

해소된 blocker: `BLK-004`(낡은 vendored 네이티브 아티팩트, .NET 791/791), `BLK-006`(RouteMesh 자기 후보
제외 판정·spec 명문화), `BLK-008`(join 시그니처 5개 언어 정렬), `BLK-013`(C++ multicast slot 회계,
800회 스트레스 0 실패), `BLK-031`(런타임 옳음·테스트 낡음). `BLK-009`는 5개 중 4개 해소되고
`channel-client.test.js`만 남았다(네이티브 `zlink_ctx_term` 블록, Node lane 소유 경로 밖).

새로 연 것: `BLK-023`·`BLK-030`·`BLK-032`~`BLK-035`. 이 중 `BLK-033`(C++)과 `BLK-034`(JVM)은 같은
모양이다 — 같은 프로세스 ClientServer 서버가 admission을 완료하지 못한다. spec이 세 문단을 할애한
경로이므로 공통 계약 갭 여부를 조사 중이다.

**다음 작업자가 먼저 알아야 할 것.** 이 저장소에 동시 작업 중인 Codex 세션이 있다. `.NET` 트리를
계속 재작성하고 있어 빌드 오류 수가 시간대에 따라 0에서 1000까지 요동한다. `V11-M6A-DN` 게이트,
candidate manifest, contract snapshot은 그 트리가 안정된 뒤에 생성해야 한다. 반쯤 마이그레이션된
표면을 snapshot으로 고정하면 그 자체가 잘못된 기준이 된다. 동시 편집으로 `BLK-009` ID가 한 번
중복 발급됐고(C++ 쪽을 `BLK-020`으로 이동), 여러 lane의 미완성 작업이 남의 커밋에 함께 쓸려
들어간 사례가 있다.

#### 0.1.2 2026-07-26 .NET 기준 구현 수렴 checkpoint

[Spot·Actor routing 통합 spec](../../framework/common/spec/18-object-routing.ko.md)을 정식 구현 입력에
추가했다. 이 문서는 direct global ID의 positive Ready cache, Session에 저장한 Actor route, request의
reply route, 같은 operation의 자동 재제출 금지, relocation 뒤 bounded Message Follow와 command 44·45 순서를
한곳에서 연결한다. 분산된 기존 spec과 exact interface는 세부 계약을 확인하는 입력으로 함께 사용한다.

현재 `.NET`은 기준 구현을 고정하기 전의 병렬 수렴 단계다.

- Canonical relocation production 경로에서 private `.stage.v1`·`.publish.v1`·`.abort.v1`·
  `.held-relay.v1` DTO와 handler·dispatcher를 제거했다. Retire source와 target은 service wire command
  `31`·`32`·`34`·`35`를 raw infrastructure dispatch로 처리하며, target reservation owner가 exact peer,
  lifecycle, coordinator, operation, participant high-water와 immutable root를 소유한다. User Spot aggregate와
  standalone Instance Spot의 capacity fence handoff·commit·abort·slot cleanup을 같은 owner 경계에 연결했다.
  Framework와 UnitTests build는 warning·error 0, focused canonical owner·codec·relocation runtime은
  103/103, 전체 Unit은 987/987, Contract는 65/65가 통과했다. Private wire symbol scan은 0건이고 관련
  source·test·문서의 `git diff --check`도 통과했다. Standalone Actor canonical maintenance owner와
  mixed-language process E2E가 남아 있으므로 `V11-M6C-DN`과 `BLK-044`는 진행 상태를 유지한다.
- Standalone Actor canonical maintenance 경로를 `ZLinkStandaloneActorRelocationRuntime`이 소유한다.
  Retire scheduler는 application `JoinActorAsync`를 재사용하지 않고 command `40→30→41`, immutable
  state·accepted queue root, 동일 capacity fence의 `NewOwner` CAS, command `35` finalization을 사용한다.
  Target은 application join admission 없이 factory·Restore를 수행하며 ObjectGeneration을 유지하고
  authority owner generation만 증가시킨다. Command `34` 응답 유실 뒤에는 durable authority를 다시 읽어
  source authority가 그대로라는 exact proof가 있는 경우에만 source queue를 복원한다. Startup recovery도
  canonical Actor root를 이전 JSON·application join 경로보다 먼저 분리하여 같은 target restore owner에서
  phase·session route·FIFO replay를 재개한다. Framework와 UnitTests build는 warning·error 0, focused
  standalone Actor·canonical reservation·Actor handoff는 53/53, 전체 Unit은 994/994, Contract는 65/65가
  통과했다. Hosted process crash/restart integration, mixed-language process 검증과 독립 post-review가
  남아 있으므로 `V11-M6C-DN`과 `BLK-044`는 계속 진행 상태다.
- Standalone Actor canonical maintenance 후속 교정에서 command 40의 Actor ID와 Location authority key를
  같은 값으로 비교하던 오류를 제거하고, Actor ID를 authority key codec으로 변환해 검증한다. Published
  startup recovery는 `Activated`·`Cleaning`을 임의로 `Completed`로 올리지 않고 immutable root에 보존한
  source owner·lease·node fence가 실제로 만료되거나 교체됐는지 먼저 확인한다. Command 34 ACK 유실 뒤에는
  relocation root reference·checksum·target owner fence가 모두 같은 durable publication일 때만 같은 attempt의
  source cleanup을 계속한다. Command 35의 pending cleanup state `0`은 거부하고, published target stage는
  command 34 duplicate에서 durable `Completed`를 확인해 finalization을 재개한다. Pre-publication stage는
  1,024개와 5분 TTL로 제한한다. Accepted Actor journal은 private JSON을 제거하고 공통 canonical frozen
  record kind 9·10으로 operation ID high·low, reply route와 source·target fence를 보존한다. 이전 994/994
  결과는 이 교정 전 중간 checkpoint다. 교정 뒤 focused 결과는 standalone 6/6, canonical Actor journal
  3/3, canonical reservation owner 13/13이며 전체 Unit 999/999, Contract 65/65, Framework build warning·error
  0, private JSON journal symbol 0과 scoped `git diff --check`가 통과했다. Hosted process crash/restart,
  mixed-language process 검증과 final post-review가 남아 있으므로 `V11-M6C-DN`과 `BLK-044`는 진행 상태를
  유지한다.
- Clean HEAD `efcba6f0a1e9dbded360893ff163e6158a4a24f4`에서 `.NET` reference runtime regression을
  다시 실행했다. 기존 reference와 같은 base `01b13d41cb54f367ba8f46e42b5946f037c6ee47`, owned path
  6개와 direct input 5개를 사용한 candidate는 90 files이며 aggregate SHA-256은
  `d8f354ae4811d41c2c5d4b86bf0560f4cc34535459e54bc47fe1b151737c441a`다. Candidate manifest는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/candidate-efcba6f0a1.json`이고 manifest SHA-256은
  `ac2b518955dc995b583cffa00baf36f16d89b5a39d4eb38082c798ef8e98ff04`다. `M6-RUNTIME`은 compile
  warning·error 0, public declaration 65/65, internal runtime 984/984, resource 85/85, protocol
  86/86으로 5/5를 통과했다. Sample·E2E project와 task 실행은 모두 0이다. 결과는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-efcba6f0a1.json`이며 SHA-256은
  `0cd15a2a87edc750467c92292ea1e1e7d3262986aa1b802d9ec4fbbe0ac63106`이다. Contract snapshot
  `Zlink.Framework.api.txt`의 SHA-256은
  `30b3c471a1e1f3566c518fc2502dc428a66645a2a3e918c5ea852c34f22a51b9`로 고정 snapshot과
  일치한다. 같은 candidate에서 `REMOVE --scope framework:dotnet`을 다시 실행해 inventory record 1개,
  제거 대상 symbol·file·artifact 0으로 통과했다. 결과는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/removal-efcba6f0a1.json`이고 SHA-256은
  `d00dc090769551115db00535200d0c1e96fed6ca50215cc7993233e1353e2a60`이다. Candidate와 owned-path
  manifest, 위 `M6-RUNTIME` evidence를 입력한 `ROW-GATE`도 files 90·commands 5로 통과했다. Owned-path
  manifest SHA-256은 `43b3907a023c3bd5212ac0347eb9f4558df39489e76c425a0b0df083d1e189ae`다.
  이 결과는 sample·E2E 제외 runtime regression과 candidate provenance gate 통과 증거다. Hosted
  crash/restart, mixed-language process 검증과 final post-review가 별도 완료 조건으로 남아 있으므로 이
  checkpoint만으로 `V11-M6-DN-REFERENCE`를 완료로 전환하지 않는다.
- `.NET` final post-review 수정 checkpoint(2026-07-26)는 세 경계를 함께 닫았다. Location Store
  lane은 `Preparing`부터 `NewOwner`까지 phase별 허용 field를 검증하고, 이미 예약한 relocation capacity
  fence를 `Preserve` CAS의 새 `StoreVersion`에 같은 transaction으로 다시 연결한다. InMemory·Redis provider와
  precommit focused test는 현재 19/19가 통과했다. Startup recovery는 remote target lease와 descriptor가
  현재 generation인지 확인한 뒤에만 takeover하며, CAS 응답을 잃은 경우 durable authority를 다시 읽어
  성공 여부를 판정한다. Final handoff lane은 initial root를 `Captured`로 공개한 뒤
  target capacity·Restore를 준비하고, commit 직전 high-water까지 포함한 final immutable root를 다시
  `Captured`로 publish한 다음 delta stage·`Prepared`·`NewOwner` 순서로 진행하도록 수정 중이다. Abort는
  captured queue와 hold queue를 arrival 순서로 source에 복원하며, target payload permit은 성공·실패·crash
  모든 terminal에서 한 번만 반환한다. Reply lane은 receiver가 새 ID를 만들던 User·Instance Spot
  request 경로를 제거하고, caller의 `OperationId`와 source fence를 Actor request와 같은 bounded
  terminal-once registry에서 보존한다. 공유 UnitTests project는 warning·error 0으로 compile됐고 조기 전체 Unit은 1,068개 중
  1,065개가 통과했다. 단독 재실행에서 Spot terminal replay와 precommit phase test는 통과했다. 남은
  source-owned `Captured` startup recovery 실패는 immutable root의 `AggregateGeneration`에
  `TargetAttemptGeneration`을 잘못 기록한 production projection 오류로 확인했으며, root 계약의
  aggregate generation을 기록하도록 수정했다. 수정 뒤 startup recovery 10/10과 precommit 2/2가
  통과했다. Spot reply lane은 caller·old owner·new target을 분리한 User·Instance Spot request에서 original
  correlation, source capacity 선거부, terminal ACK 유실·duplicate를 검증했고 focused 6/6,
  Stateful 26/26, Relocation 116/116과 당시 전체 Unit 1,069/1,069가 통과했다. 최종 합류 뒤 Unit은
  1,074/1,074, Contract는 65/65가 통과했다. Redis 최초 전체 실행은 C# `null` fence가 Lua의 truthy
  `cjson.null`로 decode되어 일반 `Preserve`·`Delete`까지 relocation reservation 검증에 들어가는 7개
  회귀를 찾았다. `nil`·`cjson.null`·빈 문자열을 명시적으로 제외한 뒤 Redis는 68/68이 통과했고
  cross-language fixture 2개만 환경 조건으로 skip됐다. `git diff --check`가 통과했고
  `framework/doc/plan/log/` 변경은 0이다. 독립 Codex `gpt-5.6-sol high` review와 새
  candidate·removal·row gate를 다시 실행하기 전에는
  `V11-M6-DN-REFERENCE`를 완료로 전환하지 않는다.
- 위 candidate에 대한 독립 Codex `gpt-5.6-sol high` 3-lane review는 clean 판정을 거부했다. Recovery
  review는 expired-source `Preparing` abort가 live lease 검증에 막히는 문제, post-commit target crash를
  live source coordinator가 계속 defer하는 문제, phase 3 takeover의 이전 capacity fence 누수,
  materialize된 target rollback 누락, target permit 조기 반환과 abort queue partial restore를 찾았다. Spec
  review는 durable `Aborted`·session ACK보다 source admission을 먼저 여는 순서, canonical phase 4~8을
  앞당겨 해석하는 문제, same-node direct Actor request의 reply route 누락과 Spot accepted journal의
  ingress-time source owner fence 누락을 찾았다. POSD·test-gap review는 24시간 reply tombstone이 live
  request capacity를 모두 차지하는 문제, no-RID STREAM monitor metadata의 FIFO 오연결·무제한 보존과
  captured framework terminal reply의 backpressure 유실을 찾았다. 따라서 137-file
  `candidate-final-review.json`과 이에 연결된 5/5 runtime·removal·row-gate 결과는 pre-fix 진단 증거일
  뿐이며 완료 증거로 사용하지 않는다. 세 수정 lane과 post-fix review가 clean으로 끝난 뒤 새 candidate를
  생성한다.
- `.NET` public contract surface audit는 Location 관련 exact interface를 application·operations API와 외부
  provider SPI로 구분한다. `ZLinkLocationOptions`, runtime query·readiness·status·topology·summary와 공식 Redis
  options·store는 application·operations surface다. Provider SPI는 앞서 확정한 두 Store 책임에 맞춰
  `IZLinkLocationStore`와 `IZLinkRelocationStore`를 기준으로 통합한다. MeshNode·ClientServer·fanout descriptor,
  owner lease와 authority operation은 `IZLinkLocationStore`가 직접 소유하며, public
  `IZLinkAuthorityStore`·`IZLinkMeshNodeLocationStore`·`IZLinkOwnerLeaseStore`·
  `IZLinkClientServerLocationStore`·`IZLinkFanoutLocationStore` 분할은 제거한다. Change stamp는 correctness와
  무관한 optional provider optimization이므로 별도 public interface를 두지 않고 `IZLinkLocationStore`의
  optional/default operation으로 흡수한다. Provider가 지원하지 않으면 Framework가 polling한다. 사용되지 않는
  `ZLinkRouteKind`와 Framework 내부 watch용 `ZLinkLocationKind`·`ZLinkLocationKey`는 public contract에서
  제거한다. Framework 내부 recovery를 위해 추가한 optional provider capability도 public exact interface에
  올리지 않는다. Spec·source·contract test·public declaration snapshot을 함께 정렬하고 불필요한 노출 0을
  확인하기 전에는 `.NET` post-review를 clean으로 판정하지 않는다.
- `.NET` public boundary 정리를 구현과 exact interface에 반영했다. POSD 대안으로 capability별 Store를
  유지하는 방식과 하나의 typed Store SPI로 통합하는 방식을 비교했고, 전자는 provider 조합·transaction
  domain·phase 지식을 호출자에게 노출하므로 제외했다. 범용 command envelope 하나로 축소하는 방식도
  provider가 command union과 phase를 해석해야 하는 얕은 interface가 되므로 제외했다. 따라서
  `IZLinkLocationStore`와 `IZLinkRelocationStore`만 provider SPI로 유지하고, phase 전용 recovery와 범용
  change-scope, row-level event, routing-slot, legacy message-flow observer를 제거했다. Application이 사용하는
  `IZLinkLocationReadiness`와 `IZLinkLocationRuntimeQuery`는 유지했다. Runtime에서만 사용하는 Spot·Actor
  location projection과 key, auto-connect type은 `internal`로 전환하고 `Contracts/` 밖으로 이동했다.
  고유 public declaration이 없는 `09-routing-id-allocation.ko.md`와 `12-dispatch-ownership.ko.md`는 삭제하고
  public identity 규칙은 configuration·공통 spec, 실행 순서는 internals가 소유하도록 분리했다. 공식 Redis
  extension은 options와 두 Store 구현만 공개한다. 검증은 ContractTests 65/65, UnitTests 1076/1076,
  Redis tests 69/69이며 cross-language 환경 test 2개만 skip됐다. 같은 기준의 Java/Kotlin·Node.js·C++
  source와 exact interface 수렴은 병렬 진행 중이므로 다섯 언어 parity gate는 아직 완료하지 않는다.
- C++ public boundary도 같은 두 Store 기준으로 수렴했다. `location_store_t`가 descriptor, owner lease,
  opaque authority, creation reservation, relocation capacity와 aggregate operation을 소유하며 외부 provider가
  구현할 필요가 없는 peer·Spot·Actor·route raw DTO·CRUD, watch, dynamic registration과 routing allocation
  surface를 제거했다. Actor·Spot resolver는 authority payload를 사용하고 Automatic RouteMesh discovery는
  MeshNode descriptor와 smaller-RID initiator 규칙을 사용한다. Redis와 in-memory 구현에 남아 있던 raw row
  storage·codec과 비활성 fixture도 삭제했다. 검증은 framework source/library와 contract header build,
  lifecycle 8/8, runtime 1/1, resolver 37/37, in-memory Store 9/9, Redis Store 15/15 및 M6C runtime이다.
  제거 대상 public symbol과 Redis test의 `#if 0` fixture는 0건이며 diff-check도 통과했다. Java/Kotlin과
  Node.js의 post-review 수정이 남아 있으므로 전체 parity gate는 계속 열린 상태다.
- C++ 독립 post-review에서 mandatory `authority_restore_t`, Entry Spot RID 설정 제거, owner lease 내부 row
  격리와 legacy generation alias·미사용 renewal DTO 제거를 추가로 요구했다. Restore는 exact StoreVersion과
  ExpectedOwner를 검증하지만 live lease를 요구하지 않고 payload와 StoreVersion만 변경하여 owner,
  allocation과 두 generation을 보존한다. In-memory와 Redis 구현 및 Instance Spot startup recovery가 같은
  mutation을 사용한다. Focused final gate는 M6B startup recovery, in-memory Store, Redis Store와 contract
  headers가 모두 통과했고 제거 대상 symbol은 0이다. `target_contract`의 기존 9개 실패는
  IMP-CP-05/06/37과 E2E-CP-38/39/43 계열의 선행 구현 gap이므로 이 boundary 수정의 회귀로 분류하지 않는다.
  다섯 언어 교차 감사에서는 socket `internal`·native event/value, SpotNode polling·status·peer·subject,
  STREAM native code와 runtime lifecycle scope가 C++ public header에 남은 점을 추가로 확인했다. 해당
  monitoring 표면을 제거하고 timer failure event만 유지했으며, `service_scope_t`와 scope kind·생성은
  `src/runtime/configuration`으로 옮겼다. Runtime submit queue에서만 사용하는 `pending_operation_t`도
  public include tree에서 runtime messaging으로 옮기고 umbrella `framework.hpp`의 detail helper 직접 include를
  제거했다. Framework library와 contract header, DI scope, STREAM, monitoring test가 통과했고 public
  install·exact scan의 제거 대상 symbol은 0이다. RuntimeMonitoring server와 전체 sample build의 기존 API
  migration gap은 별도로 유지하며 internal header로 우회하지 않는다.
- Node.js는 framework root의 `ZLinkProviderResolver`, decorator scanner metadata와 Nest root의 Spot handle
  resolver·Actor Spot resolver·runtime event publisher token을 제거했다. Companion composition은 private
  structural type과 internal token module이 소유하고, sample과 E2E의 Spot lookup은 public
  `ZLINK_SPOT_MANAGER.find`로 수렴했다. Provider가 구현하는 모든 transitive DTO·value·union은
  `08-location-maintenance.ko.md`가 단일 exact owner이며 stale raw Location barrel은 runtime internal로 옮겼다.
  `restore(expectedOwner)`는 in-memory·Redis와 root 없는 Preparing startup recovery에 연결했고 optional
  `getMeshNodeChangeStamp?`도 source·exact·test에 고정했다. 검증은 typecheck, full build와 browser build,
  M6C runtime 64/64, Redis contract 11/11, contract·documentation·sample 50/50 및 변경 관련 Nest 3/3이다.
  Nest 전체 단일 test file의 기존 `ZLinkHttpClientModule` runtime DI 실패는 public-boundary assertion과
  분리해 기록하며 디버그 변경은 남기지 않았다. 독립 post-fix review에서 stale Location Store test와
  Location Store를 Relocation Store로 중복 등록한 SpotActorTransfer E2E를 찾아 각각 internal enum과 별도
  `ZLinkRedisRelocationStore`로 수정했다. Framework-issued Entry SpotId 계약과 충돌하던
  `configureEntrySpot`·`ZLinkEntrySpotOptions`·caller fixed RID도 public source, runtime, sample·E2E와 fresh
  declaration에서 제거했다. Runtime은 `<prefix>-entry-<uuid>`를 발급해 Actor join에 전달한다. 최종 독립
  증거는 public contract·decorator 33/33, Location·Redis 16/16, M6B startup recovery 39/39이며 제거 API와
  내부 root export 잔존은 0이다. 다섯 언어 교차 감사에서는 `.NET`에서 제거한 native SpotNode polling과
  STREAM native diagnostic이 Node.js에 남은 점을 추가로 확인했다. Spot polling registration과
  status·peer model, `StatusChanged`·`PeersChanged` event를 제거하고 timer failure event 두 종류만
  유지했다. Readiness sample·E2E는 public `ZLinkRouteMeshRuntime` snapshot·observe를 사용하며 STREAM
  error는 provider-neutral error kind와 optional message만 공개한다. 최종 typecheck·build와 focused
  monitoring·contract-surface·stream-session 73/73이 통과했고 제거 대상 root export 5종과 negative
  assertion 외 symbol 잔존은 0이다. Sample·E2E 독립 tsconfig의 기존 rootDir·다른 public API WIP 오류는
  이번 monitoring 경계 변경과 분리한다. 독립 post-fix review에서 Entry Spot timer diagnostic이 `NodeRid`를
  기록하던 오류와 중단되는 timer가 실패 event 두 개를 발행하던 오류를 찾았다. Diagnostic은 canonical
  `SpotId`를 기록하고, 계속 실행할 때 `TimerHandlerFailed`, 중단할 때
  `TimerStoppedAfterUnhandledException` 하나만 발행하도록 수정했다. Source에는 있으나 exact 문서에 빠졌던
  `ZLinkRouteMeshRuntime`의 snapshot·observe·isReady와 필요한 DTO·event도 단일 owner에 추가하고 inventory가
  누락을 검출하도록 고정했다. 이어 host-only termination 계약과 충돌한 RouteMesh별 drain·await와 drain
  DTO·snapshot field를 제거하고 RuntimeMonitoring·DiscoveryRegistryHa·RegistryMessaging·ObservabilityOps·
  Bingo를 public host `retire`로 이관했다. Decorator로 등록한 Entry Spot timer도 context의 canonical
  `SpotId`를 diagnostic에 전달한다. TicTacToe sample의 internal `spotNodeRuntime.primaryMeshNode.status()`
  cast는 public RouteMesh snapshot·observe로 바꾸고 재유입 방지 gate를 추가했다. 최종 build·typecheck,
  focused contract 60건·monitoring 8건, host-only 전환 56/56과 TicTacToe gate 1/1이 통과했으며 public
  source·exact·generated declaration과 sample·E2E의 topology drain 직접 호출은 0이다.
- Java/Kotlin은 `ZLinkLocationStore`에 optional change stamp와 mandatory Restore mutation을 추가하고
  in-memory·Redis·startup recovery를 exact StoreVersion과 ExpectedOwner fence로 정렬했다. Obsolete
  `addRouteMeshChannel`·`addSpotMesh`, transfer-forward option, `ZLinkDrainControl`과 mesh별 drain/result 계층은
  public source에서 제거하고 runtime internal bridge 또는 canonical host `retire/shutdown`으로 이관했다.
  Legacy Spot binding 67개와 backend SPI/DTO 44개는 각각 `runtime.internal.binding.spot`과
  `runtime.internal.backend`로 옮겼으며 SpotId는 String, NodeRid만 RoutingId로 유지한다. Monitoring event의
  internal DTO 누출도 public MeshNode state·peer snapshot projection으로 교체했다. Functional
  `ZLinkNetworkOptions`는 root bind/advertise 기본값과 listener override를 RouteMesh, ClientServer, fanout과
  STREAM listener에 적용하며 actual bound port를 discovery endpoint에 반영한다. Java provider exact 문서는
  Store signature가 요구하는 transitive public type을 소유하고 Kotlin은 같은 Java SPI/type hierarchy를
  복제하지 않는다. 검증은 Java core 576/576, targeted contract 16/16, testkit·Spring·Kotlin compile과
  diff-check다. 전체 contract의 documentation scenario inventory 불일치 1건은 기존 fixture inventory gap으로
  분리한다. 후속 독립 review에서 exact interface에 없는 `ZLinkMeshDrainSnapshot`과 MeshNode snapshot의
  drain projection, public Store signature가 사용하지 않는 owner-lease 보조 DTO 3개를 추가로 확인했다.
  종료 상태 조회는 host `ZLinkRuntimeQuery`로 통일하고 보조 DTO는 `runtime.internal.locations`로 옮겼다.
  Claim·Read·Renew·Release 결과 union은 외부 provider가 구현하는 Store SPI에 필요하므로 유지했다. 최종
  재검증은 Java core 576/576, Redis 실행 15건 통과·환경 조건 11건 skip, Kotlin 46/46, Spring·testkit
  compile과 제거 class·Kotlin 중복 SPI 잔존 0이다. RuntimeMonitoring E2E compile의 기존
  `ZLinkRequestContext`·`setHeartbeatInterval` 참조 3건은 별도 구현 gap으로 분리한다.
- `.NET` post-review는 native backend 정보를 application public contract에서 추가로 제거했다. SpotNode
  status·peer·subject polling projection과 `AddSpotEvents`를 삭제하고 상태·peer 관측은 canonical RouteMesh
  monitoring으로 통일했다. Spot event는 provider-neutral timer failure 두 종류만 유지한다.
  `IncludeNativeDiagnostics`, socket `Internal`, STREAM native code와 diagnostic record도 제거하고 stable
  Framework error kind와 optional message만 공개한다. Public declaration이 없는 exact
  `13-examples.ko.md`는 삭제했으며 사용 설명은 guide가 소유한다. 중복 SpotNode projection을 유지하는
  대안은 얕은 monitoring 표면과 backend 정보 누출을 만들기 때문에 제외했다. 검증은 packaged API
  snapshot 생성·비교, ContractTests 65/65와 UnitTests 1073/1073이다. ZoneWorld·RuntimeMonitoring service와
  doc fixture의 전체 build에는 이 변경과 무관한 병행 API 수렴 gap이 남아 있어 해당 gate의 증거로
  사용하지 않는다. 독립 post-fix review도 추가 finding 없이 끝났고 exported assembly signature snapshot
  test 1/1로 제거 대상 내부 계약이 public API에 남지 않았음을 다시 확인했다.
- Host maintenance audit에서 `Retiring` publication의 부분 실패를 보강했다. 시도한 descriptor 전체의
  `MarkServing` 결과를 확인한 경우에만 `Blocked/StoreUnavailable`로 복원하며, 하나라도 복원을 확인하지
  못하면 placement·membership·inbound relocation·session binding admission을 닫고 bounded teardown 뒤
  `ForceStopped/TeardownFailed`로 끝낸다. Publication 성공 시점에 admission을 닫고 그 commit을
  Retire·Shutdown arbitration 경계로 사용한다. Relocation 결과는 `HasCommitted`와
  `CommittedUnitCount`를 internal drain result에 보존하여 첫 commit 전 실패만 `Serving` rollback과
  `Blocked`를 허용하고, commit 뒤 실패는 `ForceStopped`로 처리한다. User Spot member Actor는 Spot
  aggregate가 소유하므로 standalone Actor preflight·drain inventory에서 제외했다. Shutdown은 runtime,
  auto-connect와 Location teardown을 마친 뒤에만 `Stopped`를 공개하며 `ForceStopped` cleanup도 최종 host
  state는 `Stopped`다. Runtime relocation limit 변경은 기존 lease를 바꾸지 않고 다음 admission부터
  적용하며 `PollingInterval`과 `StoreFailureGrace`의 non-positive 값은 startup configuration error다.
  Focused 증거는 AspNetCore와 Unit project build 0 warning·0 error,
  `DrainCoordinatorTests` 34/34, `MaintenanceRuntimeTests` 19/19,
  `RelocationRuntimeTests` 88/88, `NodesAndServicesTests` 54/54와 User Spot member inventory 1/1이다.
  전체 Unit은 983/984였으며 남은 1건
  `StatefulServiceRuntimeTests.PublicSpotManagerUsesReserveAndRemoteUserSpotCommands`는 동시 진행 중인
  Relocation Store 등록 계약 변경 때문에 startup에서 Store 미등록으로 실패했다. 이 checkpoint만으로
  `.NET` 완료 상태를 올리지 않는다.
- Location provider에서 정식 exact interface에 없는 public Routing ID slot allocation, allocated RID와
  legacy owner lease helper를 제거했다. Production·sample·E2E의 해당 symbol은 0이고 contract test에는
  제거를 확인하는 negative assertion만 남겼다. API declaration snapshot과 회귀 증거는 source tree가
  안정된 뒤 다시 생성한다.
- Standalone Actor relocation은 application state와 accepted frame·reply/session fence를 immutable root에
  저장하고 manifest만 wire에 전달하며, target restore와 startup recovery가 published authority reference를
  사용하도록 연결했다. Inventory digest는 participant mutation을 canonical encoding하여 검증한다.
- 같은 audit에서 seal 전 source·target relocation permit의 all-or-nothing 획득, target node·Spot generation
  fence를 포함한 durable mutation, 여러 local Object Server 중 exact target recovery, command 45 뒤
  steady normalization 전 session admission이 열리는 순서 문제가 확인됐다. 이 항목은 Actor relocation
  lane에서 수정 중이다.
- Host Retire의 User·Instance Spot aggregate는 source seal·capture·aggregate prepare/commit과 target
  Stage·Publish·Abort·held relay를 실제 service wire에 연결하는 중이다. Target command의 exact source fence,
  authority publication 재확인, local aggregate visibility, held-ingress idempotency·ACK·crash recovery와
  Instance Spot startup recovery가 완료 조건으로 남아 있다.
- Retire static contract audit에서 authority commit 전에 held ingress가 durable root에 연결되지 않는 crash
  window, target replay terminal 전에 command 44·45와 admission을 여는 순서, target ACK 전에 source retained
  resource를 해제하는 순서와 Instance Spot이 256 MiB byte gate의 oversized 예외를 공유하는 gap을 확인했다.
  Source가 수락한 ingress의 durable-before-ACK, replay barrier, cleanup phase 분리와 object-kind별 permit
  정책을 Retire lane에서 수정 중이며 전체 회귀 전에는 완료로 판정하지 않는다.
- Automatic MeshNode RID·Entry Spot ID는 startup candidate에서 한 번만 발급하고 registration을
  변경하지 않은 채 owner lease, `Preparing` descriptor와 Entry authority `NewClaim`, socket bind,
  actual endpoint renew, `Serving` renew 순서로 연결했다. 같은 candidate의 active conflict는
  takeover나 재발급 없이 typed conflict로 끝내고, exact owner token cleanup도 InMemory·Redis에
  연결했다. 전체 `.NET` 회귀 전이므로 통과 증거로 확정하지 않는다.
- Direct positive route cache는 owner lease의 provider expiry가 아니라
  `OwnerLeaseFencingMargin`을 뺀 monotonic admission deadline까지만 유지하도록 수정했다. 같은 deadline을
  host의 outbound·inbound operation admission과 Location mutation token에 연결했으며, cleanup은 deadline
  뒤에도 exact owner token으로 진행할 수 있게 분리했다. Monotonic expiry와 exact-token renew 재개 test를
  추가했지만 전체 `.NET` 회귀 전이므로 통과 증거로 확정하지 않는다.
- Direct Spot·Actor route snapshot과 backend authority observation에 target `NodeGeneration`을
  보존했다. Remote target은 현재 admitted peer lifecycle과 resolve한 generation이 정확히 같아야 하고,
  local target도 local lifecycle·object generation·authority owner generation을 함께 검사한다.
  Restart한 같은 `NodeRid`로 operation을 retarget하지 않는 test를 추가했지만 전체 `.NET` 회귀 전이므로
  통과 증거로 확정하지 않는다.
- `26-object-routing` 대조에서 direct wire와 target admission에 `OwnerLeaseGeneration`이 빠지고 Actor
  stale 결과가 positive cache를 즉시 제거하지 않는 gap을 확인했다. Public location row는 exact interface의
  positional `long LeaseGeneration`을 유지하고 internal route·wire fence로 변환하며, current operation을
  재제출하지 않은 채 다음 call만 fresh resolve하도록 수정했다. Service wire schema·validator·generated
  asset과 decoder fixture도 같은 fence와 forwarding operation ID·hop 계약으로 정렬했다. Root 재검증에서
  schema 40 commands·167 types·37 bounds, invalid mutation 233건, generated drift와 decoder fixture가 모두
  통과했다. 전체 `.NET` 회귀 전이므로 runtime 완료 증거로 확정하지 않는다.
- Direct lane 독립 source review에서 확인한 P1 네 건 가운데 global ID resolver의 잘못된 `MeshName`
  선검증, API baseline의 낡은 Actor target signature와 exact contract가 금지한 public
  `ActorRefSnapshot`을 제거했다. Actor Message Follow는 original operation ID·hop, reply
  correlation·flags·payload와 source·target node·authority·lease fence를 adapter부터 remote relay까지
  보존한다. Committed mapping만 사용하고 hop 8에서 추가 forwarding을 typed stale error로 끝내며 relay
  중 Store lookup이나 fresh-owner retry를 하지 않는다. 관련 source test를 추가했지만 전체 `.NET` 회귀
  전이므로 완료 증거로 확정하지 않는다.
- Public `ActorRef`는 exact `(ActorId, ObjectGeneration, MeshName, NodeRid)`만 유지한다.
  `ActorRefSnapshot`과 `BindAsync(IZLinkActor)`는 production·API declaration에서 제거했고 bind는 caller가
  제출한 exact route를 한 번 사용한다. Owner reply의 node·authority·lease fence를 stored binding에
  기록하며 relay·disconnect·push는 Store를 다시 읽거나 default mesh를 사용하지 않는다.
- Session replacement는 Actor별 serialization gate에서 새 owner ACK 뒤 local binding table을 atomic하게
  바꾸도록 정리했다. 독립 audit에서 cross-owner rebind의 이전 exact tombstone이 실패하면 다음 bind가
  올 때만 재시도되는 P1을 확인했다. Location·Relocation Store에 binding route를 저장할 수 없으므로
  target owner가 새 identity를 등록하고 이전 owner tombstone ACK까지 받은 뒤 bind terminal을 반환하도록
  내부 protocol 책임을 옮기는 중이다. Source session owner는 terminal 전까지 기존 binding을 유지한다.
  Same-owner atomic replacement, tombstone failure·retry와 cancellation fixture까지 통과해야 완료로
  판정한다.
- Retire aggregate의 64 MiB 단일 root 제한을 없애는 과정에서 새 `.NET` tree codec이 공통
  `relocation-manifest-v1`·`relocation-data-chunk-v1` golden과 다른 private format을 사용하고, relocation마다
  64 MiB buffer를 즉시 할당하며 async Store를 동기 대기하는 gap을 확인했다. 공통 golden의 manifest·chunk
  envelope, canonical inventory digest와 order 검증을 그대로 사용하고, 작은 payload의 memory를 실제 크기에
  비례시키며 sync-over-async 없이 최대 256 GiB logical stream을 저장·복원하도록 수정했다. 독립 source
  review는 ZLTC·ZLTM golden bytes, 64 MiB·4096·256 GiB bounds, 256 MiB permit와 complete-tree renew·delete를
  clean으로 판정했다. Initial root는 `Pending`, source queue commit 뒤 만드는 새 immutable root는
  `Completed` marker와 다음 aggregate generation을 사용하도록 분리했다. 모든 participant authority가
  Completed root를 가리키는 aggregate CAS 뒤에만 target이 command 44·45, steady normalization과 admission을
  진행한다. Recovery도 Completed root만 finalize할 수 있다. Precommit failure는 durable aggregate abort
  ACK, Session route restore ACK, target cleanup, source unseal 순서이며 abort outcome이 불명확하면 sealed
  상태를 유지한다. 독립 final audit와 전체 `.NET` 회귀 전이므로 완료 증거로 확정하지 않는다.

이 checkpoint에는 build·test 통과 주장을 기록하지 않는다. 세 source lane이 합류한 뒤 `.NET` source build,
Unit·Contract·Redis 회귀, API snapshot 재생성, 문서 verifier와 `P-HIGH` 독립 review를 순서대로 실행한다.
`framework/doc/plan/log/`는 수정하지 않았다.

2026-07-26 source 합류 뒤 `Zlink.Framework.csproj --no-restore -m:1`과 Unit project compile은
warning·error 0으로 통과했다. `26-object-routing`의 stored binding route를 검증하는
`SessionActorCoordinatorTests` 25건, exact owner token과 typed capacity projection을 검증하는
`LocationResolverTests` 27건과 `LocationRuntimeTests` 15건도 통과했다. Entry Spot,
relocation·stateful service와 owner lease focused suite 및 전체 Unit·Contract·Redis 회귀는 진행
중이다. 이어서 owner lease와 InMemory Store focused suite 13건이 통과했고, Contract project는
warning·error 0으로 compile된 뒤 고정 API snapshot test를 제외한 63건이 통과했다. API snapshot은
병렬 source lane이 모두 끝난 뒤 한 번만 다시 생성한다. Relocation codec은 private `ZLR1/v2`만
해석하던 gap을 제거하고 공통 `relocation-envelope-v1` logical stream을 decode·byte-exact
재저장하도록 연결했으며 `RelocationRuntimeTests` 57건이 모두 통과했다. 이 전체 gate와 독립
review가 끝나기 전에는 위 부분 통과를 `M6-RUNTIME` 또는 row 완료 증거로 사용하지 않는다.
Hosted startup은 generated runtime RID를 Location lease와 MeshNode가 한 번만 공유하고, DI와
registration이 같은 Location Store instance를 사용하도록 정렬했다. User Spot은 registered stable
type으로 authority를 검증하고 physical close와 authority CAS 삭제 책임을 분리했다. 이 경계의
`StatefulServiceRuntimeTests` 20건과 Unit project build도 warning·error 없이 통과했다.
Entry Spot Actor dispatch fixture는 실제 InMemory Location Store owner lease, descriptor와 Actor·Spot
authority lifecycle을 사용하도록 바꿨다. Exact session 사전 binding, restart descriptor cleanup,
Spot reply envelope와 straggler의 mesh·lifecycle identity를 target contract에 맞춘 뒤
`EntrySpotActorDispatchTests` 78건이 모두 통과했다. Production validation은 완화하지 않았다.
통합한 `18-object-routing.ko.md`를 다시 기준으로 실행한 focused 회귀에서는
`LocationResolverTests` 27건, `EntrySpotActorDispatchTests` 79건과
`SessionActorCoordinatorTests` 31건이 모두 통과했다. 이 결과는 positive Ready cache와 Store miss,
exact generation·owner fence, Session owner가 보관하는 binding route를 확인한 중간 증거이며,
canonical relocation successor와 recovery release gate가 끝나기 전에는 `.NET` 기준 구현 완료로
판정하지 않는다.
.NET Redis provider와 test project는 warning·error 없이 build됐고 전체 Redis suite는 65 pass,
0 fail, 2 skip으로 끝났다. Redis authority·relocation 11건, Entry Spot identity 1건,
Actor manager production 2건과 Redis/InMemory parity도 모두 통과했다. 두 skip은
`ZLINK_REDIS_TEST_ENDPOINT`와 `ZLINK_REDIS_CROSS_LANGUAGE_PREFIX`를 사용하는 cross-language
harness가 설정되지 않은 조건이다. `RemoveAllByOwner`는 spec대로 exact owner의 ephemeral descriptor만
제거하고 durable authority·reservation을 유지하도록 InMemory와 Redis 의미를 통일했다.
`LocationLifecycleTests`를 제외한 전체 Unit suite 887건은 모두 통과했다. 이 과정에서 concurrent
Spot·Actor `GetOrCreate`가 Creating `Conflict(Found)`를 즉시 실패로 끝내던 gap을 확인했고, 이미
진행 중인 authority의 terminal publication을 join·wait하도록 두 manager를 정렬했다. InMemory Store와
Location runtime regression도 Creating은 Conflict, Ready만 AlreadyExists이며 host cleanup 뒤 durable
authority가 유지되는 계약으로 갱신했다.
마지막 Location lifecycle lane에서는 Actor leave가 빈 Spot ID를 기록하지 않고 tracked Entry Spot
identity를 복원하도록 했으며, authority CAS conflict가 exact ownership-loss event와 deactivation으로
이어지게 연결했다. Auto-connect도 deterministic identity conflict를 Store outage로 삼키지 않는다.
`LocationLifecycleTests` 16건을 포함한 전체 `Zlink.Framework.UnitTests` 903건이 0 fail로 통과했고
Unit project build는 warning·error 0이다.
고정 API snapshot은 외부 review 디렉터리에서 생성한 결과를 exact spec과 대조한 뒤 갱신했다.
`ContractTests` 64건, 8개 package의 source·packaged API 비교, clean consumer와 standalone HTTP
consumer가 모두 통과했으며 public API snapshot SHA-256은
`78199baab05ef79282b114d59e66967404781d03252e7f6e74af8bcd3dffe4ed`이다.
Retire relocation delta는 target staging용 initial root와 authority가 가리키는 final root를 분리했다.
Initial root를 encoding한 실제 크기와 16 MiB ingress hold를 함께 예약한 뒤, source가 hold를 고정하면
held journal을 포함한 final `Pending` root의 encoding 크기로 permit을 줄인다. Source cleanup이 끝나면
같은 journal과 다음 aggregate generation을 가진 `Completed` root로 authority를 한 번 더 전환한다.
Target singleton reconciliation loop는 staged participant의 exact authority key만 다시 읽고, final root의
held suffix를 복원해 sealed replay를 계속한다. Exact source lifecycle이 유지되는 동안에는 target admission과
expiry cleanup을 모두 sealed 상태로 유지한다. Source lifecycle이 fenced된 뒤에만 cleanup completion과
local publication을 진행하며, authority read 실패·resolver unavailable·늦은 authority commit은 local abort로
처리하지 않는다. Steady authority normalization은 completed root reference와 checksum을 stage에 보존하고,
aggregate CAS 성공 뒤 manifest를 삭제한다. CAS 응답이 유실되면 다음 reconciliation이 normalized authority를
확인하고 같은 manifest 삭제만 다시 실행한다. `RelocationRuntimeTests` 66건과
`RelocationStartupRecoveryTests` 7건, 전체 Unit 926건, 전체 Contract 64건이 0 fail로 통과했다.
공통 문서 verifier는 service wire 40 commands·167 types·233 negative self-tests,
public-contract trace 56 documents·189 code blocks·unclassified 0·ambiguous 0, 다섯 언어 exact
contract와 198개 ledger row를 통과했다. Instance Spot 전용 verifier도 다섯 언어의 required
fragment 112개와 forbidden rule 11개를 통과했다. Review profile의 이전 `medium` 기대값은 사용자가
확정한 Codex `gpt-5.6-sol high`와 맞도록 plan README와 semantic inventory를 함께 갱신했다.

최초 전체 `.NET M6-RUNTIME`은 candidate 234 files를 고정한 뒤 62.6초에 통과했다. Compile은
warning·error 0, public contract 64/64, internal runtime 888/888, resource 70/70, protocol 82/82이며
sample·E2E project와 task 실행은 0이다. Candidate manifest는
`.artifacts/v11/evidence/V11-M6A-DN/candidate-current.json`, 결과는 같은 디렉터리의
`result-current.json`에 기록했다. Manifest SHA-256은
`2a42d73655559af414254da9530430bcf2a21c6ee60929f75a633a784ae96340`, source aggregate는
`1c92865eb1f1f8ceaafdae7df14cfdbdc1c022efbcc70d16955e8c6c81f0e21f`이다. 이 결과는 정식
POSD·DDD review의 시작 조건이며, review finding 수정과 전체 재회귀 전에는 `.NET` reference
revision 완료 증거로 사용하지 않는다.
`REMOVE --scope framework:dotnet`도 1개 inventory record에서 제거 대상 symbol·file·artifact
모두 0으로 통과했고, 결과는
`.artifacts/v11/evidence/V11-M6-DN-REFERENCE/removal-current.json`에 기록했다. 이 gate는 Core
service migration 잔존물을 검사하며, exact public interface 호환 우회와 미사용 internal seam은
현재 진행 중인 정식 review에서 별도로 판정한다.

Codex `gpt-5.6-sol high` 정식 review는 P1 3건과 P2 3건을 확정했다. 각 finding은
다음 대안을 비교한 뒤 수정했다.

- Aggregate normalization은 zero vector 전체 허용, participant별 CAS 분해, Preserve-only
  aggregate에만 exact zero 허용을 비교했다. NewOwner capacity validation과 aggregate 원자성을
  모두 유지하는 세 번째를 선택했다.
- Canonical relocation payload는 별도 DTO 확산, streaming opaque reader, incomplete vector 거부를
  비교했다. Codec이 bounded journal·timer·completion vector와 EOF를 모두 검증하고
  runtime에는 immutable bytes projection을 제공하도록 정렬했다.
- 미사용 RouteMesh 10 ActorTransfer seam은 새 deep port로 재정의하는 안과 완전히
  제거하는 안을 비교했다. Current relocation caller가 없으므로 backend·wrapper·dispatch·managed
  state machine과 전용 test를 함께 제거했다.
- Actor relocation deadline은 call site 변환과 공통 deadline executor를 비교했다. Remote path가
  사용하는 internal executor에 terminal winner와 `DeadlineExceeded` mapping을 집약했다.
- Session binding route는 shared mutable holder와 Session owner table의 immutable value 단독 소유를
  비교했다. Table이 validation·accepted high-water·atomic replacement를 소유하고 Actor handle은
  exact binding snapshot만 읽는 두 번째를 선택했다.
- Store CAS 없는 takeover는 exact relocation reservation으로 재정의하는 안과 미사용 path
  제거를 비교했다. Caller가 없고 authority fence를 우회하므로 mode·branch·handoff helper를
  제거했다.

수정 후 전체 Unit 906/906, Contract 64/64, Redis 66 pass·0 fail·2 environment skip,
8개 package source·packaged API·clean consumer와 standalone HTTP consumer가 통과했다. Public API
snapshot SHA-256은 `943a4976a59e43d5827ec05932bfe04b4d7a3fbc88df2b9bf11ad80da7acce56`이다.
Post-review candidate는 243 files이며 manifest와 result는
`.artifacts/v11/evidence/V11-M6A-DN/candidate-post-review.json`과 `result-post-review.json`이다.
Manifest SHA-256은 `870183c7429596404c1960126ede46f4a581c1b3a9c6119746d79beb40d1742a`,
source aggregate는 `70c0ccb58aa5b40175b3dde427690066f603c1c214b24c5cca20b4b50adba696`이다.
`M6-RUNTIME` 5/5에서 compile warning·error 0, public contract 64/64, internal runtime 891/891,
resource 70/70, protocol 81/81이 통과했고 sample·E2E 실행은 0이다. Protocol count 1건 감소는
미사용 ActorTransfer fence test를 production seam과 함께 제거한 결과이다. 현재 post-fix 독립
review가 이 candidate를 재검사 중이며 clean 판정 전에는 reference revision을 고정하지 않는다.

후속 review는 aggregate completion·steady normalization을 별도 public API로 늘리지 않고 기존 aggregate
CAS의 두 mode로 폐쇄적으로 정의했다. Relocation mode는 `NewOwner` participant가 하나 이상이며 capacity
vector가 해당 participant의 durable allocation delta와 정확히 같아야 한다. Completion·normalization mode는
모든 participant가 `Preserve`, capacity가 exact zero, membership mutation이 모두 empty여야 하며 owner,
object·authority generation, membership과 durable Active allocation을 유지한다. 공통 Location·Redis spec,
다섯 언어 exact interface와 .NET Redis provider 계약을 같은 의미로 수정했고 document verifier는 exact
문서 56개·code block 189개·unclassified 0·ambiguous 0으로 통과했다. InMemory와 Redis provider도 같은
검증을 적용하며 Redis 전체 회귀는 66 pass·0 fail·2 cross-language environment skip이다.

같은 review에서 target Actor handoff expiry가 raw `TimeoutException`으로 wire에 노출되는 문제, canonical
relocation text가 invalid UTF-8을 replacement character로 수락하는 문제와 효과가 없는 backend
`BindRemoteActorBoundSession` seam을 추가로 확인했다. Expiry는 typed `DeadlineExceeded`로 통일했고,
canonical reader는 strict UTF-8과 malformed identity rejection을 적용했다. No-op interface·runtime call·빈
wrapper 구현·test fake도 모두 제거하여 symbol reference가 4건에서 0건이 됐다. 관련 focused test
97/97와 Session binding focused test 27/27, 수정 후 전체 Unit 907/907, Framework build warning·error 0이
통과했다. Codex `gpt-5.6-sol high` 재review는 조치가 필요한 POSD·DDD finding 0으로 `CLEAN` 판정했다.
최종 candidate는 243 files, manifest SHA-256
`79929c3ef3c7a37edae7a3705d09a4d2db8f613277bba63f5d395f269a38263e`, source aggregate SHA-256
`e962b460f90a59860a46f9066bbdcc88fd6e22f93a2294946eb3a0d3a36e5184`이다. Evidence는
`.artifacts/v11/evidence/V11-M6A-DN/candidate-reference.json`과 `result-reference.json`에 기록했다.
최종 `M6-RUNTIME` 5/5에서 compile warning·error 0, public contract 64/64, internal runtime 892/892,
resource 70/70, protocol 81/81이 통과했고 Sample·E2E 실행은 0이다.

#### 0.1.3 2026-07-26 reference 이후 미러링·E2E 재활성화 checkpoint

Reference commit `84be905a72`를 `codex/route-mesh-v11`에 push한 뒤 C++·JVM·Node production runtime
미러링을 병렬 시작했다. 각 lane은 [Spot·Actor routing 통합 spec](../../framework/common/spec/18-object-routing.ko.md)과
.NET reference의 component boundary, state machine, ordering, ownership, error와 resource lifetime을 기준으로
작업하며 Sample·E2E는 실행하지 않는다.

.NET은 다른 언어 미러링과 동시에 topology E2E를 한 scenario씩 다시 활성화한다. 첫
`LocationMessaging` compile에서 낡은 handler context, `ChannelName`, mesh 중복 인자를 받는 channel call,
`SubmitAsync`, public resolver와 `IZLinkDrainControl` 사용을 확인해 exact public surface로 옮겼다. Runtime
실행에서는 channel-only automatic MeshNode가 Object Server가 아니라는 이유로 Location descriptor claim을
건너뛰어 peer가 0건이 되는 gap을 확인했다. Automatic acquisition이면 object role과 무관하게 generated
RID candidate로 descriptor를 claim하고 actual endpoint를 renew하도록 startup 조건을 수정했다. Automatic
provider는 fixed RID 대신 prefix를 사용하고 manual topology의 targeted route만 fixed RID를 유지한다.
`RM-A1` Location Store auto-connect, `RM-A4` prefix replacement·stale endpoint 제거와 `RM-A6`
multi-channel isolation scenario가 각각 통과했다. `RM-B1` scale-out에서는 두 Ready provider가
100:100 weight여도 기존 selector가 한 provider의 weight 구간을 연속으로 소비해 새 provider를 최대
99회 동안 선택하지 않는 gap을 확인했다. Weighted ring 전체에서 exact 비율을 유지하면서 서로소 step으로
후보를 고르게 순회하도록 수정했고 focused unit test 8/8과 `RM-B1`이 통과했다. 전체
`RM-B2` scale-in에서는 `Retire`도 `Shutdown`처럼 selection 제외 전 admission을 먼저 seal해 propagation
중 target-free request가 실패하는 gap을 확인했다. `Shutdown`은 seal을 먼저 유지하고 `Retire`는 Retiring
marker와 weight 0을 게시·전파한 뒤 seal하도록 intent별 순서를 분리했다. Drain focused unit test 30/30과
`RM-B2`가 통과했다. `RM-B3`은 automatic generated RID를 logical fixture label로 오인한 target을 actual
descriptor RID로 바꾸고, lease expiry 뒤 current member가 아닌 target을 `RequestTargetNotFound`로 검증한다.
`RM-C3`은 expected RID 없는 두 outbound admission을 첫 intent 하나에 결합하던 runtime gap을 발견해
advertised endpoint로 connection intent를 식별하도록 수정했으며 3-node 회귀 test가 통과했다. `RM-C9`은
result-free one-way terminal도 source-local admission timeout을 typed `DeadlineExceeded`로 완료한다는 현재
계약에 맞춰 정상 submit과 bounded failure를 함께 허용하고 handler evidence와 후속 request 복구를 확인한다.

최종 full run은 `RM-A1`, `RM-A2`, `RM-A4`, `RM-A6`, `RM-B1`~`RM-B3`, `RM-C1`~`RM-C5`,
`RM-C7`~`RM-C9`의 15 scenario를 149초에 모두 통과했다. Evidence는
`framework/languages/dotnet/e2e/LocationMessaging/logs/20260726-060216-2117260`이며 E2E Client build도
warning·error 0이다. `framework/doc/plan/log/`는 수정하지 않았다.

이어 SpotService의 첫 회귀 묶음을 exact public surface와 generated RID로 전환했다. Session→remote Actor
request는 stored binding route를 사용했지만 actor-frame relay가 STREAM `RequestSeq`와 reply flag를 버려
target reply가 원래 Session으로 돌아오지 못했다. Relay envelope에 preserved reply correlation을 넣고,
Session node는 pending request의 exact binding token으로 원래 STREAM Session을 찾아 reply frame을 쓰도록
수정했다. Target은 이 reply route를 새 Location lookup이나 native binding token으로 대체하지 않는다.
`Remote_Actor_Reply_Uses_The_Preserved_Session_Request_Route` focused test가 통과했다. E2E harness는 caller가
node를 지정한다는 이전 가정 대신 public runtime placement weight를 변경하고 Location descriptor 반영을
확인한 뒤 eligible candidate를 제한한다. Session host의 Object Server는 Actor dispatch capability를
유지하되 새 placement 후보에서는 제외했다. `SM-B1`, `SM-B2`, `SM-B3`, `SM-B5` 회귀 묶음은 모두
통과했고 evidence는
`framework/languages/dotnet/e2e/SpotService/logs/20260726-064955-3125330`이다. 이 묶음은 기존 source
회귀를 증명하며, Config 2가 요구하는 실제 User Spot membership relocation·adapter·durable cleanup 전체를
대체하지 않는다. 해당 full scenario는 후속 E2E 활성화에서 별도로 검증한다.
변경 뒤 전체 `Zlink.Framework.UnitTests` 912건도 0 fail로 통과했다. Startup error log는 각 negative
configuration test가 의도한 관측 결과이며 test failure가 아니다.
두 번째 SpotService 회귀 묶음은 generated runtime RID와 node-wide placement 계약으로 정렬했다. 이 과정에서
E2E와 다섯 .NET sample의 User Spot이 제거된 `OnClosingAsync(CancellationToken)` overload를 계속 선언해
default interface callback이 실행되는 gap을 확인했다. 모든 구현을
`OnClosingAsync(ZLinkSpotClosingContext, CancellationToken)`으로 옮겨 close reason과 cleanup token을 받도록
수정했다. `SM-A3`, `SM-A6`, `SM-B4`, `SM-B7`이 모두 통과했으며 evidence는
`framework/languages/dotnet/e2e/SpotService/logs/20260726-065925-3314255`이다.
세 번째 묶음은 각 Auth 전에 node-wide placement weight가 Location descriptor에 반영됐는지 확인해 Actor
owner를 고정하고, 두 Session을 사용하는 targeting scenario 중간에도 candidate를 전환했다. `SM-D1`,
`SM-D2`, `SM-D6`이 모두 통과했으며 evidence는
`framework/languages/dotnet/e2e/SpotService/logs/20260726-070326-3377150`이다.
`26-object-routing`의 preserved reply route를 사용하는 `SM-D11`은 새 server lifecycle에서 단독
실행해 통과했으며 evidence는
`framework/languages/dotnet/e2e/SpotService/logs/20260726-071006-3403657`이다. 같은 server lifecycle의
`SM-D9` 다음 `SM-D11`이 이전 Actor `actor-sm-d9`을 받은 원인은 STREAM binding이 아니었다. D9의 remote
Actor create와 D11의 local Actor create가 각각 독립 counter의 첫 operation ID를 사용해 동일한 durable
creation terminal key를 만들었고, D11이 D9 terminal을 idempotent replay로 읽었다. Local User Spot create,
Instance Spot activation과 Actor create가 MeshNode의 remote operation과 같은 allocator를 사용하도록 통합해
local·remote operation ID namespace를 분리했다. 수정 뒤
`framework/languages/dotnet/e2e/SpotService/logs/20260726-075545-8582`에서 `SM-D9`, `SM-D11`, `SM-D13`
client operation이 모두 통과했다. 이 실행에서 D13 callback이 누락된 원인도 이어서 닫았다. Session owner가
bind 때 저장한 remote Actor route 대신 process-local Actor state를 disconnect 시점에 다시 조회해 remote
binding을 찾지 못했다. 이제 `26-object-routing`의 stored binding route를 그대로 사용하며 disconnect frame에
exact binding token·binding generation·Session owner lifecycle generation을 싣고, target은 이 값과 source
node·Session RID가 current binding과 모두 일치할 때만 callback을 실행하고 terminal 뒤 exact binding을
제거한다. E2E runner도
create coordinator인 `play-a`를 owner로 가정하지 않고 Location
Store가 확정한 eligible owner의 evidence를 확인한다. D13 단독 실행은
`framework/languages/dotnet/e2e/SpotService/logs/20260726-081007-305308`, `SM-D9`·`SM-D11`·`SM-D13`
회귀 묶음은 `framework/languages/dotnet/e2e/SpotService/logs/20260726-081035-324200`에서 process exit 0으로
통과했다. Binding generation fence를 추가한 최종 D13 단독 재실행도
`framework/languages/dotnet/e2e/SpotService/logs/20260726-081818-654108`에서 통과했다. Exact remote
disconnect 회귀 test를 추가한 뒤 전체 `Zlink.Framework.UnitTests` 913건도
0 fail로 통과했다. 같은 process의 다른 MeshNode를 target으로 고른 Instance Spot local activation도 target이
아니라 source MeshNode의 allocator에서 operation ID를 발급하도록 고쳤다. Local create·activation과 transport
operation이 같은 source lifecycle namespace를 공유하는 회귀 test를 추가한 뒤 전체 UnitTests 914건이
0 fail로 통과했고 public ContractTests 64건도 0 fail이다. Session-owned binding entry를 Actor-side
projection으로 복제하지 않도록 DDD boundary를
정리한 뒤 D13을 `framework/languages/dotnet/e2e/SpotService/logs/20260726-082513-866581`에서 다시 실행해
통과했다. 이 수정은 이미 고정한 .NET reference candidate 이후의 routing correction이므로 다음
.NET integration checkpoint에서 delta review와 reference manifest 재생성을 수행한다. 기존 candidate는 다른
언어가 미러링을 시작한 기준 revision의 이력으로 유지하되 최종 reference로 다시 사용하지 않는다.
`26-object-routing`과 `31-session-actor-dispatch` 기준의 독립 delta review에서는 remote reply가
`(ActorId, RequestId)`만 확인하고 transport source·owner/binding fence와 flags를 버리는 문제, reply 유실과
cancellation에서 pending entry와 `ActiveFrames`가 남는 문제, 빈 disconnect payload가 exact fence를
우회하는 문제, disconnect callback보다 tombstone을 먼저 적용하는 문제를 각각 확인했다. Request마다
Framework가 발급한 reply capability를 stored binding snapshot과 함께 보관하고 actor frame·Message Follow·reply
relay가 그대로 보존하도록 수정했다. Session owner는 capability, 실제 transport source와 responder identity,
exact current binding snapshot, no-bind flags가 모두 일치할 때만 reply를 전달한다. Timeout과 runtime reset,
reply delivery cancellation은 pending correlation을 terminal-once로 제거하고 accepted frame accounting을
정리한다. Disconnect의 빈 payload와 잘린 payload는 callback 없이 drop하며 callback 성공·실패 terminal 뒤에만
같은 binding token을 제거한다. 후속 재리뷰에서 invalid reply가 pending을 소비하거나 concurrent duplicate가
두 번 reply할 수 있는 terminal claim race와 이전 timeout이 재사용된 request ID를 제거하는 ABA를 확인했다.
Identity 검증과 `Claimed` 전환을 pending lock 안에서 수행하고, claim terminal까지 같은 entry를 유지해 request ID
재사용을 차단했다. Invalid·duplicate·이전 timer는 entry를 소비하지 않으며 claim dispose 한 경로에서만 exact
pending 제거와 `ActiveFrames` 1회 감소를 수행한다. Invalid 뒤 valid reply, concurrent duplicate, timeout ABA와
같은 binding의 active frame 두 개 회귀를 포함한 `Zlink.Framework.UnitTests` 918/918과
`Zlink.Framework.ContractTests` 64/64가 통과했다. 실제 process D13의
첫 실행 `framework/languages/dotnet/e2e/SpotService/logs/20260726-084851-1458873`은 responder RID를 display
문자열로 encode한 오류를 찾아 실패했고, wire가 요구하는 hex로 고친 뒤
`framework/languages/dotnet/e2e/SpotService/logs/20260726-085041-1466449`에서 통과했다. Atomic claim 보강 뒤
최종 D13도 `framework/languages/dotnet/e2e/SpotService/logs/20260726-085913-1698427`에서 통과했다. Codex
`gpt-5.6-sol high` 독립 최종 재리뷰는 invalid·duplicate·timeout·reset 경쟁과 exact cleanup을 다시 확인해
`CLEAN`으로 종료했다. 이 routing correction을 새 .NET reference manifest로 고정해야 한다.

#### 0.1.4 2026-07-26 .NET 기준 구현 완료 감사

현재 source와 exact interface, Unit·Contract test를 진행 중인 .NET 관련 행과 다시 대조했다. WSL 경로에서
`Zlink.Framework.UnitTests` 940/940과 `Zlink.Framework.ContractTests` 65/65가 통과했다. 처음 Windows
UNC 경로에서 실행한 회귀는 다른 build와 `obj/.../ref` 산출물이 충돌해 compile 단계에서 중단됐으므로
통과 증거로 사용하지 않는다.

감사 결과는 다음 세 범주로 구분한다.

- `V11-M6-OBJECT-CONTEXT-DN`과 `V11-M6B-ENTRY-IDENTITY-DN`은 production 연결과 현재 전체 회귀가
  확인되어 완료로 전환한다. 이전 행에 적힌 fixture·snapshot 대기는 더 이상 유효하지 않다.
- `V11-M6-SESSION-BINDING-DN`은 focused runtime과 process D13까지 구현 gate를 충족했다. 남은 조건은
  공통 변경 review와 전체 process E2E이므로 runtime 미구현으로 분류하지 않는다. `V11-M6A-WEIGHT-DN`도
  구현과 focused test는 끝났고 candidate artifact와 `ROW-GATE` 재생성만 남았다.
- `V11-M6-DEFERRED-JOIN-DN`의 User Spot cross-node 경로는 이미 application state·accepted journal·session
  route와 completion cursor를 같은 immutable Relocation root에 저장하고 startup에서 재개했다. 남아 있던
  Entry Spot 경로도 target descriptor의 `EntrySpotId`를 선택한 뒤 같은 authority CAS·relocation transaction을
  사용하도록 연결했다. Completion root 갱신이 application state·accepted journal·logical timer·recovery
  payload와 `ObjectGeneration`을 보존하는 test를 추가했고, 전체 Unit 941/941과 Contract 65/65가 통과했다.

기존 `V11-M6-DN-DESIGN-REVIEW`와 `V11-M6-DN-REFERENCE` 완료 표시는 현재 candidate에 적용할 수 없다.
고정 revision `01b13d41cb54f367ba8f46e42b5946f037c6ee47` 뒤에 routing correction, canonical Retire aggregate,
automatic topology Retire preflight와 cleanup ordering, public API snapshot 변경이 들어왔다. 이전 review와
manifest는 실행 이력으로 보존하되 두 행을 `수정 진행`으로 되돌린다.

새 reference를 고정하기 전의 실제 차단 조건은 다음과 같다.

1. `30-implementation-gap.ko.md`의 오래된 .NET 판정을 source와 다시 맞춘다. Actor location의 Spot
   generation, STREAM Actor dispatch mesh, relocation outcome metric, Context composition, Entry identity,
   typed capacity와 Retire aggregate는 현재 source에 구현되어 있다. 반면 Stream Connector의 공개
   `ReceivedCount`, handler-bound receive의 bounded queue admission, RouteMesh snapshot의 Core 미노출 값을
   빈 값·근사값으로 채우는 부분은 실제 차이로 남아 있다. 물리 ROUTER의 방향별 HWM·timeout과
   `ConfigureSpotPublisher()`의 책임 중복은 계약 결정을 먼저 끝내야 한다.
2. 현재 source tree가 안정된 뒤 API snapshot, Redis 회귀, `.NET M6-RUNTIME`, `REMOVE`, `ROW-GATE`를
   새 candidate manifest로 다시 실행한다.
3. Codex `gpt-5.6-sol high` 독립 POSD·DDD delta review에서 finding 0을 확인한 뒤 새 immutable reference
   revision과 hash를 기록한다.

따라서 940/940·65/65 통과만으로 현재 `V11-M6-DN-REFERENCE`를 완료로 유지하지 않는다. Runtime 차이와
정식 spec의 구현 차이 목록을 먼저 0으로 만들고, 새 manifest와 review가 같은 source revision을 가리켜야
다른 언어의 최종 미러링 기준으로 사용할 수 있다. `framework/doc/plan/log/`는 수정하지 않았다.

#### 0.1.5 2026-07-26 .NET 기준 구현 high delta review

Codex `gpt-5.6-sol high`가 0.1.4 이후의 .NET production delta를 exact interface와 다시 대조했다. 이번
review는 finding 0이 아니며, 다음 red flag를 POSD·DDD 기준으로 분류했다.

- Retire preflight가 publication state를 미리 바꾸고 Shutdown과 보상 rollback으로 경쟁했다. 이는 lifecycle
  결정을 실행 순서에 분산한 temporal decomposition이다. 보상 rollback을 유지하는 안과 preflight를 read-only로
  두고 shared barrier 안에서만 `Retiring`을 publish하는 안을 비교해 후자를 적용했다.
- Canonical authority를 legacy relocation projection으로 변환한 뒤 다시 저장해 canonical field를 잃을 수 있었다.
  이는 authority 지식이 codec과 coordinator에 중복된 information leakage다. Legacy round-trip을 보강하는 안과
  canonical state를 그대로 보존하고 protocol boundary에서만 분기하는 안을 비교해 후자를 적용했다.
- Entry Spot route가 User Spot authority row를 합성해 공통 resolver에 통과시켰다. 이는 Entry descriptor와 User
  authority의 bounded context를 섞는 domain boundary leak이다. 합성 authority를 유지하는 안과 descriptor의
  RID·lifecycle generation fence로 직접 route하는 안을 비교해 후자를 적용했다.
- Source cleanup 완료 marker와 실제 source resource cleanup의 owner가 갈라져 marker가 먼저 보일 수 있었다.
  이는 conjoined method와 temporal decomposition이다. 선행 marker를 보상하는 안과 target replay 뒤 실제 source
  cleanup, durable completion, finalization 순서로 한 owner에 모으는 안을 비교해 후자를 적용했다.

수정 뒤 Maintenance focused 16/16, Service runtime foundation 18/18, Relocation runtime 74/74가 통과했다.
전체 회귀는 Unit 944/944, Contract 65/65, Redis 67/67이며 Redis cross-language fixture 2건은 환경 조건으로
skip됐다. `M6-RUNTIME`의 첫 실행은 기존 STREAM shutdown upper-bound test 1건이 10초 timeout으로 실패했지만
해당 test의 단독 재실행과 전체 gate 재실행은 통과했다. 최종 5개 category 결과는
`.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-high-review.json`에 기록했다.

Post-review에서 다음 세 contract gap이 남아 있어 `V11-M6-DN-DESIGN-REVIEW`와
`V11-M6-DN-REFERENCE`를 완료로 전환하지 않는다.

1. Accepted request replay가 successor relocation root의 replay cursor·terminal completion과 authority의
   reply relay ACK count를 갱신하지 않는다. 관련 codec editor는 test에서만 호출되므로 request를 포함한
   relocation은 durable source cleanup 조건을 충족할 수 없다.
2. Public `EnableActorDispatch()`는 MeshName을 받지 않지만 startup은 Object Mesh가 정확히 하나이기를 요구하고
   내부 `ActorDispatchMeshName`을 추론한다. 여러 Mesh의 global Actor authority 계약과 다르다.
3. Local User Spot join은 target membership을 in-memory registry에 먼저 stage한 뒤 Actor authority CAS를
   실행한다. CAS 실패 시 rollback하더라도 commit 전 target membership 비가시성 계약을 충족하지 않는다.

`30-implementation-gap.ko.md`는 Spot generation과 relocation metric의 현재 .NET 구현을 반영하고, 위
durable completion과 global Actor dispatch 차이를 실제 잔여로 갱신했다. 세 gap을 production test와 함께
해소한 뒤 high delta review finding 0, `REMOVE`, 새 immutable candidate와 `ROW-GATE`를 다시 실행해야 한다.
`framework/doc/plan/log/`는 수정하지 않았다.

## 0.1.6 .NET post-review P0와 Retire preflight 보강 (2026-07-26)

§0.1.5의 세 finding을 production 경로에서 수정했다.

- Accepted request replay는 application handler 완료 뒤 canonical successor root에 replay cursor와 terminal
  completion을 먼저 기록하고 participant authority reference·checksum·count를 aggregate CAS로 전환한다. Source
  reply는 request/ACK로 전달하며 ACK 뒤 pending delivery state를 다시 durable CAS한다. Retry는 current authority가
  가리키는 cursor와 pending terminal payload를 읽어 handler를 다시 실행하지 않고 relay와 ACK CAS만 재개한다.
  Source cleanup은 initial publication reference가 아니라 current successor reference에서 terminal count와 pending
  count를 검증하고 phase 8을 commit한다.
- `EnableActorDispatch()` startup은 Location Store와 Object role 하나 이상만 요구한다. Object Mesh 수를 하나로
  제한하거나 `ActorDispatchMeshName`을 추론하지 않으며, started runtime의 session bind는 local·remote 구분 없이
  resolve한 `ActorRef.MeshName`을 framework route에 전달한다.
- Same-node User Spot join은 Actor state lock 안에서 authority CAS를 먼저 완료한 뒤 target membership과 Actor의
  current Spot state를 게시한다. CAS conflict test는 target membership count와 lookup이 모두 0인 상태를 확인한다.

Retire preflight도 함께 보강했다. Automatic RouteMesh마다 host의 모든 local RID와 draining descriptor를 제외한
replacement가 최소 하나 있어야 하며 exact RID·lifecycle generation의 Core peer가 `Admitted`여야 한다. Empty,
all-draining, source-only와 같은 host의 다른 local MeshNode만 있는 snapshot은 통과하지 않는다. Active User·Instance
Spot aggregate와 standalone Actor target은 `Serving`, source 이상 application version, maintenance wave exclusion,
stable type·policy·Snapshot adapter capability, Actor·Spot·type·activation concurrency headroom을 `Retiring` publication
전에 확인한다. `Disabled` policy는 `RelocationDisabled`, compatible target 부재는 `TargetUnavailable`로 source
admission과 authority를 유지한 채 끝난다.

`gpt-5.6-sol high` 독립 review는 이 수정 뒤에도 다섯 가지 P0를 찾았다. Canonical final validation이 replay 뒤
제거된 journal로 terminal count를 다시 계산한 문제, source reply capability를 30초 Message Follow duration으로 정리한
문제, population limit `0`을 headroom 없음으로 해석한 문제, 여러 Spot·Actor unit이 같은 target capacity를 각각
독립적으로 확인한 문제, same-node join의 authority commit 뒤 callback failure가 retry state 없이 caller exception으로
끝난 문제다. Reply relay의 bool ACK와 started runtime rebind rollback도 P1으로 확인했다.

Final delta는 durable root의 terminal completion·pending count로 target normalization을 검증한다. Source reply
capability는 24시간 유지하고 `TerminalReceived`·`AlreadyTerminal` ACK를 구분하며, relay 실패 시 frozen source
owner lease의 Missing·stale·expired를 Store에서 exact 확인한 경우에만 `SourceLeaseExpired`를 successor root와
authority에 CAS한다. Spot aggregate와 standalone Actor는 하나의 provisional capacity plan에서 Actor·Spot·stable
type·activation 사용량을 누적하고 population limit `0`을 unlimited로 처리한다. Same-node join은 authority commit
뒤 target joined와 source leave callback을 caller cancellation과 분리한 sealed serial turn에서 phase별로 재시도한다.
Started runtime의 failed rebind는 교체 전 table entry가 그대로 남으므로 존재하지 않는 native binding을 rollback으로
다시 만들지 않는다.

최종 검증은 focused finding 회귀 7/7, Unit 950/950, Contract와 API snapshot 65/65, Redis 67/67이다. Redis
cross-language fixture 2건은 기존 환경 조건으로 skip됐다. `M6-RUNTIME` 첫 실행은 기존
`RemoteUserSpotTerminalReplaysAfterDeadlineAndExpiresWithoutReexecution` deadline test 1건이 실패했지만 단독
재실행과 같은 candidate의 전체 gate 재실행은 통과했다. 최종 compile·public declaration·internal·resource·protocol
5/5는 `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-p0-final.json`, 67개 owned file immutable manifest는
`candidate-p0-final.json`에 기록했다. `REMOVE --scope framework:dotnet`은 symbol·file·artifact 0으로 통과해
`removal-p0-final.json`에 기록했고 같은 candidate의 `ROW-GATE`도 통과했다. Core·bindings와
`framework/doc/plan/log/` 변경은 0이다. Final delta 독립 `gpt-5.6-sol high` review는 finding 0이 아니라
Critical 1건, High 3건, Medium 1건을 확인해 candidate 승인을 거부했다. Critical은 .NET Spot relocation이
canonical service wire command 30~35·40~46 대신 `.stage.v1`·`.publish.v1`·`.abort.v1`·`.reply.v1`
typed Route packet을 사용해 mixed-language target과 통신할 수 없고 reply relay ACK에 stable RelocationId,
coordinator와 exact request-source fence가 없는 문제다. High는 target abort cleanup 전에 stage를 제거해 duplicate
abort가 false success ACK를 반환하는 문제, closed `GenerationExhausted`를 CAS conflict처럼 무한 재시도하는 문제,
completed target가 full actor/envelope graph를 bounded tombstone으로 바꾸지 않는 문제다. Medium은 Actor drain이
non-retriable framework error까지 target-local rejection으로 삼켜 terminal reason을 deadline으로 왜곡하는 문제다.
따라서 두 reference 행은 `수정 진행`으로 되돌리고 post-fix 전체 회귀와 새 독립 finding 0 review 전에는 기준
구현으로 승인하지 않는다.

Canonical relocation wire 수정의 첫 checkpoint에서 공통 schema의 command 33 `replyRelay`와 command 46
`replyRelayAck`를 .NET `ZLinkServiceWireCodec`에 exact field order·bound로 구현했다. Command 33은 stable
Relocation ID, target attempt, coordinator owner·lease·node·authority version, participant·sequence와 terminal
result를 보존하고, command 46은 stable Relocation ID, coordinator, operation ID와 exact request-source
owner·lease·node fence 및 `TerminalReceived`·`AlreadyTerminal` closed status만 허용한다. 공통 byte fixture는
`framework/runtime/protocol/golden/reply-relay-v1.json`에 두 command를 고정했으며 .NET focused codec test 2/2와
Framework build warning·error 0이 통과했다. C++도 같은 bytes와 terminal failure mapping을 codec에 구현해
golden·trailing byte·open ACK negative test, 전체 `zlink_framework`와 contract header build를 통과했다. 이
후속 C++ checkpoint에서는 command 33·46 송신과 수신을 production raw RouteMesh infrastructure transport에
연결했다. Relay와 optional application reply는 infrastructure mailbox로만 전달하며, ACK는 authenticated peer의
RID·lifecycle generation이 frozen request-source fence와 정확히 일치할 때만 수락한다. 실제 두 raw node의 relay,
terminal ACK와 stale source-generation 거부를 포함한 `test_cpp_framework_m6b_runtime`이 통과했다. 이
checkpoint는 command 33·46 transport 기반만 완료한 것으로, command 30~35·40~41 전체가 네
runtime에서 같은 fixture를 사용하는 시점까지 `BLK-044`와 reference 행을 완료로 올리지 않는다.

Node.js도 같은 checkpoint에서 maintenance terminal 경로를 production `NodeRequest` dispatch의 canonical
command 33·46으로 교체했다. Accepted request는 source OwnerId·lease generation·node RID·node generation,
non-zero ReplyRouteId와 OperationId를 동결하고, source는 처음 받은 terminal에 `terminalReceived`, 같은 terminal의
재전송에 `alreadyTerminal`을 반환한다. Target은 ACK가 유실되면 exact source owner lease가 유효한 동안 같은
terminal을 재전송하고, ACK 또는 Store가 확인한 exact lease expiry 뒤에만 immutable Completed root의
`pendingRelayCount`를 generation-preserving authority CAS로 줄인다. Reply route는 duplicate ACK를 위해 24시간
유지한다. 공통 `reply-relay-v1.json` fixture의 command 33·46 byte-exact test, ACK 유실 뒤
`alreadyTerminal` 수렴을 검증하는 two-owner test와 Completed root `1→0` CAS test를 포함해 Node M6C 58/58,
M6B 39/39, Actor handoff 10/10, workspace typecheck·build와 수정 파일 scoped ESLint가 통과했다. 전체 `npm test`는
이 변경 범위 밖 `spot-node-runtime-manager.ts:423`의 기존 `no-unnecessary-condition` lint 오류에서 중단됐다.
Prepare·publish·replay·route·normalize·open·release는 아직 `ZLRH1` private control envelope를 사용하고 replay
결과와 delivery 목록도 `node:v8` encoding에 남아 있다. Command 30~35·40~41의 canonical production 연결과
private envelope 제거 전까지 `BLK-042`·`BLK-044`와 Node reference 행을 완료로 올리지 않는다.

Node.js canonical relocation control codec checkpoint는 command 30 `relocationReady`, 31
`relocationData`, 32 `relocationAck`, 34 `relocationSeal`, 35 `relocationComplete`, 40
`relocationPrepare`, 41 `relocationReserved`의 field order와 길이 경계를 구현했다. Command 30은
필수 extension TLV의 source·target node generation, reservation generation, application version와
participant progress를 검증하며 durable root reference와 CRC32C를 함께 보존한다. Command 31은
`frozen-record`의 node source fence, operation·relocation ID와 `relocationControl` body를 검증한다.
Participant와 progress vector는 unsigned participant ID 기준으로 정렬되어야 하고 중복을 허용하지 않는다.
이 fixture와 현재 Node codec이 덮는 command 31 body는 `recordKind=relocationControl` 한 case뿐이다. Schema의
`frozen-record`가 정의하는 node·channel·Spot·Actor send/request, completion, send-ready와 Instance activation
case는 아직 구현 증거가 없으므로 accepted journal production 전환이나 command 31 전체 완료로 판정하지 않는다.
공통 fixture `framework/runtime/protocol/golden/relocation-control-v1.json`은 일곱 command의 semantic field와
byte-exact hex를 고정한다. Node test는 각 command의 decode·re-encode 결과, truncated frame 거부와 duplicate
participant 거부를 검증하며 M6C 59/59, workspace typecheck·build와 수정 source scoped ESLint가 통과했다.
이 checkpoint는 codec과 공통 fixture만 완료했다. Production prepare·publish·replay·route·normalize·open·release는
여전히 `service-relocation-control.ts`의 `ZLRH1` JSON envelope를 사용하고, replay 결과와 terminal delivery 목록은
`node:v8` encoding을 사용한다. Production state machine이 command 40→30→30→41 reservation, command 31→32
participant high-water ACK, command 34 seal, command 35 durable source-cleanup 완료 순서로 동작하고 target이 shared
Relocation Store root를 읽도록 전환하기 전에는 `BLK-042`·`BLK-044`와 Node reference 행을 완료로 판정하지 않는다.

후속 Node.js production checkpoint(2026-07-26)에서 `ZLRH1`과 `node:v8`을 제거하고 command
30·31·32·34·35·40·41을 raw `sendToNode` infrastructure frame으로 직접 교환하도록 전환했다. 요청과 ACK는
relocation·coordinator·target attempt를 key로 사용하는 bounded pending table에서 correlation하며 응답 객체의
callback이나 binding request wrapper에 의존하지 않는다. Source는 immutable root를 Store에 기록한 뒤 target
prepare를 실행하고, target이 같은 root와 CRC32C를 확인한 뒤에만 authority CAS를 수행한다. Target은 admitted
source RID와 coordinator node generation, candidate RID·generation·owner lease, participant ID·sequence와 frozen
target authority generation을 exact fence로 검증한다. Non-empty Actor accepted queue는 원래 operation ID와 request
source fence를 보존한 canonical frozen record로 command 31에 실어 전송하고, target은 durable envelope에서 같은
record를 다시 만들어 byte-for-byte 비교한 뒤 dispatch하고 command 32 high-water ACK를 반환한다. 실제 source와
target 두 owner를 연결한 focused test는 command `40, 30, 35, 31, 34`와 non-empty replay·seal을 통과했다.

같은 checkpoint에서 command 31 decoder·encoder를 frozen-record kind 1~14 전체 closed union으로 확장했다. Node,
channel, Spot, Actor, bound Session source와 operation·reply route matrix, metadata, 각 body의 conditional field와
closed enum을 검증하며 canonical bytes를 그대로 보존한다. Relocation phase 0~9는 모두 round-trip하고 unknown 10은
거부한다. Shared command 31 relocation-control record의 authoritative inner operation ID는 zero로 맞췄다. Node
workspace typecheck·build, 변경 source scoped ESLint, M6C 61/61이 통과했고 production source에서 `ZLRH1`,
`node:v8`, `requestToNode`, `record.reply`가 남지 않은 것도 확인했다. 이 checkpoint는 command 31까지의
production 전환 증거다. Command 33·46의 independent raw send, authenticated pending ACK, durable completion과
exact source lease fence, pre-commit abort의 target staged-state 정리, bound Session production replay 및
mixed-language process 검증은 남아 있으므로 `V11-M6C-NODE`, `BLK-042`, `BLK-044`는 계속 진행 상태다.

후속 Node.js command 33·46 production checkpoint(2026-07-26)에서 terminal relay를 일반 control
request/response union에서 분리했다. Command 33과 command 46은 각각 독립 raw `sendToNode` frame으로 송신하며,
pending key는 ACK target RID·operation·relocation·target coordinator 전체를 길이 경계가 있는 값으로 보존한다.
Target은 command 31의 frozen request source owner·lease·RID·lifecycle generation과 terminal result를 completed
successor root에 `delivery=pending`으로 먼저 CAS하고 participant authority를 같은 root로 정렬한 뒤, 그 root를 다시
읽어 command 33을 만든다. Relocation target coordinator는 current target owner·lease·RID·lifecycle과 현재
authority store version으로 별도 고정한다. Source는 authenticated sender의 admitted descriptor와 coordinator를
exact 비교하고, original request source는 local reply capability와 current source owner에 독립적으로 비교한다.
두 fence를 서로 같다고 가정하거나 RID에서 owner·lease를 추정하지 않는다. Target은 command 46의 request source와
authenticated admitted peer를 durable completion의 source fence에 exact 비교한다. 첫 ACK 유실 시 bounded retry,
duplicate `alreadyTerminal`, operation/source collision, admitted lifecycle mismatch, coordinator authority version
mismatch와 exact source lease expiry를 two-owner focused test에서 검증했다.

Command 40 처리도 target factory·Restore 이전의 side-effect-free offer slot으로 분리했다. Slot은 validated root
fingerprint·participant inventory와 target runtime이 소유한 64-message·256 MiB replay budget의 실제 잔여량만
보존한다. 같은 fingerprint의 retry는 같은 offer에 합류하고, command 30 source accept가 empty offer의 모든 fence와
exact prepare participant를 그대로 수락한 뒤에만 single-flight materialization을 실행하고 command 41을 반환한다.
Zero-work relocation도 요청량에서 값을 추정하지 않고 실제 non-zero target budget을 제시한다. Offer 전
factory·Restore event가 0인 조건과 zero-work offer를 focused test에 고정했다. Node M6C 62/62, workspace
typecheck·build, 변경 source scoped ESLint와 legacy/private relay symbol 검색이 통과했다. Node command 33·46
production 전환은 완료했지만 pre-commit abort target cleanup, bound Session production replay와 모든 방향의
mixed-language process E2E가 남아 있으므로 `V11-M6C-NODE`, `BLK-042`, `BLK-044`는 계속 진행 상태다.

후속 Node.js target lifecycle checkpoint(2026-07-26)에서 command 30 accept와 command 41 사이의 reservation
수명을 정식 계약에 맞췄다. User Spot은 participant 수가 하나여도 aggregate prepare를 사용하고 standalone
Actor·Instance Spot만 단일 relocation capacity fence를 사용한다. Side-effect-free offer는 capacity를 소비하지
않으며 exact accept가 실제 64-message·256 MiB permit을 획득한다. 처음 고정한 reservation request 또는 aggregate
plan만 unknown Store response reconcile에 재사용하고 cleanup 중 새 authority snapshot으로 reservation을 다시
구성하지 않는다. Accepted·prepared 전이마다 precommit timer를 다시 설정하며, committed fence는 abort하지 않는다.
Success는 live stage를 bounded terminal tombstone으로 교체하고 abort와 shutdown은 permit·timer·pending control·
reply relay를 정리한다. Live offer·stage와 tombstone은 각각 1,024개로 제한하고 tombstone은 5분 뒤 제거한다.
`npm run lint`·`build`·`typecheck`, focused host relocation 2/2와 독립 재실행한 M6C 62/62가 통과했다. 이 증거로
target reservation lifecycle은 닫았지만 bound Session ACK와 mixed-language process E2E는 남아 있으므로 세 row의
상태는 계속 진행이다.

같은 시점의 공통 문서 verifier는 service wire 40 commands·167 types·233 negative self-test까지
통과한 뒤 reviewed v11-first member set에서 중단됐다. 현재 actual은 4,800개이고 승인 snapshot은
4,797개다. 차이는 진행 중인 C++ public surface와 Java SpotId constructor inventory 교정이 합류한
candidate이므로, 두 public lane의 high review가 끝난 뒤 semantic inventory를 한 번만 갱신하고 verifier를
재실행한다. 이 실패를 무시하거나 중간 snapshot으로 승인하지 않는다.

## 1. 목표 구조와 범위

Core 11.0은 context, message, raw socket, transport, poller와 generic monitor만 제공한다. MeshNode, Channel,
dispatch, Spot, Actor, Instance Spot, bound STREAM session과 maintenance는 Core에서 제거한다.

C++, .NET, JVM과 Node.js는 각 언어가 service runtime 구현을 소유한다. 공통 Framework native runtime과
네 runtime이 호출하는 private C SPI는 만들지 않는다. 각 runtime은 해당 언어 binding의 설치된 public raw
socket API만 사용한다.

언어 사이에 공유하는 구현 입력은 다음으로 제한한다.

- Framework 공통 공개 계약
- protocol schema와 생성 상수
- golden frame, normalized trace와 오류 fixture
- 공통 E2E scenario와 결과 schema

Java와 Kotlin은 Java binding과 JVM service runtime 하나를 공유한다. 공개 계약은 Java와 Kotlin을 각각
검증하지만 구현 worker는 JVM lane 하나만 둔다. 별도 Kotlin runtime은 만들지 않는다.

C, Python, Go와 Rust bindings는 Core 10.x의 마지막 지원 조합에 격리한다. Core 11.0 build·package·CI에
포함하거나 11.0 호환을 표기하지 않으며 후속 11.x 전환 계획을 만들지 않는다.

## 2. 상태와 기록 규칙

| 상태 | 의미 |
|---|---|
| `대기` | 선행 gate 또는 담당 배정을 기다린다. |
| `진행 중` | 기준 revision과 소유 파일을 기록하고 작업 중이다. |
| `검토 준비 완료` | 구현과 자체 검증은 끝났으나 독립 review가 남아 있다. |
| `리뷰 중` | 독립 reviewer가 기준 revision과 누적 변경을 검토 중이다. |
| `완료` | 완료 gate, review와 이후 변경 영향 확인이 모두 통과했다. |
| `차단` | 미확정 계약이나 외부 환경 때문에 안전하게 진행할 수 없다. |
| `폐기` | 11.0 책임 경계와 충돌하여 실행하지 않는다. 기존 증거는 보존한다. |

작업자는 다음 규칙을 지킨다.

1. 담당 ID, 기준 revision, 이후 변경 목록, 소유 파일과 실행할 test를 증거 칸에 기록한 뒤 시작한다.
2. 정식 spec이나 exact interface가 바뀌면 변경 파일, 바뀐 의미와 직접 의존 ID를 먼저 기록한다.
3. 공통 spec, protocol schema·fixture와 shared manifest는 coordinator만 수정한다.
4. 각 language lane은 자기 언어 source·test·sample과 package 설정만 수정한다.
5. 같은 언어 runtime의 lifecycle과 data path를 서로 다른 worker가 동시에 수정하지 않는다.
6. timeout 증가, polling, raw-frame adapter, reflection과 private Core·binding 접근으로 실패를 우회하지 않는다.
7. skipped required test, 미검증 package, 중단된 review와 일부 언어 결과를 완료 증거로 인정하지 않는다.
8. Core, binding 또는 Framework runtime package가 바뀌면 그 artifact를 사용하는 test·E2E·package 증거를
   다시 만든다.
9. 문서나 aggregate hash 변화 자체는 작업을 차단하지 않는다. 의미가 바뀐 spec, protocol, public API와 주요
   불변 조건의 영향 범위만 다시 검증한다.
10. SPEC 선행 gate가 완료되지 않아도 담당 범위의 문서 작성과 자체 검증은 병렬로 진행할 수 있다.
    다만 모든 선행 ID가 `완료`가 되기 전에는 해당 행을 `검토 준비 완료`, `리뷰 중`, `완료`로
    전환하거나 독립 review를 시작하지 않는다.
11. 기존 Framework public interface, sample과 E2E source는 migration 불변 입력이다. Contract amendment가
    승인되기 전에는 Core service runtime 이관을 이유로 기존 source, scenario ID나 실행 registration을
    수정·삭제하지 않는다. 승인한 public contract 변경으로 영향을 받는 항목은 amendment impact manifest에
    기존 hash, disposition, 새 acceptance intent와 대체 coverage를 먼저 기록한 뒤에만 변경한다. 영향받지 않은
    source와 registration은 Git diff 0을 유지한다.
12. Core service ABI를 대체하는 public·private binding service API를 만들지 않는다. Framework 내부에
    private binding-facing port를 두고 해당 언어 binding의 설치된 public raw API만 호출한다.
13. Framework service runtime이 완성되기 전에는 sample·E2E를 실행해 완료를 판정하지 않는다. Contract
    amendment impact manifest는 각 항목을 `retain`, `amend`, `replace`, `add`, `remove`로 분류하고 실행 상태를
    기록한다. 실행 source와 registration은 `pending-disabled-by-contract-amendment`로 고정한다. Review할
    E2E·sample contract 문서는 `active-contract-spec`으로 구분해 runtime과 병렬로 고칠 수 있지만 실행 증거로
    사용하지 않는다. Review를 이미 통과한 source 변경을 보존해야 하면 exact hash와 함께
    `pending-disabled-reviewed-source`로 기록한다. 세 상태 모두 skip이나 성공 결과가 아니며 source와
    registration을 삭제하거나 주석 처리하지 않는다.
14. Runtime 구현 build에서는 sample·E2E project와 task만 실행 graph에서 분리한다. Sample·E2E source가 새
    interface 때문에 compile되지 않아도 runtime 통과를 위해 source를 임시 수정하거나 compatibility helper를
    추가하지 않는다. 이 구간은 Core raw regression, binding raw contract, protocol fixture, public declaration
    snapshot과 Framework internal unit·contract·resource test를 실행한다.
15. 전체 runtime과 production placeholder 제거가 끝난 뒤 E2E·sample spec을 승인한 contract snapshot에 맞춰
    최종 확정한다. E2E는 topology, stateful object, maintenance, race·cross-language 순서로 작은 묶음씩
    활성화하며 각 묶음의 runtime gap을 닫은 뒤 다음 묶음을 시작한다. Sample은 전체 E2E가 통과한 뒤
    활성화하고 public API만 사용해 compile·run한다.

### 2.1 Runtime-first 실행 순서

RouteMesh 11.0 migration은 다음 순서를 바꾸지 않는다.

1. Core 11 raw runtime과 regression·sanitizer 결과를 고정한다.
2. Core 11 local package와 Framework runtime이 필요로 하는 최소 raw binding package를 만든다.
3. M5 뒤 public contract amendment의 identity, remote create, placement, failure, maintenance 의미와 다섯 언어
   exact interface를 확정한다. 변경될 E2E scenario, sample과 유지할 regression test를 impact manifest에
   나열하고 baseline source·registration hash를 봉인한다.
4. E2E·sample 실행을 source 변경 없이 격리한 뒤 각 Framework 언어의 private binding-facing port에 새 contract의
   실제 동작을 연결한다. Protocol codec, transport·session, liveness, routing·registry, Spot·Actor lifecycle과
   maintenance를 순서대로 구현하되, 기존 Framework source의 구조 변경은 동작 복구에 필요한 최소 범위로
   제한한다.
5. 각 vertical slice가 internal unit·contract·protocol regression을 통과하면 다음 기능으로 진행한다. 새
   public behavior는 E2E 대신 deterministic internal contract test로 먼저 검증한다. 네 언어 runtime과
   production placeholder 제거가 모두 끝날 때까지 E2E·sample source를 구현 편의에 맞춰 바꾸지 않는다.
6. Runtime 전체 합류 뒤 amendment impact manifest를 기준으로 E2E spec과 sample spec을 최종 확정한다.
   영향받은 source만 승인한 disposition에 따라 바꾸고 영향받지 않은 source·registration은 diff 0을 유지한다.
7. E2E를 topology, stateful object, maintenance, race·cross-language 순서로 재활성화해 runtime gap을 닫는다.
8. 전체 E2E 통과 뒤 sample을 재활성화하여 public API 사용, startup, operation과 cleanup을 검증한다.

M4까지는 sample·E2E source와 registration의 Git diff 0만 확인하고 실행하지 않는다. M5와 contract amendment,
M6 runtime 구현은 internal runtime contract만 완료 증거로 사용한다. Contract amendment가 승인한 변경은
impact manifest가 old→new source·registration hash와 coverage를 소유한다. 최초 runtime candidate의 단계별 E2E
활성화와 전체 matrix는 M6 후반과 M7이 소유하고, sample 실행은 전체 E2E 뒤 M7이 소유한다. Final package
조합의 재검증 결과는 M9가 소유한다.

이 순서에서 public interface는 승인된 contract snapshot을 따른다. 제거 대상인 Core service ABI나 binding
service projection을 compatibility layer로 되살리지 않는다. Contract amendment에서 replace·remove한 Framework
surface도 구현 편의를 위한 adapter로 유지하지 않는다. Sample과 E2E 호출부 변경은 runtime 구현 중이 아니라
spec 최종화 뒤 impact manifest가 승인한 항목에만 적용한다.

### 2.2 자율 실행과 blocked issue 처리

작업자는 담당 row의 실행 정보만으로 자율적으로 진행하며 별도 승인을 기다리지 않는다. 진행을 막는 문제를
만나면 작업을 중단하지 않고 스스로 원인을 판정해 해결한 뒤 같은 row를 계속 진행한다. 여기서 blocked issue는
담당 row를 기록된 절차대로 끝낼 수 없게 만드는 실패, 결함, 미확정 입력과 환경 문제를 뜻한다.

해결 순서는 다음과 같다.

1. 담당 row가 소유한 path 안에서 근본 원인을 수정한다.
2. 원인이 소유 밖이면 해당 owner lane의 계약을 지키는 최소 변경으로 해결하고 변경 path와 영향 ID를 기록한다.
3. 공개 계약을 바꿔야 하면 §9.1의 contract amendment 경로로 처리하고 impact manifest에 먼저 분류한다.

자율 해결은 이 문서의 금지 규칙을 무효화하지 않는다. §2의 6·7·11·13·14, §15, §16, §17과 §18의 조건을
우회하는 방법으로 blocked issue를 닫지 않는다. timeout 확대, polling, private Core·binding 접근, fake
success·data, assertion 약화, scenario·registration 삭제나 skip 처리, public contract 축소, 미검증 package
승격은 해결이 아니다. 이런 우회로만 진행할 수 있는 문제는 해결이 아니라 아래 두 예외로 판정한다.

다음 두 경우에만 진행을 멈추고 담당 row를 `차단`으로 남긴다. 그 외에는 `차단`으로 남기지 않는다.

- 자격 증명, 네트워크, 외부 서비스나 라이선스가 없어 실행 자체가 불가능하다.
- 어떤 해석으로 진행해도 공개 계약이나 데이터 정합성을 되돌릴 수 없게 훼손한다.

한 blocked issue가 해당 row를 막고 있어도 그것에 의존하지 않는 다른 row는 계속 진행한다.

작업 중 손댄 코드는 그 자리에서 POSD·DDD 기준으로 정리한다. 깊은 모듈과 정보 은닉, caller 부담, pass-through
제거, aggregate 경계와 중복 상태 기계 제거가 판단 기준이며, 이는 별도 리팩토링 row나 계획을 만들지 않고
진행 중 수행하는 정리다. 손대지 않은 영역을 정리하려고 작업 범위를 넓히지 않는다. 정리가 공개 계약이나
state machine 의미를 바꾸면 그때는 정리가 아니라 계약 변경이므로 §2.2 3단계를 따른다. 의미 있는 단위가
끝날 때마다 관련 변경을 함께 commit하고 push한다.

모든 blocked issue는 발견 즉시 [blocked issue log](blocked-issue-log.md)에 `BLK-<번호>` 항목으로 기록하고,
해결한 뒤 같은 항목의 근본 원인, 수정 내용, 재검증 command와 결과를 갱신한다. 담당 row 증거 칸에는 해당
항목 ID만 참조하고 상태를 이중으로 기록하지 않는다. 이 log는 진행표가 아니며 row 상태의 기준은 계속 이
ledger다.

### 2.3 구현 순서와 동형 수렴

기본 순서는 .NET 우선이다. 먼저 .NET Framework가 정식 spec·.NET exact interface·wire schema의 전체
계약을 구현한다. 전체 회귀가 통과한 뒤 POSD·DDD 정식 review와 그 finding을 반영한 refactoring, 재회귀까지
끝낸 revision만 reference candidate로 고정한다. 그 뒤 나머지 네 언어가 그 형태를 미러링한다. 네 언어가
미완성 .NET 구현을 기능별로 따라가거나 같은 기능을 각자 설계하지 않는다. 전자는 기준 변경을 반복해서
전파하고, 후자는 설계 비용과 수렴 비용을 함께 늘리기 때문이다. 다음 여덟 단계를 순서대로 수행한다.

1. **기준 구현.** .NET lane이 정식 spec·exact interface·wire schema의 M6A·M6B·M6C와 연결된 amendment
   runtime을 모두 구현한다. 각 기능은 focused test를 통과하기 전에 손댄 책임 범위의 design debt를 함께
   정리한다. 새 pass-through helper나 compatibility layer로 임시 통과시키지 않는다.
2. **중간 refactoring checkpoint.** 기능 단위 통합 때마다 POSD red flag와 DDD boundary leak을 검사한다.
   비자명한 구조 변경은 두 가지 이상의 대안을 interface 수준에서 비교하고 선택 이유를 row 증거에 기록한다.
   Lifecycle·ownership·state transition·error invariant는 해당 domain owner 안에 모으고 transport·codec·Store
   adapter detail이 public contract나 object domain으로 새지 않게 한다.
3. **.NET 전체 회귀.** 통합된 .NET candidate에서 internal contract·unit·runtime·resource·protocol 회귀를
   한 runner가 실행한다. 일부 기능이나 focused test만 통과한 revision은 review candidate나 다른 언어의
   미러링 기준으로 사용하지 않는다.
4. **.NET 정식 POSD·DDD review.** 전체 회귀가 통과한 revision을 `P-DEEP` 독립 review 대상으로 고정한다.
   Review는 먼저 red flag를 열거하고 각 항목이 위반하는 원칙을 연결한다. 이어서 최소 두 가지 수정안을
   비교한다. POSD 축은 깊은 모듈, 정보 은닉, caller 부담, pass-through·temporal decomposition,
   special/general 혼합, 오류 집약과 resource 수명이다. DDD 축은 Topology·Channel, Object execution,
   Location authority·Relocation, Maintenance·Session bounded context의 용어, aggregate invariant,
   lifecycle owner, application orchestration과 transport·codec·Store adapter 분리다. DDD 이름을 붙인
   forwarding-only layer를 만드는 것은 해결로 인정하지 않는다.
5. **Review refactoring과 재회귀.** Review finding을 모두 수정하고 해당 focused test와 3단계 전체 회귀를
   다시 실행한다. Refactoring 뒤 공개 계약이나 state machine 의미가 달라지면 §2.2의 amendment 절차로
   돌아가며, test assertion을 약화해 finding을 닫지 않는다.
6. **.NET reference 고정.** Review finding 0, 금지된 compatibility·placeholder·unused code 0과 재회귀
   통과를 증명한 revision만 reference candidate와 evidence로 고정한다.
7. **미러링.** reference candidate가 고정된 뒤 나머지 lane이 기준 형태의 component 경계, state machine,
   ordering·ownership, 오류 분류, resource 수명과 test 구성을 자기 언어로 옮긴다. 형태를 바꾸려면 그
   이유를 담당 row 증거 칸에 기록한다.
8. **교차 review와 동형 수렴.** 다섯 언어가 준비되면 §18의 `I4` 축으로 한 번에 비교한다. 이 단계는
   구현 순서와 무관하게
   항상 수행하는 완료 gate다. 미러링은 형태를 미묘하게 비틀기 때문에 기준 구현이 있어도 리뷰 없이는
   발산을 잡지 못한다. 비교 축은 component 분리와 이름, state machine과 전이 조건,
   ownership·lock·queue 순서, 오류 분류와 복구, resource 해제 시점, test 구성과 negative coverage다.
   기준 구현보다 나은 형태가 나오면 그것을 새 기준으로 채택하고 .NET을 포함한 다섯 언어를 정렬한다.
   축마다 선택과 사유를 기록하며, 우열이 분명하지 않으면 .NET 형태를 유지한다. 수렴 뒤 정식 spec·common
   internals·다섯 exact interface에 반영한다.

기준 구현을 빠르게 완성하기 위해 .NET 내부 작업은 기능별로 병렬 실행한다. 병렬 실행은 설계를 여러 개
만드는 것이 아니라, 같은 정식 계약과 하나의 기준 구조를 서로 겹치지 않는 책임 영역에서 구현하는 방식이다.

lane은 남은 row 수에 맞춰 나눈다. 남은 .NET row가 적으면 나누지 않고 순서대로 처리한다. 분할 비용이 이득보다
큰 구간에서 lane을 늘리면 통합 순서만 길어진다.

각 source 파일에는 한 lane만 write owner를 둔다. `ZLinkManagedMeshNode`처럼 여러 책임을 연결하는 파일과
public registration surface는 .NET integration owner 한 명만 수정한다. 각 lane은 담당 focused test까지
통과시키며, integration owner는 `Topology·Store foundation → Object runtime → Maintenance·relocation`
순서로 합친다. 같은 `bin`·`obj`를 쓰는 full build와 전체 test를 여러 lane이 동시에 실행하지 않는다.
전체 .NET regression은 통합된 candidate에서 한 runner가 실행한다. 최초 통과는 정식 POSD·DDD review의
입력일 뿐 reference candidate가 아니다. Review refactoring과 전체 재회귀까지 통과한 revision만 reference
candidate로 고정하며, 그전에는 C++·JVM·Node production runtime을 확장하지 않는다.

실행 모델에 민감한 새 메커니즘은 예외다. 동시성, 순서, 수명 원시처럼 .NET 형태가 다른 실행 모델로 옮겨지지
않을 수 있는 기능은 기준을 확정하기 전에 실행 모델이 가장 다른 C++(RAII·스레드)와 Node.js(event loop)
두 곳에서 먼저 짧게 시험 구현할 수 있다. 공통 wire schema·fixture 변경 때문에 필요한 최소 compile·parity
수정도 허용한다. 언어별 build·package·test infra 결함과 정식 spec 위반 수정도 기준 구현과 무관하게 계속
진행한다. 이 수정은 기준 형태를 만들지 않고, 미루면 실행 재활성화 구간에서 한꺼번에 드러나기 때문이다.
이 예외를 다른 언어의 일반 runtime 구현으로 넓히지 않으며, 시험 결과로 기준 형태를 정한
뒤에는 다시 1~8단계를 따른다.

한 개념에는 한 이름만 쓴다. 이름의 기준은 정식 spec과
[공통 용어집](../../framework/common/spec/01-glossary.ko.md)이며, 코드 식별자·test 이름·문서 산문이 그 용어를
따른다. 반대 방향으로 코드에서 굳어진 이름을 근거로 spec 용어를 바꾸지 않는다. 새 이름을 붙이기 전에 용어집과
다섯 언어 식별자를 먼저 찾아 이미 쓰는 이름이 있으면 그것을 쓴다. 새 개념이라 이름이 필요하면 용어집과 정식
spec을 먼저 고치고, 그 다음에 다섯 언어 식별자·test 이름·guide 산문을 그 용어로 정렬한다. 코드에 먼저 이름을
붙이고 나중에 문서를 맞추지 않는다. 이미 쓰는 이름이 부정확하다고 판단하면
동의어를 새로 추가하지 않고 용어집·정식 spec·다섯 언어 식별자·test 이름을 한 번에 바꾼 뒤 옛 이름을
제거한다. 이름은 대상을 드러내야 하며, 다른 기능이 이미 점유한 단어를 재사용하지 않는다. 예를 들어
relocation 뒤 이전 owner로 도착한 message를 current owner에게 전달하는 동작의 이름은
[Message Follow](../../framework/common/spec/01-glossary.ko.md#message-follow)이고 유효 기간은
[Message Follow duration](../../framework/common/spec/01-glossary.ko.md#message-follow-duration)이다.
관측 기능인 message flow tracing이나 session relay와 구분한다. 언어별 public option은 같은 기능을
나타내는 `MessageFollowDuration`, `messageFollowDuration`, `messageFollowDurationMs`,
`message_follow_duration`으로 정렬한다.

언어 고유 idiom은 억지로 통일하지 않는다. 리플렉션·attribute·`suspend`·`Flow`·`Promise`·template처럼
언어가 제공하는 표현 수단의 차이는 유지하고, 그 차이가 만드는 public signature 차이도 exact interface가
소유한다. 동형으로 맞추는 대상은 구조와 의미다. 즉 component 경계, 책임 분배, state machine, ordering과
ownership, 오류 분류, resource 수명과 test 구성이다. 언어 능력 차이가 아니라 구현 편의 때문에 생긴 차이는
허용하지 않는다.

교차 리뷰 결과와 조합 결정은 해당 기능의 lane row 증거 칸에 축·선택·사유·정렬 대상 ID로 기록한다. 수렴이
끝나기 전에는 그 기능을 완료로 판정하지 않는다. 이미 완료된 기능이라도 이후 다른 언어에서 더 나은 형태가
확인되면 같은 절차로 조합안을 갱신하고 영향 ID를 기록한다.

## 3. Codex profile과 병렬 lane

각 lane은 아래 중앙 profile 하나를 지정한다. Profile은 작업 성격에 맞는 기본 model과 reasoning
effort를 중앙에서 관리한다. 각 stage 행은 model 문자열을 반복하지 않고 profile만 지정한다.
Lane을 시작할 때 실제 사용한 model, reasoning effort와 profile을 증거 칸에 기록한다. 기본
model을 사용할 수 없으면 대체 model과 이유를 기록하며 조용히 effort를 낮추지 않는다.
기본 model과 effort 선택은 [Codex model·reasoning 공식 안내](https://learn.chatgpt.com/docs/models)를
참고한다. 이 외부 링크의 접근 가능 여부나 문서 hash는 실행 gate와 candidate 판정에 사용하지 않는다.

| Profile | 기본 model·effort | 적용 범위 |
|---|---|---|
| `P-DEEP` | `gpt-5.6 high` | 공개 계약·POSD, 동시성·ownership·recovery 설계와 구현 |
| `P-HIGH` | `gpt-5.6-sol high` | 모든 독립 review. Final E2E·sample spec처럼 public contract와 다섯 언어 coverage를 함께 판단하는 교차 영역 review를 포함한다 |
| `P-DELIVERY` | `gpt-5.6 medium` | 일반 기능 구현, E2E, package, smoke와 consumer 검증 |
| `P-SCAN` | `gpt-5.6-terra low` 또는 `gpt-5.6-terra medium` | manifest·링크·inventory, 범위 분류와 read-heavy scan |

`P-SCAN`은 단순 수집·no-hit 검사에 `low`를 사용하고 분류 판단이 필요하면 `medium`을 사용한다.
이후 모든 독립 review는 `P-HIGH`의 Codex `gpt-5.6-sol high` 단독 reviewer로 진행하며 Claude 병렬
reviewer를 두지 않는다. `P-HIGH`는 기존 row 참조를 유지하려고 남긴 label이며 지금은 `gpt-5.6-sol high`로
해석한다. 완료된 review row가 기록한 `P-DEEP`·`P-HIGH` profile, `gpt-5.6-sol high`·`xhigh` model,
`claude-sonnet-5` 병렬 reviewer와 두 reviewer 수렴 회차는 당시 실제 실행 이력이므로 변경하지 않는다.
재현되지 않은 race나 protocol·ABI 판단을 이 profile로 종료할 수 없을 때만 별도 상향
사유와 검토 범위를 해당 행의 증거 칸에 기록한다.

병렬 실행의 단일 소유자와 합류 gate는 다음과 같다.

| 항목 | 단일 소유자 | 병렬 lane 입력 | 합류 gate |
|---|---|---|---|
| Framework 공통 spec | contract coordinator | 승인된 spec snapshot | 다섯 exact interface와 contract test |
| Core raw spec | Core contract lead | 승인된 raw contract | Core header·export·raw test |
| Protocol | protocol coordinator | schema·생성 상수·fixture revision | 네 codec과 cross-language E2E |
| C++ runtime | C++ lead | 공통 계약과 C++ public binding | C++ contract·race·package gate |
| .NET runtime | .NET lead | 공통 계약과 .NET public binding | .NET contract·race·package gate |
| JVM runtime | JVM lead | Java·Kotlin 계약과 Java public binding | Java·Kotlin ABI·coroutine·package gate |
| Node.js runtime | Node.js lead | 공통 계약과 Node public binding | Node event-loop·export·package gate |
| Cross-language E2E | E2E coordinator | scenario와 fixture revision | 방향이 있는 `4 x 4` 결과 |

### 3.1 Ledger 경로와 ID만 사용하는 작업 지시

작업 프롬프트는 이 ledger의 repository 상대 경로와 담당 ID 목록만 전달한다. 작업자는 다른 v11 plan을 찾아서
checklist를 보완하지 않는다. 각 ID를 시작할 때 다음 순서로 이 문서 안에서 실행 정보를 확인한다.

1. 담당 row의 선행 ID가 모두 `완료`인지 확인한다.
2. 아래 authoritative source 표에서 계약을 읽는 목적을 확인하고 해당 정식 spec·common internals·exact
   interface를 먼저 읽는다. Framework runtime 구현 row는 migration machine inventory가 연결한 보존된 Core
   service 구현과 test snapshot도 함께 읽는다. 새 runtime을 처음부터 별도로 설계하지 않고 기존 구현의 component
   분리, state machine, algorithm, ordering·ownership과 failure 처리를 언어별 runtime에 맞게 옮긴다. 정식
   문서가 목표 계약의 정본이며 보존된 Core service 구현은 구현의 기준 자료다.
3. §3.4의 같은 ID execution card에서 수정 가능한 path, 필수 명령, 산출물과 증거 위치를 확인한다.
4. 명시한 path 밖의 변경이 필요하면 작업을 확장하지 않고 담당 row 증거 칸에 사유와 영향 ID를 기록한다.
   그 변경이 담당 row를 끝내기 위한 blocked issue 해결이면 §2.2의 순서를 따르고 `BLK-<번호>` 항목을 남긴다.
5. 필수 명령의 전체 command line, exit code, candidate manifest와 결과 path를 담당 row의 증거 칸에 기록한다.

각 작업의 상세 증거는 repository에 commit하지 않는
`.artifacts/v11/evidence/<ID>/result.json`에 저장한다. 담당 row의 증거 칸에는 기준 revision, 변경 파일,
authoritative input revision, candidate manifest, 명령과 exit code, 결과 JSON의 절대 경로·SHA-256, finding과
재검증 결과를 요약한다. 진행 상태의 단일 기준은 이 ledger row이며 별도 진행 문서를 만들지 않는다.
§2.2의 blocked issue log는 진행 문서가 아니라 blocker 원인·수정 이력 기록이며 row 증거 칸은 항목 ID만
참조한다.

여기서 기준 revision은 Git base revision과 review 시점의 candidate manifest를 함께 뜻한다. Candidate가 아직
commit되지 않았거나 working tree에 다른 작업이 함께 있다는 이유만으로 review를 막지 않는다. Reviewer는
manifest에 포함된 파일과 당시 content digest를 기준으로 결과를 남긴다. Review 뒤 파일이 바뀌면 바뀐 의미와
직접 의존 범위를 다시 확인하고 manifest를 갱신하며, 이전 hash와 다르다는 사실만으로 finding을 만들거나
전체 review를 처음부터 반복하지 않는다.

### 3.2 Authoritative source와 확인 목적

| 계약 입력 | 이 입력에서 확인할 내용 |
|---|---|
| [Core 11 raw 공개 경계](../../../../core/doc/spec/core/09-runtime-boundary.ko.md) | Core에 남길 raw C ABI, 제거할 service·ZMP heartbeat 공개 표면과 ownership |
| [Core 11 raw 내부 경계](../../../../core/doc/internals/runtime-boundary.ko.md) | 제거할 engine·timer·state와 남길 generic transport·timer·monitor 구조 |
| [Framework 공통·server 정식 spec](../../framework/common/spec/README.ko.md) | Application이 관찰하는 topology, messaging, object, maintenance, liveness와 오류 계약 |
| [Framework 공통 service internals](../../framework/common/internals/README.ko.md) | 네 언어 runtime이 공유할 protocol, mailbox, stateful object, maintenance, session, liveness와 resource 목표 구조. M6 완료 전 target model 표기는 ledger 구현 gap과 함께 해석함 |
| [다섯 언어 exact interface](../../framework/common/spec/server/languages/README.ko.md) | C++·.NET·Java·Kotlin·Node.js의 정확한 public signature와 package owner |
| 보존된 Core service 구현·test snapshot | 기존 component 경계, type 관계, state machine, algorithm, queue·lock 순서, 오류·종료 처리. 새 runtime은 이를 언어별 구조와 raw binding 경계에 맞게 이관하며 목표 의미가 spec·internals와 다를 때만 해당 차이를 적용하지 않음 |
| [Service wire schema](../../../../framework/runtime/protocol/service-wire-v1.schema.json) | Command·field·bound·encoding과 생성 상수의 단일 wire 정본 |
| [공통 E2E 계약](../../framework/common/e2e/README.ko.md)과 이 ledger §14 | 공통 scenario ID, 방향성 조합, race·crash·functional 완료 조건 |
| [공통 sample spec](../../framework/common/sample/README.ko.md) | 다섯 언어 sample이 보여 줄 public 사용 흐름, 역할, message와 acceptance marker |
| `route-mesh-11.0.0-contract-amendment-impact.json` | Contract amendment가 영향을 주는 public member, E2E scenario, sample, registration과 regression test의 baseline hash, disposition, owner와 활성화 단계 |
| [Local package 규칙](../../../../scripts/local-package/README.ko.md) | version 고정 지점, local/internal output, clean consumer와 provenance |
| [Migration machine inventory](../../contract-inventory/route-mesh-v11-core-service-migration-inventory.json) | 제거·보존·대체할 symbol, source, test, package 입력과 담당 gate |

### 3.3 필수 명령과 신규 runner CLI 계약

다음 명령은 repository root에서 실행한다. 모든 row는 `DOC`와 자신의 owned path만 검사하는
`DIFF-OWNED`를 실행한다. `INV`는 shared inventory owner·join·review·final row에서만 실행하고,
`WIRE`는 protocol 영향 card에서 실행한다. 최종 `V11-R7`만 revision으로 고정한 전체 candidate snapshot에
`DIFF-SNAPSHOT`을 사용한다.

```bash
# DOC
scripts/verify-framework-doc-contracts.sh

# INV
scripts/verify-v11-core-service-migration-inventory.sh

# WIRE
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json

# DIFF-OWNED
git diff --check -- <§3.4 card의 owned path 목록>

# DIFF-SNAPSHOT (V11-R7 only)
git diff --check <reviewed-base-revision>..<candidate-revision> -- .

# CORE
./core/build.sh
core/tests/run_test_lanes.sh \
  --build-dir core/build --include-e2e --include-regression

# CORE-ASAN
test ! -e .artifacts/v11/build/core-asan
cmake -S core -B .artifacts/v11/build/core-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DENABLE_ASAN=ON
cmake --build .artifacts/v11/build/core-asan
ctest --test-dir .artifacts/v11/build/core-asan --output-on-failure

# BIND-TEST
./bindings/cpp/tests/run_tests.sh
./bindings/dotnet/tests/run_tests.sh
./bindings/java/tests/run_tests.sh
npm --prefix bindings/node test

# BIND-PKG
scripts/local-package/native/sync-local-core-libs.sh cpp dotnet java node
scripts/local-package/build-wsl.sh cpp
scripts/local-package/build-wsl.sh dotnet
scripts/local-package/build-wsl.sh java
scripts/local-package/build-wsl.sh node

# FW-CPP
cmake -S framework/languages/cpp \
  -B .artifacts/v11/build/framework-cpp \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_E2E=ON
cmake --build .artifacts/v11/build/framework-cpp
ctest --test-dir .artifacts/v11/build/framework-cpp --output-on-failure

# FW-DN
dotnet test framework/languages/dotnet/Zlink.Framework.sln

# FW-JVM
framework/languages/java/gradlew \
  -p framework/languages/java --no-daemon clean check

# FW-NODE
npm --prefix framework/languages/node ci
npm --prefix framework/languages/node test

# SAMPLES
framework/languages/cpp/samples/run_samples.sh
framework/languages/dotnet/samples/run_samples.sh
framework/languages/java/samples/run_samples.sh
framework/languages/node/samples/run_samples.sh

# RAW-PERF-SMOKE
cmake --build core/build
bindings/c/perf/run_benchmarks_multi.sh --pattern ROUTER_ROUTER_REQREP

# E2E-CURRENT
framework/languages/cpp/e2e/run_e2e_all.sh
framework/languages/dotnet/e2e/run_e2e_all.sh
framework/languages/java/e2e/run_e2e_all.sh
framework/languages/java/e2e-kotlin/run_e2e_all.sh
framework/languages/node/e2e/run_e2e_all.sh

# PACKAGE-CONTRACT
framework/languages/cpp/scripts/verify_packaged_contract.sh \
  .artifacts/v11/build/framework-cpp
framework/languages/dotnet/scripts/verify_packaged_contract.sh
framework/languages/java/scripts/verify_packaged_contract.sh java
framework/languages/java/scripts/verify_packaged_contract.sh kotlin
framework/languages/node/scripts/verify_packaged_contract.sh
```

아래 runner는 현재 없는 v11 전용 강제 장치다. 지정한 owner ID가 정확한 path와 CLI로 새로 만들고 unit
self-test를 추가한다. 이후 card가 이 명령을 요구하면 runner가 없는 상태, 다른 CLI 또는 schema가 없는 결과를
완료로 인정하지 않는다.

| 명령 ID | 신규 path와 CLI 계약 | 최초 owner |
|---|---|---|
| `ORACLE` | `scripts/v11/run-oracle.sh --manifest framework/testdata/v11/oracle/oracle-manifest-v1.json --scenario <exact-scenario-id> --output <absolute-trace.json>`; oracle는 child process로만 실행하고 normalized trace를 출력 | `V11-M2-ORACLE` |
| `ROW-GATE` | `scripts/v11/run-ledger-gate.sh --id <exact-ledger-id> --candidate-manifest <absolute-candidate.json> --owned-path-manifest <absolute-owned-paths.json> --evidence <absolute-result.json>`; candidate와 owned-path schema로 provenance·freshness·exit, 변경 path가 §3.4 소유 범위 안인지와 `git diff --check -- <owned paths>` 결과를 검증 | `V11-M2-READY` |
| `REMOVE` | `scripts/v11/verify-removal.sh --scope <core|binding:cpp|binding:dotnet|binding:jvm|binding:node|framework:cpp|framework:dotnet|framework:jvm|framework:node|common> --inventory framework/doc/contract-inventory/route-mesh-v11-core-service-migration-inventory.json --evidence <absolute-result.json>`; export·include·generated·build·package와 runtime load를 검사 | `V11-M2-READY` |
| `CORE-PKG` | `scripts/local-package/core/build-wsl.sh --build-dir core/build --candidate-manifest <absolute-V11-M3-CORE-VERIFY-candidate.json> --review-evidence <absolute-V11-R2-result.json> --output-root <absolute-local-root> --evidence <absolute-result.json>`; R2가 exact candidate SHA를 승인한 install tree·provenance를 만들고 `scripts/v11/verify-core-package-consumer.sh --prefix <absolute-core-install-prefix> --candidate-manifest <absolute-V11-M3-CORE-VERIFY-candidate.json> --review-evidence <absolute-V11-R2-result.json> --evidence <absolute-result.json>`로 빈 C consumer를 검증 | `V11-M3-CORE-CLEAN` |
| `CORE-PKG-TEST` | `scripts/local-package/core/test-build-wsl.sh --self-test --dry-run --evidence <absolute-result.json>`; install artifact를 발행하지 않고 service header copy 0, argument·manifest·clean C consumer fixture를 검증 | `V11-M3-CORE-CLEAN` |
| `BIND-PKG-TEST` | `scripts/local-package/<cpp|dotnet|java|node>/verify-consumer.sh --self-test --dry-run --evidence <absolute-result.json>`; ID suffix `JVM`은 `java`를 선택한다. Package를 발행하지 않고 각 lane이 소유한 package metadata·resolver·public-only consumer fixture를 검증 | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` |
| `WIRE-GEN` | `node framework/runtime/protocol/generate-service-wire-assets.mjs --schema framework/runtime/protocol/service-wire-v1.schema.json <--write|--check>`; C++·C#·Java·TypeScript 생성물과 golden fixture의 source schema revision·drift를 검증 | `V11-M5-PROTOCOL` |
| `TRACE` | `node scripts/generate-v11-public-contract-trace.mjs <--write|--check>`; `framework/doc/contract-inventory/route-mesh-v11-public-contract-trace.json`의 member identity, disposition, POSD decision, spec·test·E2E·package owner와 implementation ledger ID를 deterministic하게 검증 | `SPEC-06` |
| `AMENDMENT-IMPACT` | `scripts/v11/verify-contract-amendment-impact.sh --manifest framework/doc/plan/v11.0/route-mesh-11.0.0-contract-amendment-impact.json --mode <quarantine|finalized> --evidence <absolute-result.json>`; 모든 영향 항목의 baseline·approved hash, disposition, spec·runtime·activation owner를 검증하고 quarantine을 skip·pass로 집계하지 않음 | `V11-CA-IMPACT` |
| `M6-RUNTIME` | `scripts/v11/run-framework-runtime-regression.sh --language <cpp|dotnet|jvm|node> --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; sample·E2E project와 task를 실행 graph에서 제외하고 compile, public declaration snapshot, internal unit·contract·resource·protocol regression만 실행 | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` |
| `V11-E2E` | `scripts/v11/run-cross-language-e2e.sh --slice <topology|stateful|maintenance|full> --matrix 4x4 --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; §14의 유지·변경·대체·추가 ID를 approved impact manifest와 대조하고 required skip을 실패로 처리 | `V11-M6A-E2E`, `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-M9-E2E-4X4` |
| `V11-SMOKE` | `scripts/v11/run-smoke.sh --kind <functional|perf> --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; perf는 §15 최소 operation·provenance·cleanup만 판정 | `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` |
| `FW-PKG` | `scripts/local-package/framework/build-wsl.sh <cpp|dotnet|jvm|node> --binding-manifest <absolute-binding-manifest.json> --output-root <absolute-local-root> --evidence <absolute-result.json>`; persistent Framework package를 만들고 package version·Core·binding provenance를 기록 | `V11-M8-CLEAN-COMMON` |
| `FW-PKG-TEST` | `scripts/local-package/framework/test-build-wsl.sh --language <cpp|dotnet|jvm|node> --self-test --dry-run --evidence <absolute-result.json>`; persistent package를 발행하지 않고 package manifest·provenance·clean consumer fixture를 검증 | `V11-M8-CLEAN-COMMON` |

### 3.4 ID별 execution card catalog

`공통`은 `DOC`와 `DIFF-OWNED`를 뜻한다. 개별 language lane은 shared migration inventory를
수정하지 않고 자신의 candidate delta와 no-hit evidence만 만든다. `INV`는 inventory owner, join, review와
final card에서 coordinator가 병렬 lane delta를 합친 뒤만 실행한다. Wire·protocol을 읽거나 바꾸는 card는
`WIRE`도 실행한다. `V11-R7`은 임의의 dirty worktree가 아니라 reviewed base와 final candidate revision 사이의
전체 snapshot을 `DIFF-SNAPSHOT`으로 검사한다. `<ID>`는
담당 row의 exact ID이며 wildcard나 stage 이름으로 바꾸지 않는다. 모든 output은
`.artifacts/v11/candidates/<ID>.json` candidate manifest,
`.artifacts/v11/candidates/<ID>-owned-paths.json` owned-path manifest와
`.artifacts/v11/evidence/<ID>/result.json`을 포함한다.

Group card의 `<lang>`은 runtime wildcard가 아니라 ID suffix에 따른 다음 고정 매핑을 줄여 쓴 표기다.
`CPP`는 `bindings/cpp/`와 `framework/languages/cpp/`, `DN`은 `bindings/dotnet/`과
`framework/languages/dotnet/`, `JVM`은 `bindings/java/`과 `framework/languages/java/`, `NODE`는
`bindings/node/`와 `framework/languages/node/`를 뜻한다. Owned-path manifest에는 `<lang>`이나 glob을
남기지 않고 해당 ID의 실제 repository path를 나열한다.

| ID | Authoritative input | Owned source·test·package path와 산출물 | 필수 명령·runner | 완료 증거 |
|---|---|---|---|---|
| `SPEC-01` | migration machine inventory와 정식 Framework owner | inventory JSON·generator·verifier; 미분류 0 inventory | `INV`, `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-02` | Framework 공통·server spec | `framework/doc/framework/common/spec/`; 완결된 service 공개 계약 | `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-03` | Core raw 공개·내부 경계 | Core `09-runtime-boundary` 한국어·영문, 관련 inventory; raw-only·ZMP heartbeat 제거 계약 | `DOC`, `INV`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-04` | 다섯 언어 exact interface README와 기능별 interface | `framework/doc/framework/common/spec/server/languages/*/interfaces/`; member trace | `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-05` | Framework 공통 service internals와 wire schema | `framework/doc/framework/common/internals/`, `framework/runtime/protocol/`, golden·negative fixture; schema·formal semantic drift 0 | `WIRE`, `DOC`, `DIFF-OWNED` | 담당 row, schema·formal 대조 결과와 `<ID>` result |
| `SPEC-06` | §5.2·§5.6, 다섯 exact interface, §14 scenario | `route-mesh-v11-public-contract-trace.json`, deterministic generator·checker와 ledger member trace; ctor·overload·generic·callback·enum·extension·export 누락 0 | `TRACE --write`, `TRACE --check`, `DOC`, `INV`, `DIFF-OWNED` | 담당 row, member·owner·disposition·implementation ID 미분류 0과 `<ID>` result |
| `V11-R1`, `V11-R2`, `V11-R3`, `V11-R4`, `V11-R4A`, `V11-R4B`, `V11-R5A`, `V11-R5B`, `V11-R5C`, `V11-R6` | 직접 선행 row의 candidate·evidence, §18의 I1·I2·I3·I4·D1·D2 | source 수정 없음; 독립 finding과 수렴 결과(완료 row는 Codex·Claude 두 reviewer, 이후 row는 Codex 단독 reviewer) | §18 독립 review, `DOC`, `INV`, `DIFF-OWNED`; protocol 영향 시 `WIRE` | 담당 review row, reviewer 결과와 `<ID>` result |
| `V11-R7` | final candidate revision, 직접 선행 row의 evidence, §18; Codex `gpt-5.6-sol high` 단독 reviewer | source 수정 없음; reviewed base→final candidate 전체 snapshot | §18 독립 review, `DOC`, `INV`, `WIRE`, `DIFF-SNAPSHOT` | final review row, reviewer 결과와 `<ID>` result |
| `V11-M2-ORACLE` | 10.x disposition, §14 baseline 대상 | `scripts/v11/run-oracle.sh`, `framework/testdata/v11/oracle/`; frozen manifest·normalized trace | `ORACLE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-CORE-READINESS` | Core raw 공개·내부 경계, machine inventory | Core·perf removal classification; Core removal manifest | `INV`, inventory 기반 Core no-hit probe, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE` | Core raw spec과 현재 binding public surface | `bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`의 read-only probe·gap 결과 | 언어별 binding build·test, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M2-BIND-READINESS` | 네 raw probe와 machine inventory | bindings·package·C/Python/Go/Rust 격리 manifest | `INV`, inventory 기반 binding no-hit probe, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-READY` | M2 전체 evidence와 package 규칙 | `scripts/v11/run-ledger-gate.sh`, schema, `verify-removal.sh`; removal-first candidate contract | `ROW-GATE`, `REMOVE`, `DOC`, `INV`, `WIRE`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M3-PERF-LEGACY` | §15와 oracle manifest | `bindings/c/perf`, `scripts/v11/run-perf-legacy.mjs`; active Spot 입력 제거·10.x archive 격리와 재현 가능한 evidence 생성 | `REMOVE --scope common`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-REMOVE` | Core raw 공개·내부 경계와 Core removal manifest | `core/include`, `core/src`, `core/tests`, Core build manifest; service·ZMP heartbeat 제거 | `CORE`, `REMOVE --scope core`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-CLEAN` | POSD·DDD 원칙, M3 removal diff | Core raw source·test·build, `scripts/local-package/core/`, `scripts/local-package/native/sync-local-core-libs.sh`, `scripts/v11/verify-core-package-consumer.sh`; aggregate·unused cleanup과 review 대상 package tooling | `CORE`, `CORE-PKG-TEST`, `REMOVE --scope core`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-VERIFY` | Core raw spec과 final M3 candidate | Core build·ASAN result, raw test evidence, migration inventory JSON·generator·verifier | `CORE`, `CORE-ASAN`, `CORE-PKG-TEST`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-PKG` | `V11-R2` approved revision, review된 `CORE-PKG` tooling | source·tooling 수정 없음; Core 11 install artifact·header·provenance와 clean C consumer result | `CORE`, `CORE-PKG`, `ROW-GATE`, 공통 | 담당 row, approved revision·artifact path·SHA-256과 `<ID>` result |
| `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` | Core 11 package, Core raw spec, 언어 binding public surface | 각 `bindings/<lang>` source·generated·test와 `scripts/local-package/<lang>/` package metadata·clean-consumer verifier; service·heartbeat projection 0. JVM은 `build.gradle`의 Core source include를 제거하고 Node는 ESM·CJS public export를 모두 제공 | 대응 `BIND-TEST`, 대응 `BIND-PKG-TEST`, 대응 `REMOVE --scope binding:<lang>`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M4-BIND-JOIN` | 네 binding candidate·raw proof | 통합 removal·capability manifest, migration inventory JSON·generator·verifier | binding scope 네 `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M4-PKG-CPP`, `V11-M4-PKG-DN`, `V11-M4-PKG-JVM`, `V11-M4-PKG-NODE` | `V11-R3` approved revisions와 review된 package tooling | source·tooling 수정 없음; `.artifacts/wsl`의 C++ install·NuGet·Maven·tgz와 provenance | 대응 `BIND-PKG`, `ROW-GATE`, 공통 | 각 담당 row, approved revision·artifact SHA-256과 각 `<ID>` result |
| `V11-M4-CONSUMER-JOIN` | 네 local binding package와 중앙 Framework version 지점 | clean consumer workspace·resolution manifest | 네 `BIND-PKG`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M5-PROTOCOL` | wire schema, 공통 service wire·resource internals, service liveness spec | schema·generator·golden·negative fixture와 internal `livenessProbe`·`livenessAck` | `WIRE-GEN --write`, `WIRE-GEN --check`, `WIRE`, `ROW-GATE`, `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M5-SCAFFOLD-CPP`, `V11-M5-SCAFFOLD-DN`, `V11-M5-SCAFFOLD-JVM`, `V11-M5-SCAFFOLD-NODE` | 해당 exact interface, 새 binding package, 공통 service runtime architecture | 각 Framework private binding-facing port, 보존 public value type ownership과 internal compile contract; 새 port의 Core adapter·production placeholder·fake data 0. 기존 service owner 실행 경로의 제거는 M6 vertical slice와 `V11-M6-SCAFFOLD-ZERO`가 소유 | 대응 Framework M5 internal contract, 신규 owned path의 forbidden-reference audit, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M5-FOUND-CPP`, `V11-M5-FOUND-DN`, `V11-M5-FOUND-JVM`, `V11-M5-FOUND-NODE` | 공통 service runtime·wire·mailbox·resource internals, wire schema | 각 Framework transport·codec·operation·resource source와 unit test | 대응 Framework command, `WIRE`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M5-FOUND-JOIN` | 네 foundation evidence와 required E2E catalog | codec cross-language fixture·pending scenario manifest, migration inventory JSON·generator·verifier | `WIRE`, `INV`, `ROW-GATE`, 공통 | 담당 row, pending·completed·skipped count와 `<ID>` result |
| `V11-CA-DECISION` | 두 contract 변경 제안, POSD 공개 경계, M5 foundation evidence | global identity·remote create·placement·reservation·handover·failure의 결정 기록과 proposal open item disposition | `DOC`, `TRACE --check`, `ROW-GATE`, 공통 | open item 미결정 0, 대안·선택 이유·정식 spec owner와 `<ID>` result |
| `V11-CA-SPEC` | 승인한 amendment decision, Framework 공통·server 정식 spec·common internals | `framework/doc/framework/common/spec/`, `framework/doc/framework/common/internals/`; 변경된 목표 계약과 runtime model. 두 임시 target 문서 디렉터리와 repository link는 0 | `DOC`, `TRACE --write`, `TRACE --check`, `ROW-GATE`, 공통 | identity·placement·failure 의미, runtime owner와 migration 입력의 미분류 0과 `<ID>` result |
| `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE` | 승인한 amendment spec, 다섯 언어 exact interface | 대응 exact interface와 public declaration trace. JVM row는 Java·Kotlin을 함께 소유 | `DOC`, `TRACE --write`, `TRACE --check`, `ROW-GATE`, 공통 | public member parity·표현 차이 미분류 0과 각 `<ID>` result |
| `V11-CA-PROTOCOL` | 승인한 amendment spec·common internals와 wire schema | placement reservation·aggregate relocation·route·identity command와 schema·generated constant·golden·negative fixture | `WIRE-GEN --write`, `WIRE-GEN --check`, `WIRE`, `DOC`, `ROW-GATE`, 공통 | formal·schema·fixture semantic drift 0과 `<ID>` result |
| `V11-CA-IMPACT` | 승인한 amendment spec, baseline E2E·sample·registration·regression catalog | `route-mesh-11.0.0-contract-amendment-impact.json`, verifier와 self-test; 각 항목의 baseline hash·disposition·owner·activation stage | `AMENDMENT-IMPACT --mode quarantine`, `DOC`, `INV`, `ROW-GATE`, 공통 | 미분류 0, `pending-disabled-by-contract-amendment` 수, executed·skipped 0과 `<ID>` result |
| `V11-CA-JOIN` | amendment spec·네 exact interface·protocol·impact evidence | amendment candidate aggregate와 public contract trace·impact manifest reconcile | `DOC`, `INV`, `WIRE`, `TRACE --check`, `AMENDMENT-IMPACT --mode quarantine`, `ROW-GATE`, 공통 | 정식 계약·언어·wire·E2E·sample·regression owner 누락 0과 `<ID>` result |
| `V11-CA-DRAFT-RETIRE` | `V11-R4A`가 승인한 amendment candidate와 두 임시 변경 제안 | 두 proposal 삭제, 이 README·ledger의 임시 link 제거와 decision disposition 요약 | `DOC`, `TRACE --check`, `AMENDMENT-IMPACT --mode quarantine`, `ROW-GATE`, 공통 | 채택 내용의 formal spec·exact interface·protocol owner 누락 0, 미채택·수정 이유 미기록 0, 두 proposal과 repository link 0, `<ID>` result |
| `V11-CA-STORE-POSD-SPEC`, `V11-CA-STORE-POSD-IFACE` | 승인한 generic Store 경계, Framework 공통 정식 spec과 다섯 언어 exact interface | Location atomic key/value·version·TTL·scan, Framework-issued immutable Relocation blob, 기존 host lifecycle·Store 등록 API, provider abstraction package와 공식 Redis 최소 공개 표면 | `DOC`, `TRACE --write`, `TRACE --check`, `ROW-GATE`, 공통 | domain-specific public Store operation·중복 등록 helper 0, 다섯 언어 의미·상한·오류·수명 parity와 각 `<ID>` result |
| `V11-M6-STORE-POSD-DN`, `V11-M6-STORE-POSD-DN-REVIEW` | 승인한 Store spec·.NET exact interface와 현재 .NET M6 runtime | .NET provider abstraction, in-memory·Redis Store, authority·relocation runtime adapter, contract snapshot·provider·Redis·crash-retry test | `.NET M6-RUNTIME`, `REMOVE --scope framework:dotnet`, Codex `gpt-5.6-sol high` POSD·DDD review, `ROW-GATE`, 공통 | domain-specific public SPI 0, regression 통과, finding 0과 immutable .NET reference |
| `V11-M6-STORE-POSD-MIRROR`, `V11-M6-STORE-POSD-CONVERGENCE` | 승인한 .NET reference와 네 언어 Store candidate | C++·Java·Kotlin·Node.js의 component 경계·atomic ordering·오류 분류·resource 수명·test 구성 미러링과 교차 review | 대응 네 `M6-RUNTIME`, Framework scope `REMOVE`, §18 I4 review, `TRACE --check`, `ROW-GATE`, 공통 | 다섯 언어 public·runtime·provider·Redis parity, 차이 사유 미분류 0, 동형 수렴 finding 0 |
| `V11-CA-STORE-POSD-DRAFT-RETIRE` | 승인한 Store 경계를 소유하는 공통 spec·다섯 언어 exact interface와 임시 설계 문서 | 임시 문서 세 개 삭제, document inventory·trace·ledger link 정리 | `DOC`, `TRACE --check`, repository link 0, `ROW-GATE`, 공통 | 채택 계약의 정식 owner 누락 0, 임시 문서와 repository link 0, `<ID>` result |
| `V11-CA-SPOT-FLUENT` | 승인한 Instance Spot direct messaging 결정, 공통 Spot spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node exact interface, document inventory·trace와 검증 script; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | global SpotId·Spot 전용 fluent call·User-only manager·type inference·internal close parity와 `<ID>` result |
| `V11-CA-RELOCATION-LIFECYCLE` | `CA-D37~CA-D43`, 공통 Spot Actor·Location·maintenance spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node Actor·Spot·configuration·Location exact interface, document inventory·trace; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --refresh-review`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | opaque bytes·adapter kind·Snapshot invocation·Restore-before-commit·Entry callback·queue·timer·bounded concurrency parity와 `<ID>` result |
| `V11-CA-USER-SPOT-SPEC`, `V11-CA-USER-SPOT-CORE-RID`, `V11-CA-USER-SPOT-IFACE-CPP`, `V11-CA-USER-SPOT-IFACE-DN`, `V11-CA-USER-SPOT-IFACE-JVM`, `V11-CA-USER-SPOT-IFACE-NODE`, `V11-CA-USER-SPOT-E2E-SPEC`, `V11-CA-USER-SPOT-REVIEW`, `V11-CA-USER-SPOT-DRAFT-RETIRE` | User Spot execution·typed capacity·Framework-issued Spot identity·공통 weight 변경 요청, 원문 §11 UUID v4와 §12 weight 대체 조항 | Core socket spec, Framework 공통·server·HTTP client spec, 다섯 exact interface, common E2E, trace와 ledger; proposal은 review clean 뒤 제거 | `DOC`, `TRACE --refresh-review`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | 원문 요구 추적 100%, 상충·미소유 0, 두 reviewer 결과, proposal·link 0과 각 `<ID>` result |
| `V11-CA-SPOT-ID-SPEC`, `V11-CA-SPOT-ID-WIRE`, `V11-CA-SPOT-ID-IFACE`, `V11-CA-SPOT-ID-E2E`, `V11-CA-SPOT-ID-REVIEW` | Spot transport RID와 global logical Spot ID 분리 결정 | 공통 Spot·Location·Redis spec, service wire schema·generated asset, 다섯 exact interface, common E2E, trace와 ledger | `DOC`, `WIRE-GEN --write`, `WIRE`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | Spot 파생 field에 RoutingId 사용 0, UTF-8 1..255-byte exact string·global namespace·UUID 발급·v3 Store schema·binary legacy 거부와 각 `<ID>` result |
| `V11-CA-ACTOR-CREATE-SPEC`, `V11-CA-ACTOR-CREATE-STORE-WIRE`, `V11-CA-ACTOR-CREATE-IFACE`, `V11-CA-ACTOR-CREATE-E2E`, `V11-CA-ACTOR-CREATE-REVIEW` | Actor 생성 승인·거절과 Entry Spot lifecycle 분리 결정 | 공통 Actor·Spot·Location spec, creation operation record·service wire, 다섯 exact interface, common E2E, trace와 ledger | `DOC`, `WIRE-GEN --write`, `WIRE`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | Created·Rejected·Existing terminal result, durable reply, Entry admission callback 제거, 일반 복귀·maintenance callback 순서와 각 `<ID>` result |
| `V11-CA-ONE-WAY-SPEC`, `V11-CA-ONE-WAY-IFACE-DN`, `V11-CA-ONE-WAY-IFACE-JVM`, `V11-CA-ONE-WAY-IFACE-NODE`, `V11-CA-ONE-WAY-IFACE-CPP`, `V11-CA-ONE-WAY-PACKAGE-IFACE`, `V11-CA-ONE-WAY-E2E`, `V11-CA-ONE-WAY-REVIEW`, `V11-CA-ONE-WAY-DRAFT-RETIRE`, `V11-M6-ONE-WAY-CPP`, `V11-M6-ONE-WAY-DN`, `V11-M6-ONE-WAY-JVM`, `V11-M6-ONE-WAY-NODE`, `V11-M6-ONE-WAY-PACKAGES`, `V11-CA-ONE-WAY-RUNTIME-JOIN` | One-way submission 변경 요청과 정식 계약 | Server Framework·HTTP Client·Stream Connector spec, 다섯 exact interface, 네 runtime, package adapter, E2E와 유지 중인 request 문서 | `DOC`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework·package test, `ROW-GATE`, 공통 | result-free terminal, bounded admission, package별 naming, runtime·E2E parity. Draft retire 뒤 request와 `temporary_review_documents` 항목 0 |
| `V11-CA-DEFERRED-JOIN-SPEC`, `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E`, `V11-CA-DEFERRED-JOIN-REVIEW`, `V11-CA-OBJECT-CONTEXT-REVIEW`, `V11-CA-MESSAGE-CONTEXT-REVIEW`, `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE`, `V11-M6-DEFERRED-JOIN-CPP`, `V11-M6-DEFERRED-JOIN-DN`, `V11-M6-DEFERRED-JOIN-JVM`, `V11-M6-DEFERRED-JOIN-NODE`, `V11-M6-OBJECT-CONTEXT-CPP`, `V11-M6-OBJECT-CONTEXT-DN`, `V11-M6-OBJECT-CONTEXT-JVM`, `V11-M6-OBJECT-CONTEXT-NODE`, `V11-M6-MESSAGE-CONTEXT-CPP`, `V11-M6-MESSAGE-CONTEXT-DN`, `V11-M6-MESSAGE-CONTEXT-JVM`, `V11-M6-MESSAGE-CONTEXT-NODE` | Deferred Actor Join·Object Context·MessageContext 변경 요청 | 공통 Actor·Spot·message spec, 다섯 exact interface, Config 8·10 E2E, 네 runtime과 유지 중인 request 문서 | `DOC`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework test, `ROW-GATE`, 공통 | process-local barrier, durable accepted 경계, Context composition·field parity. Draft retire 뒤 request와 `temporary_review_documents` 항목 0 |
| `V11-CA-SESSION-BINDING-SPEC`, `V11-CA-SESSION-BINDING-PROTOCOL`, `V11-CA-SESSION-BINDING-IFACE-DN`, `V11-CA-SESSION-BINDING-IFACE-JVM`, `V11-CA-SESSION-BINDING-IFACE-NODE`, `V11-CA-SESSION-BINDING-IFACE-CPP`, `V11-CA-SESSION-BINDING-E2E`, `V11-CA-SESSION-BINDING-REVIEW`, `V11-CA-SESSION-BINDING-DRAFT-RETIRE`, `V11-M6-SESSION-BINDING-CPP`, `V11-M6-SESSION-BINDING-DN`, `V11-M6-SESSION-BINDING-JVM`, `V11-M6-SESSION-BINDING-NODE` | Session–Actor stored route·disconnect 변경 요청 | Session·Actor·Location·maintenance spec, common internals·wire schema, 다섯 exact interface, Config 2·10, 네 runtime과 유지 중인 request 문서 | `DOC`, `WIRE-GEN --check`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework test, `ROW-GATE`, 공통 | no-Store relay, physical automatic all-settled 통지, same-generation Completed-only route switch parity. Draft retire 뒤 request와 inventory 항목 0 |
| `V11-M6B-EXEC-CPP`, `V11-M6B-EXEC-DN`, `V11-M6B-EXEC-JVM`, `V11-M6B-EXEC-NODE` | User Spot execution mode·Yield·same-gate 정식 계약과 Config 8 | 각 Framework Actor·Spot mailbox, worker·request call, scheduler와 deterministic contract test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | SpotWide·PerActor ordering, Yield allowlist, same-gate 선거부와 각 `<ID>` result |
| `V11-M6C-BARRIER-CPP`, `V11-M6C-BARRIER-DN`, `V11-M6C-BARRIER-JVM`, `V11-M6C-BARRIER-NODE` | execution mode별 all-lane barrier 계약과 Config 10 | 각 Framework close·snapshot·relocation seal/barrier/abort source와 race test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | yielded continuation·Actor·Spot·timer lane quiescence와 각 `<ID>` result |
| `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE`, `V11-M6C-CAPACITY-MONITORING` | typed capacity·Redis v3·monitoring 정식 계약과 Config 6·7·10 | 네 runtime·공식 Location provider, 다섯 monitoring projection, cross-language Redis v3 fixture와 atomic contract test | 대응 Framework `M6-RUNTIME`, Redis provider test, `DOC`, `ROW-GATE`, 공통 | creation·relocation·aggregate·abort·destroy bundle 원자성, monitoring parity와 각 `<ID>` result |
| `V11-M6B-ENTRY-IDENTITY-CPP`, `V11-M6B-ENTRY-IDENTITY-DN`, `V11-M6B-ENTRY-IDENTITY-JVM`, `V11-M6B-ENTRY-IDENTITY-NODE` | Framework-issued Spot UUID v4·global identity claim·descriptor mapping 계약과 Config 2 | 각 Framework identity generator, User Spot automatic `Create`, descriptor `NewClaim`·cleanup, 공식 Location provider와 contract test | 대응 Framework `M6-RUNTIME`, Redis provider test, `ROW-GATE`, 공통 | UUID version·variant, User·Entry first-conflict 즉시 실패, exact lifecycle claim·immutable digest·cleanup과 각 `<ID>` result |
| `V11-M6A-CORE-RID-UUID` | Core raw socket automatic RID 정식 계약 | Core raw socket RID 생성 regression과 caller-fixed·STREAM 경계 test | `CORE`, `ROW-GATE`, 공통 | exact 16-byte UUID v4 version·variant와 `<ID>` result |
| `V11-M6A-WEIGHT-CPP`, `V11-M6A-WEIGHT-DN`, `V11-M6A-WEIGHT-JVM`, `V11-M6A-WEIGHT-NODE` | 공통 signed weight 0..10000 계약과 Config 1·2·12 | 각 Framework RouteMesh·ClientServer·placement configuration, runtime option, descriptor·selection과 contract test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | boundary·runtime revision·100:300 ratio·weight 0·capacity-first·64-bit 합산·multicast once와 각 `<ID>` result |
| `V11-CA-USER-SPOT-RUNTIME-JOIN` | execution·capacity·Entry identity·weight sub-ID 전체 evidence | source 수정 없음; sub-ID candidate·evidence와 기존 M6A·M6B·M6C row 상태 reconcile | `ROW-GATE`, `DOC`, `TRACE --check`, `DIFF-OWNED`, 공통 | runtime·provider·monitoring·identity·weight gap 0과 `<ID>` result |
| `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | amended formal spec·exact interface, 공통 topology·dispatch·liveness internals, 보존된 Core service 구현·test | 각 Framework topology·dispatch·Location·liveness source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | amended formal spec·exact interface, 공통 mailbox·stateful object·STREAM·resource internals, 보존된 Core service 구현·test | 각 Framework Spot·Actor·STREAM·Instance source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE` | amended formal spec·exact interface, 공통 maintenance·monitoring·resource internals, 보존된 Core service 구현·test | 각 Framework maintenance·monitoring·hosting source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6-DN-DESIGN-REVIEW`, `V11-M6-DN-REFERENCE` | 전체 .NET M6A·M6B·M6C·amendment runtime과 최초 전체 회귀 | POSD·DDD red flag·대안 비교 review, finding refactoring, focused·전체 재회귀와 immutable reference manifest | `P-DEEP` 독립 review, `.NET M6-RUNTIME`, `REMOVE --scope framework:dotnet`, `ROW-GATE`, 공통 | finding 0, compatibility·placeholder·unused 0, 재회귀 통과와 exact reference revision |
| `V11-M6-SCAFFOLD-ZERO` | 네 Framework runtime과 amendment impact manifest | production scaffold·placeholder·fake data와 quarantine 실행 graph 검사 결과 | Framework scope 네 `REMOVE`, `AMENDMENT-IMPACT --mode quarantine`, `INV`, `ROW-GATE`, 공통 | production 금지 count 0, sample·E2E source 삭제·임시 우회 0과 `<ID>` result |
| `V11-E2E-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 E2E 계약 | `framework/doc/framework/common/e2e/`, §14와 impact manifest의 approved scenario·registration hash | `DOC`, `TRACE --write`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required scenario·negative·race·matrix owner와 acceptance 누락 0 |
| `V11-SAMPLE-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 sample spec | `framework/doc/framework/common/sample/`과 impact manifest의 approved sample·registration hash | `DOC`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | 다섯 언어 sample의 public 흐름·marker·owner 누락 0 |
| `V11-R5D` | final E2E·sample spec candidate, finalized impact manifest, §18의 I1·I2·I3·I4·D1·D2; Codex `gpt-5.6-sol high` 단독 reviewer | source 수정 없음; Codex `gpt-5.6-sol high` 독립 finding과 수렴 evidence | §18 독립 review, `DOC`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `INV`, `DIFF-OWNED` | assertion 약화·coverage 손실·언어 parity gap 0, reviewer 결과와 `<ID>` result |
| `V11-M6A-E2E` | final E2E·sample spec, topology·liveness impact 항목 | topology fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice topology`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M6B-E2E` | final E2E·sample spec, stateful object impact 항목 | stateful fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice stateful`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M6C-E2E` | final E2E·sample spec, maintenance·hosting impact 항목 | maintenance·hosting fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice maintenance`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M7-CONTRACT` | amended common spec, 다섯 exact interface | 네 language contract suite와 public declaration comparison | 네 Framework command, `ROW-GATE`, 공통 | required contract skipped 0과 `<ID>` result 관련 blocked issue: `BLK-036`. |
| `V11-M7-RACE-CRASH` | §14.2와 공통 maintenance·concurrency resource internals | race·phase crash·pause·resource test와 seed manifest | `V11-E2E --slice full`, `ROW-GATE`, 공통 | 담당 row, seed·반복·결과와 `<ID>` result |
| `V11-M7-E2E-4X4` | final E2E spec과 approved impact manifest | full directional matrix result | `V11-E2E --slice full`, `E2E-CURRENT`, `ROW-GATE`, 공통 | 담당 row, required/skipped cell과 `<ID>` result |
| `V11-M7-SAMPLES` | final sample spec과 approved impact manifest | 다섯 언어 sample source·build·run evidence | `SAMPLES`, 언어별 Framework command, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` | §14 functional, §15 perf smoke-only | v11 smoke runner·result schema·provenance | 대응 `V11-SMOKE`; perf row는 `RAW-PERF-SMOKE`도 실행, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M7-JOIN` | M7 모든 candidate·evidence | correctness aggregate manifest | `ROW-GATE`, `DOC`, `INV`, `WIRE`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M8-INVENTORY` | machine inventory와 M7 load·coverage evidence | Framework·common cleanup inventory | Framework·common `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M8-CLEAN-CPP`, `V11-M8-CLEAN-DN`, `V11-M8-CLEAN-JVM`, `V11-M8-CLEAN-NODE` | POSD·DDD 원칙, 해당 runtime·test | 각 Framework source·test·sample·build·package metadata·clean-consumer verifier cleanup | 대응 Framework command·`PACKAGE-CONTRACT` self-test, `REMOVE --scope framework:<lang>`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M8-CLEAN-COMMON` | wire·fixture·E2E·CI inventory | protocol·fixture·runner·manifest·CI cleanup, review 대상 `scripts/local-package/framework/build-wsl.sh` | `WIRE`, 네 언어 `FW-PKG-TEST`, `REMOVE --scope common`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M8-CLEAN-JOIN` | M8 clean candidates | 네 clean build·contract와 aggregate removal result, migration inventory reconcile | 네 Framework command, 모든 Framework·common `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-RAW-FINAL` | approved Core·binding source와 package manifests | final raw·ASAN·removal·oracle isolation evidence, migration inventory reconcile | `CORE`, `CORE-ASAN`, Core·binding `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` | `V11-R6` Framework source, approved Core·binding packages와 review된 `FW-PKG` tooling | source·tooling 수정 없음; persistent final local/internal Framework package와 clean consumer | 대응 `FW-PKG`, Framework command, `PACKAGE-CONTRACT`, `ROW-GATE`, 공통 | 각 담당 row, approved revision·artifact SHA-256과 각 `<ID>` result |
| `V11-M9-E2E-4X4` | final package manifests와 §14 | final full directional matrix | `V11-E2E --slice full`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-SMOKE-FUNCTIONAL`, `V11-M9-SMOKE-PERF` | final packages, §14·§15 | final functional·perf smoke-only result | 대응 `V11-SMOKE`; perf row는 `RAW-PERF-SMOKE`도 실행, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M9-DOCS` | final public API·source·test·package | 정식 spec·guide·internals·migration·package 문서 | `DOC`, `INV`, `WIRE`, `DIFF-OWNED`, `ROW-GATE` | 담당 row와 `<ID>` result |

## 4. 전체 실행 순서

```text
SPEC-01..06 -> V11-R1 -> M2 oracle/readiness
            -> M3 Core removal/review/package
            -> M4 bindings removal/review/package
            -> M5 private binding port/foundation/review
            -> contract amendment/spec/interfaces/protocol/impact/review
            -> temporary proposal retirement
            -> generic Store spec -> five exact interfaces
            -> .NET Store reference -> regression -> Codex high POSD review
            -> C++/Java/Kotlin/Node mirror -> cross-review -> isomorphic convergence
            -> Store proposal retirement
            -> E2E/sample execution quarantine
            -> M6A topology runtime/review
            -> M6B stateful runtime/review
            -> M6C maintenance runtime/review
            -> M6 production placeholder zero
            -> final E2E/sample spec -> gpt-5.6-sol high independent review
            -> topology E2E -> stateful E2E -> maintenance E2E
            -> M7 full E2E/race -> samples -> correctness/smoke
            -> M8 Framework cleanup/review
            -> M9 final package/re-proof/E2E/smoke/docs/review
```

정식 spec과 exact interface를 확정하기 전에 구현을 시작하지 않는다. 제거 전 Core service 구현과 test는
별도 snapshot에 이미 보존되어 있으며, 이 보존본을 상태 기계, queue, wire encoding, ordering과 오류 처리의
읽기 전용 구현 기준으로 사용한다. Framework runtime을 처음부터 각각 새로 설계하지 않고 계약이 유지된
component와 algorithm을 각 언어 runtime에 포팅한다. 11.0에서 변경한 의미만 정식 spec·internals·schema를
기준으로 바꾼다. 보존 snapshot은 Core 11에 다시 포함하거나 runtime dependency로 유지하지 않는다. 실제
migration은 removal-first 순서로 진행한다. 동작 결과는 별도 Core 10 process의 normalized trace와 대조한다.
새 Core·bindings·Framework candidate는 oracle artifact를 compile, link 또는 load하지 않는다.

Core와 bindings는 각각 제거·POSD·DDD review가 끝난 뒤에만 version을 올리고 local/internal package를 만든다.
그 package를 입력으로 네 Framework runtime의 private binding-facing port와 service runtime을 같은 계약
snapshot에서 병렬 구현한다. 완료한 초기 SPEC·M5 evidence는 당시 snapshot의 이력으로 유지하고 다시 열지
않는다. M5 뒤 contract amendment가 변경된 정식 spec, exact interface, protocol과 영향 목록을 새 snapshot으로
고정하고 독립 review를 통과한 뒤 M6를 시작한다. M6는 internal contract와 독립 review에서 합류하며
topology·stateful·maintenance runtime과 production placeholder 제거가 끝날 때까지 E2E·sample 실행을 격리한다.
그 뒤 E2E·sample spec을 최종 확정하고 E2E를 작은 묶음부터 활성화한다. 전체 E2E와 race·crash가 통과한 뒤
sample을 실행한다.

## 5. SPEC — 구현 전 계약 확정

SPEC은 모든 구현 stage보다 우선한다. `V11-R1`이 완료되기 전에는 public API, runtime source, Core·binding
제거와 package version 변경을 수행하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `SPEC-01` | 전수 contract·source·test·package inventory와 10.x disposition | coordinator, `P-SCAN` | 없음 | 완료 | Core service 의미, 네 bindings projection, 다섯 Framework public member, test·sample·package·perf 항목의 누락·미분류 0 | `INV` exit 0: 4,331 records(Core public 523, export 98, binding declaration 1,020, semantic unit 100, exact Core-symbol reference 1,009, file 1,581). Core 문서 75개는 `Remove` 6·`Rewrite` 46·`Retain` 23이며 document 2·raw rewrite 1·file routing 10·public header 2의 negative mutation 15건이 모두 검출됐다. `DOC` exit 0에서 exact 문서 56개·package declaration owner 1,575개와 inventory drift 0을 확인했다. |
| `SPEC-02` | Framework 공통 정식 spec | contract lane, `P-DEEP` | `SPEC-01` | 완료 | topology, messaging, Spot, Actor, Instance, STREAM, lifecycle, maintenance, 오류와 관측 의미 완결 | `framework/doc/framework/common/spec/`의 Framework formal 140개, target spec·internals 20개와 semantic owner 10개를 `DOC` exit 0으로 검증했다. Service public 의미와 Core raw 책임 경계의 미분류·끊어진 link는 0건이다. |
| `SPEC-03` | Core raw 정식 spec과 ZMP heartbeat 제거 계약 | Core contract lane, `P-DEEP` | `SPEC-01` | 완료 | Core raw 범위, service API와 heartbeat option·frame·engine timer 제거, 유지할 generic timer·monitor 확정 | 정본 `core/doc/spec/core/09-runtime-boundary.*`, `core/doc/internals/runtime-boundary.*`와 inventory를 `DOC`·`INV`·`git diff --check` exit 0으로 검증했다. ZMP heartbeat option·command·engine timer는 제거 대상이고 PGM SPM·generic timer·raw monitor는 보존 대상이다. |
| `SPEC-04` | 다섯 언어 exact interface | CPP·DN·JVM·NODE contract lanes, `P-DEEP` | `SPEC-02` | 완료 | C++, .NET, Java, Kotlin, Node.js public member parity 100%, Java·Kotlin 별도 ABI 확정 | Exact 문서 56개의 fence 178개를 package contract 160·application example 15·documentation support 3으로 분류하고 package declaration owner 1,575개와 canonical member 6,069개를 검증했다. C++ 10/33/345, .NET 18/41/311, Java 9/25/444, Kotlin 9/20/45, Node.js 10/41/430(문서/package fence/owner)이며 duplicate·unknown owner·forbidden exact surface는 0건이다. |
| `SPEC-05` | Service·maintenance protocol과 major invariants | protocol·architecture lanes, `P-DEEP` | `SPEC-02`, `SPEC-03` | 완료 | frame·field·version·error·상한, lifecycle·authority·ordering·recovery 불변 조건 승인 | `WIRE` exit 0: schema 37 command·132 type·4 flag·24 bound, durable fixture 3개, logical·JSON·authority-key fixture 각 1개, negative self-test 124개. Formal·target termination outcome·reason drift, stale generation과 semantic drift 0건을 `DOC`의 termination mutation 3건과 함께 확인했다. |
| `SPEC-06` | POSD API review와 contract·E2E·package 추적표 | architecture·E2E lanes, `P-DEEP` | `SPEC-02`, `SPEC-03`, `SPEC-04`, `SPEC-05` | 완료 | 비자명 API마다 대안 2개, caller 부담·정보 은닉 판단, 모든 계약의 test·package owner 존재 | POSD decision 60개와 canonical trace member 6,069개를 deterministic checker로 검증했다. Exact signature 1,945, canonical signature 238, reviewed override 230, intentional removal 912, unclassified·ambiguous·unknown owner 0, negative mutation 15건이다. Instance Spot·async submit verifier와 E2E `M01~M68`, liveness `L01~L06`, race `RACE01~RACE38` 112개가 각각 한 owner에 연결됐다. |
| `V11-R1` | SPEC 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `SPEC-01`, `SPEC-02`, `SPEC-03`, `SPEC-04`, `SPEC-05`, `SPEC-06` | 완료 | 두 독립 reviewer의 finding을 모두 반영하고 post-fix machine gate 통과 | Base `86258cb9a3ec`, candidate `.artifacts/v11/spec-review-candidate.json`은 repository path·mode·base/current SHA-256을 가진 255개 파일과 direct fixture 19개를 고정한다. 1회차 finding 16건과 2회차 고유 finding 5건을 모두 반영한 뒤 `DOC`, `INV`, `WIRE`, `TRACE`, `INSTANCE`, `SUBMIT`, candidate check와 `git diff --check`가 모두 exit 0이었다. 2026-07-21 사용자가 추가 review를 종료하고 구현 전 계약을 승인했으므로 3회차는 실행하지 않았다. Aggregate와 승인 기록은 candidate와 `.artifacts/v11/evidence/V11-R1/`이 소유하며 ledger 본문에 복제하지 않는다. |

### 5.1 무손실 이관 inventory

아래 범주는 `SPEC-01`의 machine inventory에서 symbol, public member, source, target와 package 입력 단위로
확장한다. 표에 없는 service 항목이 검색되면 먼저 이 표와 machine inventory에 추가한 뒤 처리한다.

| 현재 소유 영역 | 11.0 처리 | 구현·제거 gate |
|---|---|---|
| `core/include/zlink/service/{common,dispatch,mesh_node,spot,actor,stream_session,instance_spot_driver}.h` | Application이 관찰하는 의미는 Framework 정식 spec이 소유하고 구현 구조는 공통 service internals가 소유한다. C ABI type·token·함수는 제거 | `SPEC-01`, `SPEC-02`, `SPEC-03`, `V11-M3-CORE-REMOVE` |
| `core/include/zlink/eventing/api.h`의 Spot timer·Mesh monitor, `socket/api.h`의 ChannelName, `zlink_enum.h`의 MeshNode poller source, `zlink.h`, install header와 `libzlink.vers` | Generic timer·raw monitor·raw poller는 유지하고 service 결합 type·symbol·include만 제거 | `SPEC-03`, `V11-M3-CORE-REMOVE` |
| `core/src/api/mesh/`, `core/src/runtime/services/mesh/` | Protocol·mailbox·Actor·Spot·transfer·monitor 불변 조건은 정본과 oracle trace로 보존하고 source는 먼저 제거 | `SPEC-05`, `V11-M2-ORACLE`, `V11-M3-CORE-REMOVE` |
| `service_control_runtime`, Spot timer seam, ChannelName option·socket field와 service build 입력 | Raw 사용자에게 필요한 generic scheduler·timer·request 경로는 유지하고 service 전용 state와 target만 제거 | `SPEC-03`, `V11-M3-CORE-CLEAN` |
| Core Mesh·Spot·Actor·Instance·STREAM integration, unit, benchmark와 public-surface test | Service test·benchmark는 제거하고 raw 회귀만 Core 11 gate로 유지. Service 의미 검증은 이후 Framework contract·E2E가 소유 | `V11-M3-PERF-LEGACY`, `V11-M3-CORE-REMOVE`, `V11-M7-JOIN` |
| Core guide·internals·spec의 service 의미 | Explicit reviewed manifest에서 파일별 `Remove`·`Rewrite`·`Retain`을 고정한다. ZMP guide의 SPOT envelope는 Framework service wire owner로 옮기고 Core 문서는 raw ZMP만 설명한다 | `SPEC-01`, `SPEC-03`, `V11-M3-CORE-CLEAN`, `V11-M9-DOCS` |
| C++·.NET·Java·Node.js bindings의 service wrapper·generated symbol·test | Core 11 package를 기준으로 projection을 제거하고 일반 raw socket 사용자에게도 유효한 public capability만 보완 | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` |
| C, Python, Go와 Rust bindings | 10.x oracle 조합에 격리. Core 11 build·package·CI와 호환 표기에서 제외하고 새 candidate가 link·load하지 않음 | `V11-M2-BIND-READINESS`, `V11-M9-RAW-FINAL` |
| C++·.NET·Java·Kotlin·Node.js Framework public surface | 초기 exact interface evidence는 이력으로 유지한다. M5 이후 amendment에서 global identity·remote placement 계약과 다섯 언어 표현을 다시 고정하며 Java·Kotlin은 JVM runtime 하나를 공유 | `SPEC-04`, `V11-CA-SPEC`, `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE`, `V11-M6-SCAFFOLD-ZERO` |
| Sample, common E2E, race·crash test와 hosting integration | Amendment impact manifest에서 `retain`·`amend`·`replace`·`add`·`remove`와 대체 coverage를 먼저 확정한다. Runtime 중에는 source·registration을 보존하고 실행만 격리하며, runtime 완료 뒤 spec을 확정하고 topology→stateful→maintenance→full matrix→sample 순서로 활성화한다 | `V11-CA-IMPACT`, `V11-E2E-SPEC-FINAL`, `V11-SAMPLE-SPEC-FINAL`, `V11-M6A-E2E`, `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-M7-SAMPLES`, `V11-M7-JOIN`, `V11-M9-E2E-4X4` |
| `bindings/c/perf`의 active Spot suite | 10.x oracle trace를 봉인한 뒤 active Spot source·target·pattern·parser·CI 입력을 제거. Raw socket perf와 읽기 전용 10.x archive는 유지 | `V11-M2-ORACLE`, `V11-M3-PERF-LEGACY`, §15 |
| Core·binding·Framework 문서와 package metadata | 정식 spec·common internals를 구현에 맞춰 검증하고 제거한 service API 링크·version·package 입력을 정리 | `V11-M9-DOCS`, `V11-R7` |

Core 제거 inventory는 최소한 `zlink_mesh_node_*`, ready·receive batch와 claim·reply, `zlink_spot_*`, Actor,
Instance Spot, STREAM session, Mesh monitor와 Spot timer symbol family를 각각 펼쳐 기록한다. Prefix 하나를 완료
증거로 사용하지 않으며 구조체 field, enum 값, callback, exported symbol과 generated binding member도 독립 행을
가진다.

Core 문서 inventory는 blanket regex 결과를 곧바로 처리 결정으로 사용하지 않는다. Review한 exact path 75개와
각 target owner를 manifest에 기록하고, regex는 manifest 밖의 새 service 문서를 찾는 음성 검증에만 사용한다.
Allowlist에서 기존 ZMP guide를 제거하는 변이와 새 service guide를 추가하는 변이가 모두 실패해야
`SPEC-01`의 문서 누락·미분류 0을 인정한다.

초기 v11 입력에서 확인한 의미는 아래 표의 현재 소유 문서가 담당한다. 이전 입력 파일이나 문서 hash는 보존
조건이 아니다. 표에 적힌 계약, 실행 gate와 machine inventory 항목이 현재 소유 문서에서 확인되는지를 기준으로
판단한다.

| 대조한 입력 | 고유하게 확인할 내용 | 현재 단일 소유자 |
|---|---|---|
| 통합 구현 계획 | 언어별 runtime 경계, stage 선행 조건, package·rollback·cleanup gate | 이 ledger의 §1·§4·§6~§19, [v11 README](README.ko.md) |
| Core service 이관 inventory | Core spec 절·공개 symbol·binding projection·dirty 변경의 처리, raw capability proof | `route-mesh-v11-core-service-migration-inventory.json`, 정식 Framework spec·common internals owner, `SPEC-01`, `V11-M2-CORE-READINESS`, `V11-M2-BIND-READINESS` |
| Core socket heartbeat 계획 | ZMP heartbeat option·frame·engine timer·binding projection 제거와 Framework internal liveness command·lease·transport monitor의 분리 | Core raw 정식 spec·internals, §5.5, `V11-M3-CORE-VERIFY`, `V11-M5-PROTOCOL`, `V11-M6A-E2E`, `V11-M7-E2E-4X4`, `V11-M9-RAW-FINAL` |
| Service 성능 test 이관 계획 | 세 Framework smoke workload, fail-closed provenance, Core 10.x archive와 active suite 분리, 정량 성능 비목표 | `V11-M2-ORACLE`, `V11-M3-PERF-LEGACY`, §12의 `V11-M7-SMOKE-PERF`, §15 |
| Stateful rolling maintenance 초안 | Host 상태·종료 결과, preflight, authority CAS, transfer, transfer·recovery·object별 규칙 | [Location runtime](../../framework/common/spec/21-location-runtime.ko.md), [Actor](../../framework/common/spec/14-actor-model.ko.md), [Spot Actor](../../framework/common/spec/15-spot-actor.ko.md), [STREAM](../../framework/common/spec/20-session-actor-dispatch.ko.md), [stateful maintenance internals](../../framework/common/internals/stateful-maintenance-runtime.ko.md), §11·§14 |
| 통합 public-contract 문서 묶음 | 다섯 언어 public member 전수 범위, 의도적 delta 분류, public raw dependency와 package 검증 | 언어별 `interfaces/`, 아래 public member trace, `SPEC-04`, `SPEC-06`, `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE`, `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` |
| Stateful maintenance public-contract 문서 묶음 | `Retire`·`Shutdown`, typed transfer policy, authority·transfer와 언어별 비동기 표현 | [Location runtime](../../framework/common/spec/21-location-runtime.ko.md), [Host maintenance](../../framework/common/spec/28-graceful-drain-handoff.ko.md), 다섯 언어 exact `configuration-host`·`location-maintenance`·`monitoring`, `SPEC-04` |

진행 문서는 이 ledger 하나만 유지한다. Framework spec, common internals와 언어별 exact interface는 각각
공개 계약, 내부 구조와 signature를 소유하며 진행 상태와 migration 이력을 포함하지 않는다.

### 5.2 SPEC-04 exact interface 감사 증거

다섯 언어의 exact interface는 기능별 `interfaces/` 문서가 소유한다. 2026-07-21 문서 검증은 56개 문서의
fence 178개를 package contract 160개, application example 15개와 documentation support 3개로 분류했다.
Package declaration owner 1,575개를 확인했으며 code fence, 상대 link, 중복 package owner와 금지 public 표면은
모두 0건이다. Application example의 public type을 package owner로 분류하는 변이는 검증에 실패한다.

| 언어 | 확정한 경계 | 문서 검증 | 구현 단계에 남은 gate |
|---|---|---|---|
| C++ | ChannelName-only, role builder, manual·automatic classic fanout, opaque authority CAS, Retire·Shutdown | exact 10개 문서·package fence 33개·owner 345개 검증 통과 | install-tree header, clean consumer와 package ABI 비교 |
| .NET | generic Actor lifecycle, actor-free Instance, LocationStore authority capability, async-only submit | exact 18개 문서·package fence 41개·owner 311개 검증 통과 | Instance Spot target member 미구현으로 발생하는 source build 오류 해결 |
| Java | JVM runtime의 Java 정본, role builder, factory policy, opaque authority CAS | exact 9개 문서·package fence 25개·owner 444개 검증 통과 | binding/source 불일치 해결 뒤 fresh JAR ABI 비교 |
| Kotlin | Java runtime 재사용, coroutine extension과 Java member 충돌 회피 | exact 9개 문서·package fence 20개·owner 45개 검증 통과 | Java blocker 해결 뒤 Kotlin artifact와 compiler bridge 재감사 |
| Node.js | ChannelName-only, role builder, event-loop 비동기 submit, opaque authority CAS | exact 10개 문서·package fence 41개·owner 430개 검증 통과 | packed artifact export 재감사 |

다음 표면은 다섯 언어 target에서 제거됐다.

- Channel call의 MeshName+ChannelName overload와 `ChannelName(...)` alias
- Server 역할을 선택하기 전 weight·handler 설정
- Actor·Instance phase별 Store와 factory policy와 분리된 transfer registry
- 공개 `TrySubmit`과 MeshNode drain policy

마지막 항목은 MeshNode별 policy enum과 partial-drain 설정을 뜻한다. 언어 exact interface가 기존 사용자를 위해
host 전체 `Shutdown`에 위임하는 deprecated `Drain` 이름을 유지하는 것은 별도 host-wide compatibility 표면이며,
MeshNode drain policy가 남은 것으로 세지 않는다. 이 facade는 새 outcome, reason이나 component별 drain
authority를 만들 수 없다.

Exact 문서 검증은 runtime 구현 완료 증거가 아니다. Fresh source·package가 아직 target signature를 제공하지
않으면 해당 언어 구현 lane의 gap으로 남긴다. 마지막 성공 artifact의 hash나 이전 문서 aggregate hash를 현재
계약의 승인 근거로 사용하지 않고, 바뀐 declaration과 직접 영향받는 artifact를 다시 비교한다.

`SPEC-06`은 exact 문서의 각 public declaration owner를 member 단위 trace에 연결한다. 파일별 type 수만 세거나
대표 예제로 대신하지 않는다. Constructor, overload, generic bound, callback, enum value, extension·decorator,
DI token과 package export도 각각 추적한다. 각 행은 다음 정보를 가져야 한다.

- 언어, package와 fully qualified member identity
- 공통 정식 spec과 언어 exact interface 주소
- `verified-baseline`, `v11-first-implementation`, `intentional-removal` 중 하나의 분류
- 비교한 interface 대안 두 개 이상과 caller 부담·정보 은닉을 기준으로 한 선택 근거
- contract test, 공통 E2E scenario와 package·clean-consumer owner
- 검증한 source·package candidate와 구현 차이를 소유하는 ledger ID

Fully qualified identity는 machine trace에서 member를 구분하는 key다. 언어별 exact interface의 source signature는
이름 충돌이 없으면 import와 짧은 type 이름을 사용하는 등 해당 언어의 일반적인 표기를 유지한다.

`verified-baseline`만 이전 backend와 shadow 결과를 비교한다. `v11-first-implementation`은 정식 spec,
contract test와 E2E로 처음 검증하며 존재하지 않는 이전 구현과 동등하다고 기록하지 않는다. Runtime 이관만으로
생긴 public rename·removal·wrapper addition은 `intentional-removal`의 정식 계약 근거가 없으면 blocker다.

Member trace는 다음 공개 범주를 빠짐없이 포함한다.

| 범주 | 계약 owner | 구현·package owner |
|---|---|---|
| Host lifecycle, bootstrap와 hosting | `05-framework-api`, target `01`·`07`·`08`, 언어별 `configuration-host`·`monitoring` | `V11-M5-FOUND-JOIN`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` |
| Topology, discovery와 RID allocation | target `01`·`07`, 언어별 topology·location·allocation | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE`, `V11-M6A-E2E`, `V11-M7-E2E-4X4` |
| Messaging, handler와 execution context | target `02`, 언어별 common-runtime·channel-messaging | `V11-M5-FOUND-JOIN`, `V11-M6A-E2E`, `V11-M7-E2E-4X4` |
| Spot와 Instance Spot | target `03`·`06`, 언어별 spots | `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE`, `V11-M6B-E2E`, `V11-M7-E2E-4X4` |
| Actor와 transfer | target `04`·`07`, 언어별 actors·location-maintenance | `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| STREAM과 bound session | target `05`, 언어별 stream-session | `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| Location authority, transfer와 target eligibility | target `07`, 언어별 location-maintenance | `V11-M6A-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| Observability, diagnostics와 error | target `08`, 언어별 monitoring | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |

### 5.3 SPEC에서 고정할 major invariants

- lifecycle 상태 writer는 각 process의 해당 언어 Framework runtime 하나다.
- `Retire`는 preflight 뒤 continuity transfer를 수행하며, `Shutdown`은 새 transfer를 시작하지 않고 bounded
  terminal 결과로 완료된다.
- Preflight가 실패하면 admission과 node 상태를 변경하지 않는다.
- Preflight는 target eligibility와 bounded headroom만 확인한다. Reversible admission seal 뒤 exact accepted
  inventory를 만들고 target reservation ACK와 모든 object의 `Prepared` CAS를 완료한 뒤에만 `Draining`을
  게시한다.
- Admission seal 전에 수락된 work, STREAM barrier와 timer ordering은 terminal cleanup 전에 처리한다.
- Location Store는 TTL 없는 durable owner·phase·generation authority를 CAS로 갱신한다. Process owner lease와
  Transfer Store의 leased opaque state는 authority row 수명과 분리한다.
- Owner lease를 사용하는 host는 routing ID 자동 할당 여부와 무관하게
  `renew interval + renew timeout < TTL - owner lease fencing margin`을 startup에서 검증한다. Store failure grace는
  discovery reconcile만 유예하며 owner·coordinator lease와 local monotonic admission deadline을 연장하지 않는다.
- Authority row는 1 MiB 이하 compact metadata와 transfer root reference만 보유한다. Journal, reply payload와
  full terminal completion은 chunked transfer stream이 한 번만 소유하며 scan page는 encoded byte 상한을 가진다.
- Stale owner, transfer token, generation, reply와 timer tick은 새 owner admission을 통과하지 못한다.
- Commit 전후 crash, orphan transfer와 committed target replacement가 하나의 terminal transaction으로
  수렴한다.
- Stable transfer ID는 transfer·journal·replay·terminal identity를 유지하고 target attempt generation만
  replacement reservation마다 증가한다.
- Transfer phase는 main authority owner와 target field의 closed table을 따른다. `Preparing`·`Captured`의 main
  owner는 immutable source이고 target field는 없으며, `Prepared`는 source owner와 exact target
  token·attempt·reservation·transfer를 갖는다. `Prepared`에서 `Committed`로 바뀌는 단일 owner CAS가 main
  owner를 target으로 변경하고 이후 `Completed`까지 같은 current target fence를 유지한다.
- `Activated` target은 restore·replay와 route staging만 끝낸 상태이며 application·session admission은
  `Completed` CAS까지 sealed다. Source cleanup terminal 뒤 `Completed`가 된 경우에만 ready와 ingress를 열고
  transfer를 해제한다. 이후 owner loss는 종료된 transfer transfer를 replay하지 않는다.
- Cancellation은 waiter만 중단하며 이미 시작한 lifecycle·transfer operation을 되돌리지 않는다.
- Request의 reply·timeout·cancellation·shutdown 경쟁은 terminal completion 하나만 만든다.
- Transfer journal의 request reply는 authenticated request-source ACK 또는 accepted record에 고정한 exact
  request-source owner lease의 만료가 확인될 때까지 durable하다. Physical connection 종료나 caller timeout만으로
  terminal 처리하지 않으며 그 전에는 `Completed`와 transfer release를 허용하지 않는다.
- Terminal completion 추가와 ACK·exact request-source lease-expiry 전이는 새 immutable transfer root를
  만든다. Expected authority store version CAS 한 번으로 root·checksum·terminal completion count·pending relay
  count를 함께 교체하며, authority count와 참조 transfer vector가 다르면 recovery error로 처리하고
  `Completed`를 금지한다.
- Target replacement가 발생하면 같은 stable transfer의 target factory와 Snapshot restore는 attempt마다 한 번 이상
  호출될 수 있고 stale attempt와 겹칠 수 있다. Current exact owner와 target attempt만 completion commit과
  application admission을 열 수 있으며 public callback에 internal transfer ID를 노출하지 않는다.
- Transfer journal, accepted backlog, queue와 pending operation은 정식 상한을 가진다.
- Protocol 상수와 wire 값은 schema·생성물 밖에서 수동으로 복제하지 않는다.

### 5.4 Core 포팅 입력과 v11 protocol delta

각 기능 lane은 구현을 시작하기 전에 보존된 Core service 구현·test snapshot의 관련 component, type,
state machine, algorithm과 failure case를 inventory record에 연결한다. 새 Framework runtime을 독립적인
greenfield 구현으로 만들지 않고 기존 구현을 해당 언어의 memory·concurrency model과 public raw binding 경계에
맞게 포팅한다. 보존된 구현과 정식 spec이 다르면 spec이 우선하고 차이를 해당 lane evidence에 기록한다. 한
언어 구현을 새 기준으로 삼아 나머지 언어가 순차적으로 따라가지 않고, 네 runtime이 같은 Core reference와
계약 snapshot에서 병렬로 포팅한다.

현재 Core wire에서 그대로 보존하는 값은 command ID `1..4`, `16..39`, magic `ZM`, wire major `1`과 flag
`0x01`·`0x02`·`0x04`다. 다음 항목은 현재 Core 구현 완료 증거가 아니라 v11 runtime에서 새로 구현하고
fixture로 검증할 delta다.

- `service-wire-v1.schema.json` 한 곳에서 네 언어 protocol 상수와 codec table을 생성한다.
- Framework service liveness에 `livenessProbe=5`, `livenessAck=6`을 추가한다. Probe ID는 connection lifetime
  안에서 0이 아닌 값이며 ack는 같은 ID를 그대로 반환한다.
- Stateful maintenance reply relay에는 `replyRelayAck=46`을 추가한다. Stable transfer ID·operation ID, accepted
  record에 고정한 exact request-source owner·lease·node fence와 terminal status만 전달하며 application payload나
  metadata를 싣지 않는다. Physical connection identity는 인증된 transport context일 뿐 terminal 증거가 아니다.
- Required capability `framework-service-v11`과 descriptor extension flag `0x08`을 추가한다.
- Runtime state, application version, type·state capability, capacity와 maintenance wave를 descriptor TLV로
  검증한다.
- Endpoint 상한을 목표 schema의 4,096 byte로 통일하고 allocation 전에 모든 count·length·UTF-8을 검증한다.
- Command가 허용하지 않는 flag, unknown bit, duplicate TLV, trailing byte와 malformed payload를 공통 protocol
  error로 거부한다.
- Instance activation은 Location Store authority snapshot과 local monotonic admission deadline을 message, timer,
  factory completion과 phase update에 적용한다.
- Reply public operation은 첫 terminator가 한 번 소비하고, runtime 내부 admission operation이 send-ready,
  timeout·shutdown과 source terminal failure를 끝까지 소유한다.
- Actor·Instance phase별 provider method는 제거하고 Location Store의 opaque expected-version CAS가 owner와
  transfer phase를 같은 payload로 갱신한다.

이 delta는 `SPEC-05` 승인, schema·golden·normalized trace와 네 decoder의 negative fixture가 통과하기 전에는
Core 동작을 그대로 포팅했다는 근거로 사용할 수 없다.

### 5.5 ZMP heartbeat 제거와 service liveness 규칙

`SPEC-03`은 Core의 `ZLINK_OPT_HEARTBEAT_IVL`, `ZLINK_OPT_HEARTBEAT_TTL`,
`ZLINK_OPT_HEARTBEAT_TIMEOUT`, ZMP `HEARTBEAT`·`HEARTBEAT_ACK` frame, TCP·WebSocket engine timer와 관련
test를 제거 대상으로 고정한다. C++·.NET·Java·Node bindings도 같은 option projection을 M4에서 제거한다.
PGM transport 자체의 `PGM_HEARTBEAT_SPM`은 ZMP socket heartbeat와 다른 protocol 설정이므로
Core raw transport에 명시적으로 `Retain`한다.

C·Python·Go·Rust의 heartbeat projection은 Core 11 이관 대상이 아니다. Core 10.x 전용 조합에
`Retain/OutOfScopeV11`로 격리하고 Core 11 package·CI·compatibility metadata에서만 제외한다.

Framework runtime은 Core heartbeat option이나 제거한 frame을 호출하지 않는다. Orderly close, FIN,
RST와 local transport failure는 public raw monitor event를 관측한 즉시 반영한다. 이 전환에 intentional delay를
두지 않으며 E2E의 5초는 상태 전환 지연이 아니라 process·monitor 결과를 확인하는 test observation
budget이다.

RouteMesh와 ClientServer의 admitted bidirectional connection에서 transport event가 없는 half-open 상태는
Framework service wire의 `livenessProbe=5`·`livenessAck=6`과 monotonic deadline으로 판정한다. Application
traffic과 관계없이 5초마다 0이 아닌 connection-local probe ID를 보내고, ack는 같은 ID를 그대로 반환한다.
Current connection에서 보낸 probe의 matching ACK가 15초 동안 없으면 다른 inbound application frame이 계속
도착해도 해당 connection을 not-ready로 전환하고 닫는다. 다른 valid inbound frame은 diagnostics를 갱신할 수
있지만 round-trip deadline을 연장하지 않는다.

Manual·automatic classic fanout은 raw PUB/SUB 단방향이므로 ACK를 보내지 않는다. Subscriber는 automatic
publisher descriptor 또는 manual publisher endpoint별로 전용 SUB socket 하나를 사용해 failure를 publisher에
귀속시킨다. Publisher는 application traffic과 관계없이 5초마다 schema가 예약한 one-way
fanout liveness beacon을 보낸다. Subscriber는 유효한 application frame과 beacon을 모두 receive activity로
계산하고 15초 동안 둘 다 받지 못하면 해당 publisher만 not-ready로 전환해 disconnect·reconnect한다.
Beacon은 application queue, topic filter의 사용자 payload와 handler에 전달하지 않는다.

Beacon은 `topicFrameBytes=01 5A 4C 46 31`, `reservedTopicMatch=exact-only`,
`payloadFrameBytes=5A 46 01 01`, `multipartFrameCount=2`로 고정한다. Reserved topic과 일치하지만 payload나
2-frame 구조가 잘못된 record는 `malformedReservedTopic=protocol-error-immediate-not-ready`로 처리한다.
Subscriber는 application topic filter와 별도로 exact reserved beacon topic을 항상 구독한다. Readiness는 첫
valid application frame 또는 valid beacon을 수신한 뒤에만 성립한다.
Public fanout publish는 exact reserved topic만 호출 인자 오류로 거부하고 같은 prefix 뒤에 byte가 하나 이상
추가된 topic은 일반 application topic으로 허용한다. `V11-M5-PROTOCOL` codec negative fixture는 exact topic의
잘못된 payload와 extra frame을 각각 거부한다. `V11-M6A-E2E`는 Config 3 `PS-F1~F5`와 Config 5
`RL-E1~E5`를 네 runtime에서 실행한다.

이 값과 command를 application public option으로 노출하지 않는다. Probe·ACK·fanout beacon의 exact wire
계약은 `service-wire-v1.schema.json`이 정본이며 M5에서 golden·negative fixture와 네 codec을 만든다.

Location owner lease는 distributed authority와 process-pause fencing만 판정한다. Service liveness probe, owner lease,
STREAM session heartbeat, request timeout과 reconnect deadline을 서로 대체하지 않는다. Terminal cleanup은
`livenessProbe` scheduler, reconnect timer와 monitor subscription을 connection보다 늦게 남기지 않는다.

### 5.6 POSD 공개 경계 결정

비자명한 공개 경계는 최소 두 대안을 비교하고 caller가 알아야 하는 상태와 순서를 더 적게 만드는 쪽을
선택했다.

| 결정 | 비교한 대안 | 선택과 이유 |
|---|---|---|
| Service runtime 배치 | 공통 native C runtime / 언어별 runtime | 언어별 runtime. Task·coroutine·Promise와 DI lifecycle을 언어 안에 감추고 Framework 전용 C ABI를 만들지 않음 |
| Connection liveness 소유 | Core ZMP heartbeat 유지 / Core heartbeat 제거와 Framework topology별 liveness | 후자. Raw socket protocol에서 service failure 판단을 제거하고 RouteMesh·ClientServer probe·ACK와 단방향 fanout beacon의 차이를 Framework 내부에 감춤 |
| Manual peer lifecycle | Caller가 persistent generation을 설정 / runtime opaque lifecycle token과 current connection handover | 후자. Store가 없는 배포에서도 caller storage를 요구하지 않고 token equality와 physical connection lifetime으로 stale event를 fence하며 숫자 대소를 해석하지 않음 |
| Fanout publisher failure 귀속 | Shared SUB와 publisher identity 공개 / publisher별 dedicated SUB | 후자. Publisher 식별·timeout·reconnect를 runtime 내부 socket 수명으로 해결하고 caller에게 transport identity와 분기 책임을 노출하지 않음 |
| Handler topology context | 모든 handler에 MeshName / direct node에만 MeshName과 source node RID | Direct node context에만 두 필드를 둠. Channel·Spot handler가 물리 mesh를 알 필요가 없음 |
| Channel target 선택 | MeshName+ChannelName / process-local unique ChannelName | ChannelName 하나. 물리 topology lookup을 runtime에 감추고 direct RID만 MeshName을 요구함 |
| Actor relocation 시간 설정 | Actor 전용 timeout과 Message Follow duration / Message Follow duration과 host deadline | Message Follow duration 하나만 Framework option으로 제공한다. 기본값은 30초이고 0은 전달을 끈다. Relocation 전체 상한은 host deadline이 소유한다 |
| Channel builder | 역할 전 공용 설정 / `Client()`·`Server()` 뒤 역할별 설정 | 역할별 builder. Client에 weight·handler 같은 무효 상태를 표현하지 않음 |
| Owner authority Store | phase별 provider method / opaque expected-version CAS | Location provider의 CAS 하나. Framework 상태 기계를 provider마다 복제하지 않음 |
| Instance activation 공개 경계 | target·token과 begin/commit/close lifecycle 공개 / global Spot ID fluent call과 factory 등록만 공개 | 후자. Owner claim·fencing·activation barrier는 언어별 runtime이 소유하고 caller는 owner ID, generation, epoch와 native token을 전달하지 않음 |
| Transfer policy | 별도 adapter registry / factory-attached policy와 adapter | Factory-attached `Disabled`·`Recreate`·`Snapshot`. Snapshot adapter는 application-owned opaque bytes만 처리하고 format·version을 Framework에 노출하지 않음 |
| Transfer 시작 | Object별 explicit operation / host `Retire`만 사용 | Host `Retire`만 사용. Application이 target·phase·retry를 조합하는 public state machine을 만들지 않음 |
| Host 종료 | Drain variant 확장 / `Retire`·`Shutdown` | 두 operation을 정본으로 사용. Legacy host Drain은 별도 의미 없는 deprecated `Shutdown` facade만 허용 |
| One-way submit | `TrySubmit`과 async submit / async submit 하나 | Async submit 하나. Cold resolve와 bounded admission을 caller에게 노출하지 않음 |
| Spot lifecycle 구성 | 모든 Spot에 lifecycle callback 8개를 한 interface로 노출 / base·Actor·Entry capability와 actor-free Instance interface 분리 | 후자. Instance Spot은 `Configure`·initialize·closing만 제공하고 Actor callback을 알 필요가 없다. User·Entry Spot도 실제 역할의 interface만 조합하므로 무효 callback 구현을 요구하지 않음 |
| Remote maintenance | 공통 고정 HTTP route / hosting·application endpoint | 공통 route 제외. 인증·인가와 deployment targeting을 hosting 경계에 유지함 |
| Transfer Store 구성 | Location Store에 payload 통합 / 별도 Location·Transfer capability | 후자. Transfer Store는 immutable payload를 먼저 준비하고 Location Store의 reference CAS가 visibility와 aggregate commit을 결정한다. 같은 Redis deployment를 공유할 수 있지만 cross-store transaction을 요구하지 않음 |
| Core raw 문서 | v11 target과 Core formal 중복 / Core formal 단일 정본 | Core formal 단일 정본. v11의 09 파일은 위치만 가리킴 |
| Actor reference | Actor type과 owner fence를 reference에 포함 / Actor ID·node RID·object generation만 공개 | 후자. Type은 factory·authority가 검증하고 owner generation·lease는 runtime이 숨김 |
| Actor 생성 | Caller가 target node를 선택 / global Actor ID·stable type과 optional initial Mesh intent | 후자. Runtime이 compatible target을 선택하고 manager `Find`는 existing-only 조회만 제공하며 caller에게 physical owner 선택을 노출하지 않음 |
| Host owner lease | Component마다 lease를 claim / host lifecycle lease 하나와 component별 routing slot | 후자. 한 process의 liveness token을 중복하지 않고 각 component RID 수명은 slot으로 분리 |
| Owner lease margin | Routing ID allocation 전용 margin / 모든 owner authority에 적용하는 owner lease margin | 후자. `OwnerLeaseFencingMargin` 하나를 owner lease를 사용하는 모든 host에서 검증하고 stale owner의 local monotonic admission deadline을 같은 의미로 계산함 |
| Store failure grace | Owner lease까지 연장 / discovery reconcile과 신규 outbound connect만 유예 | 후자. 마지막 stable desired set과 기존 transport만 유지하고 stateful owner·coordinator deadline은 연장하지 않음 |
| Authority generation | Runtime별·key별 counter와 tombstone / provider-domain global counter와 opaque Store version | 후자. Object·owner·Store revision을 CAS 하나에서 발급하고 key별 영구 metadata를 남기지 않음 |
| Authority generation 이름 | `OwnerGeneration` 하나로 표현 / object·authority owner·host lease generation을 이름으로 구분 | 후자. `ObjectGeneration`, `AuthorityOwnerGeneration`, `LeaseGeneration`을 분리해 다른 수명의 fence를 호출자가 혼동하지 않게 함 |
| Owner cleanup | Owner ID string으로 bulk delete / exact owner token으로 ephemeral descriptor만 정리 | 후자. 같은 owner ID의 새 lease와 durable authority를 오래된 cleanup이 삭제하지 못함 |
| Store enumeration | Provider 전체 snapshot / bounded page와 stable scope change stamp | 후자. Caller에게 paging 재시도 상태를 노출하지 않고 runtime이 안정된 full snapshot에서만 diff를 적용함. Owner lease 전체 list는 제거하고 exact read를 사용 |
| Authority scan cursor | Public scan ID·watermark·cursor 조합 / provider-issued opaque cursor 하나 | 후자. Framework는 4,096-byte 이하 token을 해석·조합하지 않고 다음 page에 그대로 돌려주며 provider가 snapshot lease와 watermark를 소유함 |
| Provider cancellation | Cancellation을 no-commit으로 해석 / invocation 뒤 exact read·fence로 reconcile | 후자. Waiter 종료와 durable commit을 분리해 duplicate generation transition과 transfer loss를 막음 |
| Provider byte ownership | Mutable buffer lifetime을 호출자마다 추측 / operation 단위 immutable input과 stable result | 후자. Framework는 input을 async 완료까지 유지하고 provider는 retain·mutable pool 경계에서 copy해 application에 buffer lifetime을 노출하지 않음 |
| Generation exhaustion | Provider exception으로 불명확하게 보고 / authority write의 closed terminal result | 후자. `GenerationExhausted`가 row·index·counter를 바꾸지 않았음을 공통으로 보장하고 transport error와 분리함 |
| Typed JSON interop | 언어 serializer 기본값과 byte-identical 재인코딩 / `framework-json-v1` 의미 profile과 opaque application bytes | 후자. DTO 의미만 공통으로 고정하고 canonical bytes는 Framework manifest·envelope에만 요구 |
| Message size | 숨은 고정 16 MiB / connection startup에서 negotiated complete-message bound | 후자. 양쪽 상한의 작은 값을 allocation 전에 적용하고 live setter를 만들지 않음 |
| Transfer 크기 | Whole payload 64 MiB / 64 MiB immutable chunk와 root manifest | 후자. Public transfer option을 늘리지 않고 bounded streaming replay와 256 GiB logical ceiling을 runtime이 소유 |
| Recovery enumeration | Redis `SCAN` 또는 전체 copy / leased snapshot watermark와 bounded MVCC page | 후자. Startup Serving gate와 concurrent create·delete 의미를 provider 안에 감춤 |
| Transfer identity | Replacement마다 transfer transaction을 복제 / stable transfer ID와 target attempt generation 분리 | 후자. Immutable transfer·journal·terminal key는 유지하고 reservation attempt만 fence함 |
| Transfer 공개 경계 | `Activated`에서 target ingress 공개 / source cleanup과 `Completed` 뒤 steady normalize·Ready | 후자. Replacement transfer에 없는 post-activation work를 만들지 않아 data loss window를 제거 |
| Reply durability | One-way reply relay 또는 별도 장기 tombstone / internal relay ACK를 completion barrier에 포함 | 후자. Public 상태를 추가하지 않고 authenticated ACK 또는 accepted record의 exact request-source lease expiry 전 transfer를 해제하지 않음 |
| Journal source authority | 모든 accepted work에 Store lease 강제 / lease-backed accepted work만 durable capture | 후자. Manual connection-bound request와 one-way send는 모두 `Captured` 전에 terminal 완료하고 실패하면 reversible abort하므로 manual topology에 Location Store를 강제하지 않음 |
| Session-to-Actor relay | Ambient handler context 또는 별도 request family / explicit current dispatch context의 common relay | 후자. Reply capability를 한 번 runtime에 넘기고 correlation·retry를 caller가 조합하지 않음 |
| Descriptor registration | 언어별 truncate·split / 공통 count·1 MiB 상한의 startup atomic validation | 후자. Partial descriptor를 게시하지 않고 모든 언어가 같은 configuration failure를 반환 |

업무 개념을 소유하는 경계도 다음처럼 고정한다. 이 분리는 하나의 상태 기계를 두 runtime에
중복하거나 raw transport 정보를 application 계약으로 노출하지 않게 한다.

| DDD 경계 | 소유하는 개념과 불변 조건 | 다른 경계에 노출하지 않는 정보 |
|---|---|---|
| Core raw transport | socket, endpoint, routing ID, message ownership, transport monitor | MeshName, ChannelName, Spot·Actor identity, owner lease, maintenance phase |
| Framework topology·messaging | MeshNode, role별 Channel, peer registry, outbound admission | Core engine·pipe, Store provider key, application handler state |
| Stateful object runtime | Spot turn, Actor membership, Instance activation, session binding | Physical socket, Redis command, application이 조합하는 activation·transfer phase |
| Authority·maintenance | owner CAS, lease fence, transfer reference, Retire·Shutdown terminal state | Provider별 transaction 구현, handler DI, raw transport reconnect 정책 |
| Hosting·application | DI scope, factory, typed handler, remote operation endpoint의 인증·인가 | Store CAS token 해석, wire command, Core handle·errno |

경계 사이 변환은 각 runtime의 internal adapter가 담당한다. Core error를 Framework typed result로
변환하는 규칙, logical address를 owner route로 해석하는 규칙과 provider별 CAS 구현을 public
handler에 전달하지 않는다.

선택한 표면이 구현 과정에서 pass-through method, 두 번째 state machine 또는 언어 하나만의 public 동작을
요구하면 `SPEC-06`으로 되돌려 대안을 다시 검토한다.

## 6. M2 — 10.x oracle 봉인과 제거 준비

M2는 비교 기준과 제거 준비만 고정한다. Framework service 기능을 구현하지 않는다. 10.x oracle는 읽기 전용
artifact를 별도 process에서 실행하고 versioned normalized trace만 출력한다. 새 candidate process에는 oracle
library 경로를 주입하지 않으며 build·link manifest와 실제 load 목록에서 oracle payload가 0건인지 확인한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M2-ORACLE` | Core 10.x oracle baseline과 normalized trace 봉인 | baseline·E2E lane, `P-DELIVERY` | `V11-R1` | 완료 | frozen artifact provenance, 별도 process protocol, 대표 정상·오류 trace와 archive 제외 규칙 통과 | SHA로 고정한 Core 10 executable을 child process로 실행해 `M17`·`M12` normalized trace를 생성했다. `ORACLE` self-test·`DOC`·`DIFF-OWNED` exit 0. 증거: `.artifacts/v11/evidence/V11-M2-ORACLE/result.json` |
| `V11-M2-CORE-READINESS` | Core service·ZMP heartbeat 제거 manifest와 raw 보존 경계 확인 | Core·inventory lanes, `P-SCAN` | `V11-R1` | 완료 | service와 heartbeat option·frame·timer·test 분류 100%, generic timer·monitor disposition 미분류 0 | Core와 shared perf 815건을 분류했고 ZMP heartbeat 22건, generic timer 8건, raw monitor 4건과 Core 10.x perf archive 18건의 disposition을 검증했다. PGM 관련 record와 미분류 record는 0건이며 `INV`·`DOC`·readiness self-test·`DIFF-OWNED`가 통과한다. 증거: `.artifacts/v11/evidence/V11-M2-CORE-READINESS/result.json` |
| `V11-M2-RAW-CPP` | C++ binding raw capability 확인 | C++ binding lane, `P-DELIVERY` | `V11-R1` | 완료 | 현재 설치 package에서 multipart·monitor·STREAM·ready·shutdown capability와 보완 목록 확정 | public header의 다섯 raw capability와 targeted binding build·contract test가 통과했고 `ROW-GATE` exit 0이다. 전체 service runner의 ABI drift는 `V11-M4-BIND-CPP` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-CPP/result.json` |
| `V11-M2-RAW-DN` | .NET binding raw capability 확인 | .NET binding lane, `P-DELIVERY` | `V11-R1` | 완료 | public assembly만 사용해 raw capability와 보완 목록 확정, reflection·internal P/Invoke 접근 0 | public assembly의 다섯 raw capability와 targeted binding test가 통과했고 reflection·internal P/Invoke 접근 0, `ROW-GATE` exit 0이다. service sample namespace 충돌은 `V11-M4-BIND-DN` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-DN/result.json` |
| `V11-M2-RAW-JVM` | Java binding raw capability 확인 | JVM binding lane, `P-DELIVERY` | `V11-R1` | 완료 | public package만 사용해 raw capability와 보완 목록 확정, package-private·JNI 직접 접근 0 | public jar의 다섯 raw capability와 targeted binding test가 통과했고 package-private·JNI 직접 접근 0, `ROW-GATE` exit 0이다. service sample lifecycle 문제는 `V11-M4-BIND-JVM`·`V11-M7-SAMPLES` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-JVM/result.json` |
| `V11-M2-RAW-NODE` | Node binding raw capability 확인 | Node binding lane, `P-DELIVERY` | `V11-R1` | 완료 | package export·`.d.ts`로 raw capability와 보완 목록 확정, addon 내부 접근 0 | public root export·`.d.ts`의 다섯 raw capability, addon 내부 접근 0, raw test 18/18과 `ROW-GATE`가 통과했다. legacy service test의 internal module 의존은 `V11-M4-BIND-NODE` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-NODE/result.json` |
| `V11-M2-BIND-READINESS` | 네 bindings projection·package와 10.x 전용 binding 격리 확인 | binding·package inventory lane, `P-SCAN` | `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE` | 완료 | 제거·보완·보존 파일 미분류 0, C·Python·Go·Rust service·heartbeat projection은 10.x `Retain/OutOfScopeV11`로 격리되고 Core 11 입력·호환 metadata에서 제외됨 | binding·legacy 8개 언어의 분류에서 raw capability 누락·legacy disposition 불일치·compatibility claim이 모두 0이며 `INV`·self-test·`ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M2-BIND-READINESS/result.json` |
| `V11-M2-READY` | Removal-first 착수 gate | coordinator, `P-DEEP` | `V11-M2-ORACLE`, `V11-M2-CORE-READINESS`, `V11-M2-BIND-READINESS` | 완료 | 새 candidate의 oracle compile·link·load 0, Framework 기능 변경 0, Core 제거 입력 완결 | canonical schema 3종과 `ROW-GATE`·`REMOVE`를 구현했다. self-test 2종, 선행 M2 artifact 7개의 개별 `ROW-GATE`, `DOC`·`INV`·`WIRE`·`DIFF-OWNED`와 READY 자체 `ROW-GATE`가 모두 exit 0이다. 실제 source·package no-hit은 M3·M4 post-removal gate가 판정한다. 증거: `.artifacts/v11/evidence/V11-M2-READY/result.json` |

Raw capability가 부족하면 M4에서 일반 raw socket 사용자에게도 유효한 public API로 보완한다. Framework 전용
helper, private header, JNI·N-API 직접 호출과 설치되지 않는 symbol은 추가하지 않는다. 10.x oracle와의 대조는
`verified-baseline` 항목에만 적용하고 `v11-first-implementation` 기능의 완료 근거로 사용하지 않는다.

## 7. M3 — Core 11 service 제거, cleanup과 package

M3는 Core service header·export·source·test·active perf와 ZMP heartbeat option·frame·engine timer를 먼저
제거한다. Core에는 raw socket·transport·poller, generic timer와 monitor만 남긴다. 제거 뒤 POSD·DDD와 사용되지
않는 코드 review를 통과하기 전에는 Core 11 version metadata를 확정하거나 internal package를 배포하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M3-PERF-LEGACY` | Core service active perf 제거와 10.x archive 격리 | perf lane, `P-SCAN` | `V11-M2-READY` | 완료 | Spot source·target·runner pattern 0, raw perf 유지, oracle archive가 active baseline으로 선택되지 않음 | active Spot source 3개와 paired gate·test 2개를 제거했다. raw perf unit 37/37, 대표 raw target build, archive isolation, `REMOVE --scope common`, `DOC`, `DIFF-OWNED`, `ROW-GATE`가 모두 exit 0이다. 증거: `.artifacts/v11/evidence/V11-M3-PERF-LEGACY/result.json` |
| `V11-M3-CORE-REMOVE` | Core service와 ZMP heartbeat header·export·source·test·build 제거 | Core lane, `P-DEEP` | `V11-M3-PERF-LEGACY` | 완료 | service surface 0, heartbeat public·internal option·frame codec·engine timer·test 0, generic timer·monitor 유지 | service public header 7개와 Mesh API·runtime source, service test·build 입력을 제거했다. 독립 audit의 Spot·ChannelName state, service-named seam, stale build/test 입력과 `REMOVE` field 오탐 4건을 모두 수정했다. full build 100%, raw targeted 45/45, `REMOVE --scope core` 722 records·violations 0, post-fix audit clean, `DIFF-CHECK`와 `ROW-GATE`가 모두 exit 0이다. optional full lane의 비소유 환경 실패는 result의 issues에 분리했다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-REMOVE/result.json` |
| `V11-M3-CORE-CLEAN` | Core POSD·DDD와 unused code, Core package tooling 정리 | Core cleanup lane, `P-DEEP` | `V11-M3-CORE-REMOVE` | 완료 | service aggregate·compat facade·pass-through·끊긴 build 입력 0, service header copy 0, Core package·clean C consumer tooling self-test 통과 | stale internal service seam과 service-only guide 6개를 제거하고 Core 문서 39개를 raw-only로 고쳤다. Package tooling은 service header 재복사를 차단하고 네 version 지점과 clean C consumer를 검증한다. Canonical clean build, unittest 18/18, integration 42/42, regression 20/20, `CORE-PKG-TEST`, `REMOVE` 722 records·violations 0, `DOC`, `DIFF-OWNED`가 통과했다. `ROW-GATE`가 1MB 초과 base inventory에서 Node 기본 buffer로 종료되는 결함도 128MB 명시 상한으로 수정하고 self-test·candidate 182 files 검증을 통과했다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-CLEAN/result.json` |
| `V11-M3-CORE-VERIFY` | Core raw 회귀·sanitizer, package tooling와 oracle 격리 재검증 | Core test·inventory coordinator, `P-DELIVERY` | `V11-M3-CORE-CLEAN` | 완료 | clean build, full raw suite·sanitizer·package tooling self-test 통과, inventory reconcile, ZMP heartbeat 잔여와 oracle link·load 0 | 첫 transport 전 ID 0 queue와 reconnect 이후 nonzero stale 차단을 분리해 reconnect/connect 회귀를 수정했고 weighted reactivation fixture는 표준 pipe termination protocol로 정리했다. Core 11 clean build에서 unittest 18/18, integration 42/42, regression 20/20, exact 11.0.0 ASAN 80/80·sanitizer finding 0과 SONAME 11을 확인했다. Package tooling은 base revision과 candidate record에서 격리 build snapshot을 materialize하며 hash 변이, stale build, version·SONAME 변이를 거부한다. C++ consumer가 찾은 OpenSSL transitive dependency 선언을 포함해 final candidate 204 files·Core changed path 193개를 봉인한다. 병렬 binding delta의 inventory reconcile은 `V11-M4-BIND-JOIN`에 인계한다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/result.json` |
| `V11-R2` | Core 제거·POSD·DDD 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M3-CORE-VERIFY` | 완료 | I1·I2·I3, raw 보존과 제거 범위 review clean | 첫 C++ installed-package configure가 exported OpenSSL dependency 선언 누락을 찾아 Core config에 conditional `find_dependency(OpenSSL)`을 추가했다. 최종 SHA `06d72dab…`에 대해 두 reviewer가 blocking finding 0·clean/approved를 기록했고 package audit은 isolated build·C consumer와 외부 CMake `find_package(zlink)`·OpenSSL target·link·run을 통과했다. R2 `ROW-GATE` 3 files·5 commands 통과. 증거: `.artifacts/v11/evidence/V11-R2/result.json` |
| `V11-M3-CORE-PKG` | Core 11 version와 local/internal package 실행 | Core package lane, `P-DELIVERY` | `V11-R2` | 완료 | review한 source·tooling revision을 수정하지 않고 11.0.0 install artifact 생성, clean C consumer·provenance 통과, 외부 배포 0 | R2 승인 SHA `06d72dab…`를 isolated Release build해 기존 local prefix를 교체했다. Provenance 18 files, clean C consumer, runtime 11.0.0·SONAME 11, service header 0과 transitive OpenSSL CMake consumer가 통과했다. Provenance SHA-256 `7146fc20…`, runtime SHA-256 `6e950f27…`, 외부 배포 0. 증거: `.artifacts/v11/evidence/V11-M3-CORE-PKG/result.json` |

Core review 뒤 source가 바뀌면 package를 만들기 전에 영향받는 raw test와 review를 다시 실행한다. 제거한 service
symbol을 fake export나 빈 구현으로 남겨 bindings build를 통과시키지 않는다.

## 8. M4 — Bindings service projection 제거, cleanup과 package

네 bindings lane은 review를 통과한 Core 11 package를 공통 입력으로 사용한다. Service projection을 제거한 뒤
M2에서 확인한 raw capability를 보완하고 POSD·DDD와 사용되지 않는 wrapper·generated code를 정리한다. 각 언어
package는 통합 bindings review가 끝난 뒤에만 local/internal 위치에 배포한다.

M4의 test 범위는 raw binding contract와 clean package consumer로 제한한다. 기존 Framework public
sample·E2E source와 실행 registration은 Git 기준으로 보존하고 이 stage에서는 실행하지 않는다. Binding test
runner가 sample이나 Framework E2E를 함께 실행하면 raw-only subset을 별도 command로 선택하되, 이를 이유로
기존 runner registration을 삭제하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M4-BIND-CPP` | C++ projection 제거·raw 보완·cleanup | C++ binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated symbol 0, public raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Raw contract 10/10, final removal 6 records·violations 0과 actual external CMake consumer가 통과했다. Package gate는 승인된 Core provenance·candidate, version 11.0.0과 SONAME 11을 강제한다. Sample/E2E 변경·실행은 0이며 final candidate 121 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-CPP/ledger-result.json` |
| `V11-M4-BIND-DN` | .NET projection 제거·raw 보완·cleanup | .NET binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated P/Invoke 0, public raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Exact Core 11.0.0으로 127/127, final removal 7 records·violations 0과 isolated NuGet consumer가 통과했다. 기존 local native payload를 output으로 복사하는 item과 ambient fallback, 사용처 없는 service snapshot helper를 제거했고 실제 loaded runtime SHA·SONAME을 검증한다. Sample/E2E 변경·실행은 0이며 final candidate 101 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-DN/ledger-result.json` |
| `V11-M4-BIND-JVM` | Java projection 제거·raw 보완·cleanup | JVM binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated JNI 0, `build.gradle`의 Core source·Boost include 0, installed Core package-only raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Raw tests 64/64, final removal 9 records·violations 0과 isolated Maven consumer가 통과했다. 승인된 Core candidate·runtime SHA·SONAME을 POM·module metadata와 JAR provenance에서 검증한다. Sample runner는 Git 기준으로 복구했고 sample/E2E source diff·실행은 0이다. Final candidate 186 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-JVM/ledger-result.json` |
| `V11-M4-BIND-NODE` | Node projection 제거·raw 보완·cleanup | Node binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat export·generated addon projection 0, public ESM·CJS export와 raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Registration을 유지한 raw-only selector로 build·typecheck·native rebuild와 raw test 8 files·failure 0·sample execution 0을 다시 기록했다. Linux resolver를 SONAME 11로 고정했고 package build 뒤 generated `prebuilds`·provenance가 source에 남지 않도록 정리했다. Final removal 17 records·violations 0, exact Core provenance gate와 ESM·CJS·type consumer tooling이 통과했으며 candidate `9cbfaee1…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-NODE/ledger-result.json` |
| `V11-M4-BIND-JOIN` | Bindings 제거·raw proof 합류 | binding coordinator, `P-DELIVERY` | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` | 완료 | 네 언어 service projection과 ZMP heartbeat option projection 0, public raw capability 결과 일치 | Final inventory 1,826 records·package tooling 38 paths와 joined removal C++ 6, .NET 7, JVM 9, Node 17 records·violations 0을 확인했다. 네 child candidate를 final inventory와 actual package consumer evidence로 다시 봉인했고 join `ROW-GATE`가 통과했다. Immutable sample/E2E는 M7 owner로 유지한다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-JOIN/result.json` |
| `V11-R3` | Bindings 제거·POSD·DDD 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M4-BIND-JOIN` | 완료 | public raw 경계, generated output, package input과 제거 범위 review clean | 두 독립 reviewer와 package audit가 C++ `b765413a…`, .NET `70aca597…`, JVM `00e3248f…`, Node `9cbfaee1…`, join `a3b8a1a0…`를 승인했다. Actual CMake·NuGet·Maven consumer, Node package consumer tooling, Core provenance·candidate·runtime SHA·SONAME 11, sample/E2E diff 0과 row freshness를 확인했고 blocking finding은 0이다. R3 candidate `8ce37cf7…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R3/result.json` |
| `V11-M4-PKG-CPP` | C++ binding 11 local/internal package | C++ package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision package와 clean CMake consumer 통과, 외부 배포 0 | R3 승인 source를 CMake prefix에 다시 설치하고 exact package configure·compile·link·load·run을 확인했다. Package tree SHA-256은 `a37a40c7…`, 외부 배포는 0이며 `ROW-GATE` 1 file·4 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-CPP/result.json` |
| `V11-M4-PKG-DN` | .NET binding 11 local/internal package | .NET package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision NuGet과 public-only clean consumer 통과, 외부 배포 0 | `Systems.Zlink.11.0.0.nupkg` SHA-256 `727ba451…`과 isolated public-only consumer restore·build·run, loaded Core 11 SHA·SONAME을 확인했다. 외부 배포는 0이며 `ROW-GATE` 1/4가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-DN/result.json` |
| `V11-M4-PKG-JVM` | Java binding 11 local/internal package | JVM package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision Maven artifact·module metadata·clean consumer 통과, 외부 배포 0 | Maven `systems.zlink:zlink:11.0.0`을 생성해 clean Gradle consumer로 resolve·compile·run했다. JAR SHA-256은 `7c2b21cc…`이며 POM·module metadata와 Core provenance가 일치한다. 외부 배포 0, `ROW-GATE` 1/4가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-JVM/result.json` |
| `V11-M4-PKG-NODE` | Node binding 11 local/internal package | Node package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision tgz·ESM·CJS·`.d.ts` clean consumer 통과, 외부 배포 0 | `zlink-systems-zlink-11.0.0.tgz` SHA-256 `81e1ce89…`를 clean npm consumer에 설치해 CJS·ESM import와 type export를 확인했다. 외부 배포 0, `ROW-GATE` 1 file·6 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-NODE/result.json` |
| `V11-M4-CONSUMER-JOIN` | 새 bindings package 합류 | package coordinator, `P-SCAN` | `V11-M4-PKG-CPP`, `V11-M4-PKG-DN`, `V11-M4-PKG-JVM`, `V11-M4-PKG-NODE` | 완료 | 중앙 Framework 참조 version 고정, clean consumer provenance와 Core payload 일치 | C++·.NET·JVM·Node 중앙 binding version을 11.0.0으로 고정했고 Node workspace manifest와 lockfile도 같은 tgz를 해석한다. CMake tree `a37a40c7…`, NuGet `727ba451…`, Maven JAR `7c2b21cc…`, npm tgz `81e1ce89…`와 Core payload provenance를 resolution manifest에서 확인했다. Candidate `02b85a71…`의 `ROW-GATE` 8 files·8 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-CONSUMER-JOIN/result.json` |

2026-07-23 Core cleanup 이후 package refresh는 기존 11.0.0 version을 유지한 채 다시 실행했다. Core raw suite와
ASAN은 각각 80/80을 통과했고 Core provenance SHA-256은 `c0feeac0…`, runtime SHA-256은 `871e5306…`이다.
이 Core를 사용해 C++·.NET·JVM·Node local package와 isolated consumer를 다시 검증했다. 중앙 Framework version
지점은 변경하지 않았다. 증거:
`.artifacts/v11/evidence/V11-M3-CORE-PKG/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-BIND-CPP/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-BIND-DN/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-PKG-JVM/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-PKG-NODE/refresh-20260723.json`.

## 9. M5 — Private binding-facing port와 runtime foundation

M5는 Framework가 사용하던 Core service adapter를 제거하고, 각 언어 Framework 내부에 private
binding-facing port를 먼저 구현한다. 이 port는 binding API를 그대로 전달하는 facade가 아니라 service
runtime이 필요한 raw transport 동작과 binding type 변환을 내부에 감춘다. 해당 언어 binding의 설치된 public
raw API만 호출하며 Framework public interface, binding public service API와 공통 private C SPI를 추가하지 않는다.

구현은 기존 Framework public interface에서 안쪽으로 진행한다. M5는 기존 public FQN의 value type 소유권과
private port seam을 compile 가능한 상태로 고정하고, raw transport·codec·operation·resource 동작을 internal
contract로 확인한다. 기존 service owner·bridge의 실제 실행 경로를 새 port에 연결하고 제거하는 작업은 M6
vertical slice가 기능 단위로 수행하며 `V11-M6-SCAFFOLD-ZERO`에서 잔여 0을 확인한다. 이 단계에서 기존
Framework public class를 새 abstraction으로 일괄 교체하거나 호출자 코드를 새 runtime 형태에 맞춰 변경하지
않는다. 네 언어에서 동작이 복구된 뒤 중복이 확인될 때만 같은 언어 내부에서 refactoring한다.

각 M5·M6 runtime row는 구현 전에 관련 Framework 정식 spec, 해당 언어 exact interface와 공통 internals를
읽는다. Migration machine inventory가 가리키는 보존된 Core service 구현·test snapshot에서
component 분리, state machine, algorithm, ordering, ownership, timeout·shutdown·recovery failure case를 확인하고
row evidence에 참고한 snapshot revision과 path를 기록한다. 이 구현을 언어별 runtime 구조로 포팅하되 제거한
Core service symbol에 다시 의존하지 않는다. Spec 또는 internals와 다른 부분만 목표 문서를 적용하고 차이를
evidence에 남긴다.

`V11-M5-SCAFFOLD-*`는 기존 ID를 유지하지만 production scaffold를 만드는 작업이 아니다. 이 row는 private
port seam과 기존 public Framework value type의 compile 연결, 새 port 내부의 제거한 Core adapter 참조 0과
production `RuntimeNotReady`·fake data 0을 검증한다. 기존 service owner 실행 경로 전체의 제거 여부는
`V11-M6-SCAFFOLD-ZERO`가 검증한다. M5는 protocol fixture와 internal contract만 실행한다.
sample·E2E는 source·registration을 보존한 채 `pending`으로 유지한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M5-PROTOCOL` | Protocol 생성 상수·codec·golden foundation | protocol lane, `P-DEEP` | `V11-M4-CONSUMER-JOIN` | 완료 | schema의 livenessProbe `5`·livenessAck `6` 포함 생성물, probe ID echo·canonical frame·malformed fixture와 네 decoder 입력 확정 | Schema에서 C++·.NET·JVM·Node.js command·flag 상수를 생성하는 deterministic generator와 drift check를 추가했다. Liveness probe/ack canonical frame 2개는 같은 nonzero probe ID를 사용하고 wrong magic·unknown command·forbidden flag·zero/truncated ID·trailing byte 6개를 decoder 입력으로 고정했다. Schema self-test 124건, canonical 2건·malformed 6건과 probe ID echo, candidate `43ddd29f…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-PROTOCOL/result.json` |
| `V11-M5-SCAFFOLD-CPP` | C++ private binding-facing port와 기존 public API 연결 | C++ lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public raw binding-only port, 제거 projection 참조·production placeholder·fake data 0, internal compile contract 통과 | Public zlink_cpp 11 ROUTER API만 사용하는 private raw port와 isolated compile contract를 구현하고 shipped public FQN `zlink::spot_kind`를 Framework가 소유하게 했다. 새 M5 owned path의 금지 참조와 sample/E2E diff는 0이며 candidate `84bf351a…`의 `ROW-GATE`가 통과했다. 기존 service owner 6개의 include 7건은 placeholder로 우회하지 않고 M6 입력으로 남겼다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-CPP/result.json` |
| `V11-M5-SCAFFOLD-DN` | .NET private binding-facing port와 기존 public API 연결 | .NET lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public binding-only port, reflection·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public FQN의 value type은 Framework assembly로 이관하고 public `IContext`·`IRouterSocket`만 사용하는 private raw ROUTER port를 production owner와 분리해 구현했다. 실제 inproc multipart와 request/reply correlation, exception path resource cleanup, 금지 참조 0과 sample/E2E diff 0을 확인했으며 candidate `9c4c7201…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-DN/result.json` |
| `V11-M5-SCAFFOLD-JVM` | JVM private binding-facing port와 Java·Kotlin public API 연결 | JVM lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public Java binding-only port, JNI 우회·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public FQN 66개를 Framework artifact가 소유하고 public binding raw ROUTER API만 사용하는 private port를 production owner와 분리했다. Production fake-success 구현은 모두 제거했으며 isolated Gradle task 11/11, 새 owned path 금지 참조와 sample/E2E diff 0, candidate `709854af…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-JVM/result.json` |
| `V11-M5-SCAFFOLD-NODE` | Node private binding-facing port와 기존 public API 연결 | Node lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public package-only port, private addon 우회·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public export의 DTO·enum·value type은 Framework package로 이관하고 public raw Router port를 production owner와 분리했다. Fake mesh foundation은 제거했고 isolated scaffold compile, 새 owned path 금지 참조와 sample/E2E diff 0, candidate `9559af6e…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-NODE/result.json` |
| `V11-M5-FOUND-CPP` | C++ transport·codec·operation foundation | C++ lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-CPP` | 완료 | public binding-only gateway, codec·completion·clock·resource unit proof 통과 | Generated constants 기반 strict codec, bounded terminal-once registry, destructor shutdown과 callback exception isolation을 구현했다. Raw port·codec·operation test 3/3과 candidate `54d3b5ec…`의 `ROW-GATE`가 통과했다. Full owner build와 full removal은 M6가 소유한다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-CPP/result.json` |
| `V11-M5-FOUND-DN` | .NET transport·codec·operation foundation | .NET lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-DN` | 완료 | public binding-only gateway, codec·Task completion·clock·resource unit proof 통과 | Generated constants 기반 codec, terminal-once completion, injectable clock·fence와 reverse resource cleanup을 구현했다. `FailAll`이 실제 terminal result를 보존하는 회귀와 raw roundtrip을 isolated executable로 검증했고 candidate `489aad47…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-DN/result.json` |
| `V11-M5-FOUND-JVM` | JVM transport·codec·operation foundation | JVM lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-JVM` | 완료 | Java binding-only gateway, codec·executor·coroutine·resource unit proof 통과 | Authoritative single-frame codec이 shared canonical 2건·malformed 6건을 직접 decode하고 terminal-once registry, bounded mailbox와 raw resource ownership을 검증한다. Production compile과 분리된 Gradle task 11/11과 candidate `3915812f…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-JVM/result.json` |
| `V11-M5-FOUND-NODE` | Node transport·codec·operation foundation | Node lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-NODE` | 완료 | public package-only gateway, codec·Promise·event-loop resource unit proof 통과 | Generated definition codec, terminal-once Promise·injectable clock, infrastructure/application event-loop queue와 reverse cleanup을 구현했다. Shared fixture를 포함한 isolated test 4/4와 candidate `d6fed872…`의 `ROW-GATE`가 통과했고 production fake mesh runtime은 남기지 않았다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-NODE/result.json` |
| `V11-M5-FOUND-JOIN` | Foundation 합류와 pending E2E 감사 | foundation coordinator, `P-DELIVERY` | `V11-M5-FOUND-CPP`, `V11-M5-FOUND-DN`, `V11-M5-FOUND-JVM`, `V11-M5-FOUND-NODE` | 완료 | 네 codec golden·negative fixture 일치, required E2E `executed=0`, `skipped=0`, `pending=required` | 네 foundation의 generated protocol·decoder fixture가 일치하고 inventory 1,908건과 새 M5 owned path 금지 참조 0을 확인했다. Required scenario 112건은 `pending=112`, `executed=0`, `skipped=0`이며 sample/E2E source·registration diff는 0이다. Candidate `052b93c6…`의 `ROW-GATE`가 통과했다. 기존 production owner의 full removal은 `V11-M6-SCAFFOLD-ZERO` 입력으로 기록했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-JOIN/result.json` |
| `V11-R4` | Runtime foundation 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M5-FOUND-JOIN` | 완료 | schema, public binding 경계, production placeholder 0과 resource ownership review clean | C++/.NET과 JVM/Node·protocol을 나눈 독립 review에서 발견한 no-op·synthetic success·wire frame·terminal cleanup 문제를 모두 제거하고 재검토했다. Blocking finding 0, sample/E2E diff 0이며 candidate `3a7f6399…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R4/result.json`, `.artifacts/v11/evidence/V11-R4-M5/` |

### 9.1 M5 이후 public contract amendment와 실행 격리

이 stage는 M5 완료 뒤 추가된 global identity와 remote placement 변경을 M6 입력으로 확정한다. 두 변경 제안은
설계 입력일 뿐이며, 공개 계약은 Framework 공통·server 정식 spec과 다섯 언어 exact interface에 먼저 반영한다.
Protocol과 E2E·sample 영향 목록도 같은 candidate에서 확정하고 독립 review를 통과한다. 이 stage에서는
Framework runtime source를 구현하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-CA-DECISION` | Global identity·remote placement 공개 경계 결정 | architecture·contract lane, `P-DEEP` | `V11-R4` | 완료 | 두 제안과 Store amendment의 open item 미결정 0, 대안·POSD 판단·정식 owner 확정 | `CA-D01~CA-D36`으로 global identity, placement, generic reservation과 Location·Relocation Store 분리 결정을 고정했다. Location canonical participant authority와 Relocation lookup manifest, write-before-CAS publication, 별도 Redis class, `RelocationDataLost=34`까지 미결정 0이다. |
| `V11-CA-SPEC` | Framework 공통 spec·common internals 통합 | contract·internals lanes, `P-DEEP` | `V11-CA-DECISION` | 완료 | identity·remote create·placement·reservation·handover·failure 의미와 runtime owner 누락 0, 임시 target·legacy spec 문서와 active link 0 | 43개 결정을 공통 Framework spec과 common internals의 정식 owner에 반영하고 `23-relocation-store-redis.ko.md`를 추가했다. `target-spec`, `target-internals`는 mapping을 inventory·trace에 반영한 뒤 삭제했다. 2026-07-25 재작성본을 기준으로 formal document 140개, exact member 1,397개, canonical member 176개와 unclassified·ambiguous·unknown owner 0개를 확인했다. 중복된 `framework/doc/framework/spec/` 137개, 종료된 `framework/doc/plan/v10.0/` 539개와 v10 contract inventory 2개를 제거하고 active Markdown link 314개와 source·test·fixture·generator 참조를 `common/spec`으로 전환했다. Canonical Markdown link 2,378개와 formal anchor, `DOC`, `TRACE --check`, migration inventory와 impact manifest 재생성 검증이 통과했으며 `framework/doc/plan/log/`는 변경하지 않았다. 2026-07-27에는 공통 spec `42`, `50`~`55`를 작성 가이드에 맞춰 책임·순서·실패·제한 중심으로 다시 구성하고, Glossary의 첫 설명과 inbound link를 정리했다. `git diff --check`와 전체 문서 계약 검증이 통과했으며 결과는 formal document 134개, exact document 51개, declaration owner 1,232개, member 4,347개, unclassified·ambiguous·unknown owner 0개다. |
| `V11-CA-IFACE-CPP` | C++ exact interface amendment | C++ contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | C++ public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 `uint32_t` CRC32C, 별도 Store·Redis class, inventory digest, `relocation_data_lost=34`와 publication 순서를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-IFACE-DN` | .NET exact interface amendment | .NET contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | .NET public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 `uint` CRC32C, `IZLinkRelocationStore`, 별도 Redis class, inventory digest와 `RelocationDataLost=34`를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-IFACE-JVM` | Java·Kotlin exact interface amendment | JVM contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | Java·Kotlin public signature·example·trace가 amended spec과 일치 | Java Put 결과의 `long` CRC32C 범위를 맞췄다. Kotlin Location·Relocation Store의 Java `CompletionStage` method는 protected suspend hook과 연결하며 Java·Kotlin 모두 `Relocation` 공개 이름만 제공한다. |
| `V11-CA-IFACE-NODE` | Node.js exact interface amendment | Node contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | Node.js public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 bounded numeric CRC32C, 별도 Store·Redis class, inventory digest와 `RelocationDataLost=34`를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-PROTOCOL` | Placement·identity protocol과 fixture amendment | protocol lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | schema·generated constant·golden·negative fixture와 formal 의미 drift 0 | `RelocationDataLost`는 wire 35 → public 34로 고정하고 wire 23..34를 예약했다. Permanent payload missing, checksum mismatch와 inventory digest mismatch를 닫힌 terminal trigger 3개로 고정했으며 WIRE 37 commands·151 types·159 negative self-tests, generated 35 fixtures와 decoder 24 error·3 malformed가 통과했다. |
| `V11-CA-IMPACT` | E2E·sample·regression 영향 목록과 실행 격리 | E2E·sample·regression lanes, `P-DELIVERY` | `V11-CA-DECISION` | 완료 | 모든 항목 baseline hash·disposition·owner·activation stage 분류, `pending-disabled-by-contract-amendment`, executed·skipped 0 | Approved base `1f5b979675`와 현재 trace의 exact delta에서 public member 추가 2,066·제거 1,481을 생성했다. 제거 member는 exact signature replacement 316개와 closed decision·behavior replacement 1,165개로 분류했다. `CA-D72~CA-D76`의 one-way·Deferred Join·Object Context·MessageContext removal과 `CA-D77`의 publish monitoring removal 61개를 서로 겹치지 않는 독립 규칙으로 추적한다. Cross-language 150개 group과 Kotlin source/JVM 51개 group을 재검증한 결과 unmatched·ambiguous·parity mismatch는 0이다. 전체 4,805개에 대한 `--check`와 quarantine self-test는 pending 4,151, reviewed-source disabled 51, executed·skipped 0, negative mutation 14개로 통과했다. RuntimeMonitoring runner 변경은 이번 amendment의 temporary reviewed source로 소유한다. |
| `V11-CA-JOIN` | Contract amendment 합류 | amendment coordinator, `P-DELIVERY` | `V11-CA-SPEC`, `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE`, `V11-CA-PROTOCOL`, `V11-CA-IMPACT` | 완료 | 정식 spec·다섯 interface·wire·impact manifest·trace의 미분류와 semantic drift 0 | Review finding 반영 뒤 `DOC`, `INV`, `TRACE --check`, `WIRE`, `WIRE-GEN --check`, decoder fixture, impact generator `--check`, quarantine self-test, Instance Spot contract와 diff check가 통과했다. Trace는 documents 56, owners 1622, members 6199, unclassified·ambiguous·unknown 0이다. E2E·sample source diff는 0이며 Core PGM·perf는 사용자 확인에 따라 별도 병행 작업으로 제외했다. |
| `V11-R4A` | Contract amendment 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex high review lane, `P-HIGH` | `V11-CA-JOIN` | 완료 | public contract·protocol·영향 disposition·대체 coverage의 I1·I2·I3 review clean | Candidate `ddf7747688`, `9f5a9caa34`, `46da38f29b`, `d5560840f5`, `4a566820c8`의 finding을 모두 반영한 `edc361796a`를 재검토했다. Codex 5.6 sol xhigh와 Claude Sonnet은 blocking finding 0으로 판정했고 별도 policy audit도 clean이다. 제거 member 805개의 replacement는 exact signature 147개와 closed decision·behavior 658개이며, cross-language 76개 group과 Kotlin source/JVM 43개 group의 unmatched·ambiguous·parity mismatch가 0이다. Core PGM·perf는 별도 작업으로 제외했다. |
| `V11-CA-DRAFT-RETIRE` | 임시 contract 변경 제안 흡수 확인과 삭제 | contract coordinator, `P-DELIVERY` | `V11-R4A` | 완료 | 채택 내용은 정식 spec·exact interface·protocol에 모두 존재하고 E2E·sample 영향은 manifest에 분류되며, 미채택·수정 결정은 ledger에 이유가 기록되고 두 proposal과 link가 repository에서 0 | R4A clean 뒤 임시 입력 두 개를 삭제하고 README·ledger·document inventory의 link와 허용 목록을 제거했다. 정식 spec·internals·다섯 exact interface·protocol·impact manifest와 이 ledger만으로 M6 입력을 구성하며 `DOC`, `TRACE --check`, impact quarantine self-test와 repository link 0 검증이 통과했다. 증거: `.artifacts/v11/evidence/V11-CA-DRAFT-RETIRE/result.json` |
| `V11-CA-STORE-POSD-SPEC` | Generic Location·Relocation Store 경계를 정식 spec에 흡수 | contract·internals lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | Location atomic batch의 key·value·version·TTL·scan 상한과 conflict·ambiguous completion, Framework-issued immutable Relocation reference·blob·retention·reconcile 의미가 정식 owner에 존재하며 domain-specific public Store operation 0 | 공통 `40`·`41`·`42`와 implementation gap을 정렬했다. `AddLocationStore`·`AddRelocationStore`는 유지하고 provider가 authority phase·reservation·aggregate DTO를 구현하지 않도록 Framework private record가 의미를 소유한다. Host lifecycle command 명칭과 단계 분리는 후속 `V11-CA-RELOCATE-SPLIT-SPEC`이 소유한다. `DOC`와 `TRACE --check`가 통과했다. |
| `V11-CA-RELOCATE-SPLIT-SPEC` | Host Relocate와 Shutdown public command 및 target mode 분리 | contract·exact interface lanes, `P-DEEP` | `V11-CA-STORE-POSD-IFACE` | 완료 | 공통 spec과 다섯 언어 exact interface에서 public Retire declaration 0, `Relocate -> Relocated`와 `Serving|Relocated -> Shutdown` 결과·상태·cancellation·동시 호출 의미 일치, PlannedMaintenance exact same-version·RollingUpdate exact requested higher-version selection과 E2E positive·negative coverage, `DOC`·`TRACE` 통과 | 최종 용어를 `Relocated`로 정렬했다. `Relocate`는 host를 종료하지 않고 workload 이전을 끝내며, Application은 결과를 확인해 `Shutdown`을 별도로 호출하거나 relocation 없이 `Shutdown`만 호출할 수 있다. Mode별 exact version 선택, version→maintenance wave→capability→capacity→weight 순서, target 부재, 같은 option 합류, 다른 option의 `Blocked/OperationInProgress`, partial commit·Shutdown 경합은 공통 `28`과 다섯 exact interface, E2E `OBS-C5`·`C6`·`C7`·`C10`·`C11`에 존재한다. `90-implementation-gap` §12.49가 runtime 미구현 차이를 별도로 소유한다. `verify-framework-doc-contracts.sh`는 WIRE 40 commands·167 types·234 negative와 TRACE documents 51·owners 1,256·members 4,455·unclassified·ambiguous·unknown 0으로 통과했다. |
| `V11-CA-STORE-POSD-IFACE` | 다섯 언어 exact interface와 package 경계 정렬 | C++·DN·JVM·Node contract lanes, `P-DEEP` | `V11-CA-STORE-POSD-SPEC` | 완료 | C++·.NET·Java·Kotlin·Node.js의 Store primitive, closed result·error, async input lifetime, cancellation reconcile, ownership·dispose와 public declaration trace parity 100% | 다섯 언어를 generic atomic Location Store와 Framework-issued immutable Relocation Store로 정렬했다. Redis 공개 표면은 두 Store class의 최소 constructor·options뿐이며 중복 등록 helper가 없다. Trace는 documents 51, owners 1,237, members 4,421, unclassified·ambiguous·unknown 0이다. |
| `V11-M6-STORE-POSD-DN` | .NET generic Store 기준 구현과 회귀 | .NET runtime·provider lanes, `P-DEEP` | `V11-CA-STORE-POSD-IFACE` | 수정 진행 | provider abstraction, in-memory·Redis 구현과 authority·relocation adapter가 정식 primitive만 사용하고 contract snapshot·provider·Redis·crash-retry·E2E focused test 및 전체 `.NET M6-RUNTIME` 통과 | Redis package의 domain-specific Location repository·Lua·key codec과 물리 schema test는 제거하고 opaque SPI·Framework private adapter test로 교체했다. 남아 있던 Relocation internal repository 직접 구현과 전체 `Zlink.Framework` dependency도 제거했다. 별도 `Zlink.Framework.Provider.Abstractions` package를 package manifest·snapshot·소비 검증에 포함했으며 Redis package는 이 package만 exact version으로 참조한다. Framework adapter는 operation별 unique reference를 `Put` 전에 한 번 발급하고 응답 유실 뒤 같은 reference를 exact read하여 동일 bytes만 성공으로 reconcile한다. 따라서 동일 payload의 독립 relocation이 reference·retention·delete 수명을 공유하지 않는다. Linux Redis build warning·error 0, Redis 25/25, ambiguous completion focused 2/2, 전체 Unit 1,103/1,103, Contract 67/67과 fixed packaged contract·실제 NuGet 소비 검증이 통과했다. 후속 `BLK-047` 교정은 서로 다른 64개 object의 concurrent reservation·generic commit·terminal publication이 공유 capacity CAS와 경쟁해도 자기 reservation을 잃지 않도록 bounded retry를 연결했으며 focused 3/3이 통과했다. Process E2E `logs/20260728-035041-2190321`은 실제 client에서 `TD-A1..C5`를 통과해 `TD-A2` Store 공유 경로를 재검증했다. 후속 진단에서 Session gateway의 잘못된 Object Server capability, Entry Spot에서 실행하던 D1·D2·F3 setup, per-mesh RID를 그대로 사용하던 evidence route와 Entry/User Spot Join marker 차이를 수정했다. Framework 내부 route descriptor만 있고 DI 등록이 빠졌던 session route seal·abort·commit·unseal handler 네 개를 내부 runtime service로 등록하고 focused registration test를 추가했다. Join completion의 request ID와 marker는 timing 값에서 추론하지 않고 immutable `JoinDelayReq` payload에 명시해 recreated Actor가 public completion reply에서 복원한다. Route commit ACK가 검증한 relocation·object·owner generation fence와 session owner RID를 private completion result에 보존한다. Authority를 Steady로 전이하고 정상 payload로 정리한 뒤 이 RID로 ingress를 unseal하므로 제거된 relocation metadata를 다시 읽지 않는다. Duplicate commit에서도 같은 RID가 유지되는 focused test 2건이 통과했다. 실제 multi-process 실행은 `logs/20260728-050239-3599515`에서 TD-D1, `logs/20260728-050340-3689192`에서 TD-D2·TD-F3가 통과했다. `BLK-046` 교정은 prepare claim 뒤 실패를 독립 5초 reconcile 범위로 묶고 exact `Prepared/Committed` 또는 `Staging→Aborted`와 inventory 기반 fence 해제를 보장한다. Participant meta 손실, root·meta·payload 손상, ordinal permutation, 10,000-participant publication probe와 projection, 64·256 MiB decode memory를 회귀로 고정했다. `ProviderLocationRepositoryAuthorityTests` 29/29, `RelocationRuntimeTests` 140/140, `RelocationTreeParallelIoTests` 4/4와 Framework build warning·error 0이 통과했다. 이 blocker는 해소됐으며 immutable candidate `ROW-GATE`와 독립 high review는 후속 row가 계속 소유한다. |
| `V11-M6-STORE-POSD-DN-REVIEW` | .NET Store 기준 구현 POSD·DDD review와 reference 고정 | .NET independent review lane, `P-HIGH` | `V11-M6-STORE-POSD-DN` | 수정 진행 | Codex `gpt-5.6-sol high` finding 0, focused·전체 회귀 재통과, public snapshot과 `REMOVE` clean, immutable candidate·`ROW-GATE` 통과 | 후속 review의 cursor materialization, completed token bound, fixed cleanup batch, descriptor exact read-back, sub-ms retention 올림, envelope bound와 Redis shared connection pool을 현재 구현·test와 다시 대조했다. Provider authority 43/43, provider relocation 4/4, Redis lifecycle 12/12, Redis opaque 10/10과 Redis project build warning·error 0이 통과했다. 전체 `.NET` 회귀와 public snapshot·`ROW-GATE`를 같은 candidate에서 통과하기 전에는 완료로 바꾸지 않는다. |
| `V11-M6-STORE-POSD-MIRROR` | 네 언어 Store 구현 미러링 | C++·JVM·Node runtime·provider lanes, `P-DEEP` | `V11-M6-STORE-POSD-DN-REVIEW` | 대기 | C++·Java·Kotlin·Node.js의 provider·Redis·crash-retry·public snapshot과 대응 `M6-RUNTIME` 통과, 구현 차이마다 row 증거 존재 | .NET의 component 경계, state machine, atomic ordering, ownership, 오류 분류, resource 수명과 test 구성을 각 언어에 이식한다. 언어별 backend 차이는 public 계약을 늘리지 않고 internal 구현으로 흡수한다. |

`V11-M6-STORE-POSD-DN` 재검토 증거(2026-07-28): aggregate claim 직후
cancellation과 startup corrupt root를 추가로 교정했다. Canonical·non-seekable
relocation decode는 application state, recovery, accepted request와 terminal payload를
하나의 owned buffer에서 slice로 투영한다. Canonical timer와 terminal completion은
각각 `CanonicalTimer`와 `TerminalCompletions`를 사용하며 legacy payload를 다시
조합하지 않는다. 일부 participant만 publish된 probe는 `Unknown`으로 판정해 root와
authority를 보존한다. 이어서 semantic participant 수와 무관한 ordered opaque byte
stripe를 최대 64개로 제한하고 전체 chunk 상한 4,096개를 적용했다. Ambiguous
aggregate prepare는 실제 authority를 다시 읽어 target owner generation을 복원한다.
Redis 두 Store는 `OperationTimeout` 기본값 5초로 connection과 command 대기를 제한하고
caller cancellation을 별도로 보존한다. Root 재검증에서 Redis 29/29,
`RelocationTreeParallelIoTests` 10/10, `RelocationRuntimeTests` 147/147을 통과했다.
Framework와 Redis production build는 warning·error 0이다. 독립 high review가 이
candidate를 다시 승인하기 전에는 review row를 완료로 바꾸지 않는다.
| `V11-M6-STORE-POSD-CONVERGENCE` | Generic Store 다섯 언어 교차 review와 동형 수렴 | Codex review lane, `P-HIGH` | `V11-M6-STORE-POSD-MIRROR` | 대기 | §18 I1·I2·I4 finding 0, 다섯 언어 exact interface·source·provider·Redis·test parity와 domain-specific public SPI 잔존 0 | 다른 언어에서 기준보다 나은 형태를 확인하면 .NET을 포함한 다섯 구현을 다시 정렬한다. 우열을 판단할 근거가 없으면 승인한 .NET reference를 유지한다. |
| `V11-CA-STORE-POSD-DRAFT-RETIRE` | Store POSD 임시 제안 흡수 확인과 삭제 | contract coordinator, `P-DELIVERY` | `V11-CA-STORE-POSD-IFACE` | 완료 | 정식 spec·다섯 exact interface·public trace owner 누락 0, 임시 문서와 repository link 0 | 임시 설계 문서 세 개와 inventory 허용 항목을 제거했다. Repository link 검색 0, `DOC`, `TRACE --check`와 `git diff --check`가 통과했다. 구현·provider·Redis·crash-retry·E2E 완료는 후속 M6 row가 소유한다. |
| `V11-CA-SPOT-FLUENT` | Instance Spot fluent cold activation 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | global SpotId 시작 method, Spot 전용 fluent call, User-only manager, type inference·initial Mesh·internal close가 공통 spec과 다섯 exact interface에서 일치하고 source·sample·E2E 변경 0 | `InstanceSpotAddress`를 복구하지 않고 Spot direct fluent call에 explicit Instance intent를 고정했다. 후속 `CA-D47`은 Missing activation의 source-side reservation을 제거했다. Source는 first-message activation envelope를 target에 제출하고 target CAS winner가 generic reservation으로 authority와 pending capacity를 함께 확보하며 Ready commit 뒤 envelope message를 local queue에 한 번 제출한다. Spot create terminal result는 `SpotRef`, 세 state(`Existing`, `Created`, `Rejected`)와 optional reply를 반환한다. 최초 contract self-test와 R4B review는 source-side reservation 문구를 대상으로 했으므로 target-owned activation 변경은 `V11-R5B`와 최종 E2E review에서 다시 검증한다. |
| `V11-CA-RELOCATION-LIFECYCLE` | Actor·Spot relocation adapter와 Entry Spot lifecycle 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | opaque bytes adapter, Snapshot invocation scope, Restore-before-commit, infrastructure relocation callback 부재, SpotWide aggregate·application safe point와 Entry·PerActor queue·timer·route 규칙이 공통 spec·다섯 exact interface에서 일치하고 이전 `Transfer*` 공개 이름 0 | `CA-D37~CA-D43`과 2026-07-27 후속 결정을 공통 spec·다섯 언어 exact interface·E2E에 반영했다. Entry Spot과 `PerActor` User Spot은 stateless execution shell이며 Actor state·queue·accepted journal·Actor timer·session route만 Actor별로 이전한다. `PerActor` Spot은 `Recreate`만 허용하고 유지할 shared state와 Spot-level schedule은 application 외부 저장소가 소유한다. Target private shell은 같은 public SpotId·ObjectGeneration을 사용하며 Spot authority를 먼저 전환한다. `ToSpot`·Create·Join은 새 Spot owner, `ToActor`는 Actor별 current owner를 사용하고 stale source route는 operation identity·generation·deadline·correlation·reply route를 보존해 relay한다. Infrastructure relocation은 join·leave·relocation application callback을 호출하지 않으므로 다섯 언어의 `OnActorRelocated` 계열 public member 6개를 제거했다. SpotWide readiness 기본값은 `AnyTurnBoundary`이고 `ApplicationSignaled`에서만 `RelocationReady().Defer()`를 허용한다. Accepted boundary는 이동하지 않거나 precommit abort이면 source `Continued`, 이동하면 target `Relocated` default no-op callback을 일반 queue보다 먼저 실행한다. Actor, Instance Spot과 User Spot relocation unit은 source admission seal부터 target admission-open ACK까지 1초를 목표로 하며 초과해도 relocation을 계속한다. Public trace는 declaration owner 1,254개·member 4,398개·미분류 0이며 document contract verifier가 통과했다. Runtime·sample·E2E 구현은 `V11-M6C-*`와 `V11-M6C-E2E`가 계속 소유한다. |
| `V11-R4B` | Instance Spot fluent·relocation lifecycle 계약 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex high review lane, `P-HIGH` | `V11-CA-SPOT-FLUENT`, `V11-CA-RELOCATION-LIFECYCLE` | 완료 | global identity·cold activation·type inference·User-only manager, relocation adapter·callback·failure와 다섯 언어 parity의 I1·I2·I3 review clean | Codex 5.6 sol xhigh가 찾은 standalone Actor old Entry cleanup-before-replay, User Spot aggregate participant cardinality, exact request-source terminal identity, sequence domain, Java·Kotlin Instance timer, 이전 용어·metric과 impact hash 불일치를 모두 수정했다. Wire는 37 commands·157 types·36 bounds와 negative self-test 186개, DOC은 formal 137·exact 56, trace는 owner 1,650·member 6,389·미분류 0, impact quarantine은 3,458개 중 pending 2,850·executed/skipped 0으로 통과했다. 최종 Codex 재검토와 Claude Sonnet focused review는 모두 `CLEAN`이다. |

#### 9.1.1 User Spot execution·capacity·Entry identity 후속 amendment

이 amendment는 M6 구현 중 확인한 User Spot 실행 경계, capacity 계층과 Entry Spot identity의 계약 공백을
정식 입력으로 닫는다. 기존 M6 증거는 폐기하지 않지만, 아래 계약에 직접 영향을 받는 scheduler, Location
Store, provider와 monitoring 결과는 새 계약을 구현하고 재검증하기 전까지 완료 근거로 사용하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-CA-USER-SPOT-SPEC` | User Spot execution mode·Yield·capacity·Entry RID·공통 weight 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-R4B` | 완료 | 공통·server spec에 `CA-D57~CA-D64`와 원문 §11 UUID v4·§12 weight 대체 조항 누락 0, 기존 상충 문구 0 | 변경 요청의 23개 반영 항목과 §11·§12 추가 항목을 execution·capacity·identity·weight owner에 나누어 정식 spec에 반영했다. Positive weight 범위의 원문 오기는 공통 허용 범위와 같은 `1..10000`으로 확정했다. |
| `V11-CA-USER-SPOT-CORE-RID` | Core raw socket automatic RID 계약 확인 | Core contract lane, `P-DEEP` | `V11-R4B` | 완료 | 미지정 raw socket RID가 RFC 4122 UUID v4 bit layout의 16-byte binary이고 Framework 문자열 형식과 혼합되지 않음 | Core socket 정식 spec의 `set/get routing ID` 계약에 caller 미지정 기본값과 raw binary 표현을 한국어·영어로 고정했다. |
| `V11-CA-USER-SPOT-IFACE-CPP` | C++ exact interface 갱신 | C++ contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | C++ declaration·example·provider request·monitoring shape를 공통 계약에 맞췄고 placement·descriptor·monitoring weight를 signed `int`로 통일했다. |
| `V11-CA-USER-SPOT-IFACE-DN` | .NET exact interface 갱신 | .NET contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | .NET exact declaration을 공통 계약에 맞췄고 `CancellationToken`은 .NET 표현에만 유지했다. Weight monitoring도 signed `int`로 통일했다. |
| `V11-CA-USER-SPOT-IFACE-JVM` | Java·Kotlin exact interface 갱신 | JVM contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | Java ABI와 Kotlin suspend surface가 같은 실행·capacity·weight 의미를 제공 | Java ABI와 Kotlin coroutine 표현에 같은 execution·capacity·identity·weight 의미를 고정했다. Kotlin execution permit 연결은 runtime sub-ID가 검증한다. |
| `V11-CA-USER-SPOT-IFACE-NODE` | Node.js exact interface 갱신 | Node.js contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | Node.js exact declaration에 Actor claim·Spot gate, typed capacity, Framework-issued Spot identity와 signed weight 계약을 반영했다. Runtime mailbox 연결은 별도 sub-ID로 유지한다. |
| `V11-CA-USER-SPOT-E2E-SPEC` | 공통 E2E scenario와 impact disposition 갱신 | E2E coordinator, `P-DELIVERY` | `V11-CA-USER-SPOT-SPEC` | 완료 | Config 1·2·5·6·7·8·9·10·11·12·13·14와 catalog에 실행·capacity·identity·weight scenario, race와 언어 parity 누락 0 | Config 1·2·5·6·7·8·9·10·11·12·13·14와 catalog에 ordering·barrier·typed capacity·UUID collision·descriptor cleanup·weight boundary/revision/selection 검증을 반영했다. |
| `V11-CA-USER-SPOT-REVIEW` | 정식 흡수 완전성·교차 일관성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-USER-SPOT-CORE-RID`, `V11-CA-USER-SPOT-IFACE-CPP`, `V11-CA-USER-SPOT-IFACE-DN`, `V11-CA-USER-SPOT-IFACE-JVM`, `V11-CA-USER-SPOT-IFACE-NODE`, `V11-CA-USER-SPOT-E2E-SPEC` | 완료 | 원문 23개 항목과 §11 UUID v4·§12 weight 추가 항목의 spec·exact interface·E2E·runtime row 추적 100%, 상충·미소유 0 | Codex 5.6 sol xhigh가 최종 candidate를 `CLEAN`으로 판정했다. Claude Sonnet focused review는 execution·capacity와 identity·weight를 각각 `APPROVE`했다. Broad budget-limited review가 제기한 User Spot automatic UUID 미소유 주장은 `24-spot-address-messaging`과 다섯 Spot exact interface의 명시 계약으로 해소했고, flow tracing ID 제안은 원문 요구가 아니므로 범위에 추가하지 않았다. Trace는 owners 1734, members 6637, unclassified·ambiguous·unknown 0이다. |
| `V11-CA-USER-SPOT-DRAFT-RETIRE` | 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-USER-SPOT-REVIEW` | 완료 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | 두 독립 reviewer 결과와 자동 검증을 확보한 뒤 proposal과 README link를 제거했다. 이후 구현은 정식 spec·exact interface·E2E·아래 runtime sub-ID만 사용한다. |
| `V11-CA-SPOT-ID-SPEC` | SpotId logical identity 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Entry·User·Instance가 global UTF-8 string SpotId를 사용하고 NodeRid만 RoutingId를 사용하며 network identity 문서의 Spot identity 소유 0 | 공통 Spot·Location·Redis·monitoring·relocation 문서의 `SpotRid` 표면을 `SpotId`로 전환하고 identity 규칙을 `24-spot-address-messaging`으로 이동했다. UTF-8 1..255 bytes, case-sensitive exact match, no normalization과 global namespace를 정식 owner에 고정했다. |
| `V11-CA-SPOT-ID-WIRE` | SpotId wire·Store schema clean break | protocol·Location contract lane, `P-DEEP` | `V11-CA-SPOT-ID-SPEC` | 완료 | 모든 Spot field가 `text8`·`optional-text8`, generated drift 0, Redis `location-authority-hybrid-v3` fixture와 legacy arbitrary binary 거부 | Service wire target field를 `spotId` 계열과 UTF-8 text로 바꾸고 Redis v3 key·claim field fixture를 작성했다. Candidate `342cd38221`에서 WIRE는 40 commands·167 types와 negative self-test 233건을 통과했다. |
| `V11-CA-SPOT-ID-IFACE` | 다섯 언어 exact interface SpotId 전환 | public contract lanes, `P-DEEP` | `V11-CA-SPOT-ID-SPEC` | 완료 | .NET·Java·Kotlin·Node는 string, C++는 `std::string`이며 Spot 파생 public member에 RoutingId 사용 0 | 공통 이름을 `SpotId` 계열로 바꾸고 다섯 언어의 SpotRef·messaging·manager·context·Location·monitoring 선언을 string projection으로 전환했다. 후속 audit에서 Java generated inventory의 `SpotRef`, manager `getOrCreate`·`find`, Actor `joinSpot`과 Kotlin Spot send/request extension에 남은 RoutingId signature 6건을 발견해 `String`으로 교정했다. 2026-07-26 재대조에서는 accessor가 `String spotId`인데 constructor inventory만 `RoutingId`로 남은 Java `ZLinkActorLocation`·filter와 `ZLinkSpotLocation` 3건도 교정했다. Node transport identity의 NodeRid만 RoutingId를 유지한다. |
| `V11-CA-SPOT-ID-E2E` | SpotId E2E 계약과 compatibility negative 갱신 | E2E coordinator, `P-DELIVERY` | `V11-CA-SPOT-ID-SPEC`, `V11-CA-SPOT-ID-WIRE` | 완료 | cross-node global ID, UTF-8 boundary·case exact·no normalization, reserved Entry 형식, legacy binary reject scenario 누락 0 | Common E2E `SM-A13`과 catalog `M93`에 1·255-byte 성공, 256-byte 선거부, case·Unicode exact distinction, reserved Entry 형식과 invalid UTF-8 legacy frame의 side-effect 없는 거부를 고정했다. |
| `V11-CA-SPOT-ID-REVIEW` | SpotId 흡수 완전성·교차 일관성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-SPOT-ID-WIRE`, `V11-CA-SPOT-ID-IFACE`, `V11-CA-SPOT-ID-E2E` | 완료 | spec·wire·Store·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | Candidate `342cd38221`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet focused review가 actionable finding 0으로 `CLEAN`을 반환했다. Runtime 전환은 `V11-M6B-ENTRY-IDENTITY-*`가 소유하며 계약 미소유 항목은 0이다. |
| `V11-CA-ACTOR-CREATE-SPEC` | Actor 생성 승인·거절과 Entry Spot lifecycle 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | 최초 생성·User→Entry 복귀·maintenance 복원이 서로 다른 callback을 사용하고 Entry Spot에 admission callback 노출 0 | Shared membership lifecycle, User Spot admission lifecycle과 Entry Spot creation·relocation lifecycle을 분리했다. 최초 create callback의 `Rejected`는 해당 attempt만 cleanup하고 별도 operation은 새 reservation과 callback을 실행한다. |
| `V11-CA-ACTOR-CREATE-STORE-WIRE` | Actor creation terminal result와 durable reply | protocol·Location contract lane, `P-DEEP` | `V11-CA-ACTOR-CREATE-SPEC` | 완료 | Created·Rejected terminal result와 opaque reply envelope가 exact source lifecycle·OperationId에 고정되고 Ready publication·rejection cleanup과 원자적으로 닫히며 다른 operation 공유 0 | Service wire command 49와 Actor create terminal union을 추가했다. 같은 source lifecycle·OperationId만 retained semantic terminal을 replay하고 다른 operation은 callback 결과를 공유하지 않도록 Store와 schema에 고정했다. Candidate `342cd38221`의 WIRE 233개 negative self-test가 통과했다. |
| `V11-CA-ACTOR-CREATE-IFACE` | 다섯 언어 Actor manager·Spot lifecycle exact interface 갱신 | public contract lanes, `P-DEEP` | `V11-CA-ACTOR-CREATE-SPEC` | 완료 | Existing·Created·Rejected union과 callback 분리가 다섯 언어에서 같은 의미를 제공 | C++·.NET·Java·Kotlin·Node exact interface에 single-use Create·GetOrCreate call, Existing·Created·Rejected result와 분리된 Entry/User Spot lifecycle callback을 반영했다. DOC·trace의 unclassified·ambiguous·unknown은 0이다. |
| `V11-CA-ACTOR-CREATE-E2E` | Actor creation rejection·Entry lifecycle E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-ACTOR-CREATE-SPEC`, `V11-CA-ACTOR-CREATE-STORE-WIRE` | 완료 | Actor별 callback 직렬화, rejection 뒤 독립 request 재시도, 동일 OperationId terminal replay, rejection cleanup, 일반 복귀와 maintenance callback sequence 누락 0 | Common E2E `SM-A14`와 catalog에 concurrent distinct operation, rejection cleanup·재경쟁, same OperationId replay, Ready visibility와 Entry lifecycle sequence를 추가했다. |
| `V11-CA-ACTOR-CREATE-REVIEW` | Actor creation·Entry lifecycle 흡수 완전성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-ACTOR-CREATE-STORE-WIRE`, `V11-CA-ACTOR-CREATE-IFACE`, `V11-CA-ACTOR-CREATE-E2E` | 완료 | spec·Store·wire·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | Candidate `342cd38221`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet focused review가 command 49, operation-scoped replay, staging visibility, local·remote 동일 후보 규칙과 absolute deadline을 대조해 actionable finding 0으로 `CLEAN`을 반환했다. |
| `V11-CA-ONE-WAY-SPEC` | One-way 반환·오류·terminator naming 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-CA-ACTOR-CREATE-REVIEW` | 완료 | `CA-D72~CA-D73`이 Server Framework·Stream Connector·HTTP Client 정식 spec에 반영되고 result status·`SubmitAsync`·generic `async` terminator 상충 0 | 변경 요청 827행을 검토해 Server, HTTP Client와 Stream Connector 정식 spec에 result-free one-way completion, send timeout까지 capacity 대기와 exceptional completion을 반영했다. Logical Multicast target count의 monitoring 소유권은 후속 `CA-D77`이 제거했다. 미결정 오류는 operation별 기존 not-found kind와 `RuntimeShutdown=36`으로 닫고 세 package naming을 같은 snapshot으로 고정했다. `verify-framework-submit-api.sh --contract`가 languages=5, result_free=1, fanout_overloads=10으로 통과했다. |
| `V11-CA-ONE-WAY-IFACE-DN` | .NET one-way·worker exact interface | .NET contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | Messaging one-way `ValueTask Async`, direct relay `ValueTask`, request·worker·create 결과 유지와 제한된 `Yield`가 일치 | Canonical .NET Server exact interface 8개에서 one-way builder를 `ValueTask Async`, direct relay를 `ValueTask RelayAsync`로 고정하고 submit·publish result/status/detail public type을 제거했다. Actor·Spot create/get-or-create의 제한된 `Yield`와 `RuntimeShutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring 소유도 제거했다. 대상 old symbol 0, code fence 짝수와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-JVM` | Java·Kotlin one-way·worker exact interface | JVM contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | Java `CompletionStage<Void> submit`, Kotlin 전용 `await`·`yield` wrapper와 SpotId String parity가 일치 | Canonical Java 6개와 Kotlin 5개 exact interface에서 one-way를 `CompletionStage<Void> submit`과 Kotlin `await(): Unit`으로 고정하고 public submit·publish result/status/detail type을 제거했다. Request·worker·create result와 제한된 `yield`를 유지하고 Spot address·descriptor·location은 String SpotId, RoutingId는 NodeRid로만 사용하도록 정리했다. `RUNTIME_SHUTDOWN=36`, operation별 기존 not-found mapping, old result·Kotlin alias·Spot RoutingId misuse 0과 code fence·`git diff --check` 통과를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-NODE` | Node.js one-way·worker exact interface | Node.js contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | One-way `Promise<void> submit`, worker `submit`, request·create 결과와 제한된 `yield`가 일치 | Canonical Node.js Server exact interface 5개에서 one-way `submit`·direct `relay`를 `Promise<void>`로 고정하고 public submit·publish result/status/detail type을 제거했다. Actor·Spot create/get-or-create의 제한된 `yield`와 `RuntimeShutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring count도 제거했다. 대상 old symbol 0, code fence 짝수와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-CPP` | C++ one-way·worker exact interface | C++ contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | One-way `task_t<void> submit`, request·worker `submit`, create 결과와 제한된 `yield`가 일치 | Canonical C++ Server exact interface 4개에서 one-way `submit`·direct `relay`를 `task_t<void>`로 고정하고 public submit·publish result/status/detail type을 제거했다. Actor·Spot create/get-or-create의 제한된 `yield`와 `runtime_shutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring count도 제거했다. 대상 old symbol 0, result-bearing `submit`은 create 2건만 남았고 code fence와 `git diff --check`가 통과했다. |
| `V11-CA-ONE-WAY-PACKAGE-IFACE` | Stream Connector·HTTP Client terminator naming exact interface | package contract lanes, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | .NET `Async`, Kotlin `await`, Java·C++ `submit`, Node package별 충돌 없는 naming과 기존 실행 경계가 일치하고 실제 runtime signature 충돌 0 | HTTP Client와 Stream Connector canonical exact interface 전 언어를 같은 naming snapshot으로 정렬했다. .NET은 `Async`·`AsyncRaw`, Kotlin wrapper는 `await`, Java·C++와 Node Stream Connector는 `submit` 계열을 사용한다. One-way는 .NET `ValueTask`, Kotlin `Unit`, Java `CompletionStage<Void>`, Node.js `Promise<void>`로 결과값 없이 완료한다. C++ HTTP Client는 `task_t<void>`를 사용하지만 Stream Connector core는 기존 no-exception·no-coroutine 경계를 보존해 `void submit()`을 유지하고 error event로 실패를 보고한다. Node HTTP Client는 TypeScript generic erasure와 Server builder 상속 때문에 no-argument typed response와 one-way `submit()`을 구분할 수 없으므로 typed response에 `async<T>()`를 유지한다. Request·response와 callback result API는 유지했다. |
| `V11-CA-ONE-WAY-E2E` | One-way admission·error·partial publish E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-SPEC` | 완료 | immediate admission, delayed capacity, timeout·cancellation·shutdown race, target·route error, zero target와 partial multicast scenario 누락 0 | Config 13의 기존 SA-E2E-01~20·SA-REG-01~04를 유지하면서 result-free completion, immediate·delayed admission, bounded pending, timeout·cancellation·shutdown race와 late admission 차단, target·route exception, classic fanout subscriber 0과 Logical Multicast target 0 정상 완료, partial publish rollback·retry 금지를 반영했다. 후속 `CA-D77`에 따라 Config 7·13은 publish 전용 snapshot·metric·runtime event·message-flow count 부재를 검증한다. Legacy status/result 표현 0, scenario 20+4 유지와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-REVIEW` | One-way 정식 흡수와 package parity 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-ONE-WAY-IFACE-DN`, `V11-CA-ONE-WAY-IFACE-JVM`, `V11-CA-ONE-WAY-IFACE-NODE`, `V11-CA-ONE-WAY-IFACE-CPP`, `V11-CA-ONE-WAY-PACKAGE-IFACE`, `V11-CA-ONE-WAY-E2E` | 완료 | 원문 항목의 spec·interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | Codex high 최종 검토에서 정식 spec·다섯 exact interface·세 package naming, result-free terminal, bounded source admission과 publish 전용 result·monitoring·trace 제거를 확인했다. `verify-framework-submit-api.sh --contract/--implementation`, `verify-framework-doc-contracts.sh`, .NET LogicalMulticastSubmitTests 6건, Java publisher·admission·submitter test, Node focused contract test 25건, C++ async-task·messaging·message-flow test가 통과했다. Config 13의 오래된 terminal·channel 호출을 다섯 runner/test 파일에서 수정하고 .NET·Java·Node·C++ build를 통과했다. Claude Sonnet은 C++ HTTP forwarding handler 6곳의 blocking `.submit().result()`를 확인했고 `task_t<submit_response_t>`와 `co_await`를 사용하는 내부 helper로 수정한 뒤 C++ target build와 `git diff --check`가 통과했다. 2026-07-25 최종 follow-up에서 result-free terminal, publish count·monitoring 제거, 제거된 public `SubmitAsync` 0, C++ HTTP nonblocking forwarding과 package naming을 다시 대조해 `CLEAN`을 반환했다. |
| `V11-CA-ONE-WAY-DRAFT-RETIRE` | One-way 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-REVIEW` | 완료 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | 2026-07-25 두 reviewer clean 뒤 임시 변경 요청 문서를 제거하고 contract inventory의 `temporary_review_documents`에서도 제외했다. 정식 spec·exact interface·E2E·ledger만으로 계약과 구현 owner를 추적할 수 있다. |
| `V11-M6-ONE-WAY-CPP` | C++ Server runtime의 result-free one-way admission | C++ runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-CPP` | 완료 | 반환 데이터 0, bounded admission·single terminal contract test 통과 | Direct handoff와 saturation·timeout·recovery를 유지하면서 Logical Multicast caller를 worker dequeue 시점에 완료하고 target 지연·실패는 내부에서 관측하도록 분리했다. Publish message-flow trace를 제거하고 classic fanout metric은 channel 경로에서만 기록한다. Async task·messaging·message-flow focused executable과 독립 final review가 통과했다. |
| `V11-M6-ONE-WAY-DN` | .NET Server runtime의 result-free one-way admission | .NET runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-DN` | 완료 | 반환 데이터 0, bounded admission·single terminal contract test 통과 | Bounded capacity·multicast timeout·single-use 48개를 유지하면서 Logical Multicast caller를 worker commit 직후 완료하고 target 지연·실패를 generic runtime error sink로 분리했다. Logical Multicast focused 6/6, Framework build와 독립 final review가 통과했으며 publish message-flow trace는 없다. |
| `V11-M6-ONE-WAY-JVM` | JVM Server runtime의 result-free one-way admission | JVM runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-JVM` | 완료 | 반환 데이터 0, bounded admission·Kotlin parity contract test 통과 | Bounded multicast handoff·Kotlin Spot/metadata/error parity를 유지하면서 caller를 worker commit 직후 완료하고 target 처리 실패는 caller terminal과 분리했다. Target별 monitoring projection과 Logical Multicast·classic fanout Publish trace를 제거했으며 publisher focused test와 독립 final review가 통과했다. |
| `V11-M6-ONE-WAY-NODE` | Node.js Server runtime의 result-free one-way admission | Node.js runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-NODE` | 완료 | 반환 데이터 0, bounded admission·configured timeout contract test 통과 | Bounded waiter·payload 없는 overflow deadline·mesh별 configured timeout을 유지하면서 publish worker slot 확보 직후 caller를 완료하고 target Promise의 지연·실패와 late abort를 분리했다. Logical Multicast focused 9/9, build와 독립 final review가 통과했고 Publish success trace를 제거했다. |
| `V11-M6-ONE-WAY-PACKAGES` | Stream Connector·HTTP Client naming·adapter 구현 | package implementation lanes, `P-DELIVERY` | `V11-CA-ONE-WAY-PACKAGE-IFACE` | 완료 | exact interface·source·package consumer에서 제거된 terminal과 실제 runtime signature 충돌 0 | .NET HTTP Client·Connector test 63개·141개, Java·Kotlin package 4개 Gradle test, C++ HTTP 57개·sample 4개 build가 통과했다. Node Connector와 HTTP server one-way는 `Promise<void>`, raw response는 `submitRaw`, typed response·callback은 CA-D73 예외의 `async<T>`로 맞췄으며 전체 build, HTTP 36/36과 packaged contract 7개가 통과했다. C++ Connector는 no-coroutine `void submit()`을 유지한다. |
| `V11-CA-ONE-WAY-RUNTIME-JOIN` | One-way contract·runtime·package 합류 | amendment coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-DRAFT-RETIRE`, `V11-M6-ONE-WAY-CPP`, `V11-M6-ONE-WAY-DN`, `V11-M6-ONE-WAY-JVM`, `V11-M6-ONE-WAY-NODE`, `V11-M6-ONE-WAY-PACKAGES` | 완료 | 정식 계약, 네 runtime, Kotlin wrapper와 Connector·HTTP package consumer gap 0 | 2026-07-25 정식 계약, 다섯 exact interface, 네 Server runtime, Kotlin wrapper, Connector·HTTP package consumer와 E2E가 동일한 result-free·bounded admission 계약으로 합류했다. Codex high와 Claude Sonnet 최종 review, 언어별 focused test와 submit contract·implementation verifier가 모두 통과했다. |
| `V11-CA-DEFERRED-JOIN-SPEC` | Deferred Actor Join 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | Defer barrier, Yield 구분, completion durability, bounded aggregate commit과 오류 37..39 상충 0 | Process-local handler activation과 여러 Actor intent all-or-none, cross-node Accepted만 durable하고 same-node·Rejected·precommit Failed는 process lifetime인 범위, 64 Join·8 MiB cap, transition race와 bounded aggregate commit을 22·23·40장에 확정했다. Core-native 경로의 미구현은 목표 계약을 축소하지 않고 implementation gap으로 분리했다. |
| `V11-CA-DEFERRED-JOIN-IFACE-DN` | .NET deferred Join·Context exact interface | .NET contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 완료 | Context-only factory, `Defer`, completion과 MessageContext target signature 누락 0 | Context-only factory, 동기 `Defer`, non-zero 128-bit completion과 공통·specialized MessageContext signature를 canonical/common exact interface에 고정했다. TRACE와 DOC gate가 통과했고 Sonnet semantic review가 parity를 확인했다. |
| `V11-CA-DEFERRED-JOIN-IFACE-JVM` | Java·Kotlin deferred Join·Context exact interface | JVM contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 완료 | Java 동기 `defer`, Kotlin coroutine wrapper 0, Context·completion·error parity 누락 0 | Java는 동기 `defer()`만 제공하고 Kotlin은 Join terminal wrapper를 추가하지 않는다. Completion callback만 suspending bridge를 제공하며 오류 37..39와 Context signature parity를 TRACE·DOC gate와 Sonnet review로 확인했다. |
| `V11-CA-DEFERRED-JOIN-IFACE-NODE` | Node.js deferred Join·Context exact interface | Node.js contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 완료 | Promise를 반환하지 않는 `defer`, containing Spot handler와 Context·completion parity 누락 0 | Promise를 반환하지 않는 `defer()`, containing Spot·Actor·MessageContext handler와 completion union을 canonical/common exact interface에 고정했다. TRACE·DOC gate와 Sonnet review가 통과했다. |
| `V11-CA-DEFERRED-JOIN-IFACE-CPP` | C++ deferred Join·Context exact interface | C++ contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 완료 | `void defer()`, Context-only factory, completion variant와 message context parity 누락 0 | `void defer()`, Context accessor·인자 없는 configure, completion variant와 MessageContext signature를 고정했다. 남아 있던 `join.async()` 설명을 제거했고 TRACE·DOC gate와 Sonnet review가 통과했다. |
| `V11-CA-DEFERRED-JOIN-E2E` | Deferred Join·queue·Context·error E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-DEFERRED-JOIN-SPEC` | 완료 | handler terminal·failure, Yield, queue hold, completion, same/cross-node Context와 오류 parity scenario 누락 0 | Config 8 TD-E1~E3와 Config 10 ST-H1~H5에 handler terminal·failure, 두 Actor intent all-or-none, 64·8 MiB, crash 위치별 durable 범위, Context fence, 오류 37..39와 queue ordering을 반영했다. DOC gate와 Sonnet review가 통과했다. |
| `V11-CA-DEFERRED-JOIN-REVIEW` | Deferred Join 변경 요청 흡수 완전성 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E` | 완료 | 변경 요청 항목의 spec·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | P0 세 결정은 정식 계약에 확정했다. Sonnet 보강 review와 Codex 5.6 sol high follow-up이 CLEAN이다. Review 중 Node terminal, C++ Request·Join 설명, exception·cancellation E2E 분리, callback owner, .NET guide의 Defer·Entry lifecycle·SpotId 잔존을 교정했고 DOC·TRACE gate가 통과했다. |
| `V11-CA-OBJECT-CONTEXT-REVIEW` | Actor·Spot Context composition 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP` | 완료 | Context-only factory, exact identity, same/cross-node generation·fence와 containing Spot thread-safety review clean | Codex 5.6 sol high가 C++ Context factory·move-only handle, Node containing Entry Spot, Kotlin read-only Context projection, 공통 Actor·Spot composition과 mismatched Context no-Ready E2E gap을 찾아 교정했고 follow-up은 CLEAN이다. DOC·TRACE도 통과했다. 2026-07-25 Claude Sonnet 최종 review도 CLEAN이며 Context-only factories, immutable identity의 SpotId·ObjectGeneration·MeshName·NodeRid, Actor Context와 ActorRef의 역할 구분, containing Spot·Entry Spot, Kotlin parity와 mismatched Context rejection을 확인했다. |
| `V11-CA-MESSAGE-CONTEXT-REVIEW` | MessageContext naming·field parity 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E` | 완료 | nullable Mesh·Channel, correlation, cancellation ownership, specialized context와 제거 surface review clean | Codex 5.6 sol high가 legacy marker context, Route target field, Java generated return 충돌, .NET Publish·HandlerInvocation, C++ containing Spot signature와 Kotlin generated signature gap을 확인했다. 다섯 exact mirror, ST-H5와 DOC verifier를 수정했고 Codex follow-up, TRACE·DOC·submit API gate가 CLEAN이다. Claude Sonnet 최종 review에서 .NET guide의 이전 per-operation context와 Actor request reply option 잔존을 찾아 `IZLinkMessageContext`·`ZLinkRouteMessageContext`·`IZLinkFanoutHandler`와 result-free `Async`로 교정했다. Follow-up은 marker context·standalone Actor handler·reply option·`SubmitAsync` 잔존 0을 확인하고 `CLEAN`으로 판정했다. |
| `V11-M6-DEFERRED-JOIN-CPP` | C++ runtime deferred Join 전환 | C++ runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Public `actor_t`·`actor_factory_t<TActor>`·completion variant와 exact factory registration을 추가했다. User Spot admission reply와 prepare·finalize commit envelope는 source-generated non-zero 128-bit operation ID, immutable completion root reference와 checksum을 전달한다. Target은 materialization 전에 root를 검증하고, process-local admission이 없으면 root 또는 exact Actor authority의 최신 cursor에서 pending admission과 target state를 복구한다. Location commit 뒤 `Committed` root를 generation-preserving authority CAS로 publish하고 Actor callback 성공 뒤 `Delivered` root로 전환한다. Backlog 제출 뒤 authority reference를 해제하고 root를 정리하며 callback failure에서는 같은 operation ID와 committed root를 유지한다. Entry·PerActor handler는 Actor별 serial gate에서 실행하므로 result-free `defer()`가 open Framework turn을 사용하고, shared Spot handler의 Spot별 gate와 분리된다. Public-host source·target transfer는 exact target generation을 검증하고 target Actor를 materialize한 뒤 prepare·commit·activate를 수행한다. 연결 해제와 겹친 stale `connection_ready` HELLO 전송 실패는 해당 connection mapping만 제거해 `errno=113` process 종료를 막는다. Wire codec·authority publish/update/release·replacement recovery·generation fence test를 포함한 execution·header contract·M6B·M6C·messaging focused 5/5가 통과했고 SpotActorTransfer의 ActorNode·Client·Session도 build된다. `ST-A1`은 same-node에서 두 번 통과했으며(`framework/languages/cpp/e2e/SpotActorTransfer/logs/20260725-103726-153046`, `framework/languages/cpp/e2e/SpotActorTransfer/logs/20260725-103750-202016`), remote 실행도 source `leave`, target `transfer_in`·`joined`, non-zero `join_completion_accepted`, 이후 packet handler를 확인하고 통과했다(`framework/languages/cpp/e2e/SpotActorTransfer/logs/20260725-104044-288231`). Compatibility alias와 Core·bindings 변경은 추가하지 않았다. 2026-07-25 §2.3 교차 리뷰 2차(C++·.NET·Node 구조 전수 조사, JVM 조사 진행 중)에서 다음 축을 대조했다. (1) ordering 축은 이미 동형이다. 세 언어 모두 deferred join을 제출한 실행 문맥에 등록 순서로 올리고 다음 큐 turn보다 먼저 실행한다. C++는 turn의 deferred 목록을 `complete_one`에서 배수하고, .NET은 제출 turn에 post하며, Node는 handler turn 종료 시점에 순차 실행한다. (2) durable completion 축도 동형이다. 세 언어 모두 prepared→committed→delivered 단조 cursor를 immutable root와 authority CAS로 전진시키고 재전달을 idempotent하게 만든다. (3) 오류 관찰 축은 **동형이 아니다**. C++는 `actor_join_completion_t`의 rejected·failed variant를 런타임에서 한 번도 생성하지 않아 계약이 전달 불가능한 분기를 광고하고, .NET은 인식하지 못한 예외를 `RequestFailed`·retriable=false로 접으며, Node는 같은 경우를 `RequestFailed`·retriable=true로 접는다. 즉 같은 미분류 실패가 한 언어에서는 재시도 대상, 다른 언어에서는 아니다. (4) 단일 pending join 방어 축도 동형이 아니다. .NET은 actor state의 CAS 플래그, C++는 coordinator의 move phase, Node는 state의 `isMoving`과 module-level WeakSet 두 개를 함께 쓴다. 네 번째 조사(JVM)까지 받아 축별 최선안을 확정했다. **ordering 최선안 = JVM 형태.** Java는 `defer()` 시점에 대상 actor mailbox에 barrier 슬롯을 먼저 예약하고(큐 선점) handler scope가 정상 종료할 때만 활성화하며, 실패 시 예약된 슬롯을 no-op으로 해소한다. Spot handler가 소속 actor의 join을 defer하는 교차 mailbox 경우를 다루는 유일한 형태다. .NET은 같은 예약 개념을 `BarrierReservation`으로 갖고 있고 C++·Node는 없다. 정렬 대상은 C++·Node다. **durable completion 최선안 = .NET·Node 형태.** 세 상태 cursor를 모두 쓰되 delivered 뒤 root를 명시적으로 회수한다. JVM은 `CURSOR_COMMITTED` 하나만 허용하고 root를 지우지 않아 성공한 join이 24시간 retention까지 store 항목을 남긴다. 정렬 대상은 JVM이다. **오류 관찰 최선안 = .NET·JVM 형태.** 미분류 실패는 `RequestFailed`·비재시도로 접는다. Node는 같은 경우를 재시도 가능으로 접으므로 정렬 대상이고, C++는 `rejected`·`failed` variant를 실제로 생성해야 하므로 정렬 대상이다. **단일 pending join 방어 최선안 = .NET 형태.** Actor state가 소유한 CAS 플래그 하나로 판정한다. Node는 state의 `isMoving`과 module-level WeakSet 두 개를 함께 쓰고, JVM은 runtime 인스턴스와 무관한 `static` claim map을 써서 같은 JVM의 두 runtime이 actorId만 같으면 서로를 막는다. 정렬 대상은 Node·JVM이다. **test 구성 최선안 = .NET 형태.** 계약 shape, durability cursor, resource 수명 negative, ordering을 각각 별도 test가 덮는다. JVM은 988줄짜리 join call에 직접 test가 없고 integration suite가 제거된 타입을 참조해 컴파일되지 않으며, C++는 join state machine을 e2e로만 덮고 contract test는 소스 문자열 grep이다. 정렬 대상은 JVM·C++다. 네 언어 공통으로 남은 POSD 부채는 accept·reject 표현의 다중화(JVM 4종, .NET은 Service 계층 병렬 어휘), 도달 불가능한 구현(Node의 two-phase join과 그것을 설명하는 coordinator 주석), 단일 TU 잔류(C++ 6,744줄)이며 각 lane row에서 별도로 처리한다. 2026-07-25 후속 실행에서 "C++가 아직 제거된 result-bearing join 형태에 남아 있다"는 전제를 1차 자료로 검증해 기각했다. C++ exact interface `05-actors.ko.md` 41~74행이 `actor_join_accepted_t`·`actor_join_rejected_t`·`actor_join_failed_t`와 `actor_join_completion_t`, `on_join_completed`를 선언하고 `04-spots.ko.md` 326~337행·696~701행이 `timeout()`+`void defer()`만 갖는 `actor_join_call_t`를 선언하며 runtime `contracts/actors/actor.hpp`가 이미 그 형태다. .NET에서 제거된 것은 `ZLinkActorJoinResult`이고 C++의 두 이름은 .NET이 유지하는 `ZLinkActorJoinCompletion`의 세 항에 대응하므로 이름만 겹치는 false friend였다. 실제 결함은 `test_cpp_framework_target_contract`의 `CPP-G0-ACTOR-002` gate가 authority의 부정을 assert하고 있었던 것이며 gate를 exact interface로 다시 pin해 닫았다(실행 assertion 5개에서 15개로, `gate.require` 호출 지점 2곳에서 7곳으로 증가, runtime·public header 무변경). 같은 build의 gate 실패는 3건에서 1건으로 줄었고 전체 `ctest -j4`는 변경 전후 모두 42 pass·4 fail(46)이다. 남은 1건 `CPP-G0-SPOTHANDLE-001`은 v11 정식 계약이 유지하는 `spot_ref_t`를 금지하고 정식 계약에 존재하지 않는 `spot_handle_t`를 요구하는 별개 contract family 문제다. 관련 blocked issue는 `BLK-021`과 `BLK-022`다. 이 row의 다음 단계는 오류 관찰 축 정렬이다. 현재 `actor.hpp` 742행의 factory registration이 `actor_join_accepted_t`만 만들고 join 실패는 deferred turn 안에서 throw로 끝나 `rejected`·`failed` variant가 한 번도 전달되지 않는다. 해결 형태는 별도 전달 경로를 추가하는 것이 아니라 `spot_runtime.hpp` 123행의 erased completion callback signature를 넓혀 2946·3968·4029행 세 call site가 rejection과 미분류 실패를 각각 `actor_join_rejected_t`·`actor_join_failed_t`(비재시도)로 전달하게 하는 것이며, state machine 변경이므로 자체 before·after 증거와 함께 별도로 진행한다. |
| `V11-M6-DEFERRED-JOIN-DN` | .NET runtime deferred Join 전환 | .NET runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-REVIEW` | 수정 진행 | process-local barrier·completion durability·queue ordering test 통과 | Public Join을 동기 `Defer`와 `OnJoinCompletedAsync` completion으로 전환했다. Cross-node Accepted completion은 Relocation Store immutable root에 operation ID·raw reply·ActorRef·generation과 `Prepared→Committed→Delivered` cursor를 기록하고 target Actor mailbox에서 callback을 실행한다. Actor bind recovery, callback retry, authority reference 해제 뒤 root delete를 검증했으며 focused durability·mailbox·contract 6/6과 Framework build가 통과했다. 전체 767 pass/17 fail의 실패는 다른 진행 항목이다. 2026-07-25 자율 실행에서 deferred Join이 detached 실행 때문에 제출 callback의 causal flow를 잃던 결함을 근본 수정했다. 제출 시점 `ZLinkFlowContext.Current`를 capture해 `RunAsync`에서 같은 flow로 재진입하며, 변경 path는 `framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkDeferredActorJoin.cs`다. 단독 실행에서는 `Flowless_Actor_Stream_Ingress_Creates_One_Inbound_Flow_For_Join_And_Reply`가 통과하지만 전체 병렬 실행에서는 detached join 완료 순서 때문에 간헐 실패가 남아 `BLK-003`으로 추적한다. 전체 cross-node Actor state·queue·timer materialization과 callback 기반 sample/E2E 전환은 상위 relocation gap으로 남아 있다. §2.3 교차 리뷰(2026-07-25)에서 네 언어의 deferred Join 활성화 형태를 비교했다. C++는 `turn->defer()`로 같은 serial execution queue의 `_deferred_after_active`에 post하고, Java는 `ZLinkDeferredActorJoinScope.Scope.finish()`가 handler stage 뒤에 intent를 순차 체인하며, Node는 `activateAfterHandlerTerminal()`이 `setImmediate` 뒤 등록 순서대로 실행한다. .NET만 `TryRunDetached`로 순서 보장 없이 분리 실행했다. ownership·ordering 축의 최선안을 '제출한 실행 문맥에 등록 순서대로 post'로 정하고 .NET을 `ZLinkSerialTurn.TryPost`로 정렬했다(queue 거부 시에만 detached fallback). Deferred Join 관련 focused test 7/7이 통과한다. '완료 가시성' 축은 비결합(handler terminal 이후 비동기 실행)을 기준안으로 확정했다. C++·Node·.NET이 비결합이고 Node가 그 근거를 코드에 명시하며, 결합형은 원격 join 완료가 dispatch 수명과 actor mailbox turn을 잡아두는 위험이 있다. Java만 intent를 handler stage에 체인하는 결합형이라 JVM lane의 동형 수렴 대상으로 남는다. 2026-07-25 ordering checkpoint에서 cross-node 순서를 `target authority CAS → target joined callback → commit reply → source cutover·leave → cleanup → trailing queue replay`로 고정했다. Post-authority callback 실패는 source rollback이 아니라 target handoff phase가 재시도하며, same-node는 Spot activation이 stage·Location commit·target callback·source leave를 단독 소유한다. 단순 await 재배치와 explicit handoff phase 두 안을 비교해 crash·retry 상태를 표현하는 후자를 선택했다. ActorHandoff 33/33, DeferredActorJoin 6/6, ActorTransfer 4/4, EntrySpotActorDispatch 72/72와 source build가 통과했다. User Spot cross-node Join은 이미 application state·accepted journal·bound session route·completion cursor를 하나의 immutable root에 저장하고 startup recovery에서 같은 target generation으로 재개한다. 2026-07-26에는 targetless `JoinEntrySpot`이 Serving·actor stable type·capacity·positive weight를 만족하는 descriptor의 `EntrySpotId`를 선택하고, 다른 node이면 같은 `JoinActorAsync` authority CAS·relocation transaction을 사용하도록 연결했다. Deferred intent의 128-bit `OperationId`도 이 경로에 전달한다. Completion root를 새 immutable root로 교체해도 application state·accepted journal·logical timer·recovery payload와 `ObjectGeneration`이 보존되는 regression을 추가했다. Focused Deferred Join 7/7, 전체 Unit 941/941, Contract 65/65가 통과했다. Runtime 구현은 닫혔지만 callback 기반 sample/E2E와 새 reference gate가 남아 있어 행 상태는 진행으로 유지한다. |
| `V11-M6-DEFERRED-JOIN-JVM` | JVM runtime deferred Join 전환 | JVM runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Java public Join을 `timeout`+동기 `defer`로 전환했다. User·Entry Spot와 timer handler의 여러 member Actor intent를 한 terminal scope로 묶고 active turn 직후 기존 queued application turn보다 먼저 Actor mailbox barrier를 실행한다. Routed cross-node User Spot Accepted는 Relocation Store root와 commit wire에 operation ID·raw reply·generation·cursor를 보존하고 target mailbox callback, retry·dedupe와 backlog 순서를 적용했다. Java core 517/517와 Kotlin projection test가 통과했다. Core-native `JoinEntrySpot`·native Spot path는 Framework operation ID와 manifest를 전달할 wire field가 없어 Core·bindings 변경 없이 durable target replay를 닫을 수 없으며 callback 기반 sample/E2E 전환도 남아 있다. §2.3 교차 리뷰(2026-07-25)에서 '완료 가시성' 축 기준안이 비결합(handler terminal 이후 비동기 실행)으로 확정됐다. `ZLinkDeferredActorJoinScope.Scope.finish()`가 intent를 handler stage에 순차 체인하는 결합형이라 이 축의 동형 수렴 대상이며, 수렴 시 deferred join 실패가 terminal reply나 dispatch 수명에 영향을 주지 않는지 Java·Kotlin regression으로 확인한다. 같은 날 수렴을 완료했다. `finish()`의 성공 경로에서 handler 자체는 성공했는데 이후 join tail(`intent.operation.get()`의 체인)이 실패하면 그 join 오류가 `finish()`가 반환하는 stage 전체를 실패로 덮어써 `runTurn`·`ZLinkDeferredActorJoinHandlerScope.run`의 `completed`가 handler의 성공 값 대신 join 오류로 끝나던 결합 지점을 제거했다. 각 intent는 이제 개별적으로 outcome을 흡수한 뒤(`neutralize`) 다음 intent로 넘어가며, 등록 순서·Actor mailbox barrier 순서·claim 해제 시점은 그대로 유지한다. Node의 `await` 뒤 capture한 handler 결과 반환·오류 swallow, .NET의 독립 post·`OnJoinCompleted` 단독 오류 통지와 같은 비결합 형태로 수렴했다. 수렴 과정에서 별도 결함을 하나 더 발견해 같이 고쳤다. 기존 코드는 tail 체인 전체가 끝난 뒤에만 오류를 삼켰는데, `thenCompose`는 앞선 intent가 실패하면 뒤 intent의 `operation.get()`을 아예 호출하지 않는다. Cross-actor barrier 대상 intent는 `register()` 시점에 이미 target actor 큐에 `enqueueBarrierNext`로 activation-gated entry를 예약해 두므로, 앞선 intent 실패로 뒤 intent의 `operation.get()`이 호출되지 않으면 그 activation latch(`activation.complete(true)`)가 영원히 안 열려 target actor 큐가 그 turn에서 막힌다. 그래서 흡수는 체인 끝이 아니라 intent 단위로 옮겼다. 회귀는 `ZLinkDeferredActorJoinScopeTest`에 두 시나리오를 추가해 고정했다: 직접 등록한 join 하나가 실패해도 handler 자신의 성공 값이 살아남고 등록 순서의 다음 join이 실행되는지, barrier 등록 join 하나가 실패해도 다음 barrier가 activate돼 그 target actor 큐가 다음 turn을 처리하는지(고치기 전 코드로 되돌리면 두 테스트 모두 실패로 재현됨을 확인). 최종 `:zlink-framework-core:test` 528/528, `:zlink-framework-kotlin:test` 그린이다. Kotlin projection(`ZLinkCoroutineInvocationContext`)은 같은 Java `ZLinkDeferredActorJoinScope`의 ThreadLocal 상태를 그대로 참조하는 얇은 계층이라 별도 수정이 없다. 이 항목으로 '완료 가시성' 축의 JVM 결함은 닫혔으나, 같은 §2.3 교차 리뷰가 남긴 '오류 관찰'·'단일 pending join 방어' 두 축은 아직 4언어 동형이 아니며 `V11-M6-DEFERRED-JOIN-CPP`·`-DN` row가 JVM 조사 결과를 받은 뒤 최선안을 확정하는 별도 작업으로 남아 있다. |
| `V11-M6-DEFERRED-JOIN-NODE` | Node.js runtime deferred Join 전환 | Node.js runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Public Join의 result-free `defer`와 handler-local barrier에 더해 cross-node Accepted completion을 구현했다. Source operation ID를 remote admission wire로 전달하고 target은 Accepted reply 전에 Relocation Store immutable root와 Actor authority reference에 operation ID·raw reply·ActorRef·generation·`Prepared→Committed→Delivered` cursor를 기록한다. Owner·membership commit과 backlog replay 뒤 target Actor mailbox에서 callback을 실행하며 실패 시 같은 operation ID로 재시도하고 Delivered cursor로 중복 실행을 막는다. Replayed Joined lifecycle은 authority가 가리키는 root를 다시 읽어 target process 교체 뒤 delivery를 재개한다. Redis provider는 opaque relocation bytes, CRC32C와 provider-clock expiry를 구현했다. SpotActorTransfer host는 public object role, `defer`, `onJoinCompleted`, `SpotId`, `find`와 fluent create call로 전환했다. Lazy Entry Spot activation도 creation callback 전에 materialize해 initial state를 보존한다. Node build, focused deferred durability·mailbox 5/5와 Redis relocation 1/1이 통과했다. Process `ST-B1`은 public placement로 실제 cross-node 대상과 source state 21 capture까지 확인했지만 target admission 전에 native join이 completion `failed(actorRouteNotFound)`로 끝났다(`log/20260725-080443-174162`). 이 native join/runtime gap과 재시작 recovery process 증거가 남아 있으므로 상태는 진행으로 유지한다. Compatibility alias는 추가하지 않았다. 관련 blocked issue: `BLK-005`. |
| `V11-M6-OBJECT-CONTEXT-CPP` | C++ object Context composition | C++ runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 진행 | Context-only factory·identity·source fence test 통과 | Actor Context에 ActorId·ObjectGeneration·MeshName accessor를 추가하고 typed Actor factory가 반환한 Actor의 Context가 전달한 gateway state·NodeRid·stable type·ActorId·ObjectGeneration·MeshName과 모두 같은지 Ready 등록 전에 검증한다. User·Entry·Instance Spot에는 Context-by-value factory overload와 전용 `instance_spot_context_t`를 추가했으며, target user-spot materialization의 ObjectGeneration·MeshName을 Context에 설치한다. Context factory가 받은 MeshName·NodeRid·SpotId·ObjectGeneration 검증과 다른 Context를 반환했을 때 side effect 없이 `spot_create_failed`로 거부하는 focused test 2건, Actor identity·source fence Unit, contract header와 target contract gate가 통과했고 store/location resolver 전체 41/41도 통과했다. 남은 완료 조건은 public Context의 default/copy/move-assignment 제거, `spot_t<TActor>`·`entry_spot_t<TActor>` exact base 전환과 이에 따른 C++ sample·E2E fixture 갱신이다. 이 항목을 끝내기 전까지 `IMP-CTX-1`의 C++ 표기를 유지한다. |
| `V11-M6-OBJECT-CONTEXT-DN` | .NET object Context composition | .NET runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 완료 | Context-only factory public surface contract test 3건과 creation·transfer·relocation focused Unit 4건 통과. Framework source build 0 warning·0 error. Factory 호출 전 exact identity 구성, factory가 받은 Context reference 검증, same-node SpotId 갱신과 source operation typed fence를 구현했고 invalid Actor Context Unit fixture를 공통 exact fixture로 교체했다. POSD 대안은 mutable pre-bind setter와 ActorId alias를 유지하는 방식, framework가 identity를 먼저 완성하고 Context 하나로 전달하는 방식을 비교했으며 시간 결합과 identity 중복을 없애는 두 번째 방식을 선택했다. | 2026-07-26 완료 감사에서 전체 Unit 940/940과 Contract 65/65를 다시 통과했다. 이전 Location projection fixture compile gap은 해소됐으며 `IMP-CTX-1`의 .NET 표기는 구현 차이 문서 정리 대상으로 이동한다. |
| `V11-M6-OBJECT-CONTEXT-JVM` | JVM object Context composition | JVM runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 완료 | Context-only factory·identity·source fence test 통과 | `ZLinkActorFactory.create(ZLinkActorContext)`만 남기고 runtime이 Context에 Actor ID·object generation·Mesh name을 설치한 뒤 factory를 호출한다. `ZLinkActor.actorId()`를 제거하고 identity는 `actor.context().actorId()`를 단일 기준으로 사용한다. `ZLinkFrameworkLocationRuntimeTest`가 `actorId=player-join`, `objectGeneration=1`, `meshName=rooms`와 lifecycle을 검증했고 `ZLinkActorTransferRegistryTest`가 다른 Context identity를 거부하는 source fence를 검증했다. Java/Kotlin compile, JVM 전체 `testClasses`, 관련 Core·Kotlin focused test가 통과했다. |
| `V11-M6-OBJECT-CONTEXT-NODE` | Node.js object Context composition | Node.js runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 완료 | Context-only factory·identity·source fence test와 Actor Manager contract fixture 전환 통과 | `ZLinkActorFactory.create(context, signal?)`만 사용하고 `ZLinkActor.actorId`, Actor Context의 handler·leave·mutable Spot·ActorRef projection을 제거해 `actor.context.actorId`를 identity의 단일 기준으로 사용한다. User·Entry·Instance Spot Context에는 Core 또는 activation authority에서 받은 `objectGeneration`과 생성 시점의 Mesh·Spot·Node identity를 설치하고 네 identity field를 immutable로 고정했으며 Actor leave는 containing User Spot Context가 수행한다. 다른 Context를 반환한 Actor factory를 거부한다. 마지막 남은 public implementation projection인 `DefaultZLinkActorContext.actorRef`를 제거하고 native incarnation 검증 fixture는 public manager `find(actorId)` 결과를 사용하도록 바꿨다. Framework·NestJS build와 exact type contract가 통과했고, Object Context 3건과 Actor Manager 74건을 한 process에서 실행해 77/77을 확인했다. Entry serial dispatch 9/9와 M6B 39/39의 기존 증거도 유지된다. Production 우회나 Core·bindings 변경은 없다. |
| `V11-M6-MESSAGE-CONTEXT-CPP` | C++ MessageContext 통일 | C++ runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 완료 | context field·marker 제거와 dispatch test 통과 | Channel request/send, RouteMesh node-direct, Spot packet, Spot subscription과 Spot Actor handler를 `message_context_t`, `publish_message_context_t`, `route_message_context_t`로 통일하고 filter에는 `handler_invocation_t`를 전달한다. 이전 marker context 10개(`handler_context_t`, `request_context_t`, `send_context_t`, `publish_context_t`, `handler_invocation_context_t`, `route_handler_context_t`, `spot_packet_context_t`, `spot_actor_send_context_t`, `spot_actor_request_context_t`, `spot_actor_reply_options_t`)와 `route_handler_context_t::router_channel_id`, `spot_packet_context_t::cancellation_requested`, Actor request reply option field를 source에서 제거했다. Dispatch가 decode한 envelope에서 Mesh name, ChannelName, packet name, content type, forwarding metadata와 correlation ID를 한 번 채우고 publish는 topic·source를, route는 source node RID를 더한다. Route context는 .NET `ZLinkRouteHandlerInvoker`와 같게 router channel id를 Mesh name에 싣고 ChannelName은 비운다. .NET과 다른 형태 2건은 다음 이유로 기록한다. 첫째 `handler_invocation_t`는 C++ exact interface가 정의한 descriptor·message context·message 세 field를 그대로 쓰고 .NET `ZLinkHandlerInvocation`의 `OwnerKind`를 두지 않는다. exact interface가 C++ 계약 기준이기 때문이다. 둘째 공개 context struct는 virtual destructor 없는 aggregate이므로 downcast 대신 runtime private `detail::inbound_message_context_t` carrier를 한 번 채우고 각 typed invoker가 handler가 선언한 정확한 context를 project한다. Spot dispatch carrier `spot_actor_message_metadata_t`는 inbound message field를 갖게 되어 `spot_inbound_message_t`로 이름을 맞췄다. Classic fanout inbound context는 .NET이 Mesh name에 넣는 `"fanout"` 문자열 대신 `std::nullopt`를 쓴다. C++ classic fanout은 Mesh가 아니므로 이 field를 비우는 편이 정확하다. `test_cpp_framework_contract_headers`가 다섯 공개 type의 exact member와 제거한 marker 10개의 부재를 검증하고(같은 이름을 다시 선언하면 ambiguous로 compile 실패), `test_cpp_framework_handler_registry`가 채워진 inbound context를 request·send·publish dispatch에 통과시켜 Mesh name, ChannelName, packet name, content type, metadata, correlation ID, topic과 publish source가 handler까지 도달하는지 확인한다. 같은 test가 Spot packet·subscription·Actor member를 등록해 2-arg subscription branch를 instantiate하고, Actor packet dispatch로 Mesh name·packet name·content type·correlation ID·forwarding metadata 도달과 ChannelName 부재를 확인하며, subscription invoker가 쓰는 두 Spot context projection도 직접 검증한다. `zlink_cpp_framework_mesh_node_vertical_test`는 node-direct route dispatch에서 router channel id가 Mesh name으로 오고 ChannelName은 비며 packet name·wire content type·source node RID가 실린 값으로 전달되는지 확인한다. `test_cpp_framework_channel_messaging.cpp`와 `entry_spot_admission_burst.cpp`는 이 build 구성의 target에 등록되어 있지 않아 type만 맞춰 두었고 증거로 세지 않는다. `.artifacts/v11/build/framework-runtime-regression/cpp`에서 focused 12/12(m6a·m6b·m6c runtime, mesh node vertical, contract headers, layout contract, actor gateway, runtime integration, handler registry, stream framework, message flow, messaging)와 전체 ctest 42/46이 통과했다. 남은 4건은 이 gate와 무관하다. `test_cpp_framework_sample_parity`와 같은 binary를 쓰는 `test_cpp_framework_parity_contract`는 sample source가 `spot_node_manager_t`·`spot_rid_t`처럼 reference revision `95b2c00b40`에도 없던 framework API를 부르기 때문에 이전부터 compile되지 않으며 `V11-SAMPLE-SPEC-FINAL` 소관이라 손대지 않았다. `test_cpp_framework_target_contract`의 실패 3건은 `spot_ref_t` snapshot과 `actor_join_accepted_t`·`actor_join_rejected_t`로 `V11-M6-DEFERRED-JOIN-CPP` 소관이다. `test_unreal_stream_connector`는 `ZLinkStreamConnector.h` 누락으로 connector 소관이다. cpp guide README의 이전 Actor request context 예제도 `message_context_t`로 고쳤고 `scripts/verify-framework-doc-contracts.sh`는 CLEAN이다. Object Context와 Entry identity는 이 작업에서 바꾸지 않았다. |
| `V11-M6-MESSAGE-CONTEXT-DN` | .NET MessageContext 통일 | .NET runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 완료 | context field·marker 제거와 dispatch test 통과 | Production과 UnitTests의 Channel·RouteMesh·Spot subscription·Spot Actor handler를 `IZLinkMessageContext`, `ZLinkRouteMessageContext`, `ZLinkPublishMessageContext`와 `ZLinkHandlerInvocation.MessageContext`로 전환하고 이전 context type과 marker를 제거했다. 공개 contract 파일에는 interface만 유지하고 internal `ZLinkMessageContext` 구현은 runtime 위치에 둔다. Framework와 UnitTests build, exact contract·filter 4/4, contract architecture 1/1, route·Spot·Actor·channel focused dispatch 46/46가 통과했다. 전체 UnitTests는 774/791가 통과했으며 남은 17건은 문서 7건, stateful runtime 4건, flow/publish 3건, transport 2건, HTTP scheduler 1건으로 이 행의 MessageContext gate와 관련이 없다. ObjectContext와 Entry identity는 이 작업에서 변경하지 않았다. |
| `V11-M6-MESSAGE-CONTEXT-JVM` | JVM MessageContext 통일 | JVM runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 완료 | context field·marker 제거와 dispatch test 통과 | Channel·Spot handler를 `ZLinkMessageContext`, publish를 `ZLinkPublishMessageContext`, RouteMesh를 `ZLinkRouteMessageContext`로 통일하고 filter에는 `ZLinkHandlerInvocation.messageContext()`를 전달한다. 이전 공통·send/request/publish/route/Spot Actor context marker 9개를 source에서 제거했다. `HandlerContractTest`가 exact member와 이전 class 부재를 검증하고 `ZLinkChannelRuntimeTest`가 packet·content type·metadata·topic·source RID field를 검증한다. Handler scanner, channel/Spot dispatch, Spring starter와 testkit 일반 test compile, Kotlin suspend handler test가 통과했다. |
| `V11-M6-MESSAGE-CONTEXT-NODE` | Node.js MessageContext 통일 | Node.js runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 완료 | context field·marker 제거와 dispatch test 통과 | Channel·Spot·Actor handler는 `ZLinkMessageContext`, publish는 `ZLinkPublishMessageContext`, RouteMesh는 `ZLinkRouteMessageContext`를 사용한다. Filter invocation은 `ownerKind`와 `messageContext`만 전달하며 request/send/publish/route/Spot Actor marker 8개, Actor request reply option, `connectionAborted`와 `routerChannelId` context field를 production source에서 제거했다. Spot·Entry Actor dispatch는 containing Spot을 첫 인자로 전달하고 correlation·metadata를 보존한다. Framework·NestJS build, exact type contract, unified context runtime 1/1, filter 2/2, Entry dispatch 9/9와 route dispatcher 2/2가 통과했다. 전체 root typecheck에는 별도 M6C authority fixture의 `ZLinkAuthoritySnapshot` 오류 2건만 남아 있다. |
| `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE` | Actor Join 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-DEFERRED-JOIN-REVIEW`, `V11-CA-OBJECT-CONTEXT-REVIEW`, `V11-CA-MESSAGE-CONTEXT-REVIEW` | 완료 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | Deferred Join·Object Context·MessageContext의 Codex high와 Claude Sonnet 독립 review가 모두 clean으로 끝난 뒤 임시 변경 요청 문서를 제거하고 `temporary_review_documents` inventory에서도 제외했다. 정식 spec·다섯 exact interface·E2E·implementation gap과 이 ledger만으로 구현·검증 범위를 추적한다. |
| `V11-CA-SESSION-BINDING-SPEC` | Session–Actor stored route·disconnect 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | no-Store relay, physical automatic all-settled 통지, logical notification과 same-generation route update가 정식 spec에 일치 | `CA-D78`을 30·31·22·23·40·41·42·54장과 implementation gap에 반영하고 목표 계약과 runtime 차이를 분리했다. |
| `V11-CA-SESSION-BINDING-PROTOCOL` | Bound-session route·disconnect formal invariant | protocol lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | command 44·45 completed-only 유지, stored route·lease·generation·disconnect 의미와 schema drift 0 | Service wire internals에 의미를 보강하고 기존 schema의 completed-only command invariant를 대조했다. Wire field와 새 command는 추가하지 않았다. |
| `V11-CA-SESSION-BINDING-IFACE-DN` | .NET exact interface 의미 보강 | .NET contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, automatic·logical disconnect와 route update 의미 일치 | STREAM exact interface를 갱신하고 존재하지 않는 `ActorRef.MeshName` 설명도 교정했다. |
| `V11-CA-SESSION-BINDING-IFACE-JVM` | Java·Kotlin exact interface 의미 보강 | JVM contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | Java public API와 Kotlin projection의 automatic·logical disconnect 의미 일치 | Java·Kotlin STREAM exact interface에 같은 동작을 반영했다. |
| `V11-CA-SESSION-BINDING-IFACE-NODE` | Node.js exact interface 의미 보강 | Node.js contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, stored route와 disconnect 의미 일치 | Node.js channel messaging exact interface에 반영했다. |
| `V11-CA-SESSION-BINDING-IFACE-CPP` | C++ exact interface 의미 보강 | C++ contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, stored route와 disconnect 의미 일치 | C++ STREAM exact interface에 반영했다. |
| `V11-CA-SESSION-BINDING-E2E` | Session disconnect·relocation E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-SESSION-BINDING-SPEC`, `V11-CA-SESSION-BINDING-PROTOCOL` | 진행 | all-settled failure·dedupe·no-Store, same-generation·Completed-only sequence scenario 누락 0 | Config 2 SM-D4A·D4B·D5·D5A와 Config 10 ST-E1A 계약을 갱신했다. JVM `ZLinkSessionActorBindingContractTest`, Kotlin projection suite, C++ `test_cpp_framework_m6b_runtime`·`test_cpp_framework_actor_gateway`, .NET Session·cleanup·bound relay·handoff focused 56/56, Node workspace build와 CA-D78 focused 6/6이 통과했다. Node SpotService에는 SM-D4A·D4B·D5·D5A actual-process client 흐름을 추가했고 .NET SpotService의 정식 SpotId·result-free·Deferred Join 전환을 시작했다. 실제 runner는 아직 실행되지 않는다. .NET은 Play 16개·Session 6개·MultiNode 13개 compile error, Java SpotService는 73개 compile error, C++ SpotActorTransfer는 이전 SpotId·Join·manager 표면 때문에 compile error, Node SpotService·SpotActorTransfer도 이전 Spot handle·factory·Join 표면 때문에 compile error가 남는다. Java 대체 SpotActorTransfer ST-E1은 compile 뒤 User Spot create timeout으로 중단됐다. 또한 Node runtime에는 durable `Cleaning`·`Completed`와 command 44·45 연결이 없다. 따라서 다섯 언어의 physical all-settled·no-Store·stale token과 durable `Completed` 뒤 same-generation route switch를 실제 process command로 증명하기 전에는 완료로 전환하지 않는다. |
| `V11-CA-SESSION-BINDING-REVIEW` | 정식 흡수와 runtime gap 독립 review | Codex review lane, `P-HIGH` | `V11-CA-SESSION-BINDING-IFACE-DN`, `V11-CA-SESSION-BINDING-IFACE-JVM`, `V11-CA-SESSION-BINDING-IFACE-NODE`, `V11-CA-SESSION-BINDING-IFACE-CPP`, `V11-CA-SESSION-BINDING-E2E` | 진행 | spec·schema·interface·E2E·runtime owner 추적 100%, 상충·미소유 0 | Codex `gpt-5.6-sol high`와 Claude `claude-sonnet-5` 두 reviewer의 formal contract review는 모두 CLEAN이다. 남은 round는 Codex `gpt-5.6-sol high` 단독 reviewer가 수행한다. Relocation barrier의 timer replay와 네 exact Spot 상세 설명, C++ guide의 snapshot·all-settled 표현을 교정했고 DOC gate와 TRACE check가 통과했다. E2E runner 증거가 채워질 때 완료로 전환한다. |
| `V11-CA-SESSION-BINDING-DRAFT-RETIRE` | Session–Actor 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | 요청 문서와 inventory link 0, 정식 문서만으로 구현 가능 | Formal contract clean review 뒤 임시 요청 문서와 inventory link는 제거했다. 선행 E2E·review row가 완료되면 상태만 완료로 전환한다. |
| `V11-M6-SESSION-BINDING-CPP` | C++ stored route·disconnect runtime | C++ runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1~4 C++ 항목과 contract test gap 0 | Connection별 Actor map과 global Actor→single current binding index를 사용해 multi-Actor binding을 지원하고 inbound relay의 authority 재조회를 제거했다. Binding token, same-ObjectGeneration route update, stale token 차단과 physical disconnect all-settled cleanup을 구현했다. `test_cpp_framework_m6b_runtime`과 `test_cpp_framework_actor_gateway`를 다시 build·실행해 Session A→B stale fence, bind 뒤 resolver read 고정, callback failure 뒤 cleanup·Actor record 유지, live logical notification과 explicit new-generation bind가 통과했다. 실제 process `ST-E1`을 재검증하려고 현재 C++ E2E target을 다시 build했으나, E2E source가 아직 `SpotRid`, `spot_node_manager_t`, 결과를 기다리는 Actor Join과 caller 지정 Entry Spot Node RID를 사용해 compile이 실패했다. 새 deferred Join은 HTTP handler에서 기다리거나 등록할 수 없으므로 public Actor·Spot handler에서 `defer()`를 호출하고 `OnJoinCompleted` evidence로 결과를 관측하도록 E2E 구조를 먼저 전환해야 한다. Process E2E 전에는 완료로 전환하지 않는다. |
| `V11-M6-SESSION-BINDING-DN` | .NET stored route·disconnect runtime | .NET runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 수정 진행 | IMP-SA-1~4 .NET 항목과 contract test gap 0 | Relay의 hidden resolve·rebind를 제거하고 fixed binding snapshot all-settled cleanup, explicit new-generation bind와 same-generation route switch를 구현했다. SpotService `sm-d5`·`sm-d5a`가 통과했고 SpotActorTransfer fixture를 현재 `SpotId`, Context-only factory, deferred Join과 result-free publish 계약으로 전환했다. `ST-E1`과 `ST-E1A` combined process gate가 통과했다(`framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260725-094700-2778437`). Framework·ASP.NET build는 warning·error 0, focused runtime 41/41, session route 50/50과 Redis relocation 1/1이 통과했으며 feature map에 evidence를 기록했다. 자체 구현 gate는 충족했지만 선행 `V11-CA-SESSION-BINDING-REVIEW`가 진행 중이므로 이 행은 진행 상태를 유지한다. |
| `V11-M6-SESSION-BINDING-JVM` | JVM stored route·disconnect runtime | JVM runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1~4 Java·Kotlin 항목과 contract test gap 0 | Relay의 `ZLinkActorDirectory.find`와 per-message native bind·hidden rebind를 제거하고 exact binding snapshot all-settled·dedupe·failure cleanup, logical notification과 same-ObjectGeneration route fence를 구현했다. 전체 `:zlink-framework-core:test`와 `:zlink-framework-kotlin:test`가 통과했고 focused `ZLinkSessionActorBindingContractTest`도 다시 통과했다. 현재 public object role·Context-only factory·RelocationAdapter로 전환한 `SpotActorTransfer` 실제 process는 Actor 생성, remote User Spot admission, immutable relocation payload restore, target location commit, generation 유지와 relocation 이후 Actor request handler·reply까지 통과했다(`framework/languages/java/e2e/SpotActorTransfer/log/20260725-095852-3211205`). `ST-E1` 재현과 진단에서는 target이 stored snapshot의 `sourceNodeRid`, `sourceSessionRid`와 source Entry Spot ID를 정확히 받았지만, target에서 source Entry Spot으로 보내는 Framework relay가 exact source Entry Spot ObjectGeneration·AuthorityOwnerGeneration 없이 generation 0으로 제출되어 거부됐다(`framework/languages/java/e2e/SpotActorTransfer/log/20260725-103259-112492`). Core service bind나 hidden re-resolve를 사용하지 않고 이 exact fence를 relocation snapshot과 Framework-owned route에 보존한 뒤 source의 managed session binding으로 `BoundPushNotify`를 전달해야 한다. 이 relay와 `ST-E1`, `ST-E1A`를 통과한 뒤 완료로 전환한다. |
| `V11-M6-SESSION-BINDING-NODE` | Node.js stored route·disconnect runtime | Node.js runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1·4 Node.js 항목과 contract test gap 0 | Internal route refresh·rebind는 같은 ObjectGeneration에서만 route를 갱신하고 새 incarnation의 explicit bind는 새 handle·token을 발급한다. Physical disconnect는 exact snapshot all-settled·deadline·cleanup을 수행하고 explicit notification과 token으로 중복을 막는다. SpotService에 actual process `SM-D4A`·`SM-D4B`·`SM-D5`·`SM-D5A`와 `session-binding-e2e` group을 추가했고 workspace build와 CA-D78 focused 6/6이 통과했다. `timeout 420s framework/languages/node/e2e/SpotService/run_e2e.sh session-binding-e2e`는 기존 target의 `SpotHandle`·`ZLinkSpotHandleResolver` 제거, `spotRid`→`spotId`, Actor factory·create response와 fluent terminal 전환 누락 때문에 compile 단계에서 실패했다(`log/20260725-072527-2918231`). `timeout 420s framework/languages/node/e2e/SpotActorTransfer/run_e2e.sh ST-E1`도 같은 drift와 제거된 Join `submit` 사용으로 실패했다(`log/20260725-072603-2935506`). 더 근본적으로 Node가 받는 Core `ActorTransferPhase`는 Preparing·Fenced·Committed·Activated·Aborted만 제공하며 Cleaning·Completed와 command 44·45 surface가 없다. 따라서 Core·binding을 바꾸지 않는 조건에서는 durable `Completed` 뒤 same-generation route switch를 actual process로 증명할 수 없다. 이 blocker를 해소하고 두 runner가 통과하기 전에는 완료로 전환하지 않는다. |

2026-07-25 `.NET session route completion fence` 후속 checkpoint: relocation request가 원래
binding token·binding generation·ObjectGeneration·이전 AuthorityOwnerGeneration·session owner
lifecycle generation·accepted high-water를 함께 보존하고, target runtime은 이 identity를 authority
CAS 전에는 staging 상태로만 유지한다. Source가 target commit reply를 받은 직후 session route를 바꾸던
경로는 제거했다. Target의 replay와 `OnJoinCompletedAsync`가 끝난 뒤에만 framework internal command
44를 session owner로 보내며, session owner table은 모든 fence와 동일 ObjectGeneration을 한 번에
검증한 뒤 Actor route만 교체하고 command 45 ACK를 반환한다. Reply loss retry는 이미 동일한 target
authority generation과 route가 적용된 경우에만 idempotent ACK를 반환한다. ACK 전에는 target의
staged route를 bound-session steady state로 공개하지 않는다.

POSD·DDD 검토에서는 (1) authority CAS 전에 route를 갱신하고 각 실패 지점에서 보상하는 방식과
(2) immutable original binding identity를 staging한 뒤 authority commit과 completion을 통과한 한
지점에서 session owner aggregate가 route를 전환하는 방식을 비교했다. 첫 번째 방식은 authority와
session table에 이중 visibility와 시간 결합을 만들므로 제외했다. 두 번째 방식은 route 변경 규칙과
idempotence를 session binding aggregate에 모으고 caller에게 recovery 조건을 노출하지 않으므로 선택했다.
Session frame admission은 binding table과 actor owner state의 high-water를 함께 전진시키며, physical
disconnect의 기존 fixed snapshot all-settled cleanup은 유지한다.

현재 `Zlink.Framework.csproj --no-restore`는 warning·error 0으로 통과한다. Exact fence 실패,
동일 generation route 전환, ACK retry idempotence와 ACK 전 route 비공개 focused test를 추가했다.
전체 Unit project compile은 이 lane 밖에서 진행 중인 Location projection 제거 fixture가 이전
`ResolveActorAsync`·`UpdateActorAsync`·`RemoveActorAsync`·`WriteActorAsync`를 참조하는 14건 때문에
중단됐다. 해당 fixture 수렴 뒤 focused test와 전체 회귀를 다시 실행하기 전에는
`V11-M6-SESSION-BINDING-DN`을 완료로 전환하지 않는다. 현재 authority row에는 별도 durable
`Completed` CAS가 없으므로, command 44의 실행 시점은 target completion work 이후이지만
`Completed` authority 상태 자체의 crash recovery 증거는 아직 남은 gap이다.

아래 sub-ID는 이 amendment가 추가한 runtime 구현 범위를 독립적으로 할당하고 완료 판정하기 위한
안정된 작업 단위다. 각 sub-ID가 완료되기 전에는 대응하는 기존 `V11-M6A-*`·`V11-M6B-*`·`V11-M6C-*` row를 완료로
전환하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6B-EXEC-CPP` | C++ User Spot execution mode와 Yield scheduler | C++ runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | `test_cpp_framework_m6b_runtime`, `test_cpp_framework_execution`, `test_cpp_framework_contract_headers`를 root가 clean rebuild 뒤 재실행해 3/3 통과했다. SpotWide Actor FIFO double claim, PerActor Actor·Spot·timer lane, unsupported Yield와 self·same-gate request의 submit 전 거부를 검증했다. |
| `V11-M6B-EXEC-DN` | .NET User Spot execution mode와 Yield scheduler | .NET runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | Framework build error·warning 0. `UserSpotExecutionSchedulerTests` 5/5, Worker·serial focused 회귀를 포함해 37/37 통과했고 root 재검증은 execution·worker·serial 15/15를 통과했다. Config 8 E2E는 이 row의 gate가 아니며 `V11-R5D` 뒤 별도 활성화한다. |
| `V11-M6B-EXEC-JVM` | JVM User Spot execution mode와 Yield scheduler | JVM runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Java·Kotlin continuation이 같은 claim을 유지하며 실행 mode·Yield·same-gate contract test 통과 | Root가 Java core·Kotlin unit suite를 `--rerun-tasks`로 clean 재실행해 각각 494/494와 46/46을 통과했다. SpotWide double claim, PerActor Actor·Spot·timer lane, Java CompletionStage·Kotlin coroutine continuation claim, exact Yield allowlist와 same-gate 사전 거부를 검증했다. |
| `V11-M6B-EXEC-NODE` | Node.js User Spot execution mode와 Yield scheduler | Node.js runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | Root가 `npm run verify:m6b-runtime`을 재실행해 39/39를 통과했고 typecheck와 production·browser build도 통과했다. SpotWide shared gate+Actor claim, PerActor Actor·Spot·timer lane, Promise continuation 유지, unsupported Yield의 worker admission 전 거부와 Actor join Yield surface 제거를 검증했다. |
| `V11-M6C-BARRIER-CPP` | C++ all-lane lifecycle barrier | C++ maintenance lane, `P-DEEP` | `V11-M6B-EXEC-CPP` | 완료 | yielded continuation과 Actor·Spot·timer lane을 모두 quiesce한 뒤 close·snapshot·relocation을 시작하고 abort가 같은 generation seal만 복원 | Root 검토에서 lifecycle seal이 application·infrastructure active lane과 yielded continuation 종료를 기다린 뒤 queue·timer를 capture하는 것을 확인했다. Seal 동안 timer 변경과 ingress 실행을 차단하고 held ingress를 FIFO로 보존한다. Abort는 token·captured reference·barrier generation이 모두 같은 경우에만 같은 generation으로 복원하며 commit 뒤 이전 generation ingress는 stale로 거부한다. CMake build와 M6B·M6C runtime binary가 모두 exit 0이고 targeted `git diff --check`가 통과했다. |
| `V11-M6C-BARRIER-DN` | .NET all-lane lifecycle barrier | .NET maintenance lane, `P-DEEP` | `V11-M6B-EXEC-DN` | 완료 | yielded continuation과 Actor·Spot·timer lane을 모두 quiesce한 뒤 close·snapshot·relocation을 시작하고 abort가 같은 generation seal만 복원 | `ZLinkSpotSerialExecutor`의 generation-scoped barrier가 SpotWide·PerActor의 Spot·Actor·timer claim, caller cancellation 뒤 실제 callback과 yielded terminal continuation을 모두 기다린다. Relocation은 accepted journal과 seal 뒤 held ingress를 같은 barrier에 결합하고 abort는 exact generation만 reopen한다. Close도 all-lane barrier 뒤 callback을 실행하고 성공 시 admission seal을 유지한다. Focused scheduler 11/11, scheduler+serial 32/32와 Framework build warning·error 0이 통과했다. |
| `V11-M6C-BARRIER-JVM` | JVM all-lane lifecycle barrier | JVM maintenance lane, `P-DEEP` | `V11-M6B-EXEC-JVM` | 완료 | coroutine continuation을 포함한 모든 lane의 barrier와 generation-fenced abort contract test 통과 | Production User Spot Retire coordinator가 다음 turn을 선점하는 boundary reservation으로 Spot·member Actor·timer lane 전체를 하나의 composite generation으로 seal한다. Active turn과 yielded continuation이 terminal 경계에 도달하기 전에는 Capture를 시작하지 않으며, 이미 accepted된 application record는 실행하지 않고 journal로 capture한다. Exact boundary·seal instance가 맞는 abort만 captured→held FIFO를 source queue에 복원한다. Shutdown이 이미 시작한 relocation unit과 경쟁하면 teardown을 미루고 해당 unit의 terminal을 기다리며 이후 unit은 시작하지 않는다. Single worker·no-daemon focused 검증에서 `ZLinkAsyncSerialQueueTest` 20/20, `ZLinkCompositeRelocationBarrierTest` 6/6, `ZLinkRelocationShutdownGateTest` 2/2, `ZLinkUserSpotRetireSourceBuilderTest` 1/1, 합계 29/29가 통과했고 실패·오류·skip은 0이다. 실제 process rolling·Shutdown 검증은 `V11-M6C-E2E`가 소유하므로 이 component contract 완료 증거에 포함하지 않는다. |
| `V11-M6C-BARRIER-NODE` | Node.js all-lane lifecycle barrier | Node.js maintenance lane, `P-DEEP` | `V11-M6B-EXEC-NODE` | 완료 | Promise continuation을 포함한 모든 lane의 barrier와 generation-fenced abort contract test 통과 | Root가 typecheck, production·browser build, M6C 40/40과 M6B 회귀 39/39를 재실행했다. SpotWide Yield continuation은 기존 claim으로 seal 안에서 재개되고 PerActor Actor·Spot·timer lane은 같은 generation barrier에서 quiesce한다. Close·relocation capture는 barrier 뒤에 시작하며 exact-generation abort만 seal과 timer state를 복원하고 stale abort와 commit 이후 turn은 상태를 바꾸지 못한다. |
| `V11-M6C-CAPACITY-CPP` | C++ runtime·Redis typed capacity | C++ Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 production target을 재빌드하고 실제 Redis 전체 27건을 실행해 25건 통과, 별도 cross-language fixture 환경이 필요한 2건만 skip임을 확인했다. Creation·terminal·standalone relocation·aggregate direct reserve/commit/abort·destroy가 Actor total·Spot total·Spot stable-type의 여섯 HASH를 단일 v3 bundle로 변경한다. Legacy scalar `capacityDelta`, participant relocation reservation과 비활성 Lua scaffold는 production source에서 0건이다. |
| `V11-M6C-CAPACITY-DN` | .NET runtime·Redis typed capacity | .NET Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | .NET public capacity vector는 Actor total, Spot total과 optional Spot stable-type delta를 함께 표현한다. InMemory·Redis creation, standalone relocation, aggregate prepare·commit·abort와 delete가 같은 typed bundle을 원자적으로 적용하고 Redis schema는 `zlink-capacity-bundle-v2`를 사용한다. Root 재검증에서 `RelocationRuntimeTests` 23/23과 실제 Redis `RedisAuthorityRelocationTests` 11/11이 통과했다. |
| `V11-M6C-CAPACITY-JVM` | JVM runtime·Redis typed capacity | JVM Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 Core와 Redis module 전체를 `--rerun-tasks`로 재실행해 build 성공을 확인했다. Core 494/494, Redis module 35건 실패 0이며 환경 비대상 2건만 skip했다. Descriptor v2 fixture와 creation·standalone relocation·aggregate·abort·destroy의 Actor total·Spot total·Spot stable-type bundle 원자성을 실제 Redis 7.0.15에서 검증했고 legacy scalar aggregate Lua와 relocation helper는 production source에서 제거했다. |
| `V11-M6C-CAPACITY-NODE` | Node.js runtime·Redis typed capacity | Node.js Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 production typecheck·Node/browser build와 실제 Redis filtered 3/3을 재실행했다. Descriptor v2·authority fixture, exact 14-field admission과 creation·standalone relocation·aggregate·abort·destroy의 Actor total·Spot total·Spot stable-type bundle 원자성을 검증했다. 전체 M6C 36건 중 capacity와 무관한 Entry/User Spot collision 정책 2건은 `V11-M6B-ENTRY-IDENTITY-NODE` gap으로 분리한다. |
| `V11-M6C-CAPACITY-MONITORING` | 다섯 언어 capacity 관측 구현 | monitoring lanes, `P-DELIVERY` | `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE` | 진행 | Actor total·Spot total·Spot stable type별 active·reserved·limit, activation active·limit과 unlimited 표현의 parity test 통과 | C++는 Location Store descriptor의 Actor·Spot·stable type별 active·reserved·limit과 activation active·limit을 snapshot에 투영하고 store 불가 시 registration limit으로 fallback한다. .NET은 descriptor cache를 snapshot에 투영하고 capacity 변경 시 placement event를 발행한다. JVM도 같은 descriptor 값을 Java exact `objectCapacity`·`activationConcurrency`에 투영하며 Actor·Spot total과 User·Instance Spot stable type의 active·reserved·limit을 유지하고 limit 0을 unlimited로 표현한다. Store row를 조회할 수 없으면 registration의 configured limit으로 fallback한다. Spring monitor는 100 ms 간격으로 local descriptor를 비교해 `zlink.runtime.object.placement_changed`를 발행하며, exact Java event가 허용하는 descriptor revision과 `updated` reason으로 변화 시점을 알리고 값은 같은 runtime의 snapshot에서 읽는다. Exact snapshot constructor·accessor와 Java·Kotlin 사용 표면을 검증했고 Java core 565/565, Spring starter 32/32, Kotlin 49/49가 통과했다. Node snapshot도 descriptor capacity·activation shape를 투영하지만 현재 observer는 capacity 변경 event를 발행하지 않으므로 이 row는 진행 상태를 유지한다. .NET focused 2건과 public contract snapshot, C++ framework·contract header·runtime integration build/run도 통과했다. C++ vertical target은 기존 generated include와 stale service API 오류로 전체 compile evidence를 만들지 못했다. |
| `V11-M6B-ENTRY-IDENTITY-CPP` | C++ global SpotId와 Framework-issued lifecycle identity | C++ runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 수정 진행 | 모든 Spot path가 UTF-8 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | C++ runtime은 User Spot create reply를 `RoutingId`로 변환하지 않고 wire의 UTF-8 SpotId를 그대로 보존한다. InMemory·Redis Location Store는 descriptor `NewClaim`에서 Entry SpotId claim, User·Instance authority 충돌 검사와 descriptor 저장을 한 transaction으로 처리한다. Descriptor remove와 owner cleanup은 exact owner lease·lifecycle이 일치할 때만 claim을 해제하며 stale cleanup은 replacement claim을 변경하지 않는다. Generic Spot reservation은 live Entry claim을 authority `Missing` conflict로 반환한다. UUID v4 version·variant, generated User Spot 첫 conflict의 추가 UUID·reservation 0, descriptor exact mapping과 InMemory lifecycle test를 포함한 `test_cpp_framework_m6b_runtime` 1/1이 통과했다. 실제 Redis claim HASH·무기한 retention, reverse authority collision, duplicate claim, exact remove·owner cleanup focused 1/1과 provider 전체 17/17도 통과했다. checkpoint `29f7be7ade87c74dcd36657afed3e8baedbcbffa`의 immutable candidate로 실행한 전체 C++ `M6-RUNTIME`은 configure·runtime compile·public build까지 통과했으나 public contract test 1/3 통과, 2/3 실패로 PASS evidence를 만들지 못했다. 후속 조사에서 Bingo는 별도 handler가 아니라 Entry Spot `on_create_actor`가 생성 승인과 초기화를 소유하고, Actor lookup은 global ActorId authority key를 사용하며, ST-D1·SM-F4는 string SpotId field로 이미 계약을 검증하는 것을 확인했다. Contract gate를 이 현행 의미에 맞춰 고치고 ObservabilityOps에 owner lease의 before/during renewal과 기존 route 8/8 성공 증거를 추가했다. `test_cpp_framework_layout_contract`와 `test_cpp_framework_target_contract`가 모두 통과했고 ObservabilityOps host target도 compile됐다. 전체 `M6-RUNTIME`과 `ROW-GATE`는 새 immutable candidate로 다시 실행하기 전까지 완료하지 않는다. |
| `V11-M6B-ENTRY-IDENTITY-DN` | .NET global SpotId와 Framework-issued lifecycle identity | .NET runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 완료 | 모든 Spot path가 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | Entry Spot은 bind 전에 `<prefix>-entry-<lowercase UUID v4>` SpotId를 발급하고 reserved caller ID를 거부한다. Descriptor publish와 Spot authority claim을 같은 owner·lifecycle fence로 연결했으며 InMemory·Redis provider가 duplicate claim, stale owner cleanup과 renew idempotence를 동일하게 처리한다. Instance activation의 public fluent surface와 production source에서 `InstanceSpotAddress`·`CloseLegacyAsync` 잔여를 제거했다. Entry identity·topology·auto-connect·ClientServer focused 묶음 48/48, Instance·wire·metrics·Actor manager focused 묶음 50/50과 relocation·capacity focused 24/24가 통과했다. 2026-07-26 완료 감사에서 전체 Unit 940/940과 Contract 65/65를 다시 통과해 이전 fixture·snapshot 대기를 닫았다. |
| `V11-M6B-ENTRY-IDENTITY-JVM` | JVM global SpotId와 Framework-issued lifecycle identity | JVM runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 완료 | Java·Kotlin 모든 Spot path가 String SpotId를 사용하고 Entry·User UUID v4·v3 provider parity test 통과 | Root가 Core 497/497, Kotlin 46/46과 실제 Redis module 35건 실패 0을 `--rerun-tasks`로 재실행했다. Public `joinEntrySpot(Object)`는 target RID를 받지 않고 Actor stable type·capacity·weight로 eligible descriptor를 선택한 뒤 exact `entrySpotId`를 사용한다. Entry·User Spot UUID v4, 첫 충돌 즉시 실패, descriptor kind·Mesh·owner mapping과 v3 identity claim을 검증했다. 비활성 integration source의 이전 public 호출은 `V11-R5D` 뒤 E2E 활성화 단계에서 current contract로 함께 갱신하며 이 runtime sub-ID gate에는 포함하지 않는다. |
| `V11-M6B-ENTRY-IDENTITY-NODE` | Node.js global SpotId와 Framework-issued lifecycle identity | Node.js runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 완료 | 모든 Spot path가 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | Root가 typecheck, production/browser build, M6B 39/39, M6C 36/36과 Unicode Redis SpotId·public contract focused 2/2를 재실행했다. Public/runtime/Location/wire의 Spot identity는 UTF-8 string이고 hex side field·NodeRid==SpotId fallback은 0이다. Entry descriptor exact match, UUID v4 첫 충돌의 즉시 SpotIdConflict와 추가 UUID·reservation 0을 검증했다. |
| `V11-M6A-CORE-RID-UUID` | Core raw automatic RID regression | Core regression lane, `P-DELIVERY` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | caller가 RID를 지정하지 않은 raw socket의 ID가 exact 16 bytes이고 UUID v4 version·variant bit를 만족하며 caller 지정 binary RID와 STREAM 4-byte RID 계약을 바꾸지 않음 | `.artifacts/v11/evidence/V11-M6A-CORE-RID-UUID/result.json`: targeted build, automatic UUID v4·caller binary contract와 STREAM 4-byte regression 2/2 통과 |
| `V11-M6A-WEIGHT-CPP` | C++ 공통 public weight 범위와 selection | C++ topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, 64-bit 합산과 multicast contract test 통과 | C++ public builder와 runtime option은 RouteMesh channel·ClientServer·placement weight를 signed `0..10000`으로 검증한다. Topology·ClientServer selection은 `uint64_t` 누적치를 사용하고 weight 0을 새 선택에서 제외하며 Logical Multicast는 positive remote node를 한 번씩 포함한다. Runtime 변경은 descriptor revision을 증가시켜 channel·placement weight를 다시 게시한다. Root가 `test_cpp_framework_m6a_runtime`과 `test_cpp_framework_contract_headers`를 rebuild·실행해 exit 0을 확인했으며 M6A test는 4,300,000,000 누적, 1:3 selection, zero exclusion, runtime revision과 multicast를 검증한다. |
| `V11-M6A-WEIGHT-DN` | .NET 공통 public weight 범위와 selection | .NET topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 진행 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, 64-bit 합산과 multicast contract test 통과 | `WeightContractTests`의 capacity-first·weight 0 Actor/User Spot eligibility 7/7, runtime placement/channel revision 1/1, ClientServer 100:300 1/1, same-process weight 0 1/1, Logical Multicast positive remote once 1/1과 전체 Unit 940/940·Contract 65/65가 통과했다. Runtime 구현은 완료됐고 현재 source 기준 evidence artifact와 `ROW-GATE` 재생성만 남아 있다. |
| `V11-M6A-WEIGHT-JVM` | JVM 공통 public weight 범위와 selection | JVM topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Java·Kotlin RouteMesh·ClientServer·placement weight parity와 overflow-safe selection test 통과 | Root가 누락된 `ZLinkRouteMeshRuntimeOptions` production 구현을 연결해 Mesh·Channel의 live weight 변경을 Framework-owned node와 Location descriptor 새 revision에 함께 반영했다. Weight는 mutation 전에 signed `0..10000`을 검증하고 0은 selection에서 제외하며 topology 합산은 `long`과 `Math.addExact`를 사용한다. RouteMesh M6A 5/5, ClientServer M6A 11/11, live option 3/3과 Core 497/497·Kotlin 46/46 전체 회귀가 통과했다. |
| `V11-M6A-WEIGHT-NODE` | Node.js 공통 public weight 범위와 selection | Node.js topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, safe-integer 합산과 multicast contract test 통과 | Root가 M6A 9/9, Logical Multicast focused 6/6과 typecheck를 재실행했다. Public runtime option의 Mesh placement·Channel weight mutation은 `0..10000`을 검증하고 raw descriptor revision과 peer announcement를 동기 갱신한 뒤 Location descriptor republish를 직렬화한다. Weight 0 제외, capacity-first placement, safe-integer 합산, ClientServer selection과 Logical Multicast admission·backpressure 경계를 검증했다. |
| `V11-CA-USER-SPOT-RUNTIME-JOIN` | execution·capacity·Entry identity·weight runtime amendment 합류 | amendment coordinator, `P-DELIVERY` | `V11-M6B-EXEC-CPP`, `V11-M6B-EXEC-DN`, `V11-M6B-EXEC-JVM`, `V11-M6B-EXEC-NODE`, `V11-M6C-BARRIER-CPP`, `V11-M6C-BARRIER-DN`, `V11-M6C-BARRIER-JVM`, `V11-M6C-BARRIER-NODE`, `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE`, `V11-M6C-CAPACITY-MONITORING`, `V11-M6B-ENTRY-IDENTITY-CPP`, `V11-M6B-ENTRY-IDENTITY-DN`, `V11-M6B-ENTRY-IDENTITY-JVM`, `V11-M6B-ENTRY-IDENTITY-NODE`, `V11-M6A-CORE-RID-UUID`, `V11-M6A-WEIGHT-CPP`, `V11-M6A-WEIGHT-DN`, `V11-M6A-WEIGHT-JVM`, `V11-M6A-WEIGHT-NODE` | 대기 | 모든 sub-ID evidence와 대응 `V11-M6A-*`·`V11-M6B-*`·`V11-M6C-*` 상태를 대조해 runtime·provider·monitoring·identity·weight gap 0 | `<ID>/result.json` |

- 이 변경의 runtime contract test가 모두 통과하기 전에는 해당 언어의 M6A·M6B·M6C row와 `V11-R5A`·
  `V11-R5B`·`V11-R5C`를 완료로 전환하지 않는다.

`V11-R4B` 완료 증거는 당시 source-side Instance reservation 계약에 대한 기록이다. 이후 확정한 `CA-D47`의
target-owned activation envelope는 해당 과거 판정을 소급해 바꾸지 않으며, 현재 `V11-R5B`와
`V11-E2E-SPEC-FINAL`의 필수 재검토 범위에 포함한다. 네 runtime은 target이 reservation을 소유하고 source claim이
0건임을 구현하기 전까지 Instance activation gap을 완료로 판정하지 않는다.
`CA-D47`은 공통 Framework API·Spot messaging·Location runtime·flow tracing, 다섯 언어 Spot exact interface와
Config 14 E2E에 반영했다. 계약 검증기는 target-owned claim, source reservation 0과 Ready 뒤 envelope message
one-shot을 required fragment로 고정했으며 check 112개와 negative mutation 14개가 통과했다. Source runtime과
wire command 39의 기존 preclaimed authority 경로는 구현 gap으로 남겨 `V11-M6B-*`와 `V11-R5B`에서 교체한다.

이 교정으로 최종 E2E·sample spec에서 다시 확정할 직접 영향 범위는 Instance relocation·concurrent cold request·cold
one-way와 request·kind collision·claim generation·source-loss recovery·activation failure release다. 현재 catalog ID로는
`M06`, `M07`, `M24`, `M25`, `M40`, `M57`, `M62`, `M63`이며, 각 scenario는 manager create가 아니라 Spot 전용
fluent call의 marker 유무, type 생략·명시, initial Mesh option과 최초 message one-shot dispatch를 검증해야 한다.
`V11-E2E-SPEC-FINAL`과 `V11-SAMPLE-SPEC-FINAL` 전에는 기존 source·registration을 수정하거나 실행하지 않는다.

#### 9.1.2 Contract amendment decision register

아래 결정은 기존 proposal, Store amendment와 후속 execution·capacity·identity 교정의 open item을 합친 정식 spec 작성
입력이다. `CA-D01~D63`에 미결정 상태를
허용하지 않는다. 각 결정은 caller가 target node, owner fence, Store transaction과 retry state machine을
조합하지 않게 하는 방향을 선택했다. 언어별 표현은 달라도 identity, option, deadline, closed result와
failure 의미는 같아야 한다.

| ID | 비교한 대안 | 선택한 계약 | 정식 owner |
|---|---|---|---|
| `CA-D01` | `(MeshName, ActorId)` key / Store namespace global `ActorId` | `ActorId` 하나를 global key로 사용한다. UTF-8 1..255 bytes, case-sensitive exact equality이며 normalization과 case folding을 하지 않는다. MeshName은 initial placement attribute다. `ActorRef`는 `{ActorId, ObjectGeneration, MeshName, NodeRid}` location snapshot이고 message target이 아니다. JSON generation은 decimal string이다. | Framework API, Actor model, Location runtime, 다섯 Actor·serialization interface |
| `CA-D02` | Mesh별 Spot ID / global Spot ID | Global namespace와 `SpotRef` 결정은 유지한다. Spot의 Core RoutingId 재사용과 RID 명칭은 후속 `CA-D65`가 string `SpotId`로 대체했다. | Spot messaging, Spot Actor, Location runtime, 다섯 Spot·serialization interface |
| `CA-D03` | 암묵적인 default Mesh / 명시적인 ambiguity failure | `InMesh`가 있으면 그 Mesh를 사용한다. 생략했을 때 object role Mesh가 하나면 자동 선택하고, 0개면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한 Mesh가 없으면 `MeshNotFound`로 끝낸다. | Framework API, MeshNode, Actor·Spot interface, error interface |
| `CA-D04` | overload 조합·재사용 builder / single-use fluent call | Actor·User Spot `Create`·`GetOrCreate`는 required identity·stable type을 받고 Mesh·request·placement·timeout을 option으로 받는다. Spot direct는 별도 Spot call builder에서 Instance intent·stable type·initial Mesh·placement를 option으로 받는다. 같은 option의 두 번째 설정은 `InvalidConfiguration`, 두 번째 terminal submit은 `AlreadySubmitted`다. Terminal call 시작 시 하나의 end-to-end deadline을 고정한다. | Framework API, Actor·Spot interface, async policy |
| `CA-D05` | Bind source의 Store refresh·hidden retry / exact ActorRef 한 번 제출 | Session bind는 `ActorId + ObjectGeneration`을 고정한다. Active Message Follow route만 relay하며 route 없음은 `ActorLocationStale`, generation 불일치는 `ActorGenerationStale`, pre-commit seal은 `ActorMoving`이다. Local Actor overload와 hidden retry를 제거한다. | Actor model, Session Actor dispatch, 다섯 STREAM interface |
| `CA-D06` | STREAM에 MeshName 고정 / global object capability enable | `EnableActorDispatch()`는 Mesh 인자가 없다. Object Client 또는 Server role과 Location Store가 하나 이상 구성됐는지 startup에서 확인한다. Global ID가 Mesh를 resolve하며 multi-Mesh 자체는 오류가 아니다. | Session Actor dispatch, configuration·STREAM interface |
| `CA-D07` | Actor를 먼저 이동 / proposal 뒤 atomic relocation | Cross-node join은 target proposal → shared policy preflight → source seal → durable capture → target reservation·factory·필요한 Restore·journal staging → owner와 membership aggregate commit → target callback·source leave와 old membership durable cleanup → journal replay → 남은 source resource cleanup → `Completed` → route ACK·steady normalization → target Ready 순서다. Commit 전 실패는 source를 유지하고 commit 뒤 실패는 published Relocation reference로 target recovery를 계속한다. | Spot Actor, maintenance, protocol |
| `CA-D08` | Join 전용 policy / factory에 고정한 공통 policy | Actor·User Spot·Instance Spot factory는 `Disabled`, `Recreate`, `Snapshot` 중 하나를 반드시 등록한다. Snapshot factory는 object kind에 맞는 Relocation Adapter를 함께 등록하고 adapter는 application-owned opaque bytes만 capture·restore한다. Join과 maintenance는 같은 factory policy와 adapter를 사용한다. Same-node join은 relocation이 아니므로 policy로 차단하지 않는다. | Framework API, Spot Actor, configuration·maintenance interface |
| `CA-D09` | create 경쟁을 caller에게 노출 / authority attempt에 합류 | Exclusive `Create`는 Ready existing이면 `AlreadyExists`, 다른 type이면 `TypeMismatch`다. `GetOrCreate`는 같은 type의 Ready 또는 Creating attempt에 합류해 같은 ref를 반환한다. CAS loser는 다른 factory를 시작하지 않는다. | Actor·Spot lifecycle, Location runtime, manager interface |
| `CA-D10` | CAS loser가 새 owner 재선택 / 같은 attempt의 bounded wait | Creation terminal call의 deadline은 resolve, reservation, factory와 Ready barrier 전체를 포함한다. Creating loser는 같은 attempt를 관찰하고 deadline이면 `DeadlineExceeded`로 끝내며 새 create를 시작하지 않는다. 다음 call은 exact authority를 reconcile한다. | Actor·Spot lifecycle, async policy, Location runtime |
| `CA-D11` | source memory payload / durable creation intent | Encoded creation request는 최대 1 MiB다. Reservation 전 immutable content reference와 hash를 creation intent에 기록하고 Ready 또는 fenced failure cleanup까지 유지한다. CAS winner만 payload를 사용하며 factory는 `(logical key, ObjectGeneration, attempt)` 기준 at-least-once·retry-safe다. Loser는 payload를 message로 바꾸지 않는다. | Message model, Actor·Spot lifecycle, Location Store, protocol |
| `CA-D12` | Spot generation 비공개 / public exact generation | `SpotRef.ObjectGeneration`을 공개한다. 값은 non-zero unsigned 63-bit conceptual value이며 언어 exact interface는 손실 없이 표현하고 JSON은 decimal string을 사용한다. Close·stale fence에 사용한다. | Spot messaging, common contracts, 다섯 Spot interface |
| `CA-D13` | implementation class 이름 / stable User Spot type | User·Instance Spot type은 UTF-8 1..255 bytes의 case-sensitive stable name이다. Normalization하지 않고 언어 class FQN을 wire·Store identity로 사용하지 않는다. 같은 server에 duplicate stable type을 등록하면 startup 오류다. | Framework API, Spot messaging, configuration interface |
| `CA-D14` | 자유 형식 selector / 작은 typed placement surface | `CA-D70`이 대체한다. 최초 배치는 stable type·Serving·capacity와 node-wide weight만 사용하며 caller-defined selector를 받지 않는다. | Framework API, MeshNode, Location runtime, manager·configuration interface |
| `CA-D15` | User Spot inventory tree와 aggregate root CAS | User Spot과 member Actor는 non-zero 128-bit aggregate ID를 사용한다. Participant 총수에는 1,024개 상한을 두지 않는다. Location Store에는 최대 1,024개·encoded 1 MiB의 immutable leaf chunk와 필요한 index chunk를 저장한다. 모든 chunk의 count·digest를 검증한 뒤 aggregate authority의 owner·generation·inventory root와 capacity를 한 번의 CAS로 전환한다. Commit 전 partial owner를 resolve하지 않으며 commit 뒤에는 전체 target recovery만 허용한다. | Spot Actor, Location runtime·Store, maintenance, protocol |
| `CA-D16` | cache를 hidden fixed profile로 고정 / 운영 가능한 두 public duration | `RouteCacheMaxAge` 기본 15초와 `MessageFollowDuration` 기본 30초를 공통 Location option으로 공개한다. 둘 다 0이면 cache·Message Follow를 끈다. 양수이면 cache age가 Message Follow duration보다 최소 5초 작아야 한다. Runtime 변경은 새 entry와 새 relocation에만 적용한다. | Framework API, Location runtime, configuration interface |
| `CA-D17` | stale hop마다 Store 조회 / committed mapping chain | Relay는 committed source→target mapping만 사용하고 Store를 읽지 않는다. AuthorityOwnerGeneration은 hop마다 증가해야 하며 최대 8 hops다. Mapping 하나의 대기열은 1024 message·16 MiB 이하이고 negotiated message bound도 함께 지킨다. Original operation ID, generation, payload와 reply route를 보존하고 loop·bound 초과는 stale-route error다. | Actor·Spot messaging, Location runtime, protocol·monitoring |
| `CA-D18` | relocation policy 생략 overload / explicit policy | 모든 object Server factory는 policy를 명시한다. 생략 overload와 compatibility default를 두지 않는다. 이동을 지원하지 않아도 `Disabled`를 등록한다. | Framework API, 다섯 configuration interface |
| `CA-D19` | fixed RID 전면 제거 / manual topology에 한정 | Fixed Routing ID는 Location Store descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만 허용한다. Object Client·Server 또는 automatic mode와 함께 설정하면 startup 오류다. | MeshNode, network identity, 다섯 routing configuration interface |
| `CA-D20` | slot allocation 유지 / prefix+UUID v4 lifecycle RID | Public slot count·group, allocation Store·provider와 result type을 제거한다. Core raw socket의 미지정 RID는 RFC 4122 UUID v4 bit layout의 16-byte binary다. Framework automatic RID의 random identity도 UUID v4다. Diagnostic prefix를 제공하는 RID는 lowercase canonical 문자열을 prefix 뒤에 붙인 UTF-8 값을 Core socket에도 명시하고, User Spot `Create`의 logical RID도 active authority 충돌에서 새 UUID로 재시도하지 않는다. | Core socket, MeshNode, Spot messaging, Location Store, 다섯 routing interface |
| `CA-D21` | 별도 RID provider / descriptor owner CAS 재사용 | Prefix는 ASCII `[A-Za-z0-9._-]` 1..64자이고 full RID는 `prefix-<lowercase-canonical-uuid-v4>`이며 255 bytes 이하이다. Descriptor owner CAS가 `(MeshName, RID)` active conflict를 확인한다. 충돌은 기존 record를 바꾸거나 새 UUID로 재시도하지 않고 즉시 `RoutingIdConflict`로 startup을 실패한다. Replacement lifecycle은 새 RID를 사용한다. | MeshNode, Location Store, protocol·monitoring |
| `CA-D22` | Channel weight 합성 / node-wide Router weight | Placement weight는 Channel weight와 분리한다. 범위는 후속 `CA-D64`가 공통 signed `0..10000`, 기본 `100`으로 확대했다. `0`은 신규 placement·relocation target에서만 제외하고 reservation을 얻은 attempt와 existing traffic은 이후 변경으로 취소하지 않는다. Startup builder, runtime option, descriptor와 snapshot이 같은 값을 사용한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D23` | capacity 미설정 / finite node·type capacity | `CA-D61`로 대체했다. Population 기본값은 Actor total·Spot total·Spot stable type 모두 `0`(unlimited)이고 activation concurrency만 별도 기본값 128을 사용한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D24` | 두 CAS와 compensation / generic atomic reservation | Generic `Reserve`, `Commit`, `Abort` operation은 유지하되 `CA-D62`가 scalar 값을 typed capacity bundle로 대체했다. TTL 없이 authority와 exact reservation fence로 recovery·takeover·abort한다. | Location runtime·Store, Redis provider, 다섯 provider interface |
| `CA-D25` | `InstanceSpotAddress` overload / Manager explicit create / Spot direct fluent activation | `InstanceSpotAddress`와 Instance manager create를 모두 제공하지 않는다. `SendToSpot`·`RequestToSpot`은 global SpotId를 받고 Spot 전용 fluent call을 반환한다. Marker 없는 call은 existing-only다. Instance marker가 있는 Missing call만 optional stable type과 initial Mesh로 cold activation한다. Selected Mesh의 distinct type이 하나면 생략한 type을 자동 선택하고 여러 type이면 명시를 요구한다. Existing authority는 저장된 kind·type과 current Mesh를 사용한다. | Spot address messaging, Location runtime, 다섯 Spot·messaging interface |
| `CA-D26` | directory·handle surface 유지 / manager와 operational query로 통합 | Actor directory, Spot handle·resolver, Actor-Spot handle resolver와 unbounded list를 제거한다. Manager `Find(global ID)`와 current Spot query, page size 1..1000·encoded 4 MiB 이하의 operational query로 대체한다. Entry registration은 Object Server builder가 소유한다. | Framework API, Actor·Spot·Location spec, 다섯 interface |
| `CA-D27` | ID-only destroy·close / exact ref mutation | Destroy와 Close는 exact ref를 받는다. 같은 incarnation이 이미 없으면 idempotent `false`, 다른 generation은 stale-generation error, moving은 typed moving error다. Current ref를 다시 찾아 새 incarnation을 종료하지 않는다. | Actor·Spot lifecycle, manager·error interface |
| `CA-D28` | Client·Server 독립 flag와 local fallback / closed object role | MeshNode role은 `None`, `Client`, `Server` 중 하나며 Server가 Client capability를 포함한다. Client·Server는 Location Store가 필수다. None은 manager·factory·placement를 제공하지 않고 hidden local object runtime도 만들지 않는다. Factory는 Server builder에서만 등록한다. | Framework API, MeshNode, Location runtime, 다섯 configuration·DI interface |
| `CA-D29` | .NET source를 새 정본으로 승격 / formal spec parity 유지 | Current source-only member는 계약 근거로 사용하지 않는다. Formal common spec과 다섯 exact interface를 한 candidate에서 갱신하고 Kotlin extension·JVM ABI snapshot까지 별도 검증한다. Gap은 implementation row가 소유한다. | 다섯 exact interface, public contract trace |
| `CA-D30` | Missing·Creating·Store failure negative cache / negative cache 없음 | Missing, Creating과 Store failure를 cache하지 않는다. Positive Ready cache도 current owner lease의 local admission deadline까지만 유효하다. Store recovery 또는 higher StoreVersion·stale result가 있으면 즉시 invalidate하고 다음 operation이 exact resolve한다. | Location runtime·Store, monitoring |
| `CA-D31` | Location Store가 relocation payload까지 소유 / Location·Relocation capability 분리 | Location Store는 owner·location·generation, relocation phase·RelocationId·source·target fence, relocation reference·checksum, placement reservation과 aggregate authority를 소유한다. Relocation Store는 application state, accepted journal, reply·replay·recovery payload를 immutable chunk·root로 저장한다. Actor·Spot별 Store interface는 추가하지 않는다. | Framework API, Location runtime, Location·Relocation Redis spec, 다섯 provider interface |
| `CA-D32` | Relocation manifest를 participant authority로 사용 / Location inventory tree를 authority로 사용 | Location Store의 immutable inventory tree가 participant identity와 mutation을 보관한다. Aggregate authority의 root·전체 count·digest가 authority이며 한 CAS로 owner와 generation을 전환한다. Relocation manifest는 participant payload 탐색용 projection이며 authority가 아니다. Target은 두 Store의 전체 count와 digest가 같을 때만 restore한다. | Spot Actor, Location runtime·Store, Relocation Store, protocol |
| `CA-D33` | Store capability 묶음 등록 / 별도 public interface와 등록 | Root는 Location Store와 Relocation Store를 별도 public API로 등록하며 capability마다 최대 하나만 허용한다. Location 기능을 사용하는 host는 Location Store가 정확히 하나 필요하다. `Recreate` 또는 `Snapshot` factory가 하나라도 있으면 Relocation Store가 정확히 하나 필요하고, `Disabled` factory와 same-node join만 사용하는 host에는 필요하지 않다. Missing·duplicate는 socket bind 전에 startup configuration error다. `Disabled` cross-node 이동은 payload capture 전에 거부한다. | Framework API, 다섯 configuration interface |
| `CA-D34` | Redis composite class / 별도 Redis implementation class | 공식 Redis package는 Location Store와 Relocation Store의 concrete class를 따로 제공한다. 같은 Redis deployment·cluster를 서로 다른 key prefix로 사용할 수 있고 별도 Redis로 분리할 수도 있다. Client connection 공유와 disposal 방식은 언어별 구현이 소유하며 공통 correctness 조건이 아니다. | Location Redis spec, Relocation Redis spec, 다섯 Redis interface |
| `CA-D35` | Cross-store transaction·2PC / immutable payload 뒤 Location CAS publication | Runtime은 Relocation root를 먼저 저장하고 reference·checksum·retention을 검증한 뒤 Location authority 또는 aggregate CAS로 공개한다. CAS 전 실패·conflict payload는 orphan retention 또는 idempotent delete로 제거한다. Root 교체는 새 root 저장→Location reference CAS→이전 root 정리, 삭제는 Location reference 해제 CAS→Relocation payload 삭제 순서다. Cross-store transaction과 2PC를 요구하지 않는다. | Location runtime, Relocation Store, protocol·recovery internals |
| `CA-D36` | Transfer vocabulary / Relocation vocabulary와 data-loss terminal | Public contract와 protocol vocabulary를 Relocation Store·reference·root·envelope으로 통일한다. `Snapshot`, `Capture`, `Restore`, `Retire`, `Shutdown`은 state policy·callback·host operation 이름이므로 유지한다. Published reference의 payload가 영구적으로 없거나 checksum 또는 participant inventory digest가 일치하지 않으면 이전 owner로 rollback하지 않고 non-retriable `RelocationDataLost`로 끝낸다. 이전 `Transfer*` public alias는 제공하지 않는다. | Framework API, maintenance·monitoring, protocol, 다섯 exact interface |
| `CA-D37` | Generic `TState`·state contract ID / application-owned opaque bytes | Relocation adapter는 opaque byte sequence만 주고받는다. Application이 format, version, compatibility와 migration을 관리하며 Framework는 state contract ID, state type과 relocation codec을 제공하거나 descriptor에 싣지 않는다. | Framework API, Actor model, Spot messaging, Location runtime, 다섯 relocation interface |
| `CA-D38` | 하나의 generic state adapter / Actor·Spot adapter 분리 | Actor에는 `ActorRelocationAdapter`, User·Instance Spot에는 `SpotRelocationAdapter`를 등록한다. 두 interface의 operation 이름은 `Capture`와 `Restore`이며 target factory가 instance를 만들고 `Restore`는 그 instance에 bytes를 적용한 뒤 instance를 반환하지 않는다. | Spot Actor, configuration, 다섯 Actor·Spot exact interface |
| `CA-D39` | Operation별 adapter / factory Snapshot adapter 공유 | `Snapshot`으로 등록한 Actor의 maintenance와 cross-node User·Entry Spot join은 같은 Actor adapter를 사용한다. User Spot aggregate에서는 Spot과 각 Snapshot member Actor adapter를 각각 호출한다. Same-node join, `Disabled` 거부와 `Recreate`에서는 adapter를 호출하지 않는다. | Spot Actor, maintenance, Relocation Store, 다섯 exact interface |
| `CA-D40` | Commit 뒤 Restore / Restore 뒤 authority commit과 infrastructure callback 부재 | Target factory·`Restore`를 owner·membership commit 전에 끝낸다. Infrastructure relocation은 application의 join·leave·relocation callback을 호출하지 않는다. Entry·`PerActor` Actor는 state·queue·journal·Actor timer·source relay와 session route만 이전한다. `SpotWide` User Spot aggregate도 membership callback을 호출하지 않는다. `Capture`·`Restore` failure는 pre-commit source 유지와 retry-safe·at-least-once 규칙을 따른다. | Spot Actor, Location runtime, maintenance, 다섯 Entry Spot·relocation exact interface |
| `CA-D41` | 전체 queue drain / current turn 뒤 queue·timer relocation | Framework infrastructure notification이 execution queue의 turn boundary에 도달하면 현재 실행 중인 turn만 source에서 완료한다. 아직 실행하지 않은 message, accepted journal, logical timer registration과 pending tick은 Relocation Store에 저장하고 target에서 순서를 보존해 복원한다. Application이 timer를 다시 등록하지 않는다. | Async policy, Spot Actor, Location runtime, maintenance, E2E |
| `CA-D42` | 종류별·random batch / readiness-first bounded sliding relocation | Entry·`PerActor` Actor, Instance Spot과 `SpotWide` User Spot aggregate에 internal relocation notification을 전달한다. Permit을 즉시 얻은 ready unit만 seal하고 이전하며, permit을 얻지 못한 unit은 source에서 업무를 계속하고 notification을 재예약한다. `SpotWide`는 Spot과 member Actor를 분리하지 않는다. 기본 `AnyTurnBoundary`는 Framework가 안전한 경계를 선택한다. `ApplicationSignaled`는 `RelocationReady().Defer()` 경계를 사용하고 source `Continued` 또는 target `Relocated` default no-op callback을 다음 application job보다 먼저 호출한다. `PerActor`는 Spot authority를 먼저 바꾸고 Actor를 독립 unit으로 이전한다. | Maintenance, execution queue, 다섯 runtime |
| `CA-D43` | 4개 고정 relocation / count·callback·byte 독립 gate | Process 기본값은 active outbound 64, active inbound 64, concurrent Capture 8, concurrent Restore 8, encoded payload in flight 268,435,456 bytes다. 모든 값은 application configuration으로 변경할 수 있고 양수여야 한다. 모든 permit 전 source를 seal하지 않으며 byte 한도를 넘는 단일 `SpotWide` User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행한다. Actor 하나, Instance Spot 하나, `SpotWide` User Spot aggregate 하나, `PerActor` User Spot의 direct admission과 Actor 각각은 source admission seal부터 target admission-open ACK까지 1초를 목표로 한다. 1초는 timeout이 아니며 초과해도 relocation을 계속한다. Host 전체 relocation 시간은 이 목표에 포함하지 않는다. | Framework API, Location option, maintenance, 다섯 exact interface, E2E |
| `CA-D44` | Authority Put이 opaque payload에서 target owner를 추출 / owner metadata와 public transition 분리 | Public authority transition은 Active row의 `Preserve`·`NewOwner`와 delete만 제공한다. `Preserve`는 target owner token을 받지 않고 `NewOwner`는 exact `LocationOwnerToken`과 relocation capacity fence를 반드시 받는다. Provider는 같은 transaction에서 target owner lease를 검증하고 owner ID·lease generation metadata를 기록한다. Missing→Pending 생성과 initial object·owner generation 발급은 generic `Reserve`만 수행하며 compare-exchange에는 Missing expectation과 별도 create transition을 제공하지 않는다. Standalone Actor relocation은 single-key `NewOwner` CAS를 유지하고 User Spot은 aggregate target owner를 사용한다. | Location runtime·Store, Redis provider, 다섯 provider exact interface |
| `CA-D45` | Reservation provider가 Creating·Ready authority payload를 합성 / opaque payload와 provider allocation metadata 분리 | `Reserve` request는 Framework가 encode한 Creating authority payload를 받고, `Commit`은 Ready authority payload를 받는다. Provider는 payload body를 해석하거나 합성하지 않고 exact bytes를 저장한다. 별도 current placement allocation은 `Reserved`·`Active` state, kind, stable type, descriptor key·lifecycle generation과 typed capacity bundle을 저장한다. Reserve는 Missing→Reserved, exact Commit은 Reserved→Active, exact Abort는 Reserved→Missing만 수행하며 snapshot·stored·scan result가 allocation을 반환한다. Owner metadata는 allocation과 분리한다. | Location runtime·Store, Redis provider, 다섯 provider exact interface |
| `CA-D46` | Missing create reservation을 relocation에도 재사용 / existing object용 relocation capacity fence 분리 | Existing Actor·Spot relocation은 create reservation을 재사용하지 않는다. Standalone relocation fence는 current authority·source Active allocation과 typed bundle을 exact-match하고 target의 bundle 전체를 Reserved로 만든다. Standalone `NewOwner`만 이 fence를 소비한다. User Spot aggregate는 participant별 fence 없이 Spot total 1, Spot stable type 1과 Actor total N의 단일 typed bundle을 prepare하고 aggregate commit·abort가 all-or-none으로 finalize한다. Delete와 recovery도 exact bundle·authority·allocation fence를 사용하며 TTL에 의존하지 않는다. | Location runtime·Store, maintenance, Redis provider, 다섯 provider exact interface |
| `CA-D47` | Source가 Instance owner claim 뒤 target에 first message 전송 / target-owned first-message activation envelope | Ready authority는 source가 current owner로 일반 direct call을 보낸다. Missing+Instance intent에서는 source가 target만 선택하고 global Spot ID·stable type·descriptor fence·operation identity·reply correlation·deadline과 first message를 activation envelope로 target transport에 제출하며 owner claim과 reservation을 만들지 않는다. Target은 current authority와 local exact instance를 확인하고 Missing이면 자신을 owner로 generic Reserve를 수행한다. CAS winner만 factory·initialize·Commit을 실행하고 Ready barrier 뒤 envelope message를 local queue에 exactly once 제출한다. CAS loser는 local instance를 만들지 않고 Ready winner로 original operation을 한 번 redirect하거나 Creating completion에 합류한다. Authority와 일치하지 않는 local instance는 fence한다. | Framework API, Spot messaging, Location runtime, 다섯 Spot exact interface, Instance E2E |
| `CA-D48` | 언어별 Redis authority layout 중 하나를 복사 / 장점을 결합한 공통 hybrid schema | Authority current state와 active-scan history는 authority별 HASH에 두고 global counter·capacity·membership·versioned index만 shared HASH/ZSET에 둔다. Creation reservation·relocation fence·aggregate는 operation별 HASH다. `CA-D62`의 typed capacity schema부터 provider transaction domain 전체가 literal `{zlink-location-v2}` hash tag를 공유하고 모든 Lua key를 `KEYS`로 전달한다. v1과 v2 key를 한 transaction에서 혼합하거나 in-place 해석하지 않는다. Public descriptor HASH와 admission metadata HASH를 분리한다. Watermark·immutable history·tombstone·durable cursor로 1000 item·4 MiB snapshot page를 만들며 전체 materialization과 numeric revision score를 금지한다. | Redis Location Store 공통 spec·fixture, C++·.NET·JVM·Node provider와 cross-language Redis test |
| `CA-D49` | Capacity bucket과 `objectKind`를 언어별 enum 표현에 맡김 / Redis physical encoding을 고정 | Current authority의 `objectKind`는 `actor`, `user_spot`, `instance_spot` token만 사용한다. Capacity node bucket은 canonical descriptor key와 lifecycle generation decimal을 UTF-8 byte length-prefix로 encode하고, type bucket은 같은 값 뒤에 canonical `objectKind` token과 stable type을 같은 방식으로 붙인다. 이 규칙은 Unicode, enum 이름과 숫자값 차이에도 네 provider가 같은 field를 갱신하게 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider physical schema test |
| `CA-D50` | Owner lease를 언어별 string·HASH·dual-write로 유지 / 하나의 HASH 계약으로 고정 | `owner-lease:D`는 `ownerId`, `generation`, `expiresAt` 세 field와 key TTL만 사용한다. Descriptor·authority·RoutingId allocation은 이 HASH를 직접 검증·갱신하며 별도 legacy lease value나 owner lease index를 쓰지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider owner lease test |
| `CA-D51` | Descriptor owner index를 owner ID raw suffix로 구성 / exact owner token digest로 구성 | Descriptor index는 canonical descriptor key member를 저장하는 `descriptor:mesh:index` SET 하나를 사용한다. Cleanup index는 `ownerId + NUL + LeaseGeneration decimal`의 SHA-256 lower-hex suffix를 사용하고 같은 canonical key를 member로 저장한다. Owner ID만 일치하는 다른 host lifecycle descriptor는 제거하지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider descriptor cleanup test |
| `CA-D52` | Authority history를 language별 JSON·field grouping으로 저장 / revision-prefixed field encoding으로 고정 | Revision hex `R`마다 Active full snapshot은 `R:deleted=0`과 exact current 13개 `R:<field>`를 저장한다. Pending full snapshot은 provider-issued creation reservation ID, reference, SHA-256과 encoded size 네 field까지 current와 history에 모두 저장해 17개 field를 복원한다. Pending에는 네 field가 필수이고 Active에는 금지한다. Tombstone은 `R:deleted=1`, `R:authorityKey`만 저장한다. Membership history는 `R` field에 immutable bytes를 저장한다. 어느 언어가 만든 watermark snapshot도 다른 언어가 복원할 수 있어야 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider concurrent scan test |
| `CA-D53` | Automatic RouteMesh initiator와 duplicate-pipe admission을 한 문장으로 설명 / 시작 규칙과 안전장치를 분리 | Automatic RouteMesh는 canonical RID가 더 작은 MeshNode만 pairwise connect를 시작한다. Manual topology의 양방향 connect와 automatic의 경합·stale discovery 후보만 공통 duplicate-pipe admission에서 RID·lifecycle generation을 확인해 하나의 ready connection으로 수렴한다. ClientServer는 Client가 server별 intent를 만들고 classic fanout은 Subscriber가 publisher별 intent를 만드는 비대칭 topology다. | `07-channel-topology.ko.md`, `09-client-server-channel.ko.md`, `13-mesh-node.ko.md`, topology regression |
| `CA-D54` | Immutable digest를 언어별 descriptor serialization hash로 계산 / 공통 canonical preimage hash | Admission HASH의 `immutableDigest`는 `zlink-mesh-node-immutable-v2` domain부터 Entry Spot ID를 포함한 immutable descriptor·capability field를 UTF-8 byte length-prefix segment로 연결한 preimage의 SHA-256 lower-hex다. Channel name과 capability는 unsigned UTF-8 byte lexical order로 정렬한다. Descriptor revision, weight 값, maintenance wave, runtime state, owner token, timestamp와 usage count는 제외한다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider byte-level contract test |
| `CA-D55` | Instance first message를 process-local reservation·queue로만 유지 / complete activation recovery envelope와 provider-issued Pending creation projection | Target-owned Instance cold activation은 command 39의 optional metadata presence·frame까지 포함한 complete first-message envelope를 Relocation Store에 먼저 저장한다. Location Reserve는 provider-issued reservation ID, reference, SHA-256과 encoded size를 Pending authority current·history row에 함께 기록한다. Pending snapshot은 이 projection을 exact read로 반환하고 Active에서는 제거한다. Factory·initialize 뒤 durable activation inbox first record를 Ready 전에 확정하되 handler는 barrier로 막는다. Ready Instance authority만 recovery root·inbox sequence·replay cursor를 유지하며 queue head restore 뒤 barrier를 연다. 첫 handler terminal completion을 durable하게 기록하고 cursor를 sequence까지 갱신한 뒤에만 Preserve CAS로 pointer를 release하고 root를 삭제한다. Actor·User Spot generic create에는 ZLIA와 durable inbox를 사용하지 않는다. Instance Spot factory가 하나라도 있는 Object Server는 relocation policy와 관계없이 Relocation Store를 정확히 하나 등록한다. | `20`·`24`·`40`·`41`·`42`, protocol schema·golden·validator, 다섯 authority exact interface·registration, Config 14 `IS-F08`·`IS-P07`·`IS-E2E-32/34/35/36`, `V11-M6B-*` |
| `CA-D56` | User Spot remote create·close를 Location polling이나 local manager로 대체 / exact terminal service operation | User Spot create는 Location Store의 generic Pending creation content와 provider-issued reservation을 사용하되 source와 target 사이에 별도 generation-fenced service operation을 둔다. Create request는 operation correlation, source·target node lifecycle, authority key, stable type, exact reservation·StoreVersion과 deadline을 전달한다. Target은 Pending row의 immutable creation content를 exact read한 뒤 factory·initialize·Commit을 실행하고 `Existing`·`Created`·`Rejected`, exact `SpotRef`와 optional application reply를 terminal-once로 반환한다. User Spot close도 current owner로 exact `SpotRef`, authority owner generation과 target lifecycle을 전달하는 별도 operation을 사용하며 active Actor membership, moving state와 generation을 target admission 전에 검증한다. Location row polling은 terminal reply·rejection을 보존하지 못하고 application packet으로 reserved control을 흉내 내면 handler 경계가 누출되므로 사용하지 않는다. Protocol은 command 47 `userSpotCreate`, command 48 `userSpotClose`, reply operation discriminator 13·14로 고정했다. 기존 command 20 reply prefix와 field 순서를 유지하며 create·close 성공 tail cardinality와 source·target lifecycle, reservation·StoreVersion, exact SpotRef·authority generation·deadline 누락을 validator negative mutation으로 거부한다. Schema 39 commands·163 types, negative self-test 215개, generated 4-language constants drift check와 decoder fixture가 통과했다. 네 runtime codec·dispatch·source operation table과 cross-node contract를 같은 command identity로 갱신하기 전에는 remote create·close를 완료로 판정하지 않는다. | Spot manager exact interface, `20`·`23`·`24`·`40`, protocol schema·golden·validator, 네 runtime·M6B contract |
| `CA-D57` | User Spot Actor 독립 실행 / factory별 실행 mode | User Spot factory의 기본 mode는 `SpotWide`다. Actor job은 Actor별 FIFO claim을 먼저 유지한 뒤 User Spot gate를 얻으며 Spot direct handler, member Actor handler, timer와 lifecycle callback을 전체 직렬화한다. Optional `PerActor`는 factory 등록 때만 고정하고 Actor별 FIFO lane, Spot direct·lifecycle lane과 timer별 FIFO lane을 사용한다. 서로 다른 Actor와 서로 다른 timer는 동시에 실행할 수 있다. | Async policy, Actor model, Spot Actor, 다섯 configuration·Spot interface |
| `CA-D58` | 모든 owner turn의 generic Yield / shared Spot gate 전용 Yield | `Yield`는 `SpotWide` User Spot과 Instance Spot에서만 허용한다. Member Actor는 Actor claim을 유지하고 User Spot gate만 반납하며 terminal result 뒤 같은 gate를 다시 얻는다. Entry Spot·Entry Actor·`PerActor`·Node·Channel·owner 밖에서는 operation submission, queue mutation과 gate release 전에 `InvalidConfiguration`으로 끝낸다. RequestToChannel·RequestToSpot·RequestToActor와 RunIoWorker·RunCpuWorker의 제한은 유지한다. Create·get-or-create 제공 여부는 후속 `CA-D73`이 추가하며 join·send·publish·timer·close·destroy에는 제공하지 않는다. | Async policy, messaging, Actor·Spot exact interface |
| `CA-D59` | self request inline·같은 gate 대기 / no-inline과 deadlock 선검증 | Application handler를 inline 또는 reentrant dispatch하지 않는다. 같은 Actor의 awaited request는 Actor claim을 유지한 채 완료될 수 없으므로 `Async`와 `Yield` 모두 submit 전에 거부한다. `SpotWide`에서 target 처리가 현재 User Spot gate를 필요로 하는 `Async`도 submit 전에 거부한다. One-way submit은 queue 순서를 보존할 수 있으므로 별도 완료 대기를 만들지 않는다. | Async policy, Actor·Spot messaging, 다섯 runtime contract test |
| `CA-D60` | 단일 Spot queue seal / execution mode별 all-lane barrier | Close·relocation·snapshot은 새 application admission과 participant 변경을 먼저 seal하고, yielded continuation을 포함한 active Actor·Spot·timer lane이 안전한 turn 경계에 도달한 뒤 진행한다. 실패하면 같은 generation의 seal을 모두 abort한다. Snapshot은 일부 lane만 멈춘 상태를 capture하지 않는다. | Async policy, Spot Actor, maintenance, 네 runtime |
| `CA-D61` | generic object active 10,000·pending 128 / typed population limit과 activation concurrency 분리 | Node Actor total, Node Spot total과 Spot stable type limit만 제공하며 기본값 `0`은 unlimited, 양수는 최대값, 음수는 startup 오류다. Entry Spot은 Spot count에서 제외하고 그 Actor는 Actor count에 포함한다. Actor type별 limit은 두지 않는다. Activation concurrency는 population capacity와 다른 typed option이며 기존 pending 128의 보호 목적을 유지한다. | Framework API, MeshNode, configuration·monitoring exact interface |
| `CA-D62` | scalar capacity delta / typed capacity vector의 atomic reservation | Location Store는 Actor total, Spot total과 optional Spot stable type bucket의 active·reserved·limit을 lifecycle fence 아래 관리한다. Creation·commit·abort·destroy·relocation과 aggregate는 필요한 vector 전체와 authority를 한 transaction에서 바꾼다. User Spot aggregate target은 Spot total 1개, 해당 Spot stable type 1개와 member Actor total N개를 단일 bundle로 all-or-none 예약한다. Descriptor는 stale projection일 뿐 최종 admission이 아니다. | Location runtime·Redis Store, 다섯 provider·monitoring interface |
| `CA-D63` | opaque Framework-issued Entry identity / 진단 prefix와 독립 UUID v4를 가진 lifecycle identity | UUID 발급·lifecycle·collision 규칙은 유지한다. Entry identity의 타입·명칭과 문서 소유권은 후속 `CA-D65`가 string `SpotId`와 Spot identity spec으로 대체했다. | MeshNode, Spot messaging, Location Store, 다섯 Spot interface |
| `CA-D64` | topology별 0..100 weight / 공통 signed 0..10000 weight | RouteMesh Channel Server, ClientServer Server와 node-wide object placement의 public weight는 signed integer `0..10000`, 기본값 `100`이다. `0`은 새 target에서 제외하고 positive 값은 상대 비중이며 capacity·eligibility filter 뒤에 적용한다. Startup과 runtime update의 범위 밖 값은 mutation 전 configuration error다. Runtime update는 descriptor revision으로 순서화하고 이미 제출했거나 reservation을 얻은 operation에는 적용하지 않는다. Logical Multicast는 positive member를 한 번 포함하고 `0`을 제외하며 selection 합계는 최소 64-bit 정수로 계산한다. | Channel topology·ClientServer·MeshNode·monitoring, 다섯 configuration·runtime interface |
| `CA-D65` | Core RoutingId를 Spot에 재사용 / global logical string SpotId | Entry·User·Instance Spot은 Location Store transaction domain 전체에서 global인 UTF-8 1..255-byte, case-sensitive exact string `SpotId`를 사용한다. Unicode normalization과 case folding은 적용하지 않는다. User Spot `Create`는 lowercase canonical UUID v4, Entry Spot은 `<prefix>-entry-<lowercase-canonical-uuid-v4>`를 발급한다. NodeRid만 Core RoutingId를 사용한다. Wire는 Spot field를 text8로 encode하고 arbitrary binary legacy Spot RID를 거부하며 Redis는 `location-authority-hybrid-v3`을 사용한다. | Spot messaging·Actor·Location·Redis·relocation·monitoring, protocol, 다섯 exact interface와 E2E |
| `CA-D66` | Actor creation callback void / 승인·거절과 optional reply | Entry Spot의 Actor creation callback은 `Accepted`와 optional reply를 반환한다. Manager terminal result는 `Existing`, `Created`, `Rejected`의 닫힌 union이며 `Create`는 `Created`·`Rejected`만, `GetOrCreate`는 세 상태를 모두 반환할 수 있다. Callback exception은 application rejection이 아니라 기존 typed creation failure다. | Actor model, Entry Spot interface, 다섯 Actor manager interface |
| `CA-D67` | Entry·User Spot 공통 admission lifecycle / membership과 admission lifecycle 분리 | `OnActorJoin`은 User Spot admission에만 존재한다. Entry·User Spot은 commit 뒤 알림과 source membership 종료·disconnect callback만 공유한다. Actor 최초 생성은 Entry의 creation callback만 사용하고 joined callback을 호출하지 않는다. User→Entry 일반 복귀는 target joined와 source leave를, maintenance 복원은 target relocated와 source leave만 호출한다. | Spot model·Spot Actor, 다섯 Entry·User Spot interface와 E2E |
| `CA-D68` | 서로 다른 concurrent request가 첫 rejection을 공유 / operation-scoped terminal replay | 같은 Actor의 creation callback은 reservation으로 직렬화한다. Creating을 본 다른 operation은 authority 변경 뒤 Ready면 `Existing`, rejection·failure cleanup이면 새 reservation으로 자신의 request를 실행한다. 서로 다른 operation은 `Rejected` reply를 공유하지 않는다. 동일한 source lifecycle과 `OperationId`의 중복 전달만 Location Store의 correlation-free semantic terminal을 재사용하며 command reply는 현재 correlation과 reply route로 다시 encode한다. | Location runtime·Store·Redis v3 fixture, service wire와 다섯 provider interface |
| `CA-D69` | rejection을 Ready 뒤 destroy로 처리 / staging 단계에서 atomic reject | Rejected Actor는 Ready authority와 message admission을 열지 않고 active capacity·destroy lifecycle에 포함하지 않는다. Staging instance를 폐기한 뒤 exact Creating authority와 pending capacity를 정리한다. 다른 operation은 새 reservation으로 진행할 수 있고, 거절한 operation의 terminal record만 retry retention 동안 유지한다. | Actor runtime, Location Store, E2E·failure recovery |
| `CA-D70` | stable type 후보를 placement profile로 다시 분할하고 정의되지 않은 affinity를 전달 / stable type 기반 단일 배치 pool | Actor·User Spot·Instance Spot 최초 배치의 public API, factory option, descriptor capability, immutable digest, creation intent, reservation과 recovery에서 `PlacementProfile`·`AffinityKey`를 제거한다. 선택한 Mesh의 `Serving` Object Server 중 stable type을 등록했고 population capacity가 남은 node만 후보로 만든 뒤 node-wide placement weight를 적용한다. | Framework API·Actor·Spot·MeshNode·Location·Redis, service wire, 다섯 exact interface·runtime·E2E |
| `CA-D71` | publish 결과가 remote subscriber queue 또는 handler 완료까지 확인 / source-local submission 경계 | Logical Multicast의 remote 경계는 고정한 MeshNode route의 local transport queue 제출, local 경계는 일치하는 local Spot queue 제출이다. Remote Spot queue 수락, subscriber handler 시작·완료와 ACK는 기다리지 않는다. 반환 type과 terminator 이름은 후속 `CA-D72`·`CA-D73`이 대체한다. | Spot messaging, 다섯 messaging exact interface·runtime·contract test |
| `CA-D72` | one-way queue admission status·detail 반환 / 반환 데이터 없는 비동기 admission | `CA-D71`의 source-local admission 경계는 유지하되 Send·Publish·STREAM send·reply·bound session send·session Actor relay는 정상 완료 값을 반환하지 않는다. Queue가 가득 차면 operation family의 send timeout까지 기다리고, timeout·cancellation·route 단절·runtime 종료는 exceptional completion으로 전달한다. `Backpressured`는 중간 상태이며 public terminal 결과가 아니다. Logical Multicast의 target별 성공·drop·unreachable count는 public 결과에서 제거하며 monitoring 소유권은 후속 `CA-D77`이 제거한다. Partial submission을 rollback하거나 자동 재시도하지 않는다. Not-found는 Actor·Spot·Mesh·session별 기존 kind를 사용하며 runtime 종료는 공통 `RuntimeShutdown=36`으로 고정한다. | Async policy, messaging·Spot·Actor·STREAM, monitoring·error, 다섯 runtime·E2E |
| `CA-D73` | package별 비동기 terminator 이름과 Java call을 직접 노출하는 Kotlin surface / repository-wide fluent terminator 규칙 | Messaging·Worker call builder의 비동기 종결자는 .NET `Async`, Kotlin 전용 wrapper `await`, Java·Node·C++ `submit`을 사용한다. 즉시 제출은 `Submit`·`submit`, 실제 Spot gate를 반납하는 종결자만 `Yield`·`yield`다. .NET one-way `SubmitAsync`는 `Async`로 바꾸고 Java·Node·C++ one-way는 `Void`·`void`를 반환한다. Request·worker·Actor·Spot create·get-or-create의 application result는 유지하며 create 계열에도 허용된 `SpotWide`·Instance 문맥의 `Yield`를 제공한다. 같은 naming을 Server Framework·Stream Connector·HTTP Client의 exact interface와 package consumer에 한 snapshot으로 적용한다. 단, Node HTTP Client의 no-argument typed response와 Server builder one-way는 TypeScript generic erasure 뒤 같은 상속 signature가 되므로 one-way `submit()`을 우선하고 typed response에 `async<T>()`를 유지한다. | Framework·Stream Connector·HTTP Client 공통 async policy, 다섯 exact interface·runtime·sample |
| `CA-D74` | Actor Join 결과를 handler에서 await / handler terminal 뒤 Deferred Join 실행 | Actor membership Join call은 결과를 기다리는 `Async`·`submit`·`Yield` terminal을 제공하지 않고, 열린 Framework handler scope에 immutable intent와 Actor barrier를 동기 등록하는 `Defer`만 제공한다. Handler가 정상적으로 끝나면 등록 순서대로 활성화하고 exception·cancellation이면 모두 폐기한다. Join completion은 source 또는 target Actor의 `OnJoinCompleted`로 전달하며 same-node는 process lifetime, verified Relocation manifest가 Location authority에 publish된 cross-node `Accepted`만 recovery 뒤 at-least-once completion을 보장한다. Relocation은 `ObjectGeneration`을 유지하고 owner가 바뀔 때만 `AuthorityOwnerGeneration`을 증가시킨다. | Async policy, Actor·Spot lifecycle·Relocation, 다섯 exact interface·runtime·E2E |
| `CA-D75` | lifecycle Context를 callback 인자로 임시 전달 / application object가 exact Context를 composition | Actor·User Spot·Entry Spot·Instance Spot application instance는 Framework가 생성 전에 제공한 exact Object Context를 읽기 전용 member로 보유하고 lifecycle 동안 같은 객체를 반환한다. Actor factory는 ActorId를 별도 인자로 받지 않고 Actor Context 하나를 받으며 Spot `Configure`는 Context 인자를 받지 않는다. Handler는 application object를 받고 object operation은 그 객체의 Context를 통해 호출한다. Source Context는 relocation commit 뒤 fence되어 새 current-owner operation을 시작하지 못한다. | Actor·Spot object model과 factory·handler contract, 다섯 exact interface·runtime·E2E |
| `CA-D76` | send·request·Spot Actor별 중복 context와 handler invocation context 혼용 / 공통 MessageContext와 명시적 specialization | 모든 inbound message의 공통 정보는 `MessageContext`로 통일하고 nullable MeshName·ChannelName·ContentType·CorrelationId, packet name과 immutable metadata를 제공한다. Route는 source node, Publish는 topic·source, STREAM Session은 reply 가능 여부가 필요하므로 각각 `RouteMessageContext`, `PublishMessageContext`, `SessionMessageContext`를 사용한다. Universal context에는 reply operation이나 connection cancellation을 넣지 않는다. Filter chain의 descriptor·payload·next는 message context가 아닌 `HandlerInvocation`으로 분리한다. | Message model, Channel·Actor·Spot·STREAM handler와 filter, 다섯 exact interface·runtime·E2E |
| `CA-D77` | publish target별 monitoring과 publish 전용 metric·event / source-local 시작 뒤 관측 집계 없는 best-effort publish | Publish는 설정한 timeout 안에서 worker와 source-local capacity를 확보해 작업을 시작하면 결과값 없이 정상 완료한다. 시작 전 timeout·cancellation·shutdown만 typed exceptional completion이며, 시작 뒤 target별 수락·실패를 기다리거나 전체 실패·rollback·자동 retry로 바꾸지 않는다. Logical Multicast snapshot type, MeshNode multicast field, target count field, `zlink.mesh_node.multicast.*` metric과 multicast runtime event를 제거한다. Logical Multicast와 classic fanout publish는 message-flow event를 만들지 않으며 peer·transport·mailbox·shutdown과 classic fanout 연결 상태 같은 공통 monitoring은 유지한다. | Spot messaging, runtime monitoring·metrics·tracing, 다섯 exact interface·E2E |
| `CA-D78` | Session callback의 선택적 disconnect 통지와 message별 route 재조회 / Framework automatic all-bound 통지와 stored exact binding route | Bind 성공 때 Session owner가 Actor별 exact route·generation·lease fence를 저장한다. Relay·request relay와 disconnect는 Location Store를 message마다 조회하지 않고 저장 route를 owner lease·local admission deadline 안에서 사용한다. Physical disconnect는 current binding 전체에 automatic all-settled 통지를 수행하며 exact binding identity마다 Spot callback을 최대 한 번 실행한다. Public `NotifyDisconnected`는 연결이 유지된 상태의 logical notification이다. Relocation route update는 같은 ObjectGeneration에만 허용하고 owner·membership commit, callback·journal replay, durable source cleanup, `Completed`, command 44·45 route switch·ACK, steady normalization 뒤 target admission을 연다. | Session Actor dispatch, Actor·Spot·Location·maintenance, wire schema, 다섯 exact interface·runtime·E2E |
| `CA-D79` | Relocation 완료 상태 `Drained`와 shutdown 진행 상태 `Draining`의 혼동 / `Relocated`와 `Draining` 분리 | Host relocation이 끝났지만 infrastructure를 유지하는 안정 상태와 결과는 `Relocated`다. `Draining`은 `Shutdown`이 이미 수락한 작업과 resource를 정리하는 진행 상태다. `Operational state`라는 범용 이름은 topology 하나의 가용성을 나타내는 `Topology state`로 바꾸며 host lifecycle과 분리한다. | Runtime monitoring·host relocation, metric label, 다섯 host exact interface와 .NET topology exact interface |
| `CA-D80` | Mailbox·queue 변화마다 log·metric 기록 / 실패와 lifecycle 중심의 저비용 관측 | Mailbox enqueue·dequeue와 turn마다 structured log, counter, timestamp와 histogram을 기록하지 않는다. Spot·Actor queue depth·wait, generic mailbox·turn metric과 전체 Location record count를 제거한다. Select-one은 성공마다 계수하지 않고 member를 고르지 못한 실패만 기록한다. Request latency·timeout, peer readiness, capacity, relocation terminal과 Store 오류는 유지한다. C++·.NET·JVM·Node hot path와 OBS-B2·Bingo 검증에서 제거했으며 .NET metric test 16/16, Node typecheck·metric test 11/11, Java compile과 C++ Spot runtime object build가 통과했다. | Runtime monitoring·metrics, 네 runtime, ObservabilityOps E2E와 sample |
| `CA-D81` | `off`에서도 trace 객체와 내부 telemetry message를 만든 뒤 출력만 생략 / hot path 첫 분기에서 trace 작업 생략 | Message-flow diagnostics는 process를 재시작하지 않고 실행 중에 level을 바꿀 수 있다. 각 처리 지점은 trace용 데이터를 만들기 전에 현재 level을 확인한다. `off`에서는 level read와 branch 외에 event·attribute·log 문자열, timestamp·duration, sampling hash, trace 전용 `flow_id`·context, telemetry queue item과 내부 전달 message를 만들지 않으며 provider를 호출하지 않는다. Request/reply 정확성에 필요한 `correlation_id`는 계속 생성·전파한다. 변경 전에 이미 queue에 들어간 기록은 전달하거나 버릴 수 있고, 다시 켜도 이전 단계를 소급해 만들지 않는다. | Message flow tracing·flow correlation, 다섯 runtime control과 allocation·queue contract test |
| `CA-D82` | Object role만으로 Client pair 연결을 생략 / RouteMesh 수신 capability를 함께 판정 | Object role과 RouteMesh Channel role은 독립이다. Object Client에도 RouteMesh Channel Server를 등록할 수 있으며 이 node는 object placement target은 아니지만 Channel target이다. 두 node가 모두 Object Client이고 양쪽 모두 RouteMesh Channel Server membership이 없을 때만 `NotRequired`다. 어느 한쪽에 Server membership이 있으면 weight가 `0`이어도 연결한다. Channel Client, ClientServer와 classic fanout role은 이 판정에 포함하지 않는다. Object Client의 application Node direct handler·target 제한은 유지한다. Automatic descriptor filtering과 Manual handshake admission은 같은 조건을 사용한다. | RouteMesh topology·liveness·monitoring, 다섯 exact interface·runtime, Config 1 RM-A3 |

`CA-D55` review checkpoint(2026-07-24)에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet이
수정된 공통 spec, Redis current·history 13/17 field, 다섯 언어 exact interface, Config 14와
protocol schema·golden을 독립 검토해 clean으로 판정했다. `WIRE` self-test는 37 commands,
161 types, durable fixture 4개와 negative 200건이 통과했고 generated asset 35개와
`git diff --check`도 통과했다. 전체 `DOC` runner는 이 변경이 아닌 병행 C++ public member
trace drift(`expected=4367`, `actual=4386`)에서 중단됐으므로 reviewed trace를 임의로 갱신하지
않았다. Node production runtime은 Pending·Ready startup recovery, exact metadata, application
Instance factory와 handler terminal 뒤 recovery root release까지 연결했지만 public Spot address
transport와 User Spot generic creation coordinator가 남아 있어 `V11-M6B-NODE`는 계속 `진행`이다.

Node Spot address·manager checkpoint(2026-07-24)에서 exact single-use create/get-or-create builder,
authority-first Ready routing, Missing Instance target selection과 command 39 activation, User Spot generic
Reserve·factory·initialize·Commit coordinator를 public host에 연결했다. User Spot creation content는
Location reservation domain만 사용하고 Relocation Store를 요구하지 않는다. 하나의 deadline signal을
store·factory·initialize·commit과 concurrent waiter에 적용하며 `Rejected`는 예외로 바꾸지 않고 Pending을
Abort한 뒤 terminal result로 반환한다. Ready authority는 User·Instance kind와 stable type을 exact
검증하고 ZLIA의 최초 message metadata를 target dispatch까지 보존한다. Handler terminal completion은
replay cursor를 inbox sequence까지 올리는 durable CAS와 recovery pointer를 해제하는 CAS의 두 단계로
분리하며 중간 crash recovery는 handler를 다시 실행하지 않고 pointer release만 재개한다. Public
`SpotHandle`·resolver와 Instance Spot의 Actor·subscription handler surface를 제거하고
`onClosing(context, AbortSignal)`을 exact interface에 맞췄다. Host Shutdown drain은 User·Instance Spot에
`HostShutdown`, explicit close는 `ExplicitClose`를 전달하고 deadline 뒤 callback 대기를 종료한다.
이 checkpoint 당시 구현은 automatic create의 generated RID 충돌을 새 RID로 재시도했다. `CA-D20`이
이를 대체하므로 이 동작과 관련 test는 완료 근거가 아니다. 각 Framework-issued Spot identity sub-ID에서
첫 active collision을 즉시 `RoutingIdConflict`로 끝내고 두 번째 UUID·reservation을 만들지 않도록 수정해야
한다. Explicit get-or-create의 remote owner는 fail-closed한다. Placement와 Store·commit 실패는
`PlacementCapacityExhausted`·`RequestFailed`로 구분한다. TypeScript typecheck·build, M6B 32/32,
M6C 31/31, public contract 27/27, wire validator, scoped ESLint와 `git diff --check`가 통과했다. Codex
`gpt-5.6-sol xhigh`와 Claude Sonnet의 최종 독립 review는 P0·P1·P2 finding 0으로 수렴했다.

Remote User Spot create와 remote Spot close는 현재 service wire에 generation-fenced create·close operation과
terminal correlation이 없어 local-only 구현을 remote 성공으로 확장하지 않았다. Location polling은
`Rejected`·application reply를 보존하지 못하므로 금지하고 두 operation은 명시적으로 실패한다. 이 gap은
`CA-D56`의 protocol schema·네 runtime owner가 소유하며, 해당 command와 cross-node contract가 구현되기
전에는 `V11-M6B-NODE`를 완료로 판정하지 않는다. Sample·E2E source는 변경하거나 실행하지 않았다.

Node contract test suite 정리 checkpoint(2026-07-25, 커밋 `73fd7bf041`..`2a362e0b56`, 이후 인수인계로
세션 종료)에서 `framework/languages/node/test/contract/`를 파일 단위로 실행해 결함을 정리했다.
`spot-activation-state.test.js`(4/4), `spot-manager.test.js`(35/58→58/58), `entry-spot-serial-dispatch.test.js`
(4/22→22/22), `location-key-codec.test.js`(1/2→2/2)를 완전 green으로 만들었다. `stream-runtime.test.js`는
enum re-export 결함 1건만 고치고 여전히 71/84다(중간 커밋 메시지 하나가 이를 84/84로 잘못 적었으니
착오하지 말 것). Runtime 결함 1건을 근본 수정했다. `DefaultZLinkSpotManager.close()`가 close seal이
quiescence에 도달하기 전에 occupancy를 eager 판정해, close 호출 직전 큐에 든 actor join이 quiescence
동안 완료돼도 재확인 없이 Spot을 닫았다. `closeAfterSeal`(spot-activation.ts)이 quiescence 뒤
`canClose()`를 재확인하고 점유 상태면 seal을 풀도록 고쳤으며 `ZLinkSpotCloseOperation.ready`를
`Promise<boolean>`으로 바꿨다(spot-activation-registry.ts, spot-activation-state.ts, index.ts). 이전에
green이던 close·drain 인접 파일 전부(239/239)에 회귀 검증을 마쳤다. `BLK-008`(`onActorJoin`의 spec 문서
텍스트 vs 이미 shipped된 공개 계약·runtime·14개 샘플의 불일치, contract lane 판정 필요)과 `BLK-009`
(hang하는 test 파일 5개: `channel-client.test.js`·`client-server-location-runtime.test.js`·
`stream-connector.test.js`·`stream-connector-codecs.test.js`·`stream-session-runtime.test.js`. 앞 둘은
정확한 hang 지점을 특정했고 병렬 sweep 포트 경합 가설은 배제했다)를 `blocked-issue-log.md`에 남겼다.
아직 손대지 않은 파일은 `location-{store,host,runtime,autoconnect}.test.js`(v10 시절 Location Store
API 전면 재작성 필요 — `updatePeer`·`updateSpot`·`updateActor`·`updateRoute`와 옛 `objectCapacity`
descriptor shape), `nestjs-module.test.js`(27건), `message-flow.test.js`(8건), `contract-surface.test.js`
(2건), `deferred-actor-join.test.js`(1건), `object-message-context.test.js`(1건),
`routing-id-allocation.test.js`(4건), `runtime-execution.test.js`(1건), `tictactoe-session-dispatch.test.js`
(1건)와 `sample-*`·`e2e-*` gate 약 10개(다수 native artifact·Redis 필요 추정, §2.2 예외 영역)다.
`backend-contract.test.js`의 4건은 `BLK-006`대로 손대지 않았다(여전히 30/34). 다음 담당자는
`cd framework/languages/node && npm run build`로 다시 빌드한 뒤 `node --test test/contract/<file>`을
파일 단위로 이어서 실행하면 된다. 이 checkpoint는 `BLK-008`·`BLK-009`를 `V11-M6A-NODE`·`V11-M6B-NODE`
행에 아직 상호 참조하지 않았으므로 다음 ledger 동기화 때 반영이 필요하다.

이어진 실행에서 `BLK-008`을 해결했다. 다섯 언어 정식 spec과 §2.3 기준 구현인 .NET이 모두
`onActorJoin(actorId, request)` identity 형태이고 Node runtime 하나만 객체 형태였으므로 Node를 spec으로
정렬했다(`ZLinkSpot.ts` 공개 계약, 내부 호출부 6곳, TS 샘플 11개, 관련 test). `spot-manager.test.js` 58/58,
`actor-manager.test.js` 69/69이며 `actor-transfer-authority`·`spot-activation-state`·
`entry-spot-serial-dispatch`·`location-key-codec`와 `sample-regression`(44/47)·`sample-spot-lifecycle`(0/3)·
`contract-surface`(26/28)는 baseline을 유지한다. 이 과정에서 admission callback이 joining Actor의
`actorType`을 알 경로가 다섯 언어 어디에도 없다는 계약 공백을 `BLK-014`로 새로 열었다.

`CA-D16`은 두 값을 공개하지만 invalid 조합을 runtime에 넘기지 않는다. `CA-D61`은 `CA-D23`의 generic
population capacity를 대체한다. Population 기본값은 unlimited지만 activation concurrency는 별도 admission
option으로 bounded default를 유지한다. Descriptor와 Store reservation은 `CA-D62`의 typed capacity vector를
같은 의미로 게시·검증한다. `CA-D11`, `CA-D15`, `CA-D17`, `CA-D21`의 byte·count bound는 protocol schema의
정식 bound로 추가하고 네 runtime이 같은 negative fixture를 사용한다.

Impact manifest는 public member, E2E scenario, sample, 실행 registration과 회귀 test를 각각 독립 항목으로
기록한다. 각 항목은 stable ID·path, baseline hash, `retain`·`amend`·`replace`·`add`·`remove`, 새 acceptance
intent, spec owner, runtime owner, activation stage와 approved hash를 가진다. Approved hash는 spec 최종화 전까지
비워 둘 수 있지만 disposition과 대체 coverage는 `V11-R4A` 전에 확정한다. `remove`는 단순 삭제를 뜻하지 않는다.
공개 계약에서 해당 흐름이 없어지는 근거와 같은 의미를 검증할 대체 coverage가 함께 있어야 한다.

Runtime 중 유지할 regression 목록에는 Core·binding raw regression, public declaration snapshot, wire golden·negative,
mailbox ordering, terminal winner, ownership·CAS·lease·fencing과 resource cleanup test를 포함한다. 새 public behavior는
각 언어의 deterministic internal contract test owner를 지정한다. E2E·sample source와 registration은 삭제하거나
주석 처리하지 않고 build·test 실행 graph에서만 분리한다.

R4A에서 확정한 채택 내용은 정식 spec·internals, exact interface, protocol/schema와 impact manifest에 반영했다.
채택하지 않았거나 표현을 바꾼 항목의 결론과 이유는 `CA-D01~CA-D63`이 소유한다. 임시 설계 입력은
`V11-CA-DRAFT-RETIRE`에서 삭제했으며 이후 M6 작업 지시는 정식 문서와 이 ledger만으로 구성한다.

## 10. M6 — Framework vertical slice 네 병렬 lane

각 slice는 C++·.NET·JVM·Node.js가 같은 amended spec·schema revision에서 public interface의 실제 동작을
각각 구현하고 internal contract와 독립 review에서 합류한다. 보존된 Core service 구현·test의 component,
state machine, algorithm, ordering·ownership과 failure 처리를 참고하되 정식 spec·internals와 다른 부분은
목표 계약을 따른다. 언어별 lane은 transport부터 public operation까지 하나의 vertical slice를 완성한 뒤 다음
기능으로 진행한다. 한 언어 구현을 다른 언어가 source 수준에서 포팅하지 않으며, 동작 검증 전에 공통
abstraction을 만들기 위한 대규모 통합 refactoring을 수행하지 않는다.

M6 runtime row는 `M6-RUNTIME`으로 sample·E2E project와 task를 제외한 build, public declaration snapshot,
internal unit·contract·resource·protocol regression만 실행한다. 이 격리는 E2E·sample 실패를 숨기는 skip이 아니다.
Impact manifest의 실행 source와 registration은 계속 `pending-disabled-by-contract-amendment` 또는
`pending-disabled-reviewed-source`이며, review 중인 E2E·sample contract 문서는 `active-contract-spec`이다.
Executed·skipped는 모두 0이어야 한다. Source와
registration을 삭제하거나 runtime 통과용 compatibility helper를 추가하지 않는다.

### 10.1 M6A — Topology, dispatch, Location과 liveness

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6A-CPP` | C++ topology·dispatch·Location·liveness runtime | C++ lane, `P-DEEP` | `V11-R4B` | 수정 진행 | node·Channel·ClientServer·manual·automatic classic fanout, remote placement, mailbox·CAS·reconnect·liveness internal contract 통과 | Public raw ROUTER·DEALER·PUB·SUB API로 node·Channel send/request, ClientServer 독립 admission·send/request, Location descriptor publish/watch/CAS, manual·automatic classic fanout과 publisher별 reconnect, bounded mailbox, terminal-once registry, 5초/15초 liveness를 구현했다. 실제 `mesh_node_runtime` public host가 Framework-owned raw owner를 생성하고 app·host dispatch를 이 경계로 연결한다. ClientServer public configuration은 exact `client()`·`server()` role builder로 분리했고 legacy `enable_client`·`enable_server`와 parent의 role별 socket option을 제거했다. 같은 ChannelName의 dual role과 서로 다른 ChannelName의 동일 역할은 허용하고 같은 ChannelName·role 중복은 socket bind 전에 거부한다. Server RID는 public builder가 아니라 runtime lifecycle identity 경계에서 생성한다. `listen(0)`은 실제 bound port를 보존하면서 advertise host만 descriptor에 반영하며, 같은 process의 local Server도 기존 DEALER→ROUTER admission·request/reply 경계를 사용한다. Contract headers, M6A runtime과 `SameProcessClientServerUsesLocalReadyServerWithoutExternalStoreOrManualEndpoint`, `ClientServerPortZeroPublishesAdvertiseHostWithBoundPort`, 역할 중복·다중 ChannelName을 포함한 resolver 36/36이 통과했다. Compatibility header나 Core·bindings 수정은 없고 Sample·E2E 변경·실행은 0이다. Sample·E2E의 legacy builder callsite는 재활성화 단계 migration gap으로 남긴다. 2026-07-25 자율 실행에서 `zlink_cpp_framework_mesh_node_vertical_test`를 Framework 소유 runtime API로 이관해 전 구간 통과시켰다. 제거된 `zlink::service::*` record type을 `runtime::host::*`로 옮기고, pull batch(drain_ready·ready_batch_t·receive_batch_t·claim)를 `dispatch_ready` push callback으로 다시 썼으며, generated protocol include 누락과 spot·actor handle 시그니처, actor stable type 선언, Location store·runtime 등록과 owner lease 순서를 v11 형태로 맞췄다. Runtime 쪽에서는 `MaxMessageSize` 0(= Framework 추가 상한 없음)이 options→runtime 경계에서 음수 sentinel로 바뀌어 거부되던 spec 위반을 고쳤고(`07-channel-topology.ko.md`), mesh node runtime에 `admitted_peer_count()`를 추가해 검증이 transport topology 내부로 손을 뻗지 않게 했다. Cross-process admission 실패의 원인은 `BLK-007`로 기록·해결했다. `ctest`로 m6a·m6b·m6c runtime과 vertical test를 함께 실행해 4/4 통과했다. 2026-07-29에는 Object Client connection policy를 builder·descriptor publication·automatic planner·manual admission·monitoring·Node direct 분류에 연결했다. 두 Client 모두 RouteMesh Channel Server membership이 없을 때만 `NotRequired`이며 weight `0` membership도 connection을 요구한다. Descriptor generation·revision을 정책 판정보다 먼저 검사해 오래된 Client-only descriptor가 최신 Server membership을 덮지 못하게 했다. Focused `test_cpp_framework_m6a_runtime`은 predicate, stale descriptor fence와 실제 manual pair를 검증했고 admitted peer 0·liveness probe 0으로 통과했다. 정상 CMake configure 뒤 `zlink_framework -j1`, public contract header와 MeshNode vertical target의 build·실행도 통과했다. 최초 configure 실패는 vcpkg protobuf와 system Abseil을 섞은 기존 build cache 때문이며, `absl_DIR`을 같은 vcpkg package로 맞춰 해소했다. RM-A3·SA-E2E-08 actual-process runner 증거는 아직 남아 있으므로 row 상태는 `수정 진행`을 유지한다. 관련 blocked issue: `BLK-006`, `BLK-010`, `BLK-011`, `BLK-012`, `BLK-013`, `BLK-020`. |
| `V11-M6A-DN` | .NET topology·dispatch·Location·liveness runtime | .NET lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·mailbox·CAS·Task terminal winner·liveness internal contract 통과 | 최신 `Systems.Zlink` 11.0.0 package의 public raw API로 managed MeshNode를 구현했다. 실제 두 node admission·remote `ToChannel`을 포함한 foundation 11/11과 backend·monitor·dispatch·Location 62/62가 통과했다. R5A 수정에서 descriptor extension의 필수·unknown TLV와 원본 descriptor bytes를 보존하고, 같은 lifecycle의 revision 증가·같은 revision exact-byte idempotence·immutable field 변경 거부를 mutation 전에 검사했다. Foundation focused regression 13/13이 통과했다. 2026-07-25 fanout checkpoint에서 exact builder·lifecycle RID·전용 descriptor/store와 discovery aggregate, publisher identity별 SUB receive loop, 5초 beacon, 첫 유효 record 뒤 Ready, 15초 inactivity reconnect와 event를 production path에 연결했다. Redis provider는 descriptor fence·paging·channel index와 owner cleanup을 같은 Lua transaction으로 처리한다. Generic auto-connect에 Fanout switch를 추가하는 안과 descriptor lifecycle·connection ownership을 전용 aggregate에 숨기는 안을 비교해 후자를 선택했다. Fanout·non-fanout startup 7/7, Redis fanout 3/3, fanout·monitoring 17/17, builder contract 6/6과 diff-check가 통과했다. `.NET` source에는 physical connection identity 기반 duplicate-pipe 판정과 exact topology compatibility 제거가 남아 있고, Location store exact surface는 별도 convergence lane에서 진행 중이다. 상수로 완료 처리하지 않았다. Sample·E2E 변경·실행은 0이다. 관련 blocked issue: `BLK-001`, `BLK-002`, `BLK-004`, `BLK-006`, `BLK-015`. |

`V11-M6A-DN` Location exact checkpoint(2026-07-25):

- `IZLinkLocationStore`의 public inheritance를 MeshNode descriptor, owner lease와 authority capability로
  줄였다. Spot·Actor projection interface와 In-memory·Redis provider의 projection method를
  제거했다. ASP.NET registration도 hidden interface cast나 별도 registration 없이 public
  `IZLinkLocationStore` 하나로 runtime을 구성한다.
- MeshNode provider enumeration과 operational query를 `ZLinkPageRequest`와
  `ZLinkLocationPage<T>` 계약으로 전환했다. Runtime의 topology·placement consumer는 모든 provider
  page를 내부에서 조립하므로 paging 정책이 application 호출부로 새지 않는다. In-memory와 Redis는
  RID 순서와 opaque continuation을 사용하고 page size `1..1000`을 검증한다.
- 사용처가 없고 exact authority·relocation 계약과 중복되던 `IZLinkActorTransferStore` family,
  in-memory·Redis 구현과 전용 Redis script를 제거했다. `ActorTransfer` change-stamp scope도 함께
  제거했다.
- POSD 검토에서는 기존 projection을 이름만 바꾼 internal adapter로 감싸는 안과 authority payload
  read·mutation으로 lifecycle 자체를 바꾸는 안을 비교했다. 전자는 custom provider가 exact
  `IZLinkLocationStore`만 구현할 때 실패하고 authority 지식을 두 군데 유지하므로 선택하지 않았다.
  Spot·Actor resolver는 opaque authority payload를 decode하고 snapshot의 object generation,
  authority owner generation과 owner lease를 기준으로 location을 만든다. payload에 중복 저장된
  owner 정보가 snapshot과 다르면 row를 거부한다. Spot release는 tracked object·owner generation을
  모두 fence로 검사한 CAS delete만 허용하며, 같은 owner가 더 새 incarnation을 만든 경우에도
  삭제하지 않는다. Entry Spot은 descriptor의 global ID claim과 lifecycle generation을 사용한다.
- Framework·Redis·ASP.NET source build가 warning·error 0으로 통과했고, ScaffoldSmoke와 Location
  Contract focused test 6/6이 통과했다. 기존 projection method를 직접 호출하던 Unit·Redis fixture는
  authority CAS 기반으로 전환 중이므로 전체 회귀가 다시 통과하기 전까지 row를 완료 처리하지 않는다.
- 2026-07-25 fixture checkpoint에서는 Entry Spot Actor destroy·ownership-loss 검증을 legacy Actor
  projection read에서 authority row read로 바꾸고, release retry가 실제 authority CAS delete 실패를
  주입하도록 수정했다. Location contract에서 제거된 Actor·Spot projection을 자체 구현하던 예제도
  삭제하고 `IZLinkLocationStore`의 authority·owner lease transaction domain을 직접 검증한다. Unit
  project compile은 warning·error 0이지만 Object Server test descriptor의 필수 `EntrySpotId` 보강과
  전체 Unit·Contract 재실행은 아직 남아 있으므로 완료 증거로 사용하지 않는다.
| `V11-M6A-JVM` | JVM topology·dispatch·Location·liveness runtime | JVM lane, `P-DEEP` | `V11-R4B` | 수정 진행 | Java·Kotlin API, remote placement, CAS·executor·coroutine·reconnect internal contract 통과 | 최신 `systems.zlink:zlink:11.0.0` public raw binding만 사용해 Framework ROUTER owner와 exact hello·admit·update, Node·Channel send/request/reply, bounded mailbox, Location CAS/watch, placement selector, reconnect와 5초/15초 liveness를 구현했다. Classic fanout connection fence·beacon·timeout contract를 포함한 service·binding regression과 M5 foundation, Java·Kotlin compile이 통과했다. 전체 core test 383개 중 M6B stateful Spot·Actor 구현을 요구하는 기존 6개만 격리됐다. Sample·E2E 변경·실행은 0이다. 관련 blocked issue: `BLK-006`, `BLK-016`, `BLK-017`, `BLK-018`. |
| `V11-M6A-NODE` | Node topology·dispatch·Location·liveness runtime | Node lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·CAS·Promise·event-loop·reconnect internal contract 통과 | Public raw binding만 사용하는 owner에 admission, node·Channel send/request, mailbox, topology·placement, Location CAS, liveness와 전용 ClientServer·fanout registry를 구현하고 public host factory를 연결해 제거된 `createMeshNode` 의존을 없앴다. Framework TypeScript compile, M6A 7/7, M5 4/4와 changed-source ESLint가 통과했다. M6B 기능은 가짜 성공 없이 `NotSupported`로 유지하며 Sample·E2E 변경·실행은 0이다. RouteMesh self-selection을 기대하던 backend fixture 세 건을 실제 TCP peer admission·remote dispatch로 바꾸고, node-direct fixture를 exact MessageContext로 맞춰 `backend-contract.test.js` 34/34를 통과했다. `BLK-006`에 이어 `channel-client.test.js`도 91/91·exit 0으로 정상 종료해 `BLK-009`를 해결했다. 다른 M6A·review 잔여는 계속 추적한다. 관련 blocked issue: `BLK-006`, `BLK-009`. |
| `V11-R5A` | Topology runtime slice 독립 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | 수정 진행 | topology·dispatch·placement·authority·liveness와 실행 격리의 I1·I2·I3 review clean, §2.3 조합 수렴의 I4 clean | Codex 5.6 sol xhigh review에서 확인한 C++ public host의 삭제된 Core Service header·owner 잔존은 Framework raw owner·stateful runtime을 실제 app·MeshNode·Spot·Actor·STREAM host 경계에 연결해 해소했다. 전체 C++ framework compile과 focused regression 5/5가 통과했다. .NET descriptor의 ObjectRole·security·placement/capacity configuration과 physical connection identity 기반 duplicate-pipe 판정은 public-contract parity 후속 조건으로 남는다. Sample·E2E source 변경은 0이다. 지금까지의 round는 Codex `gpt-5.6-sol xhigh`와 Claude `claude-sonnet-5` 두 reviewer가 수행했고, 남은 round는 Codex `gpt-5.6-sol high` 단독 reviewer가 수행한다. |

#### `V11-M6B-CPP` object routing 추가 증거

- [`18-object-routing.ko.md`](../../framework/common/spec/18-object-routing.ko.md)를 우선 기준으로
  stored Session route를 production relay에 적용했다. Bind 뒤 relay와 disconnect는 저장한 exact
  Actor route를 사용하며 message마다 Location Store를 읽지 않는다. 같은 `ObjectGeneration`의
  relocation route switch는 binding generation과 다른 Actor binding을 유지한다.
- 이전 owner의 Actor dispatch는 commit된 Message Follow mapping이 있을 때 application handler나 Location
  Store 조회 전에 target으로 전달한다. 기본 duration 30초, 최대 8 hops, mapping당 1,024 in-flight
  messages와 16 MiB를 적용하며 payload, deadline, metadata와 correlation을 보존한다.
- Command 44·45를 정식 service-wire schema대로 encode·decode하고 command 44 golden bytes를
  고정했다. 실제 두 `public_host_runtime_t` 사이에서 target이 command 44를 보내면 Session owner가
  current owner lease, Actor·object·authority generation, binding generation과 accepted sequence
  high-water를 검사한다. 검사를 통과한 경우에만 해당 Actor route를 atomic하게 바꾸고 command 45
  ACK를 반환한다. 같은 relocation ID와 같은 body를 다시 받으면 route를 재적용하지 않고 같은 ACK를
  반환하며, owner lease 또는 replay high-water가 다르면 ACK를 보내지 않고 기존 route를 유지한다.
- Command 42·43 codec과 실제 peer transport를 연결했다. Session owner는 current owner lease와 exact
  Session·binding, source Actor의 object·authority generation, node lifecycle과 owner lease를 검사하고,
  해당 Actor의 active inbound가 없을 때 ingress barrier와 last accepted sequence high-water를 함께
  고정한다. 같은 command 42 body는 같은 command 43 ACK를 반환하고 같은 relocation ID의 다른 body는
  ACK와 새 barrier를 만들지 않는다. Source 쪽은 command 43 ACK를 검증한 뒤에만 accepted journal
  capture callback을 실행하며, relocation identity·Actor fence·binding generation·high-water와 journal
  bytes를 하나의 immutable Relocation Store root로 저장하고 checksum과 전체 record를 다시 읽어 확인한다.
  같은 seal 재호출은 capture와 Store write를 반복하지 않는다. Command 44는 이 seal record가 존재하고
  아직 소비되지 않았으며 commit high-water 또는 abort authority generation이 일치할 때만 route
  admission을 진행한다.
- `test_cpp_framework_service_wire_codec`, `test_cpp_framework_m6b_runtime`과
  `test_cpp_framework_actor_gateway`를 다시 build·실행했고 세 test가 모두 통과했다.
  Core·bindings·Sample·E2E source는 변경하지 않았다.
- 2026-07-26 Deferred Join 오류 관찰 축을 같은
  [`18-object-routing.ko.md`](../../framework/common/spec/18-object-routing.ko.md)와 Actor Join
  계약에 맞췄다. 별도 public API 없이 factory의 erased callback이 `Accepted`, `Rejected`,
  `Failed`를 모두 기존 `actor_join_completion_t`로 투영한다. Target admission의 정상 거절은
  source Actor에 `Rejected`로 전달하고, admission exception과 route·transfer-out·source leave·commit
  전 실패는 typed `Failed`로 전달한다. 미분류 failure의 `retryable`은 `false`다. Cross-node
  admission request와 target의 durable Accepted에는 source가 만든 같은 non-zero operation ID를
  전달한다. Source route가 commit 준비 중 제거된 뒤에는 보존한 source Spot에서 callback을 실행하며,
  in-flight와 delivered operation ID set으로 병렬 terminal과 완료 뒤 중복 terminal을 막는다.
- Before-negative였던 accepted-only erased projection은 `test_cpp_framework_execution`에서 Rejected의
  optional reply와 `RequestFailed`·비재시도 Failed assertion으로 고정했다. 별도 build
  `framework/languages/cpp/build-v11-tests2`에서 Framework·M6B library와 execution·contract-header
  compile이 통과했고 `test_cpp_framework_m6b_runtime`, `test_cpp_framework_execution`,
  `test_cpp_framework_contract_headers` 3/3이 통과했다. 전체 build는 이 변경과 무관한 기존
  `test_cpp_framework_sample_parity`의 제거된 `spot_node_manager_t`·`spot_rid_t`·이전 Actor message
  context 참조에서 중단됐다. Sample은 이 row에서 수정하지 않았다.
- Deferred Join의 cross-mailbox ordering은 source handler가 끝난 뒤 target mailbox에 Join을 새로
  post하는 방식과 `Defer` 시점에 target Actor mailbox의 다음 slot을 예약하는 방식을 비교했다. 전자는
  handler 실행 중 target에 들어온 후속 turn이 Join보다 먼저 실행될 수 있으므로 사용하지 않았다.
  C++ runtime은 `Defer`가 target Actor execution queue의 front barrier를 즉시 예약하고, source handler가
  정상 종료한 경우에만 그 slot을 Join으로 활성화한다. handler가 exception 또는 async failure로 끝나면
  같은 reservation을 no-op으로 해제하여 뒤의 target turn이 중단되지 않는다. barrier의 생성·활성화·해제는
  serial execution queue가 소유하며 Actor public API에는 새 constructor나 bypass terminal을 노출하지
  않는다.
- `test_cpp_framework_execution`은 low-level queue와 production `actor_context_t::join_spot(...).defer()`
  경로에서 `handler → Join → 이미 대기한 target turn` 순서를 검증한다. handler exception negative는
  Join callback이 실행되지 않고 no-op barrier 뒤의 target turn이 실행되어 deadlock이 없음을 검증한다.
  async handler 결과 실패도 `spot_handler_registry_t`가 `cancel_deferred()`를 호출한 뒤 terminal을
  완료하도록 연결했다. Source queue가 handler 종료 직전에 닫혀 deferred activation post가 거부되는
  negative도 reservation을 no-op으로 해제하고 target 후속 turn을 실행하는 것으로 고정했다. 최신
  compile과 `test_cpp_framework_execution`,
  `test_cpp_framework_m6b_runtime`, `test_cpp_framework_contract_headers` 3/3 실행이 통과했고 scoped
  `git diff --check`도 통과했다.
- 같은 working tree에서 `zlink_framework`, Actor gateway, public header contract, M6B와 M6C target을
  다시 컴파일했다. `test_cpp_framework_execution`, `test_cpp_framework_actor_gateway`,
  `test_cpp_framework_contract_headers`, `test_cpp_framework_m6b_runtime`,
  `test_cpp_framework_m6c_runtime` 5/5가 통과했다. 따라서 C++ Deferred Join의 오류 variant projection과
  cross-mailbox barrier 변경은 maintenance·recovery target까지 compile·focused regression 증거를 가진다.
  Remote process 재시작 recovery와 전체 object relocation 순서는 아래 잔여 조건 때문에 아직 완료로
  판정하지 않는다.
- 앞선 row 증거에 남은 실패로 적힌 `CPP-G0-SPOTHANDLE-001`은 `BLK-022`에서 해결했다. V11 exact
  interface가 유지하는 `spot_ref_t` lifecycle snapshot과 SpotId direct messaging을 서로 다른 계약으로
  gate에 고정하고, spec owner가 없는 `spot_handle_t`·resolver는 금지했다. 다시 컴파일한
  `test_cpp_framework_target_contract`는 `target contract gate satisfied`로 종료했다.
- 실제 Actor execution queue가 journal capture callback을 자동으로 제공하는 연결, Session seal·ACK와
  command 45 terminal의 process restart recovery, target owner lease·Location `Preparing`·`Completed`
  authority의 exact 대조, relocation coordinator에서 command 42를 시작하고 durable root를 aggregate에
  연결한 뒤 command 44를 자동으로 시작하며 ACK 뒤 steady normalization·target admission을 여는 순서는
  아직 남아 있다. Command 44의 source sender role도 wire에 source RID가 없으므로 현재 peer admission만
  검증하며, Store authority와 결합한 exact source-owner 검증은 완료되지 않았다. 같은 relocation ID에
  다른 body가 도착하면 ACK와 route mutation을 거부하지만 peer를 protocol error로 종료하는 연결도
  남아 있다. 따라서 이 증거만으로
  [object routing spec](../../framework/common/spec/18-object-routing.ko.md)의 relocation 순서 전체를
  완료로 판정하지 않는다.
- 2026-07-29 현재 C++ candidate 7개 파일에 대해 정식 `M6-RUNTIME` 10개 command와
  `ROW-GATE`를 다시 실행해 모두 통과했다. Protocol test target에는 production
  `service_topology_registry.cpp`가 사용하는 Framework public header와 `zlink::cpp` dependency를
  명시했다. 증거는
  `.artifacts/v11/evidence/V11-M6B-CPP/checkpoint-current/runtime-regression.json`
  (`sha256:d4c8bd638941f7c4725d53a88b1cef152fefd5f1257e6006c7ba0e35a25366aa`)이며,
  candidate와 owned-path manifest SHA-256은 각각
  `e29da18095cc2c743d867745a530d5ca2592c55961ea0cf0683cbab6b44827ec`,
  `66e83a824a51070d1795c4708d92fde87bdef4e484810e555d535be49af9abee`다.

Framework service runtime은 제거한 Core heartbeat option을 설정하지 않는다. Raw monitor는 orderly disconnect를
즉시 알리고 Framework liveness probe scheduler가 half-open deadline을 소유한다. Location owner lease와
Framework STREAM heartbeat는 이 deadline을 authority나 application session progress로 재사용하지 않는다.
Fanout subscriber는 publisher별 전용 SUB socket을 사용하며 reserved beacon을 application message로
전달하지 않는다. Publisher 하나의 deadline은 다른 publisher의 ready 상태를 바꾸지 않는다.

### 10.2 M6B — Spot, Actor, STREAM과 Instance Spot

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6B-CPP` | C++ stateful object runtime | C++ lane, `P-DEEP` | `V11-R5A` | 수정 진행 | turn·membership·session·Instance activation과 stale generation contract 통과 | Framework-owned generic object authority에 MeshName과 분리한 global identity, weighted remote create reservation·Ready barrier, concurrent create join, exact-generation destroy, cross-node membership CAS와 application·infrastructure turn 분리를 구현했다. 이동 중 ingress hold와 commit·abort queue handoff, logical timer registration·pending tick 보존, Instance marker cold activation, STREAM connection·binding generation과 exact Actor authority fence를 검증한다. Public raw ROUTER의 exact Spot·Actor route fence send/request/reply와 stale fence terminal-once를 실제 public host에 연결했다. `mesh_node_runtime`, Spot·Actor dispatch, RouteMesh monitoring과 STREAM session binding은 제거된 Core Service owner 대신 Framework raw owner·stateful registry를 사용한다. `CA-D56`은 exact `spot_manager_t` fluent surface와 `spot_ref_t`, Location `reserve()`에서 command 47·48로 이어지는 source·target production 경로, concurrent Creating join, `inline-v1` creation reference 검증과 same-node raw command loopback을 구현했다. 공식 Redis provider도 provider-issued reservation ID, content reference, SHA-256과 encoded size를 Pending current·history snapshot에 저장하고 Commit에서 current projection을 제거하며, runtime은 provider별 side interface 없이 public `authority_snapshot_t::pending_creation`을 사용한다. 이 checkpoint의 `Create` generated RID active-collision retry는 `CA-D20`이 대체했으므로 완료 근거가 아니다. `V11-M6B-ENTRY-IDENTITY-CPP`에서 첫 collision 즉시 `RoutingIdConflict`, 추가 UUID·reservation 0건으로 수정해야 한다. Caller RID를 받은 `GetOrCreate`는 같은 Ready incarnation을 `Existing`으로 반환하며 type mismatch 의미를 유지한다. Command 47·48 source는 transport timeout, typed wire failure와 target conflict를 기존 public error kind인 `DeadlineExceeded`, `SpotGenerationStale`, `SpotMoving`, `SpotTypeMismatch`, capacity와 rejected 의미로 보존한다. Target은 type mismatch·stale generation·moving terminal을 exact code로 저장하고 같은 operation replay에도 그대로 반환한다. Command 47 source는 자신이 만든 Pending reservation만 exact key·reservation fence로 추적하고 submit throw·not-admitted와 authoritative failure/rejected terminal에서 즉시 abort/reconcile한다. Concurrent Creating join과 timeout·disconnect처럼 target outcome이 불명확한 terminal은 cleanup하지 않으며, 이미 Commit된 authority에는 exact abort가 stale로 끝나므로 successor를 변경하지 않는다. `18-object-routing.ko.md`를 우선 기준으로 direct Spot·Actor의 positive Ready cache→Store miss, `Missing`·`Creating`·Store failure의 negative cache 금지와 같은 operation 자동 재시도 금지를 대조했다. Instance Spot command 39는 source·target lifecycle fence와 reply correlation을 검증해 remote infrastructure mailbox로 전달하고 terminal reply를 한 번 반환한다. `instance-activation-recovery-v1` C++ codec은 정식 `ZLIA` golden bytes와 CRC32C를 일치시켰다. Target은 전체 activation envelope를 Relocation Store에 먼저 저장하고 checksum을 검증한 뒤 authority를 예약하며, factory·configure·initialize가 끝나도 Ready authority가 commit되기 전에는 최초 message를 handler에 제출하지 않는다. Ready payload는 operation과 request SHA-256, durable terminal reply를 보존한다. 같은 operation은 fingerprint가 일치할 때만 terminal을 replay하고, payload가 다르면 handler를 다시 실행하지 않고 거부한다. Terminal CAS 뒤 authority의 recovery reference를 제거한 다음 Relocation root를 삭제하며, process restart 시 local Ready authority의 남은 reference를 scan하여 handler와 cleanup을 이어간다. Source의 one-way terminal은 outbound admission에서 끝나고 request는 command 39 terminal을 한 번만 반환한다. Missing은 cache하지 않고 별도 call마다 Store를 다시 읽으며, 각 operation에서 activation을 자동 재시도하지 않는 focused test를 추가했다. `test_cpp_framework_service_wire_codec`, `test_cpp_framework_instance_spot_activation`, `test_cpp_framework_contract_headers`, `test_cpp_framework_store_location_resolvers`, `test_cpp_framework_target_contract`, `test_cpp_framework_m6b_runtime` 6건이 통과했다. `26-object-routing`의 stored Session binding route와 bounded stale forwarding, 다른 target에서 이미 진행 중인 command 39 Creating operation의 durable join·대기는 이 checkpoint에서 완료 증거로 삼지 않는다. Redis suite 25건 중 endpoint round-trip을 포함한 23건이 통과했고 cross-language 전용 prefix가 필요한 2건만 skip됐다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. 관련 blocked issue: `BLK-014`, `BLK-020`. |
| `V11-M6B-DN` | .NET stateful object runtime | .NET lane, `P-DEEP` | `V11-R5A` | 수정 진행 | turn·membership·session·Instance Task 경쟁 contract 통과 | Framework-owned managed MeshNode에 owner별 application·infrastructure mailbox와 claim, local·remote Spot·Actor exact-generation dispatch, accepted join membership commit, lifecycle, logical multicast, Actor request terminal CAS, relocation seal·abort·commit, STREAM exact Actor binding·relay와 Instance Spot reactivation generation을 구현했다. R5B review에서 확인한 무제한 request operation table은 기본 65,536개 capacity를 ID·deadline 할당 전에 원자적으로 검사하고 request에는 `Backpressured`를 반환하도록 수정했다. Inbound remote Spot·Actor는 queue admission 전에 target node lifecycle, object generation과 Location row에서 받은 authority owner generation을 검사한다. Object Server가 Recreate·Snapshot policy를 하나라도 등록하거나 Instance Spot factory를 하나라도 등록하면 Relocation Store 누락을 socket bind 전 configuration validation에서 거부한다. Disabled Actor·Spot만 등록하고 Instance Spot factory가 없으면 요구하지 않는다. `CA-D56`은 exact `IZLinkSpotManager` fluent surface, `SpotRef`, public Create·GetOrCreate·Find·Close, command 47·48, client-only source, concurrent Creating join, terminal replay, Ready staging, close rollback과 InMemory·Redis Pending projection을 production path에 연결했다. Generic User Spot request는 Relocation Store가 아니라 Location reservation의 inline envelope를 사용한다. Stateful 16/16, Redis authority 9/9와 Framework·Unit build warning·error 0이 통과했다. 전체 unit에는 기존 documentation regression 7건과 ClientServer timing·liveness 2건이 별도 실패로 남아 있고, fixed packaged verifier는 기존 package snapshot의 Systems.Zlink 10.1.0/11.0.0 및 XML hash 불일치에서 중단된다. 2026-07-25 자율 실행에서 command 39·47·48 remote 경로의 결함 두 건을 근본 수정했다. (1) `WriteOptionalText8`이 `optional-text8`에 없는 marker byte를 덧붙여 command 39 body가 자기 decoder와도 round-trip되지 않았다. Wire schema의 `optional-text8`(u8 length, length 0 = absent)에 맞춰 marker를 제거했다. (2) User Spot terminal reply가 raw payload part를 envelope header로 무조건 decode해 `ZLinkEnvelopeProtocolException`이 fire-and-forget task에서 터지고 reply가 나가지 않아 요청자가 `TimedOut`으로 끝났다. Envelope가 아닌 terminal payload는 correlation 재작성 없이 그대로 전달하도록 고쳤다. 변경 path는 `framework/languages/dotnet/src/Zlink.Framework/Runtime/Service/ZLinkServiceWireCodec.cs`와 `.../Runtime/Service/ZLinkManagedMeshNode.cs`다. `dotnet test --filter FullyQualifiedName~StatefulServiceRuntimeTests` 20/20이 통과했고 전체 UnitTests는 774/791에서 788/791로 올라갔다. 남은 3건은 ClientServer unsolicited router→dealer 전달 2건과 `BLK-003`의 deferred Join 완료 순서 1건이며 이 row의 gate와 별개다. 이전에 실패하던 documentation regression 7건은 재빌드된 doc 입력에서 통과한다. 실제 Instance durable activation 연결을 완료하기 전에는 이 row를 완료로 판정하지 않는다. Sample·E2E 변경·실행은 0이다. 관련 blocked issue: `BLK-001`, `BLK-014`. |
| `V11-M6B-JVM` | JVM stateful object runtime | JVM lane, `P-DEEP` | `V11-R5A` | 진행 | Java·Kotlin turn·membership·session·Instance contract 통과 | Framework가 소유하는 Spot mailbox와 lifecycle generation, local·remote Spot send/request single-terminal·timeout, Actor create·join accepted commit·membership epoch·leave lifecycle과 local·remote Actor owning-Spot dispatch를 public raw binding 위에 구현했다. Logical Multicast는 local subscription fan-out과 remote MeshNode별 ROUTER command 23 전송을 연결한다. Executor가 작업을 시작한 뒤에는 target별 수락 결과를 집계하거나 public·internal monitoring에 기록하지 않고, 일부 target 전달 실패를 전체 operation 실패나 자동 retry로 바꾸지 않는다. Spot resolver와 Actor Location row에서 받은 `AuthorityOwnerGeneration`을 raw route registry와 wire에 전달하며 receiver는 target node lifecycle, object generation과 authority owner generation을 application admission 전에 exact 비교한다. command 39는 closed codec, source·target node lifecycle, 등록된 exact Instance authority fence, object generation을 검증한 뒤 stable-type cold activation barrier와 Spot mailbox로 dispatch한다. Remote bound STREAM은 command 36·38 closed codec과 exact Actor·object·authority·node·binding·session generation fence를 사용해 bind·send·close를 owner node로 전달한다. `CA-D56`은 canonical stable-type Spot manager call builder, startup descriptor publication, Location inline creation envelope, command 47·48 actual peer transport, semantic fingerprint·terminal replay, factory-once Ready barrier, close rollback과 InMemory·Redis Pending projection을 연결했다. Durable authority runtime은 startup scan·watch subscription·재scan 순서로 `PENDING + CREATING Instance`를 command 39 intent에 자동 등록하고, `ACTIVE + READY`가 되기 전에는 일반 Spot route로 공개하지 않는다. Watch는 재scan 신호로만 사용하며 authority generation을 event나 legacy explicit `ActorRef`에서 추정하지 않는다. Core 470 tests, actual peer M6B 19/19, Kotlin compile·test와 Redis unit·fixture가 통과했고 post-fix review는 `APPROVE`했다. Redis endpoint 의존 12건은 환경변수 부재로 skip됐고 전체 Spring suite의 기존 monitoring·handler 환경성 6건은 별도 실패지만 이관한 canonical Spot manager bean test는 통과했다. Sample·E2E 변경·실행은 0이다. 관련 blocked issue: `BLK-014`, `BLK-016`. |
| `V11-M6B-NODE` | Node stateful object runtime | Node lane, `P-DEEP` | `V11-R5A` | 진행 | turn·membership·session·Instance Promise 경쟁 contract와 public command 47·48 native two-process contract 통과 | public raw binding 위에 Spot·Actor exact-generation dispatch, owner별 serial turn, membership epoch, logical multicast local fan-out·remote node당 1회 전송, STREAM binding generation·delivery와 terminal-once Promise registry를 연결했다. Instance Spot stable type·attempt reservation, command 39 exact route·source·authority fence와 registered intent cold activation generation도 연결했다. Request operation table은 기본 65,536개 capacity를 ID·timer 할당 전에 검사한다. `CA-D55`는 complete activation envelope, Pending projection, Ready recovery root·cursor, startup exact scan·queue-head restore, descriptor mismatch abort·orphan cleanup을 연결했다. `CA-D56`은 public manager와 host의 command 47·48 source·target, inline Location creation envelope, exact Pending integrity, Ready staging, concurrent reservation completion join, close rollback과 deadline+5분 terminal replay를 연결했다. Framework startup은 공식 Location Store에 owner lease로 MeshNode endpoint·object role·User Spot·Instance Spot·Actor capability를 durable publication하며, 빈 Redis authority page도 정상 처리한다. 별도 process 두 개가 public Nest builder와 `ZLINK_SPOT_MANAGER`, 공식 Redis provider를 사용해 remote User Spot Create/GetOrCreate/Find/Close를 실제 native socket command 47·48로 완료했다. Post-review P1 수정에서 MeshNode descriptor publication을 Location runtime 경계로 모으고 Core가 확정한 endpoint와 exact·legacy effective capability를 사용해 Preparing revision 1, Serving revision 2, Draining revision 3 이상을 strict하게 게시하도록 고쳤다. Command 48의 Closing CAS·local close·authority delete는 target만 소유하며 terminal reply 유실 뒤에도 Ready authority를 복원하지 않는다. `ZLinkActorFactory.create`는 exact optional AbortSignal을 받고 일반 actor creation이 같은 signal을 전달한다. 후속 독립 review에서 RouteMesh listener identity의 bind·advertise host exact surface, application version·maintenance wave·security identity 설정, command 48 membership seal과 bounded cleanup signal, 실제 transport terminal replay 증거가 남아 있음을 확인했다. Native Actor Join 후속 수정은 authority에서 해석한 User Spot route를 stateful cache에 설치하고, initial Actor authority와 Lazy Entry Actor를 actual ActorRef generation·owner fence로 materialize한다. Source 정상 reply 뒤 transfer를 시작하고 target `Joined`·Location commit·durable commit ACK·`onJoinCompleted(Accepted)`를 terminal-once journal cursor로 보존한다. Bound session 좌표와 generation은 transfer snapshot에 포함하고 target native registry에 같은 fence로 복원한다. Focused contract 8건과 TypeScript build가 통과했고 실제 process `ST-B1`과 `ST-E1`도 각각 `framework/languages/node/e2e/SpotActorTransfer/log/20260725-092511-2310951`, `framework/languages/node/e2e/SpotActorTransfer/log/20260725-092454-2307879`에서 통과했다. 남은 listener identity·command 48 transport terminal replay 조건 때문에 이 행은 계속 진행이다. 관련 blocked issue: `BLK-005`, `BLK-008`, `BLK-009`, `BLK-014`. |
| `V11-R5B` | Stateful runtime slice 독립 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | 수정 진행 | global identity, remote create, mailbox ordering, ownership·fencing·resource와 실행 격리의 I1·I2·I3 review clean, §2.3 조합 수렴의 I4 clean | Codex 5.6 sol xhigh review에서 세 managed language의 authority owner generation 미연결, command 39 durable Instance cold activation 미연결, JVM logical multicast no-op, 세 언어의 무제한 request operation table과 JVM mailbox close retained payload를 P1으로 확인했다. JVM mailbox close, JVM·.NET·Node operation capacity 수정과 focused regression은 통과했다. JVM은 정식 authority 결과·mutation·aggregate 계약과 public provider registration seam을 추가했으며, 공식 Redis provider와 durable publication 연결은 M6C에서 계속한다. authority·Instance·logical multicast 연결은 언어별로 병렬 수정한다. Sample·E2E source 변경은 0이다. 지금까지의 round는 Codex `gpt-5.6-sol xhigh`와 Claude `claude-sonnet-5` 두 reviewer가 수행했고, 남은 round는 Codex `gpt-5.6-sol high` 단독 reviewer가 수행한다. |

`V11-M6B-DN` command 39 checkpoint(2026-07-24):

- command 39 route를 `Ready` authority fence와 target-owned `ColdActivation` descriptor fence의 닫힌
  union으로 고정했다. 기존 Ready route의 version byte `1`과 body는 유지하고 ColdActivation은 version
  byte `2`에서 target Mesh, stable type, descriptor version과 deadline을
  전달한다. ColdActivation route와 `ZLIA`가 이 값을 exact하게 보존해야 한다.
- 공통 schema·golden·validator·decoder fixture와 다섯 언어 exact interface를 같은 계약으로
  갱신했다. unknown route kind, Ready route의 cold field, ColdActivation route의 authority fence와
  route/`ZLIA` authority fence와 deadline 불일치를 negative self-test로 거부한다.
- .NET managed MeshNode는 ColdActivation command 39 source submit, admitted peer와 source·target lifecycle
  검증, metadata와 first-message frame 보존, target dispatch와 request terminal 반환을 연결했다.
  schema validator, generated asset check, decoder fixture verifier와 command 39 codec·actual peer focused
  test가 통과했다.
- durable activation은 아직 완료하지 않았다. target factory·initialize barrier, canonical binary
  `ZLIA` 저장, provider-issued Pending reservation, Ready publication, startup Pending recovery, durable
  terminal·cursor와 Preserve cleanup을 하나의 production coordinator로 연결해야 한다. 이 연결 전에는
  `V11-M6B-DN`을 완료로 판정하지 않는다. 검토 중 작성했던 별도 JSON coordinator는 canonical
  authority payload와 충돌하고 loser·recovery·cleanup correctness를 보장하지 못해 소스에 남기지 않았다.

.NET Actor creation checkpoint(2026-07-24):

- Public `IZLinkActorManager`를 single-use `Create`·`GetOrCreate` fluent call과 `Created`·`Existing`·
  `Rejected` result로 전환하고 별도 public Actor directory와 caller-selected placement를 제거했다.
  Source·sample·E2E 소비 코드는 삭제하지 않고 같은 public manager 계약을 사용하도록 맞췄다.
- Managed MeshNode command 49에 source·target lifecycle fence, semantic `OperationId`, duplicate body 검증,
  target single execution과 retained terminal replay를 연결했다. Backend wrapper는 remote request와
  application reply를 completion pump로 반환하며 local factory로 우회하지 않는다.
- Source manager는 `Serving`, Actor capability, Actor aggregate active·reserved capacity와 positive
  node-wide weight로 target을 선택하고 generic `Reserve`의 capacity race에서 해당 후보를 제외해 다시
  선택한다. Target은 immutable creation intent를 검증하고 Actor factory와 Entry Spot create callback을
  staging 상태에서 실행한 뒤 `CompleteCreation`으로 Ready authority와 `Created` terminal을 함께
  publish하거나 `Rejected`·`Failed` terminal과 reservation cleanup을 함께 확정한다.
- staging Actor는 Ready commit 전 `Find`와 dispatch에서 제외하며 commit 성공 뒤에만 publish한다.
  command reply의 correlation은 retained semantic terminal에 저장하지 않고 현재 전달의 correlation으로
  다시 encode한다.
- Public `FindAsync`와 `FindSpotAsync`는 local registry가 아니라 global Ready Actor authority를 읽어
  remote `ActorRef`와 current `SpotRef`도 반환한다. Public `DestroyAsync`의 remote owner command는 아직
  production 경로가 없으므로 remote Actor를 local miss인 `false`로 숨기지 않고 typed gap으로 실패한다.
  remote destroy와 generic authority Delete가 연결되기 전에는 Actor manager 완료로 판정하지 않는다.
- Checked-in `Zlink.Framework.Locations.Redis.Tests/ActorCreateCommandRuntimeTests`에서 실제 inproc 두
  node를 연결해 첫 `Rejected`, 같은 `OperationId` 재전달의 target 실행 횟수 유지, 별도 operation의
  두 번째 target 실행을 검증했다. 동일 process의 object server도 Actor·User Spot의 일반 placement
  후보에 포함되는 조건을 함께 고정했고 focused test 2/2가 통과했다. Redis authority suite는 Actor
  pending·active population projection과 pending·active capacity overflow를 포함해 12/12가 통과했다.
  저장소 UnitTests project는 기존 SpotId·mock interface 미전환 compile gap 때문에 추가한 정식 codec
  test를 아직 실행하지 못했다.
- Actor와 User Spot source manager는 local RID를 후보에서 제외하지 않는다. 선택한 target이 같은
  process이면 같은 target coordinator를 직접 실행하되 reservation fence, source lifecycle,
  `OperationId`, immutable intent, staging과 Ready publication 규칙을 remote command와 동일하게
  적용한다. Descriptor 조회부터 reserve, Creating 대기와 target callback까지 각 submit 시작 시 계산한
  하나의 absolute deadline과 연결된 cancellation token을 사용하며, remote transport에는 남은 시간만
  전달한다. User Spot의 선행 Creating이 cleanup되면 해당 caller가 새 reservation을 다시 경쟁한다.
  Production InMemory integration은 같은 process server의 local Actor create와 첫 target `Reserve`의
  capacity race 뒤 두 번째 remote node의 factory·Entry Spot callback 성공을 직접 검증한다. 이 과정에서
  Entry Spot row가 node lifecycle generation `0`을 publish해 모든 후보가 fence에서 탈락하던 문제를
  발견했다. Node startup은 non-zero Core lifecycle generation을 bounded wait한 뒤 같은 값을 Entry row,
  descriptor와 command target fence에 사용한다. Reserved Actor staging은 Ready 전 public dispatch만
  차단하고 factory의 context·native Actor binding은 허용한다. Framework build와 command 49
  replay·local target·capacity-race focused test 4/4가 통과했다.
- exact capacity public shape를 `ZLinkPopulationCapacity`, `ZLinkSpotTypeCapacity`,
  `ZLinkPlacementCapacity`로 정리하고 InMemory·Redis projection과 Lua admission을 맞췄다. Framework와
  Redis provider build, packaged contract generation, capacity fixture·admission·projection 6건이 통과했다.
  실제 Actor manager의 target capacity race와 cross-node callback까지 묶은 checked-in integration
  test도 위 focused 4/4에 포함했다.
  서로 다른 concurrent `GetOrCreate` operation은 앞선 `Rejected` reply를 공유하지 않고 cleanup 뒤 새
  reservation을 경쟁하며, 동일한 `OperationId` 재전달만 retained terminal을 재사용한다. 이 coordinator의
  cross-node·rejection·failure·capacity race test가 통과하기 전에는 `V11-M6B-DN`을 완료로 판정하지 않는다.

Node placement·Logical Multicast checkpoint(2026-07-24):

- `CA-D70`에 따라 Node public call, factory option, descriptor capability·digest, creation intent와
  InMemory·Redis admission에서 caller-defined placement selector를 제거했다. 최초 배치는 stable type,
  `Serving`, active·pending capacity와 node-wide weight만 사용한다.
- `CA-D71`의 publish 결과는 remote source outbound transport queue 제출과 origin local Spot application
  queue 제출만 집계한다. Remote Spot queue와 remote·local handler 실행·완료는 기다리지 않는다.
- Node build, InMemory·Redis Location contract 15/15와 관련 Spot·Logical Multicast focused 4/4가 통과했다.
  전체 typecheck의 `sourceSpotRid`→`sourceSpotId` 테스트 상수명 1건은 병렬 SpotId 전환 범위다.
- Root service wire schema·golden에서도 세 selector field를 제거했다. Validator self-test는 40 commands,
  167 types와 negative 233건, generated asset write/check가 통과했다. .NET Framework build는 기존 nullable
  warning 23건 외 성공했고 Redis provider build는 warning·error 0, Redis Location 25/25와 Authority
  Relocation 10/10이 통과했으며 .NET source·test·API snapshot의 해당 identifier 검색 결과는 0건이다.
- Framework overview·API, Instance Spot E2E와 service wire internals의 남은 표현도 `CA-D70`에 맞췄다.
  `framework/doc/framework` 전체에서 제거한 세 selector identifier와 문구의 검색 결과는 0건이며 cold
  route·ZLIA는 target Mesh·stable type·descriptor version·deadline만 보존한다.

Placement option 독립 review checkpoint(2026-07-24):

- Codex 5.6 sol xhigh review에서 제거 대상 identifier가 정식 spec·exact interface·service wire와 현재
  public source에서는 제거된 것을 확인했다. 다만 obsolete Actor placement surface, 언어별 typed capacity
  선필터와 reservation 재선택, JVM direct Spot publish detail, stale public-contract trace와 일부 test
  compile gap을 완료 차단 이슈로 분류했다.
- Common Instance Spot E2E에 남아 있던 profile·affinity 조건 세 곳을 stable type·capacity·node-wide
  weight로 고쳤다. Logical Multicast의 remote capacity는 수신 Spot queue가 아니라 remote target으로 향하는
  source-local outbound transport queue의 capacity임을 common spec과 submit-admission E2E에 명시했다.
- C++에서 obsolete `actor_placement_t`·`preferred_node_rid`·`actor_directory_t::ensure`를 제거하고 Actor
  target surface를 exact interface에 맞췄다. Actor·Spot aggregate와 Spot stable type별
  `active/reserved/limit` projection, Redis codec·immutable digest·Lua capacity gate, Actor·User Spot
  capacity-first weighted selection과 capacity·stale target 실패 후 bounded candidate 재선택을 구현했다.
  C++ framework link, M6B first-fail→second-success test, Redis projection·digest test와 InMemory Location
  Store test 1/1이 통과했다.
- JVM도 같은 exact capacity projection을 InMemory·Redis descriptor, digest와 Lua admission에 적용했다.
  Actor·User Spot은 실제 descriptor usage로 후보를 먼저 거르고 reserve race에서는 다른 lifecycle을
  재선택한다. Direct Spot publish는 backend의 source-local detail 7개를 그대로 반환한다. 실제 Redis
  suite를 포함해 Core 485, Redis 32(+2 skipped), Kotlin 45 test와 exact Location Store contract test가
  통과했다.
- Public-contract review baseline은 제거한 selector와 새 fluent Actor surface를 반영해 refresh했다.
  Trace write·check는 member 6,681개, intentional removal 1,218개, unclassified·ambiguous·unknown 0으로
  통과했다. Contract amendment impact policy에는 `CA-D65` SpotId, `CA-D58~59` Yield,
  `CA-D67` lifecycle과 reviewed exact surface 제거 근거를 닫았고 manifest 4,056개를 write·check했다.
- 전체 framework document verifier는 SpotId 전환 범위의 common internals semantic,
  authority·MeshNode fixture와 .NET·JVM·Node amended object contract를 정식 계약에 맞춘 뒤 다시
  실행했다. Service wire 233개 negative self-test, public trace, exact interface 56개, formal document
  137개와 Redis fixture 5개를 포함한 전체 검증이 통과했다.
- 후속 .NET production 검증에서 MeshNode 시작 직후 Entry Spot row의 owner node generation이 `0`으로
  게시될 수 있는 race를 확인했다. Runtime은 Core가 non-zero lifecycle generation을 게시할 때까지
  bounded wait한 뒤 Entry Spot location을 claim하므로 descriptor, Entry location과 command target fence가
  같은 generation을 사용한다. 실제 manager test는 same-process target 생성과 첫 candidate의 capacity
  reservation 실패 뒤 두 번째 remote node의 factory·Entry callback 성공을 검증했고 command 49 replay
  test와 함께 4/4가 통과했다.
- 최종 focused review에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet은 `CA-D70`·`CA-D71`, .NET command 49,
  local·remote 동일 후보 규칙, capacity race 재선택, 하나의 absolute deadline, reservation·lifecycle
  fence, operation-scoped terminal replay와 staging visibility를 다시 대조해 actionable finding 없이
  `CLEAN`으로 판정했다.

Placement·publish 후속 drift 점검(2026-07-24):

- 정식 spec과 통합 중인 common 사본에서 남아 있던 구형 `ZLinkObjectPlacementOptions`,
  `object_placement_options_t`와 `placement option` 표현을 Actor·User Spot·Instance Spot별 factory option
  계약으로 맞췄다. .NET exact interface의 고아 initializer 세 개와 Java `javap`의 제거된
  `Set<String>` constructor도 정리했다.
- `PlacementProfile`·`AffinityKey` 계열은 결정 이력을 제외한 Framework spec·public source·wire·test·E2E에서
  0건이다. Instance Spot verifier에는 두 selector의 재도입을 거부하는 negative mutation을 추가했다.
- Logical Multicast의 다섯 언어 exact interface와 public source는 bounded worker와 source-local capacity를
  확보해 publish transaction을 시작하면 결과값 없이 완료하며 target별 제출 결과를 집계하지 않는다. Node binding 제거 뒤 남아 있던 commit fixture를 Framework
  MeshNode backend로 전환했으며, pre-start cancellation은 mock test, post-start cancellation은 실제 backend
  test가 각각 소유한다.
- 검증은 Instance Spot contract 5개 언어·formal 문서 5개·negative 16건, submit API 5개 언어·scenario
  20개·regression 4건, .NET Logical Multicast 3/3, JVM publish focused test와 Node Logical Multicast
  7/7이 통과했다. Contract amendment manifest 4,056개도 제거된 affinity scenario 이름 없이 재생성하고
  write/check를 통과했다.
- 실제 process E2E `SA-E2E-13`은 .NET·Java·Node·C++ feature map에서 미구현 상태이므로
  `V11-M6A-E2E` 완료 근거로 사용하지 않는다.
- 이번 runtime 합류 뒤 root가 Instance Spot verifier를 다시 실행해 언어 5개·formal 문서 5개,
  required fragment 112개와 forbidden rule 11개가 모두 통과했다. 현재 verifier invocation의
  optional negative mutation 입력은 0개였으며 이를 이전 16건 실행과 합산하지 않는다.

JVM Entry Spot identity checkpoint(2026-07-24):

- Object Server registration은 caller가 Entry Spot ID를 지정하는 표면을 제거하고
  `<diagnostic-prefix>-entry-<lowercase-canonical-uuid-v4>`를 발급한다. Descriptor는 exact
  `entrySpotId`를 immutable digest에 포함하며 In-memory와 Redis provider가 descriptor identity와 global
  Entry identity를 같은 admission에서 claim하고 exact owner cleanup으로 해제한다.
- Redis v3 Entry identity HASH를 `state`, `spotId`, `descriptorKey`,
  `lifecycleGeneration`, `ownerId`, `ownerLeaseGeneration` 여섯 field와 TTL 없음으로
  고정했다. Real Redis test가 schema, active collision, exact cleanup과 stale cleanup 무효를 검증한다.
- User Spot `GetOrCreate`는 Framework Entry ID 예약 형식을 call 생성 시점에
  `InvalidConfiguration`으로 거부하므로 Store read·reservation과 factory 실행 전에 끝난다.
- Java core 488/488과 Redis provider 35건 중 환경에 따른 2건 skip·실패 0이 clean rebuild에서
  통과했고 Kotlin runtime source도 다시 compile했다.
- `V11-M6B-ENTRY-IDENTITY-JVM`은 완료로 전환하지 않는다. Public
  `ZLinkActorContext.joinEntrySpot(...)`이 아직 caller의 target `RoutingId`를 받고, runtime의 일부
  Entry join·membership path가 descriptor의 String SpotId mapping 대신 target node RID를 요구한다.
  Exact `joinEntrySpot(request)`와 eligible Entry descriptor 선택으로 전환하고 Java·Kotlin ABI·runtime
  test를 통과해야 row를 닫을 수 있다.

Node Weight·Entry identity checkpoint(2026-07-24):

- Placement selection의 합계를 `BigInt`로 계산하고 weight `0`을 Logical Multicast remote target에서
  제외했다. Boundary, capacity-first와 100:300 deterministic selection을 포함한 M6A runtime 8/8,
  typecheck와 package build가 통과했다.
- Framework-issued Entry·User Spot UUID v4, in-memory descriptor·global identity claim, first-conflict
  무변경과 exact cleanup을 구현했다. Redis v3 provider는 shared fixture의
  `entry-spot-id:<sha256>` key와 여섯 field만 기록하며 generic User·Instance reservation 충돌도 같은 Lua
  transaction에서 처리한다. In-memory 5/5와 실제 Redis fixture·claim focused test가 통과했다.
- `V11-M6A-WEIGHT-NODE`는 runtime `mesh(meshName).placementWeight` mutation과 descriptor republish가
  아직 없어 완료로 전환하지 않는다. `V11-M6B-ENTRY-IDENTITY-NODE`도 기존 Spot path의
  `SpotRid`·`RoutingId`가 전부 String `SpotId`로 바뀌지 않아 대기 상태를 유지한다.

.NET capacity parity checkpoint(2026-07-24):

- Redis creation·relocation capacity focused test 4건은 typed Actor·Spot·Spot type bundle의 admission,
  commit 재검증과 abort cleanup을 통과했다.
- .NET provider는 descriptor JSON의 `ActivationConcurrency`, admission HASH의 activation concurrency
  limit·Entry Spot ID와 target `zlink-mesh-node-immutable-v2` digest를 게시한다. Digest는 Entry Spot ID
  presence·값, Actor·Spot population limit과 activation concurrency limit을 포함하고 mutable active count는
  제외한다. Shared fixture, 실제 admission HASH, digest fence와 capacity focused test를 함께 실행해
  7/7이 통과했다.
- 같은 canonical fixture에 대한 C++·JVM·Node provider parity와 aggregate·destroy까지 포함한 전체 bundle
  gate는 아직 끝나지 않았다. 따라서 assertion을 약화하지 않고 `V11-M6C-CAPACITY-DN`은 대기 상태를
  유지한다.
- 정식 server Redis spec은 v3 typed capacity schema를 소유하지만
  `framework/common/spec/22-location-store-redis.ko.md`의 나머지 본문에는 v1 active·pending key와 record
  설명이 남아 있다. Provider parity가 확정되면 공통 문서 전체를 v3 본문으로 통일해야 하며, 현재의
  digest 단락 수정만으로 문서 gate를 완료 처리하지 않는다.

C++ capacity parity checkpoint(2026-07-24):

- Public maintenance contract와 in-memory provider를 typed capacity bundle로 전환했다. Creation·relocation·
  aggregate·abort·delete는 Actor total, Spot total과 optional Spot stable-type bucket을 함께 변경하며
  in-memory focused 14/14가 통과했다.
- MeshNode descriptor는 population `Capacity`와 별도 `ActivationConcurrency`를 게시한다.
  `set_actor_limit`, `set_spot_limit`, `set_activation_concurrency`가 descriptor publication까지 연결됐고
  immutable digest v2·Redis v3 physical key·admission field·shared descriptor fixture focused 5/5,
  M6B runtime과 contract header compile이 통과했다.
- C++ Redis Lua의 creation·relocation·aggregate transaction은 아직 scalar counter와 participant별
  relocation reservation을 사용한다. Exact 여섯 capacity HASH와 하나의 typed bundle로
  reserve·commit·abort·destroy를 처리하는 실제 Redis gate가 끝나기 전까지
  `V11-M6C-CAPACITY-CPP`는 대기 상태를 유지한다.

ClientServer dual-role JVM checkpoint(2026-07-24):

- Java와 Kotlin이 공유하는 public configuration에 exact `client()`·`server()` role builder를 추가했다.
  같은 ChannelName의 Client와 Server를 각각 한 번 등록할 수 있고, 서로 다른 ChannelName에는 같은 역할을
  여러 번 등록할 수 있다. Java와 Kotlin focused contract가 이 조합을 검증한다.
- target RouteMesh의 `addRouteMesh(meshName).channelName(channelName)`과 ClientServer ChannelName을 startup
  validation에서 교차 검사한다. 등록 순서와 관계없이 socket bind 전에 configuration error로 실패하는
  Java test가 통과했다.
- JVM production public surface에서는 ClientServer의 기존 `enableClient`·`enableServer`,
  `clientConnections`, channel별 timeout·socket option과 explicit packet-name handler overload를
  제거했다. Active Java core test와 Kotlin focused test는 exact role builder와 framework 기본
  packet-name 규칙으로 옮겼으며 compiler warning 없이 통과했다.
- 기존 Java sample·E2E source의 ClientServer legacy callsite 110개는 이번 단계에서 수정하지 않았다.
  해당 source를 재활성화하기 전에 `client().connect(...)` 또는 `server().listen(...)`으로 옮겨야
  compile할 수 있다. 이 migration은 sample·E2E 활성화 단계의 선행 조건이며 production API에는
  compatibility member를 다시 추가하지 않는다.
- `:zlink-framework-core:assemble`과 Java core focused 99건, Kotlin focused 18건이 통과했다.
  Root `compileTestJava compileTestKotlin`도 통과해 sample·E2E를 제외한 JVM module의 production·test
  callsite가 exact interface로 compile됨을 확인했다. `server()`만 등록하고 `listen()`을 호출하지 않은
  구성은 startup validation에서 실패하며, 4-argument explicit packet-name helper는 ClientServer exact
  interface에 존재하지 않는다.

`V11-M6B-JVM` durable Instance authority checkpoint(2026-07-24):

- Authority runtime은 startup full scan 뒤 optional Location watch를 등록하고 다시 full scan한다. 따라서
  subscribe 전후 publication race를 회수하며, watch event는 row 내용을 신뢰하지 않고 authoritative
  snapshot 재scan만 요청한다. Watch가 없거나 종료되어도 bounded polling을 correctness fallback으로 유지한다.
- `PENDING + INSTANCE_SPOT + CREATING` row는 exact object·authority owner·lease·target node lifecycle과
  StoreVersion fence를 command 39 intent에 등록한다. 이 상태는 일반 Spot route로 공개하지 않는다.
  `ACTIVE + READY` 전환에서만 route를 공개하고 update·remove·shutdown에서는 이전 intent와 route를
  exact fence로 제거한다. Legacy explicit `ActorRef`에서 generation을 추정하는 경로는 추가하지 않았다.
- 검증은 authority startup·watch·Pending→Ready→Removed 1/1, command 39 actual peer 19/19,
  Framework Location runtime 6/6이 통과했다. Java core assemble과 Kotlin compile도 통과했다.
  Redis provider test는 32건 중 local unit·fixture 18건이 통과하고 외부 endpoint 의존 14건만 skip됐으며
  실패는 0이다. Sample·E2E·Core·bindings source 변경과 실행은 0이다.

`CA-D55`·`CA-D56` 구현 checkpoint(2026-07-24):

- .NET, JVM과 Node는 public User Spot manager에서 Location reservation을 만든 뒤 command 47·48을 실제
  target runtime으로 보내는 production 경로를 연결했다. Generic User Spot creation request는 Relocation
  Store를 사용하지 않고 Location reservation의 `inline-v1` opaque reference에 complete application envelope를
  보관한다. Target은 CRC32C, encoded size와 SHA-256을 factory 실행 전에 검증한다.
- 네 runtime target은 source·target lifecycle, reservation, StoreVersion, owner generation과 close의
  `Ready + UserSpot + Active`를 검사한다. 같은 operation은 semantic fingerprint가 일치할 때만 terminal을
  replay하고, live capacity가 차면 factory 전에 Busy로 거부한다. 완료 terminal은 deadline 뒤 5분 동안
  유지하며 그 뒤의 expired retry도 handler를 다시 실행하지 않는다.
- .NET focused Stateful 16/16와 Redis authority 9/9, JVM core 470과 actual peer M6B 19/19, Node M6B
  35/35·M6C 32/32, C++ M6B binary가 통과했다. C++·JVM post-fix review는 범위 내 추가 P0/P1 없이
  승인됐다. Node post-review P1은 `V11-M6B-NODE` 행의 미완료 조건으로 관리한다. .NET fixed public API
  snapshot은 generator 출력으로 갱신했다.
- C++은 exact `spot_manager_t` call builder·`spot_ref_t` surface, Location Reserve→command 47·48,
  concurrent Creating join, inline reference 검증과 local raw command loopback을 production path에 연결했다.
  `PublicSpotManagerUsesLocationReservationAndMeshCommands`가 Create/GetOrCreate→Find→Close를 검증하며
  resolver 33/33, contract headers와 M6B runtime이 통과했다.
- C++ 공식 Redis provider는 Pending current·history에 provider-issued reservation ID, immutable content
  reference, SHA-256과 encoded size를 기록하고 Commit 뒤 Active current에서는 네 field를 제거한다. Pending
  Reserve loser는 `AlreadyExists`를 반환하지 않고 같은 attempt의 snapshot을 반환한다. 공통 physical schema
  fixture와 Lua script contract 3/3, 실제 Redis endpoint round-trip을 포함한 provider suite 23/23이 통과했다.
  Cross-language 전용 prefix가 필요한 2건은 환경이 없어 skip됐다.
- 독립 C++ review에서 `InMesh` source transport 불일치, 1 MiB creation request 상한 누락, Pending
  reservation을 Ready `AlreadyExists`로 공개하던 오류와 invalid default `spot_ref_t`를 수정했다. Target
  materializer는 Commit 전 reserved facade를 local registry에 남기지 않는다. Post-fix verdict는
  `APPROVE WITH RESIDUAL GAPS`다.
- C++의 기존 `GeneratedUserSpotIdRetriesActiveCollisionWithinCreateCall`은 `CA-D20`과 상충하므로
  폐기 대상이다. 대체 contract test는 User Spot automatic `Create`의 첫 active collision이 기존 authority를
  변경하지 않고 즉시 `RoutingIdConflict`로 끝나며 추가 UUID 생성·reservation·factory 호출이 0건인지
  검증한다. Caller RID `GetOrCreate`의 `Existing`·`SpotTypeMismatch` 의미는 유지한다.
- C++ command 47·48 terminal mapping은 transport timeout과 canonical typed wire failure를 public
  `DeadlineExceeded`, `SpotGenerationStale`, `SpotMoving`, `SpotTypeMismatch`, capacity·rejected error로
  보존한다. Codec exact-code round-trip, target type-mismatch terminal replay와
  `UserSpotTerminalMappingPreservesExactPublicErrors`가 통과했다.
- C++ source cleanup은 source-created reservation과 joined reservation을 구분하고 exact fence에서만
  abort한다. Not-owned·wrong fence·exact abort·already committed 보호와 실제 materializer failure 뒤 source
  reconcile을 focused test로 검증했다.
- 남은 조건은 Node native 두 process E2E, Redis endpoint가 필요한
  JVM live round-trip과 command 39 durable Instance activation의 언어별 나머지 연결이다.
- 전체 회귀의 별도 baseline으로 .NET documentation 7건·ClientServer timing/liveness 2건, JVM Spring
  monitoring·handler 6건과 .NET packaged contract의 기존 package version·XML hash 불일치를 유지한다.
  Sample·E2E source 변경·실행은 0이고 Core·bindings 변경은 0이다.

### 10.3 M6C — Maintenance, monitoring과 hosting

M6C는 host barrier, all-or-none preflight, `Retire`·`Shutdown`, durable authority·relocation·recovery,
observability와 C++ host·ASP.NET·Spring·NestJS integration을 구현한다. 공통 Framework는 고정 maintenance HTTP
route를 만들지 않는다.

.NET command 40 target 입력에 필요한 User Spot `stableType`과 Actor authority는 새 wire field가 아니라
Location Store current authority의 Active allocation과 Actor authority scan에서 exact read한다. 공통 spec은
command `40` exact inventory, command `30` target capacity offer(empty participants), command `30` source
accept(exact prepare participants), command `41` reservation ACK 순서를 확정했다. accepted frozen owner·lease는
ingress Location read와 admitted descriptor로 검증하며 RID에서 추정하지 않는다. .NET은 이 state machine을 raw
production ingress와 Retire scheduler의 private stage 전 reservation 경로에 연결했다. Offer는 side effect 없이
bounded replay budget만 반환하고 source의 exact accept 뒤에만 runtime permit과 Location capacity fence를
single-flight로 예약한다. Retry mismatch, source·role·deadline·staging race, stale coordinator lease와 raw
command 40→30→30→41을 focused 13/13으로 검증했다. Aggregate prepare가 이 fence를 이어받는 계약과 남은
private stage·publish·abort·held relay 제거 전에는 production 전환 완료로 판정하지 않는다.

.NET aggregate semantic checkpoint(2026-07-26)에서 source ingress를 freeze한 뒤 cutoff까지 hold한 accepted
journal을 포함해 immutable root를 한 번만 생성하도록 순서를 고쳤다. Target은 command 40의 root·participant
allowance·authority fence를 exact 검증하고 User Spot aggregate를 한 번만 prepare한다. Prepared 결과가 source가
제시한 reference·CRC32C와 다르면 prepared fence를 abort하고 staging을 거부한다. Publish ACK 유실 뒤 source가
abort를 요청해도 target은 durable authority를 먼저 확인하며, commit 또는 불명확 결과에서는 local stage를
제거하거나 source admission을 복원하지 않는다. Acceptance operation과 dispose가 경합해 늦게 얻은 aggregate
fence도 abort하고, User Spot staging handoff 뒤 reservation slot을 제거해 1,024개 bound가 누적되지 않게 했다.
Framework build는 warning·error 0, `CanonicalRelocationReservationOwnerTests` 8/8,
`ServiceWireRelocationCodecTests` 5/5와 `RelocationRuntimeTests` 87/87이 통과했다. Standalone Instance Spot의
capacity fence handoff·commit·abort와 private stage·publish·abort·held relay의 command 31·32·34·35 전환은
계속 진행한다. 따라서 `V11-M6C-DN`, `BLK-044`와 .NET reference 행은 진행 상태를 유지한다.

`V11-M6C-DN` canonical wire checkpoint(2026-07-26)에서 command `31`·`32`·`34`·`35` production
전환과 private stage·publish·abort·held relay 제거를 완료했다. Target reservation owner는 participant
sequence와 allowance, duplicate bytes, cumulative payload bound, source seal response와 authority publication을
exact fence로 검증한다. Held ingress와 timer·queue payload는 별도 relay 없이 단일 immutable root에서
복원하며 command `35`가 source cleanup을 닫는다. Standalone Instance Spot은 capacity fence를 authority
commit에 넘긴 뒤 terminal cleanup에서 permit과 owner slot을 해제한다. Framework·UnitTests build는
warning·error 0, focused test 103/103, 전체 Unit 987/987, Contract 65/65가 통과했고 private wire symbol은
0건이다. 관련 diff check도 통과했다. Standalone Actor canonical maintenance owner와 mixed-language process
E2E가 남아 있으므로 이 row와 `BLK-044`는 완료로 올리지 않는다.

후속 ACK-loss·reservation 수명 checkpoint(2026-07-26)에서는 command `32`·`34`를 수신한 exact peer
instance와 lifecycle generation을 예약에 고정했다. Target은 command `34` response를 command `35`가
도착할 때까지 같은 bytes로 재전송하며, source는 authority publication 결과를 exact read로 확인하거나
durable aggregate abort가 증명된 경우에만 admission을 복원한다. Command `35`가 stage보다 먼저 도착하면
live reservation을 제거하지 않고 거부한다. 완료된 reservation은 1,024개·5분 범위에서 command
`40`·`30`·`31`·`34` duplicate의 exact fingerprint만 재생한다. Instance Spot은 immutable root identity를
검증한 뒤 capacity fence를 예약하고, stage 실패·deadline·dispose에서 permit과 slot을 한 번 해제한다.
Framework build warning·error 0, relocation focused 104/104, 전체 Unit 993/993, Contract 65/65와
`git diff --check`가 통과했다. Standalone Actor canonical owner와 독립 post-review가 남아 있으므로 완료
상태는 변경하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6C-CPP` | C++ maintenance·monitoring·hosting | C++ lane, `P-DEEP` | `V11-R5B` | 진행 | preflight·relocation·recovery·terminal observation·bounded host contract 통과 | Framework stateful runtime에 permit-before-seal relocation coordinator를 추가했다. Process gate는 outbound·inbound 기본 64, Capture·Restore 기본 8과 payload in-flight 256 MiB를 queue seal 전에 all-or-none으로 예약하며, oversized unit은 gate가 비어 있을 때만 단독으로 admit한다. Current application turn이 끝난 object만 reversible seal하고 미실행 queue와 logical timer registration을 deterministic envelope로 freeze하며 seal 뒤 application ingress는 hold하고 infrastructure queue는 계속 처리한다. Immutable root를 24시간 retention으로 먼저 저장하고 CRC32C를 검증한 뒤 authority reference·inventory digest를 publish한다. CAS conflict는 orphan root를 정리하고 frozen→held 순서로 source admission을 복원한다. 응답 유실은 authority read로 reconcile하며 publication이 불명확하면 root와 seal을 보존해 recovery가 이어지게 한다. Published root missing·checksum mismatch·inventory mismatch는 source rollback 없이 `data_lost` terminal로 분류하고, valid root는 새 runtime에 queue·timer와 exact generation으로 복원한다. 이번 slice에서 host-wide coordinator를 연결했다. Retire preflight는 create·membership·close를 하나의 structural inventory barrier로 직렬화하되 기존 application queue는 unit seal까지 계속 수락하며, provider가 반환한 unit set이 canonical inventory와 exact match일 때만 `Retiring`을 publish한다. Concurrent Retire waiter는 같은 attempt를 사용하고 preflight 중 Shutdown이 admission seal을 먼저 claim하면 모든 waiter가 `EffectiveIntent=Shutdown` 결과에 합류한다. Blocked Retire는 structural barrier를 해제하고 `Serving`과 normal admission을 복원하며 host terminal result로 저장하지 않는다. Raw public host의 start 전 단일 provider-set seam은 Location authority, aggregate CAS, Relocation payload와 eligible-target preflight capability를 분리 등록하고 start·close lifecycle hook을 host terminal observer에 연결한다. User Spot과 seal 시점의 member Actor는 한 aggregate token으로 함께 freeze하고 immutable aggregate root 저장 뒤 authority prepare·commit 한 번으로 owner와 membership participant를 전환한다. Aggregate recovery는 bounded strict envelope decode 뒤 모든 participant의 root·checksum·inventory digest, source authority generation과 target owner를 먼저 검증하고 User Spot·member Actor의 queue·logical timer·canonical membership을 `recovering` staging runtime에 원자적으로 복원한다. 검증 실패와 allocation failure에는 live runtime의 partial restore를 남기지 않는다. 같은 target generation의 재실행도 root identity, stable type, queue·timer·membership 전체가 exact match일 때만 idempotent하게 합류한다. Application claim과 timer 실행은 lifecycle restore·accepted journal replay·source cleanup·completion ACK seam이 완료될 때까지 차단하며 현재 slice는 `recovery_required`로 fail closed한다. STREAM registry는 accepted inbound completion을 추적하고 Actor relocation barrier 뒤 binding generation을 교체해 stale packet을 거부하며 Shutdown의 host-wide session seal도 같은 barrier를 사용한다. M6C focused contract는 기존 5개에 all-or-none blocker rollback, User Spot 2-participant aggregate·STREAM fence, Retire/Shutdown first-intent race, post-commit ForceStopped teardown과 aggregate crash recovery를 추가했다. Raw port·wire codec·operation registry와 M6A~C focused/internal/resource/protocol 6/6, 전체 `zlink_framework` compile도 통과했다. 독립 post-fix review는 decode bound·strict ordering, exact idempotency, staging admission과 copy-on-write atomic commit을 확인하고 `APPROVE`했다. 높은 CPU 부하 구간의 combined run에서는 기존 M6A·M6B raw receive가 반복적으로 timeout성 오류를 냈다. 변경 대상이 아닌 동일 binary의 isolated repeat는 통과했고 clean 6/6 run도 확보했으므로 별도 반복성 이슈를 유지한다. 남은 gap은 descriptor `Retiring/Draining` publication과 topology teardown, standalone Actor·Instance Spot target reservation·factory·Restore, User Spot aggregate lifecycle restore·accepted journal replay·source cleanup·completion/relay ACK이다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. 관련 blocked issue: `BLK-010`. |
| `V11-M6C-DN` | .NET maintenance·monitoring·ASP.NET | .NET lane, `P-DEEP` | `V11-R5B` | 진행 | CAS·lease·Task race·terminal observation·ASP.NET shutdown contract 통과 | `IZLinkFrameworkRuntime`의 host-wide state·snapshot·bounded observer·`RetireAsync`·`ShutdownAsync`와 ASP.NET hosting stop 연결을 유지한다. Retire preflight blocker는 admission seal 전에 `Serving`을 유지하고, Shutdown과 Retire는 first effective intent의 shared operation에 합류하며 waiter cancellation은 shared operation을 취소하지 않는다. Target .NET 계약의 `IZLinkAuthorityStore`, `IZLinkRelocationStore`, Actor·Spot relocation adapter와 policy, 분리된 `AddRelocationStore`를 추가했다. User·Instance Spot과 Actor participant를 함께 담는 deterministic root는 application state, accepted queue sequence·payload와 logical timer cursor·payload를 보존한다. Root를 24시간 retention으로 먼저 저장하고 CRC32C·immutable read를 검증한 뒤 single authority CAS 또는 aggregate prepare·commit으로 publish한다. CAS conflict와 prepare reject는 orphan을 정리하고, commit outcome exception은 authority reference를 읽어 reconcile하며 published root missing·checksum·inventory mismatch는 rollback하지 않는 data-loss로 분류한다. Managed Spot·Actor outbound frame은 object generation이나 상수 `1`을 authority fence로 쓰지 않고 Location row에서 관찰한 `AuthorityOwnerGeneration`만 사용하며 inbound는 local actual generation과 일치할 때만 dispatch한다. In-memory와 Redis Location row는 owner claim generation을 이 field로 materialize한다. 이번 slice에서 `IZLinkLocationStore`가 `IZLinkAuthorityStore`를 직접 상속하도록 연결하고 in-memory provider에 exact read·CAS·snapshot scan·reservation·aggregate state를 추가했다. 공식 Redis package에는 Location과 별도 options·connection lifecycle을 가진 `ZLinkRedisRelocationStore`를 추가했다. Relocation payload는 SHA-256 reference와 CRC32C를 사용하고 Redis `TIME` 기준 retention으로 저장·renew·read·delete한다. `ZLinkRedisLocationStore`는 같은 namespace에서 authority Preserve/Delete CAS, 1분 snapshot scan, reservation과 bounded aggregate prepare·commit·abort를 server-side script로 실행한다. Spot serial queue는 application closure를 accepted sequence·immutable payload·local executor로 분리했다. Turn boundary seal은 pending application record를 capture하고 이후 record를 hold하며 infrastructure continuation은 계속 실행한다. Abort는 infrastructure 뒤 captured→held 순서로 복원하고 commit은 source resource를 해제한 뒤 held record를 relay 입력으로 반환한다. 실제 Spot route ingress는 source RID·Spot ID·request sequence·metadata·message parts를 bounded accepted-journal record로 저장한다. Timer pump는 native handle 대신 registration, delivery·scheduled cursor, next due와 pending tick을 소유한다. Source freeze·abort resume와 target의 새 pump restore를 deterministic logical timer payload로 연결했고 freeze 뒤 timer handler가 실행되지 않도록 admission을 막는다. 정식 `IZLinkMeshObjectServerBuilder`의 stable type·placement·relocation policy 등록을 현재 MeshNode builder가 직접 구현하고 Snapshot adapter type 검증, DI 등록과 typed Capture·Restore invoker를 같은 registration record에 연결했다. User Spot capture는 같은 serial boundary에서 Spot과 canonical member Actor state를 adapter policy에 따라 수집한다. Serial focused 21/21, timer lifecycle 7/7, relocation runtime 12/12, ASP.NET·Framework build warning·error 0이 통과했다. 전체 unit은 709건 중 runtime 702건이 통과했고 실패 7건은 이동 안내 문서와 비활성 E2E fixture를 기대하는 기존 documentation regression이다. Redis 기존 회귀 8건 중 `IReadOnlySet<string>` codec 원인 6건을 수정했고 34/36이 통과했다. 남은 2건은 target fixture의 새 descriptor·authority HASH와 이전 test codec 비교가 충돌하는 contract migration gap이다. `ZLinkAuthorityMutation.Put`의 `NewOwner`·`NewObject`에는 opaque payload와 별도로 target owner token이 필요하다. 이 token을 정식 계약과 provider에 반영하고 in-memory payload decode 우회를 제거하는 작업이 진행 중이다. 실제 Retire scheduler의 target reservation·factory·Restore-before-commit·accepted reply relay ACK·STREAM fence와 aggregate completion이 아직 기존 network Actor drain을 대체하지 않았으므로 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거로 사용하지 않는다. Core·bindings와 Sample·E2E source 변경·test 실행은 0이다. 검증 중 solution 전체 build를 한 번 잘못 호출해 Sample·E2E project compile graph가 시작됐으나 package downgrade에서 중단됐고, 이후 검증은 Framework·ASP.NET과 UnitTests project로 한정했다. |
| `V11-M6C-JVM` | JVM maintenance·monitoring·Spring | JVM lane, `P-DEEP` | `V11-R5B` | 진행 | Java·Kotlin lifecycle·coroutine·Spring metadata와 shutdown contract 통과 | Host-wide `retire`·`shutdown`, runtime state·termination result·observer, 기본 64 unit·256 MiB scheduler, accepted journal queue, logical timer freeze/restore, immutable Relocation Store와 authority publication coordinator를 유지한다. `ZLinkLocationStore`가 `ZLinkAuthorityStore`를 직접 상속하며 runtime은 같은 provider를 별도 fake나 internal port 없이 authority service로 노출한다. In-memory provider도 read·`PRESERVE`/delete CAS·scan·reservation·aggregate 상태를 구현했고 공식 Redis provider의 중복 authority 선언은 제거했다. Exact object role builder와 stable type·placement·explicit relocation policy 등록 표면을 추가했다. Snapshot policy는 Actor와 Spot adapter의 generic 대상 type을 socket 생성 전 검증하며 runtime adapter registry가 stable type을 실제 adapter instance와 capture/restore 호출에 연결한다. Client·Server object role은 Location Store가 필수이고 Recreate·Snapshot policy는 Relocation Store가 필수다. Shutdown은 기존 Actor network handoff를 실행하지 않는다. Actor를 먼저 정리한 뒤 User Spot과 Entry Spot의 `onClosing`에 `HOST_SHUTDOWN`과 deadline을 전달한다. Retire에 active Actor·User Spot이 있으나 새 owner token을 publish할 수 없는 현재 계약에서는 admission seal 전에 `RELOCATION_FAILED` 또는 `RELOCATION_DISABLED`로 종료해 기존 handoff나 payload parse로 우회하지 않는다. Java core 417/417, Spring starter 33/33, Kotlin compile, Redis provider 18건 중 환경 의존 7건 skip·실패 0이 통과했다. 남은 계약 공백은 `ZLinkAuthorityPut`의 `NEW_OWNER`·`NEW_OBJECT`가 target owner token을 전달하지 못하는 점과 aggregate exact owner lease claim/read·capacity fence다. 최소 수정안은 Put에 transition별로 검증하는 optional target owner를 추가하고 Actor 단위 `NEW_OWNER` CAS에 사용하며, 이미 target owner가 있는 aggregate·reservation은 그대로 유지하는 것이다. 이 계약이 확정되기 전에는 User Spot aggregate capture/restore, target replay-before-commit과 실제 Retire relocation을 연결하지 않는다. Spring construction/start 분리도 남아 있다. 2026-07-26 checkpoint에서 `18-object-routing.ko.md`를 direct·Session binding·reply route 분리 기준으로 적용했다. Process-wide relocation permit pool은 outbound·inbound 기본 64, Capture·Restore 기본 8과 payload 256 MiB를 하나의 accounting domain에서 all-or-none으로 예약한다. 예상 payload는 Capture 뒤 실제 크기로 줄일 수 있고, 256 MiB를 넘는 unit은 pool이 비어 있으며 caller가 oversized를 명시한 경우에만 단독으로 admit한다. Target-side aggregate coordinator는 immutable root의 CRC32C와 read-back bytes를 검증한 뒤 canonical UTF-8 participant inventory의 SHA-256 digest와 ZLAR authority reference를 구성하고 `prepareAggregate`까지만 실행한다. 따라서 target factory·Restore staging이 끝나기 전에는 authority가 공개되지 않으며, staging 성공 뒤 `commitAggregate` 한 번으로 participant 전체를 publish할 수 있다. Prepare conflict는 unlinked root를 삭제하고 explicit abort는 aggregate abort 성공 뒤 root를 삭제한다. Commit 전 root retention을 renew하며 commit 응답이 불명확하면 participant authority의 root·checksum·aggregate fence·inventory digest를 exact read로 확인한다. Focused permit·aggregate·ZLAR interop test 7/7과 core `compileJava`가 통과했다. Scheduler는 아직 이 bridge에 연결하지 않았다. Aggregate root tree/chunk codec, target factory·Restore staging owner와 source queue·timer handoff가 production Retire 경로에 연결되기 전까지 기존 fail-closed preflight를 유지한다. 아직 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거가 아니며 Sample·E2E·Core·bindings 변경·실행은 0이다. 관련 blocked issue: `BLK-036`. |
| `V11-M6C-NODE` | Node maintenance·monitoring·NestJS | Node lane, `P-DEEP` | `V11-R5B` | 진행 | Promise·event-loop recovery·terminal observation·NestJS cleanup contract 통과 | first-intent-wins Retire·Shutdown barrier, mutation 전 preflight, ready-first bounded relocation scheduler(기본 outbound 64·in-flight 256 MiB), deadline 뒤 force-stop, terminal observer와 published root data-loss recovery 분류를 Framework-owned runtime에 구현했다. Relocation payload는 application state·accepted journal·미실행 queue·logical timer를 deterministic envelope로 만들고 immutable Store에 먼저 기록한 뒤 checksum·inventory digest를 검증하여 Location authority의 단일 preserve CAS로 공개한다. CAS conflict가 발생하면 proven orphan만 삭제한다. CAS 응답이 유실되면 authority exact read로 publication 성공을 reconcile하고, expected version이 유지된 것이 확인될 때만 orphan을 삭제한다. 결과가 불명확하면 retention이 정리하도록 root를 보존한다. Authority reference 해제 뒤 payload 삭제, published payload missing·checksum·inventory mismatch의 non-rollback `RelocationDataLost`를 구현했다. Owner queue는 active claim이 끝난 turn boundary에서 seal하며 기존 미실행 record를 capture하고 이후 ingress를 별도 hold한다. Abort는 captured→held 순서로 admission을 복원하고 commit은 held record만 relay 대상으로 반환하며 infrastructure queue는 계속 진행한다. Spot timer는 native timeout handle을 저장하지 않고 registration option, schedule cursor와 delivery cursor를 freeze하며 target의 동일 registration에 logical schedule을 복원한다. 기존 NestJS host는 application shutdown에서 30초 bounded RouteMesh drain 뒤 idempotent stop을 수행한다. Public `ZLinkLocationStore`와 `ZLinkRelocationStore` 및 Framework·NestJS의 `addLocationStore`·`addRelocationStore` 등록 표면을 각각 분리했다. Redis 전용 또는 두 Store를 묶는 등록 API는 추가하지 않았다. `ZLinkChannelClient`는 global channel name과 classic channel transport를 사용하고 `ZLinkRouteClient`는 globally unique Mesh channel의 MeshName을 내부에서 결정하도록 exact contract에 맞췄다. Raw-only bindings에서 제거된 `createMeshNode`를 요구하던 stale parity test를 제거했고, M6 runtime protocol graph에서 Bingo sample generator를 실행하던 test를 제외했다. Connector protocol test는 각 instance를 명시적으로 close한다. 2026-07-26 현재 candidate 24 files의 정식 `M6-RUNTIME` 7 commands가 모두 통과했으며 public declaration 38/38, M6A 9/9, M6B 39/39, M6C 54/54, resource 14/14, protocol 14/14와 Framework compile이 통과했다. 이 gate에서 sample·E2E project/task 실행과 skip은 모두 0이다. local manual RouteMesh·ClientServer client·fanout subscriber·storeless publisher가 하나라도 있으면 state와 admission을 바꾸기 전에 `Blocked/ManualTopologyUnsupported`를 반환하고, automatic topology에서는 complete live descriptor set의 exact RID·lifecycle generation peer가 Core에서 `Admitted`·`Ready`가 될 때까지 기다린다. 증거: `.artifacts/v11/evidence/V11-M6C-NODE/result-current.json`. 다만 durable coordinator가 아직 production host의 Spot·Actor restore owner, session route replacement와 aggregate drain에서 생성되지 않으므로 component contract 통과만으로 완료 판정하지 않는다. 해당 production wiring과 host-level active workload regression을 계속한다. Sample·E2E 실행은 0이다. 관련 blocked issue: `BLK-042`. |
| `V11-M6-DN-DESIGN-REVIEW` | .NET 전체 회귀 뒤 POSD·DDD 정식 review | .NET 독립 review lane, `P-DEEP` | `V11-M6A-DN`, `V11-M6B-DN`, `V11-M6C-DN`, `V11-M6-ONE-WAY-DN`, `V11-M6-DEFERRED-JOIN-DN`, `V11-M6-OBJECT-CONTEXT-DN`, `V11-M6-MESSAGE-CONTEXT-DN`, `V11-M6-SESSION-BINDING-DN`, `V11-M6B-EXEC-DN`, `V11-M6C-BARRIER-DN`, `V11-M6C-CAPACITY-DN`, `V11-M6B-ENTRY-IDENTITY-DN`, `V11-M6A-WEIGHT-DN`, `V11-M6-STORE-POSD-DN-REVIEW` | 수정 진행 | 최초 전체 `.NET M6-RUNTIME` 통과 revision에서 red flag·DDD boundary leak 목록, 원칙 연결과 각 finding의 두 대안 비교 완료 | Final delta review의 High·Medium 네 건은 수정했다. Abort는 `Staged→Aborting→Aborted`와 cleanup 성공 뒤 ACK·permit once를 보장하고, `GenerationExhausted`는 non-retriable terminal로 분류하며 conflict·stale만 8회 retry한다. Completed stage는 1,024개·5분 bounded tombstone으로 교체했고 Actor drain은 target-local retriable만 다음 후보로 진행한다. `BLK-044`의 .NET command 33·46 bounded slice도 managed raw infrastructure dispatch로 전환했다. Pending relay는 ACK target RID·operation·relocation·coordinator fence로 분리하고 expected request-source owner·lease·node·generation과 admitted peer lifecycle을 exact 검증한다. ACK-loss 뒤 duplicate는 `AlreadyTerminal`로 닫으며 non-Ok terminal payload는 송수신에서 거부한다. Private `.reply.v1` Route packet symbol은 제거했다. command 30·31·32·34·35·40·41 exact codec과 공통 golden 7개, relocation phase 0~9 closed enum, full frozen-record kind 1~14의 byte-preserving validation을 추가했다. Application frozen record와 unknown kind fail-closed도 검증했다. 추가 조사에서 User Spot `stableType`은 exact Spot authority snapshot의 allocation에 있고 Actor identity·`stableType`·authority payload는 `zla1:a:` authority scan과 steady payload에 있으므로 command 40 field 누락은 아니다. canonical root의 participant count·ID와 exact authority inventory를 대조해 staging 입력을 복원할 수 있다. 그러나 정식 sequence는 command `40→30 offer→30 accept→41`인데 현재 private stage 호출은 이 state machine을 구현하지 않으며, schema의 target offer participant vector `empty` 규칙과 공통 command 30 golden의 non-empty vector도 일치하지 않는다. 이 계약을 먼저 정리하지 않고 command 40에서 command 41로 직결하지 않는다. stage·publish·abort·held relay raw dispatch와 독립 review 재실행이 남아 있으므로 상태는 수정 진행으로 유지한다. |
| `V11-M6-DN-REFERENCE` | review finding refactoring·재회귀와 .NET reference revision 고정 | .NET integration lane, `P-DEEP` | `V11-M6-DN-DESIGN-REVIEW` | 수정 진행 | finding 0, focused test와 전체 `.NET M6-RUNTIME` 재통과, compatibility·placeholder·unused 0, immutable reference manifest와 `ROW-GATE` 통과 | High·Medium finding과 command 33·46 production raw dispatch bounded slice 수정 뒤 Unit·Contract, focused reply relay 6/6, codec golden 2/2가 통과했다. `.reply.v1` private Route packet은 제거했고 exact source collision·ACK-loss·duplicate terminal·non-Ok payload negative를 검증했다. 후속 codec checkpoint에서 command 30·31·32·34·35·40·41 공통 golden 7/7과 full frozen-record application relay, phase 0~9, unknown kind negative가 통과했다. 이 checkpoint는 codec과 durable record validation만 완료했으며 production packet 전환 완료 증거가 아니다. command 40 target staging에 필요한 `stableType`과 Actor authority는 새 wire field 없이 Location Store의 exact Spot row와 Actor prefix scan에서 얻을 수 있다. 남은 선행 조건은 command `40→30→30→41` reservation state machine과 command 30 offer participant 규칙의 schema·golden 불일치를 해소하는 일이다. private stage·publish·abort·held relay 제거, post-wire 전체 gate, 새 immutable manifest와 독립 finding 0 review 전에는 reference revision을 고정하지 않는다. 2026-07-28 solution contract migration에서 sample·E2E·Stream Connector 호출부와 regression assertion을 현행 public contract로 전환했다. Fresh solution `Rebuild`는 warning 0·error 0, Stream Connector regression은 141/141이 통과했다. Sample regression은 117/119이며 남은 2건은 SpotActorTransfer lane 소유 source이므로 이 행의 완료 조건으로 계산하지 않는다. |
2026-07-28 .NET canonical reservation 재검증에서 command `40→30 offer→30 accept→41`의 production
경로를 다시 확인했다. Target owner는 command 40 fingerprint와 authenticated source를 bounded slot에
저장하고, target offer에서는 participant vector를 비운 채 실제 replay budget만 반환한다. Exact source
accept 뒤 User Spot은 seal 시점의 단일 root와 participant 전체 typed bundle로 aggregate prepare를 한 번
실행한다. Standalone Actor와 Instance Spot만 단일 capacity fence를 사용한다. 동일 retry는 같은 Task에
합류하며 participant가 달라진 retry는 exact mismatch로 거부한다. Focused User Spot 1/1과 canonical
reservation owner 28/28이 통과했다. 따라서 이전 checkpoint의 “User Spot도 standalone capacity fence를
사용하고 target staging 뒤 두 번째 root에서 source-side aggregate prepare를 수행한다”는 설명은 현재
구현에 해당하지 않는다. Mixed-language process E2E와 독립 final review 전에는 `V11-M6C-DN`,
`V11-M6-DN-REFERENCE`, `BLK-044`를 완료로 올리지 않는다.

같은 candidate의 public error boundary는 operation별 40개 오류를 13개
`ZLinkFrameworkErrorKind`와 `ZLinkRetryAdvice`로 교체했다. HTTP timeout은
`DeadlineExceeded`, 연결 실패는 `Unavailable`, 본문 크기 제한은
`CapacityExceeded`, decode·redirect 오류는 `ProtocolError`로 분류한다. 외부
sample·E2E가 Framework exception을 직접 만들거나 이전 오류 이름을 참조하는 경우는
0건이다. HTTP Unit 64/64, Contract 69/69, Sample regression 121/121, Stream
Connector timeout focused 1/1과 packaged public contract 검증이 통과했다. Public API
snapshot SHA-256은
`22772c360281405df194e980bd8b356b2d3f1edbeb2fbcfbf031e294c4e9cc5c`다. 이 증거는
public error migration만 완료한다. Relocation process E2E, diagnostics migration,
전체 회귀와 독립 review가 남아 있으므로 reference 행 상태는 변경하지 않는다.

| `V11-R5C` | Maintenance runtime slice 독립 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE`, `V11-M6-STORE-POSD-CONVERGENCE`, `V11-CA-STORE-POSD-DRAFT-RETIRE` | 대기 | lifecycle·authority·handover·recovery·observability·hosting과 실행 격리의 I1·I2·I3 review clean, §2.3 조합 수렴의 I4 clean | C++ candidate의 host first-intent barrier·aggregate CAS·STREAM fence 증거와 JVM candidate의 accepted journal, immutable payload publication, Location authority 상속, exact adapter registration/runtime mapping과 shutdown lifecycle 연결을 확인했다. JVM은 기존 network Actor handoff를 제거했지만 `ZLinkAuthorityPut` target owner와 exact owner lease·capacity 계약이 확정되지 않아 active workload Retire를 seal 전에 block한다. 따라서 aggregate capture/restore와 target replay를 연결하기 전에는 review candidate로 승격하지 않는다. 네 언어 M6C candidate와 generic Store 경계의 다섯 언어 동형 수렴·임시 문서 흡수가 준비되기 전에는 이 row를 시작하거나 clean으로 판정하지 않는다. |
| `V11-M6-SCAFFOLD-ZERO` | Production placeholder 제거와 runtime 완료 gate | inventory·contract lane, `P-SCAN` | `V11-R5C`, `V11-CA-ONE-WAY-RUNTIME-JOIN`, `V11-M6-DN-REFERENCE` | 대기 | production scaffold branch·`RuntimeNotReady` placeholder·fake data 0, 모든 runtime regression 통과, sample·E2E source 삭제·임시 우회 0 | — |

2026-07-26 JVM production composition 재감사에서 `V11-M6C-JVM`의 이전 `BLK-036` 참조는
ClientServer ready 후보 대기 문제를 가리키므로 현재 relocation gap의 증거가 될 수 없다고 판정했다.
`ZLinkUserSpotRetireRuntime`은 host에서 생성되지만 target endpoint의 accepted journal replayer와 steady
normalizer를 no-op callback으로 구성하며, `supportsActiveInventory()`는 User Spot member가 아닌 standalone
Actor를 지원하지 않는다. 이 세 항목을 `BLK-043`으로 분리했다. 실제 hidden Spot·Actor dispatch와 reply relay,
Completed authority 뒤 normalization, standalone Actor relocation과 two-owner process test가 연결되기 전에는
JVM M6C 또는 `V11-R5C` 완료 증거로 사용하지 않는다.

JVM aggregate relocation checkpoint(2026-07-26):

- `ZLinkRelocationTreeStore`가 logical root를 최대 64 MiB의 canonical `ZLTC` chunk와 `ZLTM`
  manifest로 저장한다. 각 component를 Store에서 다시 읽어 exact bytes와 CRC32C를 확인하고,
  manifest는 logical length·전체 CRC32C·32-byte inventory digest·strict UTF-8 reference와
  chunk order를 검증한다. Root와 chunk는 24시간 retention으로 저장하고 commit 전에 모든
  component가 12시간을 초과해 유지되도록 renew한다.
- Aggregate coordinator는 tree 전체 read-back과 inventory digest가 일치한 뒤에만 Location
  aggregate를 prepare한다. Content-addressed chunk는 여러 manifest가 공유할 수 있으므로 Prepare
  conflict와 explicit abort는 해당 manifest만 삭제하며, 연결되지 않은 chunk는 provider retention이
  정리한다. Corrupt chunk는 target staging 전에 `DataLostException`으로 차단한다.
- Composite User Spot barrier는 seal 시점의 Spot·timer lane과 member Actor lane별 accepted journal을
  immutable snapshot으로 노출한다. Source abort와 commit 순서는 기존 barrier가 계속 소유한다.
- Target Actor activation을 prepare·publish·complete·discard로 나누어 factory와 `byte[]`
  adapter Restore를 live registry 밖에서 실행한다. User Spot aggregate staging owner는 Spot·member
  Actor·logical timer·accepted journal을 함께 준비하고, 전체 staging이 성공한 뒤에만 publish한다.
  중간 실패는 준비한 Actor와 Spot을 역순으로 discard하며 live registry에 노출하지 않는다.
  Timer는 staging 동안 frozen 상태를 유지하고 accepted journal replay 뒤에 시작한다.
- Tree·aggregate·permit·barrier focused test 11건과 Actor·User Spot aggregate staging·timer focused
  test가 통과했다. Java core·Kotlin·Spring starter·Redis provider 전체 unit regression과
  Java target·Location Store exact contract test도 통과했다. 이 checkpoint에서 남았던 source
  cleanup·`Completed` authority CAS, concrete journal dispatch와 `18-object-routing.ko.md`의
  command 44·45 binding route ACK·steady normalization은 다음 bullet의 후속 slice에 반영했다.
- 후속 slice에서 accepted journal replayer가 Framework-owned Spot·Actor record를 decode하고
  보존한 request sequence·header를 reply relay에 전달하도록 연결했다. User Spot Retire
  scheduler는 aggregate commit 뒤 target publish·journal replay → durable source cleanup → zero-capacity
  all-`Preserve` `Completed` aggregate CAS → bound Actor별 command 44·45 ACK → steady normalization
  → target admission을 순서대로 실행한다. Commit 전 실패는 durable aggregate abort → target
  staging discard → source admission resume의 역순 cleanup으로 닫는다.
- Completion CAS는 같은 aggregate ID의 다음 generation을 사용하고 owner·object generation·
  authority owner generation·membership·capacity를 변경하지 않는다. In-memory provider는 다음
  committed generation을 수용한다. Generation별 aggregate key를 사용하는 Redis script는
  all-`Preserve`의 empty membership·zero capacity를 검증하도록 맞췄다.
  `ZLinkSessionActorsRuntime`의 stored binding route는 exact Actor generation·source node·owner generation을
  검증한 뒤 target route를 atomic하게 교체하며 method completion이 command 45 ACK 경계다.
  Completed CAS와 모든 ACK 전 admission이 열리지 않는 race, stale source command 거부,
  precommit reverse cleanup, concrete journal decode·reply relay가 focused test를 통과했다. Java
  core·Kotlin·Spring starter·Redis provider 전체 unit regression과 target·Location Store contract도
  재통과했다.
- 후속 remote transport slice에서 raw RouteMesh node request/reply가
  `zlink.internal.spot.relocation.control.v1` packet을 application mailbox에 넣지 않고 Framework
  infrastructure handler로 전달하도록 연결했다. Source RID는 command payload가 아니라 admitted
  transport peer에서 가져와 Stage fence와 대조하며, 한 operation 안에서 stale route를 다시 조회하거나
  자동 재전송하지 않는다. 이 경계는 `18-object-routing.ko.md`의 direct route terminal-once 규칙을
  따른다. Canonical `ZLRC` command는 Stage·Publish·Abort와 aggregate·source·target node·owner·root
  fence를 bounded strict UTF-8로 보존한다. Exact duplicate는 같은 target operation에 합류하고 다른
  Stage bytes는 protocol error이며, abort tombstone은 duplicate ACK를 허용하되 Stage 재개를 거부한다.
  Canonical `ZLUE` immutable root는 User Spot·member Actor application state, object generation,
  Snapshot 여부, logical timer envelope와 lane별 accepted sequence·payload를 보존한다. Target endpoint는
  Relocation Store tree를 read-back 검증해 factory·Restore를 live registry 밖에서 stage하고, Location
  authority의 aggregate ID·generation, reference·checksum·inventory digest와 target owner token이 모두
  일치한 뒤에만 publish·journal replay·timer 시작을 실행한다. Raw transport application-mailbox 우회,
  spoofed source 거부, exact duplicate·abort terminal, envelope round-trip·unknown type·trailing bytes,
  target pre-publication 비가시성과 authority-gated publication focused test가 통과했다. 이어서 Java
  core·Kotlin·Spring starter·Redis provider 전체 unit과 target·Location Store contract 26 task가
  33초에 재통과했다.
- Source production builder가 local User Spot과 member Actor의 실제 runtime inventory를 읽고 Location
  authority의 object generation·owner generation·Store version·owner lease·stable type과 대조한다.
  Eligible target descriptor와 capacity를 먼저 고른 뒤 process-wide outbound·Capture·payload permit을
  seal 경계에서 all-or-none으로 확보하며, permit을 얻지 못하면 timer와 모든 queue lane을 즉시
  복원한다. Seal 뒤 authority를 다시 읽어 동일한 snapshot인지 확인하고 실제 Spot·Actor adapter의
  `Capture`를 실행한 다음 `ZLUE` root를 Relocation Store에 저장·read-back하고 aggregate prepare까지
  진행한다. Prepare 결과를 확정할 수 없으면 source seal과 permit을 유지해 fail-closed하며, 안전한
  precommit 실패만 aggregate abort 뒤 source admission을 복원한다. Bound Session이 있는 Actor는
  command 44·45 production 연결 전에는 seal 전에 거부한다.
- 실제 Framework runtime에서 User Spot을 생성해 Location authority와 live Spot instance를 준비한
  focused test가 같은 instance의 Snapshot adapter Capture, immutable root read-back, source 비가시성
  유지, durable abort 뒤 permit·admission 복원과 eligible target 부재 시 seal·Capture 이전 실패를
  검증했다. Source·target·transport·scheduler·tree·permit focused suite와 `git diff --check`가 통과했다.
  이 구현은 `18-object-routing.ko.md`의 Session stored route, preserved reply route와 failed operation
  비재제출 규칙을 적용한다.
- Host Retire preflight는 계속 fail-closed다. Source builder와 target endpoint를 Host scheduler에
  연결하고, accepted journal의 실제 dispatch·preserved reply relay와 remote command 44·45 Session
  binding ACK를 production 경로에서 완료하는 작업이 남았다. 따라서 이번 source prepare 증거만으로
  `M6-RUNTIME`·`V11-R5C`를 시작하거나 active workload Retire를 허용하지 않는다.
- 전체 documentation contract의 기존 실패는 단일 fixture가 아니라 공통 E2E에 추가된 65개 ID가
  Java active E2E source·feature map에 아직 없는 gap이다. 범위는 `MON-A6`, `OBS-C6~C8`,
  `PS-F1~F5`, `RC-B6`, `RL-E1~E5`, `RL-F1~F14`, `RM-A7`, `RM-C10`, `SF-*`, `SM-*`,
  `ST-G1~G2`, `ST-H1~H5`다. Runtime 단계에서 E2E source를 수정하지 않는 격리 규칙에 따라
  assertion을 약화하거나 ID만 추가하지 않고 `V11-M6A~C-E2E`에서 해소한다.
- Authority·capacity 재감사에서 `ZLinkAuthorityPut`의 `NEW_OWNER`가 exact `targetOwner`와 standalone
  relocation capacity fence를 함께 요구하고, `PRESERVE`는 둘 다 금지하는 정식 계약과 production provider
  구현이 일치함을 확인했다. In-memory와 Redis provider는 CAS 전에 target owner lease, descriptor lifecycle,
  source allocation과 reserved capacity를 exact 검증한다. Aggregate는 prepare에서 canonical participant와
  capacity bundle을 하나의 pending reservation으로 확보하고 commit에서 같은 target owner lease·descriptor와
  participant Store version을 다시 검증하므로 authority mutation과 capacity 전환이 분리되지 않는다. Target
  lease가 prepare 뒤 만료된 경우 commit이 `STALE`로 끝나고 source authority와 active capacity는 그대로이며,
  aggregate abort만 pending capacity를 해제하는 회귀를 추가했다. Authority·descriptor·aggregate focused
  Java 14/14와 같은 exact Store projection을 사용하는 Kotlin location·framework contract 14/14가 통과했다.
  Public·exact interface 변경과 fake·side port 추가는 0이다.
- Tree 후속 회귀는 64 MiB 경계에서 logical root를 두 chunk로 나누고, manifest·chunk별 CRC32C와
  inventory digest를 read-back으로 검증하며 commit 전 모든 component retention을 renew하는 동작을
  확인했다. 같은 bytes의 content-addressed chunk를 두 manifest가 공유할 때 한 manifest를 삭제해도
  다른 root가 계속 복원됨을 검증했다. Tree·authority·descriptor·aggregate focused Java 22/22와
  Kotlin location·framework contract 14/14가 통과했다. 다음 production gap은 verified root를 target
  User Spot aggregate와 standalone Actor factory·Restore hidden staging owner에 연결하고, exact published
  authority를 확인한 뒤에만 journal replay·timer 시작·admission을 여는 host scheduler bridge다.
- Target hidden staging 후속 slice에서 authority를 건드리지 않는 initial `stageRoot`와 final aggregate
  prepare를 분리했다. Source는 queue·Actor lane의 ingress high-water를 freeze한 뒤 captured journal과 held
  suffix를 합친 final root만 aggregate authority에 prepare한다. Stage control은 canonical participant authority
  key, ObjectGeneration과 source AuthorityOwnerGeneration을 전달한다. Target은 모든 participant가 exact
  ObjectGeneration, `source AuthorityOwnerGeneration + 1`, target owner lease, aggregate generation,
  inventory digest와 동일한 final reference·checksum을 가리키는지 확인한 뒤 final root를 읽는다. Final root는
  initial application state·Actor inventory·timer와 journal prefix를 보존해야 하며, captured·held suffix replay가
  모두 끝나기 전에는 prepared Actor·Spot을 local registry에 publish하거나 timer admission을 열지 않는다.
  Standalone Actor도 기존 `prepareRelocatedActor`가 factory·Restore를 registry 밖에서 수행하고 explicit publish
  전에는 조회되지 않는 경계를 재검증했다. Queue·composite barrier·tree·source·target·control·standalone
  staging focused Java 42/42가 통과했다.
- 이 slice는 새 maintenance P1 완료 증거가 아니다. JVM `ZLUE` projection을 successor durability format으로
  확장하지 않는다. Shared `service-wire-v1.schema.json`의 canonical `relocation-envelope-v1`과 golden fixture를
  사용해 participant `acceptedBoundary`·`replayCursor`, terminal completion과 delivery state를 보존해야 한다.
  각 replay terminal과 reply relay ACK 또는 exact source lease expiry는 새 immutable successor root를 먼저
  저장한 뒤 authority의 reference·checksum·`terminalCompletionCount`·`pendingRelayCount`를 expected-version
  CAS로 함께 바꿔야 한다. Target recovery pointer는 이 terminal proof 전까지 유지하고, expiry cleanup은 exact
  aggregate abort와 선형화하며, completed tombstone은 active slot을 즉시 반환해야 한다. 이 canonical codec·
  durable recovery owner와 Host Retire scheduler 연결 전에는 active workload Retire와 `V11-R5C`를 시작하지 않는다.
  Private `ZLinkRelocationAuthorityPayloadCodec`의 `ZLAR` payload에 이 state를 별도로 확장하지 않고 shared
  `authority-payload-v1`의 relocation state와 exact root pointer·progress·count를 사용한다. 현재 안전한
  intermediate slice의 전체 Java core 571/571과 Kotlin 49/49가 통과했다.
- JVM remote ingress 후속 slice는 통합된 `18-object-routing.ko.md`를 기준으로 admitted peer의
  SourceNodeRid·SourceNodeGeneration을 Location Store MeshNode descriptor와 exact 비교하고, descriptor의
  OwnerId·LeaseGeneration을 live owner lease와 함께 한 번 확인한다. Runtime은 이 resolver를 raw MeshNode에
  주입하며 missing descriptor, generation mismatch, expired lease와 Store failure는 application queue에 넣기
  전에 typed conflict terminal로 끝낸다. 같은 operation의 replay는 descriptor를 다시 조회하거나 source owner를
  재추론하지 않고 admission 시 고정한 fence만 사용한다. Source fence를 거부한 request가 handler를 실행하지
  않는 회귀를 추가했다.
- Remote Spot·Actor accepted record는 shared `service-wire-v1.schema.json`의 canonical `frozen-record`로
  기록한다. Source owner fence, original operation ID, request reply route, target generation fence,
  metadata와 canonical application payload envelope를 queue admission 시 함께 고정한다. Relocation replay는
  같은 record를 decode해 packet과 preserved reply route를 복원한다. 따라서 remote production ingress는 private
  `ZJR2`·`ZAJ1` writer를 사용하지 않는다. Local-only dispatch의 private compatibility record와 기존 `ZLUE`·
  `ZLAR` root/authority writer 제거, canonical successor root·authority CAS와 terminal delivery state 연결은
  계속 남아 있으므로 `V11-M6C-JVM`과 `V11-R5C` 상태는 `진행`·`대기`를 유지한다. Focused remote wire·journal·
  replay 23/23, Java core 전체 577/577과 Kotlin 49/49, `git diff --check`가 통과했다.
- JVM canonical wire 정렬 checkpoint(2026-07-26)에서 `spot-route-fence`의 schema 필드인
  `expectedOwnerLeaseGeneration`을 JVM·Node.js·.NET reader/writer와 shared logical·chunk·manifest golden에
  같은 순서로 반영했다. Logical root는 596 bytes, CRC32C는 `3981376350`이며 protocol verifier가 40 commands,
  167 types와 모든 durable·logical fixture를 검증했다. Canonical completion은 `terminalReceived`만으로 recovery
  data를 해제하지 않고 reply relay ACK에 해당하는 `alreadyTerminal` 또는 exact `sourceLeaseExpired`일 때만
  해제 가능하다는 회귀를 추가했다. .NET relocation focused 74/74, Node M6C 54/54, Java core 전체 578/578과
  Kotlin 49/49가 통과했다. Raw STREAM binding 경로는 `sourceBindingGeneration`과
  `sourceSessionSequence`를 accepted journal 생성까지 전달해 canonical bound-session record를 만들지만,
  일반 local Session dispatch와 legacy `forwardActorBoundSession` 경로는 두 fence를 제공하지 않아 private
  `ZAJ1` fallback을 아직 제거할 수 없다. 이 호출 경계를 통일하고, `ZLUE` root와 `ZLAR` authority payload를
  shared relocation envelope와 `authority-payload-v1` relocation state로 교체하기 전까지 `V11-M6C-JVM`은
  `진행`, `V11-R5C`는 `대기`를 유지한다.
- JVM canonical relocation 후속 checkpoint(2026-07-26)에서 통합된
  `18-object-routing.ko.md`의 exact object generation·owner fence 계약을 기준으로 local Session과 raw STREAM
  Actor dispatch가 `sourceBindingGeneration`과 monotonic `sourceSessionSequence`를 canonical frozen record까지
  보존하도록 호출 경계를 통일했다. Production accepted journal에서 private `ZJR2`·`ZAJ1` writer와 decoder를
  제거했다. User Spot aggregate는 Actor stable type·restore policy를 포함한 canonical participant inventory와
  application state·journal·timer를 shared `relocation-envelope-v1`에 기록한다. Location authority는 기존
  `authority-payload-v1` owner·object payload를 유지하고 relocation state slot만 교체하며, private `ZLUE`·
  `ZLAR` codec과 magic은 production·test source에서 제거했다. Focused canonical relocation 13/13, 격리된
  Java core 전체 578/578, Kotlin 49/49와 protocol verifier 40 commands·167 types가 통과했다. Shared Gradle
  output은 다른 lane의 동시 build와 경합하므로 검증 동안 `/tmp` build output만 사용했고 저장소에는 검증용
  init script를 남기지 않았다. `18-object-routing.ko.md`가 추가로 요구하는 stale-route Message Follow mapping과
  Actor relocation command 44·45 ACK 뒤 steady normalization gate의 전체 연결은 별도 JVM runtime gap으로
  계속 추적하므로 `V11-M6C-JVM`은 `진행`, `V11-R5C`는 `대기`를 유지한다.
- JVM object routing command checkpoint(2026-07-26)에서 command 44
  `sessionRelocationRoute`와 command 45 `sessionRelocationRouted`의 exact body codec을 service wire schema와
  같은 필드 순서로 추가했다. Relocation ID, coordinator owner·lease·node·Store version fence, Actor generation,
  Session owner·lease·binding fence, source→target authority owner generation, target node generation과 replay
  high-water를 모두 round-trip하며 unknown action, non-monotonic owner generation과 trailing bytes를 protocol
  error로 거부한다. Session owner runtime은 command 44의 exact Session RID·binding generation·Actor generation·
  source route·high-water를 확인하고 해당 Actor route만 atomic하게 교체한 뒤 command 45 ACK를 만든다. 같은
  command를 stale source route로 반복하면 ACK하지 않는다. Direct Actor request는 route를 operation 시작 때 한
  번만 resolve하고 stale result에는 cache invalidation만 수행하며 같은 operation을 새 owner에게 다시 제출하지
  않는 기존 회귀도 재확인했다. Focused command 44·45·Session binding test와 격리된 Java core 전체 582/582,
  protocol verifier 40 commands·167 types가 통과했다.
- JVM object routing production checkpoint(2026-07-26)에서 Framework-owned raw MeshNode ROUTER가 exact
  command 44를 infrastructure request로 직접 dispatch하고 exact command 45를 직접 reply하도록 연결했다.
  이 record는 application mailbox에 들어가지 않는다. Framework host는 각 MeshNode ingress를 Stream runtime의
  exact Session RID lookup과 Session owner atomic route replacement에 연결한다. Target-side route switch barrier는
  command 45의 relocation·coordinator·Actor·Session·binding·authority generation·high-water 전체가 command 44와
  일치할 때만 steady normalization을 실행하고, normalization이 완료된 뒤에만 admission gate를 연다. 실패하면
  gate를 닫은 상태로 유지하며 같은 operation의 target을 다시 resolve하거나 자동 재제출하지 않는다. 실제 두
  raw MeshNode 사이 command 44→45 왕복과 application dispatch 0을 검증했다. Core 기반 이전 owner relay도
  relocation commit 때 고정한 target transport address를 Message Follow mapping에 보관하므로 전달 중 Location
  Store를 읽지 않는다. Mapping은 같은 Actor ID·ObjectGeneration만 허용하고 동시 1,024 message·16 MiB를
  넘으면 새 relay를 stale/bounded failure로 종료하며 terminal 뒤 permit을 반환한다. Focused peer·barrier·mapping
  test와 Java core 전체 587/587, Kotlin 49/49, protocol verifier 40 commands·167 types가 통과했다. 전체 core
  첫 실행에서 기존 2초 Node request timing test 1건이 일시 timeout됐으나 동일 격리 output의 clean rerun은
  587/587이었다. User Spot aggregate의 bound-session inventory가 아직 stage envelope에 없고 source preflight가
  bound Actor를 seal 전에 거부하므로, 실제 Retire가 participant별 command 44를 생성해 이 barrier를 호출하는
  end-to-end 연결은 계속 남는다. 따라서 이 routing infrastructure slice는 완료됐지만 `V11-M6C-JVM`은 `진행`,
  `V11-R5C`는 `대기`를 유지한다.
- JVM bound-session aggregate inventory checkpoint(2026-07-26)에서 새 통합 계약
  `18-object-routing.ko.md` §3.3을 기준으로 User Spot source preflight의 bound Session 일괄 거부를 제거했다.
  Seal 뒤 Actor runtime이 보관한 Session owner RID·Session RID, binding generation과 마지막 accepted Session
  sequence를 읽고, Actor `ObjectGeneration`·source authority owner generation·Store version 및 Session owner의
  node lifecycle·owner lease fence와 함께 relocation stage control envelope에 canonical Actor ID 순서로 기록한다.
  Target은 이 inventory가 Actor participant fence와 정확히 일치하는지 decode 시 검증하며, private control
  round-trip test가 모든 값을 보존하는지 확인한다. Java core 전체 587/587, Kotlin 49/49, service wire schema
  40 commands·167 types·4 flags·37 bounds와 generated drift 검사가 통과했다. 전체 Java 첫 실행에서 기존 2초
  Node request timing test 1건이 일시 실패했으나 해당 test 단독 재실행과 전체 clean 재실행은 통과했다.
  다만 통합 계약의 순서는 source cleanup과 `Completed` authority CAS 뒤 command 44, command 45 ACK 뒤 steady
  normalization, 마지막 target admission open이다. 현재 remote Retire control은 stage·publish·abort만 제공하므로
  activation publish 단계에서 command 44를 보내면 순서를 위반한다. 따라서 잘못된 조기 전송은 추가하지 않았고,
  target completion/finalize control phase와 scheduler 연결은 남은 JVM M6C gap으로 유지한다. `V11-M6C-JVM`은
  `진행`, `V11-R5C`는 `대기`다.
- JVM Retire finalize·topology gate checkpoint(2026-07-26)에서 target publish와 admission open을 분리했다.
  Target은 final authority root의 accepted journal을 replay한 뒤에도 Actor completion과 timer publication을
  보류한다. Source의 durable cleanup과 `Completed` authority CAS를 확인한 finalize command 뒤에만 bound
  participant별 command 44를 보내고, exact command 45 ACK, steady normalization을 차례로 완료한 다음 Actor와
  timer admission을 연다. `18-object-routing.ko.md` §3.3의 단계마다 조기 command 44와 조기 admission이 없음을
  focused target test로 검증했다. Source cleanup은 target으로 commit된 authority를 release하지 않고 local Actor
  registry와 User Spot을 `RelocationOut`으로 정리하는 전용 경로를 사용한다. `Retire` preflight에는 registration만
  보고 manual RouteMesh, ClientServer client, fanout subscriber와 Location Store가 없는 manual publisher를
  `ManualTopologyUnsupported(9)`로 차단하는 gate를 추가했다. Java compile과 source·target·control·scheduler·
  topology focused test가 통과했다. Public host `retire()`가 per-Mesh scheduler를 호출하는 production wiring과
  automatic descriptor/Core Ready snapshot gate는 아직 연결 중이므로 `V11-M6C-JVM`은 `진행`을 유지한다.
- JVM public Retire wiring checkpoint(2026-07-26)에서 위의 남은 host bridge를 연결했다. Host는 Location·
  authority·Relocation Store와 process-wide permit pool을 사용해 per-Mesh source builder, target endpoint,
  private control client와 scheduler를 구성하고, `Retiring` publication 뒤 active User Spot inventory를 실제
  remote relocation transaction으로 제출한 다음에만 drain을 시작한다. Authority commit 뒤 실패는 source로
  rollback하지 않고 bounded drain 후 `ForceStopped/RelocationFailed`로 끝낸다. Standalone Actor처럼 현재
  scheduler가 소유하지 않는 active inventory는 `Retiring` 전에 `StateIncompatible`로 차단한다. Automatic
  RouteMesh preflight는 Mesh마다 local이 아닌 non-draining replacement descriptor가 최소 하나 있어야 하고,
  snapshot의 모든 replacement RID와 lifecycle generation이 Core peer table의 `Admitted` entry와 정확히
  일치해야 통과한다. Empty snapshot과 stale generation을 거부하는 회귀, public manual publisher Retire가
  state 변경 없이 `Blocked/ManualTopologyUnsupported`로 끝나는 회귀, Java compile 및 Retire focused suite가
  통과했다. Accepted journal이 존재하는 production unit은 아직 target replay dispatcher가 준비되지 않아
  authority commit 전에 reversible failure로 중단한다. Queue·reply replay production 연결과 실제 두-host
  public stateful workload 검증이 남아 있으므로 `V11-M6C-JVM`은 `진행`을 유지한다.
  현재 Java core 전체는 591건 가운데 기존 2초 raw Node request timing test 1건만 첫 실행에서 timeout됐고,
  해당 test의 즉시 격리 재실행은 통과했다.

.NET Retire scheduler checkpoint(2026-07-25)에서 host 종료 authority를
`IZLinkFrameworkRuntime` 하나로 통일했다. 공개 `IZLinkDrainControl`, per-Mesh drain snapshot·result와
`IZLinkRouteMeshRuntime`의 drain bridge를 제거하고 ASP.NET hosted service·health check·DI를 host-wide
runtime에 연결했다. Process-wide relocation permit pool은 outbound·inbound 기본 64, Capture·Restore 기본
8과 payload in-flight 256 MiB를 하나의 accounting domain에서 all-or-none으로 예약한다. Actor drain은
live descriptor의 `EntrySpotId`를 target admission 주소로 사용하고 ready unit을 병렬 제출하며, permit을
받지 못한 unit은 source state를 바꾸지 않고 다음 bounded pass에서 다시 시도한다. Target Actor commit
경로는 실제 request·state·session·accepted frame bytes를 계산해 64 MiB 단위 상한과 inbound·Restore·payload
gate를 적용한다. Framework, ASP.NET, ContractTests와 UnitTests build가 통과했고 legacy public contract
focused 3/3, maintenance·permit·Actor handoff focused 74/74와 `git diff --check`가 통과했다. 공개 legacy
type 이름은 absence assertion에만 남아 있다.

이 checkpoint는 `V11-M6C-DN` 완료 증거가 아니다. Source payload bytes는 Capture 뒤 실제 크기로
accounting domain에 승격해야 하며, 현재 source admission은 unit·Capture permit까지만 연결됐다. 새
`IZLinkActorRelocationAdapter`를 실제 Actor materialization에 연결하는 작업, User Spot aggregate와
Instance Spot의 ready-first scheduler, target capacity reservation·factory·Restore-before-commit,
accepted reply relay ACK, timer·queue aggregate restore, STREAM fence와 descriptor
`Retiring`/`Draining` publication도 남아 있다.

.NET Actor Retire 후속 checkpoint(2026-07-25)에서 source remote handoff transaction이 outbound unit,
Capture callback과 Capture 뒤 측정한 payload lease를 같은 process-wide accounting domain에서 소유하도록
연결했다. Outbound unit과 필요한 Capture permit을 source seal 전에 예약하고, exact
`IZLinkActorRelocationAdapter`의 `CaptureAsync` 결과와 request·bound session·accepted frame bytes를 합산해
64 MiB unit 상한과 256 MiB in-flight gate를 적용한다. Payload gate를 얻지 못하면 기존 handoff rollback이
같은 source generation의 admission을 복원한다. Target은 wire policy marker와 등록 policy를 exact match로
검증하고 factory로 Actor를 만든 뒤 같은 scoped instance에 `RestoreAsync`를 실행한다. Factory·Restore와
ObjectGeneration 검증이 모두 끝난 뒤 authority visibility commit과 joined notification을 진행한다.
동일 handoff identity 비교에는 ObjectGeneration과 AuthorityOwnerGeneration도 포함한다. Framework source와
UnitTests build, relocation permit·exact adapter·Actor handoff·maintenance focused 76/76,
`git diff --check`가 통과했다. 넓은 Actor transfer·Entry Spot·deferred join 실행은 80건 중 52건이
통과했고 28건은 병렬 ObjectContext 전환 중 test Actor가 유효한 Context를 제공하지 않거나 actor type을
등록하지 않은 기존 fixture gap으로 실패했다.

이 후속 checkpoint도 `V11-M6C-DN` 완료 증거가 아니다. Target의 기존 takeover claim을 typed capacity
reservation과 exact authority publication 한 경계로 수렴하고 Restore 실패가 source authority를 훼손하지
않는 contract test를 추가해야 한다. User Spot aggregate·Instance Spot scheduler, accepted reply relay ACK,
timer·queue aggregate restore, STREAM fence와 descriptor publication gap도 그대로 남아 있다.

.NET Actor authority takeover checkpoint(2026-07-25)에서 두 대안을 비교했다. Target factory·Restore 전에
이전 location projection을 takeover한 뒤 exact authority를 commit하는 방식은 Restore 실패가 source
위치를 덮어쓸 수 있고 visibility 기준을 둘로 나누므로 채택하지 않았다. Target instance를 location write
없이 staging하고 Restore가 끝난 뒤 typed target capacity fence와 exact authority CAS를 단일 visibility
commit으로 사용하는 방식을 채택했다. Actor ownership coordinator의 projection write·remove와 projection
resolver 의존은 제거했다. 같은 owner의 Spot membership 변경은 authority payload의
`ObjectGeneration`·`AuthorityOwnerGeneration`을 보존하는 `Preserve` CAS로 처리하고, release는 exact
authority version의 `Delete` CAS를 사용한다. Relocation은 target descriptor와 owner lease를 검증해 Actor
capacity를 예약한 뒤 `NewOwner` CAS 한 번으로 owner와 Spot membership을 공개하며, 성공하거나 이미
commit된 결과만 target local ownership으로 추적한다. Restore는 capacity reservation과 authority CAS보다
먼저 실행되므로 Restore 실패는 source authority와 target capacity를 변경하지 않는다. Authority CAS 직후
handoff state를 committed로 표시하므로 이후 local 후처리 실패가 source rollback을 실행하지 않는다.
Location runtime이 없으면 authority commit을 생략하지 않고 `ActorRouteNotFound`로 실패하며 이를 확인하는
focused regression을 추가했다. Framework source build는 warning·error 0으로 통과했다. UnitTests build는
동시 authority-only Location test 전환에 남은 `IZLinkActorLocationStore` 참조 9건 때문에 아직 실행하지
못했다.

이 checkpoint도 `V11-M6C-DN` 완료 증거가 아니다. UnitTests authority fixture 전환 뒤 Restore 실패,
capacity conflict·abort, generation-preserving idempotent commit과 Location runtime 부재 regression을
실행해야 한다. Session route binding-before-CAS, accepted reply relay ACK, User Spot aggregate·Instance
Spot scheduler, timer·queue restore, STREAM fence와 descriptor publication도 남아 있다.

`.NET` Entry Actor interruption checkpoint(2026-07-28)에서 permit과 현재 turn 대기를
제외하고 queue admission을 닫은 직후부터 source cleanup, `Completed`, session route ACK와
steady normalization을 끝내고 target admission을 다시 연 시점까지 production 시간을
측정하도록 연결했다. Command 41은 target staging과 reservation을 확인할 뿐 target
admission 재개를 뜻하지 않는다. Histogram
당시 `zlink.actor.relocation.interruption`은 `membership_kind=entry`를 기록했다. 이후
공통 unit 계약에 따라 `zlink.relocation.interruption`과 `unit_kind=actor`,
`execution_mode=entry`로 교체했다. 1초를
넘으면 `zlink.runtime.relocation.changed` warning을 남기지만 relocation 결과,
rollback과 retry에는 영향을 주지 않는다. Monitoring이 꺼지면 timestamp와 operation
객체를 만들지 않으며 metric listener와 logger provider 예외도 relocation으로 전파하지
않는다. Focused unit 21/21과 실제 process `ST-G5-SMALL`,
`ST-G5-SLOW-CAPTURE`, `ST-G5-SLOW-RESTORE`가 통과했다. 각 실행은 target
handler의 후속 요청 처리까지 확인했다. `ST-G5-SLOW-CLEANUP`은 application
membership callback 0회와 1초 이하 interruption으로 교정해 다시 통과했다. 증거는 각각
`logs/20260728-052440-3827011`, `logs/20260728-052514-3829598`,
`logs/20260728-052457-3828519`, `logs/20260728-053623-16179`다.
`PerActor` User Spot production 경로와 `ST-G3`를 추가했다. Fresh process
`logs/20260728-075736-1950230`에서 Actor 100개와 Spot shell이 actor-a에서 actor-b로
이전됐다. 모든 Spot·Actor는 ObjectGeneration을 유지했고 Actor별 Capture payload와
Restore payload의 byte 수·SHA가 일치했다. Actor별 `transfer_in`은 정확히 1회였으며,
이동 뒤 Actor request 100건과 Spot request 1건을 actor-b가 처리했다. stderr와 전체
로그에는 `phase=error`, unhandled exception, data-loss와 timeout failure가 없었다.
이 증거는 `ST-G3`만 완료한다. 공통 relocation unit 1초 계측과 확장된 `ST-G5`는 별도
후속 작업이며 이 checkpoint는 `V11-M6C-DN` 완료 증거가 아니다.

.NET SpotWide Relocation Store I/O checkpoint(2026-07-28):

- Spot과 member Actor의 application `Capture`·`Restore` callback은 기존 Spot serial
  boundary와 Spot→Actor 순서를 유지한다.
- 64 MiB 이하 aggregate는 하나의 data chunk로 묶는다. 큰 aggregate는 64 MiB chunk의
  `Put`·read-back 확인·target `Read`·commit 전 `Renew`를 최대 4개, encoded component bytes
  합계 256 MiB batch로 병렬 실행한다.
- 모든 chunk의 bytes·checksum 확인이 끝난 뒤에만 manifest를 저장한다. 일부 저장이
  실패하면 manifest를 만들지 않고 연결되지 않은 chunk는 retention으로 정리한다.
  Target은 I/O 완료 순서와 관계없이 manifest 순서로 logical stream을 복원한다.
- 격리 artifacts 경로에서
  `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj -m:1 -p:UseArtifactsOutput=true -p:ArtifactsPath=<isolated> --filter FullyQualifiedName~RelocationTree --verbosity minimal`
  을 실행해 7/7이 통과했다. Small coalescing, 2-chunk Put·Read·Renew overlap,
  256 MiB batch bound, 순서 보존, missing·corrupt `RelocationDataLost`, partial Put에서
  manifest 0건을 확인했다. 이 component 증거는 SpotWide 1초 service interruption
  process E2E를 대신하지 않으므로 `V11-M6C-DN`은 계속 진행 상태다.

Node multi-Mesh host drain checkpoint(2026-07-24)에서 공개 per-Mesh `Drain`·`AwaitDrained`의
multi-Mesh 거부 계약은 유지하면서 NestJS shutdown 전용 aggregate coordinator를 연결했다. Host 경로는
모든 Mesh admission을 같은 deadline으로 seal하고 Mesh별 channel weight·resource를 병렬로 정리한다.
Location owner의 Draining publication과 cleanup은 process 공유 resource이므로 각각 한 번만 실행한다. 실패하면
모든 Mesh를 같은 force-stop 결과로 수렴시킨다. Focused drain contract 7/7, M6C 32/32, workspace
typecheck·build와 changed-source ESLint가 통과했다. M6C 재검증 중 발견한 User Spot Pending join의
factory 중복 시작, operation deadline signal을 rollback에 재사용하던 오류, Ready commit provider 예외의
raw 노출도 수정했다. Cleanup은 독립된 bounded deadline을 사용하고 timeout 뒤 unresolved provider Promise를
기다리지 않는다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다. Spot·Actor restore owner,
session route replacement와 실제 relocation completion gap이 남아 있으므로 `V11-M6C-NODE`는 계속 `진행`이다.

Node object routing·native resource checkpoint(2026-07-26)에서 통합 기준
`18-object-routing.ko.md`에 따라 direct route, Session binding route와 request reply route의 증거를
분리했다. Direct Spot·Actor는 positive Ready cache만 사용하고 cache miss에서만 Location Store를 읽는다.
`Missing`은 저장하지 않으며 실패한 현재 operation을 새 owner에게 자동으로 다시 제출하지 않는다.
Session relay는 bind 때 저장한 exact Actor route와 binding token을 사용하고 reply는 원래 request의
sequence·correlation·flow route를 보존한다. Session owner의 logical route replacement는 새 context의
local binding을 먼저 준비하고 이전 exact context·token을 정리하는 동안 기존 route를 계속 반환한 뒤
한 번에 새 route를 publish한다. 늦은 이전 token unbind는 새 route를 제거하지 않는다. Node focused
object routing 8/8, Session bind·rollback·reconnect focused 4/4와 stale Spot no-retry 3/3이 통과했다.
Command 44는 실제 Spot request transport를 사용하며 이전·target authority owner generation, binding
generation, 이전·target owner lease generation과 accepted high-water를 wire에 함께 고정한다. Session owner의
production routed handler는 현재 exact binding fence를 모두 검증하고 같은 ObjectGeneration의 route를
atomic하게 바꾼 뒤 동일 fence의 command 45 ACK를 반환한다. 같은 target과 handoff fence의 재전송만
idempotent ACK하며 이전 owner·binding·lease·high-water가 하나라도 다르면 route를 유지하고 거부한다. Target
admission은 이 ACK 전까지 transfer fence를 유지하고 ACK 뒤에만 steady 상태로 연다. 후속 command 42·43
slice는 source가 Actor state를 capture하기 전에 Session owner에 exact Actor·owner·binding·lease fence와
seal ID를 보내 ingress를 seal한다. Session owner는 seal 뒤 새 relay admission을 거부하고 마지막 accepted
sequence를 high-water로 고정한다. 같은 seal의 재전송만 같은 high-water로 ACK하며 다른 seal이나 fence는
route를 바꾸지 않고 거부한다. Source는 ACK를 받은 뒤 미실행 queue를 immutable accepted-journal root로
Relocation Store에 저장하고 CRC32C를 검증한다. Target은 이 root와 seal·high-water를 다시 검증한 뒤에만
command 44를 시작한다. Commit 전 rollback은 exact seal abort ACK를 요청하고 unlinked root를 삭제한 뒤
source admission을 복원한다. Command 44를 시작한 뒤에는 target Actor·Location을 source rollback 경로로
되돌리지 않는다. Command 45 ACK가 유실되면 target은 같은 command 44 payload와 seal을 먼저 즉시 한 번,
이후 bounded backoff로 재시도한다. Session owner는 이미 같은 target route를 publish한 exact retry만
idempotent ACK한다. Route seal release도 같은 seal의 중복 request를 ACK하며, release 뒤 target process가
다시 시작해 command 44를 재전송해도 같은 target generation·lease·high-water만 ACK하고 다른 route는
거부한다. Target application admission은 command 45 ACK와 exact route seal release ACK가 모두 끝난 뒤에만
steady 상태로 열린다. Actor manager 전체 74/74, command 42~45·exact stale fence focused 7/7,
ACK loss·release duplicate·restart replay focused 5/5, 해당 production source와 contract test의 scoped
ESLint 및 Entry Spot dispatch 9/9가 통과했다. Transfer payload는 command 44에 필요한 Session fence와
accepted-journal reference·checksum을 보존하며 field가 누락되거나 root 검증이 실패한 ownership
publication은 request를 보내기 전에 실패한다. `stream-runtime.test.js` 전체는 기존과 같은 87건 중
87건이 모두 통과했다. 남아 있던 13건 가운데 production 원인은 Runtime host가 Session bind 확인,
저장 route relay와 disconnect 통지를 Actor packet runtime에 연결하지 않은 문제와 same-node Spot join이
native binding을 불필요하게 강제 refresh하던 문제였다. Host는 exact ActorRef bind를 target owner에게
확인한 뒤 저장 route를 사용하며, join 뒤에는 `commitActorRoute`가 실제 owner 변경이 있을 때만 route를
교체한다. 나머지는 legacy ownership marker, 이전 Actor factory 인자 순서, 결과가 없는 one-way transport
mock과 bind confirmation을 처리하지 않은 route fixture였다. 호환 helper를 추가하지 않고 현재 계약에
맞게 test를 정리했다. Actor manager 전체 74/74와 framework TypeScript build, 변경 파일 scoped ESLint,
`git diff --check`도 통과했다. 실제 다중 process crash/restart 증거는 별도 gap으로 남는다.
[Spot·Actor routing 통합 spec](../../framework/common/spec/18-object-routing.ko.md)의 §3.3을 이 경계의
기준으로 사용한다. NestJS fixture는 Location Store가 있는 object capability에서만 public Spot manager를
노출하고 현재 ClientServer role 등록 계약을 사용하도록 정렬했으며 전체 60/60이 종료까지 통과했다.

Node durable relocation coordinator checkpoint(2026-07-26)에서 owner commit 뒤 lifecycle을
target hidden staging publish → accepted journal replay → durable source cleanup과 final authority 완료
→ Session command 44·45 route replacement → steady normalization → target admission open → authority
reference 해제와 immutable payload 삭제 순서로 고정했다. 이전 구현은 owner commit 직후 target publish와
route replacement만 수행하고 나머지를 별도 `complete()`에 맡겨, journal replay와 admission 경계를
보장하지 못했다. Post-commit callback은 같은 staging ID와 authority fence의 exact duplicate를
idempotent하게 수용해야 하며 `resumeCommitted(...)`가 ACK loss나 process fault 뒤 같은 phase를 다시
진행한다. Route replacement 첫 시도가 실패하는 fault test에서 source abort 0, admission open 0과
committed target owner·immutable root 유지를 확인했고, exact retry 뒤 route replacement·normalization·
admission을 한 번 완료한 다음에만 root를 해제했다. Node M6C 42/42, `stream-runtime` 87/87,
`actor-manager` 74/74와 Framework TypeScript build, production coordinator scoped ESLint가 통과했다.
이번 checkpoint는 production coordinator의 ordering과 retry 경계를 닫았지만 concrete User Spot aggregate·
standalone Actor restore owner, source cleanup authority writer와 host Retire scheduler adapter는 아직 이
coordinator에 연결되지 않았다. 이 adapter 연결 전에는 active workload Retire 완료 증거로 사용하지 않는다.

Node relocation envelope v2 checkpoint(2026-07-26)에서 test scaffold의 aggregate 전역 queue·timer와
participant key만 저장하던 형식을 production participant inventory로 교체했다. 각 participant는
`objectKind`, `stableType`, `ObjectGeneration`, `AuthorityOwnerGeneration`, application state, accepted
journal과 자기 queue·logical timer를 함께 보존한다. Inventory digest는 participant key뿐 아니라 이
identity와 generation fence 전체를 정렬한 뒤 계산하므로 같은 ID의 다른 incarnation이나 이전 owner
payload를 같은 aggregate로 사용할 수 없다. Codec은 participant, queue와 timer를 결정적인 순서로
정렬하고 version 2의 정확한 field 집합만 읽는다. 0 generation, 알 수 없는 object kind, 추가 field,
participant 안에서 중복된 queue sequence와 timer ID를 거부하는 negative contract를 추가했다. Node
M6C 44/44, framework 전체 TypeScript typecheck, production codec scoped ESLint, Actor manager와
stream runtime 합계 161/161 및 `git diff --check`가 통과했다. 이 checkpoint는 durable payload의
identity·work inventory 경계만 닫았다. User Spot aggregate와 standalone Actor의 concrete capture/restore
owner, membership inventory, production authority phase codec, source cleanup authority writer와 Host Retire
scheduler adapter 연결은 다음 checkpoint에 남아 있으므로 `V11-M6C-NODE`는 계속 `진행`이다.

Node source-cleanup authority checkpoint(2026-07-26)에서 test-local JSON wrapper를 제거하고 exact
production authority phase codec을 연결했다. Authority payload는 source-cleanup `Pending` 또는
`Completed` phase, immutable root reference·CRC32C, aggregate ID·generation, inventory SHA-256와 target
owner ID·lease generation을 결정적인 field 순서로 보존하며 알 수 없는 field, phase와 잘못된 UUID·digest·
generation을 읽지 않는다. Initial publication은 target owner fence를 함께 고정하고 owner commit은 이 fence와
다른 target을 거부한다. Production source authority writer는 pending root를 복원하여 aggregate·inventory·
checksum을 확인한 뒤 generation을 하나 증가시킨 completed immutable root를 먼저 저장한다. 그 다음
Location authority reference를 `Preserve` CAS로 교체하고, CAS 결과 또는 response-loss reconciliation에서
completed reference가 확인된 뒤에만 pending root를 삭제한다. Stale retry는 current completed authority와
root의 reference·checksum·aggregate·owner·inventory를 다시 검증하고 새 root나 CAS를 만들지 않는다.
CAS conflict fault에서는 새 completed orphan만 삭제하고 authority가 가리키는 pending root를 유지한다.
Node M6C 47/47, framework TypeScript typecheck, production relocation runtime·coordinator scoped ESLint,
Actor manager와 stream runtime 합계 161/161 및 `git diff --check`가 통과했다. Concrete User Spot
aggregate·standalone Actor capture/restore owner와 membership inventory, Host Retire scheduler adapter는
아직 연결되지 않았으므로 `V11-M6C-NODE`는 계속 `진행`이다.

Node concrete relocation object-owner checkpoint(2026-07-26)에서 User Spot aggregate와 standalone Actor의
source capture·target hidden restore owner를 production internal runtime으로 추가했다. Source capture는 모든
participant의 exact object kind·stable type·object generation·authority owner generation을 고정하고 실행을
seal한 뒤 application state, accepted journal, participant queue와 logical timer를 수집한다. User Spot
aggregate의 canonical membership inventory는 Actor authority key, Spot authority key·generation과 membership
epoch를 Actor마다 한 번 기록하며 inventory digest에 포함된다. Standalone Actor는 aggregate에 포함되지 않는
Entry Spot membership fence도 같은 형식으로 보존한다. Target owner는 모든 participant factory와 application
Restore를 hidden 상태에서 실행하고 membership 전체를 적용한 뒤에만 immutable root를 Location authority에
publish하고 owner CAS를 시작한다. Restore failure test에서는 target staging만 abort되고 Relocation Store put과
authority CAS가 0회이며 기존 source store version과 payload가 유지됐다. Commit 뒤에는 target publish,
accepted journal과 queue sequence replay, logical timer restore, durable source seal commit과 Completed authority,
Session route replacement, steady normalization, admission open 순서를 따른다. Exact test는 queue·timer replay와
route replacement가 admission보다 앞서고 factory·Restore·membership 적용이 첫 authority CAS보다 앞서는 것을
확인했다. Node M6C 50/50, framework TypeScript typecheck, relocation production source scoped ESLint, Actor
manager와 stream runtime 합계 161/161 및 `git diff --check`가 통과했다.

이번 checkpoint는 object capture와 hidden target lifecycle을 coordinator에 연결한 증거다. Host의 실제
`ZLinkSpotActivation`, Actor manager, adapter registry와 mailbox를 이 port에 연결하고 User Spot participant
authority 전체를 Location Store aggregate prepare·commit으로 publish하는 Host Retire adapter는 아직 남아
있다. 이 연결 전에는 active workload User Spot aggregate Retire 완료로 판정하지 않으며
`V11-M6C-NODE`는 계속 `진행`이다.

Node deployment identity·Retire eligibility checkpoint(2026-07-26)에서 exact interface에만 있고
production builder에는 없던 `setApplicationVersion(bigint)`과 `setMaintenanceWave(string)`을 Framework와
NestJS builder에 연결했다. 설정값은 host registration을 거쳐 모든 local MeshNode descriptor에 게시된다.
Application version의 기본값은 `0`이고 허용 범위는 `0..2^63-1`이다. Maintenance wave는 optional이며
설정하면 NUL이 없는 UTF-8 `1..255` bytes인지 startup registration에서 검증한다. Retire replacement는
source와 같거나 더 큰 application version이어야 하며 source에 wave가 설정되었으면 같은 wave를 제외한다.
Framework·NestJS workspace build와 deployment identity·exact RID·lifecycle·version·wave focused contract
13/13이 통과했다.

Production active-workload relocation은 아직 완료하지 않았다. `ServiceRelocationCoordinator`는 hidden target
owner callback을 조합하는 local component이지만 production host에는 source `ZLinkSpotActivation`·Actor
mailbox를 capture unit으로 투영하는 port, 선택한 remote owner에서 factory·adapter Restore·membership·queue·timer
staging을 실행하는 control request/handler, source cleanup 뒤 Session route replacement command 44/45 ACK를
연결하는 port가 없다. 기존 `handoffActorsForDrain`은 Actor Join 전용이라 User Spot·Instance Spot aggregate를
대체할 수 없다. 이 세 port 없이 source host가 remote target object를 직접 materialize하는 test-only registry나
local callback을 넣지 않는다. Active User Spot aggregate·standalone Actor·Instance Spot의 production bridge와
two-owner host regression이 추가되기 전까지 `V11-M6C-NODE`는 계속 `진행`이고 관련 blocked issue는
`BLK-042`다.

Node maintenance recovery-retention correction(2026-07-26)에서 .NET 독립 review의 공통 P1을 반영해
coordinator가 admission open 직후 immutable recovery root를 해제하던 순서를 제거했다. 모든 production
coordinator 구성은 `ServiceRelocationRecoveryRetention`을 제공해야 하며 terminal·reply relay ACK 또는 exact
source lease expiry 가운데 하나가 recovery pointer를 안전하게 해제할 때까지 기다린다. Focused fault test는
target publish·journal replay·source completion·Session route replacement·steady normalization·admission open이
끝나도 retention gate가 닫혀 있으면 authority reference와 root가 유지되고, gate ACK 뒤에만 authority CAS와
payload delete가 실행됨을 확인한다. Node M6C 50/50과 framework TypeScript typecheck, coordinator scoped
ESLint가 통과했다.

이 correction은 기존 coordinator ordering을 Host bridge에 그대로 복제하지 않기 위한 선행 변경이다.
아직 Host bridge에는 full planned marker bytes permit, normalization 전 sealed local materialization, exact
aggregate abort와 commit의 선형화, completed tombstone의 active slot 즉시 반환, replay cursor·terminal reply를
담은 immutable successor root와 authority CAS가 연결되지 않았다. 이 항목과 participant authority 전체의
aggregate prepare·commit을 닫기 전에는 Host Retire adapter를 완료로 판정하지 않는다.

Node canonical relocation wire correction checkpoint(2026-07-26)에서 앞선 `relocation envelope v2`
checkpoint의 JSON 형식이 공통 service wire 계약이 아니라는 차이를 확인했다. Production canonical decoder는
`service-wire-v1.schema.json`과 `golden/relocation-envelope-v1.json`의 binary logical stream을 읽고 numeric
participant ID별 `acceptedBoundary`·`replayCursor`, request source fence·operation ID·terminal result·delivery
state와 application payload를 가진 terminal completion을 projection한다. Golden bytes의 decode 뒤
byte-identical re-encode, participant count 초과, progress duplicate·순서 위반, replay cursor가 accepted
boundary를 넘는 경우와 trailing byte 거부를 검증했다. Node typecheck와 M6C 52/52가 통과했다.

이 checkpoint는 canonical decoder의 첫 slice만 완료했다. 기존 durable coordinator는 아직 language-local
JSON v2 encoder를 사용하므로 interop 증거가 아니며, canonical successor root mutation encoder와 authority의
participant progress·terminal completion count·pending relay count exact match를 연결한 뒤 JSON 경로를
제거해야 한다. 그 전에는 Node relocation codec이나 Host bridge를 완료로 판정하지 않는다.

후속 canonical successor checkpoint에서 immutable journal·timer 구간을 byte-for-byte 보존하면서 progress와
structured terminal completion vector를 다시 인코딩하는 경로를 추가했다. Replay cursor가 증가하면 그 cursor
이하의 journal entry를 successor root에서 제거하므로 root를 다시 읽어도 journal order invariant가 유지된다.
Authority relocation state는 root의 relocation ID·application version·participant progress와 exact match하고,
terminal completion 전체 개수와 delivery state가 `pending`인 relay 개수를 root에서 직접 계산한다. Source·target·
coordinator node·owner fence가 없거나 0이면 Relocation Store write와 CAS 전에 실패한다. Canonical owner authority
payload에서는 relocation field만 교체하고 전체 CRC32C를 다시 계산하며 다른 owner·activation field는 보존한다.
Golden byte-identical re-encode, progress·delivery successor round-trip, root identity mismatch와 missing fence 거부를
포함해 Node typecheck, M6C 53/53, scoped ESLint와 `git diff --check`가 통과했다. 기존 coordinator의 JSON v2
publication 호출을 이 canonical CAS로 바꾸는 작업과 Host descriptor bridge는 남아 있으므로 완료 판정은 유지하지
않는다.

Canonical publication follow-up에서 production runtime이 canonical root를 먼저 저장하고 provider CRC32C receipt를
검증한 뒤 기존 canonical authority payload의 relocation state만 교체해 expected store version CAS로 공개하도록
연결했다. CAS reply가 유실되면 current authority payload가 생성한 exact bytes와 같은 경우만 성공으로 수렴하며,
다르면 새 orphan root를 삭제한다. Successor는 새 immutable root CAS 뒤 이전 root를 삭제하고, terminal completion의
delivery state가 `pending`이면 authority reference release를 거부한다. Relay ACK를 기록한 successor CAS 뒤에만
relocation state clear CAS와 root delete를 수행한다. Response-loss와 pending relay gate focused test를 포함해 Node
typecheck, M6C 54/54, scoped ESLint와 `git diff --check`가 통과했다. 기존 coordinator의 JSON envelope 호출 제거와
Host Retire bridge는 계속 남아 있다.

Node Deferred Join 오류 분류 follow-up(2026-07-26)에서 application admission exception을
`RequestFailed`로 변환하면서 `isRetriable=true`를 강제로 기록하던 drift를 제거했다. 공통 오류
분류의 `RequestFailed` 기본값과 같은 non-retriable completion을 Actor callback에 전달한다. Framework
workspace build와 `deferred-actor-join.test.js` 4/4가 통과했다. Production Host relocation bridge는
이 수정과 별개이며 `BLK-042`의 완료 조건을 그대로 유지한다.

기존 NestJS·`channel-client` 종료 지연은 Framework shutdown 누락이 아니라 Node binding의 long-lived
send-ready·monitor callback TSFN이 Node test worker를 참조하던 문제였다. 두 callback queue를 stream
callback과 같은 unreferenced resource로 바꾸고 binding process-exit regression 2/2를 추가했다. 승인된
Core package provenance `2313515b…`로 Node 11.0.0 local package를 다시 만들고 workspace에 설치한 뒤
NestJS runner가 2.6초 안에 clean exit했다. `channel-client` 전체 runner도 기존 종료 blocker를 지나
진행했다. ClientServer fixture는 같은 process의 Client·Server role, dedicated Location descriptor와
admission 뒤 submit 계약으로 맞췄다. Manual RouteMesh helper는 peer RID를 알고 연결하는 현재 계약으로
바꾸고, startup 실패 child도 runtime을 정리한 뒤 종료한다. 동일 weight Server를 weight 크기만큼 연속
선택하던 selector는 smooth weighted round-robin으로 바꿔 같은 weight 후보를 순환하고 서로 다른
weight의 장기 비율도 유지한다. Admission 전에 fake DEALER로 submit하던 중복 test 3개는 제거했으며
같은 backpressure·SEND_READY·single outstanding request 계약은 `ZLinkAsyncSubmitter` focused test가
검증한다. `channel-client.test.js` 전체 91/91, Node workspace build, object routing 8/8, NestJS 60/60과
binding process-exit 2/2가 clean exit로 통과했다. 이 checkpoint로 기존 `channel-client` 종료·fixture
blocker는 해소했지만 Spot·Actor restore owner, session route replacement와 실제 relocation completion
gap이 남아 있으므로 `V11-M6A-NODE`와 `V11-M6C-NODE`는 계속 `진행`이다. Sample·E2E source는
변경하거나 실행하지 않았다.

`V11-M6C-CPP` 추가 증거(2026-07-23): 설치 public header에 authority read·CAS·scan, object
reservation·aggregate와 분리된 Relocation Store 계약을 추가하고
`add_relocation_store(...)`를 기존 Location Store 등록과 분리했다. Public Relocation Store task를
M6C immutable payload port에 연결하는 adapter는 payload bytes, CRC32C, missing과 delete 결과를 보존한다.
`app_t::retire(...)`와 `shutdown(...)`은 first intent와 deadline을 공유하고 각
`wait_cancellation`만 취소한다. 실행 중인 host에서는 hosted-service teardown이 끝난 뒤 terminal result를
완료하며 signal, `stop()`과 `request_stop()`도 같은 Shutdown operation을 시작한다.
`test_cpp_framework_m6a_runtime`, `test_cpp_framework_m6b_runtime`,
`test_cpp_framework_m6c_runtime`, `test_cpp_framework_termination_facade` 4/4와 전체
`zlink_framework` compile이 통과했다. 이어서 `location_store_t`가 authority와 object creation
provider를 직접 상속하도록 연결하고 in-memory provider에 exact authority read·CAS·snapshot scan,
reservation과 aggregate prepare·commit·abort를 구현했다. `Preserve`는 owner metadata를 그대로 유지하고
`NewOwner`는 payload를 해석하지 않은 채 Framework가 전달한 exact target owner token을 같은
critical section에서 검증한다. Missing 생성은 `Reserve`만 수행한다. Reservation의 Creating payload와 commit의 Ready payload도 provider가
합성하지 않고 exact bytes를 저장한다. Public authority adapter는 Framework가 소유한 relocation publication
payload만 encode·decode하고 provider payload는 해석하지 않는다. Target preflight가 반환한 owner token은
standalone relocation CAS까지 전달한다. `CA-D46`에 따라 existing object relocation 전용 capacity
fence와 provider를 public header에 추가하고 `location_store_t`가 이 capability를 직접 상속하게 했다.
`NewOwner` CAS는 exact target owner와 relocation capacity fence를 함께 요구하며, 성공한 CAS만 source active와
target pending-to-active를 전환한다. Aggregate prepare는 `NewOwner` participant와 capacity fence를 authority
key 기준 exact 1:1로 검증하고 missing·duplicate·extra fence를 mutation 0인 conflict로 끝낸다. Target preflight와
standalone·aggregate runtime seam도 creation reservation이 아닌 전용 fence를 전달한다. 기존 Location resolver
test double도 새 provider 계약을 위임하도록
갱신했으며 generated protocol include 누락을 바로잡았다. 변경 뒤
`test_cpp_framework_m6c_runtime`, `test_cpp_framework_in_memory_location_store`,
`test_cpp_framework_termination_facade`, `test_cpp_framework_store_location_resolvers` 4/4와 전체
`zlink_framework` compile이 통과했다. 공식 Redis Location provider의 exact authority·reservation 구현,
owner lease claim/read/renew/release, descriptor capacity topology와 실제 target factory·Restore-before-commit
wire dispatch는 남아 있으므로 row 상태와 `V11-R5C`는 계속 `진행`과 `대기`다. Core·bindings와
Sample·E2E source를 변경하거나 실행하지 않았다.

2026-07-26 C++ maintenance topology preflight follow-up에서 Retire가 lifecycle·admission을 바꾸기 전에
local manual ClientServer client, fanout subscriber, Mesh peer와 route peer registration을 검사하도록
연결했다. Automatic Mesh는 complete descriptor page와 live owner lease를 읽고, 모든 local RID를 제외한
Serving·non-draining 후보의 application version·maintenance wave·object capability·headroom을 검증한 뒤
Core topology의 exact RID·lifecycle generation admitted peer와 대조한다. Manual 구성은
`Blocked/ManualTopologyUnsupported`, replacement 부재는 `Blocked/TargetUnavailable`로 끝나며 기존
`Serving` 상태를 유지하고 이후 `Shutdown`은 허용한다. 변경 object와 focused termination binary는
compile·실행 exit 0이며 전역 diff check도 통과했다. 전체 Framework build는 이 변경과 무관한 concurrent
`spot_context_t` copy/move compile 오류에서 중단됐다. 이어서 descriptor publication을
`Retiring → admission seal·barrier → relocation → Draining → peer marker·final barrier·cleanup → disconnect`
순서로 연결했다. Partial Retiring publication failure는 시도한 descriptor를 `Serving`으로 복구하고 local
admission을 유지하며, seal 뒤 Draining publication failure는 `ForceStopped`로 끝낸다. Termination facade와
vertical fault/order focused test가 각각 exit 0이고 변경 object compile도 통과했다. 남은
`spot_context_t` ownership 오류는 public copy나 move-assignment를 다시 허용하지 않고 해소했다. Registry가
소유한 internal state에서 필요한 위치마다 새 move-only Context handle을 만들고 `optional`·map·waiter·active
Context 경로는 direct construction 또는 `emplace`로 통일했다. 전체 `zlink_framework` build와 contract
headers, Instance Spot activation, Actor gateway, MeshNode vertical, termination facade 5/5가 fresh build·실행으로
통과했다. Store/location resolver fixture도 protected default constructor를 직접 호출하지 않고 의도적으로
비어 있는 derived test Context를 만들어 mismatch를 검증하도록 고쳤으며 fresh 41/41이 통과했다. Topology
ordering과 Context ownership blocker는 해소됐지만 Redis authority·target factory·Restore와 relocation
completion gap이 남아 있으므로 `V11-M6C-CPP`는 `진행`을 유지한다.

C++ canonical relocation control checkpoint(2026-07-26)에서 공통
`service-wire-v1.schema.json`의 command 30·31·32·34·35·40·41 codec을 추가하고
`golden/relocation-control-v1.json`의 일곱 fixture를 byte-for-byte decode→encode했다. Relocation ID,
attempt·coordinator fence, closed role·round·phase enum, conditional object identity, sorted unique participant와
terminal vector, `replayCursor <= acceptedBoundary`, 2,048 participant bound, command 30의 ordered unique TLV와
필수 field·durable root pair를 검증한다. Truncation, duplicate participant·TLV, progress 역전, bound 초과와
unknown phase를 fail-closed로 거부한다. `raw_mesh_node_owner_t`는 별도 request correlation wrapper 없이 이
command를 독립 infrastructure frame으로 전송·수신하고, frame에 exact sender fence가 있는 command는 admitted
peer RID·lifecycle generation과 대조한 뒤 infrastructure mailbox에 제출한다. 실제 두 owner를 사용한
command 40 production transport 회귀도 통과했다. 후속 full-union checkpoint에서는 command 31의
`frozen-record` 14개 record kind를 모두 해석·검증하고 canonical bytes를 byte-for-byte 보존한다. Frozen source의
node·Spot·Actor·bound Session closed union, metadata version·1,024-byte bound·unique key, operation kind·ID와
reply route matrix, node·channel·Spot·Actor send/request, multicast, Actor lifecycle control, completion,
send-ready destination, relocation control과 Instance activation의 모든 conditional body를 schema대로
fail-closed 검증한다. Request와 bound Session source, Actor lifecycle 5종, send-ready destination 5종,
Instance ready·cold route, terminal/payload 관계와 각 record truncation·closed enum·metadata/source·operation
관계 negative를 focused codec test에 추가했다. Shared command 31 fixture도 authoritative operation matrix에
맞춰 relocation control의 frozen operation ID를 zero로 정정했고 C++ byte parity를 다시 확인했다. Codec binary,
M6A·M6B·M6C focused binary와 전체 `zlink_framework` build가 모두 exit 0이다. 다만 현재
`encode_frozen_record(...)`의 reader·preserver와 별도로 Spot·Actor send/request의 typed field에서 새 canonical
bytes를 생성하는 `encode_frozen_application_record(...)`를 추가했다. Source closed union, metadata, operation·reply
matrix, Spot·Actor target fence, application envelope를 작성한 뒤 동일한 strict decoder로 다시 검증하며 zero fence,
body-kind mismatch와 request reply route 누락을 fail-closed로 거부한다. Focused test에서 production mailbox가
capture할 수 있는 네 application record kind의 typed encode→decode를 통과했다. 다만 실제 ingress
`service_mailbox_record_t`는 source RID만 보존하고 source lifecycle generation·Location owner ID·owner lease를
보존하지 않으며, 현재 Spot·Actor service header도 target owner lease와 완전한 source authority fence를 전달하지
않는다. 이 값은 RID나 lifecycle generation으로 추정할 수 없으므로 production `turn_record_t` 연결은 수행하지
않았다. 가장 좁은 후속 변경은 outbound Location authority에서 exact source owner fence와 target owner lease를
service header에 넣고, admitted peer 검증 뒤 mailbox record까지 그대로 전파하는 것이다. Production replay·이
exact fence propagation·mixed-language process E2E가 남아 있으므로 이 checkpoint를
`V11-M6C-CPP`·`BLK-044` 완료 증거로 사용하지 않는다.

C++ STREAM TLS parity checkpoint(2026-07-26)에서 정식
`stream_node_options_builder_t::set_tls_server(certificate, key,
require_client_certificate = false)`가 client certificate 요구 여부를 registration snapshot과 production TLS
listener까지 보존하도록 연결했다. `true`이면 system trust를 사용해
`verify_peer | verify_fail_if_no_peer_cert | verify_client_once`를 설정하고 TLS handshake가 끝나기 전에
인증서 없는 client를 거부하므로 application Session을 만들지 않는다. 계약 밖 low-level
`stream_builder_t::set_tls_server` 공개 메서드는 제거하고 Framework options 내부 configuration 경로만 사용한다.
전체 `zlink_framework` build, contract header와 STREAM runtime test가 통과했으며 인증서 없는 실제 TLS client에서
Session callback 0을 확인했다. `30-implementation-gap.ko.md`의 §12.30 항목을 제거했다.

JVM lane은 `CA-D44`~`CA-D46`의 exact provider 계약을 Java runtime과 Kotlin coroutine bridge에
반영했다. `ZLinkAuthorityPut`은 transition별 target owner와 relocation capacity fence 조합을 생성자에서
검증하며 snapshot과 stored 결과는 owner ID·lease generation과 provider-owned placement allocation을
반환한다. Allocation은 Pending·Active 상태, object kind·stable type, descriptor key·lifecycle generation과
capacity delta를 함께 보존한다. Public `NEW_OBJECT` transition과 Missing CAS expectation은 제거했으며 generic
reserve만 Missing에서 Pending을 만들고 commit만 Active로 전환한다. Owner lease는
claim·read·renew·release의 exact token 계약으로 바꾸고 in-memory와 Redis script가 provider의 같은
critical section 또는 Redis transaction에서 lease generation과 expiry를 검증한다. Object creation
reservation은 application이 전달한 Pending payload를 그대로 저장하고 commit에서 Active payload로
교체한다. Commit은 target descriptor·owner lease가 current인지 다시 검증하고, abort는 current liveness와
관계없이 exact Pending reservation을 정리한다. Existing object relocation은 별도 capacity reservation과
fence를 사용하며 `NEW_OWNER`
standalone CAS는 fence가 reserved 상태이고 reservation의 authority key·expected StoreVersion·source owner·target owner가
current Active allocation과 exact match일 때만 CAS를 적용한다. Source owner lease와 descriptor의 current
liveness는 요구하지 않고 target descriptor·owner lease만 reserve, prepare와 commit 직전에 검증한다.
Committed·aborted·stale·mismatch fence는 authority와 capacity mutation 0인 conflict로 끝낸다. In-memory
provider는 creation·delete·relocation·aggregate abort/commit의 pending·active capacity delta를 authority
mutation과 같은 lock에서 갱신한다. Aggregate prepare는 `NEW_OWNER` participant와
capacity fence를 authority key 기준 exact 1:1로 검증하고 missing·duplicate·extra fence를 mutation 0인
conflict로 끝낸다. Reserved fence는 exact `(aggregateId, aggregateGeneration)`에 Prepared 상태로 연결한다.
연결 뒤 direct abort는 stale이고 다른 aggregate의 prepare는 conflict며, exact duplicate만 already-prepared다.
Commit이 target stale로 실패해도 binding과 authority를 유지하고 aggregate abort만 fence와 pending delta를
해제한다. Java·Kotlin exact public inventory, Kotlin의 public capacity bridge와
`ZLinkSuspendingLocationStore` suspend hook도 같은 계약으로 맞췄다.

Creation request와 reservation은 target descriptor lifecycle generation을, relocation capacity request는
source·target descriptor lifecycle generation을 exact field로 보존한다. Commit과 abort는 호출자가 돌려준
reservation이 reserve 당시 record 및 Pending allocation과 정확히 일치하는지 다시 확인한다. Aggregate prepare와
commit도 capacity reservation record의 authority version·current source owner·source Active allocation·aggregate
target owner와 target descriptor·lease를 mutation 전에 다시 검증한다.

검증은 Java core 424/424, Spring starter 33/33, Kotlin 43/43과 Redis provider 18건 중 환경 의존
7건 skip·실패 0으로
통과했다. JVM Redis provider에는 exact MeshNode target capacity descriptor row와 target scheduler/factory가 아직
없으므로 capacity reserve는 `TargetUnavailable`로 끝내며 Redis aggregate도 성공 상태를 합성하지 않는다.
따라서 실제 Retire relocation은 admission seal 전에 계속 차단하고 `V11-M6C-JVM`은 `진행`,
`V11-R5C`는 `대기`로 유지한다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지 않았다.

2026-07-26 JVM production relocation checkpoint에서 Completed aggregate를 steady authority로 바꾸던 no-op을
실제 aggregate CAS 경로로 교체했다. 모든 participant를 exact aggregate·owner·object generation fence로 먼저
검증하고 relocation wrapper의 application authority payload를 `PRESERVE` mutation으로 구성해 prepare·commit
한 번으로 전환한다. Owner·object·authority generation과 descriptor는 유지하며 전체가 이미 steady면 idempotent
success, 일부만 steady인 mixed visibility는 DataLost로 거부한다. Coordinator focused 10/10, Java core 최초
full 592/592와 Kotlin 49/49가 통과했다. CPU 병렬 부하 중 Java full 재실행에서 서로 다른 기존 raw transport
timing test가 한 건씩 실패했으나 relocation focused와 해당 remote User Spot create isolated 재실행은 통과했다.
남은 `BLK-043`은 accepted journal이 original source request의 reply capability·owner/lease fence를 보존하지 않고
queue commit에서 즉시 close되는 문제, source relay terminal 뒤 closed ACK 경로, Actor frozen record의 bound-session
generation·sequence 보존, standalone Actor durable builder·target endpoint다. 이 경계를 구현하기 전에는 two-owner
production host 완료 증거로 판정하지 않는다.

2026-07-26 JVM production relocation 후속 checkpoint에서 User Spot aggregate의 accepted Spot·Actor
record를 target의 hidden runtime에 dispatch하고, handler가 끝난 뒤 replay cursor와 terminal payload를
canonical immutable root에 먼저 기록하도록 production 경로를 연결했다. Location Store aggregate CAS가 새
root reference를 공개하기 전에는 source reply를 전달하지 않는다. Target은 canonical service-wire command
33과 application payload를 RouteMesh의 raw service frame으로 직접 보내고, source는 operation·reply route,
authenticated target RID·lifecycle generation, target owner lease, authority Store version, participant·sequence와
durable completion을 모두 대조한 뒤 source의 보존된 reply capability를 한 번만 완료한다. Closed command 46은
relocation·coordinator·operation과 exact source owner lease·RID·lifecycle generation을 다시 확인하며 ACK가
유실되면 3회 bounded retry 뒤 Location Store가 반환한 `storeNow` 기준의 exact source lease expiry만 terminal
evidence로 인정한다. ACK 또는 lease expiry 상태는 다음 immutable root와 Location Store CAS에 기록하고,
source cleanup publication도 최신 replay root를 보존한다. 모든 completion의 delivery state가 ACK 또는 expiry로
닫힌 root만 recovery pointer를 해제한다. Private relocation-control reply command와 ACK는 이 완료 증거에
사용하지 않는다. Shared command 33·46 byte fixture, terminal/failure integrity, non-ok payload 거부, source
owner lease 불일치, cursor·delivery state와 source-cleanup 보존을 포함한 focused relocation 26/26과 Java core
전체 601/601이 통과했다. 남은 `BLK-043`은 standalone Actor의 source builder·target endpoint와 실제 two-owner
process recovery test다. 이 항목을 닫기 전까지 `V11-M6C-JVM`은 `진행`, `V11-R5C`는 `대기`를 유지한다.
Core·bindings와 Sample·E2E source를 변경하거나 실행하지 않았다.

후속 교차 transport audit에서 확인한 JVM command 33·46 운반 방식의 발산을 수정했다.
`ZLinkJavaRawMeshNode.requestRelocationReplyRelay`는 command 33을 request sequence가 없는 독립 raw
infrastructure send로 제출하고 relocation·coordinator·operation·예상 source RID로 pending ACK를 고정한다.
Source도 handler가 만든 command 46을 transport reply가 아닌 독립 send로 반환한다. Target은 durable completion에서
고정한 source owner·lease·RID·lifecycle generation 전체, 인증된 transport sender RID, admitted descriptor의
lifecycle generation, command 46의 request-source fence와 pending key가 모두
일치할 때만 terminal을 완료한다. 중복 pending key는 거부하고 ACK timeout과 runtime close는 pending completion을
명시적으로 종료한다. 실제 두 raw MeshNode가 양방향 admission 뒤 command 33과 payload를 전달하고 별도 command 46을
회수하며 중복 pending, ACK 유실 timeout과 stale source lifecycle ACK 거부를 확인하는 focused test 2/2가 통과했다.
이 변경으로 JVM의 request/reply transport 의존은 제거했지만 다섯 언어
mixed-process command 30~46 검증과 standalone Actor relocation은 아직 남아 있으므로 `BLK-044`는 유지한다.
Java core 전체 회귀는 CPU 병렬 부하 상태에서 602개 중 기존 timing test 2개가 실패했고, 두 test를
`--max-workers=1`로 즉시 격리 재실행한 결과 2/2가 통과했다. 따라서 기능 회귀는 확인되지 않았지만 전체 suite
단일 실행의 clean 증거는 부하가 낮아진 시점에 다시 확보한다.

JVM canonical relocation control codec checkpoint(2026-07-26)에서 command 30·31·32·34·35·40·41의
strict reader·byte-preserving encoder를 추가했다. Relocation ID, attempt·round·role·coordinator·candidate·object
fence, 2,048개 participant·progress·terminal bound와 ordered uniqueness, progress cursor, root presence,
command 30의 ordered required TLV와 pair field를 fail-closed로 검증한다. Command 31은 frozen-record 14종의
closed kind와 node·Spot·Actor·bound Session source union, metadata version·1,024-byte bound·unique key,
operation kind·ID·reply route matrix, application envelope, Spot·Actor route, Actor lifecycle 5종, send-ready
destination 5종, completion terminal, relocation control phase와 Instance ready·cold route를 모두 해석한다.
공통 `relocation-control-v1.json` 7/7을 byte-for-byte decode→encode하고 각 truncation, unknown command와
closed phase negative를 focused test로 확인했다. Java core 604/604와 Kotlin 49/49 전체 회귀도 clean 통과했다.
다만 production User Spot retire는 아직 private `zlink.internal.spot.relocation.control.v1` request/reply
bridge의 stage·publish·abort·finalize 상태 기계에 연결되어 있고 raw runtime에는 command 30~41 독립
infrastructure ingress가 없다. Canonical command 40에는 현재 target stage가 요구하는 User Spot `stableType`과
Actor authority payload를 의도적으로 중복하지 않는다. Target은 command 40의 object identity와 expected
authority owner generation으로 Location Store current authority와 Active allocation을 exact read해 stable type,
membership과 authority payload를 얻어야 한다. Source accepted journal은 full frozen source·target owner fence를
아직 보존하지 않는다. 이 값을 추정하지 않고 contract·authority propagation을 먼저 닫아야 하므로 private wrapper 제거와
production transport 전환은 보류하며 `V11-M6C-JVM`과 `BLK-044`는 완료로 올리지 않는다.

Command 30 reservation handshake audit(2026-07-26)에서 authoritative schema의
`relocation-participant-resource-integrity`와 `relocation-reservation-handshake-integrity`, 공통
`21-location-runtime.ko.md`가 같은 계약을 정의함을 확인했다. Target offer는 non-zero message·byte capacity와
empty participant vector만 전송하고, source accept는 offered count를 zero로 두고 prepare의 exact participant
set과 allowance를 전송한다. Target은 allowance 합계가 offer 안인지 확인한 뒤 exact accepted vector를 command
41과 non-zero reservation generation으로 반환한다. 기존 shared command 30 fixture가 target offer에 participant
두 개를 넣은 것은 계약 오류였으므로 empty vector로 교정했다. C++·.NET·Node·JVM codec은 role별 offer·accept
shape를 encode와 decode 모두에서 fail-closed로 검증하도록 정렬했고 C++·.NET·JVM focused golden이 통과했다.
Node production handshake도 offer와 accept participant 방향은 정렬했지만 zero-work prepare에서 실제 available
capacity가 아닌 zero requirement를 offer로 사용하던 문제가 남아 target capacity projection에서 non-zero offer를
산정해야 한다. Stable type과 Actor authority payload는 새 wire field로 추가하지 않고 위 exact Location Store read로
복원한다. 따라서 이 audit은 fixture divergence를 닫지만 production raw dispatch와 accepted journal fence가 남은
`BLK-044` 자체를 완료로 바꾸지 않는다.

C++ accepted journal fence checkpoint(2026-07-26)에서 live command 21·22·24·25 header를 schema 순서의
exact 128-bit operation ID와 forwarding hop count로 정렬했다. Request의 correlation은 operation ID와 별도
field로 유지한다. Raw admission이 검증한 source node lifecycle과 wire에서 decode한 operation ID는 application
mailbox에 별도 field로 보존하며, `public_host_runtime_t::dispatch_ready`는 Spot·Actor record에 local lifecycle과
correlation을 조합한 operation ID를 더 이상 만들지 않는다. Source public request가 발급한 operation ID도 raw
transport에 그대로 전달한다.

`raw_stateful_dispatch_t`는 accepted Spot·Actor send/request를 queue에 넣기 전에 Location Store에서 source
MeshNode 또는 source Object authority, target Object authority와 두 owner lease를 exact read한다. RID에서 owner를
추정하지 않으며 owner·lease, node lifecycle, object·authority generation과 Active allocation이 하나라도 다르면
enqueue를 거부한다. 검증이 성공한 record는 typed writer로 frozen kind 5·6·9·10 canonical bytes를 만들어 turn에
저장하므로 maintenance envelope이 application payload만 보존하던 gap을 닫았다. `{77,88}` operation ID와 별도
reply route를 사용한 two-owner Actor request, Spot send, stale owner와 stale lifecycle negative, 실제 in-memory
Location Store exact-read를 포함한 focused M6B와 service-wire codec test가 통과했다. C++ M6A·M6B·M6C executable과
`zlink_framework` build도 같은 revision에서 통과했다.

C++ command 31·32 replay checkpoint(2026-07-26)에서 target participant별
`RelocationId`·target attempt generation·coordinator·source fence·object fence를 등록하고 command 31을 sequence
high-water의 다음 값으로만 stage하는 production coordinator를 추가했다. 같은 sequence의 재전송은 전체 command의
SHA-256 digest가 일치할 때만 중복으로 처리하고 ACK를 다시 전송한다. Gap, 순서가 바뀐 sequence, 다른 payload를 사용한
중복, stale coordinator·source·target lifecycle은 application dispatch 전에 거부한다. Frozen kind 5·6·9·10의
내부 source fence, Actor·Spot target ID, object·authority generation과 local target RID·lifecycle도 등록 정보와
exact 비교한다. 같은 source fence에서 서로 다른 sequence가 동일한 non-zero operation ID를 재사용하면 terminal
dedupe 충돌을 막기 위해 fail-closed로 거부한다. Target이 보존하는 command identity는 전체 frame이 아니라 sequence별
32-byte digest이며, 1 MiB payload를 포함한 3개 record를 stage한 뒤 retained digest가 96 bytes임을 확인했다.
Source coordinator는 command 32의 target RID·lifecycle과 coordinator fence를 확인하고 전송한 high-water 이하의
단조 증가 ACK만 반영한다. Duplicate ACK는 무시하고 전송 범위를 넘거나 stale lifecycle인 ACK는 거부한다. Focused
M6B test는 정상 3개 sequence, duplicate ACK 재전송, gap, conflicting duplicate, operation ID reuse, spoofed inner
target, stale coordinator와 1 MiB retention bound를 검증한다. Command 33·46 durable terminal의 전체 C++ 연결과
mixed-language process 검증은 아직 남아 있으므로 `BLK-044`는 유지한다.

C++ command 33·46 terminal owner checkpoint(2026-07-26)에서 command 33의 128-bit operation ID와
reply route를 서로 다른 field로 유지하고 `RelocationId`·target attempt·coordinator·participant sequence,
original request-source owner·lease·RID·lifecycle을 하나의 exact terminal identity로 등록했다. Source owner는
첫 terminal callback만 실행하고 동일 digest의 재전송에는 `alreadyTerminal` ACK를 반환하며, 같은 identity의
다른 payload는 dispatch 전에 거부한다. Target owner는 terminal과 optional application reply를 1,024 record와
64 MiB 기본 bound 안에서 보존하고 command 46 또는 exact source lease expiry 전까지 retry한다. ACK 유실 뒤
command 33 재전송, source의 terminal-once, duplicate ACK, exact source lease expiry cleanup과 24시간 tombstone
retention을 raw two-owner test와 실제 `public_host_runtime_t::dispatch_ready` vertical test에서 검증했다.
Frozen decoder summary는 canonical bytes를 변경하지 않고 Spot·Actor application payload를 target staging에
전달할 수 있게 보강했다. Service-wire codec, C++ M6A·M6B·M6C executable과 전체 `zlink_framework` build가
같은 worktree에서 통과했다.

C++ maintenance production wiring checkpoint(2026-07-26)에서 target preflight 결과가 실제
`RelocationId`, target RID·lifecycle, coordinator fence와 participant ID를 `maintenance_runtime_t`에
전달하도록 INTERNAL context를 확장했다. Maintenance seal에 저장된 canonical frozen record에서 request source의
owner·lease·RID·lifecycle, 128-bit `OperationId`와 별도 `ReplyRoute`를 그대로 읽어 command 31 record를 만든다.
Immutable root를 쓰고 checksum을 검증한 뒤 authority를 publish하기 전에 target preparation·Restore와 source
replay registration을 완료한다. Publish가 명확하게 실패하면 source와 target의 준비 상태를 함께 제거하고,
commit 뒤에는 source replay를 활성화하여 command 32 high-water ACK까지 재시도한다.

실제 raw MeshNode 두 개를 연결한 production vertical은 target Restore, exact command 31 staging과 command 32
ACK를 검증했다. `test_cpp_framework_service_wire_codec`, `test_cpp_framework_m6a_runtime`,
`test_cpp_framework_m6b_runtime`, `test_cpp_framework_m6c_runtime` 실행 파일은 모두 Exit 0으로 통과했고, 관련
C++ source·test·schema에 대한 `git diff --check`도 통과했다. 다만 command 33 pending terminal을 target
Restore 결과에서 자동 등록하는 연결, request-source terminal registration의 process restart 복구와
mixed-language process 검증은 남아 있다. 따라서 `V11-M6C-CPP`와 `BLK-044`는 계속 진행 상태로 유지한다.

M6C provider parity checkpoint(2026-07-23)에서 public authority CAS의 Missing expectation과
`NewObject` transition을 제거하고, Missing 생성은 generic reservation만 수행하도록 네 언어 계약과
provider를 맞췄다. C++은 descriptor·capacity fence·aggregate와 StoreRevision exhaustion 회귀에서
in-memory 10/10, Redis 16/16이 통과했고 외부 cross-language 환경 2건만 skip했다. .NET은 exact
MeshNode descriptor CAS, provider-owned capacity projection과 owner generation 전달을 보강해 Redis
44/44가 통과했고 같은 환경 의존 2건만 skip했다. JVM은 descriptor immutable limit, aggregate membership
index, reservation terminal idempotency와 fence 순서 독립성을 보강해 core 428/428, Kotlin 43/43,
실제 Redis 21/21이 통과했고 외부 harness 2건만 skip했다. Node는 exact descriptor, creation·relocation·
aggregate state machine과 capacity projection을 구현해 workspace typecheck, M6C 15/15와 실제 Redis
focused scenario 1/1이 통과했다. 네 lane 모두 `git diff --check`가 통과했다.

독립 audit에서 official Redis authority의 물리 schema가 C++·JVM의 per-authority HASH, .NET의
property별 global HASH, Node의 단일 serialized state로 달라 같은 Redis transaction domain을 공유할 수
없음을 확인했다. Node 방식은 mutation과 scan 비용이 전체 authority cardinality에 비례하므로 production
후보에서 제외한다. Per-authority와 global HASH의 장점을 조합한 hybrid schema까지 포함해
snapshot-consistent bounded scan, Redis Cluster same-slot, 1024-participant aggregate, migration과 hot-key
비용을 독립 review하고 있다. Schema version gate와 공통 physical fixture를 확정해 네 provider를 통일하기
전까지 `V11-M6C-*`는 `진행`, `V11-R5C`는 `대기`로 유지한다. Core·bindings와 Sample·E2E source는
변경하거나 실행하지 않았다.

Hybrid schema implementation checkpoint(2026-07-23)에서 공통 spec과 authority·MeshNode fixture에
literal hash tag, exact current HASH 13개 field, canonical `objectKind`, UTF-8 byte length-prefix capacity
bucket, owner lease 3-field HASH, descriptor·owner-token index, revision-prefixed history·tombstone와 durable
watermark scan을 고정했다. Descriptor immutable digest도 `CA-D54`의 canonical preimage와 fixture vector로
고정했다. Node는 workspace typecheck와 실제 Redis 9/9가 통과했다. JVM은 실제 Redis 27건 중 25건이
통과했고 외부 cross-language 환경 2건만 skip했다. C++은 exact descriptor admission과 aggregate
prepare·commit을 공통 physical schema에 맞췄고 실제 Redis 20건 중 18건이 통과했으며 외부 환경 2건만
skip했다. .NET은 alias·property-map과 legacy scan을 제거하고 schema `KEYS[19]`, scan `KEYS[20]`,
candidate row `KEYS[21...]`인 unified authority script를 직접 사용한다. Provider build는 warning·error
0이고 실제 Redis 49건 중 47건이 통과했으며 외부 harness 2건만 skip했다. 네 provider의 canonical digest
fixture, explicit Lua `KEYS`, exact field·bucket·index 검증과 `git diff --check`가 통과했다. 실제
shared-prefix writer→다른 언어 reader 검증이 끝나기 전까지 `V11-M6C-*`는 `진행`, `V11-R5C`는 `대기`로
유지한다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

Topology contract audit checkpoint(2026-07-23)에서 Automatic RouteMesh는 canonical RID가 작은 쪽만
connect를 시작하고 Manual 양방향 후보와 automatic 경합만 duplicate-pipe admission으로 정리하도록 공통
spec을 수정했다. ClientServer는 Client만 server별 `(RID, lifecycle generation)` intent를 만들고, classic
fanout은 Subscriber만 publisher별 intent를 만들며 automatic·manual subscriber 혼합 등록은 startup
configuration error로 고정했다. .NET planner의 연결 방향과 RouteMesh pairwise ordering은 계약과
일치했다. 다섯 언어 exact interface에는 RouteMesh initiator와 duplicate admission을 직접 고정했고,
ClientServer와 fanout의 비대칭 연결 방향도 공통 계약과 일치시켰다. Initial source audit에서는
C++·JVM·Node intent key의 lifecycle 누락, 세 언어 fanout 혼합 설정의 삭제·병합 처리와 .NET·Node
ClientServer public/runtime surface 누락을 확인했다. 아래 correction checkpoint가 앞의 두 gap을 닫으며,
dedicated ClientServer surface와 언어별 전체 regression은 계속 완료 조건으로 남는다.

Topology source correction checkpoint(2026-07-23)에서 C++ fanout connection intent를 Publisher RID와
lifecycle generation의 tuple로 바꾸고 같은 RID의 새 lifecycle에서 socket과 readiness fence를 교체했다.
C++ M6A runtime과 mixed-mode startup focused test가 통과했다. JVM planner도 role·RID 또는 endpoint와
lifecycle generation을 intent key로 사용하고 manual peer handover에서 stable peer identity로 이전 lifecycle을
교체한다. No-arg subscriber만 automatic mode를 선택하며 manual endpoint와 섞으면 startup error로 끝낸다.
Client와 fanout Subscriber는 descriptor를 게시하지 않고 Server와 Publisher만 게시하도록 role별 source를
분리했다. Descriptor lifecycle은 Store CAS generation과 분리한 nonzero token으로 유지한다. Java core,
Kotlin과 Redis의 Gradle 22 task가 통과했다. 별도 `contractTest` source 두 곳은 제거된
`renewOwnerLease(String, RoutingId, Duration)`를 호출하는 기존 signature gap으로 분리했다. Node planner와
SpotNode executor도 lifecycle generation을 intent와 disconnected 판정에 포함했고 Framework·NestJS builder가
automatic·manual subscriber 혼합을 양쪽 호출 순서에서 거부한다.

Dedicated ClientServer source checkpoint(2026-07-23)에서 .NET과 Node에 role을 정확히 한 번 선택하는
Client·Server builder를 추가하고 process-local unique ChannelName만 받는 send·request 표면을 연결했다.
.NET은 Client 전용 DEALER와 Server 전용 ROUTER, manual endpoint, typed handler dispatch, protocol error
reply와 request terminal 처리를 구현했다. 실제 두 runtime의 send와 request/reply, role 검증과 동일
RID·endpoint의 새 lifecycle에서 disconnect·reconnect readiness reset을 포함한 focused 5/5가 통과했고
Framework build는 warning·error 0이다. Node Framework·NestJS는 Client manual connect, Server bind·actual
port advertise, weight와 manual·group handler 조합을 구현했다. 전용 ClientServer descriptor·key·Store
contract와 in-memory owner lease·lifecycle·revision fence, Server Serving publish→Draining→remove,
automatic Client의 `(ChannelName, Server RID, lifecycle)` intent를 연결했다. Same endpoint/new lifecycle도
transport readiness를 reset하고 polling 오류는 Location runtime status·metric에 기록한다. Full build,
workspace typecheck, M6A runtime 6/6, 관련 contract 48/48와 추가 focused 7/7이 통과했다. 두 언어의 공식
Redis provider operation과 handshake·security admission 뒤 ready 승격, descriptor weight의 outbound 선택
연결은 계속 진행한다. Sample·E2E와 Core·bindings는 변경하거나 실행하지 않았다. 이 checkpoint는
`V11-M6A-DN`·`V11-M6A-NODE`를 완료로 판정하지 않는다.

ClientServer Redis provider checkpoint(2026-07-23)에서 .NET과 Node 공식 Location provider에
`ChannelName`과 Server RID로 만든 key, owner lease·lifecycle·revision·immutable field CAS, 만료된 owner의
takeover, exact owner token cleanup을 구현했다. Descriptor page는 channel별 정렬 index에서 필요한 candidate만
읽으며 기본 100개, 요청 최대 1000개와 encoded 4 MiB 제한을 함께 적용한다. Node의 전체 index
`SMEMBERS`와 page 생략 시 전체 반환은 제거했다. .NET은 `RemoveAllByOwnerAsync`를 owner ID 문자열이 아닌
`ZLinkLocationOwnerToken`으로 바꾸고 runtime, in-memory와 Redis provider가 같은 lease generation을
검증하도록 맞췄다. .NET provider build는 warning·error 0, 실제 Redis focused 3/3과 전체 50/50이
통과했으며 외부 cross-language harness 2건만 skip했다. Node build·workspace typecheck와 실제 Redis를
포함한 ClientServer runtime/provider 17/17이 통과했다.

독립 contract review에서 공통 Redis fixture의 `NormalizedEffectiveMaxMessageBytes`가 잘못된 것으로 판정했다.
이 값은 Location descriptor가 아니라 실제 physical connection의 service admission에서 교환하는 transport
결과다. 다섯 언어 exact descriptor를 확장하지 않고 fixture와 verifier에서 해당 field를 제거했으며, 정식
ClientServer spec에 이 책임 경계를 명시했다. Transport `ConnectionReady`만으로는 ChannelName, lifecycle과
security identity의 service admission을 증명할 수 없다. Framework service hello와 exact
admit/reject 뒤에만 ready target으로 승격하고 descriptor weight selector를 실제 outbound submit에 연결하는
작업이 남아 있다. 따라서 이 checkpoint도 `V11-M6A-DN`·`V11-M6A-NODE`를 완료로 판정하지 않는다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

.NET ClientServer connection completion checkpoint(2026-07-24)에서 automatic descriptor와 manual
endpoint마다 dedicated DEALER·monitor와 exact `hello`·`admit`·`reject`를 연결하고, positive-weight
`Serving` target의 deterministic weighted selection을 실제 send·request에 사용한다. 같은 Server
RID·lifecycle의 두 source는 하나의 physical connection을 공유하며 source 제거 순서와 lifecycle successor
admission을 reference·physical generation·attempt token으로 fence한다. Client와 Server는 5초 probe·15초
deadline, exact outstanding ACK, server-pushed higher-revision update와 malformed control 뒤 reconnect
readmission을 구현한다. TCP endpoint의 disconnect 뒤 즉시 reconnect하지 않고 25ms asynchronous handover를
사용하며 중복 reconnect와 disposal을 같은 tracked task로 직렬화한다.

이 검증에서 public DEALER `Request→Reply` 뒤 ROUTER가 보낸 unsolicited raw frame을 `Recv`가 받지 못하는
Core 결함을 재현했다. Request dispatcher는 frame을 internal queue로 옮겼지만 DEALER part receive는 native
pipe만 읽고 있었다. Dispatcher 설치 전에 queue를 준비하고 활성 dispatcher가 있으면 DEALER receive와
multipart continuation이 그 queue를 사용하도록 Core를 최소 수정했으며 .NET binding에 TCP
request·unsolicited receive·disconnect 회귀를 추가했다. Core request/reply 14/14와 관련 CTest 9/9,
binding request/reply 12/12, Framework ClientServer 12/12 clean exit, Framework build warning·error 0,
`git diff --check`가 통과했다. Framework 전체 unit 731건 중 runtime 724건이 통과했고 실패 7건은 기존
documentation regression의 ledger·E2E ID·public symbol 문자열 불일치다. Sample·E2E source는 변경하거나
실행하지 않았다.

Core request dispatcher refresh checkpoint(2026-07-24)에서 dispatcher 설치·종료의 atomic state,
DEALER generic·typed receive queue ownership, 하나의 `RCVTIMEO` deadline, multipart prefix 선검증과
logical send atomicity, readable pipe 재활성화와 source pipe 보존을 보완했다. Candidate
`.artifacts/v11/candidates/V11-M3-CORE-VERIFY-20260724-request-dispatch-v3-195608.json`은 235개 파일과
aggregate `ba5b66c…`를 봉인한다. Normal 80/80과 ASAN 80/80, Core removal 722 records·violations 0,
package tooling self-test와 `ROW-GATE`가 통과했고 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet의 독립
review에서 blocking finding은 0이다. 승인된 candidate를 isolated Release로 다시 만들어
`.artifacts/wsl/install/zlink-core/11.0.0`에 배포했으며 clean C consumer, runtime 11.0.0·SONAME 11,
service header 0을 확인했다. Core provenance SHA-256은 `2313515b…`, runtime SHA-256은 `f5e184af…`다.
같은 provenance로 `Systems.Zlink.11.0.0.nupkg`를 다시 만들고 isolated .NET consumer를 통과시켰다.
증거는 `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/request-dispatch-refresh-result-20260724.json`,
`.artifacts/v11/evidence/V11-R2/request-dispatch-refresh-result-20260724.json`,
`.artifacts/v11/evidence/V11-M3-CORE-PKG/request-dispatch-refresh-build-20260724.json`과
`.artifacts/v11/evidence/V11-M4-BIND-DN/request-dispatch-refresh-consumer-20260724.json`이 소유한다.
Full inventory check의 Framework semantic record 60개 추가·5개 제거 drift는 concurrent M6 구현에서
발생했으며 Core removal scope와 package candidate 밖이므로 M6 join inventory reconcile 입력으로 기록한다.
다른 M6A·public-contract parity 잔여가 있으므로 `V11-M6A-DN` 상태는 계속 `수정 진행`으로 유지한다.

Node ClientServer service admission checkpoint(2026-07-24)에서 automatic descriptor lifecycle마다
전용 DEALER와 monitor를 만들고 transport identity 확인 뒤 Framework `hello`와 exact `admit`·`reject`를
교환하도록 연결했다. Admission 전 connection은 outbound target에 포함하지 않으며, admitted
positive-weight target만 기존 weighted selector가 실제 send·request마다 선택한다. 같은 Server RID의 새
lifecycle이 ready가 되면 이전 lifecycle connection을 제거하고, 종료되었거나 오래된 connection의 event와
reply는 connection ID fence를 통과하지 못한다. Server의 reserved service control frame은 application
handler에 전달하지 않는다. Framework build·workspace typecheck, M6A runtime 6/6, ClientServer와 Redis
focused 20/20, changed-source ESLint와 `git diff --check`가 통과했다. Manual endpoint-only connection의
동일 service admission, liveness probe·ACK와 server-pushed descriptor update는 남아 있으므로
`V11-M6A-NODE`를 완료로 판정하지 않는다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

Node ClientServer connection completion checkpoint(2026-07-24)에서 manual endpoint마다 shared socket이
아닌 전용 DEALER와 monitor를 만들고 transport reconnect마다 새 `hello`·`admit`을 수행하도록 연결했다.
Automatic과 manual source가 같은 Server RID·lifecycle을 승인하면 하나의 ready target만 유지한다. 이전
physical connection의 늦은 admission callback은 physical token과 attempt token으로 차단하며 새 connection은
이전 callback 완료를 기다리지 않는다. Client와 Server는 application traffic과 무관하게 5초마다
`livenessProbe`를 보내고 current physical connection의 outstanding ID와 일치하는 `livenessAck`만 15초
deadline을 갱신한다. Server가 보낸 더 큰 revision의 descriptor update는 immutable identity가 일치할 때만
weight·state를 교체하며 reserved control은 application dispatch에 전달하지 않는다. Framework build,
workspace typecheck, changed-source ESLint, focused ClientServer 15/15, M6A 6/6과 `git diff --check`가
통과했다. 기존 `channel-client` 전체 runner의 실패는 admission 뒤 packet name을 요구하는 별도 current
contract migration으로 분리했고 timeout이 남긴 자식 process는 정리했다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로
`V11-M6A-NODE` 상태는 계속 `수정 진행`으로 유지한다.

Node ClientServer physical ownership follow-up checkpoint(2026-07-24)에서 automatic과 manual source가
같은 Server RID·lifecycle을 승인하면 ready target만 중복 제거하는 대신 실제 DEALER와 monitor 하나를
ref/alias로 공유하도록 보완했다. 어느 source를 먼저 제거해도 남은 alias가 physical connection과
discovery readiness를 유지하며 마지막 alias에서만 resource를 닫는다. Monitor termination은 physical
readiness를 먼저 제거한 뒤 source별 callback을 호출해 stale alias가 ready 상태를 다시 게시하지 못하게
한다. Server peer가 15초 deadline을 넘으면 ROUTER의 `disconnectPeer`로 physical connection을 닫고,
probe send가 backpressure를 반환해도 deadline 전에는 연결 종료로 판정하지 않으며 5초 뒤 같은 probe ID를
재전송한다. Workspace typecheck·build, focused ClientServer 16/16, M6A 6/6, changed-source ESLint와
`git diff --check`가 통과했다. 전체 Node gate는 병렬 Actor runtime의 별도 public method gap 2건에서
중단됐으며 이 focused 변경의 실패는 없다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지
않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로 `V11-M6A-NODE` 상태는 계속 `수정 진행`으로
유지한다.

Node dedicated fanout checkpoint(2026-07-24)에서 generic peer descriptor와 shared SUB 경로를 제거하고
exact `ZLinkFanoutPublisherDescriptor`·key·`ZLinkFanoutLocationStore`를 public contract, in-memory와 공식
Redis provider에 연결했다. Store는 lifecycle·revision·immutable identity·owner token fence, exact owner
cleanup과 bounded page를 구현하며 공식 fixture의 key·row와 일치한다. Publisher는 실제 bind endpoint를
전용 descriptor에 게시하고 5초마다 reserved beacon을 보낸다. Automatic subscriber는 같은 ChannelName의
publisher RID·lifecycle마다 SUB와 monitor 하나를 소유하며 first valid beacon/application payload 뒤에만
ready가 되고 15초 deadline은 해당 publisher connection만 교체한다. Reserved beacon은 application
dispatch에 전달하지 않는다. Protocol error와 deadline 뒤에는 새 physical SUB를 만들며 target identity와
controller attempt를 함께 fence해 늦은 termination이 successor를 닫거나 제거된 descriptor를 다시 열지
못한다. Manual endpoint별 SUB 경로는 유지하고 automatic과 함께 구성하면 startup에서 거부한다. Workspace
typecheck·build, focused fanout 7/7 clean exit, 실제 Redis fanout fixture/fence, ClientServer 16/16,
M6A 6/6, changed-source ESLint와 `git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를
변경하거나 실행하지 않았다. 다른 M6A·review 잔여가 있으므로 `V11-M6A-NODE` 상태는 계속
`수정 진행`으로 유지한다.

JVM ClientServer provider checkpoint(2026-07-24)에서 Java public source에 exact
`ZLinkClientServerServerDescriptor`, key와 선택 capability인 `ZLinkClientServerLocationStore`를 추가하고
공식 Redis provider가 이를 직접 구현하도록 연결했다. Redis row는 fixture와 같은 field 순서·상태 이름을
사용하며 ChannelName과 Server RID key, owner lease·lifecycle·revision·immutable field fence, channel별
정렬 index, 기본 100개·최대 1000개·encoded 4 MiB page와 exact remove를 구현한다.
`removeAllByOwner`는 Java·Kotlin public source, in-memory, runtime과 Redis에서 owner ID 문자열 대신 exact
`ZLinkLocationOwnerToken`을 사용하고 lease generation·expiry를 확인한다. 제거된 예전
`renewOwnerLease(String, RoutingId, Duration)`를 호출하던 JVM contract test도 current claim contract로
수정했다. Java core compile·test와 focused Location contract, Kotlin test가 통과했다. 실제 Redis는
ClientServer fixture·operation을 포함해 25/25가 통과했고 외부 cross-language harness 2건만 skip했다.
현재 JVM automatic ClientServer runtime은 아직 generic peer enumeration을 사용하므로 dedicated Store
publication·discovery와 service admission으로 교체하기 전에는 `V11-M6A-JVM`을 완료로 판정하지 않는다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

JVM ClientServer runtime checkpoint(2026-07-24)에서 generic `ZLinkPeerLocation` ClientServer loop를
제거하고 dedicated `ZLinkClientServerLocationStore` publication·bounded list·reconcile로 교체했다.
Automatic descriptor lifecycle과 manual endpoint마다 전용 DEALER를 만들고 exact
`hello`·`admit`·`reject` 뒤에만 ready target으로 승격한다. RID는 service wire의 opaque `bytes8`을
보존하며 human-readable `toString()`이 같은 서로 다른 RID도 raw hex connection key로 구분한다.
Positive-weight `Serving` target만 deterministic weighted selector가 실제 send·request에 사용하고, 같은
RID의 새 lifecycle은 admission 성공 전까지 이전 ready connection을 유지한 뒤 connection ID fence로 늦은
callback을 차단한다. Reserved control frame은 application dispatch에 전달하지 않는다. Host 내부
configuration dependency가 owner token supplier와 start·drain·stop lifecycle을 연결하며 application이
호출할 새 public ClientServer API나 service locator는 추가하지 않았다. JVM core assemble과 unit
436/436, 신규 focused 6/6, `JavaTargetContractGapTest`와 `git diff --check`가 통과했다.
`livenessProbe`·`livenessAck`, server-pushed descriptor update와 manual endpoint reconnect retry는 남아
있으므로 `V11-M6A-JVM`을 완료로 판정하지 않는다. Core·bindings와 Sample·E2E source는 변경하거나
실행하지 않았다.

JVM ClientServer connection completion checkpoint(2026-07-24)에서 manual endpoint와 automatic
descriptor의 physical lifecycle을 monitor event와 admission attempt token으로 fence했다. 같은
Server RID·lifecycle을 가리키는 두 source는 ref/alias로 실제 DEALER와 monitor 하나만 공유하며 어느
source를 먼저 제거해도 남은 source가 ready connection을 유지하고 마지막 alias에서만 resource를
닫는다. Client와 Server는 application traffic과 무관하게 5초 probe·15초 deadline을 적용한다.
Outstanding probe가 있으면 같은 ID를 재전송하고 current physical connection의 exact ACK만 deadline을
갱신한다. ROUTER의 peer deadline은 raw `disconnectRid`로 physical connection을 닫으며 send
backpressure는 deadline 전 disconnect 증거로 사용하지 않는다. Server-pushed higher-revision update는
immutable identity가 일치할 때만 적용하고 malformed·unsolicited control은 reconnect 뒤 새 admission을
요구한다. Production shared client fallback도 제거했다. JVM focused ClientServer 11/11, Java core
test·assemble과 Kotlin main·test compile, `git diff --check`가 통과했다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로
`V11-M6A-JVM` 상태는 계속 `수정 진행`으로 유지한다.

JVM dedicated fanout checkpoint(2026-07-24)에서 exact
`ZLinkFanoutPublisherDescriptor`와 key, 선택 capability인 `ZLinkFanoutLocationStore`를 public source에
추가하고 in-memory와 공식 Redis provider가 이를 구현하도록 연결했다. Publisher는 raw PUB가 실제 bind한
endpoint를 게시하며 automatic subscriber는 generic peer planner를 사용하지 않고 publisher
RID·lifecycle별 전용 SUB와 monitor를 만든다. Native connection과 최초 valid beacon 또는 application
record를 모두 확인한 뒤 ready로 판정하고, reserved beacon은 application handler에 전달하지 않는다.
5초 beacon·15초 deadline은 publisher connection별로 격리하며 manual publisher·subscriber 경로는
유지한다. 같은 connection ID의 이전 monitor callback은 current connection identity로 fence하고,
pending Store reconcile은 lifecycle epoch를 확인해 stop·close 뒤 connection을 다시 만들지 못한다.
Redis list는 server time과 exact owner ID·lease generation·expiry를 한 번에 검사해 만료되거나 해제된
owner의 descriptor를 반환하지 않는다. 독립 Codex 5.6 sol xhigh review에서 발견한 이 세 P1을 수정한 뒤
재리뷰가 `APPROVE`로 끝났다. JVM core 전체 test·assemble, Redis provider 전체 test, Kotlin main·test
compile과 `git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지
않았다. 다른 M6A·review 잔여가 있으므로 `V11-M6A-JVM` 상태는 계속 `수정 진행`으로 유지한다.

C++ ClientServer runtime checkpoint(2026-07-24)에서 exact descriptor·key·선택
`client_server_location_store_t`와 공식 Redis capability를 추가하고, generic ClientServer peer
publication·list를 dedicated Store runtime으로 교체했다. Discovery Server는 raw ROUTER가 bind한 실제
`last_endpoint`를 exact owner token으로 게시하고, Client는 descriptor lifecycle마다 raw DEALER와
`hello`·`admit` service admission, liveness와 ready fence를 소유한다. Positive-weight `Serving`
connection만 실제 send·request weighted selection에 사용하며 같은 RID의 새 lifecycle이 ready가 된 뒤
이전 connection을 제거한다. Service runtime state는 enum 수치를 cast하지 않고 explicit mapper를 사용해
`Retiring`·`Draining`을 wire `draining`으로 통일했고, 설정되지 않은 message bound는 nonzero
`0x7fffffff`로 정규화했다. Owner lease public surface와 runtime은 legacy owner-ID renew·remove·전체
lease list를 제거하고 `claim(ownerId)→renew(token)→removeAll(token)→release(token)` exact 경로만
사용한다. In-memory와 Redis provider, live reader와 contract test도 같은 계약으로 바꿨다. Focused
M6A raw·contract header·target contract·in-memory·location runtime·resolver·Redis 7/7, resolver
27/27, in-memory 10/10, location runtime 4/4가 통과했다. Redis는 20건이 통과하고 외부 환경 의존
2건만 skip했으며 endpoint·default admission·전체 state mapping 3/3과 `git diff --check`가 통과했다.
Exact 문서에 있는 dedicated fanout descriptor·Store·공식 Redis capability가 C++ source에 아직 없고
generic peer 재사용 여부를 다음 audit에서 닫아야 하므로 `V11-M6A-CPP`를 완료로 판정하지 않는다.
Runtime gate에서 제외한 기존
`framework/languages/cpp/e2e/ObservabilityOps/Server/main.cpp`도 제거된 `list_owner_leases()`를 호출하므로,
E2E source를 다시 활성화하는 단계에서 exact read 기반 관측 시나리오로 migration해야 한다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

C++ ClientServer readiness·selection correction checkpoint(2026-07-26)에서 request target 선택 대기를
`min(call timeout, 5초)`로 제한하고, selectable descriptor가 없으면 `RequestTargetNotFound`, descriptor는
있지만 pipe가 ready가 되지 않으면 `RouteNotConnected`, runtime 종료 중이면 `RuntimeShutdown`을 반환하도록
분류했다. Legacy `zlink_builder_t`에는 Location auto-connect host가 없으므로 discovery flag가 남은 explicit
server endpoint도 `channel_host_service_t`가 소유하고, auto-connect runtime이 활성화된 application에서는
dedicated ClientServer runtime만 같은 listener를 소유하도록 중복 bind를 막았다. Endpoint weight 선택은
연속 slot 방식에서 smooth weighted round-robin으로 바꿔 75:25와 100:100 모두 짧은 deterministic cycle에서
계약 비율을 만족한다. Resolver 기존 38/38, no-selectable-snapshot focused 1/1, channel messaging exit 0과
11.0.0 Core·C++ local package를 사용하는 install consumer가 통과했다. Core와 bindings source는 수정하지
않았다.

C++ dedicated fanout checkpoint(2026-07-24)에서 exact `fanout_publisher_descriptor_t`와 key,
`fanout_location_store_t`를 추가하고 in-memory와 공식 Redis provider가 new claim·renew·higher-lifecycle
takeover, immutable identity·revision fence, exact owner cleanup과 1..1000개·encoded 4 MiB bounded page를
구현하도록 연결했다. Redis page는 store time으로 `updated_at`을 교체한 최종 JSON 크기를 기준으로
4 MiB 경계를 계산하며 공식 fixture의 kind·key·field order·hash를 byte 단위로 검증한다. Runtime은
generic peer publication·reconcile을 사용하지 않고 publisher의 실제 bind endpoint를 전용 Store에
게시한다. Automatic subscriber는 publisher RID·lifecycle별 SUB와 monitor를 소유하고 first
beacon/application payload 뒤에만 ready가 되며, lifecycle 교체와 5초 beacon·15초 deadline은 다른
publisher의 connection 상태를 변경하지 않는다. Manual fanout 경로는 그대로 유지한다. 관련 6개 target
build와 CTest 6/6, 실제 Redis focused 4/4, in-memory 1000-row·4 MiB page, scoped
`git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지 않았다.
다른 M6A·review 잔여가 있으므로 `V11-M6A-CPP` 상태는 계속 `수정 진행`으로 유지한다.

ClientServer dual-role runtime checkpoint(2026-07-24)에서 C++·.NET·JVM·Node.js는 같은
ClientServer ChannelName에 Client와 Server를 각각 한 번 등록할 수 있도록 registration을 합쳤다.
같은 역할을 두 번 등록하면 startup configuration error로 실패하고, RouteMesh와 ClientServer가 같은
ChannelName을 사용해도 startup에서 거부한다. 같은 process의 Server는 local 우선이나 제외 없이
Ready·positive weight·non-draining 조건과 remote Server에 사용하는 weight 계산을 그대로 적용한다.
선택된 local Server도 handler를 직접 호출하지 않고 실제 DEALER→ROUTER admission·request/reply 경계를
통과한다. C++ focused 29/29, .NET 신규 focused 4/4, JVM focused 75/75·전체 core unit 462/462·Kotlin
compile, Node focused 30/30·M6A 6/6·TypeScript build·changed-source ESLint와 `git diff --check`가
통과했다. .NET 전체 unit의 기존 문서 7건과 ClientServer liveness 2건, JVM integration compile의 기존
Stream relay 1건과 Fanout publish 4건은 이 변경과 무관한 선행 API drift로 분리했다. 다섯 언어 exact
interface에 정의된 ClientServer monitoring public surface가 runtime에 아직 없어 `ClientAndServer`
계열 aggregate snapshot projection은 후속 M6 monitoring gap으로 유지한다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. 독립 Codex review에서 발견한 C++ keyed action의 state self-reference
cycle을 merged action value snapshot capture로 수정하고, contract trace에 새 callable helper가 들어가지
않도록 제거했다. Clean-first C++ contract headers·target contract·app host·store/location 4/4와
resolver 29/29가 다시 통과했으며 post-fix review는 `APPROVE`로 끝났다. 따라서 네 `V11-M6A-*` 행은
계속 `수정 진행`으로 유지한다.

ClientServer local-only correction checkpoint(2026-07-24)에서 같은 process의 Client와 Server
registration을 Location Store와 독립된 peer source로 고정했다. Framework는 bound local endpoint를 직접
얻지만 handler를 직접 호출하지 않고 기존 DEALER→ROUTER service admission과 transport를 그대로 사용한다.
Location Store도 있으면 local source와 automatic descriptor를 Server RID·lifecycle generation으로 합쳐
ready target 하나만 유지한다. 공통 spec과 다섯 언어 exact interface를 같은 의미로 수정했다. .NET은
local snapshot의 weight·drain revision을 client connection에 즉시 반영하며 local-only request와 weight
`0` contract 2/2가 통과했다. 전체 ClientServer 15/17이며 나머지 2건은 앞서 분리한 liveness timing
baseline이다. JVM은 local bound endpoint를 기존 admission 경로에 연결했고 configuration과 admission
focused suite가 통과했다. Managed production constructor test는 bound endpoint connect, Hello·Admit,
positive weight 선택, weight `0`·Draining 제외를 검증한다. 기존 manual integration test를 유지하면서
local-only positive·weight `0` 시나리오도 별도로 추가했다. Integration source set은 공유 worktree의
선행 SpotManager·Stream relay·Fanout API 전환 23개 compile error 때문에 실행 전에 중단됐다. Node.js는
local source를 항상 시작하고 automatic alias와 physical connection을
합치며 focused 31/31, M6A 6/6, build·typecheck·changed-source ESLint가 통과했다. C++도
`SameProcessClientServerUsesLocalReadyServerWithoutExternalStoreOrManualEndpoint`에서 Location Store와
manual endpoint 없이 local Ready Server를 정상 후보로 선택하고 기존 DEALER→ROUTER 경계로
request/reply하는 것을 검증했다. 이 test를 포함한 resolver 31/31, contract headers와 M6B runtime이
통과했다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

Documentation verifier checkpoint(2026-07-23)에서 service wire schema 37 commands·157 types와 186개
negative self-test는 통과했다. 최초 v11-first candidate는 Codex 5.6 sol xhigh review에서 C# semicolon-only
record span, non-export TypeScript brand, computed symbol property와 C++ default argument `{}`의 false
member를 발견해 세 차례 거부됐다. Extractor가 owner span, package-private symbol, 괄호 안 brace를 정확히
구분하도록 수정하고 stale override보다 exact baseline·target overload를 먼저 보존하는 refresh 순서를
추가했다. Parser·stale-first overload 회귀 6건을 추가한 최종 trace는 56 documents, 177 code blocks,
1690 declaration owners, 6496 members이며 reviewed-new member는
`4367 / 40a99f2735193c902343d653457da5c3e038647ca8c8ef6118e744dc7b113d30`, owner는
`1000 / ea545f28a93075107baf897c404706c5278c2bc541eebb15a761cc647fe0a046`이다.
Override 297개, intentional removal 1188개이며 unclassified·ambiguous·unknown/unowned는 모두 0이다.
`--write`·`--check`·`--self-test`, `verify-framework-doc-contracts.sh`와 `git diff --check`가 통과했다.
Codex 5.6 sol xhigh와 Claude Sonnet은 수정 뒤 독립 재계산과 refresh idempotence를 확인하고 모두
`ACCEPT`로 판정했다.

### 10.4 Runtime 완료 후 E2E·sample spec 확정과 단계별 활성화

`V11-M6-SCAFFOLD-ZERO` 뒤 공통 E2E와 sample spec을 amended contract와 실제 runtime에 맞춰 최종 확정한다.
Runtime과 spec 사이에 gap이 있으면 public contract를 구현 편의에 맞춰 축소하지 않고 runtime owner로 돌려보낸다.
영향받은 source와 registration은 impact manifest가 승인한 old→new hash로만 바꾸며, 영향받지 않은 항목은
diff 0을 유지한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-E2E-SPEC-FINAL` | Amended contract 기준 공통 E2E spec 확정 | E2E·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 대기 | scenario·negative·race·directional matrix와 approved hash·runtime owner 누락 0 | Relocation 용어와 readiness-first 이전, queue·timer 자동 복원, count·callback·byte gate, precommit abort 복구를 `RL-F11~RL-F14`와 `M75~M78`에 추가했다. Automatic-only `Retire`, exact peer Ready preflight, green→old disconnect ordering과 manual topology blocker를 Config 5 `RL-A4`와 Config 11 `OBS-C9`에 추가했다. 이전 review candidate hash는 이 계약 변경으로 효력을 잃었다. Config 1~14의 기존 contract finding과 새 scenario를 수정한 뒤 Codex `gpt-5.6-sol high` 단독 reviewer의 최종 독립 review, runtime owner·approved hash를 새로 기록해야 완료로 전환한다. |
| `V11-SAMPLE-SPEC-FINAL` | Amended contract 기준 공통 sample spec 확정 | sample·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 검토 완료·통합 gate 대기 | public 사용 흐름·역할·message·marker와 다섯 언어 owner 누락 0, 미리 지정한 NodeRid 기반 object 배치·routing 0 | 공통 sample 7종을 현재 spec과 다시 대조해 생성·topology·relocation 계약을 정렬했다. TicTacToe는 `SpotManager.Create`와 Framework 발급 RoomId, Object RouteMesh와 독립 API ClientServer를 사용한다. Bingo는 `bingo.matchmaking`의 level별 Instance Spot과 Redis reservation, `bingo.play`의 Room User Spot·Actor provider를 분리하며 모든 caller가 동일 settings로 `GetOrCreate` Ready를 기다린다. SupportChat은 생성 channel wrapper를 제거하고 Spot·Actor manager와 deferred Join을 직접 사용한다. ShoppingMall·GameQuest는 reservation winner만 Instance factory를 실행하고 모든 재활성화 command가 Instance intent를 포함하며 내부 handler·timer만 `Context.CloseAsync()`를 호출한다. DeliveryDispatch는 Object routing과 두 독립 ClientServer를 분리하고 Entry Spot `Yield`를 금지한다. ZoneWorld는 NodeRid를 Admin·Ops 관측에만 사용하고 cross-node owner pair, generation 보존과 bounded Message Follow를 검증한다. 공통 fixture는 모든 object-capable process의 role, ClientServer 경계와 Instance provider mesh를 기록한다. `gpt-5.6-sol high` 병렬 review 네 범위가 수정 뒤 CLEAN이고, relative link·fixture JSON·`git diff --check`가 통과했다. 전체 `DOC`는 sample 변경이 아니라 현재 worktree의 reviewed v11-first member set drift(`3062→3088`)에서 중단되므로 trace refresh와 다섯 언어 fixture consumer·sample 구현은 `V11-M7-SAMPLES`에서 닫는다. Node direct는 Admin·Ops 관리에만 허용하며 업무 object routing은 global ActorId·SpotId와 Location Store만 사용한다. |
| `V11-R5D` | Final E2E·sample spec 독립 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-E2E-SPEC-FINAL`, `V11-SAMPLE-SPEC-FINAL` | 대기 | public contract 일치, assertion 약화·coverage 손실·미분류 impact 0, 대체 scenario와 다섯 언어 sample parity의 I1·I2·I3 review clean | E2E pre-final 범위의 두 reviewer round와 post-fix focused review는 완료했다. 남은 round는 Codex `gpt-5.6-sol high` 단독 reviewer가 수행한다. `V11-E2E-SPEC-FINAL`의 runtime·approved hash와 `V11-SAMPLE-SPEC-FINAL`이 아직 대기이므로 이 combined final gate는 시작하지 않았다. |
| `V11-M6A-E2E` | Topology·liveness E2E 활성화와 gap 해소 | E2E·topology runtime lanes, `P-DELIVERY` | `V11-R5D` | 대기 | Config 3 `PS-F1~F5`, Config 5 `RL-E1~E5`, cross-MeshNode `ToChannel`(`M73`)와 amended placement scenario를 현재 candidate에서 실행, required skip·runtime gap 0 | — |
| `V11-M6B-E2E` | Stateful object E2E 활성화와 gap 해소 | E2E·stateful runtime lanes, `P-DELIVERY` | `V11-M6A-E2E` | 대기 | Spot·Actor·bound STREAM·Instance·remote create·global identity·cross-MeshNode `ToSpot`(`M72`)·stale owner scenario required skip·runtime gap 0 | — |
| `V11-M6C-E2E` | Maintenance·hosting E2E 활성화와 gap 해소 | E2E·maintenance runtime lanes, `P-DELIVERY` | `V11-M6B-E2E` | 수정 진행 | Zero-downtime patch(`M69`), same-version maintenance(`M70`), Shutdown closing(`M71`), no-target blocker(`M74`), automatic convergence·manual blocker(`RL-A4`, `OBS-C9`), aggregate relocation·crash recovery·remote fencing·hosting scenario required skip·runtime gap 0 | Contract는 automatic topology의 exact peer Ready 전 `Retiring` 금지, `ManualTopologyUnsupported=9`, green Ready→old disconnect 순서를 고정했다. Empty·source-only·all-draining descriptor snapshot은 replacement가 아니므로 `Blocked/TargetUnavailable`이어야 한다. .NET은 local manual RouteMesh·ClientServer client·fanout subscriber·storeless publisher blocker, exact RID+lifecycle generation+Core `Admitted` gate, Retiring publication rollback과 relocation detach→Draining→barrier→descriptor·lease release→disconnect ordering을 구현했다. Focused unit 44/44, 전체 unit 940/940, contract 65/65, service wire verifier와 `git diff --check`가 통과했다. Node.js도 local manual registration을 state·admission 변경 전에 차단하고 blocked Retire를 terminal로 저장하지 않아 이후 Shutdown을 허용한다. Live MeshNode descriptor에서 own row와 non-serving row를 제외하고 최소 한 개의 replacement와 exact RID·lifecycle generation의 Core Ready peer를 deadline까지 기다린 뒤 Retiring descriptor를 게시한다. Multi-Mesh publication이 일부만 성공하거나 응답이 유실되면 시도한 descriptor 전체를 `Serving`으로 되돌린다. Rollback을 확인하면 host state와 admission을 유지하고, 확인할 수 없으면 reversible block으로 오인하지 않고 force-stop한다. 그 뒤 relocation resource detach, Draining publication·accepted work barrier·owner cleanup 순서를 적용한다. Node build와 focused topology·drain contract 38/38이 통과했다. SpotActorTransfer 추가 audit에서 .NET ST-A1은 same-node 순서와 Relocation Store 0건 assertion을 연결했지만 same-node authority commit public evidence가 없어 실제 process가 실패했다. Message Follow 0건도 actor별 public observation이 없어 완료로 주장하지 않는다. Node ST-F4/F5는 registry에 연결했고 Client·ActorNode·Session build가 통과한다. ActorNode는 current Nest Spot·Entry Spot handler registration, User Spot Actor ID admission과 Mesh channel builder를 사용하며 internal Location row event 의존을 제거했다. Redis provider는 concurrent first-use를 단일 connection attempt로 직렬화하고 `isReady` 뒤 command를 제출한다. Location·Relocation prefix를 분리했고 Redis contract 6/6과 Node workspace build가 통과했다. SpotActorTransfer readiness도 제거된 Store row API 대신 public RouteMesh snapshot과 Location query로 전환했다. Automatic RouteMesh는 Redis의 live MeshNode descriptor를 polling하고 RID가 작은 process만 연결하며, raw backend의 `serving` peer를 public Ready로 투영한다. Backend·auto-connect focused 51/51이 통과했다. Fresh Redis container와 run별 namespace를 사용한 `ST-F4`의 `log/20260728-050651-3732925`와 `ST-F5`의 `log/20260728-050709-3734259`는 네 process의 Ready gate 뒤 scenario를 시작했으나 첫 User Spot reservation이 `conflict`로 실패했다. 이전 SpotId나 authority 잔존이 아니라 opaque provider가 owner lease·MeshNode descriptor는 공유하면서 authority·reservation은 inherited in-memory repository에 남긴 production 저장 경계 gap이다. 동일 intent retry는 current result로 수렴해야 하며 이 provider 연결을 완료한 뒤 public transport-delay fixture와 `location_committed` public evidence source를 연결한다. ST-F4/F5는 전환 대상으로 유지한다. C++ ST-A1은 same-node 순서·Relocation Store·Message Follow 0건, ST-B1은 authority commit-before-joined gate를 추가해 contract gate가 통과했지만 기존 host public API drift로 process 실행이 차단됐다. Java·Kotlin·C++와 실제 process rolling E2E의 `IMP-X17` gap도 남아 있다. |

Node.js의 최신 증거는 위 row에 기록한 초기 reservation blocker를 대체한다.
Authority·object generation·node capacity·Actor creation terminal을 독립 opaque key로 저장하고 관련
row만 bounded conditional batch로 갱신했다. Provider 전체 상태를 하나의 record로 저장하지 않는다.
Entry Spot과 User Spot lifecycle callback도 exact interface대로 실제 `TActor`를 전달한다. Node workspace
build와 Redis·backend·auto-connect focused test 57/57이 통과했다. 실제 `ST-F4`
`log/20260728-053114-4073197`은 Automatic RouteMesh Ready, shared User Spot reserve·commit, Actor
creation terminal과 deferred Join 등록까지 진행했다. 현재 remote Actor Join이
`actorRouteNotFound`로 끝나 Message Follow 등록 전에 중단된다. 따라서 다음 blocker는 cross-node
routed Join transport이며, 이후 public transport-delay fixture와 `location_committed` evidence를
연결해야 한다.

`.NET` Config 10 `ST-G5-SPOT-WIDE`에는 `ACTORS-10`, `ACTORS-100`
selector를 연결했다. Spot 하나와 모든 member Actor가 각각 64 KiB application state를 실제
relocation adapter에서 Capture·Restore하며, relocation 전후 각 member의 request·one-way,
accepted operation의 loss·duplicate·FIFO, 최종 owner와
`unit_kind=user_spot`, `execution_mode=spot_wide` interruption을 검증한다. `ACTORS-100`이 정식
1초 gate다. Client와 ActorNode Release build는 warning·error 0이다. 첫 process 실행
`logs/20260728-090027-2563625`은 scenario 시작 전 provider 병렬 변경의 Debug compile gap에서
중단됐다. Public call로 commit 전에 선택한 Spot physical route를 고정·release할 수 없어 Spot
Message Follow와 atomic publication 양쪽은 완료로 주장하지 않고 feature map blocker로 유지한다.

`V11-R5D`에서 Codex `gpt-5.6-sol high` 단독 reviewer가 candidate를 검토하고 finding을 §18 규칙으로
수렴한다. Reviewer가 clean을 기록하고 post-review `DOC`, `TRACE`, `AMENDMENT-IMPACT --mode finalized`가
통과하기 전에는 E2E source·registration을 변경하거나 `V11-M6A-E2E`를 시작하지 않는다.

각 E2E row는 approved scenario ID를 한 번에 하나만 활성화한다. 먼저 한 언어의 same-language cell에서
scenario를 실행하고, 실패 원인이 runtime gap이면 다음 scenario를 활성화하지 않는다. 해당 M6 language owner가
runtime을 수정하고 internal regression을 추가한 뒤 같은 scenario를 다시 실행한다. Same-language cell이
통과하면 필요한 cross-language directional cell을 실행하고, 모든 required cell과 evidence가 통과한 뒤에만
ledger에 해당 ID를 완료로 기록하고 다음 ID를 활성화한다. Scenario 묶음을 한꺼번에 활성화하거나 scenario·sample을
삭제하거나 assertion을 약화해 통과시키지 않는다. `V11-M6C-E2E`가 끝나기 전에는 full matrix, race·crash와
sample 실행을 시작하지 않는다.

## 11. M7 — 전체 correctness, E2E, sample과 smoke

M7은 service 의미가 Core 10.x 구현 없이 네 Framework runtime에서 성립하는지 검증한다. Oracle은 별도 process의
normalized trace 비교에만 사용하며 candidate process의 dependency가 될 수 없다.

M7은 M6 후반에 단계별 활성화를 마친 E2E를 full `4 x 4` matrix와 race·crash 조건으로 다시 검증한다. 그 결과가
통과한 뒤 sample을 처음 실행한다. 영향받은 E2E·sample source와 registration은 finalized impact manifest의
approved hash와 일치해야 하며, 영향받지 않은 항목은 baseline hash를 유지해야 한다. Required registration이나
대체 coverage가 줄어든 candidate는 실행 전에 실패한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M7-CONTRACT` | 다섯 amended public contract와 네 runtime contract test | contract·language lanes, `P-DELIVERY` | `V11-M6-SCAFFOLD-ZERO`, `V11-R4A` | 대기 | exact interface·source·test·public declaration 일치, required contract skipped 0 | — 관련 blocked issue: `BLK-036`. |
| `V11-M7-RACE-CRASH` | 동시성·수명·crash·recovery suite | race·recovery lanes, `P-DEEP` | `V11-M6C-E2E` | 대기 | §14.2 race, reservation·aggregate transfer phase crash, pause fencing과 resource cleanup 통과 | — |
| `V11-M7-E2E-4X4` | Final directional cross-language `4 x 4` E2E | E2E lane, `P-DELIVERY` | `V11-M6C-E2E`, `V11-M7-CONTRACT` | 대기 | approved source·scenario·registration hash 일치, required service·maintenance caller→server cell, negative frame와 crash scenario skipped 0 | — |
| `V11-M7-SAMPLES` | C++·.NET·Java·Kotlin·Node sample 활성화·검증 | sample·language lanes, `P-DELIVERY` | `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-SAMPLE-SPEC-FINAL` | 진행 | approved source·registration hash 일치, public API만 사용, 특정 NodeRid 기반 object 배치·routing 0, remote placement·typed JSON·hosting startup와 cleanup 통과 | 잘못 삭제된 언어 sample 75개와 수정된 Node sample support/runner 3개를 Git에서 복구했다. `.NET`은 TicTacToe project를 sample category 경계인 `Server/Api`·`Server/Play`에 배치하고 Bingo Matchmaking을 sample·global solution에 등록했다. Bingo Matchmaking Instance Spot과 play User Spot은 별도 Mesh를 사용한다. SupportChat·GameQuest·ShoppingMall·DeliveryDispatch는 manager creation·Instance Spot·global ID 계약에 맞췄다. ZoneWorld는 mutable Actor instance를 Spot에 보관하지 않고 global ActorId direct route, B5 generation·owner와 B6 Message Follow gate를 사용한다. TicTacToe·Bingo·SupportChat·GameQuest·ShoppingMall·DeliveryDispatch와 ZoneWorld 6개 project build는 warning·error 0이며 전체 Sample regression 125/125가 통과했다. 실제 sample process, C++·JVM·Node 구현과 전체 cleanup이 남아 완료로 바꾸지 않는다. |
| `V11-M7-SMOKE-FUNCTIONAL` | Liveness·maintenance functional smoke | integration lane, `P-DELIVERY` | `V11-M7-CONTRACT`, `V11-M7-E2E-4X4`, `V11-M7-SAMPLES` | 대기 | reconnect·Retire·Shutdown·transfer·recovery와 resource cleanup 통과 | — |
| `V11-M7-SMOKE-PERF` | Service·Core raw performance smoke-only | perf lane, `P-DELIVERY` | `V11-M7-CONTRACT`, `V11-M7-E2E-4X4`, `V11-M7-SAMPLES` | 대기 | 네 runtime·Kotlin consumer·Core raw 최소 workload, provenance와 cleanup 통과; 수치 판정 없음 | — |
| `V11-M7-JOIN` | 전체 correctness 합류 | release test coordinator, `P-DEEP` | `V11-M7-RACE-CRASH`, `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` | 대기 | contract·race·crash·`4 x 4`·sample·functional·perf smoke 누락과 skip 0 | — |

2026-07-26 sample object routing checkpoint에서
[Spot·Actor routing 통합 spec](../../framework/common/spec/18-object-routing.ko.md)을 기준으로 Java·Kotlin·Node
source를 점검했다. DeliveryDispatch의 courier별 고정 NodeRid 선택과 Entry Spot handle 경유 Actor 생성·전달을
제거하고, global ActorId의 `getOrCreate`와 direct Actor call로 바꿨다. Session bind는 같은 operation에서
Framework `getOrCreate` 결과로 받은 exact `ActorRef`를 그대로 사용하며 wire snapshot에서 `ActorRef`를 다시
조립하지 않는다. Java·Kotlin client의 서로 다른 owner node 배치 assertion과 Node의 expected NodeRid assertion도
제거했다. Node의 Bingo·TicTacToe·ZoneWorld·SupportChat·GameQuest에 남아 있던 `MeshName + ActorRef` direct call은
global ActorId call로 정렬했다. Node workspace `npm run typecheck`와 WSL `git diff --check`가 통과했다.
JVM sample compile은 이번 변경 위치까지 진행한 뒤 이미 남아 있던 channel builder·handler context·Spot lifecycle
contract migration 오류에서 실패했으므로 sample 완료 증거로 사용하지 않는다. C++ DeliveryDispatch와
ShoppingMall에는 `spot_handle_resolver_t`와 고정 `NodeRid`를 통한 object ensure 경로가 남아 있다. C++ public
Actor client가 아직 exact `actor_ref_t`를 요구하고 sample DI에서 target `actor_manager_t`·`spot_manager_t`를
사용하는 경로가 완성되지 않았으므로 해당 구현 contract를 먼저 정렬한 뒤 제거한다. 따라서
`V11-M7-SAMPLES`는 계속 `대기`다.

.NET sample 전수 감사에서는 TicTacToe·DeliveryDispatch·GameQuest·SupportChat에 object owner를 지정하는
NodeRid 시나리오가 없음을 확인했다. `SetRoutingIdPrefix`와 runtime이 반환한 current NodeRid의 즉시 관측은
배치 입력이 아니므로 유지한다. Bingo는 Session이 Ready Play node를 골라 Actor를 생성하고 첫 Actor owner를
room preferred owner와 Redis record에 저장하던 흐름을 제거했다. Session은 global Actor `GetOrCreate` 결과의
exact `ActorRef`를 bind하고, matching queue는 `RoomId`와 actor reservation만 저장하며 Spot `GetOrCreate`와
`JoinSpot`은 global `RoomId`를 사용한다. Client도 서로 다른 NodeRid를 성공 조건으로 검사하지 않는다. Bingo
restore는 성공했고 build는 Play project까지 진행했지만 sample 전체 build는 deferred Actor Join 등 이미 승인된 target contract를 아직
반영하지 않은 Play source에서 중단되므로 완료 증거로 세지 않는다. ShoppingMall은 owner instance를 요청·상태·
assertion에서 제거하고 같은 workflow ChannelName의 Ready server를 사용하도록 정렬했으며 focused regression
3/3과 CommerceApi·Shared build가 통과했다. ZoneWorld의 static zone→node mapping, node별 channel과 client owner
assertion도 제거했다. `ZoneTopology`는 네 business `ZoneId`만 보유하고 모든 ZoneNode가 같은 object type을
등록하며 global `SpotId`의 `GetOrCreate` claim으로 owner를 정한다. Gateway는 global `PlayerId`로 Actor를
`GetOrCreate`하고 같은 operation이 반환한 exact `ActorRef`를 session에 bind한다. `ActorsChannel`,
`ActorRefWire`, message의 `NodeRid`, zone→node lookup과 node별 Ops ChannelName은 삭제했다.
`ReportNodeStatusMsg` route context의 current `SourceNodeRid`는 topology 관측 결과를 연결하는 데만
사용하며 application routing, object owner 추론이나 application DTO에는 사용하지 않는다.
Maintenance는 desired state 기록 뒤 fanout으로 전달하고 diagnostics는 최신 node status report를 읽는다.
`JoinWorldRes`와 `ZoneChangedNotify` 및 client
assertion은 logical `ZoneId`만 검증한다. .NET ZoneWorld Gateway·Ops·Client build는 각각 warning·error 0으로
통과했다. ZoneNode focused build는 owner-neutral 변경 오류가 모두 해소된 뒤 target deferred Actor Join으로
바뀐 `IZLinkActorJoinSpotCall`에 이전 `Yield`·동기 result를 사용하는 2개 source의 8건에서만 실패한다.
이 gap은 sample별 우회 API를 추가하지 않고 approved deferred join 구현과 함께 정렬한다.
Client의 입장 owner 점검, target zone 점검과 진단도 `WatchNodesRes.Zones`에서 현재 owner 또는 ready node를
선택하도록 바꿨으며 `zone-node-1/2`의 고정 zone 소유를 success assertion으로 사용하지 않는다.
Shutdown·disconnect·maintenance restore·bot rejection과 fault report도 `WatchNodes` snapshot에서 target을
선택하거나 event가 현재 snapshot의 node를 가리키는지 검증한다. .NET ZoneWorld client scenario source의
`NodeIds.West/East` 참조는 0이다.

2026-07-28 .NET sample 최종 compile checkpoint에서 Actor·Spot
Create/GetOrCreate가 NodeRid나 placement override를 받는 경우는 0건이다. 물리 node를
직접 지정하는 호출은 ZoneWorld의 runtime-discovered node에 maintenance·diagnostics
명령을 보내는 운영 경로 한 곳만 남으며 object 배치에는 사용하지 않는다.
DeliveryDispatch도 global ActorId의 `GetOrCreate`·`SendToActor`와 Framework location
routing을 사용하도록 시나리오와 README를 정렬했다. SampleRegression 122/122,
전체 sample project 순차 build 47/47, Linux runner `bash -n` 8/8과
`git diff --check`가 통과했다. 이 증거로 .NET sample의 compile·placement 조건은
닫는다. 다른 언어 sample, actual-process 실행과 M7 선행 gate가 남아 있으므로
`V11-M7-SAMPLES` 상태는 `대기`로 유지한다.

같은 checkpoint의 C++ 후속 감사에서는 DeliveryDispatch가 `CourierId`로 고정 Entry Spot `NodeRid`를 계산해
Actor를 찾거나 생성하고 Dispatch가 같은 node의 Spot으로 제안을 보내던 경로를 제거했다. Courier Session은
global Actor `get_or_create` 결과의 exact `ActorRef`를 같은 operation에서 bind하며, Dispatch는
`actor_directory_t`가 global `CourierId`의 current ref를 찾은 뒤 `actor_client_t`로 one-way message를
보낸다. Client도 courier별 owner `NodeRid`를 성공 조건으로 비교하지 않는다. GameQuest·SupportChat과
DeliveryDispatch의 Actor Entry Spot join은 targetless `join_entry_spot(request)`로 바꿔 eligible node 선택을
Framework에 맡겼다. ShoppingMall은 `OrderId → workflow-a/b` hash, owner instance field, node-direct ensure와
서로 다른 owner assertion을 제거했다. 모든 workflow node는 하나의
`shoppingmall.order.workflow` ChannelName을 제공하고 API는 Channel select-one 뒤 global `OrderId` Spot을
resolve한다.

Node TicTacToe는 `PlayNodeInfo`·milestone payload·client assertion의 `SpotNodeRid`를 제거했다. Play RouteMesh는
고정 `play-node-*` 대신 allocated RID를 사용하고 manual peer에는 endpoint만 등록한다. Node workspace
`npm run typecheck`와 변경 path `git diff --check`가 통과했다. 당시 C++ sample focused build를 막았던
`V11-M6-OBJECT-CONTEXT-CPP`의 `spot_context_t` non-copyable ownership 오류는 이후 public copy를 복원하지
않고 production runtime에서 해소했다. 전체 Framework build와 관련 Context·resolver 회귀가 통과했지만
sample translation unit은 아직 다시 실행하지 않았으므로 sample 완료 증거로 사용하지 않는다.
후속 sample target 재실행은 runtime compile 단계를 통과하고 Bingo API translation unit에서 실제 sample
migration gap을 드러냈다. Proto에서 제거된 `ActorRefWire`를 conversion이 계속 참조하고,
`room_spot_discovery` 이름과 제거된 ClientServer `enable_client`·`enable_server` builder를 API·Play·Session이
사용한다. 이는 고정 NodeRid 제거 뒤 공통 Bingo의 shared RouteMesh `ApiChannel`·`PlayChannel` 구성으로 아직
C++ sample을 끝까지 정렬하지 않은 문제다. Sample 단계에서 .NET 기준 흐름을 이식하고 전체 C++ sample target을
fresh compile하기 전까지 `V11-M7-SAMPLES`는 `대기`다.
구 transport identity를 설정으로 받는 나머지 C++·JVM sample과 C++ TicTacToe Actor state의 current ref
중복 보관은 object placement 입력은 아니지만 공통 sample의 fixed RID 금지와 Context ownership에 맞추는 최종
parity 항목으로 남긴다. 따라서 `V11-M7-SAMPLES`는 계속 `대기`다.

2026-07-27 C++ public boundary 감사에서는 `pending_operation_t`와 lifecycle scope 생성 API가
application·provider 계약이 아니라 runtime queue와 dispatch 수명 관리에만 사용됨을 확인했다.
`pending_operation_t`는 public install header와 `framework.hpp`에서 제거해
`src/runtime/messaging`으로 옮겼고, `service_scope_kind_t`·`service_scope_t`·`create_scope`도
`src/runtime/configuration`에서만 유지한다. Bingo의 사용하지 않던 scope와 GameQuest의 notification
경로는 public Actor messaging으로 정리했다. 후속 수정에서 DeliveryDispatch의 Entry Spot 조회·전달은
global `actor_manager_t`·`actor_client_t`를 사용하고, STREAM에 종속된 Actor binding은 exact contract의
`stream_t::actors()`에서만 수행하도록 분리했다. Dispatch sweeper도 hosted service가 전달받은 root provider에서
singleton dependency를 시작 시점에 한 번 resolve하므로 application code의 `service_scope_t`·`create_scope`·
`session_actor_manager_t` 직접 의존이 남지 않는다. Dispatch·CourierActorNode·CustomerGateway 세 sample target과
C++ contract header test가 compile됐다. 설치 package는 Redis extension target이 export한 redis++·libuv를
조건부 `find_dependency`로 복원하고, clean install consumer가 `zlink::framework_locations_redis`를 실제 link해
header와 transitive dependency를 검증한다. 해당 install consumer도 통과했다. C++ 전체 sample parity 조건이
별도로 남아 있으므로 `V11-M7-SAMPLES`는 계속 `대기`다.

같은 public boundary 기준을 다섯 언어 exact interface에 적용했다. Exact 문서는 application이 호출하는
public API와 외부 provider가 구현해야 하는 최소 Store SPI만 소유한다. Location·Relocation Store의 closed
request·result·fence DTO는 provider method signature에 필요하므로 유지하고, authority mutation을 별도 public
interface로 다시 분리하지 않는다. 반면 routing allocation, native diagnostic, SpotNode polling, lifecycle scope,
pending operation과 provider 내부 row·watch helper는 exact 문서와 public export에서 제거하거나 runtime internals로
옮겼다. Public 선언이 없어진 .NET routing allocation·dispatch ownership·example 문서는 삭제했다.

Java·Kotlin·Node·C++ exact 문서에 남아 있던 service command 번호, backend·native socket, native timer handle,
raw C API·poller와 installed detail 설명도 제거했다. Exact 문서는 route switch, session close, logical timer 자동
복원처럼 application에서 관측 가능한 의미만 설명한다. 내부 command·ACK와 timer 복원 순서는 기존
`common/internals` 문서가 계속 소유한다. Public contract trace는 exact 문서 52개, code block 179개,
declaration owner 1,385개, member 4,965개이며 unclassified·ambiguous·unknown owner가 모두 0이다. Framework
문서 gate와 service wire 233개 negative self-test가 통과했다. Java의 runtime service implementation main
17개와 unit test 15개는 exact contract에 추가하지 않고 `runtime.internal.service` package로 함께 옮겼다.
동일 package에서만 사용하는 3개 type은 package-private로 축소했고, 여러 internal runtime package가 공유하는
12개 type만 internal namespace 안에서 public visibility를 유지한다. 이전 package 참조, Spring·Kotlin과 다른
Java module의 직접 참조는 0이다. Java core compile, core unit 576/576, target contract 16/16, Kotlin compile과
Spring starter compile이 통과했다. `m5FoundationTest`의 20개 compile error는 이동 전부터 존재한 source-set
include drift이며 `ActorRef`, `ZLinkBackendActorRef`, `ZLinkInternalMeshNode` 누락을 별도 gap으로 유지한다.

2026-07-27 C++ Context ownership checkpoint에서는 Spot·Actor factory가 exact Framework Context를 값으로
받아 application instance가 move ownership하도록 정렬했다. Close, activation failure와 host teardown은 한
internal detach 지점에서 application instance, timer와 node edge를 끊으므로 Context graph가 남지 않는다.
`zlink_framework`, contract header, sample parity와 Store·Location resolver target build가 통과했고, exact
identity·mismatch test 2/2와 host teardown 뒤 Spot destructor count 1을 확인했다. M7 caller migration에는
`test_cpp_framework_m6b_runtime`, SpotActorTransfer, RuntimeMonitoring, SpotService, ObservabilityOps,
GameQuest와 DeliveryDispatch에 남은 no-context 등록을 포함한다. 이는 이번 runtime·public header compile을
깨뜨리지 않지만 exact factory만 사용하는 최종 E2E·sample gate 전에는 제거해야 한다.

2026-07-26 placement audit 후속에서는 남아 있던 transport identity 설정과 caller-selected owner channel을
다섯 언어 sample source에서 제거했다. .NET·Java·Kotlin·C++ TicTacToe는 두 Play instance가 하나의 shared
Play ChannelName을 제공하며 API가 round-robin owner를 선택하지 않는다. .NET·Java·Kotlin·C++ GameQuest와
Java·Kotlin·C++ ShoppingMall도 business id hash로 mission/workflow instance channel을 선택하지 않고 shared
owner ChannelName과 global `SpotId`를 사용한다. C++ DeliveryDispatch·SupportChat·ShoppingMall과 Java·Kotlin·
Node DeliveryDispatch·SupportChat의 object RouteMesh는 fixed RID 대신 runtime allocation을 사용한다. C++
DeliveryDispatch runner의 `nodeRid` 입력은 process `instanceName`으로 바꿨고, Java Bingo PowerShell runner에
남아 있던 미사용 Play NodeRid 설정도 제거했다. Node GameQuest self-check는 특정 mission instance가 player
owner라는 조건을 제거하고 player별 owner lifecycle과 business projection만 검증한다.

source scan에서 indexed Play channel, `ownerChannel(playerId)`, `ownerMissionName`, `ownerWorkflowName`,
workflow instance channel, object config의 `customerSpotNodeRid`·`courierSessionSpotNodeRid`·`sessionSpotNodeRid`
참조는 0이다. 남은 explicit `setRoutingId`·`set_routing_id`는 ClientServer Channel server/client identity뿐이다.
Framework가 반환한 exact `ActorRef`의 current NodeRid를 같은 bind operation에 사용하는 경로는 유지한다.
Runtime monitoring에서 관측한 NodeRid를 application maintenance·diagnostics target으로 바꾸는 sample 경로는
제거했다.
Node workspace `npm run typecheck`와 변경 범위 `git diff --check`는 통과했다. 당시 C++ focused sample build를
막았던 `spot_runtime.cpp`의 non-copyable `spot_context_t` ownership 오류는 이후 production runtime에서
해소했고 전체 Framework build와 store/location resolver 41/41이 통과했다. Sample translation unit의
재검증은 runtime lane이 안정된 뒤 수행한다. JVM aggregate compile은 기존 channel builder·message context·Spot lifecycle migration 오류 21개 task에서
중단됐고, 재실행한 Java GameQuest GameApi는 `ZLinkSendContext`와 `enableClient`·`enableServer` 계약 차이 4건만
남았다. .NET focused build도 기존 `ZLinkStreamRuntimeManager.cs`의 `meshNode` named parameter 오류와 일부 missing
assets에서 중단됐다. 따라서 source placement audit은 완료했지만 sample 실행 증거가 없으므로
`V11-M7-SAMPLES`는 계속 `대기`다.

## 12. M8 — Framework POSD·DDD와 unused cleanup

M8은 Framework에 남은 pass-through adapter, 중복 state machine과 사용되지 않는 source·test·sample·generated
output·build·package 입력을 정리하고 production scaffold·placeholder·fake data의 잔여와 재발이 0인지 다시
검증한다. Core와 bindings 제거를 이 단계로 미루지 않는다. Production placeholder 제거도 M6 gate 이후로
미루지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M8-INVENTORY` | Framework 제거·unused 항목 전수 목록 | inventory lane, `P-SCAN` | `V11-M7-JOIN` | 대기 | 네 runtime·공통 protocol·test·sample·build·package 입력의 미분류 0 | — |
| `V11-M8-CLEAN-CPP` | C++ Framework POSD·DDD·unused·package tooling 정리 | C++ cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·compat·pass-through·unused target 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-DN` | .NET Framework POSD·DDD·unused·package tooling 정리 | .NET cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·reflection·compat·unused project 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-JVM` | JVM Framework POSD·DDD·unused·package tooling 정리 | JVM cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | Java·Kotlin scaffold·compat·unused source set 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — 관련 blocked issue: `BLK-018`. |
| `V11-M8-CLEAN-NODE` | Node Framework POSD·DDD·unused·package tooling 정리 | Node cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·compat·unused export·handle 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-COMMON` | Protocol·fixture·E2E·CI·manifest·Framework package runner 정리 | coordinator, `P-SCAN` | `V11-M8-INVENTORY` | 대기 | unused generated constant·fixture·scenario·shared manifest·CI 입력 0, persistent Framework package runner self-test 통과 | — |
| `V11-M8-CLEAN-JOIN` | Framework cleanup 합류 | cleanup coordinator, `P-DELIVERY` | `V11-M8-CLEAN-CPP`, `V11-M8-CLEAN-DN`, `V11-M8-CLEAN-JVM`, `V11-M8-CLEAN-NODE`, `V11-M8-CLEAN-COMMON` | 대기 | 네 clean build·contract test와 scaffold·service projection·oracle link/load 0 | — |
| `V11-R6` | Framework POSD·DDD·제거 범위 독립 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-M8-CLEAN-JOIN` | 대기 | domain boundary, dead code·compat helper·build·package 제거 review clean | — |

Cleanup은 문자열 검색만으로 완료하지 않는다. Export, include, build graph, generated 산출물, test discovery,
package manifest와 clean consumer로 사용 여부를 증명한다. 빈 wrapper, forwarding helper와 deprecated alias를
남기지 않는다.

## 13. M9 — Final local package, raw 재검증, E2E와 smoke

M9는 최종 Framework source와 review를 통과한 Core·bindings 조합으로 모든 증거를 다시 만든다. M9의 모든
package는 local/internal 위치에만 생성한다. 외부 registry에는 배포하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M9-RAW-FINAL` | Final Core·bindings raw capability 재검증 | Core·binding lanes, `P-DEEP` | `V11-R6` | 대기 | Core full raw suite·sanitizer, 네 public raw proof, ZMP heartbeat projection과 oracle link·load 0 재통과 | — |
| `V11-M9-PKG-CPP` | C++ final local/internal package 실행 | C++ package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Core·binding·Framework provenance, clean CMake consumer 통과 | — |
| `V11-M9-PKG-DN` | .NET final local/internal package 실행 | .NET package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Core·binding·Framework provenance, clean NuGet consumer 통과 | — |
| `V11-M9-PKG-JVM` | Java·Kotlin final local/internal package 실행 | JVM package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Maven Java·Kotlin JAR·metadata·Spring consumer 통과 | — |
| `V11-M9-PKG-NODE` | Node final local/internal package 실행 | Node package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, packed tgz, ESM·CJS·`.d.ts`·NestJS consumer 통과 | — |
| `V11-M9-E2E-4X4` | Final 방향성 cross-language matrix | E2E lane, `P-DELIVERY` | `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` | 대기 | required service·maintenance caller→server cell, negative frame와 crash scenario skipped 0 | — |
| `V11-M9-SMOKE-FUNCTIONAL` | Final liveness·maintenance functional smoke | integration lane, `P-DELIVERY` | `V11-M9-E2E-4X4` | 대기 | liveness, Retire·Shutdown·transfer·recovery·resource cleanup 통과 | — |
| `V11-M9-SMOKE-PERF` | Final service·Core raw perf smoke-only | perf lane, `P-DELIVERY` | `V11-M9-E2E-4X4` | 대기 | 네 runtime·Kotlin consumer·Core raw 최소 workload, provenance와 cleanup 통과; 수치 판정 없음 | — |
| `V11-M9-DOCS` | 정식 spec·guide·internals·migration·package 문서 확정 | docs lanes, `P-DEEP` | `V11-M9-SMOKE-FUNCTIONAL`, `V11-M9-SMOKE-PERF` | 대기 | 정식 문서와 public API·source·test·package 불일치 0 | Legacy 문서 정리 sub-gate에서 중복 `framework/spec`, 종료된 v10 plan·inventory와 active reference를 제거했다. Canonical spec formal 140개, link·anchor, public trace·migration inventory·impact manifest와 Node documentation 17/17, .NET documentation 15/15가 통과했다. Java documentation gate는 canonical E2E에 새로 추가된 미구현 scenario가 Java active fixture·feature map에 남아 있지 않은 gap을 검출하고, Node contract-surface도 exact catalog 대비 미구현 public declaration 67개를 검출한다. 두 항목은 경로 우회나 assertion 축소 없이 해당 M6 runtime·E2E parity 행에서 해소한 뒤 이 행을 완료한다. |
| `V11-R7` | 11.0 최종 통합 review; Codex 단독 reviewer | Codex review lane, `P-HIGH` | `V11-M6-DN-REFERENCE`, `V11-M9-DOCS`, `V11-CA-USER-SPOT-RUNTIME-JOIN`, `V11-CA-ACTOR-CREATE-REVIEW`, `V11-CA-ONE-WAY-RUNTIME-JOIN`, `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE`, `V11-M6-DEFERRED-JOIN-CPP`, `V11-M6-DEFERRED-JOIN-DN`, `V11-M6-DEFERRED-JOIN-JVM`, `V11-M6-DEFERRED-JOIN-NODE`, `V11-M6-OBJECT-CONTEXT-CPP`, `V11-M6-OBJECT-CONTEXT-DN`, `V11-M6-OBJECT-CONTEXT-JVM`, `V11-M6-OBJECT-CONTEXT-NODE`, `V11-M6-MESSAGE-CONTEXT-CPP`, `V11-M6-MESSAGE-CONTEXT-DN`, `V11-M6-MESSAGE-CONTEXT-JVM`, `V11-M6-MESSAGE-CONTEXT-NODE`, `V11-CA-SESSION-BINDING-DRAFT-RETIRE`, `V11-M6-SESSION-BINDING-CPP`, `V11-M6-SESSION-BINDING-DN`, `V11-M6-SESSION-BINDING-JVM`, `V11-M6-SESSION-BINDING-NODE` | 대기 | 전체 review clean 뒤 final raw·E2E·consumer·smoke 재통과 | — |

## 14. Required correctness와 functional smoke

Correctness gate는 최소한 다음 영역을 포함한다.

| 영역 | 필수 결과 |
|---|---|
| Topology | node direct, Channel select-one, ClientServer, fanout, same-name topology, route 없음과 reconnect |
| Request | reply, backpressure, timeout, cancellation, stale reply와 shutdown 경쟁의 terminal 결과 하나 |
| Spot·Actor | Entry·User·Instance, same-ID 동시 요청, 순차 turn, membership와 close 경쟁 |
| STREAM | bound session, disconnect, Actor 이동 barrier와 stale generation 거부 |
| Maintenance control | empty·blocked·multi-Mesh preflight, `Retire`·`Shutdown`, deadline과 waiter cancellation |
| Maintenance transfer | Actor·User Spot·Instance Snapshot·Recreate, Entry Spot callback, timer·session ordering |
| Recovery | commit 전후 crash, store outage, process pause fencing, target replacement와 orphan cleanup |
| Liveness | store-backed·manual SIGKILL·blackhole·pause·reconnect, frame·timer와 cleanup |
| Protocol | malformed, unknown version·field, oversize, stale owner·generation과 partial-order 거부 |
| Hosting | C++ host, ASP.NET, Spring과 NestJS bounded shutdown |
| Interop | C++·.NET·JVM·Node.js의 방향이 있는 `4 x 4` caller→server 필수 조합 |

### 14.1 Stateful·maintenance E2E catalog

아래 ID는 공통 E2E catalog에서 같은 이름으로 구현한다. 언어별 runner가 임의로 ID를 바꾸거나 일부 항목을
단위 test 통과로 대신하지 않는다.

이 표는 amended formal spec을 반영한 pre-final catalog다. Runtime 구현 중 ID를 삭제하거나 assertion을
약화하지 않는다. `V11-CA-IMPACT`가 각 ID를 분류하고 `V11-E2E-SPEC-FINAL`이 runtime gap을 확인한 뒤 완료 조건과
approved hash를 확정한다. Global identity, remote placement, Instance Spot fluent activation과 Relocation vocabulary로
바뀐 항목은 이전 Mesh별 identity·local-only create·address wrapper·이전 payload vocabulary 가정을 다시 도입하지 않는다.

| ID | Scenario | 완료 조건 |
|---|---|---|
| `V11-E2E-M01` | Empty host retire | 모든 local component가 `Stopped`가 되고 host-owned resource가 정리됨 |
| `V11-E2E-M02` | Preflight blocked | admission과 descriptor state 변화 없이 `Blocked` 반환 |
| `V11-E2E-M03` | Mixed-version target | source보다 낮은 application version, stable type capability·Snapshot adapter capability가 맞지 않는 target 제외 |
| `V11-E2E-M04` | Actor Snapshot transfer | application-owned opaque state bytes, accepted journal과 Actor generation 유지 |
| `V11-E2E-M05` | Actor Recreate relocation | Application state와 Restore 없이 accepted journal·recovery payload를 Relocation Store에 먼저 보존하고, Location Store가 participant inventory·target reservation·positive capacity를 검증한 뒤 target factory·journal staging·aggregate commit·replay 순서로 복구를 완료함 |
| `V11-E2E-M06` | Instance Spot transfer | global Spot ID, object·authority owner generation과 순차 request 유지 |
| `V11-E2E-M07` | Concurrent cold Instance request | 100개 caller가 owner, activation과 factory 하나로 수렴 |
| `V11-E2E-M08` | User Spot aggregate transfer | Actor 100개를 포함한 SpotWide User Spot을 하나의 service unit으로 준비하고 target Spot·Actor restore와 전체 count·digest 검증 뒤 aggregate authority의 owner·generation·inventory root를 한 CAS로 전환함 |
| `V11-E2E-M09` | Source crash before commit | `Preparing` 또는 unlinked Transfer Put 중 crash는 transfer를 fenced abort하고 원 request가 일반 connection failure·timeout terminal을 따르며 hidden replay를 수행하지 않음. `Captured`·`Prepared` crash는 Location authority에 연결된 immutable Transfer root와 accepted journal을 사용해 current target commit 또는 replacement로 수렴함 |
| `V11-E2E-M10` | Source crash after commit | target activation과 source cleanup recovery 재개 |
| `V11-E2E-M11` | Store outage | seal 전 `Blocked`, seal 뒤 bounded `ForceStopped` reason 일치 |
| `V11-E2E-M12` | Request terminal race | reply·timeout·cancellation·transfer·shutdown 중 결과 하나 |
| `V11-E2E-M13` | Timer race | 이전 owner와 generation의 tick이 application callback에 도달하지 않음 |
| `V11-E2E-M14` | Full rolling operation | accepted request 완료, moving 결과 뒤 application의 명시적 retry 성공 |
| `V11-E2E-M15` | Maintenance wave | source wave node 사이의 반복 transfer 0건 |
| `V11-E2E-M16` | Manual·automatic classic fanout | publisher 자동 게시와 subscriber 자동 발견, manual mode 회귀 통과 |
| `V11-E2E-M17` | Spot Logical Multicast | Draining target과 stale generation 제외, transaction 시작 뒤 target별 결과·monitoring 집계 없음 |
| `V11-E2E-M18` | Remote owner fencing | 지연된 이전 owner message·timer·phase update가 새 owner에 적용되지 않음 |
| `V11-E2E-M19` | SIGTERM integration | hosting lifecycle이 bounded `Shutdown`을 사용하고 `Blocked`를 반환하지 않음 |
| `V11-E2E-M20` | Multi-Mesh host | host barrier 하나로 all-or-none preflight와 seal 수행 |
| `V11-E2E-M21` | Retire blocked then shutdown | 차단된 `Retire` 뒤 `Shutdown`이 shared deadline 계약으로 terminal 완료 |
| `V11-E2E-M22` | Process pause fencing | lease-derived monotonic deadline 뒤 stale admission과 CAS 거부 |
| `V11-E2E-M23` | Prepared·Committed target replacement | Current coordinator fence가 stable transfer ID의 immutable Transfer manifest로 replacement reservation ACK를 얻고 target·attempt generation·reservation을 한 CAS로 교체해 successor 하나로 activation을 수렴함. Transfer root·journal·terminal key는 attempt 교체에도 바뀌지 않음 |
| `V11-E2E-M24` | Close versus transfer | Instance `Close`와 `Prepared` CAS 승자에 맞는 결과, hidden retry 0건 |
| `V11-E2E-M25` | Cold Instance one-way and request | Global Spot ID의 Instance intent가 eligible target을 선택하고 first message·operation identity·reply correlation·deadline을 activation envelope에 포함해 target transport에 제출함. Source claim은 0건이고 target CAS winner가 `Creating` reservation을 만든다. One-way는 envelope outbound admission에서 완료되고 request만 target activation·handler terminal을 기다림 |
| `V11-E2E-M26` | STREAM binding atomicity | bind·rebind·unbind 실패 시 기존 binding 유지와 terminal result 하나 |
| `V11-E2E-M27` | Observer terminal lane | 일반 event admission을 닫은 뒤 final snapshot·terminal event가 한 번 전달됨 |
| `V11-E2E-M28` | Completion reserve saturation | infrastructure queue 포화에서도 accepted request가 terminal 완료됨 |
| `V11-E2E-M29` | Cross-topology ChannelName call | Spot이 다른 RouteMesh·ClientServer Channel을 호출하고 completion은 원래 owner로 복귀 |
| `V11-E2E-M30` | ChannelName collision | 서로 다른 physical topology의 같은 process-local ChannelName이 startup에서 거부됨 |
| `V11-E2E-M31` | Global object identity collision | 서로 다른 initial Mesh intent로 같은 Actor ID·Spot ID를 동시에 create해도 provider 전체에서 하나의 authority와 current owner로 수렴하고 direct messaging과 manager `Find`는 MeshName 없이 같은 object를 반환함 |
| `V11-E2E-M32` | Cross-language authority interop | 한 runtime이 `authority-key-v1`과 authority payload를 기록하고 다른 runtime이 steady·cold activation·maintenance state를 같은 logical object로 resolve·recovery |
| `V11-E2E-M33` | Transfer preflight growth | Preflight 뒤 reversible seal까지 수락된 work를 exact inventory에 포함하고, seal 뒤 final reservation ACK와 모든 `Prepared` CAS를 완료한 뒤에만 `Draining`을 게시해 continuity를 잃지 않음 |
| `V11-E2E-M34` | Transfer payload leased retention | Long capture 중 staged manifest tree를 추적하고 `Captured`·`Prepared` CAS 직전에 모든 component의 remaining lease를 12시간보다 길게 verify·renew하며 partial renew failure는 root를 Location authority에 연결하지 않음. Provider 기준 시각을 orphan TTL 이상 이동하면 current reference는 renew로 유지되고 orphan만 제거됨. Published reference의 payload가 24시간 연속 Store 불가 뒤 영구 유실되면 non-retriable `TransferDataLost`, 진행 중 `Retire`는 detail에 해당 오류를 보존한 `ForceStopped/TransferFailed`로 종료 |
| `V11-E2E-M35` | Actor owner ABA fence | Actor가 A→B→A로 이전된 뒤 최초 A owner의 지연 message·journal·forward record가 새 A owner에 적용되지 않음 |
| `V11-E2E-M36` | Cross-language terminal failure | 모든 stable failure code를 네 runtime 조합으로 reply·completion·relay해 같은 typed terminal result로 변환하고 unknown code는 protocol error로 수렴 |
| `V11-E2E-M37` | Authority generation atomicity | Provider-domain global object·authority-owner·store revision counters와 Missing/Found expectation으로 create·owner change·preserve·delete·재생성을 원자 처리하고 concurrent winner만 값을 소비하며 per-key version·tombstone을 남기지 않음 |
| `V11-E2E-M38` | ClientServer command isolation | ClientServer role·direction allowlist 밖의 RouteMesh·Spot·Actor·transfer·server-originated application command가 handler·authority에 도달하지 않고 offending connection만 protocol error로 종료 |
| `V11-E2E-M39` | Global Actor creation parity | 다섯 public 언어가 global Actor ID·explicit stable type과 optional initial Mesh intent로 create·GetOrCreate를 수행하고 manager `Find`는 existing-only이며 caller가 physical owner를 선택하거나 hidden create를 시작하지 않음 |
| `V11-E2E-M40` | Spot kind atomic collision | 같은 global Spot ID의 Entry·User create와 Instance fluent cold activation이 하나의 authority CAS에서 경쟁해 kind 하나만 성공하고 close 뒤 다른 kind로 재생성해도 object generation을 재사용하지 않음 |
| `V11-E2E-M41` | Owner lease stale token | Process pause와 lease expiry 뒤 이전 token의 renew·release가 거부되고 같은 owner ID의 새 claim도 global lease generation이 증가한 새 token을 사용해 이전 descriptor·ready routing을 복구하지 않음 |
| `V11-E2E-M42` | Durable authority after owner lease expiry | Owner lease가 만료되어도 `Creating`·`Committed` authority의 phase·Transfer reference·replay cursor·object generation이 유지되고 successor의 `new_owner` CAS만 recovery를 계속함 |
| `V11-E2E-M43` | Bound-session transfer barrier | Session owner ID·host lease generation·node RID·lifecycle, session RID·binding generation을 보존하고 binding ingress를 reversible seal해 last accepted sequence를 확정한 뒤 source journal·target replay·current host lease를 다시 확인한 session owner route publication ACK와 unseal로 transfer 경계의 packet을 순서대로 한 번 처리함. Abort는 durable Aborted CAS 뒤 source-route ACK와 steady normalization을 마친 후에만 source admission을 다시 엶 |
| `V11-E2E-M44` | Snapshot-consistent authority recovery scan | Opaque watermark 시점의 authority key incarnation을 pages 전체에서 정확히 한 번 열거하고 exact Read+CAS로 재검증하며 initial scan 전 Serving을 게시하지 않고 post-watermark mutation은 다음 scan에서 복구함 |
| `V11-E2E-M45` | Negotiated message bound | 양쪽 32 MiB 설정에서 17 MiB payload가 성공하고 sender·receiver 설정이 다르면 작은 effective bound를 allocation 전에 적용하며 `MaxMessageSize=0`이 숨은 16 MiB 상한 없이 normalize됨 |
| `V11-E2E-M46` | Chunked Transfer manifest | 64 MiB보다 큰 journal·Snapshot을 ordered immutable chunks와 Transfer root manifest로 보존·renew·streaming replay하고 Location authority CAS 전 orphan과 reference 제거 뒤 cleanup을 안전하게 처리함 |
| `V11-E2E-M47` | Host lease and routing slots | 한 host가 process lifecycle owner lease를 한 번 claim하고 같은 token을 여러 allocation group에 전달해 component별 slot을 얻으며 stale token과 rollback이 slot·lease를 누출하지 않음 |
| `V11-E2E-M48` | Framework JSON v1 interop | Typed application message의 property·enum·integer·bytes·null 의미를 다섯 언어가 같이 복원하고 unknown field는 무시하되 duplicate·missing required field는 거부함. Transfer adapter의 Snapshot bytes는 application-owned opaque payload로 유지하며 Framework JSON이나 state contract ID를 적용하지 않음 |
| `V11-E2E-M49` | Paused session-owner fence | Transport I/O가 유지되어도 만료된 session owner host lease의 seal·route ACK가 binding route와 unseal을 바꾸지 않고 successor의 exact owner token만 적용됨 |
| `V11-E2E-M50` | Transfer target reservation fence | Paused target의 만료된 host lease에 묶인 offer·reservation ACK·activation을 거부하고 current target owner token을 검증한 replacement reservation 하나로 수렴함 |
| `V11-E2E-M51` | Owner-token bulk cleanup | 같은 owner ID를 다시 claim한 뒤 이전 owner token의 bulk cleanup이 새 descriptor·authority를 삭제하지 않고 exact lease generation만 정리함 |
| `V11-E2E-M52` | Activated-to-Completed admission seal | Target restore·replay와 session route staging 뒤에도 authenticated source cleanup 또는 exact source lease-expiry fence와 authority `Completed` 전에는 application/session ingress를 열지 않고, steady normalization 뒤에만 `Ready`·unseal·Transfer payload release를 수행함 |
| `V11-E2E-M53` | Bounded descriptor reconcile | MeshName·ChannelName descriptor를 최대 1,000개 page와 opaque continuation으로 읽고 scope change stamp가 처음과 끝에 같을 때만 full snapshot을 적용하며 routing group 상한을 검증함 |
| `V11-E2E-M54` | Compact authority completion | Location authority row를 1 MiB compact metadata로 제한하고 full journal·reply completion은 Transfer chunks만 소유하며 late completion root CAS와 recovery scan 4 MiB page cap에서 누락·중복 없이 수렴함 |
| `V11-E2E-M55` | Admitted descriptor update fence | Current connection의 더 높은 revision에서 제한된 mutable field만 적용하고 same-revision conflict·immutable identity/capability mutation은 protocol error, lower revision은 stale ignore로 처리함 |
| `V11-E2E-M56` | Transferred reply ACK barrier | Lease-backed Node·Channel·Spot·Actor·Instance·bound-session accepted request의 exact original reply route를 Transfer payload에 보존하고 stable transfer·operation ID로 relay를 재전송하며 authenticated terminal ACK 또는 exact request-source host lease expiry 전에는 `Completed`·payload release를 허용하지 않음. Operation ID와 connection close를 reply route·terminal 증거로 대신하지 않음 |
| `V11-E2E-M57` | Instance claim generation | Generic Reserve의 Missing→Pending `Creating` claim이 nonzero object·authority owner generation을 한 번 발급하고 exact Commit의 `Ready`·`Closing`·failure completion이 같은 generation을 유지하며 Commit이 새 generation을 만들지 않음 |
| `V11-E2E-M58` | Descriptor registration bound | Fully derived descriptor 1 MiB, stable type·Snapshot adapter·placement capability vector 1,024 상한을 startup에서 원자 검증하고 초과 시 partial publish 없이 모든 언어가 같은 configuration failure로 종료함 |
| `V11-E2E-M59` | Store grace and stateful owner fence | Store failure grace 동안 마지막 stable discovery set과 ready transport는 유지하지만 owner lease의 monotonic deadline 뒤 stateful message·timer·factory completion·CAS admission은 닫히며, 복구 뒤 stable page set과 exact owner token을 다시 확인한 후에만 diff와 admission을 재개함 |
| `V11-E2E-M60` | Provider ambiguous completion | Location authority CAS·Relocation Put invocation 뒤 cancellation·timeout·response loss를 no-commit으로 가정하지 않고 exact read·expected fence 또는 content reference로 reconcile하며 unlinked Put은 orphan cleanup으로 수렴함. Async input은 완료까지 불변이고 success result bytes는 provider 재사용과 mutable adapter 뒤에도 stable함 |
| `V11-E2E-M61` | Connection-bound maintenance source | Manual/no-Store peer의 request와 one-way send를 모두 `Captured` 전에 terminal 완료하고 durable journal·relay barrier에 넣지 않으며, 하나라도 deadline 내 완료하지 못하면 pre-`Captured` abort와 `Blocked/DeadlineExceeded`로 admission을 복원함. Durable journal은 lease-backed accepted work만 기록하고 manual lifecycle token은 opaque equality와 current connection handover로 fence하며 numeric ordering을 사용하지 않음 |
| `V11-E2E-M62` | Cold activation crash boundary | Source가 activation envelope admission 전에 종료되면 authority와 factory가 0건임. Target이 envelope를 수락하고 `Creating` reservation을 획득한 뒤 종료되면 exact target owner의 initial·background bounded scan이 durable first-message reference와 같은 object·authority generation의 factory·`Ready` barrier를 idempotent하게 재개함 |
| `V11-E2E-M63` | Cold activation failure release | Factory·initialize·Ready 실패가 local barrier를 failed·sealed로 유지하고 request terminal·one-way drop을 한 번 확정한 뒤 exact Store version·object generation·authority owner generation·owner lease의 fenced delete로 row를 제거함. 응답 손실은 exact read로 수렴하며 Missing 뒤 다음 caller만 새 generation으로 activation하고 기존 registry는 hidden rerun하지 않음 |
| `V11-E2E-M64` | Store 없는 object role 거부 | Location Store 없이 Object Client 또는 Object Server 역할·factory·manager를 구성하면 socket bind 전에 startup configuration error로 끝나며 hidden process-local object runtime을 만들지 않음. Store 없는 manual `None` 역할은 explicit peer의 Node·Channel direct만 제공하고 Actor·Spot manager, factory, placement와 session Actor dispatch를 노출하지 않음 |
| `V11-E2E-M65` | User Spot close with membership | Current Actor membership이 있는 User Spot의 normal close는 `false`를 반환하고 admission·authority·membership과 closing callback을 바꾸지 않으며, hidden move·destroy 없이 explicit leave/destroy 뒤 close만 한 번 성공함. Host Shutdown·Retire는 Actor barrier를 Spot cleanup보다 먼저 완료함 |
| `V11-E2E-M66` | Store-backed local object publication barrier | Generic Reserve가 User Spot·Actor의 final generation과 Pending `Creating` row를 발급한 뒤 factory·initialize와 같은 owner fence의 exact Commit이 Active `Ready`를 게시할 때까지 resolver·remote messaging이 object를 공개하지 않음. Actor는 initial Entry membership도 barrier 안에서 완료함. Failure는 exact Abort와 read reconcile로 Pending row·scope·membership을 제거하며 다음 caller만 새 generation으로 create함 |
| `V11-E2E-M67` | Retire preflight deadline | Seal 전 preflight가 host deadline을 넘으면 `Blocked/DeadlineExceeded`와 host·descriptor·admission unchanged로 끝나고, seal 뒤 teardown이 deadline을 넘긴 경우에만 `ForceStopped/DeadlineExceeded`로 bounded cleanup을 수행함 |
| `V11-E2E-M68` | SpotWide safe point와 relocation unit lifecycle | SpotWide 기본 `AnyTurnBoundary`에서는 `Defer()`가 mutation 전에 오류이고 callback이 0건임. `ApplicationSignaled`에서는 accepted `Defer()`마다 no-move·precommit abort의 source `Continued` 또는 relocation 성공의 target `Relocated` callback을 일반 queue보다 먼저 실행하며 기본 no-op도 queue를 재개함. Source Actor는 target factory·필요한 Restore·journal staging을 commit 전에 끝내고 NewOwner CAS가 ObjectGeneration을 유지한 채 current owner를 target identity로 교체함. Entry·PerActor Actor는 callback 없이 queue·journal·Actor timer·relay·session route를 이전함. Actor, Instance Spot, SpotWide User Spot aggregate와 PerActor User Spot direct admission·각 Actor는 source admission seal부터 target admission-open ACK까지 1초 목표를 각각 측정하되 초과해도 계속함 |
| `V11-E2E-M69` | Zero-downtime application patch | Application version N source의 `Retire`가 version N+1 compatible target만 선택해 User Spot·Actor·Instance와 accepted work를 이전하고 continuous request·bound session을 유지한 뒤 source를 `Stopped/None`으로 종료함 |
| `V11-E2E-M70` | Same-version planned maintenance | Application version N source와 target만 있어도 compatible capacity가 있으면 `Retire`가 정상 완료되어 새 version이 same-version maintenance의 필수 조건이 아님을 증명함 |
| `V11-E2E-M71` | Shutdown Spot closing lifecycle | `Shutdown`이 새 relocation·reservation 없이 Entry·User·Instance Spot에 `OnClosing(HostShutdown, Deadline)`을 한 번 호출하고 callback 동안 membership을 유지한 뒤 bounded cleanup과 `Stopped/None` 또는 deadline의 `ForceStopped/DeadlineExceeded`로 끝남 |
| `V11-E2E-M72` | Cross-MeshNode ToSpot | 서로 다른 process·MeshNode에 있는 caller가 MeshName·owner RID·endpoint 없이 global Spot ID만으로 `SendToSpot`·`RequestToSpot`을 호출해 current Ready Spot handler에 정확히 한 번 도달함 |
| `V11-E2E-M73` | Cross-MeshNode ToChannel | 서로 다른 process·MeshNode에 있는 caller가 MeshName·RID·endpoint 없이 unique ChannelName만으로 `SendToChannel`·`RequestToChannel`을 호출해 ready remote member를 선택하고 terminal result를 한 번 반환함 |
| `V11-E2E-M74` | Retire without eligible target | Compatible target이 없으면 `Retire`가 `Draining` publication과 capture 전에 `Blocked/TargetUnavailable` 또는 capability 불일치의 `Blocked/StateIncompatible`로 끝나고 source authority·admission·payload reference를 바꾸지 않음 |
| `V11-E2E-M75` | Readiness-first relocation | Retire notification이 standalone Actor, Instance Spot과 User Spot aggregate queue의 turn boundary에 도달하고, ready unit부터 bounded sliding permit으로 즉시 relocation하며 느린 current turn이 다른 unit을 막지 않음. Permit 전 seal은 0건임 |
| `V11-E2E-M76` | Queue·timer relocation | Current turn 하나만 source에서 완료하고 미실행 message·accepted journal·logical timer registration·pending tick을 immutable relocation payload로 저장함. Target은 application 재등록 없이 timer를 복원하고 frozen queue→seal 중 hold→Ready 이후 message 순서를 보존함 |
| `V11-E2E-M77` | Relocation concurrency gates | 기본 active outbound·inbound 64, Capture·Restore 8, encoded payload in flight 256 MiB를 독립 high-water로 검증하고, byte 한도를 넘는 단일 User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행함 |
| `V11-E2E-M78` | Relocation precommit abort queue recovery | Target reservation·Restore의 commit 전 실패에서 durable abort와 source normalization 뒤 frozen queue·hold queue·timer schedule·operation identity를 source에 같은 순서로 복원하고 target staging과 relocation payload를 정리함 |
| `V11-E2E-M79` | SpotWide Yield double claim | Member Actor A의 job 1이 User Spot gate를 `Yield`한 동안 Actor B, Spot direct handler와 timer는 진행하지만 Actor A job 2는 job 1 continuation이 같은 gate를 다시 얻어 완료할 때까지 시작하지 않음 |
| `V11-E2E-M94` | Relocation payload와 대량 처리 | 실제 encoded payload를 4 KiB, 64 KiB, 1 MiB, 8 MiB, 32 MiB와 64 MiB 경계에서 측정하고 Actor 10,000개, Snapshot Actor 1,000개, Instance Spot 1,000개와 SpotWide aggregate 100개·participant 10,100개를 고정 workload로 이전함. Actor, Instance Spot, SpotWide User Spot aggregate와 PerActor User Spot direct admission·각 Actor의 unit별 interruption 1초 및 workload 처리량 miss는 `workload_slo_missed`로 E2E를 실패시키되 이미 시작한 runtime relocation은 취소하거나 rollback하지 않음. `ST-G5`는 `ENTRY-ACTOR`, `PER-ACTOR`, `PER-ACTOR-SPOT`, `SPOT-WIDE`, `INSTANCE-SPOT` selector별 `unit_kind`·선택형 `execution_mode`와 relocation 전후 연속 request·one-way traffic gap을 독립 evidence로 남김. SpotWide service-unit 검증은 Actor당 64 KiB state를 사용해 Spot 하나에 Actor 10·100개를 넣은 profile을 각각 실행하고 Actor 100개 profile을 정식 1초 SLO gate로 사용함 |
| `V11-E2E-M95` | Relocation 중 서비스 연속성 | 대량 relocation과 동시에 이동 대상이 아닌 control Actor·Spot에 one-way send와 request를 각각 초당 200개 제출하고 성공률, latency, ordering과 duplicate 0을 검증함. 이동 대상은 current owner 또는 Message Follow를 통해 정확히 한 번 처리됨 |
| `V11-E2E-M96` | Actor·Spot Message Follow matrix | Actor·Spot × one-way·request × authority commit 전·후 조합에서 operation identity, generation, deadline, correlation과 reply route를 유지해 이전 owner에서 current owner로 전달함. 기간 만료, duplicate, loop, 8-hop, 1,024-message·16 MiB bound, multi-hop relocation과 route 제거를 검증하며 public 기간 설정은 `MessageFollowDuration` 하나만 사용함 |
| `V11-E2E-M80` | SpotWide Async·self-request deadlock prevention | Current User Spot gate가 필요한 target을 `Async`로 기다리거나 같은 Actor 자신에게 awaited request를 제출하면 outbound admission 전에 닫힌 오류로 끝나고 inline·reentrant handler, queue mutation과 gate release가 0건임 |
| `V11-E2E-M81` | Yield context allowlist | 같은 request·worker public call type을 `SpotWide` User Spot과 Instance Spot에서는 정상 실행하고, Entry Spot·Entry Actor·`PerActor`·Node·Channel·owner 밖에서는 operation submission 전에 `InvalidConfiguration`으로 완료함 |
| `V11-E2E-M82` | PerActor lane isolation | Actor A의 장시간 `Async`가 Actor B와 서로 다른 timer를 막지 않으며 Actor A의 다음 job과 같은 timer의 다음 tick은 FIFO·non-overlap을 유지하고 Spot direct·lifecycle callback은 하나의 Spot lane에서 순서대로 실행됨 |
| `V11-E2E-M83` | Multi-lane lifecycle barrier | `SpotWide`의 yielded continuation과 `PerActor`의 Actor·Spot·timer lane이 남아 있는 동안 close·snapshot·relocation이 capture를 시작하지 않고, seal 실패·abort 뒤 같은 generation의 application admission을 정확히 복원함 |
| `V11-E2E-M84` | Typed capacity unlimited and validation | Actor total·Spot total·Spot stable type limit의 `0`은 hidden population cap 없이 동작하고 음수는 socket bind 전에 startup 오류이며 Entry Spot은 Spot count에서 제외되지만 Entry Actor는 Actor count에 포함됨 |
| `V11-E2E-M85` | Typed capacity atomic reservation | 여러 process의 concurrent create가 Actor·Spot total과 Spot stable type limit을 넘지 않고 Active+Reserved+Requested로 판정하며 factory 실패·timeout·abort가 exact reservation vector만 해제함 |
| `V11-E2E-M86` | Aggregate relocation capacity vector | User Spot과 member Actor relocation이 target Spot total 1개, Spot stable type 1개와 Actor total N개 slot을 단일 bundle로 all-or-none 예약하고 어느 bucket이든 부족하면 factory·Restore·partial authority mutation 없이 다른 candidate를 선택하거나 `PlacementCapacityExhausted`로 끝남 |
| `V11-E2E-M87` | Capacity descriptor projection | Stale descriptor가 여유를 표시해도 Location Store atomic reservation이 최종 거부하며 runtime은 다른 candidate를 시도하고 monitoring은 Actor total·Spot total·Spot stable type별 active·reserved·limit과 unlimited를 구분함 |
| `V11-E2E-M88` | Entry Spot ID lifecycle | MeshNode와 Entry Spot이 같은 diagnostic prefix와 독립 RFC 4122 UUID v4 lowercase canonical suffix를 사용하고 같은 lifecycle에서는 exact mapping을 유지하며 replacement lifecycle은 새 MeshNode RID와 Entry Spot ID를 게시함 |
| `V11-E2E-M89` | Entry Spot ID conflict and reserved pattern | Entry global identity claim의 active 충돌은 기존 record 변경, 두 번째 UUID 생성과 두 번째 claim 없이 즉시 `RoutingIdConflict`로 startup을 실패하고 caller User·Instance Spot ID의 reserved Entry 형식은 Location Store·factory 호출 전에 `InvalidConfiguration`으로 거부됨 |
| `V11-E2E-M90` | Cross-language execution·capacity parity | 다섯 public 언어의 동일 fixture가 `SpotWide`·`PerActor`, Yield allowlist, typed capacity와 Entry Spot ID lifecycle에서 같은 ordering·terminal·monitoring 결과를 기록함 |
| `V11-E2E-M91` | User Spot automatic UUID collision | User Spot `Create`가 UUID v4 Spot ID의 첫 active authority 충돌에서 기존 authority를 바꾸지 않고 즉시 `RoutingIdConflict`로 끝나며 두 번째 UUID·reservation과 factory 호출을 만들지 않음 |
| `V11-E2E-M92` | Common public weight range and selection | RouteMesh Channel, ClientServer Server와 node placement가 startup·runtime에서 signed `0..10000`, 기본 `100`을 공유하고 `-1`·`10001`을 mutation 전에 거부하며 100:300 장기 비율, weight 0 제외, capacity-first, multicast once와 64-bit 합산을 검증함 |
| `V11-E2E-M93` | SpotId UTF-8 exact string boundary | 1·255-byte Spot ID 성공, 256-byte 선거부, case·Unicode normalization 없이 distinct authority 유지, invalid UTF-8 legacy binary frame의 protocol rejection과 Store·factory side effect 0을 다섯 언어에서 검증함 |

| ID | Liveness scenario | 완료 조건 |
|---|---|---|
| `V11-E2E-L01` | Store-backed peer orderly disconnect | FIN·RST·raw monitor event에서 ready 상태는 intentional delay 없이 즉시 바뀌고 test는 5초 observation budget 안에 결과를 확인 |
| `V11-E2E-L02` | Manual peer asymmetric blackhole | Admission이 initial Ready와 15초 deadline을 시작하고 connection당 outstanding probe 하나를 5초마다 재전송하며, 반대 방향 application traffic이 계속되어도 current matching ACK가 없으면 transport target에서 제외 |
| `V11-E2E-L03` | Store-backed process pause | owner lease TTL과 polling 상한 안에 신규 routing에서 제외 |
| `V11-E2E-L04` | Peer restart | Service admission을 다시 수행하고 store-backed exact owner token 또는 manual CSPRNG lifecycle nonce를 equality로 검증하며 current connection handover 뒤 stale event가 successor를 제거하지 않음. Lifecycle token의 숫자 대소 비교는 사용하지 않음 |
| `V11-E2E-L05` | Liveness cleanup | liveness probe·reconnect timer, monitor handle과 child process가 terminal 뒤 남지 않음 |
| `V11-E2E-L06` | Manual·automatic classic fanout publisher failure | Config 3 `PS-F1~F5`: descriptor·manual endpoint별 publisher 전용 SUB socket, publisher→subscriber periodic one-way exact 2-frame beacon, application filter와 reserved subscription 분리, unrelated-topic traffic 중 false timeout 0, 첫 valid receive 전 ready 0과 application delivery 0. Public exact reserved topic은 거부하고 같은 prefix+추가 byte topic은 허용하며 malformed reserved record는 protocol error와 해당 publisher 즉시 not-ready로 수렴. M5 codec negative fixture와 같은 bytes 사용 |

### 14.2 필수 race·회귀 test

- `V11-RACE-01`: 같은 global Spot ID에 Instance intent를 지정한 100개 caller가 최초 요청해 authority owner와 factory execution 하나로 수렴하는지 확인한다.
- `V11-RACE-02`: reply, timeout, cancellation, transfer와 shutdown 순서를 무작위화해 terminal completion이
  하나인지 확인한다.
- `V11-RACE-03`: Transfer의 `Preparing`, Transfer Put, `Captured`, `Prepared`, `Committed`, `Activated`에서
  source·target·coordinator process를 각각 종료한다. `Captured` 전에는 unlinked Put을 orphan으로 정리하고
  transfer를 fenced abort하며 accepted work replay를 주장하지 않는다. `Captured` 이후에는 current coordinator
  owner·lease·node·store-version fence와 replacement reservation ACK를 가진 writer 하나만 linked immutable root로
  durable authority가 정한 방향에 수렴한다. Replacement는 target attempt generation만 증가시키며 stable transfer
  ID의 transfer·journal·replay cursor와 terminal record를 새 attempt key로 복제하거나 잃지 않아야 한다.
- `V11-RACE-04`: reconnect 중 이전 connection의 disconnect, route update와 reply가 successor connection에
  적용되지 않는지 확인한다.
- `V11-RACE-05`: application·infrastructure queue가 각각 포화된 상태에서 admission 결과, completion reserve와
  memory 상한을 확인한다.
- `V11-RACE-06`: timer callback과 Spot close가 경쟁할 때 terminal state 뒤 새 application turn이 시작되지
  않는지 확인한다.
- `V11-RACE-07`: process pause가 owner·coordinator deadline을 넘은 뒤 message, timer, factory completion과
  phase CAS가 모두 거부되는지 확인한다.
- `V11-RACE-08`: public binding monitor·poller callback과 runtime shutdown 경쟁 뒤 use-after-free, 늦은 callback과
  금지된 재진입이 없는지 확인한다.
- `V11-RACE-09`: 반환하지 않는 application·observer callback이 tombstone으로 분리돼 host `ForceStopped`가
  유한 완료되고, callback 반환 뒤 terminal host state를 바꾸지 못하는지 확인한다.
- `V11-RACE-10`: STREAM bind·rebind·unbind와 disconnect가 경쟁해 current token만 변경하고 기존 binding을
  잘못 제거하지 않는지 확인한다.
- `V11-RACE-11`: Actor owner가 A→B→A로 변경되는 순서와 최초 A의 지연 message·journal·forward record
  도착을 임의로 배치해 current membership fence와 다른 record가 모두 거부되는지 확인한다.
- `V11-RACE-12`: Maintenance preflight, 신규 application admission, reversible seal과 capacity reservation을
  경쟁시켜 preflight 성공 뒤 reservation 부족으로 accepted work가 유실되지 않는지 확인한다.
- `V11-RACE-13`: Authority object·owner generation을 signed 64-bit 최댓값에서 증가시키고 stable exhaustion으로
  실패하며 0·음수 wrap, generation 재사용과 partial authority write가 없는지 확인한다.
- `V11-RACE-14`: 같은 global Spot ID에 User Spot create와 Instance fluent cold request를 동시에 제출해
  authority CAS winner 하나만 factory와 local object를 만들고 loser가 별도 typed location row나 generation을
  남기지 않는지 확인한다.
- `V11-RACE-15`: Owner lease expiry, successor claim, 같은 owner ID의 더 높은 lease generation claim,
  이전 process resume와 지연 renew·release 순서를 무작위화해 current token만 lease와 owner descriptor를
  변경하는지 확인한다.
- `V11-RACE-16`: Durable authority phase CAS, owner lease expiry와 successor `new_owner` claim 순서를
  무작위화해 authority payload가 TTL로 삭제되지 않고 current store version의 writer 하나만 phase를
  변경하는지 확인한다.
- `V11-RACE-17`: Bound-session packet acceptance, session owner seal, source journal capture, target replay와
  route publication ACK 순서를 무작위화해 각 session sequence가 source 또는 target에서 한 번만 처리되고
  binding FIFO가 유지되는지 확인한다.
- `V11-RACE-18`: Authority recovery scan watermark와 key create·update·delete·recreate를 경쟁시켜 watermark
  시점의 incarnation이 누락·중복되지 않고 post-watermark incarnation은 다음 scan에서만 처리되는지 확인한다.
- `V11-RACE-19`: Transfer data chunk·manifest Put, authority reference CAS, renew와 cleanup 사이에 process를
  종료해 current manifest의 모든 chunk만 유지되고 orphan·교체된 chunk가 정리되며 partial payload를 replay하지
  않는지 확인한다.
- `V11-RACE-20`: Session owner의 application runtime을 pause하되 transport I/O를 유지하고 lease expiry,
  `sessionTransferSealed`, `sessionTransferRouted`와 successor claim 순서를 무작위화해 current owner token의
  route ACK만 binding을 변경하는지 확인한다.
- `V11-RACE-21`: Transfer target의 offer·reservation ACK 뒤 process pause, host lease expiry, same-owner-ID
  reclaim, `Prepared`·`Committed` CAS와 activation 순서를 무작위화해 exact target owner token과 current
  reservation을 가진 writer 하나만 authority를 변경하는지 확인한다.
- `V11-RACE-22`: 같은 owner ID의 이전·현재 lease token으로 descriptor 게시와 bulk cleanup을 경쟁시켜 이전
  token이 현재 generation의 descriptor·authority·owner index entry를 제거하지 않는지 확인한다.
- `V11-RACE-23`: Target `Activated`, source cleanup terminal, authority `Completed`, public Ready와 session
  unseal 사이에 source·target을 종료하고 조기·재정렬 `transferComplete`도 주입한다. Authenticated source
  cleanup 또는 exact lease-expiry fence 전에는 Completed와 새 application work가 0건이고 immutable transfer
  replacement가 안전하며, Completed 뒤에는 종료된 transfer를 다시 사용하지 않는지 확인한다.
- `V11-RACE-24`: Descriptor page 사이 create·update·delete와 scope change stamp를 경쟁시켜 stamp가 달라진
  partial snapshot이 connect/disconnect diff에 적용되지 않고 stable page set 하나만 desired state를 바꾸는지
  확인한다.
- `V11-RACE-25`: 같은 transfer root에 late terminal completion을 추가하는 writer를 경쟁시켜 새 immutable
  chunk·manifest와 authority root-reference CAS winner 하나만 current가 되고 loser payload가 orphan으로
  정리되며 reply가 누락·중복되지 않는지 확인한다.
- `V11-RACE-26`: 같은 connection의 descriptor update revision을 재정렬하고 equal-revision same·different
  payload와 reconnect를 경쟁시켜 current immutable identity가 바뀌지 않고 가장 높은 valid mutable snapshot
  하나만 selection에 적용되는지 확인한다.
- `V11-RACE-27`: Reply relay, relay ACK, caller timeout·cancellation, request-source connection 종료·재연결,
  exact source lease expiry와 transfer Completed를 무작위화한다. 서로 다른 request kind와 correlation의
  frozen record가 exact original reply route를 유지하며 operation ID나 reconnect route로 바뀌지 않는지 확인한다.
  Connection close만으로 accounted 처리하지 않고 application terminal completion이 한 번이며 ACK·lease expiry
  전 transfer가 해제되지 않고 lost ACK가 idempotent 재전송으로 수렴하는지 확인한다.
- `V11-RACE-28`: Instance `Creating` claim, factory failure, `Ready` CAS와 close를 경쟁시켜 claim winner가
  발급한 nonzero object generation 하나만 operation lifetime에 사용되고 failure·cleanup이 0이나 새 generation을
  기록하지 않는지 확인한다.
- `V11-RACE-29`: Store failure grace, owner lease 갱신 실패와 transport liveness를 경쟁시켜 discovery transport가
  유지되어도 마지막 valid owner deadline 뒤 stateful message·timer·factory completion·CAS가 모두 거부되고,
  복구한 stable page set과 exact owner token 확인 전에는 신규 connect와 stateful admission이 재개되지 않는지
  확인한다.
- `V11-RACE-30`: Post-commit target replacement와 이전 attempt의 factory·Snapshot restore callback 완료를
  경쟁시킨다. 같은 stable transfer에서 callback이 attempt별 한 번 이상 실행되고 겹쳐도 current exact target
  owner와 target attempt만 completion commit·application admission을 수행하며 stale callback은 authority나
  public Ready를 변경하지 않는지 확인한다.
- `V11-RACE-31`: Authority CAS·transfer Put의 invocation, commit, cancellation·timeout과 response delivery를
  무작위화한다. Invocation 전 cancellation만 no-commit이고 이후 불명확한 결과는 exact read·expected fence 또는
  content reference로 reconcile해 duplicate generation transition과 linked partial transfer가 없으며 lost Put은
  bounded orphan cleanup으로 수렴하는지 확인한다.
- `V11-RACE-32`: Prepared session transfer의 durable `Aborted` CAS, source route 복귀 command·ACK,
  reservation·transfer cleanup, steady source normalization과 admission reopen 경계마다 coordinator를 종료한다.
  `Aborted` 결정 전 abort route가 전송되지 않고 source는 sealed를 유지하며, recovery가 ACK를 재전송한 뒤 steady
  normalization까지 완료한 경우에만 source admission이 다시 열리는지 확인한다.
- `V11-RACE-33`: Manual/no-Store peer의 connection-bound request·one-way send terminal, reversible seal,
  `Captured` CAS와 connection restart·handover를 경쟁시킨다. 두 accepted work가 terminal 전 durable journal에
  들어가지 않고 capture gate를 넘지 않으며, 하나라도 timeout이면 pre-Captured abort와
  `Blocked/DeadlineExceeded`로 복원된다. 새 opaque lifecycle token과 current connection만 유효하고 이전
  connection event가 같은 RID successor를 제거하지 않는지 확인한다.
- `V11-RACE-34`: Instance factory·initialize·Ready 실패, fenced delete 응답 손실, owner lease expiry와 다음
  caller claim을 경쟁시킨다. 이전 registry는 current row가 Missing 또는 replacement임을 exact read로 확인하기
  전까지 failed·sealed를 유지하고 hidden factory 재실행이나 handler admission을 하지 않는다. 다음 caller만
  더 높은 object·authority owner generation으로 새 activation을 시작하고 request·one-way terminal evidence가
  중복되지 않는지 확인한다.
- `V11-RACE-35`: User Spot normal close와 Actor join·leave·destroy를 경쟁시킨다. Close의 membership 확인과
  admission 변경은 같은 serialized boundary에서 결정하고 current membership이 있으면 `false`와
  state unchanged로 끝난다. Leave 또는 destroy가 먼저 완료된 경우에만 close와 closing callback이 한 번
  실행되며 dangling Actor authority와 hidden move·destroy가 없는지 확인한다.
- `V11-RACE-36`: Store-backed User Spot·Actor의 Missing→Pending `Creating` Reserve, factory·initialize와 Actor initial
  membership completion, Pending→Active `Ready` Commit, remote resolve·request와 failure Abort를 경쟁시킨다. `Ready` 전에는
  handle·ActorRef와 handler가 외부에 공개되지 않고, 성공하면 처음 발급한 final generation을 유지한다. 실패와
  delete 응답 손실은 exact read로 수렴하며 partial scope·membership·runtime object와 같은 registry의 hidden
  factory 재실행이 없는지 확인한다.
- `V11-RACE-37`: Host deadline, preflight completion과 first admission seal을 경쟁시킨다. Deadline이 seal보다
  먼저면 `Blocked/DeadlineExceeded`와 state unchanged, seal이 먼저면 teardown을 진행하고 deadline 초과 시
  `ForceStopped/DeadlineExceeded`로 terminal-once가 되는지 확인한다. 같은 reason을 outcome 전이의 증거로
  대신 사용하지 않는다.
- `V11-RACE-38`: Actor NewOwner CAS, target Entry `OnTransfer`, journal replay, source Entry
  `OnLeaveActor`·membership cleanup과 target replacement를 경쟁시킨다. Current owner·attempt만 target callback과
  replay를 commit하고 authority의 owner·AOG·current Spot이 한 snapshot으로 바뀌며 source cleanup failure가 target
  admission을 Completed·route ACK·steady normalization 전에 열지 않는지 확인한다.
- `V11-RACE-39`: `SpotWide` Member Actor의 Yield terminal completion, 다른 Actor·Spot·timer admission과 같은
  Actor의 다음 job을 경쟁시킨다. Actor claim은 continuation 완료까지 하나이며 User Spot gate만 release·reacquire되고
  같은 Actor handler의 overlap·FIFO 역전과 inline self-dispatch가 없는지 확인한다.
- `V11-RACE-40`: `PerActor` Actor·Spot·timer lane admission과 close·snapshot·relocation seal을 경쟁시킨다. Seal 뒤
  새 application admission은 없고 yielded continuation을 포함한 기존 obligation이 모두 안전 경계에 도달한 뒤에만
  capture하며 abort는 같은 generation의 모든 lane을 다시 여는지 확인한다.
- `V11-RACE-41`: Actor·Spot·stable type limit의 마지막 slot에 여러 creation·relocation을 동시에 예약한다. Typed
  vector 전체가 한 transaction에서 성공하거나 실패하고 aggregate의 Spot total 1개·Spot stable type 1개·
  Actor total N개 중 일부만 reserved·active로 남지 않으며 stale lifecycle cleanup이 successor count를
  변경하지 않는지 확인한다.
- `V11-RACE-42`: Entry Spot ID의 첫 identity claim과 descriptor owner replacement를 경쟁시킨다. Exact active
  claim 충돌은 기존 record mutation, 두 번째 UUID 생성과 두 번째 claim 없이 즉시 `RoutingIdConflict`로
  끝나며 이전 lifecycle의 지연 publication·cleanup이 새 lifecycle의 Entry mapping을 제거하거나 재사용하지
  않는지 확인한다.
- `V11-RACE-43`: Unsupported execution context에서 request·worker `Yield`와 cancellation·timeout을 경쟁시킨다.
  Context validation이 항상 operation ID, outbound admission, worker scheduling과 queue mutation보다 먼저이며
  `InvalidConfiguration` terminal이 한 번만 완료되는지 확인한다.
- `V11-RACE-44`: User Spot automatic UUID 생성과 global authority reservation에 active collision을 주입한다.
  첫 collision이 기존 authority를 바꾸지 않고 즉시 terminal이 되며 두 번째 UUID·reservation·factory가
  생성되지 않는지 확인한다.
- `V11-RACE-45`: RouteMesh·ClientServer·placement runtime weight update와 target selection을 경쟁시킨다.
  Descriptor revision보다 먼저 범위 밖 값이 거부되고 이미 submit·reservation된 operation은 이전 선택을
  유지하며, 많은 `10000` member의 합산이 overflow 없이 current positive candidate 집합을 사용하는지 확인한다.

각 E2E cell은 protocol·fixture revision, 양쪽 Framework·binding package와 Core payload의 version·절대
경로·SHA-256을 기록한다. Java와 Kotlin은 JVM runtime을 공유하지만 Java `CompletionStage`와 Kotlin
coroutine·DSL 진입점을 각각 검증한다.

## 15. Performance smoke-only 판정

11.0에서는 성능 수치를 판정하거나 개선하지 않는다. C++·.NET·JVM·Node.js service runtime, Kotlin
consumer와 Core raw runner에 대해 다음 smoke만 release gate로 사용한다.

| Pattern | 최소 동작 |
|---|---|
| `FRAMEWORK_SPOT_PUBSUB` | Hub Spot이 Channel-scoped Logical Multicast로 peer Spot에 한 번 이상 전달함 |
| `FRAMEWORK_SPOT_REQREP` | Peer와 hub 사이의 Spot request와 reply가 한 번 이상 완료됨 |
| `FRAMEWORK_SPOT_SENDSEND` | Peer와 hub가 양방향 one-way submit을 각각 한 번 이상 완료함 |

네 언어 runner는 같은 workload fixture를 사용하지만 service state machine을 공유하지 않는다. Kotlin은 별도
runtime runner를 만들지 않고 Kotlin public API로 JVM runtime startup, 최소 operation과 cleanup을 검증한다.
Loopback, peer 한 개, 작은 고정 payload와 짧은 유한 operation 수만 사용한다.

- runner와 application이 clean build된다.
- server와 client가 시작되고 ready 조건을 충족한다.
- Spot publish, request/reply와 양방향 send가 작은 local topology에서 각각 한 번 이상 완료된다.
- Core raw socket의 대표 send·receive workload가 최소 한 번 완료된다.
- 결과가 versioned schema로 기록되고 Framework package, binding package, Core payload, source revision,
  protocol·fixture revision, build mode, 절대 경로와 SHA-256을 포함한다.
- `RAW-PERF-SMOKE`는 `core/build` runtime을 먼저 다시 build하고 runner가 실제로 load한
  `libzlink` 절대 경로를 출력한다. `core/build` runtime이 `core/src`·`core/include`보다 오래되었거나
  예상한 경로와 실제 load 경로가 다르면 workload 시작 전에 실패한다.
- 잘못된 artifact, 오래된 runtime과 남은 이전 process가 있으면 시작 전에 실패한다.
- timeout, assertion과 child process 비정상 종료가 없다.
- 종료 뒤 process, thread·event-loop handle, timer, pending operation과 endpoint resource가 남지 않는다.

결과 schema는 pattern과 schema version, 닫힌 결과·failure reason, 성공·오류 operation 수, 실행 시각과 child
exit status, platform·architecture·compiler·build mode, runtime 언어, dependency manifest, transport·payload·
peer·operation 수를 기록한다. Framework·binding·Core payload의 version, source revision, 실제 절대 경로와
SHA-256, protocol·workload fixture revision도 포함한다. 처리량이나 latency가 진단용으로 기록되어도 성공
판정에는 사용하지 않는다. Artifact load 경로, provenance, schema·fixture version을 해석할 수 없거나 같은 output
directory의 이전 child process가 남아 있으면 workload를 시작하기 전에 실패한다.

`V11-M2-ORACLE`이 10.x normalized trace와 읽기 전용 archive를 봉인하면 `V11-M3-PERF-LEGACY`에서
`bindings/c/perf`의 다음 active Spot 항목을 제거한다. 이후 같은 workload의 최소 동작은 Core target을
복구하지 않고 `scripts/v11/run-smoke.sh --kind perf`가 네 Framework runtime과 Kotlin consumer에서 검증한다.

- `single/src/perf_spot_pubsub.cpp`, `multi/src/perf_multi_spot_client.cpp`와
  `multi/src/perf_multi_spot_server.cpp`
- `perf_spot_pubsub`, `comp_src_spot_pubsub_*`, `comp_src_spot_reqrep_*`와
  `comp_src_spot_sendsend_*` CMake target
- Bash·PowerShell runner의 `SPOT_PUBSUB`, `SPOT_REQREP`, `SPOT_SENDSEND` pattern
- Spot paired gate, result·metric parser의 Spot mapping, fixture·assertion, CI target와 active README 안내

PAIR, DEALER, ROUTER, classic PUB/SUB와 STREAM raw perf target·result schema는 유지한다. Core 10.x Spot baseline과
result archive도 읽기 전용으로 보존하되 11.0 active result나 latest baseline으로 자동 선택되지 않게 한다.
Default와 glob을 포함한 모든 결과 선택 규칙에서 archive 경로를 명시적으로 제외한다.
Active source·build·runner·test·README에서는 `zlink_spot_`, `zlink_mesh_node_`, `perf_spot`, `SPOT_*`와
`MULTI_SPOT` 잔여를 machine gate로 확인하며 archive 제외 경로를 검사기에 명시한다.

처리량, latency, CPU, RSS, raw ROUTER 대비 비율, 대규모 peer·object·payload matrix, 반복성과 data path
개선은 11.0 완료 조건이 아니다. 별도 성능 작업에서 수행한다.

## 16. Package와 version

- Core service 제거와 raw ABI 변경은 Core `11.0.0` candidate에 반영한다.
- C++, .NET, Java와 Node bindings는 Core 11 raw surface와 같은 `11.0.0` major line을 사용한다.
- C++·.NET·JVM·Node.js server Framework package는 같은 Framework version을 사용한다.
- Java와 Kotlin은 JVM runtime을 공유하지만 Java JAR과 Kotlin JAR·metadata를 각각 생성하고 검증한다.
- Core version과 package는 `V11-R2`, bindings version과 package는 `V11-R3`가 각각 clean을 기록한 뒤에만
  확정한다.
- Framework final package는 `V11-R6` cleanup review 뒤 만들고 `V11-R7`에서 전체 조합을 최종 승인한다.
- Core candidate가 바뀌면 네 bindings native payload, Framework package와 이를 사용한 증거를 다시 만든다.
- 모든 package는 local/internal 위치에만 생성하며 외부 배포를 수행하지 않는다.
- Package manifest는 Framework, binding과 Core payload의 검증된 정확한 조합을 고정한다.

## 17. Candidate 실패와 되돌림

M2의 10.x oracle는 읽기 전용 비교 artifact로 유지한다. 제거 작업이 실패해도 새 candidate가 oracle library를
직접 호출하거나 load하는 fallback을 만들지 않는다.

M3의 Core 제거나 raw 회귀가 실패하면 service symbol 일부에 fake 구현을 추가하지 않는다. M3 candidate 전체를
review 이전 상태로 되돌린 뒤 제거 범위나 raw 보존 코드를 수정한다. `V11-R2`가 clean을 기록하기 전에는 Core
11 package를 배포하지 않는다.

M4의 한 binding lane이 실패하면 해당 lane candidate만 폐기할 수 있다. 다른 binding 결과로 public capability를
대체하지 않으며 Core service projection을 되살리지 않는다. 네 lane이 `V11-R3`를 통과하기 전에는 binding
package를 배포하지 않는다.

M5·M6의 중간 candidate는 public E2E·sample·release 대상으로 사용하지 않는다. 아직 완성되지 않은 operation은
internal contract에서 명시적인 실패로 유지하되 production public path에 `RuntimeNotReady` placeholder를 넣지 않는다.
Test를 통과시키기 위한 fake success·data, shadow backend, private binding 접근이나 compatibility facade로 전환하지 않는다.
Local/internal package 조합이 섞인 상태를 release candidate로 사용하지 않는다.

Protocol schema나 public contract 의미가 바뀌면 영향받는 생성물, runtime lane, fixture와 방향성 E2E만 다시
검증한다. Core 또는 binding payload가 바뀌면 그것을 포함한 package·consumer·E2E·smoke 증거는 다시 만든다.
문서 hash만 바뀐 경우에는 candidate와 review를 되돌리지 않는다.

## 18. 독립 review와 수렴 규칙

이후 모든 독립 review는 Codex `gpt-5.6-sol high`(`P-HIGH`) 단독 reviewer가 수행하며 Claude 병렬
reviewer를 두지 않는다. Reviewer는 구현 lane과 분리된 상태에서 같은 Git base와 candidate manifest의 누적
변경을 검토한다. Candidate의 commit 여부는 review 선행 조건이 아니다. 이미 완료된 row의 두 reviewer 기록은
당시 실행 이력이다.
Review 상태와 finding, 수정 revision, 영향 ID와 재검증 증거는 해당 review 행의 증거 칸에만 기록한다.

Review 축은 다음과 같다.

| 축 | 검토 내용 |
|---|---|
| `I1` | Core raw spec, Framework spec, protocol, 네 runtime, 다섯 public API, sample, E2E와 package의 계약 일치 |
| `I2` | POSD·DDD의 깊은 모듈, 정보 은닉, caller 부담, pass-through, aggregate 경계와 중복 상태 기계 |
| `I3` | 제거 API·source·test·문서·compat helper·dead code, build·package 입력과 참조가 끊긴 파일 |
| `I4` | §2.3의 언어 병렬 구현 교차 비교. component 경계와 이름, state machine, ownership·ordering, 오류 분류, resource 수명, test 구성에서 축별 최선안과 다섯 언어 동형 여부 |
| `D1` | documentation principles, 현재·목표 상태 구분, 독자와 디렉터리 책임 |
| `D2` | 정식 spec, public API, source, test, diagram과 package의 실제 일치 |

수렴 규칙은 다음과 같다.

1. 1~4회차에는 `Critical`, `High`, `Medium`, `Low`의 모든 유효 finding을 반영한다.
2. 각 회차는 reviewer가 같은 기준 revision과 그 회차의 누적 변경을 확인한 한 번의 독립 review다. 완료된 row의 회차는 두 reviewer 한 쌍으로 계산한 이력이다.
3. 5회차부터 `Critical`, `High` 또는 `Medium` finding이 하나라도 있으면 gate를 통과하지 못한다.
4. 5회차 이후 `Low` finding만 남으면 위치, 영향과 처리 결정을 증거 칸에 기록하고 clean으로 종료할 수 있다.
5. 정확성, 공개 계약, data loss, race, security, package mismatch와 제거 누락을 선호 문제로 낮추지 않는다.
6. Reviewer가 clean을 기록한 뒤 해당 stage가 소유한 required gate를 다시 실행한다. M5 review는 internal
   contract와 기존 pending catalog를 재검증한다. `V11-R4A`는 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet 두
   reviewer로 amended formal contract·protocol·impact manifest를 재검증했다.
   M6 runtime review는 internal regression과 execution quarantine만 재실행한다. `V11-R5D`는 Codex
   `gpt-5.6-sol high` 단독 reviewer가 final E2E·sample spec, impact disposition과 coverage를 독립 review하고
   post-review gate를 재실행한다. 최초 실제 E2E는 `V11-R5D` 뒤 `V11-M6A-E2E`가 시작하고, full matrix는 M7, final package
   재검증은 M9가 소유한다.
7. 이후 source가 바뀌면 의미와 직접 의존 범위만 다시 review한다. Hash 변화만으로 전체 review를 처음부터
   시작하지 않는다.

## 19. 전체 완료 조건

- [x] `SPEC-01~06`과 `V11-R1`이 구현 전에 완료됐다.
- [ ] Framework 공통 정식 spec, Core raw 정식 spec과 다섯 exact interface가 실제 구현과 일치한다.
- [ ] Global identity·remote placement amendment의 미결정이 0이고 정식 spec·다섯 exact interface·protocol에
  모두 흡수됐다.
- [ ] Amendment 독립 review 뒤 두 임시 변경 제안과 repository link가 제거됐으며, 이후 작업이 정식 spec과
  이 ledger의 입력만으로 수행됐다.
- [ ] 영향받은 public member·E2E·sample·registration·regression이 impact manifest에 빠짐없이 분류됐고,
  영향받지 않은 source와 registration은 baseline hash를 유지했다.
- [ ] Core 10.x oracle는 별도 process와 normalized trace로만 사용됐고 새 candidate의 compile·link·load 입력이 아니다.
- [ ] Core service 제거와 POSD·DDD review 뒤에만 Core 11 version과 internal package를 만들었다.
- [ ] 네 bindings service projection 제거와 POSD·DDD review 뒤에만 binding 11 package를 만들었다.
- [ ] Core와 네 bindings에서 ZMP heartbeat option·frame·engine timer projection이 제거됐다.
- [ ] 네 runtime의 application traffic과 독립된 5초 livenessProbe·current connection의 matching ACK 15초
  deadline과 raw monitor·owner lease 책임이 분리됐다.
- [ ] 네 public raw binding과 Store capability proof가 final candidate에서 통과했다.
- [ ] C++·.NET·JVM·Node.js runtime이 해당 언어 public binding API만 사용한다.
- [ ] Java와 Kotlin 공개 surface·ABI·coroutine·metadata를 JVM lane 하나가 각각 검증했다.
- [ ] Topology, messaging, Spot, Actor, Instance Spot, STREAM과 stateful maintenance가 네 runtime에 구현됐다.
- [ ] Runtime 구현 중 E2E·sample은 실행 graph에서만 격리됐고 source 삭제·임시 우회·skip·성공 처리가 없었다.
- [ ] Runtime 완료 뒤 공통 E2E·sample spec을 확정하고 topology→stateful object→maintenance 순서로 E2E를
  활성화해 각 묶음의 runtime gap을 0으로 만들었다.
- [ ] Final E2E·sample spec이 `V11-R5D`에서 Codex `gpt-5.6-sol high` 단독 reviewer의 독립 review와 post-review gate를
  통과한 뒤에만 E2E source·registration 변경과 실행을 시작했다.
- [ ] Required contract·race·crash·recovery와 방향이 있는 `4 x 4` E2E가 skipped 없이 통과했다.
- [ ] 전체 E2E 뒤 다섯 언어 sample이 approved source·registration과 public API만 사용해 compile·run됐다.
- [ ] Core service와 네 bindings service projection이 제거됐다.
- [ ] Production scaffold, `RuntimeNotReady` placeholder와 fake success·data branch가 production code에 남지 않았다.
- [ ] Core generic timer·monitor와 raw transport 회귀는 유지되고 제거한 ZMP heartbeat 잔여가 없다.
- [ ] 제거 API 때문에 참조가 끊긴 source·test·sample·build·package 입력이 남지 않았다.
- [ ] Final local/internal package와 clean consumer가 통과했고 외부 배포가 없다.
- [ ] Functional smoke와 performance smoke-only gate가 final package로 통과했다.
- [ ] 처리량·latency·CPU·memory 판정과 성능 개선이 11.0 gate에 포함되지 않았다.
- [ ] 모든 필수 review가 수렴 규칙에 따라 clean으로 끝났다.
- [ ] `blocked-issue-log.md`의 모든 항목이 §2.2의 우회 없이 `해결`로 끝났고 `차단` 항목이 남지 않았다.
- [ ] 모든 기능이 §2.3의 병렬 구현·교차 리뷰·최선안 조합·동형 수렴을 거쳤고, 다섯 언어의 component 경계·
  state machine·오류 분류·test 구성 차이가 언어 idiom 사유와 함께만 남아 있다.
- [ ] 모든 상태와 증거가 이 ledger의 담당 행에만 기록됐다.
### Node.js SpotActorTransfer 진행 갱신 — 2026-07-28 06:06 KST

- opaque Location Store repository는 authority row, authority별 generation, node capacity와 creation
  terminal을 별도 key로 저장하고 bounded conditional batch로 변경한다. preserve·restore·delete
  authority CAS도 provider row에서 수행한다.
- Core-routed Actor request는 application handler가 반환한 reply를 정확히 한 번 제출한 뒤 deferred
  Join을 실행한다. Join Accepted·Rejected·Failed completion은 별도 128-bit operation identity로
  Actor queue에 전달한다. Node workspace build와 `deferred-actor-join.test.js` 5/5가 통과했다.
- Automatic RouteMesh의 internal raw Actor relay는 legacy Route channel이 아니라 native MeshNode
  admission과 completion table을 사용한다. private `/probe-stale` fixture와 임시 debug 출력은 제거했다.
- actual `ST-F4` 증거 `framework/languages/node/e2e/SpotActorTransfer/log/20260728-060622-385415`에서
  Automatic Ready, shared Actor·User Spot 생성, cross-node deferred Join, target `transfer_in`·`joined`,
  source Message Follow 등록·relay, target `G1` handler를 확인했다.
- 남은 blocker는 relocation capacity reservation과 `newOwner` authority CAS가 아직 provider에
  영속화되지 않은 점이다. Message Follow가 만료된 뒤 public global Actor lookup이
  `actorRouteNotFound`로 끝난다. 이 CAS를 완료한 뒤 ST-F4를 다시 통과시키고, ST-F5를 public
  multi-hop operation만 사용하는 scenario로 전환해 실행한다.

### Node.js SpotActorTransfer 진행 갱신 — 2026-07-28 06:45 KST

- opaque provider-backed relocation capacity reservation과 `newOwner` authority CAS를 연결했다.
  Accepted journal의 preserve 갱신이 reservation 이후 authority version을 바꿔도 source owner,
  allocation과 lifecycle fence가 같으면 최신 payload를 보존한 채 CAS한다. Target User Spot 주소도
  authority payload에 저장해 remote global Actor request가 framework node-direct relay로 전달된다.
- Node workspace build와 deferred Join·Actor handoff·Redis·auto-connect focused 41/41이 통과했다.
  실제 `ST-F4` 회귀 실행 `framework/languages/node/e2e/SpotActorTransfer/log/20260728-064028-1208441`은
  cross-node relocation, duration 안 G1 relay와 만료 뒤 Location Store 재탐색을 통과했다.
- 위 ST-F4 exit 0은 공통 Config 10 완료 증거가 아니다. Commit 전에 이전 물리 owner로 resolve한 G2
  delivery를 duration 뒤에 해제해 typed `ActorLocationStale`과 source·target handler 0건을 확인하는
  process 외부 transport-delay fixture가 아직 없다. Application DTO와 endpoint에는 ActorRef·RID를
  노출하지 않는다.
- ST-F5에서 private `/probe-stale`과 고정 ActorRef·RID를 제거하고 public create·Join·Actor request
  multi-hop scenario로 바꿨다. 실행
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-064151-1266305`은 첫 A→B relocation을
  완료했지만 두 번째 B→A relocation에서 source에 `Joined` lifecycle이 다시 전달된 뒤
  `join_completion|failed|kind=actorRouteNotFound`로 끝났다. 이 lifecycle ownership gap이 Node
  ST-F5의 현재 blocker다.

### Node.js ST-F4 정본 fixture 갱신 — 2026-07-28 06:52 KST

- normal global Actor resolver와 submit 사이에 E2E host가 non-public runtime construction으로
  주입하는 internal delivery gate를 연결했다. Operation ID는 fixture entry 선택에만 사용하며 application
  metadata와 message에 넣지 않는다. HTTP fixture 응답에도 ActorRef·owner RID·ObjectGeneration을
  포함하지 않는다. Production host는 gate를 등록하지 않아 nullable dependency 확인만 수행한다.
- 실제 실행
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-065314-1321351`에서 authority commit 전에
  resolve된 G1·G2를 각각 한 번 포착했다. G1은 6초 duration 안에 해제되어 final target handler가
  정확히 한 번 실행됐다. G2는 duration 뒤 해제되어 public terminal `actorLocationStale`로 끝났고
  source·target application handler는 모두 0건이었다. 두 operation의 capture·release count도 각각
  `1/1`이다.
- Runtime Message Follow marker는 route 설치 대기에만 사용했다. 완료 판정은 public typed terminal,
  application handler count와 delivery fixture count를 사용한다. private `/probe-stale`, fixed ActorRef,
  fixed owner RID와 임시 debug 출력은 없다. 따라서 Node ST-F4는 완료로 전환한다.

### Node.js ST-F5 multi-hop 수렴 — 2026-07-28 07:14 KST

- 두 번째 relocation을 막은 원인은 이미 `Delivered`인 durable Join completion root가 다음 정상 Join
  operation을 계속 거부한 점과 Core source leave 뒤 Framework Actor shell이 registry에 남은 점이었다.
  Delivered root는 같은 ObjectGeneration의 다음 operation이 authority CAS로 교체하고 이전 immutable
  blob을 삭제한다. Core가 native source lifecycle을 끝낸 뒤에는 native actor를 다시 삭제하지 않고
  Framework shell만 제거한다.
- Message Follow의 stale fence는 relocation에서 유지되는 ObjectGeneration과 이전 physical NodeRid를
  함께 보존한다. 현재 local ActorRef는 이전 route보다 우선하므로 Actor가 이전 node로 돌아와도 follow
  cycle을 만들지 않는다. Route가 만료된 뒤에는 최초 resolve한 physical ActorRef를 public
  `actorLocationStale`로 종료한다.
- ST-F5 runner는 세 Actor node를 사용한다. Application은 global Actor ID one-way·request를 A에서
  resolve한 뒤 fixture gate에서 정지하고 A→B→C relocation을 수행한다. Fresh process
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-071841-1562392`에서 one-way는 A와 B의
  Message Follow route를 거쳐 C handler에서 정확히 한 번 처리됐다. Duration 뒤 request는
  `actorLocationStale`, 세 node handler 0건, 두 fixture capture·release `1/1`로 끝났다.
- Native MeshNode에서 one-way로 도착하는 relocation control도 application node-direct dispatch보다
  먼저 service relocation runtime이 처리한다. Internal route handler도 native dispatcher에 연결했다.
  Runner는 scenario가 끝난 뒤 isolated fixture process를 종료하고 모든 `*-flow.log`에서
  `phase=error`, `Unhandled`, `FAIL`을 검사하므로 sink가 process 종료 때 기록한 오류도 빠뜨리지 않는다.
- 이 gate를 적용한 fresh `ST-F4`
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-073637-1695410`은 actor-a와 actor-b
  flow에서 세 pattern이 각각 0건이다. Fresh 3-node `ST-F5`
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-073652-1694856`도 actor-a, actor-b,
  actor-c flow에서 각각 0건이다. 두 process scenario와 Node workspace build가 통과했으며
  Actor handoff·deferred Join·durable Join journal·Redis·auto-connect focused test 45/45도 통과했다.

### Node.js Actor Message Follow exact context와 request 수렴 — 2026-07-28 08:30 KST

- Actor Message Follow의 internal context를 immutable record 하나로 통일했다. 128-bit operation ID,
  ObjectGeneration, source·target owner lease와 node lifecycle·authority owner generation,
  original deadline, correlation·reply route, hop·visited-owner chain과 payload checksum을 보존한다.
  Public `ActorRef`와 application DTO에는 이 field를 추가하지 않았다.
- Route key는 ActorId, ObjectGeneration과 source authority owner generation을 사용한다. Relay 전에
  full owner fence를 다시 검사한다. 같은 operation ID·checksum·request·reply route의 중복은 같은
  terminal에 합류하므로 target handler가 한 번만 실행된다. 서로 다른 payload, owner loop, 8-hop
  초과, deadline과 route당 1,024-message·16 MiB 초과는 handler admission 전에 거부한다.
- Spot relay decoder에 남아 있던 dynamic `handoff*` ActorRef property를 제거하고 같은 immutable
  context를 attach한다. Context와 같은 값을 반복하던 top-level operation ID·hop·deadline·owner
  generation field도 wire와 accepted handoff packet에서 제거했다. Encoder와 decoder는 context
  하나만 identity와 relay 판정에 사용한다.
- Node workspace build와 Actor client·handoff·authority focused test 33/33이 통과했다. Authority
  test의 이전 3건 실패는 current public Store 경계와 exact ActorRef 전환을 반영하지 않은 fixture가
  원인이었다. Fixture를 `objectGeneration`·`meshName`·string SpotId·leaseGeneration과 실제 owner
  lease로 맞춘 뒤 3/3이 통과했다.
- Contract surface는 28/32다. Message Follow internal type의 public export 0, package root의 runtime
  export 0과 제거 이름 0은 통과했다. 남은 4건은 `V11-M9-DOCS`와 §12.34가 이미 소유하는 exact
  declaration gap이다. `ZLinkMeshNodeState`, `ZLinkActorRefSnapshot`, decorator·catalog export가
  production package에 아직 없다. 이번 internal runtime 변경과 분리해 해당 row에서 해소한다.
- Fresh `ST-F4`
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-082930-2149821`은 relocation 전에
  보류한 one-way와 `G3-positive-request`를 final owner에서 각각 한 번 처리하고 request reply를
  한 번 반환했다. Fresh 3-node `ST-F5`
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-082947-2149807`은 A→B→C의 이전 두
  owner가 one-way와 request를 각각 한 번 relay하고 final owner가 `chain-to-final`과
  `chain-request-to-final`을 각각 한 번 처리했다. 두 실행의 모든 stderr는 0 bytes다.
- Node Actor의 current process Message Follow P0/P1은 닫았다. Spot Message Follow, restart recovery,
  payload·bulk, service continuity와 1초 interruption 측정은 `ST-G4/G5`, `ST-I1~I6`,
  `V11-E2E-M95/M96`에서 계속한다.

### .NET Entry Actor relocation 연속 서비스 갱신 — 2026-07-28 09:24 KST

- Target canonical replay는 preserved backlog 전체에 mailbox 순서를 먼저 배정한 뒤
  direct admission을 연다. Handler 뒤 durable replay cursor CAS와 command 33 reply
  relay·ACK는 accepted sequence 순서로 실행한다. Completion callback이 병렬로 CAS해
  후속 request reply를 잃던 경쟁을 제거했다.
- ST-G5 traffic fixture는 같은 종류의 operation을 한 제출 흐름에서 순서대로 보내
  FIFO 기준을 명확히 했다. Evidence node 이름은 고정 이름과 RID prefix를 모두
  인식해 source 마지막 handler와 target 첫 handler 또는 reply 사이의 gap을 계산한다.
- Fresh `ST-G5-SMALL`
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-092319-3148154`가
  통과했다. Interruption은 0.388267초, application handler gap은 377 ms다.
  Request 71건과 one-way 80건의 loss·duplicate가 모두 0이며 FIFO, operation ID,
  deadline과 request correlation을 보존했다. Focused standalone Actor relocation
  unit test도 34/34가 통과했다.
- 이 결과는 `.NET` Entry Actor small profile만 닫는다. PerActor·SpotWide·Instance
  Spot, slow profile, Spot Message Follow, restart recovery와 mixed-language
  process 검증이 남아 있으므로 `ST-G5` 전체와 `BLK-044`는 진행 상태를 유지한다.

### Node.js Actor Message Follow review finding 해소 — 2026-07-28 10:32 KST

- Post-commit stale route에서 context가 없는 packet에 새 operation ID를 만들던 fallback을 제거했다.
  Active handoff의 최초 source admission만 immutable context를 만든다. Message Follow route는
  caller가 만든 context와 exact authority owner fence가 없으면 Store 조회, relay와 application
  handler 실행 없이 `ActorLocationStale`로 끝난다. Actor route resolver가 owner lease, node
  generation, authority owner generation이나 ObjectGeneration을 빠뜨려도 transport에 제출하지 않는다.
- Focused test는 context가 없는 동일 stale packet 두 건의 relay·handler가 0인지, resolver fence가
  불완전할 때 transport 제출이 0인지 직접 확인한다. Node build와 Actor client·handoff·authority
  test는 35/35가 통과했다.
- `ST-F4`는 기존 transport admission gate에서 동일 immutable one-way와 request operation을 각각
  두 번 제출한다. Runtime dedupe 뒤 relay, target handler와 request terminal은 각각 한 번만
  발생한다. Fresh 증거는
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-104100-140600`이다.
- `ST-F5`는 A→B→C 두 hop에서 operation ID, ObjectGeneration, absolute deadline, correlation,
  reply route와 payload checksum이 같은지 비교한다. Authority owner generation은 `1→2→3`,
  hop count는 `1→2`로 증가한다. Fresh 증거는
  `framework/languages/node/e2e/SpotActorTransfer/log/20260728-104100-140606`이다.
  두 fresh 실행은 통과했고 stderr 합계는 각각 0 bytes다.
- 이 review finding의 current-process Actor 범위는 닫았다. Spot Message Follow, restart recovery,
  payload·bulk, service continuity와 1초 interruption은 기존 `ST-G4/G5`, `ST-I1~I6`,
  `V11-E2E-M95/M96` 범위에서 계속한다.

### .NET monitoring 공개 경계와 MON-A4 갱신 — 2026-07-28

- `MON-A4`에서 Relocate가 owner lease를 정리한 뒤 Shutdown이 같은 정리를 다시 요구하던 오류를
  수정했다. Relocate가 정리를 끝낸 경우 Shutdown은 descriptor 정리와 lease release를 반복하지 않는다.
  Auto-connect와 Location·Drain focused unit test 228/228이 통과했다.
- 실제 process 실행
  `framework/languages/dotnet/e2e/RuntimeMonitoring/logs/20260728-115326-1569788`에서
  Relocate 뒤 Shutdown까지 `MON-A4`가 통과했다.
- 공통 monitoring 계약에 없는 public runtime event bus를 제거했다. Application은 typed runtime
  status와 `ObserveAsync`, `ILogger`, `ActivitySource`, `Meter`를 사용한다.
  `AddZLinkMonitoring`, raw socket·Location·Spot event DTO, event handler와 event dispatcher를
  source, sample, E2E와 unit·contract test에서 제거했다. Timer failure와 drain 상태는 기존
  `ILogger`와 공통 diagnostics 경로로 기록한다.
- .NET source, sample, E2E, test와 exact interface에서 제거 대상 이름을 다시 검색한 결과는 0건이다.
  제거를 확인하는 negative contract assertion만 남겼다. Eventing 제거 뒤 source compile 오류는
  모두 해소했다.
- Message-flow 공개 observer, runtime stream, file·label 설정과 flow DTO를 API snapshot에서도
  제거했다. Application이 설정하는 표면은 diagnostics level, sample rate와 message size 포함
  여부뿐이며 실행 중에는 `IZLinkDiagnosticsRuntime.Level`만 바꾼다.
- Diagnostics `off`에서는 listener가 있어도 trace용 operation ID, flow context와 Activity를
  만들지 않는다. Dispatch error Activity는 `event_id=zlink.dispatch_error`,
  `outcome=failed`와 공통 `reason`·`action` 닫힌 값을 사용한다.
- 제거 이름을 source, API snapshot과 .NET exact interface에서 검색한 결과는 0건이다.
  Framework와 Unit project build는 warning·error 0, diagnostics focused test는
  4/4·1/1·12/12·7/7, public contract test는 69/69가 통과했다.
- Message Follow deadline 회귀 한 건과 sample·E2E의 낡은 diagnostics 호출은 각 담당 gate에서
  계속 정리한다. 따라서 이 checkpoint만으로 `V11-M6C-DN` 전체를 완료로 바꾸지 않는다.

### .NET diagnostics E2E 이관 checkpoint — 2026-07-28

- 24개 E2E host가 `ConfigureDispatch().Diagnostics.SetLevel(Normal)`을 사용한다. 실행 중 진단을
  끄는 두 endpoint는 `IZLinkDiagnosticsRuntime.Level = Off`를 사용한다.
- 제거된 Framework 전용 message-flow observer 대신 표준 `ActivityListener`가
  `Zlink.Framework` ActivitySource를 관찰한다. Meter 이름은 `zlink.framework`로 고정했다.
- dispatch error evidence의 기존 filter와 field는 유지했다. listener와 evidence sink에서 예외가
  발생해도 application dispatch를 실패시키지 않는다.
- 제거된 diagnostics public symbol은 `SpotActorTransfer`의 동시 수정 범위를 제외하고 0건이다.
  `ObservabilityOps.Tests`는 4/4를 통과했다. 이 결과에는 Activity tag·file 기록과 sink 예외 격리
  검증이 포함된다.
- 전체 solution compile에는 diagnostics와 무관한 이전 public contract 사용이 남아 있다.
  `SpotService`, `ObservabilityOps`, `ResilienceLifecycle`의 Spot handle·Actor Join·channel 호출을
  현재 global identity와 deferred Join 계약으로 이관한 뒤 전체 gate를 다시 실행한다.

### .NET Instance Spot reference sample gap — 2026-07-28

- 공통 sample 계약은 ShoppingMall의 `OrderWorkflowSpot`과 GameQuest의 `PlayerQuestSpot`을
  Instance Spot reference sample로 지정한다.
- 현재 .NET source는 두 Spot을 User Spot factory로 등록하고 `IZLinkSpotManager.GetOrCreate(...)`로
  생성한다. direct Spot send/request에 `InstanceSpot(stableType)` intent를 지정하는 호출은 0건이다.
- 47개 sample project compile 성공은 이 계약을 검증하지 않는다. 두 sample의 factory 등록과 최초
  message 경로를 Instance Spot 계약으로 바꾸고 focused scenario를 실행하기 전에는
  `V11-SAMPLE-SPEC-FINAL`과 `V11-M7-SAMPLES`의 완료 증거로 사용하지 않는다.

### .NET Instance Spot reference sample gap 해소 — 2026-07-28

- ShoppingMall은 Order ID를 global Spot ID로 사용한다. CommerceApi의 direct Spot request가
  `InstanceSpot("shoppingmall.order-workflow").InMesh(...)`를 지정하고 OrderWorkflow는
  `AddInstanceSpotFactory<OrderWorkflowSpot>`과 `IZLinkInstanceSpot` lifecycle을 사용한다.
- GameQuest는 Player ID를 global Spot ID로 사용한다. GameApi의 direct Spot send/request가
  `InstanceSpot("gamequest.player-quest").InMesh(...)`를 지정하고 QuestMission은
  `AddInstanceSpotFactory<PlayerQuestSpot>`과 `IZLinkInstanceSpot` lifecycle을 사용한다.
- 두 sample에서 Instance Spot을 User Spot manager로 미리 생성하던 `GetOrCreate` 경로와 중간
  OrderWorkflow·QuestOwner channel을 제거했다. 특정 NodeRid를 object placement에 사용하지 않는다.
  GameQuest의 MeshName과 같아서 RouteMesh와 충돌하던 사용되지 않는 ClientServer channel도 제거했다.
- focused SampleRegression 6/6과 전체 SampleRegression 122/122가 통과했다. ShoppingMall과
  GameQuest aggregate build는 오류 0이며 이전 Instance Spot 생성 경로 검색 결과도 0건이다.
- Docker·Redis 실제 sample process 실행은 아직 수행하지 않았다. 따라서 이 checkpoint는 source와
  compile 계약만 닫고 `V11-M7-SAMPLES`의 runtime gate는 완료로 바꾸지 않는다.

### .NET ResilienceLifecycle public Channel compile checkpoint — 2026-07-28

- Consumer, ephemeral client와 storm worker는 `resilience.profile` Channel을 Client 역할로 등록한다.
  호출은 현재 `RequestToChannel(channelName, request)`와
  `SendToChannel(channelName, message).Async(...)` 계약을 사용한다. 이전 MeshName 중복 인자와
  결과를 반환하던 one-way terminal은 제거했다.
- Redis provider의 공개 Store SPI assembly를 Consumer, Provider와 Client project가 직접 참조한다.
  Framework package의 transitive internal dependency에 기대지 않는다.
- Consumer, Provider와 Client project를 각각 `-m:1`로 build한 결과 warning·error 0이다.
  이 checkpoint는 compile gap만 닫는다. Config 5 process scenario는 최종 E2E gate에서 다시 실행한다.

### .NET Config 2·10·11 runtime 수렴 checkpoint — 2026-07-28

Config 11 ObservabilityOps의 Play·Session·Workflow·Client·Tests는 현재 public
contract로 이관한 뒤 `--no-restore -m:1` build에서 warning·error 0을 확인했다.
구형 Spot handle resolver, caller가 지정하는 Entry Spot RID, RoutingId 기반 Spot
주소, 이전 ActorRef와 동기 Join call은 source에서 제거했다. Config 11 정식
scenario 중 A5와 C6·C7 source는 추가했지만 C8·C9·C12와 runner topology 분기,
C10·C11 최종 확인이 남았다. 따라서 Config 11 process E2E는 아직 시작하지 않았다.

Config 2의 실제 process 검증은 다음 gap을 순서대로 드러냈다.

- `logs/20260728-171229-3725068`: descriptor weight revision과 Actor reservation이
  경합하면 `Conflict(Missing)`을 즉시 `Unavailable`로 반환했다.
- Actor·User Spot manager가 이미 reservation 경합을 확인한 경우에는 create
  deadline 안에서 descriptor를 다시 읽고 target을 재선택하도록 수정했다.
  persistent conflict의 caller cancellation·deadline과 terminal-once capacity를
  검증하는 focused test 3개가 통과했다.
- `logs/20260728-172001-3849381`: reservation 뒤 weight가 `0`으로 바뀌자 provider가
  이미 승인한 operation까지 새 placement 조건으로 다시 거부했다. Reserve 때만
  positive weight를 검사하고 commit은 exact owner·lifecycle·capability와 capacity
  fence를 검증하도록 수정했으며 focused test 1개가 통과했다.
- `logs/20260728-172458-3863217`: 30초 application idle timeout보다 긴 crash
  scenario에서 transport heartbeat만으로 session application activity가 갱신되지
  않음을 확인했다. 장애 대기 중 survivor Actor request를 계속 제출해 실제 서비스
  연속성을 검증하도록 scenario를 보강했다.
- `logs/20260728-172726-3870164`: 위 두 runtime gap과 session continuity 구간을
  통과한 뒤 crash owner의 Actor authority가 15초 안에 정리되지 않았다.
  `/actor/wait-missing`의 HTTP 504를 `AsyncRaw` 호출이 검사하지 않아 scenario가
  계속 진행했고, 이후 `GetOrCreate`가 남은 authority를 `Existing`으로 반환해 같은
  ObjectGeneration assertion에서 실패했다. E2E는 504를 즉시 실패로 검증하도록
  보강했다. Manager lookup은 owner lease가 없거나 generation이 다르거나 만료된
  authority를 성공 결과에서 제외한다. Provider는 relocation·aggregate recovery가
  진행 중이지 않은 stale authority만 CAS로 정리하고 capacity를 반환한 뒤 generation
  high-water보다 큰 새 reservation을 발급한다. Provider focused test 6/6과 Actor
  manager focused test 3/3이 통과했다.
- `logs/20260728-175637-74263`: stale authority 제외, 재생성 generation 증가,
  이전 `ActorRef`의 exact lifecycle 거부와 fresh global Actor request 성공까지
  통과했다. 이후 restart readiness probe가 이전 `play-a-*`의 종료된 peer와 새
  Ready peer를 prefix로 함께 선택해 `SingleOrDefault` 예외를 냈다. Automatic
  RouteMesh 연결 자체의 실패가 아니라 E2E probe의 lifecycle 구분 오류였다.
- `logs/20260728-180823-436304`: control·Spot readiness probe가 같은 prefix의
  Ready peer만 선택하도록 수정한 뒤 `crash-1-ready`와 `restart-1-ready`를 모두
  통과했다. 같은 실행은 이어지는 session Auth rebind가 request timeout으로 끝나
  `crash-2-ready`에는 도달하지 못했다. 따라서 automatic restart route gap은 닫았지만
  이 실행만으로는 SM-G1 전체 process gate를 완료하지 않았다.
- 첫 crash 뒤 실패한 connector를 재사용하지 않고 fresh stream connection에서
  explicit Auth rebind를 수행하도록 수정했다. Session evidence는 새 connection의
  Actor generation과 binding session ID를 기록하고, bound Actor request가 새
  `play-a` owner에서 성공하는지 확인한다.
- Fresh rebind 뒤 request sequence `2`가 이전 binding의 pending request와 충돌했다.
  Remote session pending key를 Actor ID·Object generation·binding token·request ID로
  분리하고, send 실패 cleanup도 같은 exact identity를 사용하도록 수정했다. Reply는
  reply capability까지 일치한 pending entry만 claim한다. Rebind는 교체한 binding의
  pending admission을 정리하므로 늦은 이전 reply가 새 binding을 완료하지 못한다.
  새 focused test 1/1과 `SessionActorCoordinatorTests` 32/32가 통과했다.
- Placement weight 변경 endpoint가 peer 전체 topology `Ready`를 성공 조건으로 사용해
  의도적인 crash 중 local weight 변경을 503으로 거부하던 E2E 결합을 제거했다.
  Setter acceptance 뒤 gateway readiness와 실제 placement probe가 descriptor 게시와
  target 선택을 계속 검증한다.
- `logs/20260728-182314-490611`: `crash-1-ready`, `restart-1-ready`,
  `crash-2-ready`와 최종 `operation SpotService.sm-g1 passed`를 모두 확인했다.
  두 replacement 경로의 새 Actor generation, stale exact ref 거부, fresh session
  rebind, bound Actor messaging과 survivor 연속성이 통과했으므로 SM-G1 process
  gate를 완료한다.

Config 10의 command 35는 steady authority를 확인할 때 canonical relocation wrapper
자체를 이미 정규화된 authority로 잘못 판정해 target completion과 Relocation root
삭제를 건너뛰었다. exact steady 판정을 실제 Actor authority payload에만 적용하도록
수정했고 command 35 focused 1/1과 relocation unit suite 262/262가 통과했다.
ACTORS10 실제 실행 `logs/20260728-172610-3868647`은 Spot request 157/157과 one-way
157/157은 통과했지만 Actor request sequence 143·144 두 개가 timeout으로 끝났다.
source cleanup과 target normalization은 완료됐으나 두 operation의 accepted
journal·Message Follow·reply relay 단계별 추적이 남았다. 이 실행은 Config 10 완료
증거가 아니다.

### .NET sample NodeRid 직접 지정 제거 checkpoint — 2026-07-28

- `Bingo`, `TicTacToe`, `DeliveryDispatch`, `GameQuest`, `ShoppingMall`,
  `SupportChat`, `ZoneWorld` sample에서 object 배치 대상 NodeRid를 지정하는 호출은
  0건이다.
- 추가 audit에서 `ZoneWorld` Ops가 관측한 NodeRid를 다시 조회해
  `RequestToNode`로 maintenance와 diagnostics를 특정 process에 보내는 경로 한 건을
  확인했다. Object 배치는 아니지만 사용자가 그대로 따라 할 수 있는 node 직접 지정
  예제이므로 제거했다.
- Maintenance는 desired state를 Store에 기록한 뒤 fanout으로 전달한다. Diagnostics는
  ZoneNode가 이미 보고한 최신 상태를 Ops의 `NodeRegistry`에서 읽는다. Application
  node ID를 transport NodeRid로 바꾸어 요청하는 public sample 경로는 0건이다.
- `ZoneWorld.Server.Ops`, `ZoneWorld.Server.ZoneNode`, `ZoneWorld.Client`를 각각
  `dotnet build --no-restore --maxcpucount:1`로 검증했고 모두 warning·error 0이다.
  이 checkpoint는 compile 증거이며 `V11-M7-SAMPLES`의 실제 process gate를 대신하지
  않는다.

### .NET Config 10·11 후속 runtime 증거 — 2026-07-28

Config 10은 post-seal request의 reply capability를 Message Follow 등록 단계에서
새로 만들던 중복 보존을 제거했다. 이미 capture한 reply capability가 있으면 그
capability를 그대로 사용한다.

- Reply capability exact preservation RED test를 추가한 뒤 수정했으며 relocation
  unit suite 264/264가 통과했다.
- 실제 실행
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-175059-54624`에서
  Spot request·one-way는 각각 158/158, Actor one-way는 158/158이 통과했다.
  Actor request는 158건 중 1건이 timeout으로 끝났다.
- 처음 의심한 `request_id=560`은 실패 operation이 아니다. 이 request는 source
  backlog, target handler와 direct reply, source Message Follow reply 제출까지
  완료됐고 caller도 정상 완료했다. 실패한 sequence 145는 caller의 `corr=259`
  제출 뒤 source owner ingress 전에 사라졌다. Owner resolution·generation,
  transport submit과 source admission 사이를 연결해 추적해야 한다.

두 실행 모두 process와 container cleanup을 마쳤다. 남은 runtime gap을 닫기
전에는 Config 2·10을 완료로 판정하지 않는다.

Config 11은 caller가 RoutingId를 지정하지 않은 process의 실제 local RID를
`PreparedRoutingId`에서 읽도록 relocation preflight 세 경로를 통일했다.

- Framework, Play, Session, Workflow와 Client build는 warning·error 0이며
  maintenance focused test 16/16이 통과했다.
- 실제 실행
  `framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-175929-243831`에서
  exact peer readiness, preflight와 `Relocating` publication을 통과했다. 같은
  RollingUpdate intent의 두 waiter는 약 30초 뒤 동일한
  `Blocked/DeadlineExceeded` 결과를 받았다. 다른 option의 두 호출은 각각
  1.94 ms와 0.19 ms에 `Blocked/OperationInProgress`로 끝났다.
- 이전 `TargetUnavailable`은 해소됐지만 실제 workload relocation과 accepted
  barrier 사이에서 deadline이 끝났다. Target ingress·ACK와 completion phase를
  좁혀 수정하기 전에는 Config 11 완료 증거가 아니다.

후속 protocol audit에서 source가 command 34 response를 transport queue에 넣은
시점만 보고 canonical stage를 성공 처리하는 gap을 확인했다. Target이 accepted
barrier, inbound stage·publication과 admission open을 끝낸 뒤 exact command 34
ACK를 source에 돌려주고, source가 그 ACK를 확인해야 stage가 끝나도록 수정했다.
ACK는 relocation ID, target attempt, coordinator, participant high-water와 peer
identity를 검증한다. Focused test와 canonical reservation class test 31/31이
통과했다.

실제 재실행
`framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-181753-460601`은
동일 waiter 결과를 보존했지만 약 30.15초 뒤 `Blocked/RelocationFailed`로 끝났다.
Target process에는 relocation ingress가 없으므로 이 실행은 command 40 전
Serving preflight에서 중단됐다. Exact peer readiness와 target descriptor의
capability·version·capacity reservation 조건을 분리해 계속 추적한다.

Public readiness 증거를 추가한 실행
`framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-183331-681121`은
source에서 target의 실제 generated RID가 `Ready` peer이고 두 process의 topology
descriptor가 모두 `Ready`임을 JSON으로 남겼다. 내부 exact peer readiness도
descriptor RID·lifecycle과 core admitted peer가 일치해 통과했다. 따라서 이전
판단과 달리 automatic RouteMesh lifecycle 불일치는 원인이 아니다.

단계별 command 40 진단을 추가한 중단 실행
`framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-183834-768625`은
source seal 뒤 `source-before-reserve`까지 도달했지만 command 40 offer를 받기 전
대기했다. 다음 수정 대상은 preflight가 아니라
`ReserveCanonicalRelocationAsync`의 command 40 target ingress와 command 41
offer 반환 경로다. 이 실행은 다른 focused process와 겹쳐 즉시 종료했으며 모든
ObservabilityOps process를 정리했다.
한쪽에서만 connect한 RouteMesh fixture에서도 authenticated source RID와 target
candidate를 검증한 command 40이 offer와 seal까지 완료됐다. Focused test 1/1이
통과했으므로 automatic topology의 단방향 connect 규칙 자체는 원인에서 제외한다.

Relocation timer freeze와 runtime admission seal이 경합하면 frozen timer tick을
handler 실패로 보고하던 경로도 분리했다. Freeze 전에 admission을 확인하고,
admission 실패와 동시에 freeze가 완료된 경우에는 `false`를 반환해 해당 tick을
relocation snapshot의 pending tick으로 유지한다. `TimerLifecycleTests` 8/8이
통과했다. C11 process gate는 command 40 ingress gap을 닫고 다시 검증하기 전까지
완료로 판정하지 않는다.

### .NET Session binding·Store·Config 10 후속 checkpoint — 2026-07-28

Config 2 `SM-D4A`는 서로 다른 process의 Session A와 B가 같은 Actor를 순서대로
bind하는 실제 경로를 추가했다. Actor owner는 새 binding을 등록할 때 이전 Session
owner에 exact Actor ID, session RID, binding token·generation과 session owner
lifecycle을 가진 tombstone을 보낸다. 이전 Session owner는 같은 identity만 제거하며
아직 local commit 전이면 bounded tombstone으로 늦은 commit을 거부한다. Actor
ingress도 current session node RID와 session RID를 함께 검증한다.

- Session·Client project build는 warning·error 0이다.
- Focused binding·stale relay test 2/2가 통과했다.
- 실제 process 실행
  `framework/languages/dotnet/e2e/SpotService/logs/20260728-190632-1115028`에서
  `operation SpotService.sm-d4a passed`를 확인했다.
- 이전 Session A의 stale relay는 source에서 typed
  `InvalidOperation/RetryAfterBackoff`로 끝났다. 늦은 disconnect는 Session B의
  current binding과 두 Session의 companion Actor binding을 바꾸지 않았다.
- 이 변경은 independent `gpt-5.6-sol high` concurrency·retry review가 진행 중이므로
  `V11-M6-SESSION-BINDING-DN` 전체를 아직 완료로 바꾸지 않는다.

독립 review는 정상 A→B 경로 밖에서 5개 gap을 확인했다. Tombstone 전송 실패 시
Actor owner의 replacement 상태가 남아 이후 bind를 막는 문제, 완료된 A→B 뒤 A의
오래된 bind retry가 current binding을 되돌릴 수 있는 문제가 P0이다. Session owner의
tombstone이 한 번 거부한 뒤 제거되는 문제와 reused Node RID를 막을 full Actor
authority fence 누락은 P1이다. 같은 request 판정이 session identity 일부만 비교하는
문제는 P2이다. Replacement begin·commit·abort, bounded completed-token 결과와 exact
immutable identity 비교를 하나의 내부 aggregate로 모으고 실패 rollback, response-loss
retry, tombstone 반복 거부·expiry·bounded count, lifecycle reuse 회귀를 통과하기
전에는 위 process 성공을 완료 증거로 승격하지 않는다.

후속 구현은 replacement의 begin·join·commit·abort를 Actor runtime state에
응집했다. 두 tombstone ACK 전에는 이전 binding이 current이고 pending replacement는
relay에서 보이지 않는다. 전송 실패·cancellation은 이전 binding으로 복구하고 pending을
제거한다. 완료된 exact token은 request timeout의 두 배 동안 Actor별 최대 1,024개를
보존해 response-loss로 재전송된 오래된 bind가 current binding을 되돌리지 못하게 한다.
Session owner tombstone도 expiry까지 반복 거부하며 최대 4,096개를 보존하고
`TimeProvider` 기준으로 정리한다. Private packet은 Actor object·node·authority owner·
owner lease generation, MeshName과 Session identity·lifecycle 전체를 비교한다.
수량 상한에 도달하면 기존 binding을 변경하지 않고 fail-closed한다.

- UnitTests project build는 warning·error 0이다.
- `ActorBoundSessionRelayTests`, `SessionActorCoordinatorTests`,
  `ActorHandoffTests` focused 101/101이 통과했다.
- 실패·취소 rollback, pending 비공개, duplicate join, 오래된 token replay,
  conflicting duplicate, tombstone 반복·expiry·count, reused RID lifecycle과
  full route fence를 포함한다.
- 별도 post-fix high review와 SM-D4A process 재실행 전에는 완료로 승격하지 않는다.

Store 기준 구현의 독립 POSD review는 expired scan이 partial snapshot으로 끝나는
문제, owner claim의 ambiguous completion, Redis 전체 index scan과 stale index,
mutable options, sub-millisecond retention 절삭, mandatory SPI의 default throw,
공개 주석의 Redis schema 누출 등 7건을 찾았다. 수정 뒤 다음 focused 증거가
통과했다.

- Provider snapshot은 expired cursor에서 누적 row를 폐기하고 최대 4회 전체 scan을
  다시 시작한다. Owner claim write 예외는 독립 exact read로 같은 owner,
  generation과 expiry를 확인할 때만 성공으로 복구한다.
- Redis opaque record는 bounded MVCC page와 32개 단위 cleanup을 사용한다. Options는
  constructor에서 snapshot하고 positive retention은 최소 1 ms로 올림한다.
- Relocation repository의 deterministic write는 모든 구현체가 명시적으로 구현한다.
- Unit, Redis와 Contract project build는 warning·error 0이다. 신규 focused 5/5와
  실제 Redis provider 9/9가 통과했다.
- 전체 회귀와 finding 0 재review 전에는
  `V11-M6-STORE-POSD-DN-REVIEW`를 완료로 바꾸지 않는다.

Config 10의 source `Capturing` 구간 stale ingress는 기존 handoff journal에
operation identity, deadline, request correlation과 reply capability를 보존한다.
Abort 시 source queue 순서로 되돌리고 one-way에는 reply route를 만들지 않는다.
Focused race·ownership test는 6/6이 통과했다.

실제 ACTORS-10 재실행
`framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-190727-1166981`은
Spot request·one-way 161/161과 Actor request·one-way terminal 수를 모두
보존했다. 그러나 Actor `...000003`에서 target capture의 sequence `134`보다
source Message Follow sequence `144`가 먼저 실행됐다. Operation ID와 deadline은
각 handler에서 유지됐지만 target replay 완료 전에 direct admission이 열린
순서 위반이다. Target queue가 이전된 queue, target ingress와 source
Message Follow drain을 모두 예약한 뒤 direct admission을 열도록 수정하고
ACTORS-10을 다시 통과하기 전에는 Config 10 완료 증거로 사용하지 않는다.

후속 narrow 구현은 target-captured frame의 Actor mailbox 예약보다 먼저 SpotWide
shared serial queue 위치를 기존 relocation seal 아래 예약하고, imported journal과
trailing frame이 그 위치를 한 번 소비하도록 연결했다. Public API나 별도 queue는
추가하지 않았다. Deterministic race 1/1, canonical replay focused 4/4와
`UserSpotExecutionSchedulerTests` 14/14, Unit project build warning·error 0이
통과했다.

그러나 실제 재실행
`framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260728-194315-1981510`은
Spot request·one-way 158/158을 보존한 뒤 Actor `...000003`에서 다시 실패했다.
Target evidence는 source Message Follow sequence `144`를 target-captured sequence
`134`보다 먼저 기록했다. 두 frame 모두 operation ID와 deadline은 유지됐고 terminal
유실이 아니라 순서 검증에서 실패했다. 따라서 deterministic fixture가 실제 aggregate
target replay call graph를 덮지 못했거나 production path가 새 shared reservation을
소비하지 않은 상태다. 실제 production call graph에 같은 reservation이 연결되고
ACTORS-10이 통과하기 전에는 narrow focused 증거를 완료로 승격하지 않는다.

Production call graph를 다시 추적해 원인을 더 좁혔다. Aggregate target은 command
34를 처리하면서 authority와 local catalog를 먼저 공개하고 Actor admission도 연다.
Source는 command 34 ACK를 받은 뒤에야 Message Follow를 시작한다. 따라서 새 owner로
직접 들어온 sequence `144`가 shared queue에 먼저 들어가고, 이전 owner가 나중에
전달한 sequence `134`는 이미 예약된 위치보다 뒤에 놓인다. Target 내부에서 replay
위치만 예약하는 수정으로는 이 순서를 복구할 수 없다. Target은 source cleanup
command 35 전까지 direct ingress를 실행하지 않고, source에서 전달된 backlog를 먼저
target capture에 넣은 뒤 admission을 열어야 한다. 이 cutover 경계와 실제 aggregate
production path를 검증하는 결정적 test를 추가한 다음 process ACTORS-10을 재실행한다.

Config 11의 단일 후속 진단 실행
`framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-191309-1432665`은
target command 41 offer가 source까지 돌아온 뒤
`PendingRelocationReservation.MatchesOffer`에서 거부됐음을 확인했다. Target
precheck·Offer 처리 예외나 RouteMesh 연결 문제는 아니다. 진단 값에서
`offeredMessages`와 `offeredBytes`가 모두 `268435456`으로 기록되어 capacity field
mapping 또는 비교 순서가 의심된다. Source·target generation, participant progress와
required message·byte capacity를 정적 대조해 같은 wire 계약으로 맞춘 뒤 focused
회귀를 통과해야 한다. 이 진단 뒤 process와 container는 정리했다.

정적 대조 결과 capacity 값은 원인이 아니었다. Instance Spot의 command 40 source가
kind 3 wire에서 생략하도록 정한 `ExpectedAuthorityOwnerGeneration`을 non-zero로
구성해, target이 schema 정규형으로 decode·reply한 command 41과 exact match가
실패했다. Source는 kind 3 값을 `0`으로 정규화하고 codec은 noncanonical kind 3
입력을 거부하도록 수정했다. Normal offer, capacity 부족 뒤 다음 candidate,
lifecycle mismatch와 pending cleanup focused test를 추가했다. Production
Framework와 UnitTests assembly compile은 통과했다.
`Instance_spot_command40_uses_the_schema_normalized_object_shape`,
`Raw_two_node_instance_spot_offer_matches_the_command40_shape`,
`Capacity_failure_releases_pending_before_the_next_candidate`,
`Lifecycle_mismatch_does_not_retain_a_pending_reservation`은 4/4가
통과했다. 마지막 두 test는 실패 뒤 source pending reservation이 `0`으로
정리되고 다음 정상 candidate 또는 exact lifecycle 재시도가 완료되는지도 확인한다.
Process C11은 root가 다른 E2E와 겹치지 않게 별도로 재실행하므로 아직 완료 증거로
사용하지 않는다.

실제 process 재실행
`framework/languages/dotnet/e2e/ObservabilityOps/logs/20260728-193422-1880647`은
이전 command 41 exact-match 거부를 지나 source admission seal과 workload relocation
구간에 진입했다. 그러나 같은 intent waiter 두 개는 다시 약 30초 뒤 terminal을
반환해 C11 완료 조건을 충족하지 못했다. Source Spot timer delivery 150~156은
Spot별 timer freeze 전에 host admission이 먼저 닫힌 구간에서
`SPOT application admission is sealed for drain`을 handler failure로 기록했다.
이 tick은 application failure가 아니라 relocation snapshot에 남겨야 한다. Timer
freeze와 host admission 사이의 실제 process race, 그리고 30초 terminal의 다음
미완료 phase를 분리해 수정하기 전에는 C11을 완료로 판정하지 않는다. 실행 뒤 모든
process와 Redis container는 정리됐다.

.NET guide와 Bingo sample 문서에 남아 있던 제거된
`MessageFlow(...)`·`TraceLogFile(...)`·observer public 예제를 현재
`IZLinkDispatchOptions.Diagnostics`, `IZLinkDiagnosticsRuntime`과 표준
`ActivitySource`·`ILogger` 계약으로 교체했다. .NET guide·internals와 Bingo 범위의
stale symbol 검색은 0이고 대상 `git diff --check`가 통과했다. `TRACE --check`는 이
교정이 아니라 병렬 C++ exact-interface 변경으로 reviewed member set이
3,062개에서 3,088개로 달라져 중단됐다. C++ public candidate review와
`TRACE --refresh-review`를 마치기 전에는 trace gate를 갱신하지 않는다.

Session binding 후속 high review는 첫 수정 뒤에도 다섯 가지 P1을 확인했다. 첫
tombstone ACK 뒤 두 번째 side effect가 실패하면 이전 상태로 되돌릴 수 없고, remote
await 뒤 authority·lifecycle을 다시 확인하지 않아 destroy·relocation과 경합할 수
있다. 이전 Actor owner에 보내는 unbind는 source fence와 retired token replay
보존이 부족하며, peer lifecycle도 caller가 보낸 값을 그대로 신뢰한다. 현재
replacement를 `Prepared → Published → Completed` 단일 상태 기계로 바꾸고, 첫 durable
side effect 뒤에는 동일 token retry가 forward completion을 이어가게 한다. Publish
직전 current authority·owner lease·admitted peer lifecycle을 다시 확인하고, 이전
owner 정리는 exact source-fenced tombstone으로 교체한다. 이 수정의 focused test,
독립 finding 0 review와 SM-D4A process 재실행이 남아 있다.

Store 후속 POSD review는 여섯 가지 gap을 추가로 확인했다. Provider snapshot이 prefix
전체를 materialize하고 완료 token을 무기한 보존하며, Redis MVCC cleanup 시각을 hot
write마다 뒤로 미뤄 history가 무한 증가할 수 있다. MeshNode·ClientServer·Fanout
descriptor write에는 ambiguous commit read-back이 없고, relocation retention은
sub-millisecond 값을 내림한다. Exact SPI 문서의 64 MiB 제한은 실제 23-byte envelope
입력과 맞지 않으며, Redis 문서가 약속한 shared connection도 구현에 없다. Provider
cursor 직접 paging, fixed cleanup due와 per-key backlog bound, 공통 exact read-back,
retention 올림, 문서 bound 교정과 normalized connection 내부 refcount pool을 적용한
뒤 focused·Redis 회귀와 finding 0 review를 다시 수행한다.

### .NET final high review의 relocation terminal·Shutdown 수정 — 2026-07-28

정식 host 계약과 production 경로를 다시 대조해 세 경계를 수정했다.

- `Relocated` host에 mode 또는 target version이 다른 `Relocate`를 다시 호출해도 새
  operation을 시작하지 않고 최초 `Relocated/None` 결과를 반환한다.
- Target command 34에서 publication을 시작한 뒤 terminal receipt 기록이 실패하거나
  caller cancellation이 발생해도 이미 fault된 single-flight를 slot에 남기지 않는다.
  Durable terminal 기록은 caller cancellation과 분리하며, publication 결과를 아직
  확인할 수 없으면 동일 command 34가 staging·publication reconciliation을 다시
  소유한다.
- `Shutdown`은 호출 시 host 상태와 application admission을 먼저 `Draining`으로
  바꾼다. 진행 중인 relocation batch에는 최초 Shutdown deadline을 적용하고, phase
  사이에서 stop signal을 확인해 새 relocation unit을 시작하지 않는다. 현재 batch가
  terminal 상태가 되면 Relocate waiter는 `Blocked/ShutdownRequested`로 끝나며
  Shutdown cleanup이 이어진다. Deadline을 넘긴 현재 batch는 force-stop 경로로
  전환한다.

Maintenance·drain, canonical reservation, Actor Join/Close, Object Client 연결,
one-way와 diagnostics focused Unit 174/174, canonical reservation 단독 38/38,
public contract focused 24/24와 대상 `git diff --check`가 통과했다. 이 결과는
source·contract regression 증거다. SpotActorTransfer의 internal delivery gate를
외부 transport harness로 바꾸는 process E2E gap은
`framework/doc/framework/common/spec/30-implementation-gap.ko.md` §12.52에
남아 있으므로 Config 10과 전체 `.NET` final review를 완료로 바꾸지 않는다.

추가 high review에서 managed MeshNode의 port 0 endpoint 반영, Spot outbound의
ClientServer runtime 선택과 Bingo의 bound-session push 전환을 다시 확인했다.
기능 경로에는 새 finding이 없었다. 다만 ClientServer channel이 metadata를 거부할 때
submitter에 넘기기 전의 encoded message parts를 정리하지 않는 ownership gap을
찾았다. Global client와 current Spot client가 공유하는
`ZLinkFrameworkRuntimeChannels`의 metadata validation branch가 parts를 정확히 한 번
정리한 뒤 `NotSupportedException`을 반환하도록 수정했다. Global send·request와
current Spot send·request focused test 4/4가 통과했고 project build도 성공했다.
전체 UnitTests 1,242/1,242 통과 결과와 함께 적용 후 final high review의 새 code
finding은 0건이다. 완료 판정에는 위 external transport harness process E2E gap이
계속 남아 있다.
## 2026-07-29 JVM Object Client 연결 계약 증거

Java와 Kotlin이 공유하는 Framework-owned raw RouteMesh에 Object Client 연결 판정을
반영했다.

- Location Store descriptor는 Object Client·Server와 RouteMesh Channel Server membership을
  함께 보존한다. Object role이 없는 RouteMesh node도 automatic discovery가 필요하면 같은
  descriptor 경로를 사용한다.
- Automatic planner는 양쪽 모두 Object Client이고 양쪽 모두 RouteMesh Channel Server
  membership이 없을 때만 connection intent를 만들지 않는다.
- Manual handshake는 같은 pair를 `NOT_REQUIRED` terminal로 끝낸다. 같은 expected RID에
  reconnect announcement를 반복하지 않고 liveness admission에도 포함하지 않는다.
- 어느 한쪽에 weight `0`을 포함한 RouteMesh Channel Server membership이 있으면 connection이
  필요하다.
- Object Client와 application Node direct handler의 조합은 startup에서 거부한다.
  Object Client RID를 Node direct Send·Request target으로 지정하면 reroute 없이
  `NOT_FOUND`로 끝낸다.

Focused 검증은 single worker와 no-daemon으로 실행했다. Gradle build는 성공했고
`ZLinkAutoConnectPlannerTest` 8/8, `ZLinkAutoConnectReconcilerTest` 3/3,
`ZLinkJavaRawMeshNodeM6ATest` 6/6, `ZLinkRouteMeshRuntimeServiceTest` 5/5,
`ZLinkFrameworkRuntimeNodesAndServicesTest` 8/8이 통과했다. 실패·오류·skip은 0이다.

Java production runtime과 실제 Redis Location Store를 사용하는 별도 process `RM-A3`도
통과했다.

- Automatic과 Manual Object Client pair는 각각 20초 동안 79회 조회에서 계속
  `NotRequired`였고 Ready peer 수는 `0`이었다.
- Manual pair 앞의 두 TCP proxy는 각각 연결 한 번만 관측했다.
- Object Client RID로 보낸 Node direct Send와 Request는 모두
  `REQUEST_TARGET_NOT_FOUND`로 끝났다.
- Weight `0` RouteMesh Channel Server membership이 있는 pair는 양쪽 모두 Ready였다.
  한 process를 `SIGKILL`한 뒤 다른 process는 같은 peer를 `NotConnected`로 표시했다.
- runner exit code는 `0`이며 모든 assertion 뒤 원자적으로 기록한
  `RM-A3.result`는 `result=passed`다. 모든 stderr는 비어 있고 잔여 fixture·proxy process도 없다.

증거는 `framework/languages/java/e2e/RegistryMessaging/logs/20260729-015634-1430590/`에
있다. Java `RM-A3` actual-process gap은 해소됐다. Kotlin public projection의 별도 actual
evidence는 남아 있으므로 JVM lane 전체 완료 상태는 올리지 않는다.

## 2026-07-29 Node.js Object Client 연결 계약 증거

Node.js RouteMesh의 Automatic planner와 Manual raw admission에 같은 Object Client 연결 판정을
적용했다.

- 양쪽 모두 Object Client이고 양쪽 모두 RouteMesh Channel Server membership이 없을 때만
  연결 intent를 만들지 않으며 public snapshot에 `NotRequired` peer로 기록한다.
- weight `0`을 포함해 어느 한쪽에 RouteMesh Channel Server membership이 있으면 연결을
  요구한다. Object role과 Channel role을 서로 대신 사용하지 않는다.
- Manual handshake는 reason `4`를 terminal `NotRequired`로 처리한다. 해당 peer는 ready peer와
  liveness probe 대상에서 제외하며 reconnect announcement를 계속하지 않는다.
- Object Client와 application Node direct handler의 조합은 startup에서 거부한다. Object Client
  RID를 Node direct Send·Request target으로 지정하면 reroute 없이 typed `NotFound`로 끝낸다.

Focused 검증은 `verify:m6a-runtime` 11/11, production TypeScript typecheck, planner 1/1,
startup validation 1/1, Node-direct 1/1, public snapshot state 1/1과 exact surface 1/1이
통과했다. 실패·skip은 0이다. Automatic·Manual
actual-process `RM-A3`와 public monitoring snapshot scenario는 아직 실행하지 않았으므로
`V11-M6A-NODE`와 Config 1·7 행을 완료 상태로 올리지 않는다.

## 2026-07-29 JVM aggregate와 public boundary 회귀 증거

Codec과 testkit test가 제거된 public `ZLinkCodecRegistration` 구현을 직접 사용하던 문제를
정리했다. MessagePack·Protobuf test와 fake backend test는 production internal type을 다시
노출하지 않고, public `ZLinkCodecRegistryBuilder`와 `ZLinkCodecRegistrar`만 구현하는 test 전용
registry를 사용한다.

Raw RouteMesh 회귀에서 드러난 세 비결정 조건도 수정했다.

- Node request test는 `connectPeer` 뒤 admission 완료를 확인한 다음 request를 제출한다.
- Logical Multicast test는 placement 대상인 raw node의 Object Server role을 명시한다.
- User Spot terminal retention test는 wall clock sleep에 의존하지 않는다. Target terminal
  admission에 package-private clock을 주입해 같은 operation replay와 deadline 뒤 새 operation
  거부를 하나의 clock domain에서 검증한다.

Focused M6A·M6B 전체 test는 통과했다. 마지막 JVM aggregate
`./gradlew --no-daemon --max-workers=1 clean check`에서는 core unit 594/594가 통과했다.
Core contract test는 20/22가 통과했다. 정식 spec renumber 뒤 남은
`21-mesh-node.ko.md` 검사는 `13-mesh-node.ko.md`로 수정했고 해당 단언은 focused 재검증에서
통과했다.

Aggregate의 남은 terminal blocker는 공통 E2E에 추가된 scenario와 Java actual fixture 사이의
차이다. Java feature map에는 빠진 ID를 suite별로 기록했다. 이름만 추가해 완료로 처리하지 않으며
actual fixture와 runner selector가 생기기 전까지 JVM E2E parity와 aggregate를 완료로 올리지
않는다. 현재 gap은 `MON-A6`, `OBS-A5`, `OBS-C12`, `PS-F1`~`PS-F5`, `RC-B6`,
`RL-E1`~`RL-E5`, `RL-F1`~`RL-F14`, `RM-A7`, `RM-C10`, `SF-B3`,
`SF-C3`~`SF-C5`, `SF-F1`~`SF-F11`, `SF-G1`~`SF-G3`, `SM-A9`~`SM-A13`,
`SM-B0`, `SM-B10`, `SM-B11`, `SM-G5`다.

## 2026-07-29 .NET reference candidate gate

`main`의 `f14abf3c37f8196cbcc56ad291a28f59d07d261f`에서 .NET reference
candidate를 다시 고정했다.

- Candidate는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/candidate-f14abf3c37f8.json`이다.
  변경 파일은 507개이고 aggregate SHA-256은
  `7a4cb0a199faf02b61b886b8ff128a571400228965db9a5497ed4f15b7735f6c`다.
- Owned-path manifest는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/candidate-f14abf3c37f8-owned-paths.json`이다.
  Candidate와 owned-path manifest의 파일 SHA-256은 각각
  `1bfb1ae0e72f803f7789fe65386ac7f7c8f274b7c0f5aea17ccc5cbe5cd0ef7d`,
  `43b3907a023c3bd5212ac0347eb9f4558df39489e76c425a0b0df083d1e189ae`다.
- `.NET M6-RUNTIME`은 compile, public declaration, internal, resource와
  protocol 5개 required command를 모두 통과했다. Sample·E2E 실행과 skip은
  모두 0이다. 증거는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-f14abf3c37f8.json`이고
  파일 SHA-256은
  `9716ba4c7ee6f035fc8368eed08fe8bbdefc2b9c2d710dc838729e3fda97c350`이다.
- `REMOVE --scope framework:dotnet`은 inventory record 1개에서 제거 대상
  symbol·file·artifact 0으로 통과했다. 증거는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/removal-f14abf3c37f8.json`이고
  파일 SHA-256은
  `2605491f1966e237636cbc983ade209d21992374aec62585b5933658de787bc4`다.
- 같은 candidate와 runtime evidence를 사용한 `ROW-GATE`는
  files 507·commands 5로 통과했다. .NET owned path의 `git diff --check`도
  통과했고 `framework/doc/plan/log/` 변경은 0이다.

이 gate는 runtime·removal·provenance 회귀를 닫는다. External transport
harness를 사용하는 SpotActorTransfer Config 10과 최종 Codex high finding 0
review가 남아 있으므로 `V11-M6-DN-DESIGN-REVIEW`와
`V11-M6-DN-REFERENCE` 상태는 계속 `수정 진행`으로 유지한다.

## 2026-07-29 Logical Multicast lifetime 수정 뒤 .NET reference candidate gate

Logical Multicast request의 owner 수명을 수정한 `main`
`29f7be7ade87c74dcd36657afed3e8baedbcbffa`에서 .NET reference
candidate를 다시 고정했다.

- Candidate는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/candidate-29f7be7ade87.json`이다.
  변경 파일은 507개이며 modified 320개, added 106개, deleted 81개다.
  Aggregate SHA-256은
  `35101468935dae7a9eb4b0c717bdff9c3625a142398090585fc885cac3244d2c`다.
  Candidate 파일 SHA-256은
  `4a730eca5a12a6b4343753ef83ab5792346b65834c9cecb9bd8d41c4d7e3ae5f`다.
- Owned-path manifest는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/candidate-29f7be7ade87-owned-paths.json`이다.
  파일 SHA-256은
  `43b3907a023c3bd5212ac0347eb9f4558df39489e76c425a0b0df083d1e189ae`다.
- `.NET M6-RUNTIME`은 compile, public declaration snapshot, internal runtime,
  resource와 protocol 5개 required command를 모두 통과했다. 모든 command의
  stderr는 비어 있으며 sample·E2E 실행과 skip은 모두 0이다. 증거는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/result-29f7be7ade87.json`이고
  파일 SHA-256은
  `c601ff9439659fb21bf125b3c8a0cec3c04791a101b070d125762970b1da5929`다.
- `REMOVE --scope framework:dotnet`은 inventory record 1개에서 제거 대상
  symbol·file·artifact 0으로 통과했다. 증거는
  `.artifacts/v11/evidence/V11-M6-DN-REFERENCE/removal-29f7be7ade87.json`이고
  파일 SHA-256은
  `2605491f1966e237636cbc983ade209d21992374aec62585b5933658de787bc4`다.
- 같은 candidate와 runtime evidence를 사용한 `ROW-GATE`는
  files 507·commands 5로 통과했다. .NET owned path와 전체 tracked diff의
  `git diff --check`도 통과했고 `framework/doc/plan/log/` 변경은 0이다.

이 gate는 Logical Multicast lifetime 수정 뒤 runtime·public contract·removal과
provenance가 같은 revision에서 통과했음을 확인한다. Reference 승인 여부는 별도
Codex high review와 남은 process E2E gate가 판정한다.

## 2026-07-29 C++ RM-A3 actual-process 증거

Config 1 `RM-A3`를 C++ production runtime과 Redis Location Store로
실행했다. 증거는
`framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-014018-1256449`다.

- Automatic Object Client pair는 20초 동안 80회 조회에서 계속
  `NotRequired`였다. Ready peer와 failure는 0이었다.
- Manual Object Client pair는 20초 동안 79회 조회에서 같은 상태를
  유지했다. 양쪽 endpoint 앞의 TCP proxy는 connection open을 각각
  한 번만 기록했다.
- Object Client RID로 보낸 Node direct call은 다른 RID로 바뀌지 않고
  `request_target_not_found`인 typed `NotFound`로 끝났다.
- Weight `0` RouteMesh Server membership을 가진 Object Client가 Location
  descriptor를 publish한 뒤 정지하자, 다른 Object Client의 public status는
  해당 peer를 `NotConnected`와 `no_ready_peer`로 표시했다. Process를
  재개한 뒤 양쪽 status는 ready connection 하나를 표시했다.
- Runner는 exit code 0으로 끝났다. 종료 뒤 RegistryMessaging process,
  TCP proxy와 test Redis container는 0이었다.

따라서 C++ Config 1 `RM-A3` actual-process gap은 해소됐다.
`V11-M6A-CPP`는 별도 `SA-E2E-08` actual과 C++ lane aggregate가 남아 있어
상태를 `수정 진행`으로 유지한다.

같은 날 Codex `gpt-5.6-sol high` 독립 final review는 candidate에서 P1 한 건을
확인했다. `ZLinkExternalSpotPublishCall<TEvent>`가 runtime operation lease를
public `Async(...)` 호출 범위에서만 유지했지만, Logical Multicast worker는 public
terminal을 `Ok`로 확정한 뒤 native publish를 실행했다. 따라서 public terminal 직후
drain이 시작되면 worker가 publish와 message 정리를 끝내기 전에 runtime dispose가
진행될 수 있었다.

수정은 caller의 ambient operation과 worker의 resource 수명을 분리했다.
`ZLinkFrameworkRuntime.RetainOperationForBackgroundWork()`가 현재 operation 아래에서
별도 active-operation lease를 발급하고, `ZLinkExternalSpotPublishCall<TEvent>`는 이
lease와 message parts를 하나의 idempotent cleanup으로 묶었다. Submit 예외,
cancellation과 worker terminal 경로가 모두 같은 cleanup을 정확히 한 번 실행한다.
Blocked worker가 시작된 뒤 public terminal이 먼저 완료되어도 `StopAsync(...)`는
worker release 전 SpotNode dispose에 도달하지 않는 회귀를 추가했다.
`FullyQualifiedName~LogicalMulticast` focused test는 9/9가 통과했다.

Post-fix review는 authority·relocation ordering과 recovery, session binding
replacement·seal·commit, Message Follow의 exact fence·hop·bounded queue·reply route,
generic Location·Relocation Store SPI의 atomic batch·ambiguous completion·resource
lifetime, one-way submission ownership, Object Client pair connection policy와
Logical Multicast drain 경계를 다시 대조했다. 위 P1은 해소됐고 추가로 조치할
finding은 0이다. 다만 이 수정은
`f14abf3c37f8196cbcc56ad291a28f59d07d261f` 이후 working tree delta이므로, 새
immutable candidate와 같은 revision의 `.NET M6-RUNTIME`·`REMOVE`·`ROW-GATE`를
다시 고정하기 전에는 두 reference 행을 완료로 전환하지 않는다.

## 2026-07-29 Node RM-A3 actual-process 증거

Config 1 `RM-A3`를 Node production runtime과 Redis Location Store로 실행했다.
증거는
`framework/languages/node/e2e/RegistryMessaging/log/20260729-025247-2423445`이다.

- Automatic Object Client pair는 20초 동안 212회 연속 `NotRequired`였고 Ready
  peer와 failure가 없었다.
- Manual Object Client pair도 20초 동안 213회 연속 같은 상태를 유지했다. 각
  endpoint 앞의 TCP proxy가 기록한 connection은 각각 1회다.
- Node direct Send와 Request는 다른 Object Client RID로 전달되지 않았고 typed
  `RequestTargetNotFound`로 끝났다.
- weight `0` RouteMesh Server membership을 가진 Object Client pair는 `Ready`로
  연결되었다. 한 process를 종료한 뒤 남은 process의 public peer 상태는
  `NotConnected`로 바뀌었다. 모든 단계에서 같은 peer RID의 public row는 정확히
  하나였다.
- `RM-A3.result`는 `result=passed`이고 runner exit code는 0이다. 종료 뒤
  ObjectClient process와 TCP proxy는 남지 않았다.
- Node workspace build와
  `location-autoconnect.test.js`·`nestjs-module.test.js`·`drain-control.test.js`
  focused contract test 91개가 모두 통과했다.

따라서 Node Config 1 `RM-A3` actual-process gap만 해소했다. Full Node aggregate의
101개 missing scenario는 별도 row blocker로 유지한다.

## 2026-07-29 .NET Config 10 외부 transport Message Follow checkpoint

SpotActorTransfer `ST-F4`와 `ST-F5`를 Framework 내부 gate 대신 process 밖 TCP proxy로
검증했다. 세 ActorNode의 ROUTER는 서로 다른 loopback 주소에 bind하고 public
`SetAdvertiseHost`로 proxy endpoint를 게시한다. Proxy는 service wire를 해석하지 않고
application payload의 고유 marker만 streaming 검색한다.

`ST-F5`에서 첫 relocation은 끝났지만 두 번째 User Spot source의 committed leave callback이
끝나지 않던 원인을 수정했다. Deferred Join은 제출한 Spot의 serial turn에 게시되지만 기존
구현은 그 turn의 Spot activation context를 복원하지 않았다. Source leave가 같은 Spot queue에
lifecycle 작업을 다시 제출해 현재 turn을 기다렸다. Deferred Join이 owner turn에서 실행될 때
제출 시점의 Spot activation context를 함께 복원하도록 수정했다.

외부 proxy도 보강했다. Marker가 없는 control frame의 마지막 byte가 marker prefix와 우연히
일치하면 다음 TCP read까지 byte 하나를 보관하던 문제를 제거했다. 실제 marker가 read 경계에서
나뉘는 경우는 계속 탐지하되, 짧은 유예 동안 뒤 byte가 오지 않으면 일치하지 않은 partial
prefix를 전달한다. Chain operation의 capture를 확인한 뒤 expiry operation을 제출해 같은
connection에서 검증 순서를 고정했다.

- Focused unit: `DeferredActorJoinContractTests` 2/2 PASS
- `ST-F5` 3회 연속 PASS:
  - `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-032242-2945859`
  - `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-032307-2947057`
  - `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-032331-2948131`
- `ST-F4` 회귀 PASS:
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-032359-2949302`
- Timeout과 assertion은 변경하지 않았다.
- 원인 분석에만 사용한 source cleanup stage marker는 모두 제거했다.

이 checkpoint는 Actor one-way multi-hop, final owner exactly-once, 이전 owner handler 0회와
Message Follow 만료 뒤 request의 public `Unavailable` terminal을 외부 transport에서
확인한다. 기존 `ST-I4~I6` internal delivery gate와 friend assembly는 아직 남아 있다.
해당 selector를 외부 fixture로 바꾸거나 제거하고 내부 hook을 삭제하기 전에는 Config 10
전체와 `.NET` final reference row를 완료로 전환하지 않는다.

## 2026-07-29 .NET Config 10 internal delivery hook 제거 checkpoint

`ST-I4~I6`도 process 밖 TCP proxy로 교체했다. 기존 fixture가 사용하던 production
`IZLinkActorTransportDeliveryGate`, operation ID metadata, reply admission override와
ActorNode friend assembly를 삭제했다. E2E의 `/transport-delivery/*`와
`/reply-admission/*` endpoint, response DTO와 service 등록도 함께 제거했다.

네 번째 ActorNode는 Object Client role만 등록한다. Actor·Spot placement 후보가 아니며
application delivery를 보류해도 세 Object Server 사이의 relocation control traffic을
차단하지 않는다. Proxy는 다음 두 동작만 수행한다.

- application marker가 포함된 TCP delivery를 보류하고 release한다.
- marker를 포함한 delivery와 같은 connection의 reverse bytes를 별도 gate로 보류한다.

Proxy는 Framework protocol, authority, generation과 reply capability를 해석하지 않는다.
ST-I5의 두 correlation request는 서로 다른 Object Client connection에 보류한다. TCP
stream 안에서 불가능한 byte 재정렬을 fixture가 흉내 내지 않고, 두 connection의 release
순서만 바꾼다.

Actual-process 최종 증거는 다음과 같다.

- `ST-I4`: `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-035557-3692122`
- `ST-I5`: `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-035615-3693303`
- `ST-I6`: `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-035538-3690993`
- `ST-F4` 회귀:
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-035740-3699697`
- `ST-F5` 회귀:
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-035809-3700993`

다섯 selector의 runner terminal은 모두 `result=passed`다. Timeout과 assertion budget은
늘리지 않았다. Internal gate 제거 뒤에도 Actor request는 Location Store 조회에 사용한
시간을 전체 timeout에서 차감한다. 느린 authority read 회귀 test 1개와
`DeferredActorJoinContractTests` 2개가 통과했다.

이 checkpoint는 현재 연결한 Actor Message Follow case의 public-only 완료 증거다.
Spot·recovery·duplicate·generation·loop·hop·bounded-capacity case는 feature map에
별도 gap으로 유지한다. 따라서 Config 10 전체와 `.NET` final reference row는 아직
완료로 전환하지 않는다.

## 2026-07-29 JVM·C++ RM-A3와 C++ DeliveryDispatch checkpoint

JVM과 C++의 RouteMesh Channel registration을 Client와 Server role로 분리했다. Descriptor와
connection requirement에는 Server role만 게시한다. 따라서 양쪽 MeshNode가 Object Client-only이고
양쪽 모두 RouteMesh Channel Server가 없을 때만 `NotRequired`다. Client-only Channel은 연결
필요성을 만들지 않으며 weight가 0인 Server Channel은 server membership을 유지한다.
Automatic과 Manual 연결에 같은 판정을 적용하고 `NotRequired`와 `NotConnected`를 구분한다.

- JVM core 602/602, RM-A3 focused 89/89, Spring monitoring focused test가 통과했다.
- Kotlin·Spring과 JVM ObjectClient fixture compile이 통과했다.
- C++ Registry Provider의 route handler를 exact `route_message_context_t`로 맞췄다.
  ClientServer 구성은 `client()`와 `server()` role builder를 사용한다. 제거된 parent builder API나
  새 public API는 추가하지 않았다.
- C++ Registry Provider와 ObjectClient target, `test_cpp_framework_m6a_runtime`,
  `test_cpp_framework_contract_headers` build와 실행이 통과했다.
- Config 1 `RM-A3` actual-process runner도
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-033513-3283717`에서
  exit code 0으로 통과했다. Automatic과 Manual pair를 각각 80회 조회하는 동안
  `NotRequired`, ready peer 0과 failure 0을 유지했다. Manual TCP proxy는 양방향 connection open을
  각각 한 번 기록했다. Weight 0 Server를 중지하면 `NotConnected`가 되고 재개하면 ready connection
  하나로 복구됐다.
- Config 1 `RM-C8`은 public ClientServer channel builder가 listener의 `MaxMessageSize`를
  설정하도록 요구한다. 현재 .NET·C++ exact ClientServer role builder에는 이 설정이 없다.
  C++ `mesh_node_socket_config_t`는 RouteMesh ROUTER listener의 설정이므로 별도 ClientServer
  DEALER·ROUTER topology에 재사용할 수 없다. 이는 C++ 구현 한 건이 아니라 공통 E2E와 다섯 언어
  exact interface 사이의 public contract gap이다.
- C++ Provider와 Consumer에서 socket에 적용되지 않던 `maxMessageSize`·`clientMaxMessageSize`
  fixture 입력을 제거했다. Runner도 max-size 하위 흐름을 default socket으로 실행해 통과한 것처럼
  보고하지 않고 contract gap에서 명시적으로 중단한다. 제거된 per-role API나 새 public API는
  추가하지 않았다.
- Consumer와 Workflow의 legacy ClientServer builder를 exact `client()`·`server()`·`connect()`로,
  RegistryMessaging Client의 제거된 HTTP `.async<T>()` terminal을 `submit<T>()`로 이관했다.
  Provider·Consumer·Workflow·Client 네 target은 build에 통과했다.
- 최초 actual
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-034652-3516691`의 HTTP 500은
  Consumer 내부 ClientServer request가 `TimeoutException`으로 끝난 결과였다. RM-C8 Consumer가
  Config 1의 Location Store automatic discovery 대신 manual endpoint를 사용했지만, current
  ClientServer location runtime은 이 endpoint를 ready target으로 만들지 않았다.
- RM-C8 fixture를 Config 1의 Redis Location Store automatic discovery 구성으로 되돌렸다.
  Actual `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-035646-3695165`에서
  1 B, 4 KiB, 256 KiB와 1 MiB payload의 length·SHA-256 왕복 및 후속 정상 request가
  재시도 없이 통과했다. Client stdout은 `scenario RM-C8 passed`와
  `registry-messaging e2e result=passed`를 기록한다.
- Runner는 정상 payload 하위 흐름 뒤 ClientServer listener `MaxMessageSize` contract gap을
  명시하고 non-zero로 끝난다. 따라서 정상 request precondition만 actual-process로 고정했으며
  전체 RM-C8 완료로 세지 않는다. 공통 spec과 다섯 exact interface에서 ClientServer listener
  제한의 단일 공개 설정 경계를 정한 뒤 max-size runtime과 actual process를 함께 고정해야 한다.

C++ DeliveryDispatch도 공통 sample 계약에 맞췄다. Client JSON wire에서 `ActorRef`, `NodeRid`와
session route를 제거했고 낡은 Actor 조회·생성용 route packet과 handler를 삭제했다.
`ActorManager.GetOrCreate`가 반환한 Ready ActorRef는 Framework session binding 내부에서만
사용한다. Sample parity 54/54와 DeliveryDispatch executable 6개 build가 통과했다.

## 2026-07-29 JVM RM-A3 actual-process 재고정

HEAD `40a937e67b2146a75ac4f23b77b55d1b166eee5c`의 public Channel role 변경을 포함한
working tree에서 Java Config 1 `RM-A3`를 production runtime과 실제 Redis Location Store로
다시 실행했다. 증거는
`framework/languages/java/e2e/RegistryMessaging/logs/20260729-033208-3236609`이다.

- Automatic Object Client pair는 Client-only Channel을 각각 등록한 상태에서도 20초 동안
  79회 연속 `NotRequired`, Ready peer 0과 failure 0을 유지했다.
- Manual pair도 같은 조건으로 20초 동안 79회 연속 같은 상태를 유지했다. endpoint 앞의
  TCP proxy가 기록한 connection은 양쪽 각각 1회다.
- Object Client RID를 대상으로 한 Node direct Send와 Request는 모두 typed
  `REQUEST_TARGET_NOT_FOUND`로 끝났다.
- weight `0` RouteMesh Server Channel을 한쪽에 등록하면 pair는 `Ready`가 됐다. 상대
  process를 종료한 뒤 public peer 상태는 `NotConnected`로 바뀌었다.
- `RM-A3.result`는 `result=passed`이고 runner exit code는 0이다. 종료 뒤 ObjectClient와
  TCP proxy process는 남지 않았다.

따라서 JVM Config 1 `RM-A3` actual-process row는 최신 public contract 기준으로 완료했다.
JVM 전체 aggregate의 다른 scenario gap은 별도 row에서 계속 추적한다.

## 2026-07-29 C++ RM-C2 actual-process 증거

Config 1의 미완료 P0 중 C++ exact RouteMesh Node direct API와 public topology snapshot으로
검증할 수 있는 `RM-C2`를 먼저 완료했다. `RM-A1`은 public monitoring 구현과 fixture 구조,
`RM-A2`·`RM-C3`은 manual ClientServer ready-target runtime, `RM-B3`은 crash fixture가
선행되어야 하므로 이 bounded 작업보다 범위가 크다.

- 이전 scenario는 topology Ready를 확인한 뒤에도 의미 request를 30초 동안 반복했다.
  이를 제거하고 public topology를 최대 3초 polling한 뒤 targeted request를 한 번만 제출한다.
- Target `api-b`의 reply와 evidence가 일치하고 `api-a`에는 같은 marker가 기록되지 않는다.
- 미등록 RID는 이전 fixture 기대값 `RouteNotConnected`가 아니라 current public contract의
  `RequestTargetNotFound`로 끝난다.
- Actual 증거는
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-040050-3900121`이다.
  Client는 `scenario RM-C2 passed`와 `registry-messaging e2e result=passed`를 기록했고
  runner exit code는 0이다.
- Provider·Client와 test Redis container는 runner 종료 뒤 남지 않았다. Public API,
  compatibility helper, skip과 timeout 확대는 추가하지 않았다.

따라서 C++ Config 1 `RM-C2` actual-process gap은 해소됐다.

## 2026-07-29 C++ RM-A1 automatic request와 monitoring gap

C++ Config 1 `RM-A1`에서 current public API로 구현할 수 있는 automatic discovery request를
actual process로 고정했다. Runner는 두 Provider와 endpoint를 지정하지 않은 Consumer에 같은
Redis Location Store와 key prefix를 등록한다. Client는 Consumer HTTP endpoint를 통해 첫 의미
request를 한 번만 제출한다.

- Forward:
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-040424-3961146`
- Reverse:
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-040454-3963029`
- Fixed shuffle `1701`:
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-040458-3963605`
- 세 실행 모두 선택된 `api-a` 또는 `api-b`의 reply와 Provider evidence를 확인하고
  `scenario RM-A1 passed`, `registry-messaging e2e result=passed`와 exit code 0을 기록했다.
  Request retry, skip, settle sleep과 timeout 확대는 없다.

공통 RM-A1 전체 완료 조건은 아직 남아 있다.

- C++ fixture의 profile channel은 ClientServer topology를 사용한다. Exact 문서에는
  `client_server_runtime_t`와 ready Server snapshot이 있지만 current C++ source에는 구현이 없다.
  RouteMesh status나 Location Store provider query는 다른 topology 또는 저장소 내부 상태이므로
  대체 evidence로 사용하지 않았다.
- 공통 RM-A1은 lifecycle별 automatic RID prefix를 요구한다. Exact 문서의
  `set_automatic_routing_id_prefix(...)`도 current C++ source에 구현되어 있지 않다.

따라서 automatic discovery request 경로는 완료했지만 row 상태는 public contract 구현 gap으로
유지한다. 두 exact public surface를 구현하고 ready Server 2개와 automatic RID를 public snapshot으로
확인하기 전에는 C++ RM-A1 전체 완료로 전환하지 않는다.

## 2026-07-29 .NET Config 10 ST-H1 Actor handler checkpoint

Config 10의 남은 P0 가운데 Deferred Join의 request snapshot과 Actor queue 순서를 먼저
검증했다. E2E는 production hook이나 내부 frame을 사용하지 않는다. Actor handler가 public
`JoinSpot(...).Timeout(...).Defer()`를 호출한 뒤 application request 객체를 변경하고,
Object Client-only process가 같은 Actor에 public one-way message를 제출한다.

- Cross-node target의 joined callback을 application gate로 막는 동안 source와 target의
  후속 Actor handler는 모두 실행되지 않는다.
- Handler terminal 전 target admission은 0건이다. 따라서 target I/O는 handler의 마지막
  continuation보다 먼저 시작되지 않는다.
- `Defer()` 등록 시점부터 후속 direct frame을 capture하고, cross-node Handoff가 시작되면
  해당 frame을 기존 accepted journal로 승격한다. 이전 owner에서는 실행하지 않는다.
- Target은 completion callback이 성공할 때까지 imported journal을 dispatch하지 않는다.
  Evidence 순서는 `success_reply → handoff_packet`이며 target에서 정확히 한 번 실행된다.
- Target admission에는 변경 전 scenario·SpotId·mode만 전달된다. `Defer()` 뒤 변경한
  `ST-H1-MUTATED`, `mutated-target`, `reject`는 관찰되지 않는다.
- ActorNode와 Client build는 warning 0, error 0이다. Deferred Join focused test 7/7과
  Actor Handoff focused test 63/63이 통과했다. Actual-process 증거는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-041951-5242`이다.

Actor handler의 cross-node ST-H1 핵심 경로는 완료했다. User·Entry Spot의 Spot·Timer handler,
absolute deadline과 다른 Actor·Spot lane 진행 반복은 같은 ST-H1 matrix의 남은 항목으로
feature map에서 계속 추적한다.

## 2026-07-29 .NET Config 10 ST-A2 Deferred Join rejection checkpoint

ST-A2를 current Deferred Join terminal 계약으로 전환했다. Actor request handler의 HTTP
응답은 Join 결과가 아니라 `Defer()` registration 성공만 나타낸다. Target admission의 typed
거절은 Actor의 public `OnJoinCompletedAsync` callback에서 `Rejected`와 application reply로
관찰한다.

- Same-node target은 `reject` mode로 admission을 실행하고 typed reply의
  `Accepted=false`와 target SpotId를 callback에 전달한다.
- Rejected terminal 뒤 Actor의 ObjectGeneration과 owner node는 생성 시점과 같다.
- Source leave, target joined, authority commit, Capture, Restore와 User Spot Actor handler는
  모두 0건이다.
- Public global Actor one-way는 source Entry Spot Actor handler에서 state 12로 정확히 한 번
  실행된다. Evidence 순서는 `admission → reject_reply → entry_handoff_packet`이다.
- ActorNode와 Client build는 warning 0, error 0이다. Actual-process 증거는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-042702-217477`이다.

ST-A3의 joined callback 대기와 moving dispatch 계약은 이 checkpoint에 포함하지 않았다.

## 2026-07-29 .NET Config 10 ST-A3 local membership barrier checkpoint

ST-A3를 current Deferred Join과 same-node membership ordering에 맞췄다. 이전 scenario는
target `OnJoinedActorAsync`가 끝나기 전에 source leave를 기다렸다. Runtime은 joined callback
성공 뒤에 leave와 completion을 진행하므로 scenario 자체가 gate를 해제하지 못하고 deadline을
소진했다. Source leave를 선행 조건에서 제거하고 실제 membership barrier를 검증했다.

- Actor와 target User Spot이 같은 node에 배치됐는지 public ref로 먼저 확인한다.
- `Defer()` registration 뒤 target admission과 `joined_wait`를 확인한다. Gate가 닫힌 동안
  leave, joined, completion callback과 Actor handler는 모두 0건이다.
- Source application endpoint가 public Actor request를 제출했다는 `probe_submitted` evidence를
  확인한 뒤에도 request는 완료되지 않고 handler evidence는 0건이다.
- Gate release 뒤 queued request와 별도 follow-up request는 target Spot에서 state 13으로
  각각 정확히 한 번 처리된다. Evidence 순서는
  `admission → joined_wait → joined_released → joined → success_reply → queued request → follow-up request`다.
- Capture, Restore, transfer in/out, relocation completed와 Message Follow evidence는 0건이다.
- ActorNode와 Client build는 warning 0, error 0이다. Deferred Join focused 7/7과
  Actor Handoff focused 63/63이 통과했다. ST-A2 actual regression도
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-043656-565185`에서 통과했다.
- ST-A3 actual-process 증거는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-043746-628620`이다.

Private hook, reflection, retry와 timeout 확대는 추가하지 않았다.

## 2026-07-29 Node.js Config 2 SM-F5 actual-process 증거

Node.js SpotService의 Play fixture를 current SpotId·manager·handler context·RouteMesh Channel role
계약으로 전환했다. In-memory Spot route store와 fixed owner assertion은 제거했다. User Spot owner는
Location Store와 node-wide placement가 선택한다.

- Actual 증거는
  `framework/languages/node/e2e/SpotService/log/20260729-041927-3968`이다.
- User Spot owner는 `play-b`였다. Client는 owner를 응답에서 확인한 뒤 반대 node인 `play-a`에서
  global SpotId request를 제출했다. `play-b`의 state handler가 값 `13`을 기록했다.
- `play-a`의 같은-process Channel Server도 remote Server와 같은 weight 후보에 포함했다. Handler를
  직접 호출하지 않고 application mailbox를 거쳐 request와 reply를 처리했다.
- Spot을 owner에서 닫은 뒤 global SpotId request는 typed failure로 끝났다. 같은 caller의
  ChannelName request는 계속 성공했고 `sm-f5-after-close` evidence가 남았다.
- Binary transport reply route를 Node RID 문자열로 바꾸지 않고 raw bytes로 mailbox에 보존했다.
  Remote Channel reply가 원래 request operation으로 돌아오는 것을 actual process로 확인했다.
- Location Store가 commit한 remote Spot authority는 polling을 기다리지 않는다. 첫 public Spot
  request가 전달한 current fence를 source stateful runtime에 즉시 연결한다. peer lifecycle과 더
  최신 authority fence는 계속 검증한다.

검증은 Node workspace build, M6A focused test 12/12, M6B focused test 39/39와 `SM-F5` actual-process
PASS다. Runner 종료 뒤 Play·Client process와 test Redis container는 남지 않았다.

## 2026-07-29 C++ Config 1 RM-A2 checkpoint

C++ ClientServer runtime이 exact `client().connect(endpoint)`를 ready target으로 만들지 않던 gap을
수정했다. Manual endpoint는 최초 handshake에서 Server RID와 lifecycle generation을 고정한다.
Location Store에서 같은 endpoint의 descriptor를 발견하면 기존 연결을 automatic key로 옮겨
중복 연결을 만들지 않는다. Manual endpoint가 있는 client role도 location auto-connect host가
ClientServer runtime을 시작한다.

- `test_cpp_framework_m6a_runtime`은 manual handshake에서 identity를 고정한 뒤 send와 request/reply를
  완료한다.
- RegistryMessaging Client·Consumer·Provider target build는 통과했다.
- Actual `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-042414-96804`에서 최초 manual
  request와 topology 변경 중 in-flight request는 통과했고, consumer는 `api-a`와 `api-b`
  descriptor를 모두 발견했다.
- 두 ready target 사용 assertion은 아직 실패한다. 기존 weighted selector는 같은 weight를 연속
  구간으로 선택하므로 16개 continuity request가 한 target만 선택했다.

검증하지 않은 smooth weighted selector 변경은 제외했다. `RM-A2`를 완료하려면 100:300 비율,
weight 0 제외, draining 제외와 bounded selector state를 focused test로 먼저 고정한 뒤
두 target actual assertion을 다시 통과해야 한다. 이 checkpoint를 actual 완료로 판정하지 않는다.

## 2026-07-29 C++ Config 1 RM-A2 완료 증거

앞 checkpoint의 weighted selection gap을 bounded smooth weighted selector로 해소했다. Selector는
ready·Serving·positive-weight ClientServer member만 입력으로 받는다. Candidate가 바뀌면 제거된
member의 state를 즉시 삭제하고, 각 credit의 절댓값을 현재 active weight 합계 이내로 제한한다.

- Focused test는 `api-a=300`, `api-b=100`을 400회 선택해 정확히 `300:100`을 기록한다.
- Weight `0` member는 선택하지 않고 selector state에도 남기지 않는다.
- Draining member를 candidate 입력에서 제외한 뒤 Serving member만 32회 선택한다. 이때 state는
  한 member만 유지하고 credit 절댓값은 active total `300`을 넘지 않는다.
- RegistryMessaging Client·Consumer target build와 `test_cpp_framework_m6a_runtime`이 통과했다.
- Actual 증거는
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-043646-565086`이다. 최초 manual
  request, automatic descriptor 추가 중 in-flight request, 이후 `api-a`·`api-b` 두 target 처리와
  각 Provider evidence가 모두 통과했고 runner exit code는 0이다.

따라서 C++ Config 1 `RM-A2` actual-process gap은 완료했다. Manual connection을 automatic
descriptor key로 옮기는 handoff와 최초 handshake identity fence는 그대로 유지한다.

## 2026-07-29 .NET solution과 sample compile gate

`framework/languages/dotnet/Zlink.Framework.sln`을 current main source로 build했다.
Bingo, TicTacToe, ZoneWorld, DeliveryDispatch, SupportChat, ShoppingMall과 GameQuest를
포함한 모든 sample project가 compile됐다.

처음 build에서 sample 밖의 낡은 contract 참조 5건을 확인해 다음과 같이 정리했다.

- 제거된 `Zlink.Framework.Contracts.Eventing` namespace의 사용하지 않는 import를
  test application과 LocationMessaging fixture에서 제거했다.
- HTTP request builder에 제거된 `Yield<T>` 호출을 직접 되살리지 않았다. 공통 계약대로
  `RunIoWorker(...)` 안에서 HTTP `Async<T>`를 실행하고 Worker call의 `Yield`로 Spot
  turn을 반납한다.
- `ZLinkHttpServerClient` 공개 주석에서 제거된 `Submit`·`Yield` 설명을 없애 현재
  one-way `Async` terminal과 맞췄다.

수정 뒤 solution 전체 build는 warning 0, error 0으로 통과했다. Sample regression은
126/126이 통과했다. 새 Config 12 runner와 ST-H1 scenario를 정확한 inventory에 포함했고
assertion을 제거하거나 완화하지 않았다.

ChannelEgressRouting fixture도 raw `HttpClient`와 자체 JSON config writer를 제거했다.
정식 `ZLinkHttpClient`와 공통 `write_role_config.py`를 사용하며, shared writer가
Route Server·Client collection을 정확히 기록하도록 보강했다. Client build와 실제
`CH-E2E-01`이 통과했다. Actual 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-044618-974448`이다.

## 2026-07-29 C++ Config 1 RM-A4 public messaging 증거

C++ `RM-A4`의 persistent Consumer replacement 경로를 current public ClientServer messaging으로
실행했다. 이전 fixture가 조회한 `/locations/peers`는 별도 RouteMesh의 fixed RID row이며
ClientServer replacement 상태가 아니므로 제거했다.

- Actual 증거는
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-044350-867543`이다.
- Consumer를 유지한 채 v1 request와 `api-a-v1` evidence를 먼저 확인했다.
- v1 process 종료 후 다른 endpoint에서 v2를 시작했다.
- Retry 없이 제출한 첫 request부터 연속 20개가 모두 `api-a-v2` reply로 끝났고 v2 evidence가
  정확히 20개 기록됐다.
- Client target build, runner syntax와 actual runner exit code 0을 확인했다.

Canonical `RM-A4` 전체 완료 조건은 아직 남아 있다. 새 process의 automatic RID suffix, old RID
제외와 new RID Ready public status, `Shutdown` terminal `Stopped/None`, active descriptor conflict
반복을 검증해야 한다. Exact C++ 문서에는
`mesh_node_builder_t::set_automatic_routing_id_prefix(...)`와 `client_server_runtime_t`가 있지만
current source에는 두 public surface가 없다. Fixed RouteMesh RID 또는 Location Store provider
query를 evidence로 사용하지 않는다. 따라서 public messaging subflow만 통과로 기록하고 전체
`RM-A4`는 public-contract implementation gap으로 유지한다.

## 2026-07-29 C++ Config 1 RM-A6 public messaging 증거

C++ `RM-A6` fixture를 같은 Location Store와 persistent Consumer 하나를 사용하는 구성으로
수정했다. Consumer는 `api`와 `workflow` ClientServer client role을 함께 등록한다. Provider 자체의
HTTP endpoint로 각 local Server를 호출하던 이전 fixture는 channel 격리를 검증하지 못하므로
사용하지 않는다.

- Actual 증거는
  `framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-045601-1343518`이다.
- Consumer의 `api` request는 api Provider reply와 evidence에만 기록됐다.
- Consumer의 `workflow` request는 workflow Provider reply와 evidence에만 기록됐다. 양쪽 evidence를
  대조해 반대 ChannelName payload가 섞이지 않았음을 확인했다.
- `api-b` process를 종료한 뒤 남은 `api-a` request와 workflow request가 모두 성공했다.
- Workflow Provider process를 종료한 뒤에도 같은 Consumer의 `api-a` request가 성공했다.
- Client·Consumer target build, runner syntax, `git diff --check`와 actual runner exit code 0을
  확인했다. 의미 request 재시도와 private Store 조회는 없다.

Canonical `RM-A6`의 MeshName별 status sequence와 ready target count 검증은 남아 있다. Exact C++
문서의 `client_server_runtime_t`가 current source에 구현되어 있지 않다. 별도 RouteMesh status나
Location Store provider query는 ClientServer status의 대체 evidence가 아니므로 사용하지 않는다.
Public messaging 격리 subflow만 통과로 기록하고 전체 row는 public status implementation gap으로
유지한다.

## 2026-07-29 Node.js Config 2 SM-F3 actual-process 증거

`SM-F3` fixture는 fixed Node RID를 사용하지 않는다. Spot 생성 결과가 반환한 owner RID를
Node direct target으로 사용한다. Mesh-level Node direct handler와 ChannelName handler는 current
public builder가 제공하는 서로 다른 등록 위치에 배치했다.

- Mesh-level `addRequestHandler(...)`는 RID direct request를 처리한다.
- `channel(...).server().addRequestHandler(...)`는 ChannelName request를 처리한다.
- global SpotId request는 Location Store가 가리키는 Spot handler를 처리한다.
- 세 요청은 같은 packet name을 사용해도 각각 `node-echo`, `channel-echo`,
  `spot-state-request` evidence를 정확히 한 번 남긴다.

Actual 증거는
`framework/languages/node/e2e/SpotService/log/20260729-043537-560993`이다.
Play와 Client build, M6A focused runtime test 12/12와 `SM-F3` actual-process가 통과했다.
이전 실패는 Mesh Channel handler를 Node direct handler로 재사용한 fixture 등록 오류였으며,
새 public API나 runtime compatibility 경로는 추가하지 않았다.

## 2026-07-29 Node.js Config 2 SM-F4 actual-process 증거

`SM-F4`를 current canonical 절차 전체로 전환했다. Fixture는 Spot 존재 여부를 `find()`로 먼저
판단하지 않고 public SpotId send와 request를 직접 제출한다.

- 존재하지 않는 SpotId request는 `requestTargetNotFound`로 끝난다.
- 같은 SpotId send는 `spotRouteNotFound`로 끝난다. Instance activation이나 Spot 초기화 evidence는 없다.
- User Spot을 닫고 같은 SpotId로 다시 만들면 ObjectGeneration이 증가한다.
- 이전 `SpotRef` exact close는 `spotGenerationStale`로 끝난다.
- 새 `SpotRef` exact close는 성공한다. 이전 close가 새 incarnation을 변경하지 않았음을 확인했다.
- stale close 뒤 ChannelName과 dynamic RID direct request가 각각 정상 reply를 반환한다.

Actual 증거는
`framework/languages/node/e2e/SpotService/log/20260729-044819-1007998`이다.
Play와 Client build, M6B focused runtime test 39/39와 `SM-F4` actual-process가 통과했다.

Public Spot manager가 create 또는 close로 authority를 변경하면 같은 host의 Spot route cache에서
해당 SpotId를 무효화하도록 기존 manager 책임 경계도 보강했다. Retry, timeout 확대, raw frame과
private runtime hook은 사용하지 않았다.

추가로 recreate 직후 새 incarnation에 global SpotId request를 제출하면 local raw registry fence가
terminal result 102와 failure errno 21을 반환하는 별도 구현 gap을 확인했다. Canonical `SM-F4`는
새 incarnation 유지 여부를 current `SpotRef` exact close로 확인하고 ChannelName·RID direct 지속을
요구하므로 scenario 완료와 분리했다. 이 gap은 raw registry가 복원한 generation과 Location authority가
전달한 generation의 정렬을 focused test로 고정한 뒤 해결해야 한다.

## 2026-07-29 RouteMesh Object Client connection 계약 재검증

Object role과 RouteMesh Channel role은 서로 독립이다. Object Client는 Actor·Spot의
배치 target이 아니지만 RouteMesh Channel Server로는 등록할 수 있다. 공통
`07-channel-topology.ko.md`에 남아 있던 반대 설명을 제거해 다른 공통 spec 및 다섯
언어 exact interface와 뜻을 맞췄다.

`.NET` Config 1 `RM-A3`를 현재 main에서 다시 실행했다.

- 양쪽 모두 Object Client이고 양쪽 모두 RouteMesh Channel Server membership이 없으면
  `NotRequired`이며 Ready peer는 0이다.
- 어느 한쪽에 RouteMesh Channel Server membership이 있으면 weight가 `100` 또는 `0`이어도
  connection이 필요하며 Ready peer를 유지한다.
- Channel Client role, ClientServer와 classic fanout role은 이 판정에 포함하지 않는다.
- Object Client를 Node direct target으로 지정하면 peer를 새로 만들지 않고 `NotFound`로 끝난다.

Actual-process 증거는
`framework/languages/dotnet/e2e/LocationMessaging/logs/20260729-043637-564415`이다.
Fixture에서 제거된 `Contracts.Eventing` namespace의 사용하지 않는 import도 삭제해
현재 public contract source로 build되도록 정리했다.

## 2026-07-29 .NET Config 10 ST-B1 remote stateful relocation checkpoint

ST-B1 fixture의 생성 순서를 current public placement 계약에 맞췄다. Actor를 먼저 만든 뒤
그 owner와 다른 node에 User Spot을 배치한다. 특정 Node RID를 application request에 넣지 않고
node-wide placement weight와 public manager만 사용한다.

- Deferred Join handler가 끝나기 전에 Object Client-only process가 global Actor ID로 one-way
  packet을 제출한다. Handler terminal 전에는 source와 target handler가 모두 0건이다.
- Source adapter가 4 KiB application state를 포함한 payload를 Capture하고 target adapter가
  같은 byte 수와 SHA-256의 payload를 Restore한다.
- Public provider SPI를 감싼 observer는 opaque Relocation Store blob만 측정한다. 같은 reference
  hash·byte 수·payload hash를 가진 write와 read를 확인하며 envelope이나 provider key는 해석하지
  않는다.
- ObjectGeneration은 생성 때 발급된 값을 유지하고 owner만 target으로 바뀐다. Relocation 뒤
  다른 process의 global Actor request는 target User Spot에서 state version 21을 조회한다.
- Handler terminal 전에 수락한 one-way는 target에서 정확히 한 번 처리되고 source에서는
  처리되지 않는다. Application evidence 순서는
  `admission → Restore → transfer_in → joined → success_reply → accepted packet replay → follow-up request`다.

Actual-process 단독 증거는
`framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-044949-1108908`이다.
ST-H1과 ST-B1을 같은 fresh process에서 실행한 focused regression도
`framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260729-045056-1141323`에서 통과했다.
ST-A1·A2·A3·B1·B3·B4 묶음 실행은 기존 ST-A1 public authority commit evidence gap에서
중단됐으며 새 ST-B1 실패로 계산하지 않는다.

`source_sealed`, `journal_staged`, `prepared`, `source_cleanup`, `completed`, `route_ack`,
`steady_normalized`, `ready`, `admission_open`처럼 public application·provider 경계에서 직접
구분할 수 없는 private phase의 전체 순서는 이 checkpoint로 완료를 주장하지 않는다. Private hook,
reflection, raw frame, retry와 timeout 확대는 추가하지 않았다.

## 2026-07-29 .NET Config 12 CH-E2E-02 actual-process 증거

ChannelEgressRouting의 기존 selector를 current `ZLinkHttpClient`와 typed config runner로
실행했다. Session의 RouteMesh handler가 `audit.record` one-way를 먼저 제출하고
`workflow.command` ClientServer request를 이어서 실행한다. 원래 reply는 audit와
선택된 workflow server 결과를 순서대로 보존했다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045524-1297423`이다.
Server와 Client build는 warning 0, error 0이며 runner exit code는 0이다.

## 2026-07-29 .NET Config 12 CH-E2E-10 actual-process 증거

Result-free ClientServer send를 public call로 제출했다. Weight가 양수인 두 Server가
선택 구간에서 handler를 각각 실행했고, 두 process의 handler 합계는 제출 수와
정확히 일치했다. Subscriber별 처리 결과를 terminal 값으로 반환하지 않는다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045617-1359918`이다.

## 2026-07-29 .NET Config 12 CH-E2E-11 actual-process 증거

Session process가 target RID와 endpoint를 받지 않고 `game.api` ChannelName만 사용해
remote Api request와 send를 처리했다. 선택한 remote handler의 reply와 evidence를
확인했으며 Node direct 경로로 우회하지 않았다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045637-1365103`이다.

## 2026-07-29 .NET Config 12 CH-REG-01 actual-process 증거

Session과 Play가 같은 RouteMesh의 서로 다른 ChannelName으로 양방향 request를 실행했다.
각 handler는 요청이 들어온 ChannelName과 reply context를 보존했으며, ClientServer나
Node direct 경로로 바뀌지 않았다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045702-1376921`이다.

## 2026-07-29 .NET Config 12 CH-REG-06 actual-process 증거

정상 RouteMesh와 ClientServer request를 application retry 없이 각각 한 번 제출했다.
두 request는 모두 1초 안에 reply로 완료됐다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045757-1462397`이다.

## 2026-07-29 .NET Config 12 CH-E2E-06 actual-process 증거

두 startup validation을 별도 child process에서 실행했다. RouteMesh와 ClientServer가
같은 ChannelName을 등록한 경우와 동일한 ClientServer Client 역할을 두 번 등록한 경우
모두 socket bind 전에 configuration error로 종료됐다.

Actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-045555-1335584`이다.
Server와 Client build는 warning 0, error 0이며 runner exit code는 0이다.

## 2026-07-29 .NET Config 12 CH-REG-07 sample topology checkpoint

공통 sample topology fixture와 일곱 .NET sample의 실제 source를 대조했다. 첫 실행에서
TicTacToe의 ClientServer ChannelName과 DeliveryDispatch·ShoppingMall의 RouteMesh 이름이
공통 sample 계약과 다른 것을 확인했다. Assertion을 완화하지 않고 sample 구성을 수정했다.

- TicTacToe API ChannelName은 `tictactoe.api`다.
- DeliveryDispatch는 courier와 customer object routing을 각각
  `deliverydispatch.courier`, `deliverydispatch.customer` RouteMesh로 분리한다.
- ShoppingMall workflow RouteMesh는 `shoppingmall.workflow`다.

DeliveryDispatch solution build는 warning 0, error 0이다. 일곱 sample의 RouteMesh와
ChannelName을 다시 검사한 actual-process 증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-050208-1671604`이다.

## 2026-07-29 .NET Config 12 CH-REG-09 placement input checkpoint

일곱 .NET sample source 전체를 검사해 application placement 입력에
`PreferredNodeRid`와 `PreferredRoutingId`가 남아 있지 않음을 확인했다. Actual-process
증거는
`framework/languages/dotnet/e2e/ChannelEgressRouting/logs/20260729-050242-1677966`이다.

## 2026-07-29 C++ Config 1 RM-A7 구현 checkpoint

C++ global Actor·Spot identity의 상태 전이를
`test_cpp_framework_m6b_runtime`에 보강했다.

- 서로 다른 initial Mesh에서 같은 Actor ID와 stable type을 생성하면 하나의 create attempt에
  합류한다.
- 같은 Actor ID에 다른 stable type을 사용하면 `type_mismatch`다.
- Store가 예약한 같은 User Spot reference도 하나의 attempt에 합류하고, 다른 stable type을
  거부한다.
- Commit 뒤 반대 Mesh를 지정해 다시 생성해도 Actor·Spot의 기존 Mesh와 object generation을
  유지한다.
- `cmake --build framework/languages/cpp/build --target
  test_cpp_framework_m6b_runtime -j2`와 해당 executable 실행은 exit code 0이다.

이 checkpoint는 actual-process 증거가 아니다. Public Object fixture인 `SpotActorTransfer`
ActorNode는 current exact interface와 맞지 않아 compile되지 않는다. 확인된 차이는
Spot Actor handler context, Spot·Entry Spot context 소유권, factory callback, relocation
adapter 반환형과 Location option이다. 이 상태에서 private Store 조회나 이전 helper로
`Find`·direct messaging evidence를 대신 만들지 않는다. Fixture를 current public contract로
이관한 뒤 `profile`·`workflow` 두 process의 public `GetOrCreate`, `Find`, direct Actor·Spot
request를 실행해야 `RM-A7`을 완료할 수 있다.

## 2026-07-29 C++ Config 1 RM-C3 actual-process 증거

C++ `RM-C3`를 current public ClientServer manual endpoint 경로로 실행했다.
Consumer는 exact `client().connect(endpoint)`로 `api-a`와 `api-b`를 등록했다.

- 같은 ChannelName request 90건을 retry 없이 각각 한 번 제출했다.
- `api-a`와 `api-b`가 모두 선택됐다.
- 두 Provider의 reply 합계는 90건이다.
- 실행 전후 public evidence 증가분 합계도 정확히 90건이다.
- Runner exit code는 0이고 모든 stderr는 비어 있다.
- 종료 뒤 RegistryMessaging process와 test Redis container가 남지 않았다.

Actual-process 증거는
`framework/languages/cpp/e2e/RegistryMessaging/logs/20260729-051403-2182401`이다.
