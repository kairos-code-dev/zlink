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

이 config에는 registry process가 없다. 위치 정보의 기준 저장소는 location store이고, 연결
상태의 검증 기준도 registry topology 조회가 아니라 아래 두 가지다.

- store descriptor: 등록한 `IZLinkLocationStore`의 `ListMeshNodesAsync(meshName)`로 같은 MeshName의
  `ZLinkMeshNodeDescriptor`를 확인한다. descriptor의 `ChannelWeights`가 immutable ChannelName 집합과
  현재 weight를 제공한다.
- framework connection state: `IZLinkRouteMeshRuntime.Snapshot(meshName)`의 `Peers`와 `Channels`로
  admission·ready member 상태를 확인하고, 실제 messaging과 각 역할 server의 evidence를 함께 단언한다.

location row 모델, owner lease, freshness 같은 계약 상세는 이 문서에서 반복하지 않는다.
[location runtime spec](../../spec/server/40-location-runtime.ko.md)을 기준으로 한다.

여기서 다루지 않는 것(다른 config로): codec, stream, spot/actor, resilience 세부, store
장애/복구(Config 6). 이 config는 messaging + 연결/resolve에만 집중한다.

## 2. 서버 구성 (한 번 구동, 공유)

스크립트가 아래 구성을 한 번 시작하고 모든 client 시나리오가 함께 쓴다. scale·failover
시나리오만 provider 프로세스를 추가로 시작하거나 종료하고, weighted 시나리오(RM-C7)는 build-time
weight를 다르게 준 provider를 따로 시작한다(공유 provider는 기본 weight `100`).

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix로 격리한다. 별도 registry process는 실행하지 않는다. |
| provider (api 노드) | 2 (`api-a`, `api-b`) | `AddRouteMesh(meshName)`으로 MeshNode를 만들고 profile `ChannelName` membership에 request handler(`ProfileRequest`)·send handler(`ProfileCommand`)를 등록한다. RID direct route handler(`ScenarioRoutePing`)는 같은 MeshNode에 등록하고 routing id는 `api-a`/`api-b`다. dispatch-error observer로 evidence를 기록하며 테스트용 `/evidence`·`/health` HTTP endpoint를 함께 제공한다. |
| consumer | 시나리오별 | location store를 사용하면 같은 MeshName의 descriptor에서 peer를 자동으로 확인한다. manual topology에서는 언어별 peer-connection interface로 endpoint를 등록한다. inbound가 필요한 MeshNode만 endpoint를 bind하고 모든 MeshNode는 routing ID를 설정한다. 양쪽에 endpoint가 있으면 pairwise initiator 한쪽만 연결을 시작한다. |

각 provider는 MeshName, RID, lifecycle generation, ROUTER endpoint와 `ChannelWeights`를 포함한
MeshNode descriptor를 framework lifecycle을 통해 store에 게시한다. consumer는 MeshName과
ChannelName만 지정하며 endpoint는 descriptor에서 확인한다. 이 config는 descriptor를 application
코드에서 직접 갱신하거나 제거하지 않는다. manual topology 시나리오만 peer endpoint를
언어별 peer-connection interface로 지정한다. RouteMesh 구성원은 모두 `ROUTER`다.

store 등록은 각 역할의 `*HostFactory`에서 바로 보이게 둔다.

```csharp
// 공식 Redis extension은 peer/spot/actor/route store와 owner lease store를 결합한
// 통합 계약 인스턴스 하나다. 전용 등록 함수는 없고 AddLocationStore로 등록한다.
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
- 검증: request가 `api-a`/`api-b` 중 하나에서 처리된다(reply의 provider RID로 확인). consumer는
  endpoint를 코드에 지정하지 않는다. `ListMeshNodesAsync(meshName)`에서 owner lease가 유효한 두
  descriptor를 확인하고, `IZLinkRouteMeshRuntime.Snapshot(meshName)`에서 두 peer의 `Ready=true`와
  ChannelName의 `ReadyMemberCount=2`를 확인한다.
- 세부 동작: descriptor 자동 게시 → store 조회/reconcile → endpoint를 지정하지 않은 messaging.

#### RM-A2 수동 endpoint 연결 (대조군)

우선순위: `P0`

**검증 질문:** location store 없이 endpoint를 직접 등록해도 자동 discovery와 같은 messaging 결과를
제공하는가.

- 절차: consumer가 location store 자동 연결 없이 provider endpoint를 언어별 peer-connection interface로 등록하고 request를 보낸다.
- 검증: 지정한 provider에서 처리. 자동 resolve 경로와 같은 reply 의미. auto reconcile은 manual endpoint를 끊지 않는다(manual 연결 우선).
- 세부 동작: 수동 연결이 자동 연결과 동일 의미임을 고정.

> custom resolver는 ChannelName public API에 없다. SPOT 전송 대상 조회는 location store 기반 **spot handle resolver**가 담당하며, 반환 값은 불투명한 `SpotHandle`이다. `SpotRef`는 framework 내부 주소 snapshot이라 public 표면에 노출하지 않는다([24 §2](../../spec/server/24-spot-address-messaging.ko.md)).

#### RM-A4 같은 rid, 다른 endpoint replacement

우선순위: `P0`

**검증 질문:** 같은 rid의 provider를 다른 endpoint의 process로 교체해도, consumer가 새 endpoint를
사용하고 이전 endpoint로 계속 요청하지 않는가.

- 절차: provider v1을 rid `api-a`/endpoint p1로 시작 → request로 v1 evidence 확인 → v1에 정상 종료를
  요청 → terminal `Drained`와 descriptor snapshot에서 v1 제외를 확인 → provider v2를 같은 RID
  `api-a`/endpoint p2로 시작 → `ListMeshNodesAsync(meshName)` 결과가 RID `api-a`의 endpoint를 p2로
  보여줄 때까지 대기 → consumer 재시작 없이 다시 request. crash 뒤 lease 만료를 거치는 replacement는
  Config 5 RL-A2가 별도로 검증한다.
- 검증: v2 시작 전 v1 descriptor가 성공 조회에서 제외된다. v2 시작 뒤
  `ListMeshNodesAsync(meshName)` 결과에서 RID `api-a`의 유효한 descriptor는 하나이고 endpoint가 p2다.
  교체 뒤 신규 request는 p2 evidence에 기록되고 consumer가 p1 stale endpoint로 요청하지 않는다. 이후
  연속 20개 request가 모두 성공한다.
- 세부 동작: 같은 MeshNode key의 endpoint replacement 반영 + stale 회피. 이미 실행 중인 다른 provider가
  장애 직후 처리를 계속하는 failover는 RM-B3에서 별도로 검증한다.

> 런타임 연결 수립/재시도/해제 제어 handle은 channel messaging public API에 없다(endpoint는 startup 설정). timeout 규칙 검증은 RM-C4가 다룬다.

#### RM-A6 다중 channel 격리 (같은 store, 다른 mesh)

우선순위: `P1`

**검증 질문:** 한 location store에 여러 MeshName(`profile`, `workflow` 등)의 descriptor가 함께 있어도
각 RouteMesh가 독립적으로 관리되는가.

- 절차: 같은 location store에 서로 다른 MeshName의 provider가 descriptor를 게시하고, consumer가 각
  RouteMesh의 ChannelName으로 request를 보낸다.
- 검증: `ListMeshNodesAsync(meshName)`는 지정한 MeshName의 descriptor만 반환한다. 같은 process가 두
  MeshName에 참여해도 descriptor와 runtime snapshot은 MeshName별로 구분된다. 한 RouteMesh의 scale-in은
  다른 RouteMesh의 descriptor와 routing에 영향을 주지 않는다.
- 세부 동작: MeshName 기반 descriptor 격리.

> Track A의 `A3`·`A5`는 예약 번호로 유지한다. 신규 시나리오는 이 번호를 재사용하지 않고 새 번호를 사용한다.

### Track B — scale

#### RM-B1 scale-out (트래픽 중 provider 추가)

우선순위: `P0`

**검증 질문:** 트래픽 처리 중 provider를 추가해도 consumer 재시작 없이 새 provider가 routing 대상에
포함되는가.

- 절차: provider A만으로 request를 보낸다 → provider B를 추가로 시작 → descriptor와 runtime
  snapshot에 B가 ready member로 반영될 때까지 대기 → request를 여러 개 보낸다.
- 검증: B 추가 전엔 A만 처리. 반영 완료 뒤 검증 구간에선 A·B 모두 routing 대상. consumer 재시작 없음.
- 세부 동작: 무중단 provider 증설 반영(descriptor 추가 → reconcile connect).

#### RM-B2 scale-in / 정상 종료

우선순위: `P0`

**검증 질문:** provider 한 대를 정상 종료해 제외해도, consumer가 종료된 endpoint로 요청하지 않고 남은
provider로만 처리하는가.

- 절차: A·B로 분산을 확인 → B에 정상 종료 요청 → terminal `Drained`와 store/runtime snapshot에서
  B가 제외된 것을 확인 → 다시 request.
- 검증: B 종료 뒤 request는 A로만 처리된다. 정상 종료이므로 B의 descriptor는 owner lease 만료를 기다리지
  않고 shutdown 경로에서 제거된다. target 미지정 지속 request는 drain 전파 구간을 포함해 모두 정상
  reply로 끝나며 오류와 pending이 남지 않는다. consumer가 종료된 endpoint로 timeout을 반복하지 않는다.
- 세부 동작: 무중단 provider 감축 + shutdown 시 descriptor 제거 + stale 정리.

#### RM-B3 provider crash failover

우선순위: `P0`

**검증 질문:** provider 하나가 비정상 종료되어도 이미 실행 중인 다른 provider가 신규 요청을 계속
처리하는가.

- 절차: provider A·B가 모두 routing 대상임을 확인한다 → 처리 시간을 제어할 수 있는 request가 A
  handler에서 시작했다는 evidence가 기록되면 완료시키지 않은 상태에서 A를 `SIGKILL`한다 → consumer를
  재시작하지 않고 crash 전파 구간에 target 미지정 신규 request 20개를 보낸다 → owner lease 만료 뒤
  store descriptor와 MeshNode runtime snapshot에서 A가 제외되는지 확인한다 → target 미지정 신규 request
  20개를 보낸다 → 수동 peer 구성을 유지한 RouteMesh에서 `RequestToNode(A)`와 등록한 적 없는 rid
  `api-missing`을 대상으로 한 `RequestToNode`를 각각 한 건 보낸다.
- 검증: A handler-start evidence가 있는 in-flight request는 연결 종료가 먼저 관측되면 retriable
  `RouteNotConnected`, handler 완료 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의
  timeout으로 끝나며 무한 대기하지 않는다. framework가 그 request를 B로 자동 재전송하지 않는다.
  crash 전파 구간의 target 미지정 request는 B의 정상 reply, reply보다 연결 종료가 먼저 request 완료로
  전달된 `RouteNotConnected`, 또는 request deadline이 먼저 도달한 timeout 중 하나로 유한 시간 안에
  끝나며, B의 성공 reply가 하나 이상 있어야 한다. A가 성공 topology 조회에서 제외된 뒤 보낸 20개는
  모두 B에서 성공한다. 수동 RouteMesh에는 A의 membership이 남아 있으므로 `RequestToNode(A)`는 readiness
  한계 뒤 `RouteNotConnected`, member snapshot에 없는 `api-missing`은 `RequestTargetNotFound`로 끝난다.
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

**검증 질문:** 느린 요청이 timeout으로 끝난 뒤, 뒤늦게 도착한 reply가 다음 정상 요청을 오염시키지 않는가.

- 절차: `value=="slow"` request를 짧은 timeout으로 보내 client timeout을 유도 → 곧바로 정상 request → 잠시 뒤 또 정상 request.
- 검증: 첫 request는 timeout 예외. 이후 request는 정상 reply. late reply가 뒤 요청을 오염시키지 않음. 느린 handler도 결국 server에선 완료로 기록.
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

- 절차: startup의 `ChannelName("profile").SetWeight(...)`로 `api-a=75`, `api-b=25`를 설정한다. 실행 중
  변경 시나리오는 `IZLinkRouteMeshRuntimeOptions.Channel(meshName, channelName).Weight`를 사용한다.
  `ListMeshNodesAsync(meshName)`의 descriptor `ChannelWeights`와 local runtime option getter가 같은 값을
  제공하는지 확인한 뒤 충분한 수의 request(예: 200개)를 보낸다.
- 검증: 두 provider 모두 처리 대상이 되고(어느 쪽도 0이 아님), 각 provider evidence 합이 전체 request 수와 일치한다. 분산은 weight 비율을 따라 `api-a`가 `api-b`보다 **뚜렷이 많이** 처리한다(정확한 75/25는 보장값이 아니므로 "고weight가 저weight보다 분명히 많음 + 양쪽 모두 처리 + 합계 일치"로 검증한다).
- 세부 동작: descriptor의 ChannelName weight에 따른 select-one 부하 분산.

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

- 절차: 같은 request를 payload 크기만 바꿔(소형, 대형, `MaxMessageSize` 근접) 왕복시키고, 상한을 넘는 payload도 한 번 보낸다.
- 검증: 소형·대형·근접-max payload는 내용이 손상 없이 정확히 왕복한다(대형도 분할/재조립이 투명). `MaxMessageSize`를 넘는 payload는 정해진 public error로 거부되고, 그 뒤 정상 크기 request는 영향 없이 동작한다.
- 세부 동작: payload 크기 경계(왕복 + 상한 거부).

> 주의: 크기 다양성 **왕복**은 public typed client로 바로 유도된다. 상한 초과 거부는 각 언어의 public channel builder가 server socket의 max message size를 live socket에 적용한 뒤 검증한다. public typed client는 항상 정상 envelope를 만들기 때문에, payload decode 실패처럼 raw frame이 필요한 경로는 이 config 범위가 아니라 binding/raw-frame contract 테스트에서 다룬다.

#### RM-C9 backpressure / HWM 포화

우선순위: `P2`

**검증 질문:** provider가 느려 send 처리 backlog가 쌓여도 one-way send 호출자가
admission 결과를 유한 시간 안에 관찰하고, 적체 해소 뒤 연결과 후속 request가 정상으로
남는가.

- 절차: provider handler를 느리게 두고, client가 처리 속도보다 빠르게 다량 request/send를 보내
  송신 큐를 HWM까지 채운다. 이 상태에서 non-blocking submit과 send timeout을 적용한 blocking
  submit을 각각 실행한다.
- 검증: non-blocking submit은 즉시 `backpressured`, blocking submit은 admission 성공 또는
  정해진 timeout/backpressure 결과로 유한 시간 안에 끝난다. `submitted`는 remote handler 완료가
  아니라 송신 큐 admission을 뜻한다. 연결이 깨지거나 다른 정상 트래픽이 오염되지
  않고, 적체가 풀리면 follow-up request와 provider evidence가 정상으로 회복된다.
- 세부 동작: 송신 HWM 포화 시 backpressure 계약.

> 주의: send는 one-way operation이므로 public submit 결과는 admission까지만 보장하고 remote
> handler 완료를 보고하지 않는다. 호출자는 send-ready 재시도, peer queue와 transport 정책을
> 알 필요 없이 정식 submit result만 처리한다.

## 5. 완료 기준

- Track A·B·C의 `P0` 시나리오가 모두 통과한다.
- 각 시나리오는 public contract만 직접 호출하고 `ensure`로 단언한다. store 상태는
  `ListMeshNodesAsync(meshName)`, 연결 상태는 `IZLinkRouteMeshRuntime.Snapshot(meshName)`으로 검증한다.
- Redis를 쓰는 실행은 전용 key prefix로 격리하고, 실행 후 key cleanup 또는 disposable Redis instance를 사용한다.
- 실패 시 store 연결 상태와 provider/consumer 로그·evidence로 원인 레이어를 분리한다.
