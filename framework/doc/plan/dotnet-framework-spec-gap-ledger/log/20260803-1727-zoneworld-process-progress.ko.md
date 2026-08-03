# ZoneWorld process progress (2026-08-03 17:27 KST)

## 판정

현재 Phase B sample process gate는 완료되지 않았다. 전체 `ZoneWorld` runner는
`ZW-B4`와 `ZW-C2`를 통과했지만, `ZW-C2` 뒤의 `ZW-C3` 준비 단계에서 새
`zone-node-2`가 `topology=ready`를 기록하기 전에 종료되었다. 따라서 이후 `ZW-C3`,
`ZW-E5`, `ZW-D1`, `ZW-D2`, `ZW-F2`, `ZW-G3`와 최종 phase marker는 이 실행의
완료 evidence로 사용할 수 없다.

## 전체 실행

명령:

```text
ZLINK_SAMPLE_TRACE_STREAM=1 ZLINK_SAMPLE_EVIDENCE_DIR=/tmp/zlink-sample-evidence-zoneworld-all3-20260803 \
bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh all
```

- exit: `1`
- evidence: `/tmp/zlink-sample-evidence-zoneworld-all3-20260803/ZoneWorld/`
- 통과한 runner-driven 시나리오: `ZW-B4`, `ZW-C2`
- 전체 client batch와 독립 runner 판정: `ZW-G4`, `ZW-G1`, `ZW-G2-rid`, `ZW-G5`,
  `ZW-A1`~`ZW-A5`, `ZW-B1`~`ZW-B3`, `ZW-B5`, `ZW-C1`, `ZW-C4`, `ZW-D1` client half,
  `ZW-E1`~`ZW-E4`, `ZW-E6`, `ZW-F1`, `ZW-F3`, `ZW-F4`
- 실패 시점: C2 이후 재시작한 `zone-node-2`의 `bot-ne-y` remote actor creation
- server log: `TaskCanceledException`이 `ZLinkActorManagerService.SubmitAsync`의
  remote creation 대기에서 발생했고 process가 `Aborted`로 종료되었다.
- trace 관찰: 새 node는 새 routing ID로 discovery snapshot과 Ops·Gateway admission을
  기록했지만, `bot-ne-y`의 `actor_create_remote` 뒤 target actor creation 완료가
  기록되지 않았다.

## 단독 비교 실행

명령:

```text
ZLINK_SAMPLE_TRACE_STREAM=1 ZLINK_SAMPLE_EVIDENCE_DIR=/tmp/zlink-sample-evidence-zoneworld-c3-focused-20260803-1 \
bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh ZW-C3
```

- exit: `0`
- evidence: `/tmp/zlink-sample-evidence-zoneworld-c3-focused-20260803-1/ZoneWorld/`
- `ZW-C3` client self-check: passed
- 재시작 node의 `topology=ready`, `bot-ne-y` remote `actor_create_prepare_start`와
  `actor_create_complete_done`, status report 제출: 모두 확인

단독 실행이 통과하고 전체 실행만 실패하므로 fixed client delay나 sample assertion 완화로
처리하지 않는다. 전체 실행에서 세 번의 node lifecycle 이후 stale discovery/actor-route
상태가 새 remote creation에 미치는 영향을 Framework runtime 경계에서 재현할 대상 gap으로
남긴다. 다음 수정은 message trace와 각 process file log로 source·target의 creation
request/response, owner liveness와 route generation을 함께 확인한 뒤 owning layer에
regression test를 추가하는 순서로 진행한다.

## 기존 gate와의 관계

`ActorHandoffTests`는 최신 handoff source binding 재사용 회귀를 포함해 `71/71`,
`ZoneWorldOpsConsoleRegistryTests`는 새 routing ID correlation을 포함해 `5/5`로
통과했다. 이 결과는 해당 runtime 단위와 Ops correlation 수정의 regression evidence이며,
전체 sample process 완료나 7종 aggregate 완료를 의미하지 않는다. `ZW-B6`는 지원되는
exact-route operational harness가 없어 runner에서 계속 withheld 상태다.
