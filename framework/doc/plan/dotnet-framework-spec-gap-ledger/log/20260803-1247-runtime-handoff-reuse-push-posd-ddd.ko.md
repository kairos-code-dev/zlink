# .NET handoff state 재사용과 bound-session push trace 수정 log

## 현재 판정

이 실행에서 runtime source gap 두 가지를 수정하고 회귀 검증했다. 이전 relocation의
`MessageFollow` 상태가 같은 `ZLinkActorRuntimeState`의 다음 target activation에 남는 문제를
handoff aggregate 내부 전이로 정리했다. 이어 actor send부터 session-node local write까지의
remote push 경계를 debug trace로 기록했다. `ZW-B1`은 수정 후 실제 process E2E에서 exit `0`으로
통과했다. 전체 `.NET Runtime` filter도 `747/747`로 종료했지만, 그 실행의 negative startup
검증이 `errno 93` host log를 남겼으므로 정상 test 결과와 그 log를 구분해 기록한다.

## 관찰된 실패와 원인

`ZW-B1`의 첫 번째 재현에서는 같은 actor가 node를 왕복한 뒤 target node에서 다음 trace가
반복되었다.

```text
actor_frame_route ... route=Stale frame_node=<current-node> current_node=<current-node>
message_follow_rejected
```

원인은 이전 relocation에서 source state에 남겨 둔 `_staleSourceActor`와 만료되는
`_messageFollowRoute`가 새 target actor에 재사용된 것이었다. target `Import`는 새 replay
queue를 설치하지만 이전 source phase를 자동으로 소유권 전환하지 않았다. 따라서 동일한
node와 generation을 가진 새 actor frame도 `Current`가 아니라 `Stale`로 판정될 수 있었다.

## 선택한 수정

- `ZLinkActorHandoffState.PrepareForTransferredActivation`을 추가했다. 이 전이는 이전 source의
  `Retired`/`Idle` 상태만 정리하고 `_staleSourceActor`, message-follow lease, source admission과
  source hold frame만 해제한다. 이미 `Import`한 target handoff id와 target replay frame은
  삭제하지 않는다.
- `ZLinkActorRuntimeState.PrepareForTransferredActivation`이 위 전이를 호출한 뒤 native actor
  ref와 dispatch activation을 준비한다. target caller가 handoff 내부 field를 직접 조작하지
  않으므로 상태 소유권이 `ZLinkActorHandoffState` 안에 남는다.
- `ZLinkActorBoundSessionCoordinator`와 runtime actor host에 debug-only `SpotDiscovery` trace를
  추가했다. actor bound-session send, remote relay submit, session-node delivery 결과와 write
  여부를 file log에서 연결할 수 있다. public API, sample DTO, raw frame 우회는 추가하지 않았다.

## POSD·DDD 재검토

### PDD-DOTNET-010 — source tombstone과 target replay의 lifecycle 경계

- 원칙: POSD information hiding·deep module, DDD Actor relocation aggregate의 source/target
  phase invariant. target activation은 이전 source tombstone을 정리해야 하지만 target import
  queue의 소유권을 잃으면 안 된다.
- 대안 A: `PrepareForTransferredActivation`에서 handoff 전체 `Reset()`을 호출한다. target
  `Import` 직후 실행되므로 새 replay queue까지 삭제한다. 기각했다.
- 대안 B: `ZLinkActorRuntimeState` 또는 host caller가 `_staleSourceActor`, message-follow route,
  admission field를 각각 직접 지운다. aggregate invariant가 여러 caller에 누출되고 다음
  relocation 경로가 같은 정리를 빠뜨릴 수 있다. 기각했다.
- 선택: handoff aggregate 내부에 source-only cleanup 전이를 두고 target projection은 보존했다.
  `ActorHandoffTests.TransferredActivation_ClearsRetiredSourceFollow_ButKeepsImportedTarget`
  가 같은 source actor ref를 새 current actor로 사용할 때 `Current` 판정과 imported replay
  보존을 함께 확인한다.

### PDD-DOTNET-011 — remote session push의 관찰성 책임

- 원칙: POSD complexity below와 information hiding. sample handler가 transport detail을
  출력하면 sample이 Framework 내부 책임을 떠안고, 반대로 relay submit만 기록하면 session
  node write 실패 원인을 알 수 없다.
- 대안 A: ZoneWorld의 `PlayerZoneChangedDeliveryHandler`에 relay별 trace를 추가한다. 특정
  sample만 내부 transport를 알게 되므로 기각했다.
- 대안 B: 실패 시 exception만 남긴다. one-way push는 호출자에게 반환되지 않아 relay submit,
  session binding validation, local stream write 중 어느 단계에서 사라졌는지 구분할 수 없다.
  기각했다.
- 선택: Framework의 bound-session coordinator와 runtime relay 경계에 환경 변수로 켜는
  `SpotDiscovery` trace를 둔다. public contract와 sample flow는 그대로 유지하고, actor·source
  node·target node·write 결과를 같은 실행의 file log에서 확인한다.

## Unit evidence

```text
dotnet test .../Zlink.Framework.UnitTests.csproj --no-restore --nologo \
  --filter 'FullyQualifiedName~ActorHandoffTests|FullyQualifiedName~SessionActorCoordinatorTests|FullyQualifiedName~EntrySpotActorDispatchTests|FullyQualifiedName~CanonicalActorAcceptedJournalTests|FullyQualifiedName~ActorBoundSessionRelayTests'
```

결과: `249/249` passed. 이 묶음에는 새 handoff 재사용 회귀와 disconnect/staged route 회귀가
포함된다.

```text
dotnet test .../Zlink.Framework.UnitTests.csproj --no-restore --nologo \
  --filter 'FullyQualifiedName~Runtime'
```

결과: `747/747` passed, exit `0`, duration `2m17s`. 실행 중 negative startup 검증이
`ZlinkBindException`, `errno 93` stack trace를 log level `fail`로 출력했지만 testhost가
중단되지 않았고 전체 test result는 passed다. 이 log는 broad test green과 별도의 환경/negative
path evidence로 남긴다.

Sample source regression:

```text
dotnet test .../Zlink.Framework.SampleRegressionTests.csproj --no-restore --nologo
```

결과: `141/141` passed.

## Process trace와 file log evidence

실행 명령:

```bash
ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 \
ZLINK_DEBUG_FRAMEWORK_STARTUP=1 \
ZLINK_DEBUG_FRAMEWORK_TASKS=1 \
ZLINK_SAMPLE_EVIDENCE_DIR=/tmp/zlink-dotnet-e2e-20260803-b1-pushtrace-vuApvX \
bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh 'ZW-B1'
```

결과:

```text
scenario ZW-B1 checkpoint=remote-players-positioned
scenario ZW-B1 checkpoint=west-in-border-band
scenario ZW-B1 checkpoint=east-observed-west
scenario ZW-B1 checkpoint=diagonal-exclusion-observed
scenario ZW-B1 passed
zoneworld-batch=passed scenarios=ZW-B1
EXIT_STATUS=0
```

file log는 `/tmp/zlink-dotnet-e2e-20260803-b1-pushtrace-vuApvX/ZoneWorld/logs/`에 있다.
`zone-node-2.log`에서 `source_handoff_state_cleared_for_target_activation`과 target
`handoff_final_replay_dispatched`를 확인했고, `gateway.log`에서 `session_push_deliver ...
written=True`와 `session_push_delivery_result ... result=Delivered`를 확인했다. client log는
위 네 checkpoint와 pass를 직접 기록한다. 실행 후 `ZoneWorld`, runner, Redis 이름을 가진 잔류
process/container가 없는 것도 확인했다.

## 남은 gate

이 log는 runtime source/unit과 `ZW-B1` process 증거만 닫는다. ZoneWorld 전체 aggregate의
restart/browser scenario, 나머지 6종 process E2E, raw wire capture, fresh `Systems.Zlink`
package와 clean Framework consumer, package-only process, Windows PowerShell/browser 실행과
독립 final audit은 아직 완료로 표시하지 않는다.
