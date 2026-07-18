# S8 CPP bindings 전환 리뷰 — iteration 4 — R1 (opus)

독립 리뷰. R2·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조만(build/실행 없음).
실행 증거는 manifest(라이브러리+15 samples green, no-hit ZERO)에 의존.

## 1. Scope 확인
- 대상 commit: `50faf28fd`(merge). cpp dead-code 수정 실물은 `4a8634bcb`가 담고 있고,
  freeze HEAD `e86213d3a`가 scope를 고정.
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum) =
  `e2190823b037b438a53de285a16be3ce40e92499a7ea06dcc6a73b99291b3bdb` == manifest. **MATCH**.
- 파일 수 120 (include 39 + src 62 + samples 18 + CMakeLists 1). **MATCH**.

## 2. iter-3 finding 해소 판정 (소스 대조)

### C3-1 router_spot send-context 사멸 [해소]
- `router_spot`, `set_router_spot_send_context` 전 scope 0회.
- `zlink_router_recv_part` 호출부 3곳(native_receive.hpp:64/93, Sockets/detail.hpp:118) 전부
  6-인자 10.0.0 시그니처 `(socket,&source_rid,&request_seq,msg,&has_more,flags)` — spot_rid
  out-param 없음. 따라서 router recv가 spot_rid를 surface하지 않아 분기 도달 불가라는 근거 재확인.
- `router.cpp:39` 는 이제 무조건 `set_socket_rid_send_context`. `recv_envelope_t.source_spot_rid`
  필드·reset·ctor 제거, `socket.cpp`의 make() 호출도 source_spot_rid 인자 제거.
- `received_t::send_context_kind_t` = `{none, socket_rid}` 로 축소, 두 switch(submit_direct_send/
  reply)는 socket_rid + 방어적 default만 유지. 내부 정합. **RESOLVED**.

### C3-2 spot_spot enum orphan [해소]
- `spot_spot` 전 scope 0회. enum 값 제거 확인. **RESOLVED**.

### C3-3 iter-2 정리가 만든 orphan 연쇄 [해소]
- `resolve_timeout`, `get_string_option`, `submit_message_array`, `to_send_result`,
  `classify_nonblocking_send_errno`, `send_parts_no_wait`, `submit_message_parts_no_wait`,
  `native_request_timeout_ms` 전부 0회.
- `native_send_result.hpp` 파일 삭제(120=121−1과 정합), 이를 include하던 곳 없음(dangling 0).
- `Service/detail.hpp`의 죽은 `using`(classify_nonblocking_send_errno, get_string_option,
  submit_message_array) 제거. **RESOLVED**.

## 3. 전이적 제거로 인한 NEW orphan 검증 (coordinator 위임 항목)
전이 제거가 새 orphan을 만들었는지 whole-scope 대조:
- native_send.hpp 잔존 3 템플릿(restore/send_parts/reply_parts) 상호 사용 + received_access.hpp가
  send_parts/reply_parts 호출. caller-less 없음.
- 제거 대상 파일들(native_message_parts/options/receive, operation_detail, Service/detail,
  router/socket.cpp)의 나머지 심볼 전부 실사용 caller 확인.
- **판정: 전이적 제거가 새로 만든 orphan은 없다.** `4a8634bcb`의 "no new orphans" 주장 성립.

## 4. 3축 Fresh review

### I1 — 계약 일치 (vs Core 10.0.0)
- 이번 변경은 삭제-only. recv 경로 시그니처(router_recv_part 6-인자, recv_part) Core 10.0.0과 정합.
- Service dispatch 도메인의 `spot_rid`/`source_spot_rid`(dispatch.hpp/dispatch_access.hpp/
  actor_models)는 mesh dispatch·actor 주소 계약의 별개 필드로, C3-1이 지운 Messaging/received
  라우터 send-context와 무관. native dispatch 구조체에서 채워지는 정상 계약 surface.
- 제거된 API residue 0(§5). **Finding 없음. Verdict: CLEAN** (blocker/high/medium 0).

### I2 — POSD / DDD
- 삭제로 인한 구조 붕괴 없음. received_t 는 축소 후에도 응집 유지(lazy send-context 재구성
  주석대로 hot-path std::function 회피 목적 보존).
- 관찰(비-차단): send_context_kind_t가 실질 값 1개(socket_rid)+none 으로 축소되어 discriminator가
  다소 방어적/확장점 성격. 계약·정확성 영향 없음 → low 수준의 설계 관찰(§6).
- **Finding(blocker/high/medium) 없음. Verdict: CLEAN**.

### I3 — 정리 (dead code / no-hit / 전이 orphan)
- C3 연쇄 전량 제거 + 전이 NEW orphan 0(§3).
- 단, C3 범위 밖의 **선존(pre-existing) dead helper** 잔존 발견: `assign_parts_from_native`
  (2 overload, native_message_parts.hpp:138/155) + service::detail alias(detail.hpp:35) 전
  scope caller 0. iter-2(`e919e9857`)에서도 동일 3-occurrence 지문이라 이번 전이 제거가 만든
  것이 아니라 캠페인이 애초에 scope에 넣지 않은 선존 사체. iter-3가 dead helper(to_send_result 등)를
  [low]로 분류한 선례와 동일 성격 → **low**(§6). 내부 detail 전용, 런타임/계약/정확성 영향 없음.
- **blocker/high/medium 없음. Verdict: CLEAN** (low는 iter-4 규칙상 CLEAN 불차단).

## 5. 폐기 API no-hit 판정
전 scope 0회 재확인: zlink_msg_gets, message_t::property, native_request_timeout_ms,
native_send_result, to_send_result, classify_nonblocking_send_errno, resolve_timeout,
get_string_option, submit_message_array, send_parts_no_wait, submit_message_parts_no_wait,
router_spot, spot_spot, set_router_spot_send_context. dangling include 0.
**폐기 no-hit ZERO 확인** (manifest 실행 증거와 일치).

## 6. Low finding 목록 (CLEAN 불차단, follow-up 권장)
- **L4-1 [low, I3]** `assign_parts_from_native` 2 overload(native_message_parts.hpp:138,155) +
  `Service/detail.hpp:35` alias = caller 0 인 선존 dead helper. 제거 권장.
- **L4-2 [low, I3]** `Service/detail.hpp`의 미소비 `using` alias 잔여:
  `close_message_array`(:36), `close_native_parts`(:37) — service::detail 한정 소비처 0.
  (move/restore/submit_native 계열은 실사용 함수라 대상 아님, alias 소비 여부만 관찰.) 정리 권장.
- **L4-3 [low, I2]** `received_t::send_context_kind_t`가 실질 단일 값(socket_rid). 확장 예정
  없으면 discriminator/switch 단순화 여지. 계약 영향 없음.

## 7. 종합
iter-3 C3-1..C3-3 전량 해소, 전이 NEW orphan 0, 폐기 no-hit ZERO. I1/I2/I3 각 축 blocker/high/
medium 0. 잔여는 선존 dead helper·미소비 alias 등 low 3건뿐(iter-4 규칙상 불차단).

BINDINGS REVIEW CLEAN
