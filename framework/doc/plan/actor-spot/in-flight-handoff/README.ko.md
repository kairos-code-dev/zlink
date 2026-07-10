# In-flight packet handoff (source queue handoff) — 구현 런북 (4언어 공통)

이 문서는 `spot-actor.ko.md §10`(actor 이동 중 in-flight packet 처리)을 **C++, Java(+Kotlin), Node,
.NET** framework에 동일 의미로 구현하기 위한 공통 작업 계획이다. 언어별 작업자는 이 문서 하나로
"무엇을·어떤 순서로·무엇으로 검증"하는지 파악할 수 있게 작성했다. 언어별 구체 파일/타입은 P0 audit에서
확정한다(추정 금지).

> 상위 계획은 [../README.ko.md](../README.ko.md)다. 이 런북은 그 계획의 **§10 handoff 계약 부분만**
> 다루는 하위 런북이다. admission/transfer adapter 등 나머지 join/transfer 계약은 상위 계획을 따른다.

## 0. 근거 문서 (정본)

| 문서 | 역할 |
| --- | --- |
| [common/spec/spot-actor.ko.md §10](../../../framework/common/spec/spot-actor.ko.md) | **계약 정본**. queue handoff 6규칙(§10.2), cross-node 순서(§10.3), cutoff·mapping 수명(§10.4). |
| [common/spec/spot-actor.ko.md §3.4](../../../framework/common/spec/spot-actor.ko.md) | moving 중 dispatch 금지(handoff의 전제). |
| [common/spec/spot-actor.ko.md §8](../../../framework/common/spec/spot-actor.ko.md) | location publish 시점·generation fencing. |
| [common/spec/spot-actor.ko.md §9](../../../framework/common/spec/spot-actor.ko.md) | bound session transfer(rebind 경계). |
| [common/e2e/config-10 §4 Track F](../../../framework/common/e2e/config-10-spot-actor-transfer.ko.md) | **검증 정본**. ST-F1~F5 배포형 e2e. |
| `spot-actor.ko.md §12`(언어 요구 13~17) / `§13`(회귀 테스트) | 언어별 구현/테스트 MUST 목록. |

## 1. 무엇을 구현하나 (계약 요지)

actor가 이동(moving)하는 동안 그 actor로 도착한 packet을 **유실 없이, 보낸 순서대로** 전달한다.
기준 모델은 **개별 packet 재라우팅이 아니라 정렬된 queue의 handoff**다.

1. **보존(§10.2-1).** moving 중 source는 actor packet을 source Spot handler로 dispatch하지 않고(§3.4),
   drop하지도 않는다. arrival order로 보존한다.
2. **정렬 handoff(§10.2-2).** commit 시 보존한 backlog를 arrival order로 target에 전달한다. 같은 node
   이동은 인스턴스와 queue를 그대로 유지하므로 자동 만족.
3. **publish 전 replay(§10.2-3, 핵심).** target은 backlog를 target actor dispatch queue에 enqueue한
   **뒤에** committed location을 공개한다. 이 순서를 지켜야 이후 direct packet이 backlog를 추월하지 못한다.
4. **per-session FIFO(§10.2-4).** bound session route는 commit과 함께 atomically rebind되고 backlog가
   direct보다 먼저 enqueue되므로, 한 session의 packet은 이동을 가로질러도 순서 유지. client 협조 불필요.
5. **by-id best-effort(§10.2-5).** by-id sender는 각자 re-resolve 시점에 경로를 바꾼다. 한 sender가
   forward/direct로 갈리면 상대 순서는 best-effort.
6. **straggler cutoff(§10.2-6, §10.4).** location 공개 뒤 stale ref straggler는 `actorTransferForwardWindow`
   **(기본 5초)** 동안 arrival order로 forward, window 후 mapping 축출 + fail-fast(`ActorLocationStale`).

## 2. 공통 설계 모델 (구현 관점)

언어마다 이름은 달라도 아래 구조를 같은 의미로 갖춘다. 실제 타입/파일은 P0에서 확정한다.

### 2.1 backlog는 이미 있는 actor queue다

각 언어는 이미 actor packet을 직렬화하는 큐(per-actor mailbox 또는 per-Spot serial queue)를 갖는다.
moving 중 도착 packet은 이 큐에 arrival order로 남는다 — **새 버퍼를 만들 필요가 없다.** handoff는 이
큐의 미dispatch 부분을 스냅샷해 넘기는 것이다.

### 2.2 handoff는 commit 요청에 실린다 (cross-node)

- source: `TransferOut` state 옆에 **handoff backlog(직렬화된 packet 목록 + arrival index)**를 commit
  요청에 함께 싣는다.
- target: `TransferIn`으로 actor를 materialize하고 `OnJoinedActor`를 부른 뒤, **backlog를 target actor
  queue에 arrival order로 enqueue**한다. 그 다음에 location을 publish한다(§10.2-3).
- backlog dispatch(handler 실행)는 `OnJoinedActor` 완료 후에 시작(§3.4).

같은 node 이동은 인스턴스가 그대로라 backlog 전송이 없다. moving flag만 해제하면 기존 큐가 target Spot
handler로 재개된다.

### 2.3 forwarding mapping (straggler 흡수)

- source가 commit ack로 target location을 확정하는 시점에 `(actorId, oldGeneration) → targetLocation`
  entry를 만든다.
- window(기본 5초) 동안 old generation ref로 온 packet을 target으로 forward(arrival order).
- window 후 entry를 **반드시 제거**(retained state 누수 금지). 이 축출은 source의 멱등 사후 정리에 포함.
- **node당·actor당 entry 최대 하나**(전역 하나가 아님): 한 node에서 같은 actor가 다시 이동하면 그 node의
  기존 entry를 다음 hop으로 **갱신 + window 재시작**. entry는 자기 **다음 hop**만 가리킨다.
- **chained forward**: A→B→C 연쇄면 A의 entry→B, B의 entry→C. A로 온 straggler는 A→B→C로 hop을 따라
  전달된다(A가 C를 직접 아는 게 아님). 각 hop은 자기 window 동안 자기 mapping 유지(§10.4-4).
- lease fencing(§8)과 **독립**: location row/lease를 먼저 release해도 forwarding mapping은 window 동안 유지.

### 2.4 기존 re-routing과의 관계 (특히 .NET)

.NET은 이미 `ShouldForward`/`Forward`(generation/node 불일치 시 target으로 재전송)를 가진다. 이는
"유실 방지·오배달 방지"는 만족하지만 §10의 **정렬 backlog handoff + publish 전 replay + bounded window**와
동일하지 않을 수 있다. P0에서 다음을 확인한다:

- 개별 packet forward가 **backlog 순서를 보존**하는가, 아니면 재정렬 가능성이 있는가.
- forward가 **무기한**인가, window로 제한되는가(→ §10.4 window 필요).
- backlog가 **location publish 전에** target queue에 enqueue되는가(→ 추월 방지 §10.2-3).

gap이 있으면 P2에서 window·replay-before-publish·mapping 축출을 추가한다.

## 3. 작업 축 (언어 공통, 순서대로)

| 단계 | 내용 | 완료 신호 |
| --- | --- | --- |
| **H0. audit** | 현재 언어의 actor queue·moving 처리·(있으면) 기존 forward 경로를 코드로 확인하고 §10 대비 gap 목록화. §2.4 세 질문에 답한다. | gap 목록 + 해당 파일/타입 확정 |
| **H1. backlog handoff** | moving 중 보존 + commit 요청에 backlog 적재 + target enqueue. `handoff_backlog`/`backlog_enqueued` marker. | ST-F1 통과 |
| **H2. publish 전 replay** | backlog enqueue를 location publish보다 먼저로 고정. bound session rebind 경계도 backlog-먼저. | ST-F2, ST-F3 통과 |
| **H3. window + fail-fast** | `actorTransferForwardWindow`(기본 5초, override 가능) 도입. straggler forward, window 후 fail-fast. `straggler_forward`/`stale_fail_fast` marker. | ST-F4 통과 |
| **H4. mapping 축출** | window 후 mapping 제거(누수 없음), node별 chained forward(다음 hop), node당·actor당 entry ≤1. `mapping_evicted` marker. | ST-F5 통과 |
| **H5. contract 테스트** | `§13` 신규 4종(in-flight handoff order / direct overtakes prevented / bound session cross-move order / straggler forward then fail-fast) + forwarding mapping eviction을 in-process runner/fake backend로. | 경량 회귀 그린 |

각 단계는 상위 계획의 P0~P4와 병행/후행할 수 있으나, **H1~H2는 admission/commit 분리(상위 P2)가 선행**돼야
한다(commit 경로에 backlog를 실어야 하므로).

## 4. Evidence marker (config-10 연동)

actor 노드는 config-10 §2의 공통 marker에 더해 아래를 남긴다. 각 actor packet은 **arrival sequence
index**를 붙여 target 처리 순서와 대조한다.

| marker | 의미 | 시점 |
| --- | --- | --- |
| `handoff_backlog` | moving 중 보존한 packet(+index) | source, moving 구간 |
| `backlog_enqueued` | target queue 적재(+index) | target, publish 전 |
| `straggler_forward` | 공개 뒤 window 내 forward | source |
| `mapping_evicted` | window 후 mapping 제거 | source |
| `stale_fail_fast` | 축출 뒤 old ref 거부(`ActorLocationStale`) | source |

**필수 순서 불변식**(evidence로 검증): `backlog_enqueued` < `location_committed` < 임의의 direct packet
처리. 이 순서가 깨지면 ST-F2 실패.

## 5. 테스트

### 5.1 config-10 Track F (배포형 e2e)

| ID | 우선순위 | 검증 요지 |
| --- | --- | --- |
| ST-F1 | P0 | moving 중 도착 `P1→P2→P3`가 유실 없이 target에서 그 순서로 처리, source handler 처리 없음. |
| ST-F2 | P0 | `B1→B2`(backlog) 뒤 `D1`(direct) 순서, `backlog_enqueued`가 `location_committed`보다 먼저. |
| ST-F3 | P0 | bound session `S1→S4`가 rebind 경계를 가로질러 순서 유지. |
| ST-F4 | P1 | window 내 straggler forward, window 후 `stale_fail_fast`, 자동 재전송 없음. |
| ST-F5 | P1 | window 후 `mapping_evicted`·누수 없음, chained 재이동은 entry 갱신(≤1). |

### 5.2 §13 contract 테스트 (in-process 경량)

같은 계약을 fake backend/in-process runner로 검증하는 5종(§13 표의 in-flight handoff order / direct
overtakes prevented / bound session cross-move order / straggler forward then fail-fast / forwarding
mapping eviction). config-10을 못 돌리는 CI에서도 회귀를 잡는 층이다.

## 6. 언어별 시작점 (H0 audit 대상)

아래는 **audit 착수 지점 힌트**이지 확정 사실이 아니다. 각 worker가 코드로 검증한다.

| 언어 | audit 착수 지점(예상) | 특이 확인 |
| --- | --- | --- |
| **.NET** | User Spot serial queue(`ZLinkSpotActivationExecution`), per-actor mailbox, `ZLinkActorSessionForwarder.ShouldForward`/`Forward`, remote commit 경로(`ZLinkActorRemoteJoiner`) | 기존 forward가 §2.4 세 질문을 만족하는지. window·replay-before-publish·mapping 축출이 없으면 추가. |
| **C++** | reference 구현의 spot runtime actor dispatch·remote transfer commit 경로 | native 계층이 handoff/forward를 담당하는지, managed 계약과 정합한지. |
| **Java/Kotlin** | spot runtime actor mailbox·remote join 경로 | Kotlin은 java runtime 공유이므로 java 기준으로 audit. |
| **Node** | spot runtime actor dispatch·remote join 경로 | 단일 스레드 event loop에서 backlog 순서·window timer 처리. |

## 7. 함정 / 주의

- **replay-before-publish를 빠뜨리면** ST-F1은 통과하고 ST-F2만 깨진다(순서 역전). 두 시나리오를 함께
  돌려야 한다.
- **window를 correctness로 오해 금지.** per-session FIFO는 window와 무관(§10.2-4). window는 straggler
  흡수율 vs 자원 트레이드오프일 뿐이다. 테스트에서 window를 짧게 override해도 순서 검증은 그대로 성립해야 한다.
- **mapping 누수.** 한 node에서 같은 actor 재이동을 "새 entry 추가"로 구현하면 그 node에 entry가 쌓인다.
  반드시 갱신(node당·actor당 ≤1)으로. node 간에는 hop별로 각자 하나씩 존재하는 게 정상(누수 아님).
- **같은 node 이동에 backlog 전송을 넣지 말 것.** 인스턴스가 그대로라 큐 재개만 하면 된다(§10.2-2).
- **by-id 순서를 강하게 보장하려 하지 말 것.** 스펙은 best-effort(§10.2-5). 과도한 보장은 불필요한 복잡도.
- **fail-fast 분류.** window 후 straggler는 `ActorLocationStale`로 분류하고 재전송은 caller 몫이다.
  framework가 저장·재전송하면 계약 위반.

## 8. 완료 기준

- H0~H5가 순서대로 그린이고, 상위 계획 P0~P4(admission/commit 분리 포함)가 선행 그린이다.
- config-10 Track F의 P0(ST-F1~F3) + P1(ST-F4~F5)이 그 언어에서 그린(또는 public 표면 부재 시 parity gap 기록).
- `§13` 신규 회귀 5종이 in-process로 그린.
- 마스터 표(../README.ko.md §3, §4)의 ST-F*·계약 13~17 열을 그 언어 상태로 갱신.
- window 기본값 5초를 언어 간 동일하게 사용(override는 배포별).

## 9. .NET 완료 기록 (2026-07-10)

.NET은 H0~H5를 완료했다.

- H0 audit: 기존 Spot 직렬 큐는 moving 중 packet을 source handler에서 실행하지 않는 데에는 충분했지만,
  commit 응답 뒤 source 큐를 개별 forward하므로 target direct packet의 추월 가능성이 있었다. 또한
  `ShouldForward`는 유지 시간을 제한하지 않았다.
- H1/H2: actor runtime state가 moving ingress frame을 arrival index와 함께 보존한다. source는 commit
  payload에 snapshot을 싣고, target은 이 backlog를 actor handoff queue에 먼저 넣는다. target은
  `OnJoinedActorAsync`가 끝날 때까지 새로 도착한 frame도 같은 queue 뒤에 보존한 뒤 순서대로 replay한다.
- H3/H4: source node는 old generation에서 다음 hop으로 가는 mapping 하나를 유지한다. 기본 window는
  5초이며 `IZLinkFrameworkOptions.ActorTransferForwardWindow`로 배포별 override할 수 있다. window가 끝나면
  mapping을 제거하고 old ref request를 `ActorLocationStale`로 응답한다. A→B→C 배포 시나리오에서 hop별
  forwarding과 두 source node의 독립 축출을 확인했다.
- bound session: commit 응답과 함께 도착한 trailing frame을 target으로 보낸 뒤, target ingress barrier가
  받은 개수를 확인한 다음에 source session rebind를 완료한다. 이 경계로 replay packet 뒤에 rebound
  session packet이 이어진다.
- 경량 회귀: `ActorHandoffTests` 5개가 order, direct 추월 방지, bound-session 순서, window cutoff,
  mapping 교체·축출을 검증한다.
- 배포형 회귀: `e2e/SpotActorTransfer/run_e2e.sh ST-F1 ST-F2 ST-F3 ST-F4 ST-F5`가 Track F 전체를 검증한다.

## 10. C++ 진행 기록 (2026-07-10)

C++는 H0~H4 구현과 H5 경량 회귀를 완료했다. Track F 배포형(ST-F1~F5)은 config-10 cpp port에서
검증 예정이다(진행 중).

- H0 audit: 기존 runtime은 moving 중(`blocks_dispatch`) 모든 packet을 `request_rejected`로 거부해
  send가 유실됐고(§10.2-1 위반), commit 요청에 backlog가 없었으며, `complete_remote_actor_transfer`가
  기록한 forwarding route(`actor_routes`)는 축출 없이 영구 유지됐다(§10.4-3 위반).
- H1/H2: `actor_transfer_coordinator_t`가 moving(source local/remote 단계) 중 도착한 send를
  `handoff_packet_t`(packet 이름·payload·content-type·metadata)로 arrival order 보존한다. request는
  reply 채널이 이동할 수 없으므로 retriable `actor_location_stale`로 fail-fast한다(§10.2-5). source
  bridge는 leave 후 backlog를 snapshot해 commit 요청(`handoffBacklog`)에 싣고, commit ack 뒤 늦게
  쌓인 잔여 backlog는 target으로 즉시 forward한다. target `commit_remote_actor_to_spot`은
  `on_actor_joined` 완료 후 backlog를 spot serial queue에 enqueue한 **다음에** route 기록과 location
  publish를 실행하므로 direct packet이 replay를 추월하지 못한다(§10.2-3; 같은 serial queue FIFO).
- H3/H4: `complete_remote_actor_transfer`가 forwarding route 기록과 함께 `(old generation, evict_at)`
  entry를 arm한다. 기본 window 5초, `set_actor_transfer_forward_window`로 override. `activate_forwarding`
  은 entry를 갱신(node당·actor당 ≤1, window 재시작)한다. `cleanup_expired_actor_admissions`(admission/
  commit 경로마다 실행)가 만료 entry의 `actor_routes`/`native_actors`를 제거하되 generation 기록은
  tombstone으로 남겨 old ref를 retriable `actor_location_stale`로 fail-fast시키고 재구체화를 막는다.
- 경량 회귀: `test_cpp_framework_spot_runtime.cpp`의 in-flight handoff 블록(코드 206~218)이
  moving 중 send 보존(소스 handler 미실행)·backlog 순서 handoff·replay 후 direct 순서(`note-1→2→3`)·
  moving 중 request fail-fast(retriable stale)·window 내 mapping 생존·0-window 축출(활성 entry는
  유지, per-key)·tombstone 생존을 검증한다. moving 중 packet 분류를 바꾼 데 따라 기존 단언 6곳을
  `request_rejected`→`actor_location_stale`로 갱신했다.
- 잔여: bound session cross-move 순서(ST-F3)와 straggler 실전 forward(ST-F4)는 배포형에서만 검증
  가능하므로 config-10 cpp port(Track F 포함)에서 마감한다.

## 11. Node 완료 기록 (2026-07-10)

Node는 H0~H5를 완료했다.

- H0 audit: 기존 runtime은 moving 동안 user Spot의 actor를 `leftActors`로만 가려 packet을 보존하지
  않았다. remote commit payload에도 transfer state만 있었고 backlog, bounded forwarding mapping,
  publish 전 replay 단계가 없었다.
- H1/H2: `ZLinkActorHandoffCoordinator`가 moving 시작 뒤 도착한 frame을 arrival index와 함께 보존한다.
  source는 commit payload에 snapshot을 싣고, target은 `OnJoinedActor` 완료 뒤 backlog를 actor dispatch
  queue에서 처리한 다음 location takeover를 실행한다. snapshot과 commit ack 사이에 도착한 packet은
  같은 forwarding 직렬선에 이어 붙는다.
- H3/H4: source node는 actor마다 다음 hop mapping 하나만 유지한다. 기본 window는 5초이고
  `setActorTransferForwardWindow(timeoutMs)`로 배포별 override할 수 있다. window가 끝나면 mapping을
  제거하며 이후 old ref request는 `ActorLocationStale`로 끝난다. forwarded frame은 다음 hop의 actor
  ref와 target Spot을 함께 전달해 A→B→A 연쇄 이동에서도 반복 forwarding 없이 최종 target에 도달한다.
- bound session: moving 구간의 session frame도 같은 backlog에 들어가며, target replay 뒤 새 route로
  들어온 frame이 이어진다. ST-F3에서 `S1→S2→S3→S4` 순서를 확인했다.
- 경량 회귀: `test/contract/actor-handoff.test.js`의 5개 테스트가 backlog 순서, publish 경계, bound-session
  순서, cutoff, mapping 교체·축출을 검증한다.
- 배포형 회귀: `e2e/SpotActorTransfer/run_e2e.sh ST-F1,ST-F2,ST-F3,ST-F4,ST-F5`가 Track F 전체를 검증한다.
