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
| [common/e2e/config-10 §4 Track F](../../../framework/common/e2e/config-10-spot-actor-transfer.ko.md) | **검증 정본**. ST-F1~F6 배포형 e2e. |
| `spot-actor.ko.md §12`(언어 요구 13~18) / `§13`(회귀 테스트) | 언어별 구현/테스트 MUST 목록. |

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
7. **request framing 보존(§10.5).** in-flight packet이 request면 handoff·forward가 request id·flags·reply
   route를 보존해, 이동 후 target 처리 reply가 **원래 caller로 correlate**된다. timeout은 caller 기존
   경로(이동이 리셋 안 함), late reply는 drop. Send는 이 절 불필요.

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
| **H5. contract 테스트** | `§13` 신규 6종(in-flight handoff order / direct overtakes prevented / bound session cross-move order / straggler forward then fail-fast / forwarding mapping eviction / in-flight request reply correlation)을 in-process runner/fake backend로. 마지막 항목은 handoff frame의 request id·flags 보존(§10.5)을 검증(ST-F6). | 경량 회귀 그린 |
| **H6. POSD/DDD 리팩토링 루프** | H1~H5가 그린이 된 뒤, 새/변경 handoff 코드를 codex 에이전트로 반복 리뷰하며 의미있는 POSD/DDD 항목이 없어질 때까지 정리(§8). | codex 재리뷰 CONVERGED |

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
| ST-F6 | P1 | 이동 중 **request**가 target 처리 후 reply를 원래 caller로 correlate, timeout은 caller 기존 경로, late reply drop. (ST-F1~F3은 Send만 씀) |

### 5.2 §13 contract 테스트 (in-process 경량)

같은 계약을 fake backend/in-process runner로 검증하는 6종(§13 표의 in-flight handoff order / direct
overtakes prevented / bound session cross-move order / straggler forward then fail-fast / forwarding
mapping eviction / in-flight request reply correlation). config-10을 못 돌리는 CI에서도 회귀를 잡는 층이다.

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

## 8. POSD/DDD 리팩토링 루프 (H6, 완료 전 전 언어 공통)

H1~H5(구현 + config-10 Track F + contract 테스트 그린)가 끝난 **뒤**에만 시작한다. 목적은 새 handoff
코드(ingress capture, backlog snapshot/replay gate, forwarding mapping, window/eviction, request framing)가
POSD의 깊은 모듈·정보 은닉·복잡성을 아래로 내리는 설계 원칙과 DDD의 도메인 책임 경계 관점에서
god-file·책임 혼합·중복·vestigial을 남기지 않게 정리하는 것이다. 상위 계획 [../README.ko.md §6](../README.ko.md)
의 P5 루프와 같은 흐름이며, 이 루프는 그 P5의 **handoff 부분을 구체화**한 것이다.

**루프 (의미있는 항목이 없어질 때까지 반복):**

1. **게이트 확인** — config-10 Track F(P0 전부) + §13 contract 테스트가 그린인지 먼저 확인. 리팩토링은
   그린 상태에서만 진행한다.
2. **codex 에이전트 리뷰** — 해당 언어의 새/변경 handoff 코드에 codex 에이전트로 POSD/DDD 리뷰를 돌린다.
   handoff 특화 관점(아래).
3. **의미있는 항목 반영** — 리뷰가 낸 항목 중 **의미있는 것만** 반영한다. 핫패스(actor dispatch 큐 등)를
   건드리는 변경은 baseline vs patched 벤치 증거를 첨부한다(측정 없는 perf 변경 금지).
4. **회귀 확인** — 반영 후 config-10 Track F + contract 테스트를 다시 그린으로.
5. **재리뷰 → 수렴 판정** — 다시 2번으로 돌아가 codex 리뷰를 반복한다. 리뷰가 **의미있는 리팩토링
   항목을 더 이상 내지 않으면(CONVERGED)** 루프를 종료한다. "취향/사소" 항목만 남으면 수렴으로 본다.
6. **기록** — 각 라운드의 반영 항목·수렴 판정을 이 문서의 언어별 완료 기록 절(§10~§13) 또는 언어별 refactor-list에 남긴다.

**handoff 특화 리뷰 관점 (codex 프롬프트 seed):**

- ingress capture / backlog snapshot / target replay gate 책임이 한 god-메서드에 뭉치지 않고 분리됐는가.
- forwarding mapping 저장소의 **owner 단일화** — window timer·eviction·chained hop 로직이 한 곳에 응집됐는가.
- backlog 직렬화·arrival index 처리가 여러 곳에 중복되지 않았는가.
- **기존 재라우팅(`ShouldForward` 등)과 새 handoff 경로의 중복·이중 책임** 정리, 구 경로 vestigial 제거.
- moving-dispatch 가드와 §3.4 기존 가드의 통합(가드 중복 방지).
- request framing 보존 로직이 send/request 경로에 흩어지지 않고 한 곳에서 처리되는가(§10.5).
- 핫패스 주석 게이트 — actor dispatch 큐 같은 hot TU에 변경이 있으면 주석·벤치 근거를 남겼는가.

> 참고: 이 저장소의 기존 POSD/DDD 전수 리뷰 방식(언어별 refactor-list + codex 병합 + 다라운드 검증
> CONVERGED)과 같은 흐름을 따른다. codex 리뷰는 한 요청에 한 항목만(병렬 OK, 묶기 금지).

## 9. 완료 기준

- H0~H6가 순서대로 그린이고, 상위 계획 P0~P4(admission/commit 분리 포함)가 선행 그린이다.
- config-10 Track F의 P0(ST-F1~F3) + P1(ST-F4~F6)이 그 언어에서 그린(또는 public 표면 부재 시 parity gap 기록).
- `§13` 신규 회귀 6종이 in-process로 그린.
- 마스터 표(../README.ko.md §3, §4)의 ST-F*·계약 13~18 열을 그 언어 상태로 갱신.
- window 기본값 5초를 언어 간 동일하게 사용(override는 배포별).
- **H6 codex 리팩토링 루프가 CONVERGED**(의미있는 POSD/DDD 항목이 더 나오지 않음)이고, 각 라운드 기록이 남았다.

> 아래 언어별 기록(§10~§13)은 처음에는 H0~H5·ST-F1~F5 기준으로 작성됐다. Node, .NET,
> Java/Kotlin은 H6와 ST-F6/계약18까지 갱신했으며, 나머지 언어는 각 기록의 잔여 표기를 따른다.

## 10. .NET 완료 기록 (2026-07-10)

.NET은 H0~H6와 ST-F1~F5를 완료했다. ST-F6은 framework 구현은 있지만
현재 고정한 bindings 패키지의 응답 연결 제약으로 배포형 검증을 완료하지 못했다.

- H0 audit: 기존 Spot 직렬 큐는 moving 중 packet을 source handler에서 실행하지 않는 데에는 충분했지만,
  commit 응답 뒤 source 큐를 개별 forward하므로 target direct packet의 추월 가능성이 있었다. 또한
  `ShouldForward`는 유지 시간을 제한하지 않았다.
- H1/H2: actor runtime state가 moving ingress frame을 arrival index와 함께 보존한다. source는 commit
  payload에 snapshot을 싣고, target은 이 backlog를 actor handoff queue에 먼저 넣는다. target은
  `OnJoinedActorAsync` 뒤 초기 backlog를 replay하되 capture를 유지한다. 완료 요청에서 source trailing
  frame 뒤에 target에서 새로 보존한 frame을 합쳐 replay한 다음 location을 공개한다.
- H3/H4: source node는 old generation에서 다음 hop으로 가는 mapping 하나를 유지한다. 기본 window는
  5초이며 `IZLinkFrameworkOptions.ActorTransferForwardWindow`로 배포별 override할 수 있다. window가 끝나면
  mapping을 제거하고 old ref request를 `ActorLocationStale`로 응답한다. A→B→C 배포 시나리오에서 hop별
  forwarding과 두 source node의 독립 축출을 확인했다.
- bound session: target은 commit 요청의 session route를 `OnJoinedActorAsync` 전에 설치한다. 이때부터
  target ingress capture가 새 session packet도 backlog 뒤에 보존한다. target은 handoff id와 trailing
  frame을 받은 뒤 replay와 location 공개를 마치고 응답하며, source는 이 응답 뒤에만 정리를 확정한다.
  따라서 개수 기반 barrier 없이 replay packet과 새 session packet의 순서를 유지한다.
- request framing: handoff frame은 원 actor ref, request id·flags, caller node·session 좌표를 보존한다.
  target은 source를 다시 거치지 않고 공개 no-bind reply API를 호출한다. 다만 `Systems.Zlink
  8.6.4`에 포함된 runtime은 원 actor node·generation으로 등록한 대기 request와 target node의
  응답을 연결하지 못하므로 ST-F6의 caller 응답은 timeout된다. source relay는 target이
  caller에 직접 응답해야 하는 계약을 어기므로 사용하지 않는다.
- 경량 회귀: `ActorHandoffTests` 26개가 order, direct 추월 방지, bound-session 순서, window
  cutoff, mapping 교체·축출, 완료 요청 멱등성, request framing·reply route, 중복 commit
  대기, 동시 handoff 거부, admission route 상관관계를 검증한다.
- 배포형 회귀: `e2e/SpotActorTransfer/run_e2e.sh`는 ST-F1~F5를 통과한 뒤 ST-F6 target handler와
  direct reply 제출까지 도달하지만 caller timeout으로 실패한다. 이동한 actor reply 연결을
  지원하는 runtime이 포함된 새 bindings 패키지를 배포한 뒤 중앙 버전을 갱신하고 ST-F6을
  다시 검증한다.
- H6: 완료 신호를 frame 개수 barrier로 유지하는 안과 handoff id를 가진 상관 완료 요청으로 모으는 안을
  비교해 후자를 선택했다. handoff 상태 전이는 `ZLinkActorHandoffState`, wire framing은
  `ZLinkRemoteActorJoinPackets`, ingress capture/forward는 `ZLinkActorHandoffIngress`, location 공개는
  `ZLinkActorOwnershipCoordinator` 한 곳이 각각 소유한다. 2차 재리뷰에서 completion wire에 중복으로
  남은 session route를 제거하고 target capture를 location 공개까지 유지하도록 바로잡았다. count
  barrier와 중복 route 지식이 더 남지 않아 당시 범위는 `CONVERGED`로 판정했다.
- 2026-07-11 재리뷰에서 배포된 bindings의 part 단위 forwarding 계약에는 header가 접수된 뒤 body
  제출이 실패했을 때 multipart를 취소하는 공개 API가 없음을 확인했다. framework가 일반
  `SendToActor`에 .NET 전용 envelope를 얹는 대안도 검토했지만, 다른 언어가 해석할 수 없고 일반 actor
  ingress가 내부 route 메타데이터를 신뢰하게 되므로 폐기했다. 현재 구현은 공개
  `ForwardActorBoundSessionPart`만 사용하고 forwarding window가 끝나면 재시도를 중단한다. 다만 이미
  접수된 header를 원자적으로 취소하는 문제는 framework 내부 리팩토링으로 해결하지 않고 bindings의
  공개 atomic forward 또는 multipart abort 계약 후보로 남긴다.

## 11. C++ 진행 기록 (2026-07-10)

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
  가능하므로 config-10 cpp port(Track F 포함)에서 마감한다. 이후 추가된 계약인 ST-F6/계약18(§10.5 —
  backlog/forwarding의 request framing 보존과 target→caller 직접 correlate. 현재 구현은 moving 중
  request를 retriable fail-fast로 처리하는 §10.5 이전 상태)과 H6(§8 POSD/DDD 루프)도 다른 언어와
  같이 잔여다.

## 12. Node 완료 기록 (2026-07-10)

Node는 H0~H6와 ST-F1~F6을 완료했다.

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
- request framing: source capture는 request sequence, flags, correlation id가 든 stream header 전체를
  backlog에 보존한다. target 처리 결과는 원래 대기 중인 request에 한 번만 연결되며, caller timeout이
  먼저 끝나면 뒤늦은 결과는 기존 late-reply 경로에서 버린다. `handoff_request_frame` evidence로 request
  sequence와 flags 보존도 확인한다.
- 경량 회귀: `test/contract/actor-handoff.test.js`의 6개 테스트가 backlog 순서, publish 경계, bound-session
  순서, cutoff, mapping 교체·축출, request framing·reply correlation·caller timeout을 검증한다.
- 배포형 회귀: `e2e/SpotActorTransfer/run_e2e.sh ST-F1,ST-F2,ST-F3,ST-F4,ST-F5,ST-F6`가 Track F 전체를 검증한다.
- H6 1차: handoff 상태·snapshot·trailing forward·window mapping을 `ZLinkActorHandoffCoordinator` 한 곳의
  책임으로 모으고 Host의 bound-session 패스스루와 이전 dispatcher 잔재를 제거했다. shutdown signal로
  사후 retry를 중단하고 detached task 오류도 공통 runner가 소유하게 했다.
- H6 2차: Entry Spot과 User Spot에 중복돼 있던 target replay·결과 생성·message close를
  `replayActorHandoffBacklog`로 통합했다. request framing evidence는 실패해도 handoff 동작을 바꾸지 않게
  격리했고, target이 request replay 결과를 누락하면 source가 조용히 성공시키지 않고 실패시킨다.
- H6 성능: normal actor dispatch에 추가된 idle handoff 확인을 500만 회·7라운드로 측정한 중앙값은
  baseline 2.16ns, 적용 경로 7.29ns(증가 5.13ns)였다. 실제 per-actor mailbox admission burst는
  200 actor × 50 packet × 5라운드에서 중앙 처리량 905,052 packet/s였다.
- H6 재리뷰: ingress capture, replay, forwarding mapping, framing 소유자가 각각 한 곳이고 기존 경로와
  중복된 의미 있는 리팩토링 항목이 더 남지 않아 `CONVERGED`로 판정했다.

## 13. Java/Kotlin 완료 기록 (2026-07-10)

Java 런타임 한 벌과 이를 공유하는 Kotlin surface에서 H0~H6와 ST-F1~F6을 완료했다.

- H0 audit: actor packet은 `ZLinkActorDispatchSerials`에서 직렬화됐지만 remote 이동 중 packet을
  보존하는 상태와 commit wire backlog가 없었다. routed target에서 다시 이동할 때 native Spot leave를
  호출하는 경로도 실제 membership 형태와 맞지 않았다. stale source native actor는 곧바로 제거되어
  forwarding window와 축출 상태도 없었다.
- H1: `ZLinkActorTransferHandoff`가 moving 시작 뒤 packet을 arrival index와 함께 보존한다.
  `ZLinkActorSpotRoutePackets`는 index, stream header, payload를 commit request에 싣고 target에서 같은
  순서로 복원한다. source capture와 target enqueue는 각각 `handoff_backlog`, `backlog_enqueued` message
  flow marker를 남긴다.
- H2: `ZLinkActorSpotAdmission.commitRoutedActor`는 target의 joined callback과 backlog replay가 끝난 뒤
  location을 publish한다. `location_committed` marker도 이 경계 뒤에 기록한다. commit snapshot 뒤에
  source로 들어온 packet은 같은 직렬선에서 target으로 이어 보내며, ST-F2의 `B1→B2→D1`과 ST-F3의
  bound session `S1→S2→S3→S4`로 순서를 확인했다.
- H3: `actorTransferForwardWindow`의 기본값은 5초이고 배포별 override를 허용한다. source native actor를
  window 동안 유지하므로 old generation ref가 target으로 전달된다. window가 끝나면 source actor를
  제거하고 이후 request가 실패한다. config-10은 2초 override로 forward와 fail-fast를 함께 검증한다.
- H4: forwarding 상태는 node에서 actor ID별 `Map` entry 하나만 유지한다. A→B→C 시나리오에서 A의
  stale ref가 두 hop을 따라 C에 도달하고, A와 B의 source actor가 각자 window 뒤 제거되는지 확인했다.
  `mapping_evicted`, `straggler_forward`, `stale_fail_fast` 증거도 actor node 로그에 남긴다.
- H5: `ZLinkActorTransferHandoffTest`의 6개 테스트가 backlog arrival order, commit snapshot 뒤 trailing
  packet 순서, bound-session metadata, forwarding cutoff, 반복 이동 mapping 교체·축출, request header와
  native reply 좌표 보존을 검증한다.
- request framing: handoff wire는 stream request header와 original actor ref, caller node/session,
  native request id·flags를 함께 보존한다. target은 이 좌표로 caller에 직접 reply하고 source에는
  one-way 전달 완료만 남긴다. ST-F6은 정상 correlation과 caller timeout 뒤 late reply drop을 검증한다.
- 배포형 회귀: Java와 Kotlin의 `SpotActorTransfer/run_e2e.sh`가 ST-F1~F6을 모두 실행한다. Kotlin은
  suspend handler와 Kotlin actor node를 사용하고, backlog와 forwarding 구현은 Java 런타임을 공유한다.
- H6 1차: runtime 본체에 backlog·window state를 계속 두는 안과 transfer 전용 소유자로 분리하는 안을
  비교해 `ZLinkActorTransferHandoff`로 상태·snapshot·mapping·retirement를 모았다. wire framing은
  `ZLinkActorSpotRoutePackets`, target direct reply는 `ZLinkSpotRuntime` 한 곳이 소유한다.
- H6 2차: normal actor dispatch에서 reply 좌표를 먼저 만들던 경로를 moving 확인 뒤로 옮겨, 이동하지
  않는 hot path는 기존 moving state 확인만 하고 allocation을 만들지 않게 했다. target replay와 late
  forwarding은 같은 direct-reply helper를 사용하고 source reply 중계를 제거했다.
- H6 성능: `EntrySpotAdmissionBurstBenchmark`의 현재 200 actor × 500 packet 결과는 210,797 packet/s,
  p95 40.9µs였다. 기존 200 actor × 50 packet baseline 중앙값 183,062 packet/s와 비교해 throughput
  회귀가 없었다. workload 크기가 달라 latency 수치는 직접 비교하지 않고 throughput gate만 사용했다.
- H6 재리뷰: ingress capture, backlog snapshot, mapping 수명, wire framing, target direct reply의 owner가
  각각 한 곳이고 normal dispatch에 새 allocation이 없음을 확인했다. 의미 있는 추가 리팩토링 항목이
  남지 않아 `CONVERGED`로 판정했다.
