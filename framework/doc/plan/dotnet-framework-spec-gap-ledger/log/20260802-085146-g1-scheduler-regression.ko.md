# G1 scheduler regression 진행 log

## 현재 조건

G1 relocation card는 production source와 aggregate-level regression을 반영했지만, 실제
`ZLinkSpotRetireScheduler.TryRelocateAsync` 실행 경로의 regression 증거가 부족하여 Sol Medium
review가 아직 `NOT CLEAN`이다. 이 log는 그 마지막 조건을 닫기 위한 작업 상태를 기록한다.

Formal public contract spec과 exact-interface 문서는 이 작업에서 수정하지 않는다. 변경 대상은
implementation, regression test, plan ledger와 이 log다.

## 확인한 결과

- G0: Sol Medium read-only review `CLEAN`.
- DN-IMP-017: targeted test와 full unit test 증거를 포함해 `CLEAN`.
- G1 aggregate/host targeted test: `42/42 PASS`.
- Full `.NET` UnitTests의 최근 실행: `1397/1398`; `RemoteUserSpotTerminalReplaysAfterDeadlineAndExpiresWithoutReexecution` 1건이 suite에서 실패했다. 같은 test를 단독 실행하면 `1/1 PASS`여서 suite flake 여부를 다시 확인해야 한다.
- 최신 Sol Medium review: `Critical 0`, `High 0`, `Medium 1`, `NOT CLEAN`.
- 남은 review 조건: fake target을 사용하는 실제 scheduler-level test로 Stage ACK 불확실성, durable abort, reconciliation unknown과 commit 결과를 각각 확인한다.

## 다음 실행

1. `ZLinkSpotNodeCatalog`에서 실제 scheduler를 호출하는 regression test를 추가한다.
2. targeted test, full unit test와 `git diff --check`를 다시 실행한다.
3. 같은 candidate를 바꾸지 않은 상태에서 Sol Medium read-only review를 재요청한다.
4. Sol `CLEAN`과 필수 test 통과 뒤 G1을 완료하고, 이 ledger의 최종 audit를 끝낸다.
5. 이 문서가 완료되면 같은 폴더의
   통합 ledger의 `## 10. Phase B — common sample gap`을 다음 작업 구간으로 연다.

## 작업 조건

현재 branch는 `agent/framework-contract-runtime-update`이며, working tree에는 사용자가 이미
수정한 여러 언어·문서·E2E 변경이 있다. 해당 변경을 되돌리거나 분리하지 않고 보존한다. 전체
commit/push는 현재 ledger의 review gate와 최종 검증이 끝난 뒤 진행한다.
