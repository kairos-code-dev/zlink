# ObservabilityOps C6 최신 process 검증 checkpoint

## 현재 판정

2026-08-03 20:23 KST의 `OBS-C6` 실행은 완료로 판정하지 않는다. runtime trace와 target
process file log에서는 `room`과 `instance` relocation aggregate가 모두 stage를 끝내고
`committed=2`가 되었으며, target process에도 두 aggregate의 completion gate가 기록되었다.
그러나 client가 relocation 직후 조회한 Instance Spot evidence에서 target node 배치를 확인하지
못해 scenario assertion이 실패했다. 따라서 이 실행은 runtime relocation 단계의 진전 증거이지,
`OBS-C6` process E2E 통과 증거가 아니다.

## 실행 범위와 결과

실행 directory는 다음과 같다.

```text
framework/languages/dotnet/e2e/ObservabilityOps/logs/20260803-202343-3856873/
```

보존된 결과는 다음과 같다.

- `phase1-play-a.stderr.log`에 `room`과 `instance`의 `relocation_begin`,
  `relocation_reserved`, `relocation_sealed`, `relocation_stage_begin`과
  `relocation_stage_end`가 기록되었다.
- 같은 file log에 `relocation_phase_aggregates completed=True committed=2`와
  `admission_sealed site=relocate_completed`가 기록되었다.
- `phase1-play-b.stderr.log`에는 두 aggregate의
  `aggregate_target_complete_gate`가 기록되었다.
- `flow-play-a.log`, `flow-play-b.log`, `flow-session-a.log`에는 relocation 중 Session
  route seal·commit과 client message trace가 남아 있다.
- client 결과는 `OBS-C6 Instance Spot did not move to application version 2.` assertion에서
  중단되었다.

이 결과는 다음 두 조건을 분리한다.

1. relocation runtime이 source admission seal, target stage와 aggregate commit까지 진행되는지:
   최신 실행에서 trace와 server file log로 확인했다.
2. client-visible Instance Spot evidence가 relocation 완료 직후 target node를 반환하는지:
   확인하지 못했다. evidence 조회 시점의 eventual consistency인지, target projection 저장 문제인지
   아직 결정하지 않았다.

## 관련 unit regression

relocation 중 stale session binding과 route seal 전 frame admission을 public
`Unavailable`로 전달하고, session error wire code가 Framework error kind를 보존하도록 수정한
경로는 다음 focused test로 확인했다.

```bash
dotnet test \
  framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj \
  --no-restore \
  --filter 'FullyQualifiedName~Framework_error_wire_code_preserves_the_public_error_kind|FullyQualifiedName~Rebind_Fences_Stale_Relay_And_Late_Disconnect_Without_Affecting_Other_Actors|FullyQualifiedName~Sealed_Route_Reports_Unavailable_Before_Frame_Admission|FullyQualifiedName~Ingress_Seal_Waits_For_The_Accepted_Frame_To_Terminate|FullyQualifiedName~Completed_Route_Commit_Is_Exactly_Fenced_And_Idempotent' \
  --maxcpucount:1 \
  --verbosity minimal
```

결과는 `5/5`, exit `0`이다. 이 결과는 source/unit regression evidence이며, C6 process
assertion과 Phase A·B 완료 gate를 대신하지 않는다.

## 남은 조건

- C6 client가 Instance Spot target evidence를 bounded wait로 확인해야 한다. wait를 추가할 때는
  stale 결과를 성공으로 바꾸지 않고, target node·generation·application version을 모두 확인한다.
- target process file log에서 Instance Spot 초기화와 location projection이 어떤 순서로 기록되는지
  evidence endpoint 응답과 함께 대조해야 한다.
- 수정 후 `OBS-C6`를 message trace와 role별 file log를 보존한 상태로 재실행한다.
- `OBS-C6`가 통과하기 전에는 ObservabilityOps 전체 aggregate, sample 7종 process gate,
  Phase A/B 독립 final audit을 완료로 표시하지 않는다.
