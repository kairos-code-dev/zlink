<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Resilience](config-5-resilience-lifecycle.ko.md) | [다음: Monitoring](config-7-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# Config 6 — Location store 장애·복구 배포

공유 location store 자체가 흔들릴 때를 보는 config다. Config 1이 store가 정상일 때의 자동
연결과 messaging을 봤다면, 여기서는 **store가 끊기고 다시 살아나는 동안** framework가 기존
연결을 어떻게 지키고, 죽은 owner의 row를 어떻게 걸러내고, 복구를 어떤 순서로 진행하는지를
본다.

먼저 짚어 둘 것 두 가지.

- framework는 store 장애를 즉시 연결 해제로 번역하지 않는다. 기본 정책은 **fail-static**이다.
  store 조회나 heartbeat가 실패하면 마지막으로 성공한 desired target set을 유지하고, 새
  connect/disconnect diff를 계산하지 않는다. 이미 수립된 연결의 messaging은 store와 독립적으로
  계속 동작한다.
- row의 생존은 row 자체가 아니라 **owner lease**로 판단한다. 노드가 비정상 종료하면 row는
  남아 있어도 그 owner의 lease가 만료되면서 resolve/list 성공 결과에서 제외된다. store 제품
  자체의 HA/복제는 store 구현체(예: Redis) 책임이며 framework가 검증하지 않는다.

fail-static 표, owner lease 모델, watch/polling, 복구 순서 같은 계약 상세는
[location runtime spec](../../spec/server/40-location-runtime.ko.md)과
[Redis store spec](../../spec/server/41-location-store-redis.ko.md)을 기준으로 하고 이 문서에서
반복하지 않는다.

판정은 public 표면으로만 한다: `IZLinkLocationRuntimeQuery.GetStatusAsync`(store health,
watch/polling 상태, owner lease 갱신 상태, last error, last refresh),
`IZLinkLocationRuntimeQuery.ListPeerLocationsAsync(filter)`(raw peer location row, cache 없음), 실제
messaging 성공, 각 역할 server의 evidence.

## 1. 목적과 범위

- 다룬다: store 장애 중 fail-static(기존 연결 유지, diff 계산 중단), store failure grace 초과
  시 신규 outbound connect 중단, owner lease 만료로 인한 stale row 제외, store 복구 순서(owner
  lease와 local row 재등록 → heartbeat interval 1회 유예 → disconnect diff), watch가 없는
  store 구성의 polling fallback, runtime status 관측, store가 끊기지 않고 응답만 느려질 때
  Redis client 호출이 무관한 concurrent 처리를 막지 않는지(비블로킹 실측).
- 여기서 다루지 않는 것: 정상 상태 자동 연결·scale·failover(Config 1), provider 노드 자체의
  restart/crash resilience(Config 5), monitoring event 표면(Config 7), store 제품 자체의
  HA/복제(store 구현체 책임).

## 2. 서버 구성

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 Redis instance. 실행마다 전용 key prefix. harness가 정지/재기동해 store 장애를 만든다. |
| provider (api 노드) | 2 (`api-a`, `api-b`) | client-server channel server. `AddLocationStore(new ZLinkRedisLocationStore(...))`로 store를 등록하면 framework lifecycle이 peer location row와 owner lease를 자동 갱신한다. `/evidence`·`/health` + runtime query 조회용 HTTP endpoint. |
| consumer | 1 | 같은 store를 등록하고 자동 연결로 provider에 붙는 client. 지속 request로 연결 유지 여부를 관측하고, 자기 runtime query 결과(status, peer list)를 HTTP endpoint로 노출한다. |
| probe | 시나리오별 | 각 역할 server의 runtime query endpoint를 조회해 store health, peer row, lease 상태를 확인하는 client 흐름. |

시간 관련 option(heartbeat interval, owner lease TTL, polling interval, store failure grace)은
시나리오가 유한 시간 안에 기다릴 수 있도록 짧게 설정한다(예: heartbeat 1초, lease TTL 3초,
polling 0.5초). 값 자체는 언어별 option 표면을 따르되, 의미는 [40 §8.2](../../spec/server/40-location-runtime.ko.md)의
option 정의와 같아야 한다. 이 값들은 `run_e2e.sh` 상단의 명시적 config 상수로 두고 시나리오 대기 시간의 근거로
사용한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) → provider → consumer 순으로 띄우고 client 시나리오를
실행한다. 장애 시나리오는 harness가 Redis process를 정지(stop)했다가 재기동(restart)한다.
provider crash 시나리오는 provider를 SIGKILL한다. 이 프로세스 제어 연산이 없는 harness에서는
해당 시나리오를 "미구현(하네스 대기)"로 둔다. 실행이 끝나면 전용 prefix의 key를 정리하거나
disposable Redis instance를 버린다.

store 정지·복구를 기다리는 대기값은 [README](README.ko.md) §2.1의 readiness 기준이 아니라 §2의
heartbeat/lease/grace 상수에서 계산한 별도 이름의 시나리오 대기값으로 둔다(장애 자체가 검증
대상이므로 readiness 기본값으로 통과시키지 않는다).

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — 기준 동작과 polling fallback

#### SF-A1 store 정상 상태 baseline

우선순위: `P0`

**한마디로:** store가 정상일 때 자동 연결·row 등록·status가 모두 건강하게 보이는가(다른 시나리오의 baseline).

- 절차: Redis store + provider 2 + consumer로 자동 연결을 만들고 request를 보낸다. probe가 각 노드의 runtime query를 조회한다.
- 검증: `ListPeerLocationsAsync(filter)`에 두 provider의 peer row가 살아 있는 owner로 보인다. request가 둘 중 하나에서 처리된다. `GetStatusAsync`가 store healthy, owner lease 갱신 정상, last refresh 갱신을 보여준다. (Config 1 RM-A1과 같은 baseline)
- 세부 동작: store 기반 자동 연결 + runtime status 기준값.

#### SF-A2 polling fallback (watch 없는 store)

우선순위: `P1`

**한마디로:** watch를 제공하지 않는 store 구성에서도, polling만으로 peer 변경이 polling interval 안에 같은 결과로 반영되는가.

- 절차: watch를 구현하지 않은 store 구현체를 `AddLocationStore(instance)`로 등록한 배포에서, provider 하나를 추가로 띄웠다가 정상 종료한다. 등록 표면은 통합 계약 인스턴스 하나뿐이며 책임별 개별 등록 함수는 없다([40 §3](../../spec/server/40-location-runtime.ko.md)).
- 검증: watch event 없이 polling만으로 추가/제거가 desired target set에 반영된다 — 추가 후 polling interval 몇 tick 안에 새 provider가 routing 대상이 되고, 제거 후 그 provider로 더 가지 않는다. `GetStatusAsync`가 watch가 꺼져 있고 polling이 동작 중임을 보여준다. watch를 지원하는 Redis extension 배포와 결과 의미가 같다(watch는 latency 최적화일 뿐 correctness는 polling이 보장).
- 세부 동작: polling이 correctness 경로임을 고정(watch는 선택 최적화).

### Track B — store 장애 중 (fail-static)

#### SF-B1 store 장애 중 기존 연결 유지

우선순위: `P0`

**한마디로:** store가 끊겨도 이미 연결된 자동 연결과 messaging이 그대로 살아 있는가(즉시 전체 disconnect가 없는가).

- 절차: SF-A1 상태에서 consumer가 지속 request를 보내는 동안 harness가 Redis를 정지한다. lease TTL보다 짧은 시간 뒤 상태를 관측한다.
- 검증: store 장애 중에도 기존 연결로 request가 계속 성공한다(messaging은 store와 독립). consumer는 disconnect diff를 계산하지 않아 provider 연결이 유지된다. `GetStatusAsync`가 store unhealthy와 last error를 기록하고, owner lease heartbeat 실패가 status에 나타난다(backoff 재시도).
- 세부 동작: fail-static — 마지막 성공 desired set 유지 + 장애 관측.

#### SF-B2 store failure grace 초과

우선순위: `P1`

**한마디로:** 장애가 grace를 넘겨 길어지면 새 outbound connect만 멈추고, 이미 ready인 연결은 transport가 살아 있는 한 유지되는가.

- 절차: store 장애를 store failure grace(기본 후보 `owner lease TTL * 2`)보다 길게 유지한다. 그 사이 지속 request를 계속 보낸다.
- 검증: grace 초과 후에도 이미 ready인 연결의 request는 계속 성공한다. 새 outbound connect는 중단된다(장애 중 재시작한 provider가 있어도 store 복구 전에는 연결 대상에 추가되지 않음 — framework log와 status로 관측). store 복구 후에는 Track C의 복구 순서를 따른다.
- 세부 동작: grace 초과 시 신규 connect 중단 + 기존 연결 보존.

### Track C — owner lease 만료와 stale row

#### SF-C1 provider crash → owner lease 만료 → stale row 제외

우선순위: `P0`

**한마디로:** provider가 row를 지우지 못하고 죽어도, owner lease 만료만으로 그 row가 성공 결과에서 빠지고 consumer가 연결을 정리하는가.

- 절차: store는 정상인 상태에서 `api-b`를 SIGKILL한다(row remove 없이 죽음). owner lease TTL 경과를 기다린 뒤 peer list와 routing을 관측한다.
- 검증: lease 만료 전에는 `api-b`의 row가 아직 보일 수 있지만, lease TTL 경과 후 `ListPeerLocationsAsync(filter)` 성공 결과에서 `api-b` row가 제외된다(물리 삭제는 background cleanup이 담당하므로 성공 결과 제외로 판정한다). consumer의 desired set에서 `api-b`가 빠져 disconnect되고, follow-up request는 `api-a`로만 간다. 죽은 endpoint로 반복 timeout 하지 않는다.
- 세부 동작: row write 없는 crash 전파 — owner lease 만료 join으로 stale row 제외.

#### SF-C2 graceful shutdown 대조 (drain 뒤 owner 정리)

우선순위: `P1`

**한마디로:** 정상 종료한 provider는 먼저 배정 대상에서 제외되고, drain 완료 시 owner row와 lease를
정리해 crash 경로처럼 종료 뒤 lease 만료를 기다리지 않는가.

- 절차: `api-b`의 정상 종료를 요청한다. `Draining=true`가 게시된 동안 새 요청이 `api-b`에
  배정되지 않는지 확인하고, 30초 기본 drain deadline 안에 process가 강제 종료 없이 종료되는지
  기다린다. terminal 종료 직후 owner row가 사라지는지 확인한다.
- 검증: drain 중에는 row를 유지해 기존 연결과 작업을 정리하지만 신규 배정에서는 제외된다.
  정상 종료가 완료되면 `ListPeerLocationsAsync(filter)`에서 `api-b` row가 별도 lease 만료 대기 없이
  사라지고 consumer가 그쪽으로 더 가지 않는다. SF-C1과 달리 강제 종료나 lease 만료만으로
  통과시키지 않는다.
- 세부 동작: draining marker 게시 → 기존 작업 drain → owner 단위 row bulk remove와 lease 제거 →
  terminal 정상 종료.

### Track D — store 복구

#### SF-D1 짧은 장애 복구 (grace 안)

우선순위: `P0`

**한마디로:** store가 grace 안에 돌아오면, 기존 연결을 유지한 채 다음 성공 조회로 조용히 reconcile되는가(mesh 전체 재연결 폭풍이 없는가).

- 절차: SF-B1 상태에서 lease TTL보다 짧게 store를 정지했다가 재기동한다. 복구 후 지속 request와 peer list를 관측한다.
- 검증: 복구 후 첫 성공 조회로 reconcile이 재개되고, 살아 있는 provider와의 기존 연결은 끊기지 않는다(각 provider evidence에 불필요한 disconnect/reconnect marker가 없음). `GetStatusAsync`가 store healthy로 돌아오고 last refresh가 갱신된다. request는 전 구간에서 성공한다.
- 세부 동작: 짧은 장애의 무해 통과(fail-static → fresh list reconcile).

#### SF-D2 긴 장애 복구 — 재등록 우선과 heartbeat 유예

우선순위: `P0`

**한마디로:** 장애가 lease TTL보다 길어 모든 lease가 만료된 뒤 복구돼도, 각 노드가 재등록을 먼저 하고 한 heartbeat interval을 기다린 뒤에만 disconnect diff를 적용해, 살아 있는 peer를 한꺼번에 끊지 않는가.

- 절차: store 장애를 owner lease TTL보다 길게 유지한다(복구 시점에 모든 owner lease가 만료된 상태). 장애 중 `api-b`를 SIGKILL해 "실제로 죽은 peer"도 하나 만든다. store를 재기동하고 복구 흐름을 관측한다.
- 검증: 복구 직후 각 노드가 조회보다 먼저 자기 owner lease와 local peer row를 다시 upsert한다(`ListPeerLocationsAsync`로 재등록 확인). disconnect diff는 heartbeat interval 1회 유예 뒤에 적용된다 — 살아 있는 `api-a`와 consumer 사이 연결은 끊기지 않고 request가 전 구간 성공한다. 유예가 지난 뒤 재등록하지 못한 `api-b`만 desired set에서 빠져 disconnect된다.
- 세부 동작: 복구 순서 — owner lease/local row 재등록 → heartbeat interval 유예 → 빠진 target만 disconnect.

#### SF-D3 runtime status 전이 관측

우선순위: `P1`

**한마디로:** 장애→복구 한 사이클 동안 runtime status가 실제 상태 전이(healthy → unhealthy/last error → healthy/last refresh)를 정확히 보여주는가.

- 절차: SF-D1 또는 SF-D2 실행 중 probe가 각 노드의 `GetStatusAsync`를 단계별로 조회한다.
- 검증: 정상 구간은 store healthy + owner lease 갱신 정상, 장애 구간은 store unhealthy + last error 기록 + lease 갱신 실패 표시, 복구 후 healthy 복귀 + last refresh 갱신이 순서대로 관측된다. watch/polling 상태와 owner lease 갱신 상태 같은 필드가 조회 가능하다. **주소 cache가 없으므로 cache 관련 필드는 status에 없다**([40 §7](../../spec/server/40-location-runtime.ko.md)).
- 세부 동작: `ZLinkLocationRuntimeStatus` 필드의 장애 사이클 반영.

### Track E — store 응답 지연(장애 아님) 중 비블로킹

#### SF-E1 store 응답 지연 중 무관 concurrent 처리 비블로킹

우선순위: `P1`

**한마디로:** store가 끊기지 않고 응답만 느려질 때, 그 지연이 같은 프로세스가 처리하는 무관한
동시 요청(location store와 상관없는 application messaging)까지 막지 않는가.

- 절차: harness가 Redis를 정지시키지 않고 응답 지연만 주입한다(예: proxy를 통한 지연 주입,
  또는 느린 Lua 스크립트로 특정 key에 대한 응답만 지연). 지연이 걸리는 동안 같은 provider
  프로세스로 location store와 무관한 concurrent request를 계속 보낸다. 지연 주입 없는 harness에서는
  이 시나리오를 "미구현(하네스 대기)"로 둔다.
- 검증: 지연 중에도 무관 request의 p99 latency가 SF-A1 baseline 대비 유의미하게 증가하지 않는다
  (예: baseline의 N배 이내로 사전 정의). `GetStatusAsync` 조회 자체도 무관 요청 경로를 막지
  않는다. 이 결과로 store client가 스레드나 이벤트 루프를 점유하지 않고 진짜 비동기·논블로킹으로
  I/O를 수행함을 실측으로 증명한다. 비동기 실행 계약은
  [공통 비동기 실행 정책](../../spec/04-async-execution-policy.ko.md)을 따른다.
- 세부 동작: Redis 응답 지연이 코루틴 dispatcher(Kotlin)나 core I/O 스레드(C++, `PERF_IO_THREADS`)를
  점유하지 않음을 검증. 이 트랙은 store 자체의 정상/비정상보다 client 구현의 비블로킹 여부를 보는
  점에서 Track A~D와 다르다.

## 5. 완료 기준

- `P0` 시나리오(SF-A1·B1·C1·D1·D2)가 모두 통과한다.
- 판정은 public 표면으로만 한다: `GetStatusAsync`의 store health·lease 갱신·last error·last
  refresh, freshness 없는 `ListPeerLocationsAsync(filter)`의 성공 결과(살아 있는 row만), 실제 messaging
  성공, 역할 server evidence. framework 내부 상태를 직접 읽지 않는다.
- stale row 판정은 "성공 결과에서 제외"로 검증한다. 물리 삭제 시점은 background cleanup의
  책임이므로 단언하지 않는다.
- 장애·복구 시나리오는 복구 후 messaging 정상화 + stale row 부재 + 살아 있는 peer 연결 보존을
  함께 검증한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.

비고: Config 1은 store가 정상일 때의 자동 연결·scale·failover를 보고, 이 config는 store 장애와
복구 자체를 본다. store를 함께 쓰는 두 config의 baseline 단언(SF-A1 ↔ RM-A1)은 의도적으로
겹친다.
