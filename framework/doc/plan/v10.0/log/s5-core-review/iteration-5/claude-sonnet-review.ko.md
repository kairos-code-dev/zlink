# S5 Core 구현 독립 리뷰 R2 (Claude Sonnet) — iteration 5 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 4 finding 4건: **해소 3건, 1건은 핵심 결함 해소·부작용 신규 발견**
- 신규 finding: **4건** (`medium 2`, `low 2`) + 관찰 1건(finding 아님)
- 현재 유효 finding: **4건** (`medium 2`, `low 2`)
- I1 계약 구현 일치: **NOT CLEAN** (medium 2)
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN** (low 2)

codex-review.ko.md(iteration-5)는 나의 독립 분석을 실질적으로 마친 뒤에 대조
목적으로만 읽었다(§7 참조). N5-I1-01·N5-I3-01·N5-I3-02는 codex 열람 이전에
이미 직접 코드에서 발견한 사항과 일치해 교차검증됐다.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Sonnet |
| Candidate commit / HEAD | `c8d567c644e06de6e4a477fc960dd1c8a9e2a097` — 시작·종료 모두 동일 |
| Scope hash (시작) | `53ae8e44f5085109c684e81423c88342456d4b1ded9fc54c234a226d14b4c140` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | 동일 값 — 일치, `git status core/` clean |
| iteration 5 delta | `59b3ea940..c8d567c64`, 18 files(core 8 + internals/spec 문서 8쌍 일부 + CHANGELOG.md), +415/−45, 단일 커밋 `core(mesh): resolve S5 iteration-4 findings` |
| 공개 표면 | `check_public_surface.py . core/build/lib/libzlink.so.10.0.0`: **PASS**, 196 exports 정확 일치, 제거 identifier 없음 |
| header contract | `unittest_public_contract_headers`: 재빌드 후 1/1 PASS |
| 정적 hygiene | delta 전수 검토에서 TODO/FIXME/디버그 출력 잔재 없음. clang-format 미준수는 5개 변경 파일 전부에서 관측되나, iteration-4 baseline(`59b3ea940`)에서도 동일 파일이 동일하게 미준수 — delta 신규 아님, finding으로 세지 않음 |
| `cmake --build core/build -j20` | 성공 |
| `ctest --test-dir core/build -j8` | **100% tests passed, 0 failed out of 85** |
| `test_mesh_lifecycle_contracts` 단독 3회 | 매회 9/9 PASS (신규 bind/destroy race 케이스 포함, 안정) |
| ASAN 5 mesh 바이너리 재빌드·재실행 | lifecycle 9·peer_admission 12·stress 3·monitor_matrix 6·node_basic 8 — 전부 AddressSanitizer/LeakSanitizer 리포트 0 |
| TSAN lifecycle (`setarch $(uname -m) -R`) | 9/9 PASS, 경고 14건 전수 stack frame 분류: lock-order-inversion 10건 전부 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy` 계열(known risk #1), data race 4건 = `mailbox_t::recv` command mailbox ypipe 1(known risk #2) + `asio_engine_t::error`의 `blob_t` 읽기 2 + `pipe_t::detach_peer_backref` 1(모두 known risk #3). mesh 신규 race 0 |
| TSAN stress 3회 실행 | 2/3회 `test_ready_handler_churn_under_load` FAIL(동일 "Expected 0 Was 304"), 1/3회 3/3 PASS — §6 관찰 참조(delta 무관 판정) |
| TSAN 2-process peer_admission (timeout 280s) | 3건 실패: round-robin·MIXED·reconnect — manifest가 baseline `472f66a32`에서 동일 재현으로 문서화한 바로 그 3건과 정확히 일치. 신규 `test_nodrop_unreachable_target_accounting` 자체는 PASS. 새 테스트를 거치는 경고들도 전수 known risk #1/#2로 귀결, mesh 신규 race 0 |

scope hash는 manifest §2 정의를 그대로 사용했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## 2. iteration 4 finding 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F-I1-03(재재재) | **해소** | `mesh_stream_session_api.cpp:685-705`에서 `if (!idempotent)` 게이트가 제거되고 insert·idempotent 두 성공 형태 모두 node-lock 재검증(부재/generation 불일치/`draining`)을 거친다. §3에서 형식적 논증으로 전 interleaving을 닫음을 확인 |
| N3-I1-01(재) | **핵심 결함 해소, 부작용 신규 발견(→ CS-I1-01)** | `mesh_messaging_api.cpp:844-905`에서 local mailbox deque placeholder·ready-index 키를 remote commit 이전에 선예약하고 try/catch(bad_alloc)로 감싸며, 실패 시 unlock 전 전량 롤백한다. 원 결함(remote commit 뒤 fallible container 연산)은 닫혔으나, 이 보강 자체가 새 fallible 지점(`slot_base` 벡터 생성)을 try 밖에 남겼다 — §4.1 |
| F-I1-01 | **부분 수용·부분 반박** | 회계 투명화(`unreachable_remote_target_count` 신설, snapshot 사후 축소 중단)와 §5·§7 조화는 수용. §9 문구가 §7의 새 예외를 반영하지 못해 여전히 잔여 긴장이 있음(medium) — §5 상세 |
| N4-I3-01 | **해소** | `git ls-files core/src/runtime/services/mesh/`의 실제 4모듈(+`mesh_wire_internal.hpp`)과 posd-module-structure·architecture·services-internals ko/en 전부 정확히 일치 |

## 3. F-I1-03(재재재) — interleaving 전수 논증

`mesh_stream_session_api.cpp`의 bind 경로를 처음부터 재추적해 결정적
interleaving 주입 seam 없이도 성립하는 논증을 독립적으로 구성했다.

1. **generation은 node 전역에서 단조 증가**한다(`mesh_actor_api.cpp:436,476`:
   `actor.generation = node->next_actor_generation; next_actor_generation += 1;`).
   파괴된 generation 값은 같은 actor_id로도 재사용되지 않는다.
2. **destroy의 순서**(`mesh_actor_api.cpp:600-745`): node lock 아래
   `draining = true` 설정(~638) → drain 대기 루프 → `node->actors.erase`(~718) →
   락 해제 뒤 무조건 `session_bindings_remove_actor(node, *actor_)`(~739) 호출.
   이 마지막 스윕은 (actor_id, generation) 정확 일치로 **삽입 시점과 무관하게**
   해당 generation의 모든 live binding을 제거한다.
3. 임의의 bind 호출이 재검증(node-mutex 임계구역)에서 "not stale"을 관측했다면
   — mutex 임계구역들이 하나의 전순서(total order)를 이룬다는 표준 보장에 따라
   — 그 순간까지 destroy의 draining-set이 아직 일어나지 않았음이 **논리적으로
   강제**된다. 따라서 그 destroy(존재한다면)의 draining-set은 이 재검증보다
   반드시 나중에 정렬되고, 이는 program order로 erasure·최종 스윕까지 이어진다.
4. happens-before는 서로 다른 mutex(registry_lock ↔ node->mutex) 사이에서도
   전이적이므로, bind의 삽입(registry_lock 아래)은 이 체인을 통해 destroy의
   최종 스윕보다 happens-before로 앞선다 — 즉 destroy가 나중에 실행되면
   반드시 그 삽입을 관측하고 제거한다.
5. 결론: 어떤 호출이 성공(= not stale)을 보고했다면, 그 시점 이후 실행되는
   destroy는 반드시 그 binding을 회수한다. iteration-4의 원 반례("idempotent
   경로가 재검증을 건너뛰어, 이미 완전히 파괴되고 스윕도 끝난 generation에
   대해 성공을 보고")는 두 성공 경로 모두 이제 동일하게 재검증하므로 발생
   불가능하다.
6. `session_bindings_remove_actor`(`:1200-1222`)의 제거 기준이 (actor_id,
   generation) 정확 일치이므로, 한 호출의 stale 롤백이 **다른 actor 세대의
   binding을 오삭제할 수 없음**도 함께 확인했다.

이 논증은 신규 테스트의 결정성과 무관하게 성립한다(과제 지시대로 "논증으로
검증"). 다만 §6.2에서 테스트 자체의 검증범위 한계를 별도로 지적한다.

## 4. 축별 finding

### I1 계약 구현 일치 — NOT CLEAN (medium 2)

#### CS-I1-01 (medium) — `slot_base` 사전예약 벡터가 bad_alloc 보호 밖

- 이슈·근거: `core/src/api/mesh/mesh_messaging_api.cpp:878`
  `std::vector<size_t> slot_base (accepting.size ());`가 바로 다음 줄의
  `try { ... } catch (const std::bad_alloc &)`(:882-896) **밖**에서 실행된다.
  `vector`의 크기지정 생성자는 힙 할당을 수행하며 실패 시 `std::bad_alloc`을
  던진다. 이 지점은 이 함수의 다른 모든 fallible 준비 단계(레코드 생성,
  `ready_added.reserve`, `records.push_back`, `ready.insert`)와 달리
  catch되지 않는다.
- 영향: 실제 OOM 조건에서 이 한 줄의 할당 실패가 `extern "C"` 경계를 넘어
  전파되어 C ABI 계약(`ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM` typed 반환)을
  위반하고 `std::terminate` 계열의 process abort로 이어질 수 있다. 데이터
  훼손은 아니지만, 이 보강이 스스로 표방한 불변식("모든 실패 경로가
  unlock 전에 롤백")과 어긋나는 유일한 예외 지점이다.
- 수정 범위: `publish_common`의 local slot 선예약 보조 storage(`slot_base`)
  확보를 나머지 fallible 준비와 동일한 실패-매핑 경계 안으로 이동.
- 검증 방향: `slot_base` 생성 시점의 할당 실패를 주입해 delivery 0, local
  placeholder/ready 잔존 0, `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM` 반환, C ABI
  밖 예외 0을 확인한다.
- 교차검증: codex-review.ko.md(iteration-5) N5-I1-01과 동일 지점·동일 결론.
  codex 열람 전 독립적으로 코드 재추적 중 직접 발견했다(raw-output §4 참조).

#### CS-I1-02 (medium) — spec §9 문구가 §7의 신규 예외를 반영하지 못함

- 이슈·근거: `core/doc/spec/core/service/01-mesh-node.ko.md:467-469`
  ("Logical Multicast는 snapshot 전체의 원자적 reserve/commit까지 보장하며",
  영문 `01-mesh-node.md:512-516` 동일)는 이번 iteration에서 편집되지 않은
  "9. Option과 handle 지원" 절의 비교 문장이다. 반면 §7(:378-395, 이번에
  갱신됨)은 all-or-none이 capacity admission 보장이며 reserve~commit 사이
  peer 이탈은 §5 이탈 규칙에 따라 unreachable로 분리 보고되고 나머지
  target에는 그대로 전달됨을 명시한다. §9의 문장은 이 예외를 참조하거나
  한정하지 않아, §9만 읽으면 여전히 무조건적 snapshot 전체 delivery
  atomicity로 오독될 수 있다.
- 영향: 정본 문서 내부의 두 절이 같은 API에 대해 다른 수준의 보장을
  서술한다. §7이 상세하고 정확한 절이며 실제 구현·공개 필드 의미와
  완전히 정합하지만, §9만 참조하는 독자는 잘못된 기대를 가질 수 있다.
  medium으로 판단한 근거: 실제 동작·회계는 이미 투명하고 §7이 동일 문서
  내에서 올바른 완전한 설명을 제공하므로, 이는 코드 동작의 결함이 아니라
  spec 내부 상호참조 누락이다.
- 수정 범위: `01-mesh-node.{ko,md}` §9의 해당 문장.
- 검증 방향: §7과 §9를 나란히 놓고 동일한 reserve~commit 이탈 시나리오에서
  동일한 성공/전달/회계 결론이 도출되는지 문구 수준에서 대조한다.
- codex-review.ko.md의 F-I1-01 재검토(N5 관련 서술)는 같은 §9/§7 긴장을
  지적하며 **high**로 판정했다. 나는 이슈의 존재 자체는 수용하되 severity는
  **medium으로 반박**한다 — §7이 같은 문서 안에서 이미 명확하고 정확한
  1차 규범이고, 실제 구현·공개 detail 필드가 §7과 완전히 일치하며, 수정
  범위가 문서 한 문장 교정으로 국한되기 때문이다(코드 변경 불요).

### I2 POSD·DDD — CLEAN

- finding 없음. wire 4모듈(`mesh_wire`/`_codec`/`_admission`/`_ingress`) 분리는
  `mesh_wire_internal.hpp` 공유 계약으로 깊은 모듈 경계를 유지하고, 이번
  delta도 그 경계를 넘지 않았다(mesh_messaging_api.cpp·
  mesh_stream_session_api.cpp의 수정은 각자 API 계층의 기존 helper
  `session_bindings_remove_actor`·`publish_common`을 재사용했을 뿐 새 우회
  경로를 만들지 않음). CS-I1-01·CS-I1-02는 계약/구현 정합성 문제이며 모듈
  경계 위반이 아니므로 I1에만 계상하고 I2에 중복 계상하지 않았다.

### I3 정리 완결성 — NOT CLEAN (low 2)

#### CS-I3-01 (low) — CHANGELOG.md 검증 수치 낡음

- 이슈·근거: `CHANGELOG.md:57-58` "Full core suite green: 84/84 CTest
  targets... (`test_mesh_peer_admission`, 10 cases...)". 이번 세션의 실측:
  `ctest --test-dir core/build`는 85개 target 100% pass, `test_mesh_peer_admission`
  단독 실행은 `12 Tests 0 Failures 0 Ignored`. 두 수치 모두 CHANGELOG와
  불일치.
- 영향: release-candidate 공개 검증 기록이 실제 acceptance snapshot보다
  이전 상태를 서술해 신규 regression coverage(`test_nodrop_unreachable_target_accounting`
  등)가 기록에 드러나지 않는다.
- 수정 범위: `CHANGELOG.md` 10.0.0 Verification 항목의 target·case 수치.
- 검증 방향: `ctest -N` target 수와 5개 mesh 바이너리의 `RUN_TEST` 수를
  재집계해 CHANGELOG 서술과 맞춘다.
- 교차검증: codex-review.ko.md N5-I3-01과 동일 지점·결론(codex 열람 전 직접
  ctest·바이너리 실행으로 독립 확인).

#### CS-I3-02 (low) — 신규 bind/destroy race 테스트가 개별 호출 결과를 관측하지 않음

- 이슈·근거: `test_stream_session_bind_destroy_race_leaves_no_binding`
  (`test_mesh_lifecycle_contracts.cpp`)의 두 binder 스레드는
  `(void) zlink_stream_session_bind_actor (...)`로 반환값·errno를 모두
  버린다. assertion은 destroy 이후의 late bind 실패와 최종 binding 잔존
  0에만 있다. §3의 논증이 코드 자체의 정확성을 독립적으로 증명하지만,
  이 테스트는 "어떤 호출도 이미 죽은 generation에 성공을 보고하지 않는다"는
  더 강한 속성을 직접 관측하지 않는다 — 최종 상태만 확인하므로, 만약 향후
  회귀가 재도입되어 한 호출이 이미 스윕이 끝난 generation에 성공을
  보고하더라도 이 테스트의 hammer가 우연히 통과할 가능성을 배제하지 못한다.
- 영향: 테스트 주석이 주장하는 검증 범위보다 실제 회귀 검출력이 좁다. 이번
  iteration의 수정 자체는 §3의 독립 논증으로 정확성이 확인됐으므로 코드
  결함은 아니다.
- 수정 범위: 해당 테스트의 동시 bind 호출 결과·errno 관측과 assertion.
- 검증 방향: 두 스레드의 반환값을 캡처해, "성공 반환 + 그 결과 binding
  부재"의 조합이 발생하지 않음을 라운드마다 직접 assert한다.
- 교차검증: codex-review.ko.md N5-I3-02와 동일 지점·결론.

#### 관찰 (finding 아님) — `test_mesh_stress`의 TSAN 시간예산 flake, delta 무관

- `test_ready_handler_churn_under_load`(`test_mesh_stress.cpp`)를 TSAN 아래
  3회 독립 실행한 결과 2회 동일하게 "Expected 0 Was 304"로 실패, 1회는
  3/3 PASS. 이 테스트 파일은 S4 종료 커밋(`8206fd44d`) 이후 무변경이고,
  경유 함수(`zlink_mesh_node_send_to_channel` → `node_channel_submit`)는
  이번 iteration이 수정한 `publish_common`과 무관한 별개 함수다(grep으로
  미변경 확인). 실패 시 잔여 수치가 두 번 다 동일(304)한 점은 무작위
  race보다 TSAN 감속 아래 고정된 drain 예산(`spin<2000, msleep(2)`, ~4초)
  부족에 가깝다. manifest의 known risk 목록에는 없던 신규 관찰이지만, delta
  무관·기존 파일·자기치유(재실행 시 통과)로 판단해 finding으로 세지 않고
  기록만 남긴다. codex는 이번 iteration에서 바이너리를 재빌드·재실행하지
  않아(스스로 명시) 이 관찰을 하지 못했다.

## 5. Known risk 4건 명시 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **수용·추적 유지, 신규 finding 없음** | lifecycle 10건·peer_admission 다수 경고의 top frame이 전부 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket` 계열(`socket_base.cpp:225,372`). 새 테스트(`test_nodrop_unreachable_target_accounting`)가 node를 생성하며 우연히 이 경로를 거치는 경우도 동일 계열로 귀결 확인 |
| TSAN raw command mailbox ypipe | **수용·추적 유지, 신규 finding 없음** | lifecycle·peer_admission 양쪽 data race 모두 `mailbox_t::recv`(`mailbox.cpp:66,95`)의 command mailbox 읽기/쓰기 경합. mesh 응용 계층 mutex는 스택에 등장하지 않음 |
| raw socket teardown(`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk로 수용·추적 유지, mesh delta finding 아님** | lifecycle TSAN 실행에서 `pipe.cpp:202/723`(`detach_peer_backref`)와 `asio_engine.cpp:1846-1847`(`blob_t::size/data` via `session_base_t::peer_routing_id`)를 직접 관측·재현. manifest가 이미 "신규 raw STREAM 테스트가 노출한 9.x 기계"로 분류한 그대로이며, 이번 delta(mesh 6개 파일)와 무관한 raw socket 계층 코드 |
| ctx_term linger | **수용·추적 유지** | `socket_base.cpp:129-134`의 blocky 기본 linger `-1`은 이번 delta로 변경되지 않음. 신규 `test_nodrop_unreachable_target_accounting`도 parent teardown 전 child 종료를 위해 1500ms 대기를 두는 기존 회피 패턴을 그대로 사용(`test_mesh_peer_admission.cpp` 신규 코드) |

TSAN 2-process admission 3건 실패(round-robin·MIXED·reconnect)는 manifest §4의
주장대로 baseline `472f66a32`에서 동일 재현되는 delta-무관 시간민감성이다 —
이번 세션에서 재실행한 결과도 정확히 이 3건과 동일해 그 주장과 정합했다.

## 6. 최종 판정

blocker 0, high 0, medium 2(CS-I1-01, CS-I1-02) — I1이 `NOT CLEAN`이고,
low 2(CS-I3-01, CS-I3-02) — I3도 `NOT CLEAN`이다. I2는 CLEAN. clean gate는
blocker·high·medium 0과 세 축 CLEAN을 동시에 요구하므로 충족하지 못한다.

CORE REVIEW NOT CLEAN
