# S8 CPP bindings 전환 리뷰 manifest — iteration 1

## 1. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S8-CPP bindings / 1 |
| 대상 commit | `2f34aacf2` (`s8-cpp(bindings): convert 15 samples to pull-dispatch + fix 2 library defects`) |
| Scope | `git ls-files bindings/cpp/{include,src,samples,CMakeLists.txt}` 중 `native/` 제외 |
| Scope 파일 수 | 129 (include 46, src 64, samples 18, CMakeLists 1) |
| Scope aggregate SHA-256 | `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023` (`LC_ALL=C sort` 고정) |
| 공통 prompt | `prompt.md` |
| R1 | Codex |
| R2 | Claude Sonnet |
| 규칙 | 리뷰어 산출물은 progress.md·review.ko.md 두 문서뿐, 실행 작업 전면 금지. iteration 1=세 축 finding 0이어야 CLEAN |

## 2. Coordinator 실행 증거 (리뷰 전 확보)

- `cmake --build`(라이브러리 `zlink_cpp` + samples ON, core build runtime): **rc=0, 0 error**. libzlink_cpp.a 생성, 15개 sample 실행파일 compile+link green.
- 폐기 개념 no-hit(Contracts/Service): SpotNode/spot_node/route_bridge/RouteBridge 각 0.
- 전환 중 발견·수정한 라이브러리 결함 2건(커밋 `2f34aacf2`): received_t::send/reply 정의 복원, mesh_node_t::set_routing_id 추가.

## 3. 종료 검증 목록 (두 clean 후 coordinator)

- 라이브러리+samples clean 재빌드, 공개 API surface 대조, 로컬 package(해당 시) smoke.

## 4. Session 기록

| 항목 | R1 Codex | R2 Claude Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
| 종료 상태 | 기록 예정 | 기록 예정 |
