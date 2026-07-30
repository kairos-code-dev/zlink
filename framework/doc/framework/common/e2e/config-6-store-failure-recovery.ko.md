<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Resilience](config-5-resilience-lifecycle.ko.md) | [다음: Monitoring](config-7-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# Config 6 — Location·Relocation store 장애·복구 배포

분리 등록한 location store와 relocation store가 일시 중단되거나 복구되는 조건을 검증한다. Config 1이 정상 상태의 자동
연결과 messaging을 다룬다면, 여기서는 store 장애 중 기존 연결 유지, owner lease가 만료된
descriptor 제외와 복구 순서를 확인한다.

먼저 짚어 둘 것 두 가지.

- framework는 store 장애를 즉시 연결 해제로 번역하지 않는다. 기본 정책은 **fail-static**이다.
  store 조회나 owner lease 갱신이 실패하면 마지막으로 성공한 desired target set을 유지하고, 새
  connect/disconnect diff를 계산하지 않는다. 이미 수립된 연결의 messaging은 store와 독립적으로
  계속 동작한다.
- descriptor의 유효성은 descriptor 자체가 아니라 **owner lease**로 판단한다. 노드가 비정상 종료하면
  descriptor가 남아 있어도 owner lease 만료 후 성공 결과에서 제외된다. store 제품
  자체의 HA/복제는 store 구현체(예: Redis) 책임이며 framework가 검증하지 않는다.

fail-static 표, owner lease 모델, watch/polling, 복구 순서 같은 계약 상세는
[location runtime spec](../spec/21-location-runtime.ko.md)과
[Redis location store spec](../spec/22-location-store-redis.ko.md),
[Redis relocation store spec](../spec/23-relocation-store-redis.ko.md)을 기준으로 하고 이 문서에서
반복하지 않는다.

Application 판정은 public RouteMesh status와 실제 messaging 결과를 사용한다. Location Store와
Relocation Store는 Framework가 사용하는 provider SPI이며 application query API가 아니다. E2E client가
descriptor·owner lease record를 직접 읽거나 의미를 해석하지 않는다. Store 호출 순서가 필요한 provider
시나리오는 opaque key·bytes와 transaction 결과만 기록하는 instrumented provider evidence를 사용한다.

## 1. 목적과 범위

- 다룬다: store 장애 중 fail-static(기존 연결 유지, diff 계산 중단), store failure grace 초과
  시 신규 outbound connect 중단, owner lease 만료로 인한 stale descriptor 제외, store 복구 순서(owner
  lease와 local MeshNode descriptor 재등록 → owner lease renew interval 1회 유예 → disconnect diff), watch가 없는
  store 구성의 polling fallback, runtime status 관측, store가 끊기지 않고 응답만 느려질 때
  Redis client 호출이 무관한 concurrent 처리를 막지 않는지(비블로킹 실측), immutable Relocation payload의
  renew·orphan cleanup·recovery와 두 Store의 독립 장애.
- 여기서 다루지 않는 것: 정상 상태 자동 연결·scale·failover(Config 1), provider 노드 자체의
  restart/crash resilience(Config 5), monitoring event 표면(Config 7), store 제품 자체의
  HA/복제(store 구현체 책임).

## 2. 서버 구성

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension. 실행마다 전용 location key prefix를 사용한다. Harness가 이 capability만 정지·지연·복구할 수 있다. |
| relocation store | 1 | 공식 Redis relocation store extension. 기본 topology는 같은 Redis deployment를 공유하되 별도 relocation key prefix와 별도 Store instance를 등록한다. Track F에서는 이 capability만 독립적으로 정지·지연·복구할 수 있다. |
| provider (api·object server 노드) | 2 (`api-a`, `api-b`) | ChannelName handler와 Object Server 역할을 함께 등록한다. `RecoveryActor`와 `RecoverySnapshotSpot`에는 `PreserveStateWith` policy와 kind별 Relocation Adapter를, `RecoveryInstanceSpot`에는 adapter 없는 `RecreateOnRelocation` policy를 등록하고 별도 negative type에만 `DisableRelocation`을 사용한다. 모든 stable factory type은 explicit policy와 positive placement capacity를 제공한다. `AddLocationStore(...)`와 `AddRelocationStore(...)`를 각각 호출하며 `/evidence`·`/health`와 runtime query endpoint를 제공한다. |
| consumer | 1 | Object Client 역할과 두 Store를 별도 등록하고 descriptor 기반 automatic discovery로 provider와 연결한다. 지속 request와 Track F의 object operation으로 연결·authority 상태를 관측하고 public RouteMesh status를 HTTP endpoint로 노출한다. |
| probe | 시나리오별 | 각 역할 server의 public status, structured log와 실제 messaging 결과를 확인하는 client 흐름. Store provider protocol 시나리오에서는 별도의 opaque operation evidence를 확인한다. |

시간 관련 option(owner lease renew interval, owner lease TTL, polling interval, store failure grace)은
시나리오가 유한 시간 안에 기다릴 수 있도록 짧게 설정한다(예: lease renew 1초, lease TTL 3초,
polling 0.5초). 값 자체는 언어별 option 표면을 따르되, 의미는
[Location runtime §4](../spec/21-location-runtime.ko.md#4-owner-lease와-local-admission-deadline)의
option 정의와 같아야 한다. 이 값들은 `run_e2e.sh` 상단의 명시적 config 상수로 두고 시나리오 대기 시간의 근거로
사용한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) → provider → consumer 순으로 시작하고 client 시나리오를
실행한다. 장애 시나리오는 harness가 Redis process를 정지(stop)했다가 재기동(restart)한다.
provider crash 시나리오는 provider를 SIGKILL한다. 이 프로세스 제어 연산이 없는 harness에서는
해당 시나리오를 "미구현(하네스 대기)"로 둔다. 실행이 끝나면 전용 prefix의 key를 정리하거나
disposable Redis instance를 버린다.

store 정지·복구를 기다리는 대기값은 [README](README.ko.md) §2.1의 readiness 기준이 아니라 §2의
lease-renew/lease/grace 상수에서 계산한 별도 이름의 시나리오 대기값으로 둔다(장애 자체가 검증
대상이므로 readiness 기본값으로 통과시키지 않는다).

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — 기준 동작과 polling fallback

#### SF-A1 store 정상 상태 baseline

우선순위: `P0`

**검증 질문:** store가 정상일 때 자동 연결, descriptor 등록과 runtime 상태가 모두 정상인가.

- 절차: Redis store + provider 2 + consumer로 자동 연결을 만들고 request를 보낸다. probe가 각 노드의 runtime query를 조회한다.
- 검증: Request는 두 provider 중 하나에서 처리된다. Public RouteMesh status는 `Ready`이고 두 provider
  Node RID의 peer state가 `Ready`이며 Channel의 `ReadyTargetCount=2`다(Config 1 RM-A1과 같은 baseline).
  Descriptor revision, endpoint, owner lease와 Store 성공 시각은 public status에 포함하지 않는다.
- 세부 동작: store 기반 자동 연결 + runtime status 기준값.

#### SF-A2 opaque Store polling

우선순위: `P1`

**검증 질문:** provider 전용 변경 알림에 의존하지 않고, generic Store scan polling만으로 peer 변경을
polling interval 안에 반영하는가.

- 절차: opaque `LocationStore`를 등록한 별도 consumer에서 provider 하나를 추가로 시작했다가 정상 종료한다.
  Relocation Store는 별도로 등록하며 polling discovery 검증에는 장애를 주입하지 않는다
  ([Location runtime §3](../spec/21-location-runtime.ko.md)).
- 검증: Generic snapshot scan polling만으로 추가·제거가 peer intent와 public RouteMesh status에 반영된다.
  추가 후 polling interval 몇 tick 안에 새 provider가 ready member가 되고, 제거 후 그 provider를
  선택하지 않는다.
- 세부 동작: polling이 correctness 경로다. Provider-specific change stamp나 watch는 public SPI가 아니다.

### Track B — store 장애 중 (fail-static)

#### SF-B1 store 장애 중 기존 연결 유지

우선순위: `P0`

**검증 질문:** store 연결이 중단되어도 admitted peer 연결과 messaging이 유지되는가.

- 절차: SF-A1 상태에서 consumer가 지속 request를 보내는 동안 harness가 Redis를 정지한다. lease TTL보다 짧은 시간 뒤 상태를 관측한다.
- 검증: Store 장애 중에도 기존 연결로 request가 계속 성공한다. Consumer는 disconnect diff 계산을
  중단하고 provider 연결을 유지한다. RouteMesh status는 필요하면 `Degraded`와
  `LocationUnavailable` reason을 제공하고 structured log의 `zlink.runtime.location.store_changed`가
  오류 원인을 기록한다. Public status에 최근 Store 실패 시각이나 provider record를 추가하지 않는다.
- 세부 동작: fail-static — 마지막 성공 desired set 유지 + 장애 관측.

#### SF-B2 store failure grace 초과

우선순위: `P1`

**검증 질문:** 장애가 grace를 초과하면 새 outbound connect는 중단되고 이미 ready인 transport 연결은
유지되는가.

- 절차: store 장애를 store failure grace(기본 후보 `owner lease TTL * 2`)보다 길게 유지한다. 그 사이 지속 request를 계속 보낸다.
- 검증: grace 초과 후에도 이미 ready인 연결의 request는 계속 성공한다. 새 outbound connect는 중단된다(장애 중 재시작한 provider가 있어도 store 복구 전에는 연결 대상에 추가되지 않음 — framework log와 status로 관측). store 복구 후에는 Track C의 복구 순서를 따른다.
- 세부 동작: grace 초과 시 신규 connect 중단 + 기존 연결 보존.

#### SF-B3 store failure grace와 stateful owner fence 분리

우선순위: `P0`

**검증 질문:** Store failure grace가 discovery 연결 보존에만 적용되고, 만료된 owner lease의 stateful
admission을 연장하지 않는가.

- 절차: 기존 Channel transport와 owner가 있는 Instance Spot을 준비한 뒤 Redis를 중단한다. Transport의
  service liveness는 정상으로 유지하고 store failure grace를 owner lease TTL보다 길게 설정한다. Owner
  runtime의 lease 갱신을 멈춘 채 message, timer, factory completion과 authority CAS를 각각 시도한다.
- 검증: Consumer는 grace 동안 마지막 stable desired set과 기존 transport를 유지하지만 신규 outbound
  connect를 시작하지 않는다. Stateful runtime은 마지막으로 확인한 owner lease의 monotonic deadline에서
  admission을 닫고 이후 message·timer·factory completion과 CAS를 모두 거부한다. Grace가 owner 또는
  coordinator lease를 연장하지 않는다. Store 복구 뒤에는 stable descriptor page set과 exact owner token을
  다시 확인한 후에만 connect diff와 stateful admission을 재개한다.
- 세부 동작: Discovery fail-static과 stateful owner fencing의 독립성.

### Track C — owner lease 만료와 stale descriptor

#### SF-C1 provider crash → owner lease 만료 → stale descriptor 제외

우선순위: `P0`

**검증 질문:** provider가 descriptor를 제거하지 못한 채 비정상 종료되어도 owner lease 만료 후 해당
descriptor가 성공 결과에서 제외되고 consumer가 연결을 정리하는가.

- 절차: store는 정상인 상태에서 `api-b`를 `SIGKILL`한다. owner lease TTL 경과를 기다린 뒤
  descriptor snapshot과 routing을 관측한다.
- 검증: Lease 만료 전에는 stale discovery 정보가 남을 수 있지만, TTL 경과 후 public RouteMesh status의
  ready peer와 Channel ready target에서 `api-b` Node RID가 제외된다. Consumer의 peer intent에서
  `api-b`가 제외되어 연결이 해제되고 follow-up request는 `api-a`에서만 처리된다.
  이전 endpoint에 반복 timeout이 발생하지 않는다.
- 세부 동작: descriptor remove 없는 crash 전파 — owner lease 만료로 stale descriptor 제외.

#### SF-C2 Shutdown 대조 (owner 정리)

우선순위: `P1`

**검증 질문:** `Shutdown`으로 정상 종료한 provider는 먼저 배정 대상에서 제외되고, bounded cleanup이
완료되면 owner descriptor와 lease를 정리해 crash 경로처럼 종료 뒤 lease 만료를 기다리지 않는가.

- 절차: `api-b` host에서 public `Shutdown`을 요청한다. Host가 `Draining`을 게시한 동안 새 요청이
  `api-b`에 배정되지 않는지 확인하고, 30초 기본 deadline 안에 process가 강제 종료 없이 종료되는지
  기다린다. terminal `Stopped/None` 직후 owner descriptor가 사라지는지 확인한다.
- 검증: `Draining` 중에는 이미 수락한 작업과 resource를 정리하지만 신규 배정에서는 제외된다.
  `Shutdown`이 `Stopped/None`으로 완료되면 consumer의 public status에서 `api-b`가 ready peer·target에서
  별도 lease 만료 대기 없이 제외되고 consumer가 그쪽으로 더 가지 않는다. SF-C1과 달리
  강제 종료나 lease 만료만으로 통과시키지 않는다.
- 세부 동작: `Shutdown` admission seal → `Draining` 게시 → accepted work와 resource의 bounded cleanup →
  owner 단위 descriptor bulk remove와 lease 제거 → `Stopped/None`.

#### SF-C3 stale owner lease token과 generation fencing

우선순위: `P0`

**검증 질문:** Process pause 뒤 이전 lease token의 renew·release와 같은 owner ID의 새 claim이 서로 다른
lease generation으로 구분되는가.

- 절차: `api-a` host가 process lifecycle owner lease를 claim한 뒤 process를 pause해 lease를 만료시킨다. 새
  host lifecycle은 새 owner ID로 lease와 여러 local descriptor를 게시한다. 이전 process를 재개해 늦은
  renew·release를 보낸다. 별도 fixture는 만료된 owner ID로 새 claim을 수행해 provider-domain global
  lease generation이 증가하는지 확인한다.
- 검증: 이전 token의 renew·release는 `stale`로 끝난다. 같은 owner ID의 새 claim을 허용하더라도 더 높은
  lease generation의 새 token을 발급하므로 이전 token과 descriptor를 다시 유효하게 만들지 않는다.
  Successor lease, descriptor와 ready routing은 바뀌지 않으며 이전 owner의 handler가 다시 실행되지 않는다.
  Active lease row는 expiry·release에서 제거되고 provider는 owner ID별 tombstone을 남기지 않는다.
- 세부 동작: host lifecycle owner token과 global lease generation fencing.

#### SF-C4 host lease와 여러 routing slot

우선순위: `P0`

**검증 질문:** 한 host의 여러 MeshNode·ClientServer server·fanout publisher가 owner lease를 중복 claim하지
않고 같은 process lifecycle token으로 각 routing slot을 얻는가.

- 절차: 한 host에 서로 다른 allocation group의 MeshNode 두 개, ClientServer server와 fanout publisher를
  구성한다. Host가 owner lease를 한 번 claim한 뒤 각 group의 slot Acquire에 같은 token을 전달한다. Invalid
  또는 stale token의 slot Acquire와 startup 중간 실패 rollback도 실행한다.
- 검증: Owner lease claim count는 host lifecycle당 1이고 각 slot Acquire는 active token을 원자적으로
  검증하지만 새 token이나 TTL을 만들지 않는다. 모든 descriptor가 같은 owner ID·lease generation을
  참조하고 자체 RID·lifecycle generation은 별도로 유지한다. Stale token Acquire는 slot을 소비하지 않는다.
  Startup 실패와 정상 종료는 획득한 slot을 먼저 release하고 host lease를 마지막에 release한다.
- 세부 동작: host-wide lease authority와 component별 routing slot 분리.

#### SF-C5 bounded descriptor reconcile

우선순위: `P0`

**검증 질문:** MeshName 또는 ChannelName scope의 descriptor가 많아도 provider가 한 번의 무제한 조회와
script 실행 없이 bounded page로 reconcile하는가.

- 절차: 한 scope에 1,001개 descriptor를 게시하고 page size 1, 100과 1,000으로 각각 조회한다. Page 사이에
  descriptor create·update·delete를 경쟁시키고 snapshot cursor의 일관성을 관찰한다. Routing allocation fixture는
  slot 경계 `1`·`65535`, 255 members와 각 상한을 넘는 입력을 실행한다.
- 검증: 각 page는 요청 item 1..1,000 또는 encoded 4 MiB 가운데 먼저 도달한 상한에서 끝나고 continuation
  token은 provider가 해석하는 opaque 값이다. 모든 page는 첫 요청에서 고정한 snapshot을 읽는다.
  Cursor가 만료되면 Framework는 partial diff를 적용하지 않고 첫 page부터 다시 읽는다. Routing group은
  slot 1..65,535와 member 1..255만 허용하고 상한 초과를 startup 또는 provider argument 오류로 거부한다.
- 세부 동작: bounded descriptor page, snapshot cursor와 coherent routing group 상한.

### Track D — store 복구

#### SF-D1 짧은 장애 복구 (grace 안)

우선순위: `P0`

**검증 질문:** store가 grace 안에 복구되면 기존 연결을 유지한 채 다음 성공 조회로 reconcile되고
mesh 전체의 불필요한 재연결이 발생하지 않는가.

- 절차: SF-B1 상태에서 lease TTL보다 짧게 Store를 정지했다가 재기동한다. 복구 후 지속 request와
  public RouteMesh status를 관측한다.
- 검증: 복구 후 첫 성공 조회로 reconcile이 재개되고, 정상 provider와의 기존 연결은 유지된다.
  각 provider evidence에 불필요한 disconnect/reconnect marker가 없어야 한다.
  RouteMesh status는 다시 `Ready`로 수렴하고 request는 전 구간에서 성공한다. Store 최근 성공 시각은
  public status로 제공하지 않는다.
- 세부 동작: 짧은 장애의 무해 통과(fail-static → fresh descriptor snapshot reconcile).

#### SF-D2 긴 장애 복구 — 재등록 우선과 owner lease renew 유예

우선순위: `P0`

**검증 질문:** 장애가 lease TTL보다 길어 모든 lease가 만료된 뒤 복구되어도 각 노드가 재등록을 먼저
수행하고 owner lease renew interval 한 번을 기다린 뒤 disconnect diff를 적용해 정상 peer 연결을 유지하는가.

- 절차: store 장애를 owner lease TTL보다 길게 유지한다. 장애 중 `api-b`를 `SIGKILL`하고 store를
  재기동해 복구 흐름을 관측한다.
- 검증: 복구 직후 각 노드가 조회보다 먼저 owner lease와 local MeshNode descriptor를 다시 upsert한다.
  이 순서는 instrumented Location Store의 opaque transaction evidence와 structured log로 확인한다.
  Disconnect diff는
  owner lease renew interval 한 번의 유예 후 적용된다. `api-a`와 consumer 사이의 연결은 유지되고 request가
  전 구간에서 성공한다. 유예 후에도 재등록되지 않은 `api-b`만 peer intent에서 제외된다.
- 세부 동작: 복구 순서 — owner lease/local MeshNode descriptor 재등록 → owner lease renew interval 유예 → 빠진 target만 disconnect.

#### SF-D3 public topology status 전이 관측

우선순위: `P1`

**검증 질문:** 장애→복구 한 사이클 동안 public topology status가
`Ready → Degraded(LocationUnavailable) → Ready`로 수렴하는가.

- 절차: SF-D1 또는 SF-D2 실행 중 probe가 각 노드의 RouteMesh status stream과 structured log를 단계별로
  조회한다.
- 검증: 정상 구간의 `Ready`, 장애 구간의 `Degraded/LocationUnavailable`, 복구 후 `Ready`가 같은
  source의 증가하는 sequence로 관측된다. Structured log에는
  `zlink.runtime.location.store_changed`가 장애와 복구에 각각 기록된다. Last success·failure 시각,
  descriptor owner ID와 owner lease는 public status에 포함하지 않는다.
- 세부 동작: Public topology status와 Store 장애 사이클 반영.

### Track E — store 응답 지연(장애 아님) 중 비블로킹

#### SF-E1 store 응답 지연 중 무관 concurrent 처리 비블로킹

우선순위: `P1`

**검증 질문:** store가 끊기지 않고 응답만 느려질 때, 그 지연이 같은 프로세스가 처리하는 무관한
동시 요청(location store와 상관없는 application messaging)까지 막지 않는가.

- 절차: harness가 Redis를 정지시키지 않고 응답 지연만 주입한다(예: proxy를 통한 지연 주입,
  또는 느린 Lua 스크립트로 특정 key에 대한 응답만 지연). 지연이 걸리는 동안 같은 provider
  프로세스로 location store와 무관한 concurrent request를 계속 보낸다. 지연 주입 없는 harness에서는
  이 시나리오를 "미구현(하네스 대기)"로 둔다.
- 검증: 지연 중에도 무관 request의 p99 latency가 SF-A1 baseline 대비 유의미하게 증가하지 않는다
  (예: baseline의 N배 이내로 사전 정의). Public `GetStatus(meshName)` 조회 자체도 무관 요청 경로를 막지
  않는다. 이 결과로 store client가 스레드나 이벤트 루프를 점유하지 않고 실제 비동기·논블로킹으로
  I/O를 수행함을 실측으로 증명한다. 비동기 실행 계약은
  [공통 비동기 실행 정책](../spec/05-async-execution-policy.ko.md)을 따른다.
- 세부 동작: Redis 응답 지연이 코루틴 dispatcher(Kotlin)나 core I/O 스레드(C++, `PERF_IO_THREADS`)를
  점유하지 않음을 검증. 이 트랙은 store 자체의 정상/비정상보다 client 구현의 비블로킹 여부를 보는
  점에서 Track A~D와 다르다.

### Track F — Authority interop과 relocation 보존

Track F는 Location Store의 authority CAS와 Relocation Store의 immutable payload Put·Get·renew·delete를 별도
failure point로 주입한다. Payload를 먼저 준비하고 Location Store reference CAS로 publish하는 순서를 검증하며,
두 Store 사이의 distributed transaction이나 provider 전용 묶음 등록 API를 사용하지 않는다.

#### SF-F1 언어 간 authority key·payload interop

우선순위: `P0`

- 절차: 한 언어 runtime이 Actor와 Instance Spot의 steady, cold activation, maintenance authority를
  기록한다. 다른 언어 runtime이 같은 Store와 logical address를 사용해 resolve와 recovery를
  수행한다. 네 runtime의 방향있는 조합을 모두 실행한다.
- 검증: 모든 조합이 `authority-key-v1`의 같은 Store key를 사용하고 opaque store version,
  object kind, owner과 phase를 같게 해석한다. Unknown field·code를 추측해 수용하지 않는다.

#### SF-F2 current relocation renew과 orphan 정리

우선순위: `P0`

- 절차: Virtual clock으로 capture를 길게 지연하면서 여러 relocation chunk를 기록한다. 일부 staged chunk의
  remaining lease를 12시간 이하로 만들고 renew 한 건을 실패시킨다. 성공 반복에서는 complete manifest tree를
  renew한 뒤 authority CAS로 current reference를 확정한다. 별도 relocation은 authority CAS에 실패해 orphan으로
  남긴다. Harness가 provider 기준 시각과 renew cycle을 제어해 24시간 retention 경계를 통과시킨다.
- 검증: Current authority owner 또는 recovery coordinator가 현재 reference만 renew해 recovery를
  계속할 수 있고 orphan은 TTL 뒤 제거된다. Reference가 CAS로 교체되거나 해제되면 이전
  reference renew는 stale 결과로 종료하고 cleanup한다. `Captured`·`Prepared` CAS 직전에는 root와 모든 chunk가
  12시간보다 긴 lease를 가졌는지 provider 시각으로 확인한다. Missing component나 renew 실패가 있으면 root를
  authority에 연결하지 않고 precommit abort하며 partial relocation을 current로 공개하지 않는다.

#### SF-F3 relocation recovery horizon 초과

우선순위: `P1`

- 절차: Relocation Store와 모든 recovery coordinator를 24시간 이상 사용할 수 없는 상태로 두고
  relocation renew가 발생하지 않게 한 뒤 Store를 복구한다. Location Store에는 current authority가
  이미 publish한 relocation reference를 그대로 유지한다.
- 검증: Published reference가 가리키는 payload가 retention 이후 영구적으로 없으면 Runtime은 새 state를
  추측하거나 이전 owner로 rollback하지 않고 non-retriable `DataLost`로 끝낸다. 진행 중인 `Relocate`는
  `ForceStopped/RelocationFailed`로 terminal 완료하고 detail에 `DataLost`를 보존한다. Metric·event에는
  object kind, phase와 relocation reference hash를 기록한다. Authority에 publish되지 않은 orphan expiry는 이
  data-loss 결과로 분류하지 않는다.

#### SF-F4 authority generation 원자 전이와 exhaustion

우선순위: `P0`

- 절차: 같은 authority key에서 Missing expectation의 `new_object`, Found store version의 `preserve`·`new_owner`·delete,
  다시 Missing expectation의 `new_object`를 순서대로
  실행한다. 두 언어 runtime이 같은 expected Store version으로 `new_owner`를 동시에 시도하는 case도
  실행한다. 별도 provider fixture는 object 또는 owner generation을 `9223372036854775807`로 설정한 뒤 증가가
  필요한 mutation을 시도한다.
- 검증: `new_object`는 provider-domain global object·authority-owner generation counter를 모두 증가시키고
  `new_owner`는 global authority-owner counter만 증가시키며 `preserve`는 두 값을 유지한다. 모든 성공 mutation은
  global store revision을 증가시킨다. Delete는 row를 제거하며 Missing read는 fake Store version을 만들지 않는다.
  다시 만든 object는 global counter에서 새 값을 받아 이전 generation을 재사용하지 않는다.
  Concurrent mutation은 CAS winner 하나만 generation을 소비하고 logical key별 영구 tombstone을 남기지 않는다.
  최댓값 뒤 mutation은 stable exhaustion 결과로 끝나며 0 또는 음수로 wrap하거나 authority payload를
  기록하지 않는다.

#### SF-F5 durable authority와 owner lease 분리

우선순위: `P0`

- 절차: Instance cold activation은 durable authority의 `Creating`에서 `Ready` commit 전에 멈추고, 별도
  Actor relocation은 `Committed`에서 target activation이 끝나기 전에 멈춘다. Creation authority에는 immutable
  creation intent·reservation과 현재 generation을, relocation authority에는 published relocation reference·replay
  cursor와 현재 generation을 기록한다. 각 process를 중지해 owner lease를 만료시킨 뒤 authority row가
  유지되는지 확인하고 successor가 새 owner token과 `new_owner` CAS로 해당 state machine의 recovery를 계속한다.
- 검증: Owner lease expiry는 descriptor와 신규 admission만 무효화하고 authority row, phase, relocation
  reference, replay cursor와 object generation을 삭제하지 않는다. Successor만 더 높은 owner generation으로
  Instance는 같은 ObjectGeneration으로 factory·`Ready` barrier를 수렴시키고 Actor relocation은 published root에서
  current target activation을 이어서 완료한다. 이전 owner의 늦은 phase CAS와 cleanup은 stale이며 current
  authority를 변경하지 않는다.
  Authority row는 terminal cleanup의 explicit fenced delete 전까지 TTL 없이 유지된다. Durable owner tuple의
  owner ID와 owner lease generation은 current host lease와 모두 일치해야 하며, 같은 owner ID를 새 generation으로
  다시 claim해도 이전 authority admission이나 owner ID만 사용한 bulk cleanup을 유효하게 만들지 않는다.

#### SF-F6 snapshot-consistent recovery scan

우선순위: `P0`

- 절차: Provider가 registered MeshName scope의 authority recovery scan 첫 page에서 opaque scan token과
  watermark를 만든다. Page를 읽는 동안 watermark 전 row의 update·delete와 watermark 뒤 새 key create,
  같은 key delete·recreate를 경쟁시킨다. 각 반환 key는 current row를 다시 읽고 expected store version CAS로
  recovery ownership을 시도한다.
- 검증: Watermark 시점에 존재한 key incarnation은 page 전체에서 정확히 한 번 열거된다. 중간에 삭제된
  row의 exact read는 `missing`으로 닫히고, watermark 뒤 create·recreate는 다음 scan에서만 나타난다.
  Startup runtime은 등록한 MeshName의 initial scan을 끝내기 전에 `Serving` descriptor를 게시하지 않으며,
  이후 background scan이 새 orphaned transaction을 발견한다. Concurrent mutation이 recovery row를 영구히
  누락시키거나 같은 store version을 두 coordinator가 소유하면 실패다.

#### SF-F7 chunked relocation manifest

우선순위: `P0`

- 절차: 64 MiB보다 큰 accepted journal과 captured application state를 만들어 reversible seal 뒤 logical relocation을
  여러 immutable data chunk와 root manifest로 저장한다. Chunk write, manifest write, authority CAS, renew와
  cleanup 사이에서 process를 각각 종료한다. Empty `RecreateOnRelocation` relocation도 별도 실행한다.
- 검증: 각 data chunk는 64 MiB 이하이고 manifest가 total length·checksum, 1부터 시작하는 order와 각
  reference·length·checksum을 고정한다. Authority는 manifest reference 하나만 가리키며 target은 전체 payload를
  한 번에 allocation하지 않고 bounded streaming validation·replay로 원래 journal과 state를 복원한다. Authority
  CAS 전 chunk·manifest는 orphan으로 만료되고, current manifest와 모든 chunk는 함께 renew된다. Reference CAS
  제거 뒤 cleanup은 반복해도 같은 결과다. Empty relocation도 zero-data deterministic manifest 하나를 기록한다.
  Chunk 수 4,096 또는 logical 256 GiB ceiling을 넘으면 relocation이 state relocation 계약과 호환되지 않으므로
  reversible seal을 풀고 `Blocked/StateIncompatible`로 끝내며 `Draining`을 게시하지 않는다.

#### SF-F8 relocation target reservation lease fence

우선순위: `P0`

- 절차: Target이 relocation capacity를 offer하고 reservation ACK를 보낸 직후 target process만 pause한다.
  Transport I/O는 유지한 채 target host owner lease를 만료시키고, 같은 target node lifecycle에서 늦은 ACK와
  activation completion을 전달한다. 별도 반복에서는 같은 owner ID를 더 높은 lease generation으로 다시
  claim한다.
- 검증: Candidate, reservation ACK와 durable authority가 기록한 target owner ID·owner lease generation이
  current host lease와 일치하지 않으면 `Prepared`·`Committed` CAS와 activation을 수행하지 않는다. Coordinator는
  current lease를 다시 확인한 새 target reservation으로 replacement round를 시작하며 stale target의 예약과
  completion은 current authority를 변경하지 않는다.

#### SF-F9 owner-token bulk cleanup fence

우선순위: `P0`

- 절차: 같은 owner ID로 lease generation A와 B를 순서대로 claim하고 B가 descriptor를 게시한 뒤 A의 지연된
  bulk cleanup을 실행한다.
- 검증: Bulk cleanup은 owner ID string이 아니라 exact owner token으로 동작한다. A cleanup은 B descriptor와
  authority를 삭제하지 않으며 owner index와 각 row의 owner ID·lease generation을 같은 provider operation에서
  확인한다.

#### SF-F10 compact authority와 relocation completion

우선순위: `P0`

- 절차: Accepted request와 large reply completion을 계속 추가하면서 relocation phase CAS와 authority recovery
  scan을 실행한다. Late completion 두 개가 같은 current relocation root에서 새 completion chunk와 root
  manifest를 동시에 만들도록 경쟁시킨다. Scan은 item 1,000개보다 먼저 encoded bytes 4 MiB에 도달하는
  authority payload를 포함한다.
- 검증: Authority payload는 1 MiB 이하의 phase·fence·target·reservation·relocation reference와 compact
  cursor/count만 보유하고 full journal·reply payload·terminal completion map을 중복하지 않는다. Completion
  bytes는 relocation logical stream의 immutable chunk만 소유한다. Completion 추가와 ACK·exact request-source
  lease-expiry 전이는 새 immutable root를 만들고 expected authority store version CAS 한 번으로 root·checksum,
  terminal completion count와 pending relay count를 함께 교체한다. 두 count는 참조 relocation에서 계산하며
  accepted request count와 terminal completion count가 다르거나 pending relay count가 pending delivery entry
  수와 다르면 recovery error로 처리하고 `Completed`를 금지한다. Delivery state는 pending에서
  `terminalReceived`, `alreadyTerminal`, `sourceLeaseExpired` 중 하나로만 단조 전이한다. Root reference CAS
  winner 하나만 current가 되고 loser root/chunk는 orphan cleanup 대상이며 loser는 current root를 다시 읽는다.
  여기서 pending relay는 accepted request의 terminal reply delivery를 뜻한다. 이전 owner route로 들어온
  Actor·Spot message를 current owner에 전달하는 Message Follow의 route 수가 아니다.
  Recovery scan은
  1,000 item 또는 encoded 4 MiB 가운데 먼저 도달한 상한에서 page를 끝내고 continuation으로 모든 row를
  정확히 한 번 반환한다.

#### SF-F11 provider cancellation과 buffer lifetime

우선순위: `P0`

- 절차: Authority CAS와 content-addressed relocation Put의 provider invocation 직전·직후에 waiter cancellation,
  timeout과 응답 유실을 각각 주입한다. Input byte buffer는 operation 완료 전까지 동일 내용을 유지하고,
  성공 결과로 받은 byte buffer를 provider 내부 pool에서 재사용하려는 변이도 실행한다.
- 검증: Invocation 전에 이미 취소된 operation은 I/O와 commit이 0건이다. Invocation 뒤 취소·timeout·응답 유실은
  no-commit으로 추정하지 않고 exact authority read와 expected fence로 결과를 reconcile한 뒤에만 retry한다.
  Relocation Put은 같은 content reference를 verify·retry하고 authority에 연결되지 않은 committed Put은 orphan
  retention과 cleanup을 따른다. Provider는 operation 완료 뒤 input을 보관할 때 완료 전에 copy하며 success result
  bytes를 mutate·reuse하지 않는다. Mutable-buffer 언어 adapter의 defensive snapshot 뒤에도 checksum과 replay가
  같아야 한다.

### Track G — typed capacity transaction

#### SF-G1 Actor·Spot·stable type limit의 atomic reservation

우선순위: `P0`

- 절차: Actor total, Spot total과 한 Spot stable type의 limit을 서로 다른 작은 양수로 설정한다. 여러
  process에서 마지막 available slot보다 많은 Actor·User Spot·Instance Spot creation을 동시에 시작하고
  일부 factory는 failure와 timeout을 반환하게 한다.
- 검증: Location Store는 Active+Reserved+Requested를 같은 transaction에서 검사한다. Actor, Spot total과
  stable type bucket 가운데 하나라도 부족하면 vector 전체가 바뀌지 않고 factory가 실행되지 않는다.
  성공 수는 각 limit을 넘지 않으며 failure·timeout·abort는 exact reservation ID와 lifecycle fence가 확보한
  slot만 해제한다. 다른 node가 eligible하면 그 node를 시도하고 모두 소진되면
  `CapacityExceeded`로 끝난다.

#### SF-G2 unlimited·Entry Spot·activation concurrency 분리

우선순위: `P0`

- 절차: population limit을 `0`으로 둔 node에서 기존 10,000 상한을 넘는 synthetic reservation contract를
  실행한다. 별도 node는 작은 activation concurrency와 unlimited population을 사용한다. Entry Spot과 그
  member Actor도 함께 관찰한다.
- 검증: `0`은 hidden population limit 없이 unlimited다. Entry Spot 자체는 Spot total·stable type count에서
  제외되지만 member Actor는 Actor total에 포함된다. Activation concurrency가 차면 새 factory 시작만
  backpressure되고 이미 Active인 population은 capacity exhaustion으로 바뀌지 않는다. 음수 limit은 socket
  bind 전에 startup configuration error다.

#### SF-G3 User Spot aggregate relocation capacity vector

우선순위: `P0`

- 절차: member Actor가 N개인 stable type `room` User Spot aggregate를 target으로 relocation한다. Target은
  (a) Spot total slot은 남지만 Actor total slot이 N보다 하나 부족한 경우, (b) Spot total과 Actor total은
  충분하지만 `room` stable type slot만 부족한 경우, (c) 세 bucket이 모두 충분한 경우를 각각 만든다.
  Prepare 뒤 abort와 process restart도 주입한다.
- 검증: (a)와 (b)에는 target Spot total 1개, `room` stable type 1개와 Actor total N개 가운데 어떤
  reservation도 남지 않고 factory·Restore·authority mutation이 0건이다. (c)는 단일 typed capacity
  bundle 전체와 aggregate participant authority를 한 transaction으로 Reserved→Active 전환한다.
  Abort·recovery는 aggregate에 연결된 exact bundle만 해제하고 successor lifecycle count를 변경하지 않는다.

## 5. 완료 기준

- `P0` 시나리오(SF-A1·B1·B3·C1·C3·C4·C5·D1·D2·F1·F2·F4·F5·F6·F7·F8·F9·F10·F11·G1·G2·G3)가 모두 통과한다.
- Application 판정은 public RouteMesh status, 실제 messaging과 역할 server evidence를 사용한다.
  Provider protocol 검증은 Store SPI의 opaque operation evidence만 사용하며 E2E client가 descriptor,
  owner lease나 authority bytes를 해석하지 않는다.
- stale descriptor 판정은 "성공 결과에서 제외"로 검증한다. 물리 삭제 시점은 background cleanup의
  책임이므로 단언하지 않는다.
- 장애·복구 시나리오는 복구 후 messaging 정상화 + stale descriptor 부재 + 정상 peer 연결 보존을
  함께 검증한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.

비고: Config 1은 store가 정상일 때의 자동 연결·scale·failover를 보고, 이 config는 store 장애와
복구 자체를 본다. store를 함께 쓰는 두 config의 baseline 단언(SF-A1 ↔ RM-A1)은 의도적으로
겹친다.
