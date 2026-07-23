<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: 관측·운영 배포](config-11-observability-ops.ko.md)
<!-- framework-adapter-nav:end -->

# Config 12 — Channel egress routing

ChannelName 하나가 process 안의 정확한 RouteMesh 또는 ClientServer 송신 경로를 선택하는지 실제 다중
process 배포에서 검증한다. 이 config는 새 공개 API의 근거가 아니다. 공개 계약은
[Channel topology](../../spec/server/10-channel-topology.ko.md),
[Channel messaging](../../spec/server/11-channel-messaging.ko.md),
[ClientServer Channel](../../spec/server/12-client-server-channel.ko.md),
[network listener identity](../../spec/server/13-network-listener-identity.ko.md)가 소유한다.

## 1. 목적과 범위

이 config는 다음 경계를 함께 검증한다.

- Channel 호출자는 MeshName이나 endpoint가 아니라 ChannelName 하나로 논리 대상을 지정한다.
- Framework는 같은 process에 등록된 송신 경로 중 정확히 하나를 고르며 다른 RouteMesh나
  ClientServer 경로로 fallback하거나 relay하지 않는다.
- RouteMesh의 `Client` 역할은 송신 경로만 등록하고, `Server` 역할만 handler와 weight를 제공한다.
- ClientServer에서는 client만 업무 send와 request를 시작하고 server는 handler와 reply만 담당한다.
- 다른 송신 경로에서 온 reply도 원래 request를 정확히 한 번 완료한다. Spot의 `async`와 `yield`는
  기존 serial turn 계약을 유지한다.
- Listener의 실제 bind 주소와 remote에 제공하는 advertised endpoint가 구분되고 topology별 descriptor가
  섞이지 않는다.

Node direct, Spot direct, Actor direct와 Logical Multicast의 자체 기능은 기존 config가 소유한다. 이
config에서는 Channel egress index가 그 경로를 가로채지 않는 회귀만 확인한다.

## 2. 서버 구성

한 번의 실행에서 다음 역할을 사용한다. Redis는 실행마다 runner가 만든 전용 container를 사용한다.

| 역할 | 수 | 물리 topology와 Channel 역할 |
|---|---:|---|
| location store | 1 | Automatic RouteMesh·ClientServer discovery와 Object Client·Server authority가 공유하는 공식 Redis Location Store. 실행마다 전용 key prefix를 사용한다. |
| relocation store | 1 | CH-REG-02의 Snapshot Actor join에 사용하는 공식 Redis Relocation Store. Location Store와 별도 key prefix를 사용하며 `Session`·`Play` root가 등록한다. |
| `Session` | 1 | `game` RouteMesh의 `game.session` Server, `game.play` Client, `game.api` Client. Location Store와 Relocation Store를 등록하고 `game` MeshNode를 Object Server로 구성한다. Entry Spot과 stable Actor type `channel.player` factory를 명시적 `Snapshot` policy, Actor relocation adapter, placement weight `100`, active `128`·pending `32` capacity로 제공한다. |
| `Play` | 1 | `game` RouteMesh의 `game.play` Server, `game.session` Client, `game.api` Client, `audit` RouteMesh의 `audit.record` Client, `workflow.command` ClientServer Client. Location Store와 Relocation Store를 등록하고 `game` MeshNode만 Object Server로 구성한다. Entry Spot, stable Actor type `channel.player`과 User Spot type `channel.room` factory를 명시적 `Snapshot` policy로 제공한다. Actor·Spot factory에는 kind에 맞는 relocation adapter를 지정하고 placement weight `100`, active `128`·pending `32` capacity를 사용한다. `audit` MeshNode의 object role은 `None`이다. |
| `Api` | 2 | `game` RouteMesh의 `game.api` Server. 서로 다른 weight와 lifecycle generation 사용 |
| `WorkflowClient` | 1 | `workflow.command` ClientServer Client |
| `WorkflowServer` | 2 | `workflow.command` ClientServer Server, `game` RouteMesh membership 0개. Location Store를 등록하고 `game` MeshNode를 Object Client로 구성해 Spot·Actor direct 호출을 시작하지만 factory와 placement target은 제공하지 않는다. 서로 다른 weight와 `Draining` 상태를 사용한다. |
| `Audit` | 1 | 별도 `audit` RouteMesh의 `audit.record` Server |

`Play`는 두 RouteMesh를 등록한다. `game`은 Session·Play·Api의 공통 물리 연결이고, `audit`은 분리된
물리 연결이다. `Play`의 handler와 Spot timer가 `audit.record`를 호출해 다른 egress 선택을 검증한다.
Session에서 Api로 가는 정상 호출은 Play를 거치지 않고 같은 `game` RouteMesh에서 직접 선택한다.

ClientServer 역할은 RouteMesh descriptor를 사용하지 않는다. `WorkflowServer`는 전용 ClientServer
server descriptor를 게시하고 `WorkflowClient`는 location store에서 이를 발견한다.

Object role을 지정하지 않은 `Api`, `WorkflowClient`, `Audit`의 object role은 `None`이다. Automatic
discovery에 참여하는 역할은 object role과 무관하게 Location Store를 등록한다. CH-REG-02의 cross-node
Actor join은 `Session` Entry Spot에서 `Play` User Spot으로 진행하며 두 Object Server의 같은 stable Actor
type·Snapshot adapter capability와 충분한 target headroom을 사용한다. Channel egress 검증과 무관한 host에
object factory를 추가하지 않는다.

## 3. 공통 fixture와 관측 값

언어별 구현은 다음 파일을 자기 소스에 복사해 소유하지 않는다. 저장소 공통 fixture의 같은 JSON을
읽고 Channel topology projection과 비교한다. Object role·factory·Store prerequisite는 §2의 role 설정과
role server startup evidence로 별도 확인하며 Channel-only fixture에 중복하지 않는다.

```text
framework/doc/framework/common/e2e/fixtures/
`-- config-12-channel-egress-routing.json
```

Fixture는 최소한 다음 정보를 가진다.

```json
{
  "config": "ChannelEgressRouting",
  "roles": {
    "session": {"routeMeshes": ["game"], "channels": {"game.session": "server", "game.play": "client", "game.api": "client"}},
    "play": {"routeMeshes": ["game", "audit"], "channels": {"game.play": "server", "game.session": "client", "game.api": "client", "audit.record": "client"}, "clientServer": {"workflow.command": "client"}},
    "api": {"routeMeshes": ["game"], "channels": {"game.api": "server"}},
    "workflowClient": {"clientServer": {"workflow.command": "client"}},
    "workflowServer": {"routeMeshes": ["game"], "channels": {}, "clientServer": {"workflow.command": "server"}},
    "audit": {"routeMeshes": ["audit"], "channels": {"audit.record": "server"}}
  }
}
```

각 역할 server는 `/health`와 `/evidence`를 제공한다. Evidence는 업무 결과뿐 아니라 다음 관측 정보를
포함한다.

- process-local ChannelName과 선택한 egress 종류
- RouteMesh peer의 논리 RID와 실제 connection 수
- ClientServer ready server identity, weight, generation과 `Draining` state
- request correlation의 시작·terminal 횟수
- listener의 configured bind endpoint, actual bound endpoint와 advertised endpoint
- handler 실행 횟수와 unsolicited message drop·protocol 오류 수

## 4. 시나리오

### CH-E2E-01 — 하나의 RouteMesh에서 양방향 Channel request

Session이 `game.play`를 요청하고 Play가 `game.session`을 요청한다. 두 호출은 같은 Session↔Play 물리
peer 연결을 사용하면서 각 Channel의 Server handler에서 처리되고 reply로 한 번씩 완료되어야 한다.
역할 반대 방향을 위해 두 번째 물리 peer를 만들면 실패다.

### CH-E2E-02 — handler에서 다른 egress 호출

Play의 `game.play` handler가 별도 `audit` RouteMesh의 `audit.record`와 ClientServer
`workflow.command`를 순서대로 요청한다. 각 ChannelName이 process-local egress 하나를 선택하고 원래
`game.play` request는 downstream 결과를 포함한 reply 하나로 완료되어야 한다. Reply를 새 application
packet으로 dispatch하거나 downstream correlation을 원래 correlation으로 재사용하면 실패다.

### CH-E2E-03 — Spot handler와 timer의 ClientServer 호출

Play Entry Spot의 packet handler는 `workflow.command` request를 `async`로 기다린 뒤 같은 Spot 상태를
변경한다. Timer callback은 같은 Channel을 `yield`로 기다리고 재개 뒤 필요한 상태를 다시 확인한다.
두 호출의 serial turn 순서, timeout, cancellation과 shutdown 경쟁이 공통 실행 정책과 일치해야 한다.
다른 egress의 reply를 Spot application queue에 새 packet으로 넣으면 실패다.

### CH-E2E-04 — ClientServer server 선택, Shutdown과 재시작

두 WorkflowServer를 positive weight로 시작해 선택 비율과 같은 weight의 순환 순서를 확인한다. 한
server의 weight를 0으로 바꾸고 `Shutdown`하면 새 request 대상에서 제외되지만 이미 수락한 request는 deadline
안에서 끝나야 한다. 같은 논리 역할을 다시 시작하면 automatic topology가 새 RID와 lifecycle generation을
발급하고 이전 generation의 늦은 reply가 새 request를 완료하지 않아야 한다.

### CH-E2E-05 — ClientServer 방향 제한

언어별 public API snapshot과 compile-negative test에서 server가 client를 대상으로 새 업무 send/request를
시작하는 표면이 없어야 한다. Protocol integration 단계에서 unsolicited server message를 주입하면 client
업무 handler로 전달하지 않고 protocol 오류 evidence만 증가해야 한다.

### CH-E2E-06 — process-local ChannelName 충돌

같은 process에서 같은 ChannelName을 서로 다른 RouteMesh와 ClientServer에 각각 중복 등록한다. Host는
listener bind와 dispatch 시작 전에 설정 오류로 종료하고, 진단에는 ChannelName과 두 등록 위치가 모두
포함되어야 한다. 서로 다른 process의 같은 Server ChannelName은 정상 scale-out이므로 충돌로 처리하지
않는다.

### CH-E2E-07 — egress 등록과 연결 상태별 즉시 결과

세 상태를 별도로 검증한다.

1. 등록하지 않은 ChannelName 호출은 process-local 송신 경로가 없으므로 즉시
   `RequestTargetNotFound`로 끝나야 한다.
2. `Api`의 `game.api` Server는 Client를 중복 등록하지 않은 상태에서 다른 ready `Api`를 대상으로
   outbound request를 시작한다. 같은 RouteMesh peer 연결의 target membership을 선택해 정상 reply 하나로
   완료되어야 한다.
3. 등록한 ChannelName의 target identity는 알려져 있지만 해당 pipe가 아직 ready가 아닌 상태에서 호출하면
   즉시 `RouteNotConnected`로 끝나야 한다. 이 검증을 위해 known target의 연결을 시작 전에 차단하고,
   descriptor나 manual intent가 없는 상태와 섞지 않는다.

세 경우 모두 다른 MeshNode, ClientServer client 또는 handler를 relay로 사용하면 실패다. 오류는 operation의
terminal completion 하나로 관측하며 timeout 증가, settle 대기나 반복 retry로 결과를 늦추면 실패다.

### CH-E2E-08 — ClientServer에서 RouteMesh 상태 주소로 연속 호출

WorkflowServer handler가 server membership 0개인 `game` RouteMesh에서 Spot과 Actor를 요청하고 그 결과로 원래 ClientServer request에
reply한다. ClientServer correlation, Spot·Actor generation과 RouteMesh reply token이 섞이지 않아야 하며,
원래 client completion은 reply·timeout·cancellation 경쟁에서도 한 번만 발생해야 한다.

### CH-E2E-09 — 자동 port와 advertised host

RouteMesh, ClientServer server, classic fanout publisher와 STREAM server를 port 0으로 bind한다. 실제 bound
port와 AdvertiseHost로 만든 endpoint가 해당 topology의 record 또는 설정에만 기록되고 remote client가
모두 연결되어야 한다. Wildcard BindHost와 port 0이 advertised endpoint에 남거나 MeshNode와
ClientServer descriptor가 섞이면 실패다.

### CH-E2E-10 — 응답 없는 ClientServer send

WorkflowClient가 `workflow.command`에 one-way send를 제출한다. Ready server 하나의 send handler만 한 번
실행되고 reply token, client 수신 packet과 request completion을 만들지 않아야 한다.

### CH-E2E-11 — ToChannel 다른 MeshNode Server 호출

`Session`과 `Api` Server를 같은 `game` RouteMesh의 서로 다른 process·MeshNode로 시작한다.
`Session`은 MeshName, target RID와 endpoint를 전달하지 않고 process-local `game.api` ChannelName만으로
`RequestToChannel`·`SendToChannel`을 제출한다.

Framework는 `game.api` Server membership의 positive-weight ready member 중 remote `Api` MeshNode를 선택해야
한다. Request는 해당 remote handler reply로 terminal-once 완료되고 send는 outbound admission으로
완료된다. Remote evidence에 packet이 각각 한 번 기록되고 source·target이 동일 peer connection을
공유하되 ChannelName handler namespace와 reply correlation이 유지되어야 한다. 등록하지 않은 다른
RouteMesh나 ClientServer egress를 fallback·relay로 사용하면 실패다.

## 5. 회귀 gate

Config 12 구현과 함께 다음 회귀를 실행한다.

| ID | 검증 범위 | 실패 조건 |
|---|---|---|
| `CH-REG-01` | 기존 같은 RouteMesh Channel send/request와 weighted routing | handler, weight 또는 reply 의미가 바뀐다 |
| `CH-REG-02` | `Session`·`Play` Object Server의 Node·Spot·Actor direct, Snapshot Actor join과 bound-session push. 두 root의 Location·Relocation Store, stable factory type, adapter capability와 target capacity를 startup evidence로 먼저 확인한다. | Channel egress index가 상태 주소 route를 가로채거나 stateful prerequisite 없이 scenario를 시작한다. |
| `CH-REG-03` | Logical Multicast와 classic Pub/Sub | ClientServer 경로로 잘못 선택되거나 대상 수가 바뀐다 |
| `CH-REG-04` | reply·timeout·cancellation·disconnect·Spot shutdown 경쟁 | completion이 누락·중복되거나 늦은 reply가 새 generation에 전달된다 |
| `CH-REG-05` | 같은 endpoint의 automatic 역할 교체, 새 RID·generation과 reciprocal handover | 재연결 뒤 request가 소실되거나 이전 RID·generation을 선택한다 |
| `CH-REG-06` | local 정상 완료 시간 | timeout 증가나 반복 retry가 있어야 통과한다 |
| `CH-REG-07` | 7개 공통 sample 구성 snapshot | 공통 sample topology fixture와 다르다 |
| `CH-REG-08` | 물리 peer와 listener 수 | 같은 peer pair에 반대 방향 또는 RouteMesh·ClientServer 중복 연결이 생긴다 |
| `CH-REG-09` | sample 공개 API source | MeshName 은닉 helper, weight 0 client 표현, 가짜 membership 또는 언어별 예외가 남는다 |

## 6. 언어별 feature map과 runner inventory

각 언어 feature map은 `CH-E2E-01~11`, `CH-REG-01~09`를 한 행씩 대응시킨다.
구현 전에는 `planned` 또는 구체적 gap으로 표시하고 runner·assertion·evidence 경로없이
`implemented`로 표시하지 않는다. Java와 Kotlin은 binding/runtime을 공유하지만 각 언어의
public builder와 handler 문법을 compile fixture로 검증한다. Runtime E2E는 JVM lane에서 한
구성을 공유할 수 있다.

| lane | feature map | config runner | 필수 보충 검증 |
|---|---|---|---|
| `.NET` | `framework/languages/dotnet/e2e/ChannelEgressRouting/feature-map.ko.md` | `framework/languages/dotnet/e2e/ChannelEgressRouting/run_e2e.sh` | C# public compile·negative surface fixture |
| Java | `framework/languages/java/e2e/ChannelEgressRouting/feature-map.ko.md` | `framework/languages/java/e2e/ChannelEgressRouting/run_e2e.sh` | Java public compile·negative surface fixture |
| Kotlin | `framework/languages/java/e2e-kotlin/ChannelEgressRouting/feature-map.ko.md` | JVM runtime runner를 재사용하면 해당 경로를 feature map에 명시 | Kotlin DSL public compile·negative surface fixture |
| Node.js | `framework/languages/node/e2e/ChannelEgressRouting/feature-map.ko.md` | `framework/languages/node/e2e/ChannelEgressRouting/run_e2e.sh` | TypeScript·NestJS public compile·negative surface fixture |
| C++ | `framework/languages/cpp/e2e/ChannelEgressRouting/feature-map.ko.md` | `framework/languages/cpp/e2e/ChannelEgressRouting/run_e2e.sh` | 설치 public header compile·negative surface fixture |

각 config runner는 공통 JSON fixture를 직접 읽고 role별 설정 snapshot을 비교한다. 언어별
fixture 복사본을 두지 않는다. 통합 runner inventory에는 `ChannelEgressRouting`과 config runner
경로를 추가하고, 선택 실행에서 `ChannelEgressRouting:<scenario-id>`를 전달할 수 있어야 한다.

개별 runner는 다음 selector를 지원한다.

```text
all
CH-E2E-01 ... CH-E2E-11
CH-REG-01 ... CH-REG-09
```

Runner는 build, 실행 전용 Redis 준비, role별 typed 설정 파일 생성, server readiness, scenario client,
evidence 수집과 자신이 시작한 process·Redis 정리를 담당한다. Native abort, semantic assertion,
descriptor 불일치와 completion 중복은 retry하지 않는다. Local readiness·route settle·scenario settle은
[E2E README §2.1](README.ko.md#21-로컬-e2e-대기-기준)의 기본값 안에서 통과해야 하며 timeout을 늘려
완료 처리하지 않는다.

## 7. 완료 조건

- 네 framework lane과 Java/Kotlin public compile fixture가 `CH-E2E-01~11`, `CH-REG-01~09`를 모두 통과한다.
- 공통 fixture와 언어별 구성의 Channel topology projection에 차이가 없다. Object prerequisite는 §2의
  role server startup evidence와 일치한다.
- 종료 뒤 남은 server/client/Redis process가 없고 native assertion과 timeout이 없다.
- Public API snapshot, 정식 exact interface, sample source와 실제 package가 같은 signature를 사용한다.
- 같은 peer pair의 중복 물리 연결과 topology descriptor 혼용이 없다.
