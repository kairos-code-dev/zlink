# S2 Framework 10.0.0 정식 계약 inventory

> 이 문서는 Framework 10.0.0 공통·server 정식 spec과 .NET·C++·Java·Kotlin·Node 공개 interface를 작성하는 담당자가
> 어떤 파일에 어떤 목표 계약을 기록하고 어떻게 검증해야 하는지 답하는 실행 inventory다.
> 현재 구현을 설명하는 정식 spec이 아니며, 이 문서의 완료 표시는
> [`RouteMesh 10.0.0 실행 진행표`](./route-mesh-10.0.0-execution-ledger.ko.md)의 S2 상태를
> 대신하지 않는다.

## 1. 기준과 범위

이 inventory는 다음 문서를 목표 계약의 입력으로 사용한다.

- [`S0 범위와 기준선`](./s0-scope-baseline.ko.md)
- [`RouteMesh messaging 통합 계획`](./framework-route-mesh-messaging-consolidation.ko.md)
- [`MeshNode Core API 검토`](./mesh-node-core-api-review.ko.md)
- [`MeshNode framework dispatch 설계`](./mesh-node-framework-dispatch-design.ko.md)
- Core 10.0.0 정식 spec

Framework 공통 정식 spec은 모든 언어가 제공할 의미와 실패 계약을 먼저 고정한다. S2에서는 .NET,
C++, Java, Kotlin과 Node.js의 exact public interface를 각 언어별 정식 spec에 모두 고정한다. S3는
이 다섯 interface가 같은 공통 계약을 언어 관용 표현으로 정확히 투영했는지 구현 전에 리뷰한다.
RouteMesh 전환 차이와 stage 상태는 임시 plan이 기록한다. framework 목표 계약과 현재 언어 구현의 차이는
공개 계약이 아닌 `90-implementation-gap.ko.md`와 언어별 gap 문서가 기록한다. 정식 목표 spec은 구현
상태를 설명하지 않으며, plan이나 gap 문서에만 있는 결정을 구현자가 다시 해석하지 않도록 각 결정은 아래
owner 문서 한 곳에서 완결된 계약이 되어야 한다.

이 문서가 다루는 범위는 다음과 같다.

1. framework 공통·server 정식 spec의 owner와 교차 참조
2. .NET·C++·Java·Kotlin·Node exact public interface owner와 검증 기준
3. 다섯 언어의 machine-readable public contract projection과 임시 구현 차이 추적
4. 공통·언어별 E2E 문서, 공통·언어별 sample 문서와 public 예제의 영향 범위
5. 현재 worktree에서 같은 파일을 수정할 때의 충돌 위험

E2E, sample, runner와 package 영향은
[`S2 E2E·sample·runner 영향 inventory`](./s2-e2e-sample-runner-impact-inventory.ko.md)가 소유한다.

## 2. 동시 작업과 변경 보존

S2 문서 작업을 시작할 때마다 `git status --short`와 대상 파일의 staged·unstaged diff를 다시 읽는다.
이 inventory에는 특정 시점의 worktree 상태를 복사하지 않는다. 상태 표가 오래되면 충돌이 없는 파일을
위험 파일로 오인하거나 다른 작업의 새 변경을 놓칠 수 있기 때문이다.

다른 작업이 수정한 파일과 겹치면 파일 전체를 다시 쓰거나 formatter로 일괄 치환하지 않는다. 필요한
절만 수정하고 기존 변경의 목적을 보존한다. 소유권이 다른 범위는 해당 담당자와 분리한 뒤 작업한다.
정식 spec, 언어별 exact interface, E2E·sample 영향 inventory와 검증기는 서로 다른 담당 범위로
나누되, S2 완료 시점에는 같은 frozen revision과 같은 검증 결과로 함께 판정한다.

## 3. 공통 정식 spec owner

### 3.1 기반 계약

| S2 ID | 계약 질문 | 정식 owner | 필요한 10.0.0 계약 | 검증 | 상태·위험 |
|---|---|---|---|---|---|
| S2-01 | 목표 계약과 구현 차이를 어떤 순서로 관리하는가 | `framework/doc/framework/spec/00-public-contract-governance.ko.md` | 공통 spec, 언어별 exact interface, 구현과 contract test 순서 및 승인 조건 | 정식 spec에서 구현 상태와 임시 계획 문서 참조 scoped no-hit | `clean` |
| S2-02 | 사용자가 Framework를 어떤 interaction 집합으로 이해하는가 | `framework/doc/framework/spec/01-overview.ko.md` | RouteMesh, MeshNode, ChannelName, Logical Multicast와 classic fanout의 역할과 경계 | `02-interaction-model`과 용어·보장 비교 | `clean` |
| S2-03 | node, channel, Spot, Actor, fanout과 STREAM이 무엇을 주고받는가 | `framework/doc/framework/spec/02-interaction-model.ko.md` | request, send, Logical Multicast, classic fanout, Actor와 STREAM의 대상·응답·전달 의미 | interaction별 target, selection, reply와 delivery 표 완전성 검사 | `clean` |
| S2-04 | 사용자가 어떤 root API와 client를 사용하는가 | `framework/doc/framework/spec/05-framework-api.ko.md` | MeshNode 중심 등록, 두 handler family, typed client, Spot·Actor·STREAM 구성과 option owner | 언어별 interface의 모든 public family가 공통 family에 대응 | `clean` |

`framework/doc/framework/spec/03-message-model.ko.md`는 service call metadata의 단일 정본이다.
`04-async-execution-policy.ko.md`는 timeout, cancellation, handler turn과 completion 진행을 소유한다.
overview나 framework API 문서에 이 두 계약을 복제하지 않고 직접 link한다.

### 3.2 RouteMesh와 messaging

| S2 ID | 계약 질문 | 정식 owner | 필요한 10.0.0 계약 | 검증 | 상태·위험 |
|---|---|---|---|---|---|
| S2-05 | MeshNode가 어느 물리 mesh와 논리 channel에 참여하는가 | `framework/doc/framework/spec/server/10-channel-topology.ko.md` | MeshName 하나, RID 하나, ROUTER endpoint 하나, 하나 이상의 immutable ChannelName, lifecycle generation과 descriptor revision, 실행 중 weight snapshot 전파, process의 MeshName별 MeshNode 하나, full mesh와 수동·자동 peer admission | duplicate name, 빈 membership, RID pin, stale revision, descriptor mismatch startup test ID 연결 | 검증 완료 |
| S2-06 | RID direct와 ChannelName select-one 호출이 어떤 결과를 만드는가 | `framework/doc/framework/spec/server/11-channel-messaging.ko.md` | selection과 submit 원자성, weighted round-robin, no-member, timeout, cancellation, late reply, one-shot reply와 metadata snapshot | Config 1 scenario와 함수별 error 표 대응 | `clean` |
| S2-07 | MeshNode가 topology와 service runtime을 어떻게 소유하는가 | `framework/doc/framework/spec/server/21-mesh-node.ko.md` | MeshNode 등록, start validation, peer connection, channel membership, Spot·Actor owner, readiness와 shutdown 경계 | spec index, gap, 언어별 interface와 guide link no-hit 검사 | 검증 완료 |
| S2-08 | Spot publish와 subscription은 어느 node와 Spot을 선택하는가 | `framework/doc/framework/spec/server/20-spot-messaging.ko.md` | target ChannelName 필수, node-local `(ChannelName, topic, Spot)` match, Logical Multicast metadata, NoDrop, remote·local 6개 count, source ChannelName, no-relay와 complete message 의미 | Config 2 multicast/local match/metadata/NoDrop scenario 대응 | 검증 완료 |

`21-mesh-node.ko.md`가 MeshNode lifecycle과 물리 topology를 소유한다. `20-spot-messaging.ko.md`는
Spot handler, publish, subscription과 Spot 실행 turn만 소유한다. 두 문서가 peer handshake나 local
subscription index를 각각 다시 정의하지 않는다.

### 3.3 Actor, Spot address와 STREAM

| S2 ID | 계약 질문 | 정식 owner | 필요한 10.0.0 계약 | 검증 | 상태·위험 |
|---|---|---|---|---|---|
| S2-09 | Actor와 Spot 주소가 MeshNode owner를 통해 어떻게 resolve되는가 | `server/22-actor-model.ko.md`, `server/23-spot-actor.ko.md`, `server/24-spot-address-messaging.ko.md` | ActorRef·SpotHandle generation, owner MeshNode, direct call, join·lifecycle, handler turn과 stale result | Config 2·9·10 scenario ID와 계약 행 연결 | 검증 완료 |
| S2-10 | STREAM session과 Actor mailbox가 어느 MeshNode를 사용하는가 | `server/30-stream-session.ko.md`, `server/31-session-actor-dispatch.ko.md` | StreamNode의 명시적 MeshName, session bind/unbind, complete relay, bound push, reply correlation과 transport 정보 은닉 | Config 2 session track과 Config 9·10 bound-session scenario 대응 | `clean` |

Actor payload는 Actor handler family로 전달되고 Spot callback을 거치지 않는다. join과 membership
lifecycle은 Spot control family가 처리한다. 이 분리는 `22-actor-model.ko.md`와
`23-spot-actor.ko.md`에서 같은 owner·generation 용어로 설명해야 한다.

### 3.4 Location과 Actor transfer authority

| S2 ID | 계약 질문 | 정식 owner | 필요한 10.0.0 계약 | 검증 | 상태·위험 |
|---|---|---|---|---|---|
| S2-11 | topology descriptor와 Spot·Actor location row를 어디에 저장하는가 | `server/40-location-runtime.ko.md`, `server/41-location-store-redis.ko.md` | MeshNode descriptor와 logical owner row 분리, lifecycle generation·descriptor revision, Redis extension 명시 등록, 분산 기능의 store 필수 조건, 수동 admission handshake, test-only in-memory 경계 | startup capability matrix, descriptor byte fixture와 Config 1·6 failure scenario 대응 | 검증 완료 |
| S2-11A | transfer 결정을 누가 원자적으로 기록하고 복구하는가 | `server/23-spot-actor.ko.md`, `server/41-location-store-redis.ko.md` | participant-set CAS, transfer token, lease, prepared·committed·activated·aborted 상태, exact Redis HASH fixture, successor recovery와 startup capability validation | Config 10 각 failure point, byte fixture와 Redis atomic contract 대응 | 검증 완료 |

`40-location-runtime.ko.md`는 store-neutral row와 capability를 소유하고,
`41-location-store-redis.ko.md`는 Redis key와 atomic operation을 소유한다. transfer state machine과
application callback ordering은 `23-spot-actor.ko.md`가 소유한다.

### 3.5 관측, drain, metadata와 timer

| S2 ID | 계약 질문 | 정식 owner | 필요한 10.0.0 계약 | 검증 | 상태·위험 |
|---|---|---|---|---|---|
| S2-12 | RouteMesh readiness와 shutdown을 어떻게 관측하고 종료하는가 | `server/50-runtime-monitoring.ko.md`, `server/54-graceful-drain-handoff.ko.md` | MeshNode/peer/channel readiness, multicast backpressure·drop, drain snapshot, sealed work, rollback과 terminal completion | Config 5·7·10·11 scenario 대응 | 검증 완료 |
| S2-12A | 운영 metric이 어떤 bounded label로 RouteMesh와 connector 동작을 나타내는가 | `server/51-runtime-metrics.ko.md`, `server/52-message-flow-tracing.ko.md`, `stream-connector/32-stream-connector.ko.md` | node direct, channel, Logical Multicast, classic fanout, Actor와 server STREAM 계기 및 client connector reconnect 계기의 owner 분리 | server·connector metric catalog, 네 언어 connector exact interface와 Config 11 marker exact match | `clean` |
| S2-12B | 한 호출의 flow가 relay와 completion 사이에서 어떻게 이어지는가 | `server/53-flow-correlation.ko.md` | direct RID, channel select-one, multicast tree, Spot·Actor relay와 request completion correlation | Config 11 flow tree와 error line 검증 | `clean` |
| S2-12C | application metadata를 누가 만들고 검증하며 handler에 전달하는가 | `framework/doc/framework/spec/03-message-model.ko.md`, Core MeshNode·Spot 정식 spec | Node·Channel·Spot direct와 Logical Multicast canonical codec, byte 상한, last-write-wins builder, hostile ingress rejection, immutable handler snapshot, relay 전이표, reply 비자동복사와 일반 reply metadata 미지원 | 공통 byte corpus, Config 1·2·4, Core publish ABI와 언어별 builder/context signature 대조 | 검증 완료 |
| S2-12D | Spot timer가 언어별 backend 차이와 무관하게 어떤 순서로 실행되는가 | `04-async-execution-policy.ko.md`, `05-framework-api.ko.md`, `server/20-spot-messaging.ko.md`, `server/25-stage-wrapper-on-spot.ko.md` | Spot key별 직렬 실행, generation, cancel, overrun, shutdown freeze와 backend 선택 | Config 2 timer와 Config 8 turn scenario 대응 | `clean` |

## 4. 언어별 public interface owner

### 4.1 공통 투영 규칙

다섯 언어는 공통 정식 spec의 같은 public capability와 실패 의미를 제공해야 한다. S2는 다음 항목을
각 언어의 exact signature로 고정한다. 언어 특성에 맞는 이름, async type과 DI 표현은 달라도 같은
capability를 빠뜨리거나 구현 stage로 계약 결정을 미룰 수 없다.

- `AddRouteMesh(meshName)`에 대응하는 root registration
- 반복 가능한 `ChannelName(channelName)`과 channel handler scope
- MeshNode RID와 ROUTER listen endpoint
- optional expected RID를 받는 manual peer connection
- node direct와 channel handler family
- node/channel/Spot direct send·request client와 immutable metadata snapshot
- target ChannelName을 받는 Spot Logical Multicast, immutable metadata snapshot, `NoDrop`과 remote·local 6개 count
- Spot·Actor·STREAM session lifecycle와 명시적 MeshName 선택
- Redis location extension 등록과 startup capability validation
- MeshNode runtime option의 startup-only validation
- monitoring, metrics, flow correlation과 graceful drain

### 4.2 언어별 파일

| S2 ID | 언어 | exact interface owner | 함께 수정할 owner | 필요한 10.0.0 변경 | 검증 | 상태·위험 |
|---|---|---|---|---|---|---|
| S2-13 | .NET | `server/languages/dotnet/02-handler-interfaces.ko.md`, `05-route-mesh.ko.md`, `06-location-store.ko.md` | `01-system-structure.ko.md`, `04-routing-id-allocation.ko.md`, `README.ko.md` | `IZLinkFrameworkOptions`, MeshNode·channel·fanout·STREAM builder, `IZLinkMeshPeerConnections`, 두 handler family, Spot·Actor client, metadata, NoDrop, runtime 관측, location store와 Redis exact C# signature | C# code block SHA-256, public declaration 양방향 inventory, 구현 단계 compile fixture와 package snapshot | S2 문서 계약 검증 `clean`; 구현·package green은 S8 gate |
| S2-14 | C++ | `server/languages/cpp/02-framework-interfaces.ko.md`, `03-location-store.ko.md` | `01-system-structure.ko.md`, `README.ko.md` | C++ builder, handler, client, value/result, lifecycle, location과 Redis exact signature 작성 | 문서 선언 inventory와 header compile fixture 설계; package consumer green은 S9 | S2 문서 계약 검증 완료; 구현·package green은 S9 gate |
| S2-15 | Java | `server/languages/java/02-handler-interfaces.ko.md`, `03-location-store.ko.md` | `01-system-structure.ko.md`, `README.ko.md` | Java builder, handler, client, CompletionStage, Spring 등록과 Redis extension exact signature 작성 | 문서 API inventory와 Java compile fixture 설계; package consumer green은 S9 | S2 문서 계약 검증 완료; 구현·package green은 S9 gate |
| S2-15 | Kotlin | `server/languages/kotlin/02-handler-interfaces.ko.md`, `03-location-store.ko.md` | `README.ko.md` | Kotlin DSL·extension, suspend/async 표현과 Java runtime 경계 exact signature 작성 | 문서 extension inventory와 Kotlin compile fixture 설계; package consumer green은 S9 | S2 문서 계약 검증 완료; 구현·package green은 S9 gate |
| S2-16 | Node.js | `server/languages/node/02-handler-interfaces.ko.md`, `04-location-store.ko.md` | `01-system-structure.ko.md`, `03-routing-id-allocation.ko.md`, `README.ko.md` | TypeScript declaration, Promise, NestJS 등록, peer connection, metadata, NoDrop, runtime option, location과 Redis exact signature 작성 | 문서 declaration inventory와 compile fixture 설계; package consumer green은 S9 | S2 문서 계약 검증 완료; 구현·package green은 S9 gate |

`server/languages/README.ko.md`는 다섯 언어 owner와 parity 검증 경로를 연결한다. S2 리뷰 범위에는
다섯 언어의 exact signature를 모두 포함한다. 각 exact signature는 해당 언어 문서만 소유하며 공통
문서나 다른 언어 문서에 복제하지 않는다.

## 5. 금지 surface와 exact-once 전수 mapping

### 5.1 10.0.0 public surface no-hit

S2 정식 공통 spec과 다섯 언어 exact interface에는 다음 topology surface가 나타나지 않아야 한다.
공통·언어별 E2E 문서, sample 문서와 public 예제도 같은 검사를 수행한다.

- `AddClientServerChannel`
- `AddRouteMeshChannel`
- `AddSpotMesh`
- `IZLinkClientServerChannelBuilder`
- `IZLinkRouteMeshChannelBuilder`
- `IZLinkSpotNodeBuilder`
- `UseInMemoryLocationStores`
- `MapSpotMeshToRouteChannel`
- `EnableRouter`, `ConnectRouter`, `EnablePubSub`, `ConnectPeerPub`

classic fanout 등록과 client는 독립 기능이므로 no-hit 대상이 아니다.

### 5.2 전수 mapping owner

| S2 ID | owner | 내용 | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-17 | RouteMesh v10 contract inventory와 다섯 언어 exact interface | 언어별로 허용하는 root, builder, handler, client, option과 result의 exact 목록 | 각 언어 public 선언의 양방향 집합·허용 횟수와 code fixture 비교 | 검증 완료 |
| S2-18 | RouteMesh 전환 inventory와 framework의 비계약 gap 문서 | RouteMesh 전환 차이는 plan이, 목표 framework 계약과 현재 언어 구현의 차이는 `90-implementation-gap.ko.md`와 언어별 gap 문서가 소유 | 정식 목표 계약에서 임시 plan 참조가 없고 gap 문서가 목표 계약·S2 완료 증거로 사용되지 않음 | 전환 실행 상태는 중앙 ledger만 갱신하며 framework gap은 계약과 분리 |
| S2-18A | 다섯 언어 exact interface와 machine inventory | root option과 topology·location·runtime 공개 선언의 언어별 exact mapping | public declaration 집합·중복 횟수와 언어별 문서 fixture 비교, S8·S9에서 package API snapshot 비교 | S2 문서 검증 완료; package 비교는 S8·S9 |

## 6. Machine-readable contract inventory

현재 `framework/doc/contract-inventory/framework-public-contract-inventory.json`은 다른 framework
계약의 언어별 구현 추적 입력이다. S2의 RouteMesh exact interface 검증 입력은 별도 RouteMesh v10
contract inventory가 소유한다. 기존 .NET 전용 inventory가 있다면 이를 다섯 언어 projection을
수용하는 단일 inventory로 확장하거나, 공통 capability ID를 공유하는 언어별 inventory로 분리한다.
어느 형식을 선택하더라도 S2에서 다섯 언어의 exact declaration을 모두 검증해야 한다.

| 집합 | 필수 내용 |
|---|---|
| languages | `dotnet`, `cpp`, `java`, `kotlin`, `node` |
| root registration | MeshNode, channel membership, fanout, StreamNode와 Redis extension |
| builders | MeshNode parent, channel child, peer connections와 runtime options |
| handlers | node direct, channel, Spot, Actor, STREAM session과 lifecycle |
| clients | node, channel, Spot direct, Logical Multicast, Actor와 bound session |
| values | MeshName, ChannelName, RID, descriptor, operation result와 metadata snapshot |
| errors | invalid config, no member, disconnected, backpressured, timeout, cancellation, stale와 shutdown |
| observation | monitoring, metrics, flow correlation와 drain |

machine inventory ID는 언어 중립 의미를 나타내고, 언어별 이름은 `names.dotnet`, `names.cpp`,
`names.java`, `names.kotlin`, `names.node` 투영으로 둔다. 한
언어에서만 필요한 helper는 public capability가 아니라는 근거가 있을 때만 language-only 집합에 넣는다.

## 7. Link와 contract 검증

S2 정식 spec 작업은 다음 순서로 검증한다.

1. `21-spot-node.ko.md`와 해당 link가 S2 정식 spec 범위에서 0건인지 확인한다.
2. §5.1의 금지 surface를 정식 spec, 언어별 exact interface와 sample에서 검색한다.
3. machine inventory의 public declaration 집합·허용 중복 횟수와 다섯 언어 정식 문서 선언을 양방향 비교한다.
4. 언어별 interface code block의 수와 SHA-256을 inventory와 비교해 반환형·인자·overload·property
   변경이 inventory 갱신 없이 통과하지 못하게 한다. Socket runtime monitoring의 닫힌 여섯
   event kind도 .NET·C++·Java·Node.js exact enum과 Kotlin의 Java 타입 재사용 계약으로
   같이 검증한다.
5. 정식 spec이 `framework/doc/plan/v10.0/`이나 구현 상태 추적 문서를 참조하지 않는지 검사한다.
6. relative link, duplicate anchor와 Markdown table 구조를 검사한다.
7. 실제 렌더 결과에서 표, code block과 link가 읽을 수 있는지 확인한다.

검증 자동화는 `scripts/verify-framework-doc-contracts.sh`를 단일 진입점으로 사용한다. S2에서는 문서
선언 집합, 다섯 언어 exact interface fixture, link, fence와 금지 surface를 검증한다. 실제 package public API와 compile
fixture의 green 결과는 구현 전에는 만들 수 없으므로 다음 red/green gate로 분리한다.

| 구현 stage | red gate와 green 명령 | 실패 조건 |
|---|---|---|
| S8 `.NET` | `dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj`, `framework/languages/dotnet/scripts/verify_packaged_contract.sh` | target type·member·overload·nullable·기본값 누락, 제거 surface 존재 또는 NuGet export 차이 |
| S9 C++ | S2·S3에서 리뷰한 C++ exact interface를 기준으로 `framework/languages/cpp/scripts/verify_packaged_contract.sh` 실행 | header compile, public type·method·result 또는 package export 차이 |
| S9 Java/Kotlin | S2·S3에서 리뷰한 두 exact interface를 기준으로 `framework/languages/java/scripts/verify_packaged_contract.sh` 실행 | Java/Kotlin compile fixture, export 또는 package consumer 차이 |
| S9 Node.js | S2·S3에서 리뷰한 TypeScript exact interface를 기준으로 `framework/languages/node/scripts/verify_packaged_contract.sh` 실행 | declaration compile, export snapshot 또는 clean npm consumer 차이 |

## 8. S2 완료 점검

- S2-01~S2-12D의 계약이 표에 지정한 정식 owner에 기록되어 있다.
- `21-mesh-node.ko.md`가 MeshNode lifecycle과 topology의 단일 owner다.
- service call metadata는 `03-message-model.ko.md`를 단일 정본으로 사용한다.
- .NET·C++·Java·Kotlin·Node exact interface가 공통 capability와 error 의미를 표현한다.
- 다섯 언어 public 선언 집합, 허용 중복 횟수와 exact fixture가 machine inventory에 대응한다.
- RouteMesh 전환 차이와 stage 상태는 임시 plan에, framework 언어별 구현 차이는 비계약 gap 문서에
  기록되고 정식 목표 spec은 이를 완료 근거나 계약 정본으로 참조하지 않는다.
- `MM` 파일의 기존 staged·unstaged 변경을 보존했다.
- S2 문서 link, fence, 금지 surface와 다섯 언어 exact fixture 검증이 통과했고 package green은 S8·S9 gate로 분리했다.
- 공통·언어별 E2E 문서, 공통·언어별 sample 문서와 public 예제가 영향 inventory에 포함되어 있다.

S3 독립 리뷰는 이 inventory의 작성 완료 조건이 아니라 실행 진행표의 S3 gate가 관리한다.
