<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [다음: Spot 서비스](config-2-spot-service.ko.md)
<!-- framework-adapter-nav:end -->

# Config 1 — Registry 기반 messaging 배포

이 문서는 첫 e2e config의 시나리오를 정의한다. config는 실제 배포처럼 생긴 서버 구성을
한 번 띄우고, 그 위에서 messaging과 연결·rid resolve를 실 사용자처럼 검증하는 단위다.

## 1. 목적과 범위

실 배포 형상(실 registry, 실 resolve, 다중 provider, 실제 프로세스 경계) 위에서
messaging과 연결·rid resolve를 검증한다. 기존 unit/contract 테스트와 단언이 겹쳐도 된다 —
차별점은 단언의 새로움이 아니라 현실적인 배포 컨텍스트와 sample 수준 public API 사용이다.
각 시나리오는 helper 없이 public contract를 직접 호출하고 `ensure`로 단언해, 실 사용 흐름이
한눈에 들어오게 작성한다.

범위 밖(다른 config로): codec, stream, spot/actor, resilience 세부. 이 config는 messaging
+ 연결/resolve에 집중한다.

## 2. 서버 구성 (한 번 구동, 공유)

스크립트가 아래를 한 번 띄우고 모든 client 시나리오가 공유한다. scale·failover 시나리오만
provider 프로세스를 추가로 띄우거나 종료한다.

| 역할 | 수 | 구성 |
|------|----|------|
| registry | 1 | discovery server. pub endpoint + router endpoint. |
| provider (api 노드) | 2 (`api-a`, `api-b`) | 세 channel 종류를 함께 노출한다: ① registry-discovered **client-server channel**(`AddClientServerChannel`) — request handler(`ProfileRequest`)·send handler(`ProfileCommand`); ② peer-wired **route mesh channel**(`AddRouteMeshChannel`) — route request handler(`ScenarioRoutePing`), routing id `api-a`/`api-b`; ③ peer-wired **dealer mesh channel**(`AddDealerMeshChannel`) — `ProfileRequest`. dispatch-error observer로 evidence 기록. 테스트용 `/evidence`·`/health` HTTP endpoint. |
| consumer | 시나리오별 | client-server는 Discovery client(endpoint 모름, registry resolve). route mesh는 자신이 route node가 되어, `EnableServer(clientEndpoint)`로 자기 endpoint를 bind하고 `ConfigureRouting().RoutingId`로 자기 routing id를 설정하며 peer를 `EnableClient(peerEndpoint)`로 붙는다(세 호출 순서는 무관 — 최종 registration으로 적용. route channel은 bind endpoint 없으면 startup 거부). dealer mesh는 peer endpoint로 `EnableClient`만 한다. |

client-server channel provider는 자신의 logical routing id(`api-a`, `api-b`)와 channel
endpoint를 registry에 광고한다. consumer는 channel 이름만 알고 endpoint는 registry가 알려준다.
route/dealer mesh는 registry discovery가 아니라 peer endpoint로 직접 묶는다.

handler 동작(공유): `ProfileRequest(value)` → `profile:{value}` reply + 처리한 provider의
routing id 포함. `ScenarioRoutePing(value)` → `route:{value}` + target/source routing id 포함.
`value=="slow"`면 1s 지연(timeout 유도). 미등록 packet 이름은 handler 없음 → request면 error
reply, send면 drop. 모두 observer evidence에 marker로 남긴다.

## 3. 실행 모델

`run_e2e.sh`가 registry → provider 순으로 띄우고(포트 readiness 확인) 그다음 client
시나리오를 하나씩 실행한다. scale·failover 시나리오는 같은 스크립트가 추가 provider를
띄우거나 종료한다. client가 `e2e result=passed`를 출력하면 통과로 본다.

## 4. 시나리오

### Track A — 연결과 rid resolve

#### RM-A1 registry 자동 연결 + rid 자동 resolve

우선순위: `P0`

- 절차: consumer가 endpoint 없이 channel만 등록하고 Discovery로 연결한 뒤 `ProfileRequest`를 보낸다.
- 검증: request가 `api-a`/`api-b` 중 하나에서 처리됨(reply의 provider rid로 확인). consumer는 endpoint를 코드에 적지 않았다. 두 provider 모두 `Ready`로 topology에 보인다.
- 세부 동작: registry 광고 → topology 수렴 → endpoint 없는 messaging.

#### RM-A2 수동 endpoint 연결 (대조군)

우선순위: `P0`

- 절차: consumer가 registry 없이 provider endpoint를 직접 `EnableClient`로 등록하고 request를 보낸다.
- 검증: 지정한 provider에서 처리. 자동 resolve 경로와 같은 reply 의미.
- 세부 동작: 수동 연결이 자동 연결과 동일 의미임을 고정.

> custom resolver는 client-server channel public API에 없다(SPOT용 `AddSpotRemoteAddressResolver<T>`만 존재). 해당 검증은 Config 2(spot route resolver)에서 다룬다.

#### RM-A4 같은 rid, 다른 endpoint failover

우선순위: `P0`

- 절차: provider v1을 rid `api-a`/endpoint p1로 시작 → request로 v1 evidence 확인 → v1 종료 → provider v2를 같은 rid `api-a`/endpoint p2로 시작 → topology가 endpoint를 p2로 갱신할 때까지 대기 → consumer 재시작 없이 다시 request.
- 검증: topology에 rid `api-a`는 하나만(중복 provider 없음). 교체 뒤 신규 request는 p2 evidence에 기록. consumer가 p1 stale endpoint로 반복 timeout 하지 않음. 이후 연속 20개 request 모두 성공.
- 세부 동작: rid 기준 최신 endpoint 덮어쓰기 + stale 회피.

> 런타임 연결 수립/재시도/해제 제어 handle은 channel messaging public API에 없다(endpoint는 startup 설정). timeout 규칙 검증은 RM-C4가 다룬다.

#### RM-A6 cross-channel discovery

우선순위: `P1`

- 절차: 같은 registry에 서로 다른 channel(예: `api`, `workflow`)의 provider를 광고하고, consumer가 각 channel을 resolve해 request를 보낸다.
- 검증: 각 channel의 provider 집합이 섞이지 않는다. 같은 endpoint host라도 channel name이 다르면 독립 topology로 관리된다. 한 channel scale-in이 다른 channel routing에 영향을 주지 않는다.
- 세부 동작: channel별 독립 discovery.

### Track B — scale

#### RM-B1 scale-out (트래픽 중 provider 추가)

우선순위: `P0`

- 절차: provider A만으로 request를 보낸다 → provider B를 추가로 시작 → topology가 2개 반영될 때까지 대기 → request를 여러 개 보낸다.
- 검증: B 추가 전엔 A만 처리. 반영 완료 뒤 검증 구간에선 A·B 모두 routing 대상. consumer 재시작 없음.
- 세부 동작: 무중단 provider 증설 반영.

#### RM-B2 scale-in / graceful drain

우선순위: `P0`

- 절차: A·B로 분산을 확인 → B를 정상 종료 → topology에서 B가 빠질 때까지 대기 → 다시 request.
- 검증: B 종료 뒤 request는 A로만. consumer가 죽은 endpoint로 timeout을 반복하지 않음. 지속 request 중 scale-in이 나도 완료된 요청은 정상 reply 또는 정해진 public error로 끝나고 pending이 남지 않음.
- 세부 동작: 무중단 provider 감축 + stale 정리.

### Track C — resolve된 연결 위의 messaging 세부 동작

아래는 RM-A1의 registry-resolved 연결을 재사용해 messaging verb와 negative path를 검증한다.

#### RM-C1 request / send happy path

우선순위: `P0`

- 검증: `request`는 정확한 reply. `send`(one-way)는 reply 없이 provider send-handler evidence에 command id 기록.

#### RM-C2 targeted request by rid

우선순위: `P0`

- 절차: route mesh로 특정 rid(`api-b`)에 request. 그리고 미존재 rid로 request.
- 검증: 지정 rid provider에만 도달(다른 provider evidence엔 없음). 미존재 rid는 public error로 실패.
- 세부 동작: rid 타깃 routing의 정확성.

#### RM-C3 다중 provider 분산

우선순위: `P0`

- 절차: consumer가 두 provider endpoint를 직접 `EnableClient`로 등록(수동 multi-endpoint)한 뒤, warm-up 후 충분한 수의 request(예: 90개)를 보낸다.
- 검증: 두 provider가 모두 처리 대상이 되고, 각 provider evidence 합이 전체 request 수와 일치한다. 분산은 transport(dealer) fair-queuing으로 대체로 고르되, 정확한 비율(45/45)은 보장값이 아니므로 "양쪽 모두 충분히 처리 + 합계 일치"로 검증한다.
- 세부 동작: 다중 provider 부하 분산(수동 multi-endpoint).

#### RM-C4 timeout과 late reply 비오염

우선순위: `P0`

- 절차: `value=="slow"` request를 짧은 timeout으로 보내 client timeout을 유도 → 곧바로 정상 request → 잠시 뒤 또 정상 request.
- 검증: 첫 request는 timeout 예외. 이후 request는 정상 reply. late reply가 뒤 요청을 오염시키지 않음. 느린 handler도 결국 server에선 완료로 기록.
- 세부 동작: timeout 경로 + 연결 비오염.

#### RM-C5 미등록 packet 처리

우선순위: `P0`

- 절차: handler 없는 packet 이름으로 request, 그리고 send.
- 검증: request는 **error reply로 실패**하고(client는 예외로 받음), observer evidence의 reason/action은 `HandlerMissing`/`ReplyError`다. send는 reply 없이 drop되고 observer reason/action은 `HandlerMissing`/`Drop`이다. 다른 정상 request는 영향 없음.
- 세부 동작: negative path(client-visible error + observer) 구분.

#### RM-C6 dealer mesh peer request

우선순위: `P0`

- 절차: client가 시작 전에 dealer mesh의 두 peer endpoint를 모두 `EnableClient(endpoint)`로 등록하고(런타임 연결 추가 handle은 없음 — startup 설정), 충분한 수의 request를 보낸다.
- 검증: request가 등록된 dealer mesh peer들에 분산되어 처리된다(특정 peer 지정 없이 mesh가 분배). 각 peer evidence 합이 전체 request 수와 일치한다.
- 세부 동작: dealer mesh 분산 messaging(정적 peer 등록).

> weighted routing은 weight를 설정하는 public channel builder API가 없다(registry model에 `Weight` 조회 필드만 존재). 현재 public API로 검증 불가하여 제외한다.

> payload decode 실패는 public typed client로 유도할 수 없다(typed client는 항상 정상 envelope로 직렬화). 실제 decode-failure는 raw frame 주입이 필요해 public-API-only인 이 config 범위 밖이며, raw-frame contract 테스트(E2ETests DispatchErrors)가 다룬다. decode 실패의 surface/reason 분류(channel=`PayloadDecodeFailed`, route mesh=`HandlerException`)도 거기서 검증한다.

## 5. 완료 기준

- Track A·B·C의 `P0` 시나리오가 모두 통과한다.
- 각 시나리오는 public contract만 직접 호출하고 `ensure`로 단언한다(framework 내부 우회 금지).
- 실패 시 registry/provider/consumer 로그와 evidence로 원인 레이어를 분리한다.
