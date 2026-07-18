# S8 CPP bindings 리뷰 iteration-3 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-3(snapshot `f9b6ba50c`, 121파일 `dbe1085f`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. **iter-2 finding C2-0..C2-4 전량 해소, I1·I2 CLEAN, no-hit ZERO 확인.**
잔여는 전부 **I3 dead-code 연쇄** — iter-2 정리가 orphan을 만든 whack-a-mole. 전이적(transitive) 제거 필요.

## I3 dead-code (전이적 제거 대상)

### C3-1. router_spot send-context 사멸 [medium→dead]
R2는 `submit_direct_send/reply`가 `router_spot`을 switch 미처리해 `default` 후 true 반환(버그)이라
지적했으나, coordinator 검증 결과 **router_spot 자체가 dead**: `router.cpp:39-40`이
`if (out_.spot_rid().has_value()) set_router_spot_send_context(...)`인데, 10.0.0 전환에서
`zlink_router_recv_part`가 spot_rid out-param을 제거해 router recv는 spot_rid를 surface하지 않음 →
분기 도달 불가. 따라서 `send_context_kind_t::router_spot`(received.hpp:69), `set_router_spot_send_context`
(received_access.hpp:41-43), `router.cpp:39-40` spot_rid 분기, submit switch의 관련 처리가 모두 dead.
→ router_spot 경로 전체 제거(spot_spot과 동형). `received_t::spot_rid()` 공개 접근자는 다른 소비처
있으면 유지, 없으면 함께 정리.

### C3-2. spot_spot enum orphan [low]
`received.hpp:70` `send_context_kind_t::spot_spot` — C2-2가 setter 제거, 값 잔존. 제거.

### C3-3. iter-2 정리가 만든 orphan 연쇄 [low] (R1 F3-1~F3-4)
- `native_request_timeout_ms` 제거로 `resolve_timeout`(operation_detail.hpp:26) orphan.
- `native_send.hpp` dead trio + `to_send_result` cascade.
- `get_string_option` + dead `using`.
- `submit_message_array`(F1에서 service는 borrowed adapter로 이전, raw도 미사용화됐는지 확인) + dead `using`.

## 처리 방침 (전이적)
coordinator 격리 수정. **fixpoint까지 반복**: 각 dead 심볼 제거 후 whole-scope grep으로 새로 orphan된
심볼(occurrence==1 또는 정의만 남고 caller 0)을 재탐색해 함께 제거. 한 번에 끝내 iter-4를 최소화한다.
단, 공개 계약(received_t 공개 접근자 등)이 실제 미사용인지 반드시 재확인 후 제거(과제거로 빌드/계약 파손
금지). 라이브러리+15 samples 컴파일+링크 green, no-hit ZERO 유지. 완료 후 iteration-4.

주: iteration-4는 4회차이므로 이후 규칙상 blocker/high/medium 0이면 low는 CLEAN을 막지 않는다. 그러나
이번 dead-code는 전량 제거가 목표(전이적으로 깨끗이).
