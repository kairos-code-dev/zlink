# C++ worker — Spot Actor Join / Transfer 적용

> 이 문서 하나로 C++ framework의 Spot actor join/transfer 적용을 끝낼 수 있게 썼다.
> 계약 정본은 [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md),
> 검증 정본은 [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md),
> **C++ interface 정본은 [cpp/spec/cpp-framework-interfaces.ko.md](../../framework/cpp/spec/cpp-framework-interfaces.ko.md)**다
> (`cpp/spec/handler-interfaces.ko.md`는 정렬용 문서로 아직 stale). 전 언어 현황은 [README.ko.md](README.ko.md).

C++ 목표 interface 문서는 가장 먼저 정리되어 있으므로, C++ 구현이 완료되면 다른 언어가 참고할
수 있는 중요한 기준이 된다. 다만 현재 source 구현과 contract test가 이미 레퍼런스라는 뜻은 아니다.

## 0. C++ 시작 상태 (목표 interface 문서는 정렬됨, source는 P0에서 확인)

목표 interface 정본 `cpp-framework-interfaces.ko.md`는 admission/adapter 모델로 정렬돼 있다. 실제
`framework/languages/cpp` source와 contract test가 아직 이 표면을 요구한다는 뜻은 아니다. P0에서
source를 확인하고, 구형 `on_actor_join(actor, message_t)` 또는 adapter 등록 API 부재가 확인되면 P1에서
public source interface, contract test expectation, 샘플 compile break를 함께 고친다.

```cpp
// admission — actor instance가 아니라 identity만 받는다
struct actor_join_admission_t {
    std::string actor_id;
    std::string actor_type;
    spot_rid_t  source_spot_rid;
    spot_rid_t  target_spot_rid;
    node_rid_t  source_node_rid;   // remote route metadata (non-optional)
    node_rid_t  target_node_rid;
};
struct spot_actor_join_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

// user Spot / Entry Spot(재진입) admission member callback
spot_actor_join_response_t on_actor_join(
    const zlink::framework::actor_join_admission_t &admission,
    const zlink::framework::message_t &request);
void on_actor_joined(const TActor &actor);   // join 완료 신호
void onLeaveActor(const TActor &actor);

// transfer adapter (actor type별)
template <typename TActor>
class actor_transfer_adapter_t {
public:
    virtual task_t<zlink::framework::message_t> transfer_out(const TActor &actor) = 0;
    virtual task_t<TActor> transfer_in(std::string actor_id,
                                       zlink::framework::message_t state) = 0;
};

// mesh options 등록: stateless(기본 adapter) / custom
template <typename TActor>
spot_node_builder_t &add_stateless_actor_transfer(std::string actor_type);   // 빈 state 기본 adapter
template <typename TActor, typename TAdapter>
spot_node_builder_t &add_actor_transfer_adapter(std::string actor_type);     // custom adapter type
```

> 주의: admission callback이 반환하는 `spot_actor_join_response_t`(accepted + optional reply)와,
> actor-side `join_spot(...)` 결과인 `actor_join_result_t<TReply>`(`result_code` + actor ref + reply)는
> **다른 타입**이다. `on_actor_join`은 전자를 반환한다.

따라서 C++의 실작업은:

- (a) 정렬용 stale 문서 `cpp/spec/handler-interfaces.ko.md` 정리 — 이 문서는 아직
  `on_actor_join(actor, message)`(instance 기반)로 설명하고 transfer adapter surface를 언급하지 않는다.
  정본 표면으로 문구를 맞춘다(별도 계약 추가 아님).
- (b) public source interface와 contract test expectation을 목표 정본에 맞춘다.
- (c) **runtime 코드**가 정본 interface 의미대로 돌게 구현. (현재 dispatch가 instance 기반
  `on_actor_join`을 호출하는 경로, adapter 미구현 여부를 P0에서 확인.)

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) C++ 열에 반영.

- [ ] SPOT dispatch가 `on_actor_join`을 **admission(`actor_join_admission_t`)** 으로 호출하는지, 아직
      actor instance를 넘기는지(예: `contracts/spots/spot.hpp` 등 dispatch 지점 확인).
- [ ] `actor_transfer_adapter_t` / `add_stateless_actor_transfer` / `add_actor_transfer_adapter`가
      runtime에 구현·등록·조회되는지(없으면 grep 무결과 → 미구현으로 기록).
- [ ] local join이 `on_actor_join → onLeaveActor → on_actor_joined` 순서이고 success가
      `on_actor_joined` 완료 뒤에 나가는지.
- [ ] remote 이동이 admission/commit 분리 + adapter `transfer_out`/`transfer_in` 호출로 되는지.
- [ ] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과를 이 문서 하단 `## 현황`으로 추가하고 각 P를 gap 중심으로 좁힌다.

## P1. Interface / 문서 정렬

- [ ] `cpp/spec/handler-interfaces.ko.md`를 정본(`cpp-framework-interfaces.ko.md`)에 맞춰 정리:
  `on_actor_join`을 admission identity 기반으로, transfer adapter surface 반영. (계약은 정본이 소유
  하므로 이 문서는 서술만 정합.)
- [ ] 실제 C++ public source interface와 contract test expectation을 목표 정본으로 변경.
- [ ] 기존 샘플/e2e compile break를 정본 시그니처로 변경.
- [ ] admission callback이 **actor instance를 받지도 저장하지도 않는다**(정본 §3.1 금지 목록) 확인.
- [ ] 저수준 header/raw message로 admission 필드를 노출하지 않는지(정본 §6 마지막 문단).

## P2. Framework runtime 구현

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `on_actor_join`(admission) accept → moving 표시 → source `onLeaveActor` → membership commit → target `on_actor_joined` → committed location → success reply. reject면 leave/joined/location 모두 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. target은 admission에서 instance·membership·public location을 만들지 않음. source `transfer_out` → source `onLeaveActor` → commit(state) → target `transfer_in` materialize → membership commit → `on_actor_joined` → committed location → commit ack → success. remote materialize에서 Entry Spot create callback 호출 안 함. |
| transfer adapter 미등록 | remote transfer 시작 전 실패(source leave 없음). |
| **stateless adapter** | `add_stateless_actor_transfer(type)`로 등록한 type은 `transfer_out`이 **빈 `message_t`**, `transfer_in`이 actor factory/public 생성 경로로 instance 생성. 빈 state transfer와 미등록 실패를 **구분**(정본 §6). |
| moving dispatch 차단 | moving 구간에 source·target user Spot handler가 같은 actor packet을 동시 처리하지 않음. dispatch가 actor 현재 위치 재조회 후 큐 atomic 선택(정본 §3.4). |
| pending admission deadline | admission accepted 응답에 deadline, commit 미도착 시 target이 스스로 정리. source down signal 대기 안 함(정본 §5.2). |
| 멱등 source cleanup | commit ack 이후 source 실패/종료가 성공 rollback 아님. stale owner release 멱등(정본 §5.1). |
| location pending/committed + fencing | `on_actor_joined` 완료 전 public location 확정 금지. generation fencing(정본 §8). |
| bound session transfer | commit 요청에 source bound session route, `on_actor_joined` 완료 전 target push 성공 금지, 성공 시 A→B 이전, 실패 시 route 비오염(정본 §9). |
| callback/transfer 실패 분류 | 정본 §10 표대로. |

구현 배치는 cpp 정책(`contracts/*` 소유 vs `src/runtime/*` 구현)을 지킨다.

## P3. 샘플 적용

- **local join**: `framework/languages/cpp/samples/Bingo`, `framework/languages/cpp/samples/TicTacToe`가 정본 순서
  (admission → `on_actor_joined` 완료 후 push/location)로 동작하는지 맞춘다. admission에서 room
  membership 확정 코드가 있으면 `on_actor_joined`로 옮긴다.
- **remote transfer**: 다중 node 샘플(`framework/languages/cpp/samples/DeliveryDispatch`의 CourierActorNode/
  CustomerGateway, 필요 시 `framework/languages/cpp/samples/SupportChat` Session/Support)에 actor type별 **transfer adapter
  등록**(state 옮기는 type은 `add_actor_transfer_adapter`, 안 옮기면 `add_stateless_actor_transfer`)을
  추가하고 node 간 이동이 정본 순서로 흐르는지 확인.
- C++ sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/cpp/e2e/SpotActorTransfer`(기존 `ToActorMessaging`=config-9 구조 참고).

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2(`actor-a`,
  `actor-b`, 같은 actor type + 같은 transfer adapter) · session gateway 2 · transfer controller
  (실제 app HTTP endpoint) · consumer(HTTP client + stream connector).
- `run_e2e.sh`: Redis → actor 노드 → session gateway → transfer controller 순 기동, 시나리오별
  독립 actor/spot id, process 중단/복구.
- 시나리오(P1은 ST-C3·ST-D2뿐, 나머지 전부 P0): ST-A1/A2/A3, ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2.
  - ST-B3 = adapter 미등록 실패, ST-B4 = **stateless adapter 등록 + 빈 state transfer 성공**(둘을
    혼동하지 않게 별도 actor type: `actor-no-adapter` vs `actor-empty-state`).
- evidence: callback order marker(`admission, transfer_out(_empty), leave, commit_request,
  transfer_in(_empty), joined, domain_state_loaded, location_committed, commit_ack, source_cleanup`),
  admission input snapshot(instance 없음), transfer state marker, packet handler marker, bound session
  snapshot. 로그 `log/` 파일, message flow 최소 `key_transitions`.
- 실패 주입은 application endpoint 또는 `run_e2e.sh` process 제어로만.
- config-10 단독 runner가 통과한 뒤 C++ e2e 전체 runner를 실행해 기존 config가 깨지지 않는지 확인.

## P5. POSD/DDD 리팩토링 루프

config-10 P0 전부 + §12 contract 테스트(README §3.1 매핑)가 그린이 된 뒤 시작. **codex 에이전트 리뷰 → 의미있는 항목 반영
→ 회귀 그린 → 재리뷰**를 의미있는 항목이 없어질 때(CONVERGED)까지 반복(README §6).

- C++ 특유 관심: `spot_runtime.cpp`류 god-file 분할(join/transfer/adapter/dispatch 책임 분리),
  adapter 등록/조회 응집, moving-dispatch 가드·generation fencing owner 단일화, 구 factory-recreate/
  codec 잔재 제거, 핫패스 주석 게이트.
- hot TU 변경은 baseline vs patched 벤치 증거 첨부(측정 없는 perf 변경 금지).
- 라운드별 반영·수렴을 기록.

## 체크리스트 (C++)

### 계약 항목(§11)
- [ ] 1. 같은 node join 순서 `on_actor_join→onLeaveActor→on_actor_joined`
- [ ] 2. remote admission/commit 분리
- [ ] 3. transfer adapter로 state 전달 **또는 빈 state transfer 명시 처리**
- [ ] 4. transfer adapter 미등록 시 명시 실패(source leave 없음)
- [ ] 5. source cleanup 실패 → 멱등 정리(성공 유지)
- [ ] 6. source down signal 없이 pending admission deadline 정리
- [ ] 7. `on_actor_joined` 완료 전 caller success 없음
- [ ] 8. `on_actor_joined` 완료 전 packet dispatch 차단
- [ ] 9. location pending/committed 구분
- [ ] 10. bound session transfer commit 전 성공 노출 없음

### interface/문서
- [ ] 정렬용 `handler-interfaces.ko.md`를 정본 admission/adapter 모델로 정리
- [ ] 실제 public source interface와 contract test expectation을 목표 정본으로 변경
- [ ] 기존 샘플/e2e compile break 정리
- [ ] runtime dispatch가 `on_actor_join(admission,...)` + adapter 호출로 동작

### 샘플
- [ ] Bingo/TicTacToe local join 순서 정합
- [ ] DeliveryDispatch(+SupportChat) remote transfer adapter 등록·순서 정합
- [ ] sample 전체 runner 통과

### e2e config-10 (`framework/languages/cpp/e2e/SpotActorTransfer`)
- [ ] ST-A1 · [ ] ST-A2 · [ ] ST-A3
- [ ] ST-B1 · [ ] ST-B2 · [ ] ST-B3 · [ ] ST-B4
- [ ] ST-C1 · [ ] ST-C2 · [ ] ST-C3(P1)
- [ ] ST-D1 · [ ] ST-D2(P1)
- [ ] ST-E1 · [ ] ST-E2
- [ ] e2e 전체 runner 통과

### P5
- [ ] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

## 함정 (C++)

- 목표 interface 이름은 정본에 있다 — `actor_transfer_t`/`add_actor_transfer` 같은 **새 이름을 만들지 말고**
  `actor_transfer_adapter_t` / `add_stateless_actor_transfer` / `add_actor_transfer_adapter` /
  `spot_actor_join_response_t` / `actor_join_admission_t`를 그대로 쓴다.
- admission은 절대 instance를 잡지 않는다 — 다른 언어가 미러링하는 레퍼런스 계약이다.
- **미등록 adapter 실패(ST-B3)와 빈 state transfer(ST-B4)는 다른 결과다.** 구분 못 하면 둘 다 깨진다.
- 같은 node join에서 adapter를 호출하면 안 된다(인스턴스 그대로 이동).
- moving 중 dispatch는 "현재 위치 재조회 후 atomic 큐 선택"으로 막는다.
- Track D·E는 실제 location store/stream connector로 관찰(내부 store key 직접 읽어 판정 금지).
