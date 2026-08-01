<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [다음: Spot 서비스](config-2-spot-service.ko.md)
<!-- framework-adapter-nav:end -->

# Config 1 — Location store 기반 messaging 배포

첫 e2e config다. 실제 배포와 같은 서버 구성을 한 번 시작하고, 그 구성에서 messaging과
연결·RID resolve가 public API 계약대로 동작하는지 확인한다. 연결 대상 정보는 registry
process가 아니라, 각 MeshNode가 공유 location store에 등록한 descriptor에서 얻는다.

## 1. 목적과 범위

확인하려는 것은 단순하다. 공유 location store를 두고, provider가 자기 위치를 자동 등록하고,
consumer가 endpoint를 코드에 적지 않은 채 자동 연결하고, 프로세스 경계까지 실제로 나눈 상태 —
즉 **배포 현장과 같은 조건**에서 messaging과 연결·RID resolve가 의도대로 동작하는가.

기존 unit/contract 테스트와 단언이 겹쳐도 괜찮다. 차별점은 "새로운 단언"이 아니라 "현실적인
배포 컨텍스트 + 샘플 수준의 public API 사용"이다. 그래서 각 시나리오는 helper 없이 public
contract를 직접 호출하고 `ensure`로 단언해서, 실제 사용 흐름이 한눈에 보이게 쓴다.

이 config에는 registry process가 없다. 위치 정보의 기준 저장소는 Location Store다. Application과
E2E client는 Store provider를 직접 조회하거나 descriptor record를 해석하지 않는다. 연결 상태는
`IZLinkRouteMeshRuntime.GetStatus(meshName)`이 반환한 peer·Channel status와 실제 messaging evidence를
함께 사용해 검증한다. Store provider의 opaque record와 transaction은 Config 6의 provider contract
시나리오에서 검증한다.

location row 모델, owner lease, freshness 같은 계약 상세는 이 문서에서 반복하지 않는다.
[location runtime spec](../spec/21-location-runtime.ko.md)을 기준으로 한다.

여기서 다루지 않는 것(다른 config로): codec, stream, spot/actor, resilience 세부, store
장애/복구(Config 6). 이 config는 messaging + 연결/resolve에만 집중한다.

## 2. 서버 구성 (한 번 구동, 공유)

스크립트가 아래 구성을 한 번 시작하고 모든 client 시나리오가 함께 쓴다. scale·failover
시나리오만 provider 프로세스를 추가로 시작하거나 종료하고, weighted 시나리오(RM-C7)는 build-time
weight를 다르게 준 provider를 따로 시작한다(공유 provider는 기본 weight `100`).

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix로 격리한다. 별도 registry process는 실행하지 않는다. |
| relocation store | 1 (`RM-C10` 전용) | `PreserveStateWith` adapter capability bound를 검증하는 Object Server root가 등록하는 공식 Redis relocation store extension. Location Store와 별도 key prefix를 사용한다. 다른 시나리오의 `DisableRelocation` factory는 이 Store를 요구하지 않는다. |
| provider (api 노드) | 2 (`api-a`, `api-b`) | `AddRouteMesh(meshName)`으로 MeshNode를 만들고 profile `ChannelName` membership에 request handler(`ProfileRequest`)·send handler(`ProfileCommand`)를 등록한다. RID direct route handler(`ScenarioRoutePing`)는 같은 MeshNode에 등록한다. Automatic routing ID는 역할 prefix `api-a`·`api-b` 뒤에 lifecycle별 lowercase canonical UUID v4를 붙여 발급하며 application이 exact RID를 고정하지 않는다. Dispatch-error observer로 evidence를 기록하며 테스트용 `/evidence`·`/health` HTTP endpoint를 함께 제공한다. |
| object authority 노드 | 2 (`profile-object`, `workflow-object`) | 각각 `profile`·`workflow` Mesh의 Object Server다. 두 노드는 같은 stable Actor type `cfg1.actor`과 User Spot type `cfg1.user-spot` factory를 `DisableRelocation` policy로 등록하고 placement weight `100`, Actor total·Spot total limit `128`과 activation concurrency `32`를 사용한다. RM-A7의 manager call과 direct request를 시작할 Client capability는 Server role에 포함된다. |
| consumer | 시나리오별 | Location Store를 사용하는 automatic topology에서는 같은 MeshName의 descriptor에서 peer를 확인하고 RID prefix만 설정한다. 두 MeshNode 중 RID가 canonical byte order에서 더 작은 쪽만 connect를 시작한다. Manual topology에서는 role `None` MeshNode에 fixed RID와 peer endpoint를 언어별 peer-connection interface로 등록한다. Application 구성에 따라 한쪽 또는 양쪽에서 connect할 수 있으며, 양쪽에서 시작한 중복 후보는 admission이 하나의 ready 연결로 수렴시킨다. |
| Object Client pair | 2 (`client-a`, `client-b`, RM-A3 전용) | 같은 MeshName과 Location Store를 사용한다. 기본 반복은 `Objects().Client()`와 outbound Channel Client만 등록한다. 대조 반복은 한쪽에 RouteMesh Channel Server를 weight `100`과 `0`으로 각각 등록한다. Automatic 반복과 runner가 endpoint를 지정한 Manual 반복을 분리한다. Application Node direct handler와 object factory는 등록하지 않는다. |

각 provider는 MeshName, RID, lifecycle generation, ROUTER endpoint와 `ChannelWeights`를 포함한
MeshNode descriptor를 framework lifecycle을 통해 store에 게시한다. consumer는 MeshName과
ChannelName만 지정하며 endpoint는 descriptor에서 확인한다. 이 config는 descriptor를 application
코드에서 직접 갱신하거나 제거하지 않는다. manual topology 시나리오만 peer endpoint를
언어별 peer-connection interface로 지정한다. RouteMesh 구성원은 모두 `ROUTER`다.

`profile-object`와 `workflow-object` root는 Location Store를 명시적으로 등록한다. 두 factory의 policy가
모두 `DisableRelocation`이므로 RM-A7에는 Relocation Store와 relocation adapter가 필요하지 않다. `RM-C10`의 `PreserveStateWith`
capability fixture만 Location Store와 Relocation Store를 함께 등록하고 factory kind에 맞는 adapter를
제공한다. Channel-only provider와 consumer에 object factory를 추가하지 않는다.

store 등록은 각 역할의 `*HostFactory`에서 바로 보이게 둔다.

```csharp
// 공식 Redis Location Store는 descriptor, owner lease와 location authority를 제공한다.
// Relocation payload가 필요한 Object Server는 별도 AddRelocationStore로 capability를 등록한다.
options.AddLocationStore(new ZLinkRedisLocationStore(redis =>
{
    redis.ConnectionString = redisConnectionString;
    redis.KeyPrefix = "zlink:e2e:cfg1:" + runId; // 실행별 전용 prefix로 격리
}));
```

handler 동작(공유):

- `ProfileRequest(value)` → `profile:{value}` reply + 처리한 provider의 routing id 포함.
- `ScenarioRoutePing(value)` → `route:{value}` + target/source routing id 포함.
- `value=="slow"`면 1s 지연(timeout 유도용).
- 미등록 packet 이름은 handler가 없으니 → request면 error reply, send면 drop.

이 동작들은 모두 observer evidence에 marker로 남는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis를 준비하고(기동 또는 연결 확인, 실행별 key prefix 결정) provider를 순서대로
시작한 뒤(포트 readiness 확인) client 시나리오를 하나씩 실행한다. 실행이 끝나면 전용 prefix의
key를 정리하거나 disposable Redis instance를 버린다. scale·failover 시나리오는 같은 스크립트가
추가 provider를 시작하거나 종료하고, weighted 시나리오(RM-C7)는 weight를 차등 설정한 provider를
시작한다. client가 `e2e result=passed`를 출력하면 통과로 본다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — 연결과 rid resolve

#### RM-A1 location store 자동 연결 + rid 자동 resolve

우선순위: `P0`

**검증 질문:** endpoint를 코드에 지정하지 않고 공유 location store의 MeshNode descriptor로 provider를
확인해 메시지를 보낼 수 있는가.

- 절차: consumer가 endpoint 없이 channel을 등록하고 같은 location store(`AddLocationStore(...)`, 같은 key prefix)를 등록한 뒤, 자동 연결이 성립하면 `ProfileRequest`를 보낸다.
- 검증: request가 `api-a`/`api-b` 중 하나에서 처리된다(reply의 provider RID로 확인). Consumer는
  endpoint를 코드에 지정하지 않는다. `IZLinkRouteMeshRuntime.GetStatus(meshName)`에서 두 peer의 state가
  `Ready`이고 ChannelName의 `ReadyTargetCount=2`인지 확인한다. Connection evidence는 각 MeshNode 쌍에서 RID가 작은
  쪽의 connect attempt만 1건이고 반대쪽 attempt는 0건임을 보여 준다.
- 세부 동작: descriptor 자동 게시 → Store 조회/reconcile → RID order로 connect initiator 결정 →
  endpoint를 지정하지 않은 messaging.

#### RM-A2 수동 endpoint 연결 (대조군)

우선순위: `P0`

**검증 질문:** location store 없이 endpoint를 직접 등록해도 자동 discovery와 같은 messaging 결과를
제공하는가.

- 절차: 첫 반복은 consumer 한쪽에만 provider endpoint를 등록한다. 두 번째 반복은 서로의 endpoint를
  양쪽에 등록하고 connect를 동시에 시작한 뒤 request를 보낸다.
- 검증: 두 반복 모두 지정한 provider에서 처리하며 automatic 경로와 같은 reply 의미를 제공한다.
  양쪽 connect 반복은 handshake와 admission에서 같은 RID·lifecycle generation의 중복 후보를 확인하고
  ready 연결 하나만 유지한다. Automatic reconcile은 manual endpoint를 제거하지 않는다.
- 세부 동작: Manual topology의 one-sided·simultaneous connect와 duplicate admission.

> ChannelName은 process-local egress index가 선택하고 MeshName을 호출자에게 요구하지 않는다.
> Spot direct는 global Spot ID를 받고 current owner를 Location Store에서 resolve한다. `SpotRef`는
> create·find·exact close에 사용하는 immutable snapshot이며 messaging target은 아니다.

#### RM-A3 Object Client pair의 연결 필요 판정

우선순위: `P0`

**검증 질문:** Object Client 두 node의 RouteMesh Server capability를 기준으로 필요한 connection만
만들고 불필요한 reconnect loop를 피하는가.

- 절차:
  1. Automatic 반복에서 `client-a`와 `client-b`를 같은 MeshName과 Location Store로
     시작한다. 두 descriptor가 보인 뒤 20초 동안 상태와 connect evidence를 수집한다.
  2. Manual 반복에서 runner가 두 endpoint를 서로 등록한다. 양쪽 connect를 동시에
     시작하고 handshake 뒤 20초 동안 상태와 retry evidence를 수집한다.
  3. Automatic과 Manual 반복에서 한쪽 Object Client에 RouteMesh `Channel(...).Server()`를
     추가하고 weight `100`으로 시작한다. 같은 구성을 weight `0`으로 다시 실행한다.
  4. 양쪽에 Channel Client membership만 등록한 반복을 실행한다.
  5. 같은 두 host에 independent ClientServer와 classic fanout registration만 추가한 반복을
     실행한다.
  6. Object Client에 application Node direct handler를 등록하는 negative host를 시작하고,
     별도 정상 Object Client RID를 Node direct target으로 지정한다.
- 검증:
  - Automatic 반복은 두 descriptor를 발견하지만 connect attempt, ready peer와
    liveness probe가 모두 0이다.
  - Manual 반복은 각 intent가 최대 한 번 handshake를 시작하고 양쪽 Object role을
    확인한 뒤 `NotRequired` terminal로 끝난다. Ready 전 socket을 닫고 같은 endpoint와
    configuration generation에 reconnect하지 않는다.
  - Public RouteMesh status는 이 peer를 `NotRequired`로 표시한다.
    `ReadyPeerCount`, liveness probe·failure와 topology health failure에는 포함하지 않는다.
  - 어느 한쪽에라도 RouteMesh Channel Server membership이 있으면 두 Object Client는 ready
    connection 하나를 유지한다. Weight `0`도 Server capability이므로 connection을 생략하지 않는다.
    Weight `0` Server는 새 Channel operation의 선택 후보에서만 제외한다.
  - Channel Client membership만 있는 반복은 기본 반복과 같이 `NotRequired`다.
  - ClientServer와 classic fanout registration은 RouteMesh 연결 필요 판정을 바꾸지 않는다.
  - 연결이 필요한 대조군의 connection을 끊으면 같은 status가 `NotConnected`가 된다.
    `NotRequired`로 바뀌지 않으며 topology와 liveness 장애 집계에 반영된다.
  - Object Client와 RouteMesh Channel Server 조합은 startup에 성공한다.
  - Object Client의 application Node direct handler 등록은 socket bind 전에
    configuration error로 실패한다.
  - Node direct 호출은 다른 RID로 바뀌지 않고 `NotFound`로 끝난다.
  - 대조군 Object Client↔Object Server와 Object Server↔Object Server는 기존 RID
    initiator 규칙으로 ready connection 하나를 유지한다.
- 세부 동작: Object Client pair의 RouteMesh Server capability 판정, automatic 사전 제외,
  manual terminal admission, retry 억제와 Node direct target 제한.

#### RM-A4 새 RID·endpoint replacement

우선순위: `P0`

**검증 질문:** Automatic provider를 같은 application 역할의 새 process로 교체하면 새 RID와 transport
connection을 사용하고, consumer가 이전 connection으로 계속 요청하지 않는가.

- 절차: Provider v1을 automatic RID `api-a-<suffix1>`과 runner가 기록한 bound endpoint p1로 시작 →
  request로 v1 evidence 확인 → v1에 정상 종료를 요청 → host `Shutdown` terminal `Stopped/None`과 consumer
  RouteMesh status에서 v1 제외를 확인 → provider v2를 같은 `api-a` 역할 prefix와 새 automatic RID
  `api-a-<suffix2>`·bound endpoint p2로 시작 → consumer status에서 새 RID가 `Ready`가 될 때까지 대기 →
  consumer 재시작 없이 다시 request.
  Crash 뒤 lease 만료를 거치는 replacement는
  Config 5 RL-A2가 별도로 검증한다. 별도 반복에서는 첫 descriptor `NewClaim`에 같은 `(MeshName, RID)`의
  active conflict를 주입하고 UUID 생성 횟수와 Store claim 횟수를 기록한다.
- 검증: v2 시작 전 v1 Node RID가 ready peer에서 제외된다. v2 시작 뒤 public status에서
  `suffix2 != suffix1`인 새 Node RID가 Ready이고 두 suffix는 모두 RFC 4122 version 4·variant bit와
  lowercase canonical `8-4-4-4-12` 형식을 만족한다. Endpoint는 public status에 포함하지 않는다.
  교체 뒤 신규 request는 v2 evidence에 기록되고 consumer의 이전 connection에는 새 write가 없다. 이후
  연속 20개 request가 모두 성공한다. Active conflict 반복은 기존 descriptor mutation 없이 startup
  startup configuration error로 끝나고 두 번째 UUID 생성과 두 번째 descriptor claim은 0건이다.
- 세부 동작: application 역할 replacement의 새 automatic identity 반영 + stale 회피. 이미 실행 중인 다른 provider가
  장애 직후 처리를 계속하는 failover는 RM-B3에서 별도로 검증한다.

> 런타임 연결 수립/재시도/해제 제어 handle은 channel messaging public API에 없다(endpoint는 startup 설정). timeout 규칙 검증은 RM-C4가 다룬다.

#### RM-A6 다중 channel 격리 (같은 store, 다른 mesh)

우선순위: `P1`

**검증 질문:** 한 location store에 여러 MeshName(`profile`, `workflow` 등)의 descriptor가 함께 있어도
각 RouteMesh가 독립적으로 관리되는가.

- 절차: 같은 location store에 서로 다른 MeshName의 provider가 descriptor를 게시하고, consumer가 각
  RouteMesh의 ChannelName으로 request를 보낸다.
- 검증: `GetStatus("profile")`과 `GetStatus("workflow")`는 각 MeshName의 peer·Channel만 반환한다. 같은
  process가 두 MeshName에 참여해도 status sequence와 ready target 수는 MeshName별로 구분된다. 한
  RouteMesh의 scale-in은 다른 RouteMesh의 status와 routing에 영향을 주지 않는다.
- 세부 동작: MeshName 기반 runtime 격리.

#### RM-A7 global Actor·Spot identity 충돌

우선순위: `P0`

**검증 질문:** 서로 다른 Mesh에서 같은 Actor ID 또는 Spot ID를 동시에 생성해도 Location Store
namespace 전체에서 authority 하나로 수렴하는가.

- 절차: `profile`과 `workflow` Object Server에서 같은 global Actor ID와 Spot ID의 `GetOrCreate`를
  각각 동시에 시작하되 서로 다른 initial `InMesh`를 지정한다. 그 뒤 manager `Find`와
  Actor·Spot direct request를 각각 실행한다.
- 검증: ID별 authority CAS winner 하나만 성공하고 loser는 이미 고정된 stable type·kind과 다르면
  typed mismatch로 끝난다. `Find`는 current owner의 `ActorRef`·`SpotRef` 하나를 반환하고 direct
  request는 MeshName을 받지 않으며 current owner evidence에만 기록된다. 같은 ID의 Mesh별
  authority row나 두 번째 object generation이 생기면 실패다.
- 회귀: current owner가 있는 Mesh와 다른 `InMesh`를 후속 create에 지정해도 기존 owner를 이동시키지
  않고 같은 current reference를 유지한다.

> Track A의 `A3`·`A5`는 예약 번호로 유지한다. 신규 시나리오는 이 번호를 재사용하지 않고 새 번호를 사용한다.

### Track B — scale

#### RM-B1 scale-out (트래픽 중 provider 추가)

우선순위: `P0`

**검증 질문:** 트래픽 처리 중 provider를 추가해도 consumer 재시작 없이 새 provider가 routing 대상에
포함되는가.

- 절차: Provider A만으로 request를 보낸다 → provider B를 추가로 시작 → public RouteMesh status에 B가
  ready peer·Channel target으로 반영될 때까지 대기 → request를 여러 개 보낸다.
- 검증: B 추가 전엔 A만 처리. 반영 완료 뒤 검증 구간에선 A·B 모두 routing 대상. consumer 재시작 없음.
- 세부 동작: 무중단 provider 증설 반영(descriptor 추가 → reconcile connect).

#### RM-B2 scale-in / 정상 종료

우선순위: `P0`

**검증 질문:** provider 한 대를 정상 종료해 제외해도, consumer가 종료된 endpoint로 요청하지 않고 남은
provider로만 처리하는가.

- 절차: A·B로 분산을 확인 → B에 host `Shutdown` 요청 → terminal `Stopped/None`과 public RouteMesh
  status에서 B가 ready peer·Channel target에서 제외된 것을 확인 → 다시 request.
- 검증: B 종료 뒤 request는 A로만 처리된다. 정상 종료이므로 B의 descriptor는 owner lease 만료를 기다리지
  않고 `Shutdown` 경로에서 제거된다. target 미지정 지속 request는 `Draining` 전파 구간을 포함해 모두 정상
  reply로 끝나며 오류와 pending이 남지 않는다. consumer가 종료된 endpoint로 timeout을 반복하지 않는다.
- 세부 동작: 무중단 provider 감축 + shutdown 시 descriptor 제거 + stale 정리.

#### RM-B3 provider crash failover

우선순위: `P0`

**검증 질문:** provider 하나가 비정상 종료되어도 이미 실행 중인 다른 provider가 신규 요청을 계속
처리하는가.

- 절차: provider A·B가 모두 routing 대상임을 확인한다 → 처리 시간을 제어할 수 있는 request가 A
  handler에서 시작했다는 evidence가 기록되면 완료시키지 않은 상태에서 A를 `SIGKILL`한다 → consumer를
  재시작하지 않고 crash 전파 구간에 target 미지정 신규 request 20개를 보낸다 → owner lease 만료 뒤
  public RouteMesh status에서 A가 ready peer·Channel target에서 제외되는지 확인한다 → target 미지정 신규 request
  20개를 보낸다 → 수동 peer 구성을 유지한 RouteMesh에서 `RequestToNode(A)`와 등록한 적 없는 rid
  `api-missing`을 대상으로 한 `RequestToNode`를 각각 한 건 보낸다.
- 검증: A handler-start evidence가 있는 in-flight request는 연결 종료가 먼저 관측되면
  `Unavailable`, handler 완료 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의
  `DeadlineExceeded`로 끝나며 무한 대기하지 않는다. Framework가 그 request를 B로 자동 재전송하지 않는다.
  crash 전파 구간의 target 미지정 request는 B의 정상 reply, reply보다 연결 종료가 먼저 request 완료로
  전달된 `Unavailable`, 또는 request deadline이 먼저 도달한 timeout 중 하나로 유한 시간 안에
  끝나며, B의 성공 reply가 하나 이상 있어야 한다. A가 성공 topology 조회에서 제외된 뒤 보낸 20개는
  모두 B에서 성공한다. 수동 RouteMesh에는 A의 membership이 남아 있으므로 `RequestToNode(A)`는 readiness
  한계 뒤 `Unavailable`, member snapshot에 없는 `api-missing`은 `NotFound`로 끝난다.
  이 시나리오는 A의 메모리 상태를 B로 자동 이전한다고 단언하지 않는다.
- 세부 동작: ChannelName provider crash 격리와 신규 부하 failover + stale descriptor 제외.

### Track C — resolve된 연결 위의 messaging 세부 동작

아래는 RM-A1에서 만든 location store 기반 자동 연결을 그대로 재사용해, messaging verb와
negative path를 하나씩 점검한다.

#### RM-C1 request / send happy path

우선순위: `P0`

**검증 질문:** request는 reply가 정확히 오고, send(단방향)는 reply 없이 server에 기록만 남는가.

- 검증: `request`는 정확한 reply. `send`(one-way)는 reply 없이 provider send-handler evidence에 command id 기록.

#### RM-C2 targeted request by rid

우선순위: `P0`

**검증 질문:** RID를 지정한 request가 해당 provider에서만 처리되고, 등록되지 않은 RID는 정해진
public error로 실패하는가.

- 절차: route mesh로 특정 rid(`api-b`)에 request. 그리고 미존재 rid로 request.
- 검증: 지정 rid provider에만 도달(다른 provider evidence엔 없음). 미존재 rid는 public error로 실패.
- 세부 동작: rid 타깃 routing의 정확성.

#### RM-C3 다중 provider 분산

우선순위: `P0`

**검증 질문:** provider 둘을 manual peer로 등록하고 여러 request를 보내면 두 provider가 모두 처리하며
처리 합계가 전송 수와 일치하는가.

- 절차: consumer가 두 provider endpoint를 언어별 peer-connection interface로 등록한 뒤, readiness 확인 후 충분한 수의 request(예: 90개)를 보낸다.
- 검증: 두 provider가 모두 ready member가 되고, 각 provider evidence 합이 전체 request 수와 일치한다.
  동일 weight의 round-robin은 장기 분포와 합계로 검증하며 요청별 정확한 교대 순서는 보장하지 않는다.
- 세부 동작: 다중 provider 부하 분산(수동 multi-endpoint).

#### RM-C4 timeout과 late reply 비오염

우선순위: `P0`

**검증 질문:** 느린 요청이 `DeadlineExceeded`로 끝난 뒤, 뒤늦게 도착한 reply가 다음 정상 요청을 오염시키지 않는가.

- 절차: `value=="slow"` request를 짧은 timeout으로 보내 client timeout을 유도 → 곧바로 정상 request → 잠시 뒤 또 정상 request.
- 검증: 첫 request는 `DeadlineExceeded`다. 이후 request는 정상 reply다. Late reply가 뒤 요청을 오염시키지 않으며 느린 handler는 server에서 완료될 수 있다.
- 세부 동작: timeout 경로 + 연결 비오염.

#### RM-C5 미등록 packet 처리

우선순위: `P0`

**검증 질문:** handler가 없는 packet을 보냈을 때, request는 error로 명확히 실패하고 send는 조용히 drop되며, 그 이유가 observer에 정확히 남는가.

- 절차: handler 없는 packet 이름으로 request, 그리고 send.
- 검증: request는 **error reply로 실패**하고(client는 예외로 받음), observer evidence의 reason/action은 `no_handler`/`reply_error`다. send는 reply 없이 drop되고 observer reason/action은 `no_handler`/`drop`이다. 다른 정상 request는 영향 없음.
- 세부 동작: negative path(client-visible error + observer) 구분.

#### RM-C7 weighted 분산 (server쪽 weight 차등)

우선순위: `P1`

**검증 질문:** 두 ChannelName member의 weight를 다르게 설정하면 select-one 분포가 높은 weight의
member를 더 자주 선택하는가.

- 절차: startup의 `Channel("profile").Server().SetWeight(...)`로 `api-a=300`, `api-b=100`을
  설정한다. 실행 중 변경 시나리오는 `IZLinkRouteMeshRuntimeOptions.Channel(channelName).Weight`를
  사용한다. 별도 반복에서 startup과 runtime update에 `0`, 기본값 `100`, `10000`, `-1`, `10001`을
  적용하고, 많은 member의 `10000`을 합산해 32-bit 범위를 넘기는 fixture도 사용한다.
  각 설정 뒤 RouteMesh status의 ready target 수와 실제 request 분포를 확인하고 충분한 수의 request
  (예: 200개)를 보낸다.
- 검증: 두 provider 모두 처리 대상이 되고(어느 쪽도 0이 아님), 각 provider evidence 합이 전체 request 수와
  일치한다. 장기 분포는 `300:100`에 수렴하되 요청별 정확한 순서는 단언하지 않는다. `0`, `100`, `10000`은
  startup과 runtime update에서 허용하고 `-1`, `10001`은 descriptor mutation 없이 configuration error다.
  Weight `0`은 새 target에서 제외하고 이미 제출한 operation은 유지한다. 합계는 최소 64-bit로 계산해 overflow나
  음수 wrap 없이 모든 positive member를 선택 후보로 유지한다.
- 세부 동작: ChannelName weight에 따른 select-one 부하 분산.

> ChannelName select-one은 ready positive-weight member만 후보로 사용한다. weight `0`은 신규 select-one과
> Logical Multicast remote target에서 해당 member를 제외하지만 기존 연결을 즉시 해제하지 않는다.
> weight 제외·복원 검증은 Config 5 RL-B4·B5에서 다룬다.

> payload decode 실패는 public typed client로 유도할 수 없다(typed client는 항상 정상 envelope로
> 직렬화). 실제 decode-failure는 raw frame 주입이 필요해 public-API-only인 이 config 범위 밖이며,
> raw-frame contract 테스트(E2ETests DispatchErrors)가 다룬다. channel과 RouteMesh의 decode 실패
> reason이 `decode_error`이고 handler 실행 실패의 `handler_exception`과 구분되는지도 거기서 검증한다.

#### RM-C8 메시지 크기 다양성

우선순위: `P1`

**검증 질문:** 작은 payload부터 큰 payload, 상한 근처까지 모두 정확히 왕복하고, 상한을 넘기면 정해진 error로 거부되는가.

- 절차: 같은 request를 payload 크기만 바꿔 소형, 17 MiB, negotiated `MaxMessageSize` 근접 크기로
  왕복시킨다. 양쪽을 32 MiB로 설정한 조합, sender 32 MiB·receiver 8 MiB 조합과 `0`을 사용하는 조합을
  분리해 실행하고 effective bound를 admission evidence로 기록한다.
- 검증: 32 MiB 조합은 17 MiB와 근접-max payload를 손상 없이 정확히 왕복하므로 숨은 16 MiB 상한이
  없어야 한다. Mismatch 조합은 양쪽 bound의 작은 값보다 큰 payload를 encoding·allocation 전에 정해진
  public error로 거부한다. `0`은 binding/transport receive max를 사용하고 unlimited transport에서는 service
  wire의 u32 representational limit으로 normalize한 non-zero 값을 교환한다. 상한 거부 뒤 정상 크기 request는
  영향 없이 동작한다.
- 세부 동작: negotiated complete-message bound, u32 표현 한계와 allocation 전 거부.

> 주의: 크기 다양성 **왕복**은 public typed client로 바로 유도된다. 상한 초과 거부는 각 언어의 public channel builder가 server socket의 max message size를 live socket에 적용한 뒤 검증한다. public typed client는 항상 정상 envelope를 만들기 때문에, payload decode 실패처럼 raw frame이 필요한 경로는 이 config 범위가 아니라 binding/raw-frame contract 테스트에서 다룬다.

#### RM-C9 backpressure / HWM 포화

우선순위: `P2`

**검증 질문:** provider가 느려 send 처리 backlog가 쌓여도 one-way send 호출자가
결과 데이터 없는 정상 완료나 실패를 유한 시간 안에 관찰하고, 적체 해소 뒤 연결과 후속 request가 정상으로
남는가.

- 절차: provider handler를 느리게 두고, client가 처리 속도보다 빠르게 다량 request/send를 보내
  송신 큐를 HWM까지 채운다. 이 상태에서 public asynchronous submit을 실행하고 MeshNode send timeout과
  caller cancellation을 각각 경쟁시킨다.
- 검증: 비동기 terminal은 queue admission 뒤 반환 데이터 없이 정상 완료하거나 timeout·cancellation 중
  먼저 확정된 실패로 유한 시간 안에 끝난다. Backpressure는 send timeout까지 기다리는 내부 상태이며
  public status로 반환하지 않는다. 별도 동기 `TrySubmit`이나 즉시 한 번만 시도하는 public path는 사용하지
  않는다. 정상 완료는 remote handler 완료가 아니라 송신 큐 admission을 뜻한다. 연결이 깨지거나 다른 정상 트래픽이 오염되지
  않고, 적체가 풀리면 follow-up request와 provider evidence가 정상으로 회복된다.
- 세부 동작: 송신 HWM 포화 시 backpressure 계약.

> 주의: send는 one-way operation이므로 public 비동기 terminal은 admission까지만 보장하고 remote
> handler 완료를 보고하지 않는다. 호출자는 send-ready 재시도, peer queue와 transport 정책을
> 알 필요 없이 정상 완료 또는 예외만 처리한다.

#### RM-C10 descriptor registration bound

우선순위: `P0`

**검증 질문:** Host가 공개 registration을 모두 반영한 descriptor의 공통 상한을 startup에서 원자적으로
검증하고 언어별로 truncate하거나 나누어 게시하지 않는가.

- 절차: Encoded descriptor가 정확히 1 MiB 이하인 경계와 1 MiB 초과인 host를 각각 시작한다. Stable type
  bound fixture는 Object Server에 `DisableRelocation` policy의 distinct stable factory type 1,024개와 1,025개를
  등록한다. State-preserving adapter bound fixture는 Object Server에 `PreserveStateWith` policy와 factory kind에 맞는
  distinct adapter capability를 1,024개와 1,025개 등록한다. 모든 fixture는 Location Store, positive
  placement weight와 Actor total·Spot total limit `2,048`, activation concurrency `128`을 사용한다. State-preserving fixture는 별도 Relocation
  Store도 등록해 Store 누락 오류가 descriptor bound 결과를 가리지 않게 한다.
- 검증: 경계 이하는 descriptor 하나로 게시되고 모든 entry가 보존된다. 한 상한이라도 넘으면 host startup이
  stable configuration error로 전체 실패하며 descriptor, owner lease와 partial capability가 Store에 남지 않는다.
  Truncate, 여러 descriptor로 split과 언어별 다른 상한은 허용하지 않는다.

## 5. 완료 기준

- Track A·B·C의 `P0` 시나리오가 모두 통과한다. `RM-C10` 음성 startup case도 required다.
- 각 시나리오는 public contract만 직접 호출하고 `ensure`로 단언한다. 연결 상태는
  `IZLinkRouteMeshRuntime.GetStatus(meshName)`으로 검증하며 Store provider를 application query API로
  사용하지 않는다.
- Redis를 쓰는 실행은 전용 key prefix로 격리하고, 실행 후 key cleanup 또는 disposable Redis instance를 사용한다.
- 실패 시 store 연결 상태와 provider/consumer 로그·evidence로 원인 레이어를 분리한다.
