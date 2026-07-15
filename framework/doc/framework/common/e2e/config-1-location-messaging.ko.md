<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [다음: Spot 서비스](config-2-spot-service.ko.md)
<!-- framework-adapter-nav:end -->

# Config 1 — Location store 기반 messaging 배포

첫 e2e config다. 실제 배포와 똑같이 생긴 서버를 한 번 띄워 두고, 그 위에서 messaging과
연결·rid resolve가 실제 사용자가 쓰듯 잘 도는지를 확인한다. 연결 대상 정보는 registry
process가 아니라, 각 노드가 공유 location store에 자동 등록한 peer location row에서 온다.

## 1. 목적과 범위

확인하려는 것은 단순하다. 공유 location store를 두고, provider가 자기 위치를 자동 등록하고,
consumer가 endpoint를 코드에 적지 않은 채 자동 연결하고, 프로세스 경계까지 진짜로 나눈 상태 —
즉 **배포 현장과 같은 조건**에서 messaging과 연결·rid resolve가 의도대로 도는가.

기존 unit/contract 테스트와 단언이 겹쳐도 괜찮다. 차별점은 "새로운 단언"이 아니라 "현실적인
배포 컨텍스트 + 샘플 수준의 public API 사용"이다. 그래서 각 시나리오는 helper 없이 public
contract를 직접 호출하고 `ensure`로 단언해서, 실제 사용 흐름이 한눈에 보이게 쓴다.

이 config에는 registry process가 없다. 위치 정보의 기준 저장소는 location store이고, 연결
상태의 검증 기준도 registry topology 조회가 아니라 아래 두 가지다.

- peer location list: 운영 조회는 `IZLinkLocationRuntimeQuery.ListPeerLocationsAsync(filter)`로 raw peer
  row를 확인하고, 사용자 기능 검증은 `IZLinkPeerLocationResolver.ListLivePeersAsync(filter)`로
  live peer를 확인한다. **두 표면 모두 cache를 두지 않으며 매 호출이 store에 도달한다** —
  `Refresh` 같은 freshness 인자는 없다([40 §1](../../spec/server/40-location-runtime.ko.md)).
- framework connection state: 실제 messaging 성공과 각 역할 server의 evidence로 연결이
  실제로 성립했는지를 본다.

location row 모델, owner lease, freshness 같은 계약 상세는 이 문서에서 반복하지 않는다.
[location runtime spec](../../spec/server/40-location-runtime.ko.md)을 기준으로 한다.

여기서 다루지 않는 것(다른 config로): codec, stream, spot/actor, resilience 세부, store
장애/복구(Config 6). 이 config는 messaging + 연결/resolve에만 집중한다.

## 2. 서버 구성 (한 번 구동, 공유)

스크립트가 아래 구성을 한 번 띄우고 모든 client 시나리오가 함께 쓴다. scale·failover
시나리오만 provider 프로세스를 추가로 띄우거나 종료하고, weighted 시나리오(RM-C7)는 build-time
weight를 다르게 준 provider를 따로 띄운다(공유 provider는 기본 weight `100`).

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix로 격리한다. 별도 registry process는 띄우지 않는다. |
| provider (api 노드) | 2 (`api-a`, `api-b`) | 두 channel 종류를 함께 노출한다: ① location store 자동 연결을 쓰는 **client-server channel**(`AddClientServerChannel`) — request handler(`ProfileRequest`)·send handler(`ProfileCommand`); ② peer-wired **route mesh**(`AddRouteMesh`) — route request handler(`ScenarioRoutePing`), routing id `api-a`/`api-b`. dispatch-error observer로 evidence 기록. 테스트용 `/evidence`·`/health` HTTP endpoint. |
| consumer | 시나리오별 | client-server는 location store 기반 자동 연결(endpoint 모름) 또는 명시 endpoint 여러 개로 연결한다. route mesh는 자신이 `ROUTER` route node가 되어, 필요하면 `EnableServer(clientEndpoint)`로 자기 endpoint를 bind하고 `SetRoutingId(...)`로 자기 routing id를 설정하며 peer를 `EnableClient(peerEndpoint)`로 연결한다. endpoint가 없는 `ROUTER`도 유효하며 remote peer를 항상 dial한다. 양쪽에 endpoint가 있으면 pairwise initiator 한쪽만 dial한다. |

client-server channel provider는 자기 logical routing id(`api-a`, `api-b`)와 channel
endpoint를 담은 peer location row를 framework lifecycle이 store에 자동 upsert한다. 그래서
consumer는 channel 이름만 알면 되고, 실제 endpoint는 location store에서 resolve된다. 수동
row update/remove API는 이 config에서 사용하지 않는다(위치는 자동 lifecycle로만 갱신된다).
이 config의 route mesh는 location store 자동 연결을 쓰지 않고 peer endpoint를 직접
설정한다. RouteMesh 구성원은 모두 `ROUTER`이며 `DEALER` row나 호환 socket 역할을
사용하지 않는다.

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
띄운 뒤(포트 readiness 확인) client 시나리오를 하나씩 실행한다. 실행이 끝나면 전용 prefix의
key를 정리하거나 disposable Redis instance를 버린다. scale·failover 시나리오는 같은 스크립트가
추가 provider를 띄우거나 종료하고, weighted 시나리오(RM-C7)는 weight를 차등 설정한 provider를
띄운다. client가 `e2e result=passed`를 출력하면 통과로 본다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — 연결과 rid resolve

#### RM-A1 location store 자동 연결 + rid 자동 resolve

우선순위: `P0`

**한마디로:** endpoint를 코드에 한 줄도 안 적고, 공유 location store만 보고 알아서 provider를 찾아 메시지를 보낼 수 있는가.

- 절차: consumer가 endpoint 없이 channel을 등록하고 같은 location store(`AddLocationStore(...)`, 같은 key prefix)를 등록한 뒤, 자동 연결이 성립하면 `ProfileRequest`를 보낸다.
- 검증: request가 `api-a`/`api-b` 중 하나에서 처리됨(reply의 provider rid로 확인). consumer는 endpoint를 코드에 적지 않았다. `IZLinkLocationRuntimeQuery.ListPeerLocationsAsync(filter)`로 두 provider의 peer location row가 살아 있는 owner로 조회되고, 실제 messaging 성공으로 framework connection state가 두 provider와 연결되었음을 확인한다.
- 세부 동작: peer row 자동 등록 → store 조회/reconcile → endpoint 없는 messaging.

#### RM-A2 수동 endpoint 연결 (대조군)

우선순위: `P0`

**한마디로:** location store 없이 endpoint를 직접 적어 붙였을 때도, 자동 연결과 똑같은 결과가 나오는가.

- 절차: consumer가 location store 자동 연결 없이 provider endpoint를 직접 `EnableClient`로 등록하고 request를 보낸다.
- 검증: 지정한 provider에서 처리. 자동 resolve 경로와 같은 reply 의미. auto reconcile은 manual endpoint를 끊지 않는다(manual 연결 우선).
- 세부 동작: 수동 연결이 자동 연결과 동일 의미임을 고정.

> custom resolver는 client-server channel public API에 없다. SPOT 전송 대상 조회는 location store 기반 **spot handle resolver**가 담당하며, 반환 값은 불투명한 `SpotHandle`이다. `SpotRef`는 framework 내부 주소 snapshot이라 public 표면에 노출하지 않는다([24 §2](../../spec/server/24-spot-address-messaging.ko.md)).

#### RM-A4 같은 rid, 다른 endpoint replacement

우선순위: `P0`

**한마디로:** 같은 rid의 provider를 다른 endpoint의 process로 교체해도, consumer가 새 endpoint를
사용하고 이전 endpoint로 계속 요청하지 않는가.

- 절차: provider v1을 rid `api-a`/endpoint p1로 시작 → request로 v1 evidence 확인 → v1에 정상 종료를
  요청 → terminal `Drained`와 peer location 성공 조회에서 v1 row 제거를 확인 → provider v2를 같은 rid
  `api-a`/endpoint p2로 시작 → runtime query의 peer location list가 rid `api-a`의 endpoint를 p2로
  보여줄 때까지 대기 → consumer 재시작 없이 다시 request. crash 뒤 lease 만료를 거치는 replacement는
  Config 5 RL-A2가 별도로 검증한다.
- 검증: v2 시작 전 v1 row가 성공 조회에서 제외된다. v2 시작 뒤
  `ListPeerLocationsAsync(filter)`의 성공 결과에서 rid `api-a`의 유효한 row는 하나이고 endpoint가 p2다.
  교체 뒤 신규 request는 p2 evidence에 기록되고 consumer가 p1 stale endpoint로 요청하지 않는다. 이후
  연속 20개 request가 모두 성공한다.
- 세부 동작: 같은 peer key의 endpoint replacement 반영 + stale 회피. 이미 실행 중인 다른 provider가
  장애 직후 처리를 계속하는 failover는 RM-B3에서 별도로 검증한다.

> 런타임 연결 수립/재시도/해제 제어 handle은 channel messaging public API에 없다(endpoint는 startup 설정). timeout 규칙 검증은 RM-C4가 다룬다.

#### RM-A6 다중 channel 격리 (같은 store, 다른 mesh)

우선순위: `P1`

**한마디로:** 한 location store에 여러 channel(`api`, `workflow` 등)의 peer row가 섞여 있어도, 각 channel의 provider가 서로 섞이지 않고 독립적으로 관리되는가.

- 절차: 같은 location store(같은 key prefix)에 서로 다른 channel(예: `api`, `workflow`)의 provider가 peer row를 등록하고, consumer가 각 channel로 자동 연결해 request를 보낸다.
- 검증: `ListPeerLocationsAsync(filter)`를 mesh name으로 filter하면 각 channel의 peer row 집합이 섞이지 않는다. 같은 endpoint host라도 channel 이름이 다르면 독립 row로 관리된다. 한 channel의 scale-in이 다른 channel의 peer row와 routing에 영향을 주지 않는다.
- 세부 동작: mesh name 기반 peer row 격리.

> Track A의 번호 `A3`·`A5`는 비어 있다(이전 개정에서 빠진 번호 — A3 자리는 위 custom resolver 노트로 갈음). 신규 시나리오는 빈 번호를 재사용하지 않고 뒤에 이어 붙인다.

### Track B — scale

#### RM-B1 scale-out (트래픽 중 provider 추가)

우선순위: `P0`

**한마디로:** 트래픽이 흐르는 도중에 provider를 한 대 더 붙여도, consumer 재시작 없이 새 provider가 routing 대상에 들어오는가.

- 절차: provider A만으로 request를 보낸다 → provider B를 추가로 시작 → runtime query의 peer location list에 B의 row가 반영될 때까지 대기 → request를 여러 개 보낸다.
- 검증: B 추가 전엔 A만 처리. 반영 완료 뒤 검증 구간에선 A·B 모두 routing 대상. consumer 재시작 없음.
- 세부 동작: 무중단 provider 증설 반영(peer row 추가 → reconcile connect).

#### RM-B2 scale-in / 정상 종료

우선순위: `P0`

**한마디로:** provider 한 대를 정상 종료해 제외해도, consumer가 종료된 endpoint로 요청하지 않고 남은
provider로만 처리하는가.

- 절차: A·B로 분산을 확인 → B에 정상 종료 요청 → terminal `Drained`와 runtime query의 peer location
  list에서 B의 row 제거를 확인 → 다시 request.
- 검증: B 종료 뒤 request는 A로만 처리된다. 정상 종료이므로 B의 peer row는 owner lease 만료를 기다리지
  않고 shutdown 경로에서 제거된다. target 미지정 지속 request는 drain 전파 구간을 포함해 모두 정상
  reply로 끝나며 오류와 pending이 남지 않는다. consumer가 종료된 endpoint로 timeout을 반복하지 않는다.
- 세부 동작: 무중단 provider 감축 + shutdown 시 row 제거 + stale 정리.

#### RM-B3 provider crash failover

우선순위: `P0`

**한마디로:** provider 하나가 비정상 종료되어도 이미 실행 중인 다른 provider가 신규 요청을 계속
처리하는가.

- 절차: provider A·B가 모두 routing 대상임을 확인한다 → 처리 시간을 제어할 수 있는 request가 A
  handler에서 시작했다는 evidence가 기록되면 완료시키지 않은 상태에서 A를 `SIGKILL`한다 → consumer를
  재시작하지 않고 crash 전파 구간에 target 미지정 신규 request 20개를 보낸다 → owner lease 만료 뒤
  consumer runtime의 peer location 성공 조회에서 A가 제외되는지 확인한다 → target 미지정 신규 request
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
- 세부 동작: client-server provider crash 격리와 신규 부하 failover + stale row 제외.

### Track C — resolve된 연결 위의 messaging 세부 동작

아래는 RM-A1에서 만든 location store 기반 자동 연결을 그대로 재사용해, messaging verb와
negative path를 하나씩 점검한다.

#### RM-C1 request / send happy path

우선순위: `P0`

**한마디로:** request는 reply가 정확히 오고, send(단방향)는 reply 없이 server에 기록만 남는가.

- 검증: `request`는 정확한 reply. `send`(one-way)는 reply 없이 provider send-handler evidence에 command id 기록.

#### RM-C2 targeted request by rid

우선순위: `P0`

**한마디로:** rid를 콕 집어 보낸 request가 정확히 그 provider에만 가고, 없는 rid로 보내면 깔끔하게 실패하는가.

- 절차: route mesh로 특정 rid(`api-b`)에 request. 그리고 미존재 rid로 request.
- 검증: 지정 rid provider에만 도달(다른 provider evidence엔 없음). 미존재 rid는 public error로 실패.
- 세부 동작: rid 타깃 routing의 정확성.

#### RM-C3 다중 provider 분산

우선순위: `P0`

**한마디로:** provider 둘을 직접 붙여 두고 많이 보내면, 양쪽 모두 충분히 처리하고(정확한 비율은 보장 안 함) 합계가 보낸 수와 맞는가.

- 절차: consumer가 두 provider endpoint를 직접 `EnableClient`로 등록(수동 multi-endpoint)한 뒤, warm-up 후 충분한 수의 request(예: 90개)를 보낸다.
- 검증: 두 provider가 모두 처리 대상이 되고, 각 provider evidence 합이 전체 request 수와 일치한다. 분산은 transport(dealer) fair-queuing으로 대체로 고르되, 정확한 비율(45/45)은 보장값이 아니므로 "양쪽 모두 충분히 처리 + 합계 일치"로 검증한다.
- 세부 동작: 다중 provider 부하 분산(수동 multi-endpoint).

#### RM-C4 timeout과 late reply 비오염

우선순위: `P0`

**한마디로:** 느린 요청이 timeout으로 끝난 뒤, 뒤늦게 도착한 reply가 다음 정상 요청을 오염시키지 않는가.

- 절차: `value=="slow"` request를 짧은 timeout으로 보내 client timeout을 유도 → 곧바로 정상 request → 잠시 뒤 또 정상 request.
- 검증: 첫 request는 timeout 예외. 이후 request는 정상 reply. late reply가 뒤 요청을 오염시키지 않음. 느린 handler도 결국 server에선 완료로 기록.
- 세부 동작: timeout 경로 + 연결 비오염.

#### RM-C5 미등록 packet 처리

우선순위: `P0`

**한마디로:** handler가 없는 packet을 보냈을 때, request는 error로 명확히 실패하고 send는 조용히 drop되며, 그 이유가 observer에 정확히 남는가.

- 절차: handler 없는 packet 이름으로 request, 그리고 send.
- 검증: request는 **error reply로 실패**하고(client는 예외로 받음), observer evidence의 reason/action은 `HandlerMissing`/`ReplyError`다. send는 reply 없이 drop되고 observer reason/action은 `HandlerMissing`/`Drop`이다. 다른 정상 request는 영향 없음.
- 세부 동작: negative path(client-visible error + observer) 구분.

#### RM-C7 weighted 분산 (server쪽 weight 차등)

우선순위: `P1`

**한마디로:** server 두 대에 weight를 다르게(예: 75 vs 25) 주면, client의 분산도 그 비율을 따라 한쪽으로 더 쏠리는가.

- 절차: client-server channel의 두 provider를 서로 다른 build-time weight로 띄운다 — `api-a`는 `ConfigureServerSocket().Weight = 75`, `api-b`는 `ConfigureServerSocket().Weight = 25`(둘 다 `1..99`라 후보에서 빠지지 않음). consumer는 server가 광고한 weight를 실제로 관측할 수 있는 연결 경로(location store 자동 연결 — weight는 peer location row의 `Weight` 필드에 실린다 — 또는 transport가 peer weight를 전달하는 수동 multi-endpoint)를 사용하고, warm-up 후 충분한 수의 request(예: 200개)를 보낸다.
- 검증: 두 provider 모두 처리 대상이 되고(어느 쪽도 0이 아님), 각 provider evidence 합이 전체 request 수와 일치한다. 분산은 weight 비율을 따라 `api-a`가 `api-b`보다 **뚜렷이 많이** 처리한다(정확한 75/25는 보장값이 아니므로 "고weight가 저weight보다 분명히 많음 + 양쪽 모두 처리 + 합계 일치"로 검증한다).
- 세부 동작: peer location row에 실린 server쪽 weight에 따른 client측 부하 분산.

> 분산 주체 주의. client-server channel에서 server는 ROUTER, client는 DEALER다. server(ROUTER)는 자기 weight를 연결된 client(DEALER) peer에게 advertise만 하고, 비율 분산은 **client(DEALER)의 load balancer**가 수행한다. ROUTER 자신은 weight를 비율 분산이 아니라 `0`=부하 제외 / non-`0`=허용의 이진 transport 게이트로만 쓴다. 따라서 weighted 비율(`1..99`)은 **이미 연결된 peer의 LB 분배**에만 작용하고, weight `0`은 RM-C3 분산에서 그 노드를 후보에서 빼는 별개 동작이다(weight 제외·복원 검증은 Config 5 RL-B4·B5에서 다룬다).

> payload decode 실패는 public typed client로 유도할 수 없다(typed client는 항상 정상 envelope로 직렬화). 실제 decode-failure는 raw frame 주입이 필요해 public-API-only인 이 config 범위 밖이며, raw-frame contract 테스트(E2ETests DispatchErrors)가 다룬다. decode 실패의 surface/reason 분류(channel=`PayloadDecodeFailed`, route mesh=`HandlerException`)도 거기서 검증한다.

#### RM-C8 메시지 크기 다양성

우선순위: `P1`

**한마디로:** 작은 payload부터 큰 payload, 상한 근처까지 모두 정확히 왕복하고, 상한을 넘기면 정해진 error로 거부되는가.

- 절차: 같은 request를 payload 크기만 바꿔(소형, 대형, `MaxMessageSize` 근접) 왕복시키고, 상한을 넘는 payload도 한 번 보낸다.
- 검증: 소형·대형·근접-max payload는 내용이 손상 없이 정확히 왕복한다(대형도 분할/재조립이 투명). `MaxMessageSize`를 넘는 payload는 정해진 public error로 거부되고, 그 뒤 정상 크기 request는 영향 없이 동작한다.
- 세부 동작: payload 크기 경계(왕복 + 상한 거부).

> 주의: 크기 다양성 **왕복**은 public typed client로 바로 유도된다. 상한 초과 거부는 각 언어의 public channel builder가 server socket의 max message size를 live socket에 적용한 뒤 검증한다. public typed client는 항상 정상 envelope를 만들기 때문에, payload decode 실패처럼 raw frame이 필요한 경로는 이 config 범위가 아니라 binding/raw-frame contract 테스트에서 다룬다.

#### RM-C9 backpressure / HWM 포화

우선순위: `P2`

**한마디로:** provider가 느려 send 처리 backlog가 쌓여도 one-way send 호출자는 전송 완료를 기다리지 않고, framework가 내부 backpressure를 처리한 뒤 연결과 후속 request가 정상으로 남는가.

- 절차: provider handler를 느리게 두고, client가 처리 속도보다 빠르게 다량 request/send를 보내 송신 큐를 HWM까지 채운다.
- 검증: send pressure 중 one-way send submit은 public 완료 객체나 bounded failure oracle을 노출하지 않는다. 호출부는 제출만 확인하고, 연결이 깨지거나 다른 정상 트래픽이 오염되지 않는다. 적체가 풀리면 follow-up request와 provider evidence가 정상으로 회복된다.
- 세부 동작: 송신 HWM 포화 시 backpressure 계약.

> 주의: send/publish는 one-way submit이다. backpressure 대기, send-ready 재시도, timeout 정책은 framework 내부 책임이며 public send 호출자가 await할 완료값으로 드러내지 않는다. HWM 포화 자체의 직접 오류 결과 검증은 binding 또는 runtime 내부 테스트가 더 적합하다.

## 5. 완료 기준

- Track A·B·C의 `P0` 시나리오가 모두 통과한다.
- 각 시나리오는 public contract만 직접 호출하고 `ensure`로 단언한다(framework 내부 우회 금지). raw peer row 상태 확인은 `IZLinkLocationRuntimeQuery.ListPeerLocationsAsync(filter)`로, member peer 사용자 기능 검증은 `IZLinkPeerLocationResolver.ListLivePeersAsync(filter)`로 나눈다(둘 다 cache 없이 store에 도달한다).
- Redis를 쓰는 실행은 전용 key prefix로 격리하고, 실행 후 key cleanup 또는 disposable Redis instance를 사용한다.
- 실패 시 store 연결 상태와 provider/consumer 로그·evidence로 원인 레이어를 분리한다.
