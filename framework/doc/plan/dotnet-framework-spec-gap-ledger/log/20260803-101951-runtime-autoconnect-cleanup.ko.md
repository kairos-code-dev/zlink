# Runtime auto-connect cleanup 검증

## 범위

Owner lease가 만료되어 Location descriptor가 사라진 뒤에도 non-initiating side의
physical peer가 `Connecting` intent로 남아 RouteMesh 상태를 `Degraded`로 유지하던
runtime gap을 수정했다. 이미 `Admitted` 또는 `Draining`인 transport는 descriptor
부재만으로 닫지 않아야 하며, admission이 끝나지 않은 intent만 정리해야 한다.

## 수정 내용

- `IMeshNode.RemovePeerConnectionIfNotAdmitted(...)`를 추가해 peer state 확인과 intent
  제거를 한 lock 안에서 처리했다.
- `IZLinkBackendSpotNode`와 .NET backend wrapper에
  `DisconnectPeerBeforeAdmission(...)`을 연결했다.
- non-initiating auto-connect target은 discovery row가 사라졌을 때 admitted peer를
  즉시 제거하지 않는다. liveness가 `Connecting`으로 바뀐 다음 reconcile tick에서
  admission-pending intent만 제거한다.
- 다음 unit test를 추가했다.
  - `SpotPeerConnectorTests.Auto_NonInitiator_Delegates_Admission_Pending_Cleanup`
  - `ServiceRuntimeFoundationTests.AutoConnect_Cleans_Connecting_Peer_And_Preserves_Admitted_Peer`

## 검증 결과

```text
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --filter 'FullyQualifiedName~Runtime' --nologo
Passed 744, Failed 0, Skipped 0, Total 744

dotnet test ... --filter 'FullyQualifiedName~SpotPeerConnectorTests|FullyQualifiedName~ServiceRuntimeFoundationTests.AutoConnect' --nologo
Passed 3, Failed 0, Skipped 0, Total 3

ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 bash framework/languages/dotnet/e2e/SpotActorTransfer/run_e2e.sh ST-B2
operation SpotActorTransfer.ST-B2 passed
spot-actor-transfer e2e result=passed
```

Process evidence는
`languages/dotnet/e2e/SpotActorTransfer/logs/20260803-103152-737943/`(로컬 실행 evidence, gitignore 대상이라 저장소에는 없음)
에 있다. `actor-a.stdout.log`의 `JoinTargetReq` 송신과 `actor-b.stdout.log`의
`__zlink.actor.join_spot.admission`, `commit`, `handoff_completion` 수신은 같은
flow ID `019fc53f-d985-7865-9934-eb9233d6d84f`를 사용한다. `actor-b.stdout.log`의
`ProbeReq` 수신·reply도 같은 file log에 남아 있으며, `actor-a.evidence.log`와
`actor-b.evidence.log`는 payload hash, target restore, authority commit, Join completion과
post-recovery handler의 순서를 기록한다.

`actor-a.stderr.log`와 `actor-b.stderr.log`는
`ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1`로 남긴 discovery trace를 포함한다. 이 trace와
message trace를 file log로 대조해 application message가 source cleanup callback을
기다리지 않고 target에서 처리되는지 확인했다.

`git diff --check`도 통과했다. 최초 package verifier는 이전 public XML documentation
변경이 snapshot에 반영되지 않은 hash drift에서 중단됐고, `Zlink.Framework.package.txt`의
생성 hash를 갱신한 뒤 다음 결과가 통과했다.

```text
bash framework/languages/dotnet/scripts/verify_packaged_contract.sh
dotnet packaged contract result=passed
dotnet standalone http package result=passed
public_api_snapshot_sha256=399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a
```

Current source public surface도 다음 명령으로 다시 확인했다.

```text
dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-restore --nologo
Passed 76, Failed 0, Skipped 0, Total 76
```

Config 1~14 전체 aggregate와 독립 final audit은 아직 남아 있다.

이후 backend contract의 기본 cleanup 결과를 `false`로 바꾸어 미지원 구현이 성공으로
처리되지 않도록 fail-closed 경계를 보강했다. 이 최신 source 기준으로 Runtime filter는
다시 `744/744`, cleanup targeted test는 `3/3`으로 통과했다.
