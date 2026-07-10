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
// admission — actor instance도 route metadata도 받지 않는다
struct spot_actor_join_response_t {
    bool accepted;
    std::optional<zlink::framework::message_t> reply;
};

// user Spot / Entry Spot(재진입) admission member callback
spot_actor_join_response_t on_actor_join(
    std::string_view actor_id,
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

// mesh options 등록: state 이동이 필요한 actor type만 custom adapter 등록
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

- [x] SPOT dispatch가 `on_actor_join`을 actor id로 호출하는지, 아직 actor instance나
      `actor_join_admission_t`를 넘기는지(예: `contracts/spots/spot.hpp` 등 dispatch 지점 확인).
      → 2026-07-10 재확인: `contracts/spots/spot.hpp`의 erased admission callback과 template
      dispatch 모두 actor id 기반이다.
- [x] `actor_transfer_adapter_t` / `add_actor_transfer_adapter`가
      runtime에 구현·등록·조회되는지(없으면 grep 무결과 → 미구현으로 기록).
      → 2026-07-10 재확인: `framework_options.hpp` 등록 API와 `spot_runtime.cpp`의
      `transfer_actor_out`/`commit_remote_actor_to_spot` 조회 경로, unit test
      (`test_cpp_framework_spot_runtime.cpp`의 stateful transfer)까지 존재한다.
- [x] local join이 `on_actor_join → onLeaveActor → on_actor_joined` 순서이고 success가
      `on_actor_joined` 완료 뒤에 나가는지.
- [x] remote 이동이 admission/commit 분리 + adapter `transfer_out`/`transfer_in` 호출로 되는지.
- [x] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과를 이 문서 하단 `## 현황`으로 추가하고 각 P를 gap 중심으로 좁힌다.

## P1. Interface / 문서 정렬

- [x] `cpp/spec/handler-interfaces.ko.md`를 정본(`cpp-framework-interfaces.ko.md`)에 맞춰 정리:
  `on_actor_join`을 actor id 기반으로, transfer adapter surface 반영. (계약은 정본이 소유
  하므로 이 문서는 서술만 정합.) → 2026-07-10 확인: 이미 정합 상태.
- [ ] 실제 C++ public source interface와 contract test expectation을 목표 정본으로 변경.
- [ ] 기존 샘플/e2e compile break를 정본 시그니처로 변경.
- [x] admission callback이 **actor instance와 route metadata를 받지도 저장하지도 않는다**(정본 §3.1 금지 목록) 확인.
      → 2026-07-10 확인: erased callback 시그니처는 spot instance + actor id + request + serializers뿐이다.
- [x] 저수준 header/raw message로 admission 필드를 노출하지 않는지(정본 §6 마지막 문단).

## P2. Framework runtime 구현

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `on_actor_join`(admission) accept → moving 표시 → source `onLeaveActor` → membership commit → target `on_actor_joined` → committed location → success reply. reject면 leave/joined/location 모두 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. target은 admission에서 instance·membership·public location을 만들지 않음. source `transfer_out` → source `onLeaveActor` → commit(state) → target `transfer_in` materialize → membership commit → `on_actor_joined` → committed location → commit ack → success. remote materialize에서 Entry Spot create callback 호출 안 함. |
| transfer adapter 미등록 | 실패가 아니다. source는 빈 `message_t`로 이동하고 target은 actor factory/public 생성 경로로 materialize한다. |
| custom adapter 빈 state | `add_actor_transfer_adapter`가 등록되어 있고 `transfer_out`이 빈 `message_t`를 반환해도 정상 transfer다. |
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
  CustomerGateway, 필요 시 `framework/languages/cpp/samples/SupportChat` Session/Support)에 actor type별
  `actor_transfer_adapter_t` 구현 + `add_actor_transfer_adapter` 등록을 추가한다. 옮길 domain state가
  없는 actor는 기본 빈 state transfer를 사용한다.
- C++ sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/cpp/e2e/SpotActorTransfer`(기존 `ToActorMessaging`=config-9 구조 참고).

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2(`actor-a`,
  `actor-b`, 같은 actor type + 같은 transfer adapter) · session gateway 2 · transfer controller
  (실제 app HTTP endpoint) · consumer(HTTP client + stream connector).
- `run_e2e.sh`: Redis → actor 노드 → session gateway → transfer controller 순 기동, 시나리오별
  독립 actor/spot id, process 중단/복구.
- 시나리오(P1은 ST-C3·ST-D2뿐, 나머지 전부 P0): ST-A1/A2/A3, ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2.
  - ST-B3 = adapter 미등록 actor type의 기본 빈 state transfer 성공.
  - ST-B4 = custom adapter가 빈 state를 반환해도 성공(`actor-empty-state`, target joined 이후 별도 store에서 domain state 로드 marker).
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

아래 [x]는 2026-07-10 기준 source 확인 + `test_cpp_framework_spot_runtime` unit 검증 근거가 있는
항목이다. 남은 [ ]는 E2E config-10 evidence 또는 추가 검증이 필요하다.

- [x] 1. 같은 node join 순서 `on_actor_join→onLeaveActor→on_actor_joined`
      (unit: local move probe가 `{"admission","leave","joined"}` 순서를 단언)
- [x] 2. remote admission/commit 분리 (`admit_remote_actor_to_spot`/`commit_remote_actor_to_spot`)
- [x] 3. `on_actor_join` public callback은 actor id와 request만 받음
- [x] 4. transfer adapter로 state 전달 **또는 빈 state transfer 명시 처리**
      (unit: stateful transfer_out/transfer_in round-trip)
- [x] 5. transfer adapter 미등록 시 기본 빈 state transfer
      (source는 빈 `message_t`, target은 factory 생성 경로)
- [x] 6. lifecycle callback 기본 no-op public API 없음
      (`spot_t`는 가상 소멸자만 노출, lifecycle callback은 `if constexpr (requires ...)`
      구조 감지로 연결 — 기본 no-op 멤버가 없다)
- [x] 7. source cleanup 실패 → 멱등 정리(성공 유지)
      (store remove는 owner+generation fencing으로 stale release를 ignored_stale 처리,
      lifecycle `release_actor`는 쓰기 전 untrack — takeover 이후 source release가 다른
      claim을 건드리지 않음을 Bingo cross-node E2E로 확인)
- [x] 8. source down signal 없이 pending admission deadline 정리
      (unit: `cleanup_expired_actor_admissions` 후 commit이 admission 부재로 거부)
- [x] 9. `on_actor_joined` 완료 전 caller success 없음
      (unit: joined 콜백을 blocking한 동안 commit future 미완료)
- [x] 10. `on_actor_joined` 완료 전 packet dispatch 차단
      (unit: moving 구간 `relay_actor_packet`이 retriable `actor_location_stale`로 fail-fast.
      2026-07-10 in-flight handoff(§10) 적용으로 send는 거부 대신 backlog 보존 —
      아래 in-flight handoff 절 참조)
- [x] 11. location pending/committed 구분
      (unit: joined 완료 전 store row가 source 위치를 유지, 완료 후 target으로 갱신)
- [ ] 12. bound session transfer commit 전 성공 노출 없음
      (happy path는 unit(commit packet의 bound session 필드·route 기록)과 Bingo cross-node
      E2E(A→B 이전 후 push 도달)로 확인. 실패 시 route 비오염 evidence는 config-10 ST-C 필요)

### in-flight handoff (§10, [in-flight-handoff/README.ko.md](in-flight-handoff/README.ko.md))

2026-07-10 H0~H4 구현 + H5 경량 회귀 완료(상세는 런북 §10 C++ 기록):

- [x] H1/H2 moving 중 send 보존(`actor_transfer_coordinator_t` backlog) + commit 요청
      `handoffBacklog` 적재 + target replay-before-publish + commit ack 후 잔여 backlog forward
- [x] H3/H4 forwarding window(기본 5초, `set_actor_transfer_forward_window` override) +
      `cleanup_expired_actor_admissions` 축출(entry ≤1/actor, generation tombstone 유지)
- [x] H5 경량 회귀: `test_cpp_framework_spot_runtime` 코드 206~218
      (보존·순서·direct 추월 방지·moving request fail-fast·window 생존/축출/tombstone)
- [ ] Track F 배포형 ST-F1~F5 (config-10 cpp port에서 검증 — bound session cross-move
      순서(ST-F3)·straggler 실전 forward(ST-F4) 포함)

### interface/문서
- [x] 정렬용 `handler-interfaces.ko.md`를 정본 actor id admission/adapter 모델로 정리
      (2026-07-10 확인: 문서가 이미 actor id admission·`add_actor_transfer_adapter`·
      미등록 시 기본 빈 state transfer 서술로 정렬돼 있음)
- [ ] 실제 public source interface와 contract test expectation을 목표 정본으로 변경
      (2026-07-10: `session_actor_manager_t`→`actor_ref_t` 마이그레이션 시도는 원복됨.
      현행 public surface 기준 contract test는 그린)
- [ ] 기존 샘플/e2e compile break 정리
- [x] runtime dispatch가 `on_actor_join(actor_id,...)` + adapter/default 빈 state transfer로 동작

### 샘플
- [ ] Bingo/TicTacToe local join 순서 정합
      (2026-07-10: Bingo/TicTacToe run_sample.sh 각각 통과. 순서 코드 검토는 별도 확인 필요)
- [ ] DeliveryDispatch(+SupportChat) remote transfer adapter 등록·순서 정합
- [ ] sample 전체 runner 통과
      (2026-07-10 실측, redis-vcpkg 빌드(8.6.4 package로 재구성): Bingo·TicTacToe·GameQuest·
      SupportChat·DeliveryDispatch 통과. DeliveryDispatch는 run_sample.sh의
      `LOCAL_READINESS_*` unbound variable 결함을 수정한 뒤 통과.
      **ShoppingMall 실패**: 서버 프로세스 segfault(139) 후 클라이언트가 HTTP
      "end of stream"으로 abort. 2026-07-10 격리 결과 두 가지 발견:
      ① 한 바이너리에 boost 3계열 혼합(vendored 1.85 + root 소유 shim의 시스템 beast 1.83
      + vcpkg 1.91) — `zlink_http_client`가 자체 include 배선을 쓰던 것을
      `zlink_framework_cpp_add_boost_headers` 공용 헬퍼로 통일하고 vcpkg manifest에
      `boost-beast`를 추가해 해소(ABI 지뢰 제거, actor transfer와 무관한 기존 위험).
      ② 통일 후에도 지속되던 진짜 크래시 — **2026-07-10 근본 해결**: ASAN 전 스택 계측으로
      크래시 PC가 매 실행 동일 주소(=ODR 시그니처)임을 확인, `addr2line`으로 crash PC는
      vcpkg boost 1.91 헤더, caller 프레임은 vendored 1.85로 판별. vcpkg manifest에
      `boost-beast`를 추가하며 생긴 vcpkg boost가 redis++ interface include 경로를 타고
      `<boost/asio.hpp>`를 직접 include하는 샘플 TU(store.hpp 등)에 유입 → 한 바이너리에
      1.85/1.91 혼합(weak 심볼 병합이 TU별로 갈림) → 1.85가 만든 scheduler를 1.91 코드가
      읽어 SEGV. workflow 역할의 `pipe.cpp:742` "Bad address" abort도 같은 혼합의 발현.
      수정: boost를 직접 include하는 실행 타깃 전부에 `zlink_framework_cpp_add_boost_headers`
      적용(`add_zlink_framework_sample`/`add_zlink_connector_test` 함수 내부 +
      store_location_resolvers 테스트 + runtime_monitoring trigger). ShoppingMall 스모크 통과.
      ③ 격리 중 발견한 부가 결함 수정: libzlink.so가 내부 asio 심볼 3,900여 개를 전역으로
      export해(전체 4,117 중 공개 API는 213) 소비자 프로세스의 asio 인스턴스화를 로드타임에
      가로챌 수 있었다(core asio는 `BOOST_ASIO_STANDALONE=1` 구성, framework asio는 비구성 —
      레이아웃 불일치 위험). 기존 `core/src/libzlink.vers`(미배선)를 Linux shared 타깃에
      `--version-script`로 연결해 `zlink_*`만 export하도록 수정. 내부 심볼을 쓰는 core
      whitebox 테스트는 Windows DLL과 같은 규칙으로 static 링크로 전환. 코드젠 무변경
      (perf 영향 없음). core unit 23/23·framework unit 20/20·Bingo E2E 통과 재확인.
      단, 이 수정으로도 SM 크래시는 재현되어 심볼 간섭은 원인이 아니었음(위생 개선으로 유지).
      함정: `build-redis-vcpkg`는 zlink_cpp package 버전이 CMake 캐시에 고정되므로 package
      버전을 올리면 `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION`과
      `ZLINK_FRAMEWORK_CPP_LOCAL_ZLINK_CPP_PREFIX`를 함께 재구성해야 한다.)

### e2e config-10 (`framework/languages/cpp/e2e/SpotActorTransfer`)

2026-07-10 착수 조사: 포팅 정본은 `framework/languages/dotnet/e2e/SpotActorTransfer`
(run_e2e.sh 174줄 + Client/Program.cs 712줄 + Shared/Messages.cs 114줄 +
Server/ActorNode/{Program 669, Support 136}줄 — actor 노드 2개 + HTTP 컨트롤 엔드포인트
구조로, 계획 §P4의 5-role 서술보다 작다). java·node는 이미 포팅됨, cpp만 미존재.
선행 조건이던 cpp HTTP hosting co_spawn SIGSEGV는 2026-07-10 해결됨(위 ShoppingMall 항목
② — boost ODR 혼합). 2026-07-10 포팅 구현:
Shared/messages.hpp + Server/ActorNode/main.cpp(3 spot node actor-a/b/c, HTTP 컨트롤 엔드포인트
+ stream 세션 bind) + Client/main.cpp(19 시나리오) + run_e2e.sh 작성, 빌드 그린.
- [x] ST-A1 · [x] ST-A2 · [x] ST-A3 (로컬 join/reject/moving-dispatch 차단 — 실측 통과)
- [x] ST-B1 · [x] ST-B2 · [x] ST-B3 · [x] ST-B4
      (B1/B3/B4 + B2 source cleanup 실패 후 성공 — 단독 실측 통과)
- [x] ST-C1 · [ ] ST-C2 · [x] ST-C3(P1)
      (C1 source down before commit, C3 콜백 실패 4종 — 단독 실측 통과.
      **C2는 cross-node bound-session push 의존**: 세션을 node-b에 bind했는데 actor는 아직 node-a라
      push가 노드를 건너야 함(`request_timeout` code=3). cpp는 "reply/push 채널이 이동 못 함" 설계라
      세션이 actor를 따라가는 forward가 없음 — F4/F5와 동일한 cross-node 채널 forward(ST-F6) 클래스)
- [x] ST-D1 · [x] ST-D2(P1) (로컬/원격 커밋 타이밍·stale release fencing 실측 통과)
- [x] ST-E1 · [x] ST-E2 (bound-session push after remote transfer / rebind isolation — 실측 통과.
      2026-07-11 근본해결: 블로커는 stream 왕복이 아니라 e2e messages.hpp DTO 직렬화 비대칭이었음 —
      connector `stream_payload.hpp`의 응답 디코드 `apply_packet_payload`는 `from_stream_payload`
      오버로드가 없으면 no-op fallback으로 빈 메시지를 남김(요청 경로 `to_packet_payload`는 from_json
      fallback 있어 정상). 작동 e2e(DeliveryDispatch/SpotService)처럼 DTO 네임스페이스에 generic ADL
      브리지 `to_stream_payload`/`from_stream_payload`(=from_json/parse_json) 2개 + message.hpp include 추가로 해결)
- [x] ST-F1 · [x] ST-F2 · [x] ST-F3 · [ ] ST-F4(P1) · [ ] ST-F5(P1)
      (F1 handoff 순서 P1→P2→P3, F2 direct 추월 방지, F3 bound-session cross-move 순서 — 실측 통과.
      **F4/F5는 ST-F6 의존**: window 후 stale ref 요청→ActorLocationStale fail-fast 기대이나, cpp
      `request_to_actor_erased`는 ref를 무시하고 actor_id로 재해결(라이브 위치)→성공. dotnet은 ref를
      native에 제출→노드가 window 내 forward/window 후 stale. cpp 요청은 forward 대신 fail-stale+재해결
      설계라 forward 상관(§10.5)이 없음 → ST-F6 구현 선행 필요. gen 비교 휴리스틱은 F6 케이스서 갈려 미채택)
- [ ] e2e 전체 runner 통과 (**16/19 실측 통과**: 첫배치 14 + B2 + C1. 잔여 3 = C2/F4/F5 —
      전부 cross-node 채널 forward(ST-F6: request reply correlation + bound-session 이동) 클래스에
      막힘. cpp는 reply/push 채널이 노드를 못 넘는 설계라 이 feature가 선행조건. 전 언어 공통 transverse 잔여)

**config-10 포팅 중 발굴·수정한 프레임워크 결함**:
1. 원격 transfer 요청 재시도: `actor_client_impl_t::request_to_actor_erased`가 moving 중
   `actor_location_stale`를 단발 재시도만 해 실패. caller timeout budget 안에서 re-resolve 반복
   루프로 교체(§10.2-5/§10.5-2). "transfer is in progress" 메시지도 stale로 분류.
2. spot mesh 피어 연결: router-only mesh는 `connected_peer_count`가 0이라 원격 transfer의
   peer-wait가 timeout. pub/sub(enable_pub_sub) 활성화로 redis auto-connect가 피어를 잇게 함
   (Bingo/TicTacToe와 동일 패턴). config-10 러너에 pub 포트 3개 추가.
3. handoff evidence marker: spot runtime/bridge에 handoff_backlog·backlog_enqueued·
   straggler_forward·mapping_evicted·stale_fail_fast를 env-gate(`ZLINK_FRAMEWORK_CPP_ACTOR_HANDOFF_MARKERS`)로
   추가. backlog_enqueued는 commit-embedded 재생 경로에서만 발화 — moving 뒤늦게 도착한 packet은
   post-ack forward 경로라 미발화(순서는 보존, F1/F2 통과). marker 발화 시점 정합은 잔여.

### P5
- [ ] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

## 함정 (C++)

- 목표 interface 이름은 정본에 있다 — `actor_transfer_t`/`add_actor_transfer` 같은 **새 이름을 만들지 말고**
  `actor_transfer_adapter_t` / `add_actor_transfer_adapter` / `spot_actor_join_response_t`를 그대로 쓴다.
- admission은 절대 instance나 route metadata를 잡지 않는다 — 다른 언어가 미러링하는 레퍼런스 계약이다.
- adapter 미등록은 실패가 아니라 기본 빈 state transfer다.
- 같은 node join에서 adapter를 호출하면 안 된다(인스턴스 그대로 이동).
- moving 중 dispatch는 "현재 위치 재조회 후 atomic 큐 선택"으로 막는다.
- Track D·E는 실제 location store/stream connector로 관찰(내부 store key 직접 읽어 판정 금지).

## 현황

2026-07-10 P0 audit 결과 C++ 구현은 목표 계약 적용 전 상태다.

- `spot_actor_admission_callbacks_t::join`과 template dispatch는 actor instance를 넘기며,
  `on_actor_join(actor, request)`를 호출한다. P1에서 actor id만 전달하도록 public interface,
  erased callback, contract test와 기존 호출부를 함께 바꿔야 한다.
- framework source 전체에서 `actor_transfer_adapter_t`, `add_actor_transfer_adapter` 구현과 등록·조회
  경로가 발견되지 않았다. remote join은 optional actor
  snapshot과 actor factory를 사용해 target instance를 만드는 기존 경로다.
- local join은 admission accept 뒤 source leave와 target joined를 호출하지만, target route와 location을
  `on_actor_joined`보다 먼저 기록한다. 따라서 joined 완료 전 public location 비노출 계약을 만족하지
  않는다.
- remote join은 admission과 commit이 분리되지 않았고 transfer adapter의 `transfer_out`/`transfer_in`,
  pending admission deadline, commit ack 이후 멱등 source cleanup을 제공하지 않는다.
- generation 값 검사는 일부 기존 leave·dispatch 경로에 있으나, 이동 전체를 소유하는 moving 상태와
  pending/committed location 전환은 없다. bound session route를 commit에 포함해 A에서 B로 옮기는
  경로도 없다.
- P1은 public interface와 compile contract 정렬에 한정한다. P2는 admission/commit 상태와 adapter
  registry를 한 소유자 아래 구현한 뒤 local commit 순서, remote transfer, moving dispatch,
  location·session commit 순으로 검증한다.

2026-07-10 P1 완료 및 P2 진행 상태:

- 이 진행 기록은 이전 스펙 기준이다. 새 스펙에서는 actor id admission, `add_stateless_actor_transfer`
  제거, adapter 미등록 기본 빈 state transfer로 다시 보정해야 한다.
- source runtime의 adapter 미등록 사전 거부는 제거하고, 기본 빈 state transfer로 연결해야 한다.
- location은 target joined 전 source 위치를 유지한 takeover claim을 사용하고, joined 완료 뒤 target
  위치로 갱신하도록 구현 중이다. 실제 location store 조회와 stale source release fencing 검증은 아직
  완료되지 않았다.
- pending admission은 host receive loop에서 deadline cleanup을 수행하지만 timeout evidence와 source
  down 통합 검증이 남아 있다. bound session route의 commit 전달·target bind·source cleanup, callback
  실패별 reconcile, local join moving latch 검증도 남아 있다.

2026-07-10 Bingo cross-node transfer 진단·근본 수정 (gdb thread dump 기반):

- **commit deadlock 수정**: `admit_remote_actor_to_spot`/`commit_remote_actor_to_spot`/
  `leave_actor_for_remote_transfer`가 node `_state->mutex`를 잡은 채 `run_serial_sync`로 user
  lifecycle callback(`on_actor_joined` 등)을 실행했다. 콜백이 spot serial 스레드에서 framework
  send(`send_spot_mesh_parts`→`native_node()`)를 호출하면 lock-order inversion으로 30초 hang이
  재현됐다(Bingo player-2 join 시 `two-player-room-1`의 `send_to_players`). 로컬 join 경로가 이미
  쓰는 관용구대로 콜백 전 unlock/후 relock으로 수정. regression unit(재진입 probe) 추가.
- **stale location-loss fencing 수정**: transfer 완료 직후 source 노드에서 늦게 도착한 location
  ownership-loss 통지(`deactivate_actor_location`)가 `complete_remote_actor_transfer`가 방금 기록한
  forwarding route와 generation을 지워, packet route reply가 수신 ref(옛 generation)를 반향했고
  이후 세션의 모든 relay가 "actor generation is stale"로 실패했다. 노드에 더 새로운 generation
  기록이 있으면 loss 통지를 stale로 무시하도록 fencing 추가. regression unit 추가.
- stale 오류 메시지에 `current=/received=` generation 진단 필드를 추가했다.
- 위 두 수정 후 Bingo cross-node transfer는 admission→commit→game start→number draw→game end까지
  진행된다. 잔여 1건: client1(session-a)의 bound session push가 session-a 수신(`dispatch_send`
  성공)까지 확인되나 stream 클라이언트에 미도달(서버 stream write 경로 의심, 진단 trace 추가 상태).
- core `options_t`의 `can_send_hello_msg`/`can_recv_disconnect_msg`/`can_recv_hiccup_msg` 미초기화
  read(fanout teardown segfault, `test_cpp_framework_store_location_resolvers` 간헐 실패)를
  core 8.6.3 rebuild + local package(8.6.4 prefix) 재배포로 수정했다. valgrind 0 errors, 10회 반복
  통과 확인.
- 검증 시점 기준 core unit 23/23, framework unit 20/20. 단, 같은 날 진행 중인
  `session_actor_manager_t`→`actor_ref_t` 계약 마이그레이션이 일부 테스트/샘플 compile break를
  유발한 상태라, 마이그레이션 안정화 후 전체 rebuild 재검증이 필요하다.
  (같은 날 후속: 해당 마이그레이션은 원복되었고 전체 rebuild 재검증 완료.)

2026-07-10 Bingo cross-node transfer E2E **통과** (근본 수정 3건 추가):

- **stream connector delivery 기아 수정**: immediate dispatch 모드의 `schedule_delivery`가 user
  callback을 connector read pump 스레드에서 inline 실행해, callback이 블록하면(예: 시나리오
  코루틴의 `future.get()`) pump 재가동이 볼모로 잡혀 이후 inbound 프레임(서버 push)이 커널
  Recv-Q에 쌓인 채 배달되지 않았다(ss로 Recv-Q 실측 + gdb thread dump로 확정). .NET처럼 delivery를
  shared runner pool로 post하도록 수정 (`connector_runtime.cpp`).
- **location ownership-lost per-key 수정**: `location_runtime`이 아무 row의 stale 쓰기 1건에도
  무인자 ownership-lost를 발화하고 lifecycle이 `deactivate_all`로 노드의 모든 claim(라이브 actor
  인스턴스 포함)을 파괴했다. cross-node transfer 완료 후 source의 idempotent release(다른 owner가
  takeover한 row 제거 시도)는 store가 의도적으로 ignored_stale을 반환하는 정상 경로인데, 이것이
  같은 노드의 무관한 actor(observer)까지 파괴해 spot의 actor 포인터가 dangling이 됐다. .NET
  (`ZLinkLocationRuntime.OwnershipLost(kind, canonicalKey)`) 동형으로 (kind, canonical key) 단위
  통지 + 해당 key만 deactivate로 수정하고, `release_actor`는 .NET처럼 쓰기 전에 untrack한다.
- **Bingo 클라이언트 시나리오 .NET 정렬**: submit reply는 `running`을 단언(.NET 105행)하고 승자/
  카드/marks 최종 검증은 GameEnded notify 상태로 수행하도록 잘못 포팅된 단언을 수정. `ensure`에
  `std::source_location` 위치 정보 추가.
- 진단 trace는 hot path(send/push/구독 수신)에서 전부 제거했고, transfer 제어면 스테이지 trace
  (dispatcher 수신측 admission/commit 단계, bridge의 generation 필드)와 stale 오류 메시지의
  `current=/received=` 상세만 유지했다.
- 검증: core unit 23/23, framework unit 20/20, Bingo E2E 3회 연속 통과(수정 직후 2회 + trace 정리
  후 1회).
