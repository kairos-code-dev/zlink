# .NET disconnect relay·staged relocation runtime 수정과 POSD·DDD 점검

## 현재 판정

handoff 중 Session disconnect가 accepted Actor frame 계약을 만족하지 못하거나, target authority가
이미 준비된 상태에서 target projection을 보지 못해 disconnect를 버리는 두 runtime gap을 Framework
owner에서 수정했다. 변경은 sample workaround나 public API 추가가 아니다. 영향을 받은 unit 묶음은
현재 통과했지만, 최신 변경을 포함한 전체 `FullyQualifiedName~Runtime` 분모와 7종 전체 process E2E는
아직 남아 있으므로 Phase A와 통합 ledger는 완료로 판정하지 않는다.

## 관찰된 실패와 원인

첫 번째 실패는 relocation 중 Session disconnect가 handoff replay에 들어갈 때 발생했다. 이전
구현은 relay를 만들면서 `ZLinkBackendActorRouteContext`의 operation을 기본값으로 두었다. 그러나
`ZLinkActorHandoffState`는 accepted frame을 캡처할 때 operation·reply·authority·lease fence를
검사하므로 다음 오류가 발생했다.

```text
An accepted Actor frame requires exact operation, reply and authority fences.
```

두 번째 실패는 target의 relocation route가 authority 단계까지 준비되었지만 Session owner route가
아직 최종 commit되지 않은 순간에 발생했다. `TryValidateDisconnectedBinding`이 committed binding만
조회하여 target의 정확한 staged binding을 찾지 못했고, 다음 메시지를 기록한 뒤 disconnect를
버렸다.

```text
session disconnect did not match the current binding
```

첫 번째 재현은 `/tmp/zlink-dotnet-e2e-20260803-8-U1v21A/ZoneWorld/logs/zone-node-2.log:189`,
두 번째 재현은 같은 실행의 `zone-node-1.log:409`에 보존되어 있다.

## 선택한 수정

- `ZLinkActorBoundSessionCoordinator.NotifyRemoteDisconnectedAsync`가 relay를 준비하기 전에 현재
  MeshNode에서 operation을 할당한다. 따라서 handoff 캡처와 이후 forwarding이 같은 operation
  fence를 사용한다. trace에는 `disconnect_route_prepared`를 actor, operation, target/source node와
  함께 남긴다.
- `ZLinkActorBoundSessionRelay.TryValidateDisconnectedBinding`은 target이 inbound authority를
  이미 commit한 경우 `TryGetBoundSessionForInbound`가 제공하는 target projection을 조회한다.
  Session owner의 최종 route commit을 target actor replay보다 앞당기지 않고도 정확한 binding을
  검증할 수 있다.
- `ZLinkActorRuntimeState.UnbindSession`은 disconnect가 exact binding token을 제거한 경우 같은
  token의 pending route도 취소한다. 이미 제거된 binding을 나중에 stale retry가 다시 commit하지
  않도록 pending state의 owner를 runtime state로 고정했다.

## POSD·DDD 재검토

### PDD-DOTNET-008 — accepted frame fence의 생성 책임

- 원칙: POSD information hiding·deep module, DDD aggregate transition invariant. accepted Actor
  frame에 필요한 operation fence는 caller나 sample이 정하는 값이 아니라 MeshNode runtime이
  operation lifecycle과 함께 발급해야 한다.
- 대안 A: sample 또는 relay caller가 임의의 non-zero operation을 생성한다. operation allocator와
  reply route의 책임이 분리되고, 다른 handoff path가 다른 fence를 사용할 위험이 있으므로
  기각했다.
- 대안 B: handoff capture 시 기본 operation을 허용하거나 frame validator를 완화한다. 계약 검사를
  약화해 잘못된 replay를 통과시키므로 기각했다.
- 선택: 기존 MeshNode operation allocator를 coordinator가 호출하고, route context와 relay frame에
  그 결과를 전달한다. `EntrySpotActorDispatchTests.Remote_Disconnect_Relay_Allocates_An_Operation_Fence`
  가 operation·reply·authority·lease fence를 직접 역직렬화해 검증한다.

### PDD-DOTNET-009 — staged inbound binding과 pending route의 소유권

- 원칙: DDD handoff aggregate의 authority 단계와 Session route commit을 한 상태 전이로 섞지
  않는다. POSD의 temporal decomposition 위험을 피하기 위해 target projection과 pending route를
  runtime state가 함께 관리한다.
- 대안 A: target Session route를 먼저 committed 상태로 승격한 뒤 disconnect replay를 처리한다.
  아직 source cleanup과 target route가 모두 durable하지 않은 순간을 committed public state로
  노출하므로 기각했다.
- 대안 B: disconnect를 무시하고 pending route retry가 정리할 때까지 기다린다. disconnect와
  binding token을 잃어 stale route가 남을 수 있으므로 기각했다.
- 선택: inbound authority가 준비된 동안에는 정확한 staged target projection으로 binding을
  검증하고, exact token unbind가 pending route를 함께 취소한다. `ActorHandoffTests.Pending_Relocation_Disconnect_Uses_The_Target_Projection_And_Cancels_It`
  가 이 전이를 고정한다.

## Unit evidence

다음 fresh 실행은 최신 runtime 변경을 포함한다.

| 테스트 묶음 | 결과 |
|---|---:|
| disconnect relay·staged route focused | `3/3` |
| `CanonicalActorAcceptedJournalTests` | `7/7` |
| `ActorHandoffTests` | `66/66` |
| `SessionActorCoordinatorTests` | `36/36` |
| `EntrySpotActorDispatchTests` | `128/128` |

전체 Runtime filter는 이전 snapshot의 `744/744` 뒤에 이 변경이 추가되었고, 과거 broad 실행에서
testhost가 `ZlinkBindException`, `errno 93`로 중단된 적이 있다. 따라서 최신 전체 분모는 bind
isolation을 정리한 뒤 별도 실행한다.

## Process trace·file log evidence

다음 명령으로 ZoneWorld `ZW-B2,ZW-B5`를 실행했다.

```bash
ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 \
ZLINK_DEBUG_FRAMEWORK_STARTUP=1 \
ZLINK_DEBUG_FRAMEWORK_TASKS=1 \
ZLINK_SAMPLE_EVIDENCE_DIR=/tmp/zlink-dotnet-e2e-20260803-9-BTLP3j \
bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh 'ZW-B2,ZW-B5'
```

결과는 exit `0`, `scenario ZW-B2 passed`, `scenario ZW-B5 passed`,
`zoneworld-batch=passed scenarios=ZW-B2,ZW-B5`다. 보존된 file log는
`/tmp/zlink-dotnet-e2e-20260803-9-BTLP3j/ZoneWorld/logs/`에 있다. `gateway.log`에서
`disconnect_route_prepared`가 non-default operation과 target/source node를 포함하고,
`zone-node-2.log`에서 `handoff_final_replay_dispatched`와 message follow trace를 확인했다.
이 실행의 client self-check와 runner cleanup은 통과했지만, 이는 ZoneWorld 전체 scenario 또는
7종 aggregate 완료를 의미하지 않는다.

## 남은 gate

최신 전체 Runtime 분모, 7종 source process E2E, raw wire capture, fresh `Systems.Zlink` package와
clean Framework consumer, package-only process, Windows PowerShell/browser 실행, 그리고 독립 final
audit을 순서대로 남겨 둔다.
