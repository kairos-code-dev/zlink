# Spot Actor Join / Transfer — framework 적용 계획 (정본 index)

이 디렉토리는 **수정된 Spot Actor Join / Transfer 스펙**을 4개 framework 언어
(C++, Java(+Kotlin), Node, .NET)에 **구현 + 샘플 적용 + e2e 검증 + POSD/DDD 리팩토링**까지
반영하기 위한 작업 계획이다. 언어별 작업자는 자기 언어 worker 문서 하나만 읽고도 누락 없이 진행할
수 있게 작성했고, 이 README는 언어 공통 계약과 전 언어 진행 현황(체크 표)을 모은다.

> 용어 주의: 이번 스펙 개정에서 예전 "transfer codec"은 **"transfer adapter"**로 이름이 바뀌었다.
> adapter를 등록하지 않은 actor type은 별도 API 없이 framework 기본 빈 state transfer를 사용한다.

## 0. 근거 문서 (정본)

| 문서 | 역할 |
| --- | --- |
| [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md) | **계약 정본**. admission/commit 분리, transfer adapter, 기본 빈 state transfer, callback 순서, 장애 처리 기준. 언어 구현이 이 의미와 다르면 parity gap. |
| [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md) | **검증 정본**. 실제 배포형 서버 위에서 계약을 확인하는 e2e 시나리오(Track A~E, ST-A1~ST-E2). |
| [common/spec/actor-model.ko.md](../../framework/common/spec/actor-model.ko.md) | actor 개념·lifecycle 전체 배경. |
| `framework/<언어>/spec/handler-interfaces.ko.md` | **언어별 목표 interface 정본**(java/node/dotnet). 실제 source public API가 아직 이 문서와 다르면 P1에서 source interface를 함께 바꾼다. |
| [cpp/spec/cpp-framework-interfaces.ko.md](../../framework/cpp/spec/cpp-framework-interfaces.ko.md) | **C++ 목표 interface 정본**. admission/adapter 모델이 여기에 확정되어 있다(cpp의 `handler-interfaces.ko.md`와 runtime/contract test는 P1/P2에서 맞춘다). |

worker 문서:

- [cpp-worker.ko.md](cpp-worker.ko.md)
- [java-kotlin-worker.ko.md](java-kotlin-worker.ko.md)
- [node-worker.ko.md](node-worker.ko.md)
- [dotnet-worker.ko.md](dotnet-worker.ko.md)

## 1. 무엇이 바뀌었나 (수정된 스펙 요지)

정본 `spot-actor.ko.md`의 **transfer-adapter 모델**이 이번 적용 대상이다. 핵심:

1. **`OnActorJoin`은 admission만 한다.** actor instance를 받지 않고 actor id와 request만 받는다.
   Accept/Reject만 결정하고 membership·location·client event·instance 접근을 하지 않는다.
   반환형은 언어별 admission 응답 타입(`ZLinkSpotActorJoinResponse`(java/node) /
   `ZLinkSpotActorJoinResult`(dotnet) / `spot_actor_join_response_t`(cpp))이다.
2. **`OnJoinedActor`가 join 완료 신호다.** caller 성공 reply, public location commit, target
   handler dispatch는 모두 target `OnJoinedActor` 정상 완료 **뒤에만** 관찰된다.
3. **`OnLeaveActor`는 target `OnJoinedActor`보다 먼저** 관찰된다(source Spot이 있는 이동).
4. **join 완료 전 packet dispatch 금지.** moving 구간에 source·target 양쪽 handler가 같은 actor
   packet을 동시에 처리하면 안 된다.
5. **remote transfer는 state가 필요한 actor type만 transfer adapter를 등록한다.**
   `TransferOut(actor) → ZLinkMessage`, `TransferIn(actorId, state) → actor`. adapter 미등록 actor
   type은 실패가 아니라 framework 기본 빈 state transfer로 이동한다. **같은 node join은 adapter를
   쓰지 않고 인스턴스를 그대로 이동**한다.
6. **기본 빈 state transfer.** 별도 adapter가 없으면 source는 **빈 `ZLinkMessage`**를 보내고 target은
   actor factory/public 생성 경로로 instance를 만든다. domain state가 필요하면 사용자가
   `ActorTransferAdapter`를 등록한다. `AddStatelessActorTransfer`/`addStatelessActorTransfer` 계열 API는
   제거 대상이다.
7. **admission/commit 분리(remote).** target node는 admission 단계에서 target instance·membership·
   public location을 만들지 않는다. commit 요청의 state를 `TransferIn`으로 복원해 materialize한 뒤
   `OnJoinedActor`를 호출한다. remote materialize는 새 actor 생성이 아니므로 target Entry Spot
   `OnCreateActor`를 **호출하지 않는다**.
8. **location row는 pending/committed를 구분한다.** `OnJoinedActor` 완료 전 public location을 target
   user Spot으로 확정하지 않는다. generation fencing으로 source/target 이중 owner를 막는다.
9. **장애 처리**: source cleanup 실패는 성공을 되돌리지 않고 멱등 재시도로 남긴다. source down
   before commit이면 target은 down signal을 기다리지 않고 **pending admission deadline**으로 정리한다.
   source down after commit이면 target ownership이 이긴다.
10. **bound session transfer**: remote transfer 성공 시 session-bound actor의 push/reply가 target
    instance로 이어진다. 실패한 transfer는 bound session route를 성공처럼 바꾸지 않는다.

### 1.1 문서/코드 정합 이슈 (worker가 반드시 처리)

- **.NET** — public source, guide, 샘플과 e2e를 정본에 맞췄다. remote 이동은 transfer adapter 또는
  기본 빈 state transfer를 사용하며 target에서 `OnCreateActor`를 호출하지 않는다.
- **실제 public source interface** — 2026-07-10 현재 언어별 spec 문서는 목표 정본이지만,
  source 코드는 일부 구형 actor-instance 기반 `OnActorJoin` 또는 adapter 미구현 상태일 수 있다.
  worker는 P0에서 실제 source를 확인한 뒤 P1에서 public source interface와 샘플 compile break를 함께
  처리한다.
- **C++** — interface 정본도 이번 결정에 맞춰 `on_actor_join(actor_id, message_t)`와
  `add_actor_transfer_adapter` 중심으로 정리해야 한다. `actor_join_admission_t`와
  `add_stateless_actor_transfer`가 source나 test에 남아 있으면 P1에서 제거한다. → cpp-worker P0/P1 참고.

## 2. 작업 축 (모든 언어 공통 6단계)

각 worker 문서는 아래 6개 축을 자기 언어로 구체화한다.

| 축 | 내용 |
| --- | --- |
| **P0. 현황 audit** | 현재 runtime/interface/샘플이 정본 계약 대비 어디까지 만족하는지 코드로 확인하고 gap 목록화. 추정 금지. |
| **P1. Interface/contract 정렬** | admission·transfer adapter·joined/leave surface를 목표 정본과 일치시키고, 기존 `ZLinkActorJoinAdmission`/`AddStatelessActorTransfer`/joined·leave 기본 no-op API 제거, 실제 public source interface·샘플 compile break·언어 문서를 함께 reconcile. |
| **P2. Framework runtime 구현** | admission/commit 분리, transfer adapter 호출, adapter 미등록 기본 빈 state transfer, `OnJoinedActor` gate, moving dispatch 차단, pending/committed location, generation fencing, pending admission deadline, 멱등 source cleanup, bound session transfer. |
| **P3. 샘플 적용** | Bingo/TicTacToe/SupportChat처럼 actor domain state가 있는 샘플에 transfer adapter 등록 + 정본 순서 반영. DeliveryDispatch처럼 옮길 domain state가 없거나 진행 중 request 상태만 있는 actor는 기본 빈 state transfer를 사용한다. 마지막에 언어별 sample 전체 runner가 깨지지 않는지 확인. |
| **P4. e2e config-10 구현** | `languages/<lang>/e2e/SpotActorTransfer`(신규)에 Track A~E(ST-A1~ST-E2) 구현. 역할 server endpoint + runner. 마지막에 언어별 e2e 전체 runner가 깨지지 않는지 확인. |
| **P5. POSD/DDD 리팩토링 루프** | P0~P4가 그린이 된 뒤, codex 에이전트 리뷰로 POSD/DDD 관점 리팩토링을 **의미있는 항목이 없어질 때까지 반복**(§6). |

## 3. 마스터 체크 표 — config-10 시나리오 × 언어

상태 표기: `⬜ 미착수` / `🔶 진행` / `✅ 완료` / `🚫 parity gap(feature-map 기록)`.
작업자는 자기 열만 갱신한다. `우선순위`는 config-10 기준.

| ID | 시나리오 | 우선순위 | C++ | Java | Kotlin | Node | .NET |
| --- | --- | :---: | :---: | :---: | :---: | :---: | :---: |
| ST-A1 | local join accept 순서 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-A2 | local join reject side effect 없음 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-A3 | target joined 전 packet dispatch 차단 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-B1 | remote transfer 성공 순서·state 복원 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-B2 | source cleanup 실패는 성공 유지 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-B3 | transfer adapter 미등록 기본 빈 state transfer | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-B4 | custom adapter가 빈 state를 반환해도 성공 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-C1 | source down after admission/before commit | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-C2 | source down after target commit | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-C3 | callback/transfer 실패 분류 | P1 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-D1 | location commit 시점(pending/committed) | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-D2 | stale source release generation fencing | P1 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-E1 | remote transfer 뒤 bound session push | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-E2 | 실패한 transfer는 bound session route 비오염 | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-F1 | in-flight handoff order | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-F2 | direct overtakes prevented | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-F3 | bound session cross-move order | P0 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-F4 | straggler forward then fail-fast | P1 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| ST-F5 | forwarding mapping eviction | P1 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |

> Track F(ST-F1~F5)는 `spot-actor.ko.md §10`(source queue handoff) 신설 계약이다. Node와 .NET은 구현과
> 배포형 검증을 완료했고, 나머지 언어는 아직 구현 전이다.
> 구현 런북은 [in-flight-handoff/README.ko.md](in-flight-handoff/README.ko.md)를 따른다.
>
> Track D·E의 P0는 location store + stream connector가 있는 언어에서 public API만으로 구현한다.
> 필요한 public 표면이 없으면 `🚫`로 두고 feature-map에 public contract parity gap으로 남긴다
> (skip으로 "완료" 처리 금지).

### 3.1 §13 회귀 테스트 → 시나리오 매핑 ("contract 테스트"의 정의)

정본 `spot-actor.ko.md §13`는 별도의 MUST로 언어별 **최소 회귀 테스트 16종**을 요구한다. 이는
config-10 e2e(무거운 다중 프로세스)와 별개로 **각 언어가 in-process runner/fake backend로 갖춰야 하는
경량 회귀 suite**다. 이 문서에서 말하는 **"contract 테스트"는 이 §13 회귀 suite**를 가리킨다(언어별
in-process 테스트 프로젝트에 둔다). 아래 매핑처럼 내용상 config-10 시나리오가 §13를 포괄하므로, e2e와
contract 테스트는 같은 계약을 두 층위(배포형/in-process)에서 검증한다.

| §13 테스트 | 대응 시나리오 |
| --- | --- |
| local join accept order | ST-A1 |
| local join reject no side effect | ST-A2 |
| remote join success order | ST-B1 |
| remote transfer state | ST-B1 |
| remote transfer empty state | ST-B4 |
| missing transfer adapter uses default empty state | ST-B3 |
| source down before commit | ST-C1 |
| source down after commit | ST-C2 (+ ST-B2) |
| joined callback failure | ST-C3 |
| packet during moving | ST-A3 |
| bound session transfer | ST-E1 / ST-E2 |
| in-flight handoff order | ST-F1 (P0) |
| direct overtakes prevented | ST-F2 (P0) |
| bound session cross-move order | ST-F3 (P0) |
| straggler forward then fail-fast | ST-F4 (P1) |
| forwarding mapping eviction | ST-F5 (P1) |

각 worker의 P5 게이트는 **config-10 e2e(P0 전부) + 이 §13 contract 테스트**가 모두 그린인지 확인한다.

> 신규 5종(ST-F*)은 `spot-actor.ko.md §10`(source queue handoff) 추가로 생긴 계약이며, config-10
> Track F(`config-10-spot-actor-transfer.ko.md §4 Track F`)에 시나리오가 정의되어 있다. 구현 런북은
> [in-flight-handoff/README.ko.md](in-flight-handoff/README.ko.md)를 따른다(현재 전 언어 미구현).

## 4. 마스터 체크 표 — 계약 항목(§12) × 언어

`spot-actor.ko.md §12`의 언어별 구현 요구 17항목. 하나라도 빠지면 그 언어는 스펙 미충족(parity gap).

| # | 계약 항목 | C++ | Java | Kotlin | Node | .NET |
| --- | --- | :---: | :---: | :---: | :---: | :---: |
| 1 | 같은 node join callback 순서 `OnActorJoin→OnLeave→OnJoined` | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 2 | remote transfer에서 admission/commit 분리 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 3 | `OnActorJoin` public callback은 actor id와 request만 받음 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 4 | transfer adapter로 state message 전달 **또는 빈 state transfer 명시 처리** | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 5 | transfer adapter 미등록 시 기본 빈 state transfer | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 6 | `OnJoinedActor`·`OnLeaveActor` 기본 no-op public API 없음 | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 7 | source cleanup 실패를 join 실패로 되돌리지 않고 멱등 정리 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 8 | source down signal 없이 pending admission deadline 정리 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 9 | `OnJoinedActor` 완료 전 caller success 반환 안 함 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 10 | `OnJoinedActor` 완료 전 packet dispatch 차단 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 11 | location row가 pending/committed 구분 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 12 | bound session transfer가 commit 전 성공 노출 안 함 | 🔶 | ⬜ | ⬜ | ✅ | ✅ |
| 13 | moving 중 packet을 drop 없이 arrival order 보존·handoff (§10.2-1,2) | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 14 | handoff backlog를 location publish 전에 enqueue (추월 방지, §10.2-3) | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 15 | bound session packet이 이동 가로질러 per-session FIFO (§10.2-4) | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 16 | straggler bounded forwarding 후 상한 초과 시 fail-fast (§10.2-6) | ⬜ | ⬜ | ⬜ | ✅ | ✅ |
| 17 | forwarding mapping을 window(기본 5s) 후 축출·누수 없음, node당·actor당 entry ≤1(다음 hop 지시) (§10.4) | ⬜ | ⬜ | ⬜ | ✅ | ✅ |

> 항목 13~17은 `spot-actor.ko.md §10`(source queue handoff) 신설로 생긴 계약이다. Node와 .NET은 moving
> ingress capture, commit backlog, target replay gate, bounded forwarding mapping으로 구현했다.

## 5. 언어별 시작 gap 요약 (P0 audit 전 사전 정보)

정확한 상태는 각 worker의 P0 audit로 확정한다. 아래는 문서 기준 사전 신호다.

| 언어 | interface 정본 | 특이 gap |
| --- | --- | --- |
| **C++** | 목표 정본은 `actor_transfer_adapter_t<TActor>`, `on_actor_join(actor_id, request)`, `add_actor_transfer_adapter`다. | `actor_join_admission_t`, `add_stateless_actor_transfer`, joined·leave 기본 no-op API가 남아 있으면 P1에서 제거. |
| **Java/Kotlin** | 목표 정본은 `ZLinkActorTransferAdapter<TActor>`, `onActorJoin(actorId, request)`, `addActorTransferAdapter`다. | 실제 Java source는 P0에서 확인하고, 구형이면 P1에서 source public interface와 Kotlin surface를 함께 변경. |
| **Node** | 목표 정본은 `ZLinkActorTransferAdapter<TActor>`, `onActorJoin(actorId, request)`, `addActorTransferAdapter`다. | 실제 Node source는 P0에서 확인하고, 구형이면 P1에서 source public interface와 runtime dispatch를 함께 변경. |
| **.NET** | `IZLinkActorTransferAdapter<TActor>`, `OnActorJoinAsync(string actorId, request)`, `AddActorTransferAdapter`를 구현했다. | source, guide, 샘플, config-10과 전체 runner 검증을 완료했다. |

## 6. P5 — POSD/DDD 리팩토링 루프 (구현 완료 후, 전 언어 공통)

P0~P4(구현·샘플·config-10 e2e 그린)가 끝난 **뒤**에만 시작한다. 목적은 새 join/transfer 코드가
POSD(Program to an interface, Ownership/Separation of concerns, Direct/Declarative)·DDD 관점에서
god-file·책임 혼합·중복·vestigial을 남기지 않게 정리하는 것이다.

**루프 (의미있는 항목이 없어질 때까지 반복):**

1. **게이트 확인** — config-10 e2e(P0 전부)와 contract 테스트가 그린인지 먼저 확인. 리팩토링은
   그린 상태에서만 진행한다.
2. **codex 에이전트 리뷰** — 해당 언어의 새/변경 join·transfer·adapter·admission·dispatch 코드에
   대해 codex 에이전트로 POSD/DDD 리뷰를 돌린다. 관점 예: admission/commit/transfer 책임 분리,
   god-file(대형 spot runtime) 분할, adapter 등록/조회 중복, moving-dispatch 가드 응집, pending
   admission·generation fencing·bound session 로직의 owner 단일화, vestigial(구 codec/factory-recreate
   잔재) 제거, 핫패스 주석 게이트.
3. **의미있는 항목 반영** — 리뷰가 낸 항목 중 **의미있는 것만** 반영한다. 핫패스(hot TU)를 건드리는
   변경은 baseline vs patched 벤치 증거를 첨부한다(측정 없는 perf 변경 금지).
4. **회귀 확인** — 반영 후 config-10 e2e + contract 테스트를 다시 그린으로.
5. **재리뷰 → 수렴 판정** — 다시 2번으로 돌아가 codex 리뷰를 반복한다. 리뷰가 **의미있는 리팩토링
   항목을 더 이상 내지 않으면(CONVERGED)** 루프를 종료한다. "취향/사소" 항목만 남으면 수렴으로 본다.
6. **기록** — 각 라운드의 반영 항목·수렴 판정을 이 plan 디렉토리(또는 언어별 refactor-list 문서)에
   남긴다.

> 참고: 이 저장소의 기존 POSD/DDD 전수 리뷰 방식(언어별 refactor-list 문서 + codex 병합 + 다라운드
> 검증 CONVERGED)과 같은 흐름을 따른다.

## 7. 완료 정의 (DoD)

전체 작업이 완료된 것으로 보려면:

1. 4언어 모두 §4 계약 17항목이 `✅`이다. `🚫`는 중간 상태나 별도 승인된 불가능 항목 표시일 뿐,
   이 작업의 기본 완료 조건이 아니다.
2. config-10 Track A·B·C의 P0 시나리오(ST-B4 포함)와 Track F의 P0(ST-F1~F3)가 4언어에서 같은 의미로
   `✅`(Kotlin 포함). Track F의 P1(ST-F4~F5)은 window/mapping을 public 관찰 수단으로 검증할 수 있는
   언어에서 `✅`, 없으면 `🚫`+feature-map 근거.
3. Track D·E의 P0가 location store + stream connector 보유 언어에서 `✅`이다. public 표면 부재 등으로
   즉시 구현할 수 없는 경우에는 `🚫`와 feature-map 근거를 남기되, 전체 완료 전에 별도 승인받는다.
4. 언어 문서(dotnet guide, cpp handler-interfaces 등)가 정본 transfer-adapter 모델과 정합.
5. 샘플이 정본 순서로 동작하고, Bingo/TicTacToe/SupportChat처럼 actor domain state가 있는 remote transfer 샘플이 transfer adapter를 등록.
6. 언어별 sample 전체 runner와 e2e 전체 runner가 config-10 추가 뒤에도 통과한다.
7. 모든 P0 시나리오가 source/target node rid·actor id·transfer id·callback order marker·location
   snapshot·bound session snapshot을 실패 evidence로 남긴다.
8. **P5 POSD/DDD 리팩토링 루프는 P0~P4 완료 뒤 hardening phase로 수행**한다. 각 언어의 P5 완료 조건은
   CONVERGED(의미있는 항목 소진)이고, 종료 시점에 config-10 e2e + contract 테스트가 그린이어야 한다.
