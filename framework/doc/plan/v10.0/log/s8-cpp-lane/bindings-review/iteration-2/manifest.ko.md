# S8 CPP bindings 전환 리뷰 manifest — iteration 2

## 1. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S8-CPP bindings / 2 |
| 대상 commit | `de299e184` (iteration-1 finding 수정 반영) |
| Scope | `git ls-files bindings/cpp/{include,src,samples,CMakeLists.txt}` 중 `native/` 제외 |
| Scope 파일 수 | 123 (iter-1의 129에서 구 v9 헤더 6개 삭제) |
| Scope aggregate SHA-256 | `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` |
| 공통 prompt | `prompt.md` |
| R1 / R2 | Codex / Claude Sonnet |
| 규칙 | 리뷰어 산출물은 progress.md·review.ko.md만. iteration 2=세 축 finding 0이어야 CLEAN |

## 2. Coordinator 실행 증거 (리뷰 전 확보)

- iteration-1 병합 finding-ledger(F1-F11, I2-1..3, I3-1) 전량 수정. `../iteration-1/finding-ledger.ko.md` 참조.
- `cmake --build`(라이브러리 `zlink_cpp` + samples ON, core build runtime) clean from-scratch: **rc=0, 0 error**. 15개 sample 실행파일 compile+link green.
- no-hit ZERO: SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/msg_gets/recv_actor_part 전량 0(`bindings/cpp/{include,src,samples}` minus native).

## 3. 종료 검증 목록 (두 clean 후 coordinator)

- clean 재빌드, 공개 API surface 대조, 로컬 package smoke(해당 시).

## 4. Session 기록

| 항목 | R1 Codex | R2 Claude Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
| 종료 상태 | 기록 예정 | 기록 예정 |
