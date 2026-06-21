<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Resilience](config-5-resilience-lifecycle.ko.md) | [다음: Monitoring](config-7-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# Config 6 — Discovery·Registry HA 배포

registry tier 자체를 검증하는 config다. Config 1이 단일 registry 위에서 provider 발견과
messaging을 본다면, 이 config는 **registry가 1·2·3개 있을 때**, 늘리거나 줄일 때, 한 대가
죽을 때도 discovery와 messaging이 의도대로 도는지를 본다.

framework의 multi-registry는 **복제·합의(consensus)·convergence 계약이 아니다.** registry끼리
`AddPeer(peerPubEndpoint)`로 peer를 맺으면 서로의 PUB broadcast(service list)를 구독해
service/provider(member-peer) view를 **합산(merge)**한다(topology entry가 아니라 service projection). 노드는 `UseDiscovery().AddRegistryEndpoint(...)`로 여러 registry
endpoint를 둘 수 있는데, 이 또한 client-side로 여러 endpoint view를 합쳐 보는 것이다. 이
config는 그 **peer 합산 view**와 **client multi-endpoint view**를 구분해 검증한다.

판정은 public query 결과로만 한다(`IZLinkRegistryQuery`/`IZLinkRegistryQueryClient`):
`StatusAsync`(`PeerRegistryCount`, `ConnectedPeerRegistryCount`, `ListSeq`), `TopologyAsync`,
`ServiceSummaryAsync`, `MemberPeersAsync(channel)`. 주의: `TopologyAsync`는 **로컬 topology-report만**
반영해 peer가 합산한 provider를 보여 주지 않는다 — peer 합산은 `MemberPeersAsync` + 실제
messaging 성공으로 증명한다. peer 연결성은 `ConnectedPeerRegistryCount`로 본다(registry "peer list"
query field는 없다). `ListSeq`는 provider/route projection·attribute 변경이나 stale topology 처리(READY→LOST 전이·제거)에서 증가한다(일반
topology-report upsert는 올리지 않음) — liveness 지표로 쓰지 않는다.

## 1. 목적과 범위

- 다룬다: peer broadcast 합산 view, late-start peer/registry endpoint, registry 1대 장애 중(살아 있는 endpoint로 구성된) discovery 지속, 다운 registry 복구 후 재합류, embedded/standalone(혼합 포함) 배포, in-process/원격 topology 조회.
- 범위 밖: provider scale·resolve(Config 1), client multi-endpoint **failover 정책**(공개 정책 API 없음 — bounded timeout 관찰만), 일반 resilience(Config 5).

## 2. 서버 구성

| 역할 | 수 | 구성 |
|------|----|------|
| registry | 1~3 (`reg-1~3`) | discovery server. `AddPeer`로 서로의 PUB broadcast를 구독(peer 합산). 각자 pub·router endpoint. peer/registry endpoint는 **구성 단계**에서만 정해진다(runtime 추가 API 없음). |
| provider | 2 (`api-a`, `api-b`) | client-server channel server. `UseDiscovery().AddRegistryEndpoint(...)`로 광고. 시나리오별로 어느 registry에 광고하는지 달라진다. `/evidence`·`/health`. |
| consumer | 시나리오별 | `UseDiscovery`에 registry endpoint를 둔 Discovery client. |
| probe | 시나리오별 | 각 registry를 query해 합산 view를 확인. |

## 3. 실행 모델

`run_e2e.sh`가 시나리오가 요구한 수만큼 registry를 띄워 peer로 묶고(필요한 peer/registry
endpoint는 미리 선언), provider를 광고시킨 뒤 client 시나리오를 실행한다. late-start
시나리오는 미리 선언된 endpoint의 프로세스를 늦게 띄운다. probe는 각 registry의
`MemberPeersAsync` 합산 view를 query한다(`TopologyAsync`는 로컬 report 전용).

## 4. 시나리오

### Track A — peer 합산 view

#### DR-A1 단일 registry (대조군)

우선순위: `P0`

- 절차: registry 1대 + provider 2 + consumer로 request를 보낸다.
- 검증: `TopologyAsync`에 provider 2개가 `Ready`. request가 둘 중 하나에서 처리. (Config 1 RM-A1과 같은 baseline)
- 세부 동작: 단일 registry discovery.

#### DR-A2 비대칭 광고로 peer 합산 검증 (2 registry)

우선순위: `P0`

- 절차: `reg-1`·`reg-2`를 peer로 묶는다. provider는 **`reg-1`에만** 광고한다. probe/consumer는 **`reg-2`만** query/사용한다.
- 검증: provider가 `reg-2`에 직접 광고하지 않았는데도 `reg-2`의 `MemberPeersAsync(channel)`에 그 provider가 member로 나타나고, `reg-2`만 보는 consumer가 그 provider로 실제 messaging에 성공한다(= peer broadcast 합산이 동작). (`TopologyAsync`는 로컬 report만이라 여기선 판정에 쓰지 않는다. direct registration이나 client multi-endpoint가 아니라 peer 합산임을 분리 검증.)
- 세부 동작: peer broadcast 합산 view(MemberPeers + messaging).

#### DR-A3 비대칭 광고로 peer 합산 검증 (3 registry)

우선순위: `P0`

- 절차: `reg-1~3`를 peer로 묶고, provider A는 `reg-1`에만, provider B는 `reg-3`에만 광고. probe는 세 registry를 각각 query.
- 검증: 세 registry 모두 `MemberPeersAsync(channel)`로 A·B를 member로 보고하고, 각 registry만 보는 consumer가 각각 messaging에 성공한다. 각 registry의 `ConnectedPeerRegistryCount`가 기대값(=2)이다.
- 세부 동작: 3-노드 peer 합산 일치(MemberPeers + messaging).

### Track B — registry 증감 (late-start / 정지)

#### DR-B1 late-start registry 합류 (선언된 peer가 늦게 기동)

우선순위: `P1`

- 절차: `reg-1`만 먼저 띄워 discovery·messaging을 돌리고, **미리 선언된** peer `reg-2`(이어서 `reg-3`)를 늦게 기동한다. (runtime에 새 peer/registry endpoint를 추가하는 public API는 없으므로, peer 관계는 처음부터 선언하고 프로세스만 늦게 띄운다.)
- 검증: 늦게 뜬 registry가 peer broadcast를 구독해 기존 provider set을 합산한다 — `MemberPeersAsync(channel)`에 기존 provider가 나타나고, 그 registry만 보는 consumer가 messaging에 성공한다. `ConnectedPeerRegistryCount`가 증가한다. provider·consumer 재시작 없음. (`TopologyAsync`는 로컬 report만이라 peer 합산을 보여 주지 않으므로 판정에 쓰지 않는다.)
- 세부 동작: 선언된 peer의 late-start 합류.

#### DR-B2 registry 정지 시 살아 있는 endpoint로 지속

우선순위: `P1`

- 절차: `reg-1~2` cluster + consumer는 `reg-1`·`reg-2` 두 endpoint를 configured. provider는 `reg-1`·`reg-2` 모두에 직접 광고한다(peer-sourced provider는 peer timeout 후 제거되므로 살아 있는 registry에도 직접 등록). `reg-2`를 정지한다.
- 검증: `reg-1`로 provider discovery·messaging이 bounded timeout 안에 정상 지속된다. (registry-tier HA: 살아 있는 registry로 유지. client가 죽은 endpoint를 자동 failover하는 정책 API는 없으므로 consumer는 살아 있는 endpoint가 configured에 포함된 상태에서만 검증한다.)
- 세부 동작: 살아 있는 registry endpoint로 지속(직접 광고 전제).

### Track C — registry 장애와 복구

#### DR-C1 registry 1대 다운 중 discovery 지속

우선순위: `P0`

- 절차: `reg-1~2`(또는 3) peer cluster에서 1대를 SIGKILL한다. provider는 살아 있는 registry에도 직접 광고된 상태, consumer는 살아 있는 registry endpoint를 configured.
- 검증: 살아 있는 registry로 `MemberPeersAsync`·provider discovery·messaging이 bounded timeout 안에 정상 지속된다. 죽은 endpoint로의 원격 query는 정해진 오류(자동 retry 없음)로 끝나고 무한 대기하지 않는다.
- 세부 동작: registry HA — 1대 장애 무중단(살아 있는 endpoint 기준).

#### DR-C2 다운된 registry 복구 후 재합류

우선순위: `P1`

- 절차: 죽였던 registry를 다시 띄운다(선언된 peer 관계 그대로).
- 검증: 되살아난 registry가 peer broadcast를 다시 구독해 합산한다 — 판정: `MemberPeersAsync(channel)`에 provider가 다시 나타나고 그 registry로 messaging이 성공한다. `ConnectedPeerRegistryCount`가 복구 후 기대값, `ListSeq`는 provider/route projection·attribute 변경이나 stale topology 처리(READY→LOST 전이·제거)에서 증가(일반 topology-report upsert 제외). (provider를 그 registry에 직접 재광고하지 않는 한 `TopologyAsync` "동일 Ready set"으로 판정하지 않는다 — peer-sourced provider는 topology entry가 아니다.)
- 세부 동작: registry 복구 + 합산 view 재구성.

#### DR-C3 전체 registry 일시 다운 후 복구

우선순위: `P2`

- 절차: cluster의 registry를 모두 잠시 내렸다가 복구한다.
- 검증: 전부 다운된 동안 이미 resolve된 연결의 messaging은 정해진 규칙대로 동작하고(원격 query는 명시 재조회로 확인), 복구 후 provider 재광고로 모든 registry의 `TopologyAsync`가 동일 `Ready` set으로 돌아온다.
- 세부 동작: registry 전체 장애 복구.

### Track D — 배포 모델과 topology 조회

#### DR-D1 embedded 배포 (Registry + 서비스 한 프로세스)

우선순위: `P1`

- 절차: registry와 provider 서비스를 한 프로세스(embedded)로 띄우고 consumer가 discovery·messaging을 수행한다.
- 검증: embedded 배포에서도 provider 광고·consumer discovery·messaging이 standalone과 같은 의미로 동작한다.
- 세부 동작: embedded registry 배포.

#### DR-D2 standalone 배포 (Registry만, 대조)

우선순위: `P1`

- 절차: registry를 별도 프로세스(standalone)로 띄우고 같은 검증을 수행한다.
- 검증: standalone 배포가 embedded와 같은 discovery·messaging 의미를 준다.
- 세부 동작: standalone registry 배포(대조군).

#### DR-D3 embedded + standalone 혼합 cluster

우선순위: `P2`

- 절차: embedded `reg-1`(registry+서비스 한 프로세스)과 standalone `reg-2`/`reg-3`를 peer로 묶는다.
- 검증: 두 배포 모델이 같은 `AddZLinkRegistry` API로 peer가 되어, 혼합 cluster에서도 peer 합산 view·discovery·messaging이 정상 동작한다.
- 세부 동작: 혼합 배포 cluster peering.

#### DR-D4 topology 조회 — in-process vs 원격

우선순위: `P1`

- 절차: 같은 router endpoint를 대상으로, embedded 노드는 in-process `IZLinkRegistryQuery.TopologyAsync`로, 별도 client는 원격 `IZLinkRegistryQueryClient.TopologyAsync`로 조회한다.
- 검증: 두 경로가 같은 router endpoint에 대해 **동일한 topology snapshot**을 반환한다. (원격 query client는 단일 endpoint의 `TopologyAsync`만 제공하므로 status/service/member-peers 동등성이나 cluster 전체 병합 view는 요구하지 않는다.)
- 세부 동작: 같은 endpoint에 대한 in-process·원격 topology snapshot 일치.

## 5. 완료 기준

- Track A·C의 `P0`(DR-A1·A2·A3·C1)는 모두 통과한다.
- 판정은 public query field로만 한다: `MemberPeersAsync` member set + 실제 messaging 성공, `ConnectedPeerRegistryCount` 기대값, `ListSeq`는 provider/route projection·attribute 변경이나 stale topology 처리(READY→LOST 전이·제거)에서 증가(일반 topology-report upsert 제외), 같은 endpoint의 topology snapshot 일치.
- 장애·증감 시나리오는 복구·합류 후 messaging 정상화 + stale 부재를 함께 검증한다.
- public contract만 직접 호출하고 `ensure`로 단언한다.

비고: Config 1은 단일 registry에서 provider scale·resolve를 보고, 이 config는 registry tier
자체(peer 합산·증감·장애·배포·조회)를 본다. 두 config의 discovery 단언은 의도적으로 겹친다.
