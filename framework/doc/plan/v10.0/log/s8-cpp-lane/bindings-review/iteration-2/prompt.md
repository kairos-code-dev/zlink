# RouteMesh 10.0.0 S8 CPP bindings 전환 리뷰 — iteration 2 공통 prompt

너는 S8 CPP bindings 전환 리뷰 iteration 2의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `de299e184` (iteration-1 finding 수정 반영)
- Scope: 검토 checkout에서 `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples bindings/cpp/CMakeLists.txt` 중 `native/` 제외
- Scope 파일 수: 123 (iteration-1의 129에서 구 v9 vendor 헤더 6개 삭제로 감소)
- Scope aggregate SHA-256 (각 파일 sha256sum을 `LC_ALL=C sort` 후 다시 sha256sum): `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 대상과 목적

C++ bindings를 zlink Core 10.0.0 공개 C API(`core/include/zlink/service/*.h`, `core/include/zlink/socket/api.h`)로 전환한 결과를 검토한다. Runtime raw-socket 레이어는 존속한다. iteration-1에서 두 리뷰어가 NOT CLEAN을 냈고, coordinator가 병합 finding-ledger(`../iteration-1/finding-ledger.ko.md`)를 수정했다.

## 우선 검증: iteration-1 finding 해소 여부

`../iteration-1/finding-ledger.ko.md`와 두 리뷰(`../iteration-1/{codex,claude-sonnet}/review.ko.md`)를 읽고, 수정 commit `de299e184`를 소스에서 대조해 아래가 실제로 해소됐는지 판정하라:
- F1 service parts borrowed(caller ownership 유지) adapter, F2 claim/reply-token 수명(실제 release까지), F3 full-batch drain, F4 typed kind_data, F5 metadata+publish_detail, F6 actor transfer fence API, F7 options/peer_channels, F8 sample routing_id, F9 ready-handler 동시성·예외 경계, F10 close-result/swap move, F11 actor-id 검증, I2-1 dispatch-turn, I2-2 state 분리, I3-1 구 v9 헤더·dead spot_node 제거.
- 이전에 해소 판정된 finding은 새 반례 없이 다시 열지 마라. 같은 근본 원인은 root-cause family로 묶어라.

## 이후: 전체 scope 재검토 (3축)

- **I1 계약 구현 일치**: Core C API와 표면의 계약 일치. 매핑 정확성, 인자·수명·오류·동시성, pull dispatch(claim/batch/reply-token RAII·turn 소유), metadata·transfer·caller-ownership, 필수 pre-start 설정 노출.
- **I2 POSD·DDD**: 깊은 모듈·정보 은닉·복잡성 하향, 책임 경계, 얕은 wrapper·누출·중복.
- **I3 정리 완결성**: 폐기 개념 잔재(공개 계약·구현·samples·build·주석), 죽은 code·정의 없는 선언·호환 잔재. scoped grep 근거.

## 절차 규칙

- 시간 제한 없음. 산출물은 자신의 review 디렉터리(`codex/` 또는 `claude-sonnet/`)의 `progress.md`·`review.ko.md` 두 문서뿐. **build·테스트·sanitizer·package 실행 금지.** 실행 증거는 manifest의 coordinator 결과(라이브러리+15 samples compile+link green, no-hit ZERO)만 사용. 판정은 소스 정적 대조.
- iteration 2는 4회차 미만이다: 각 축의 `CLEAN`은 해당 축 finding 0건.

## 출력 계약

`review.ko.md`에 작성하고 같은 내용을 반환:
1. Scope 확인 (시작·종료 파일 수·hash)
2. iteration-1 finding 해소 판정 (F1~I3-1 각각)
3. I1 / I2 / I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안`, 없으면 "없음"), Evidence, Verdict
4. 폐기 개념 no-hit 판정 (SpotNode/spot_node/route_bridge/subjects/internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part/msg_gets)
5. 마지막 줄: 세 축 CLEAN이면 정확히 `BINDINGS REVIEW CLEAN`, 아니면 `BINDINGS REVIEW NOT CLEAN`

문체·취향은 finding 아님. 공개 계약·관찰 동작·concurrency·resource·build·검증 누락·폐기 잔재만.
