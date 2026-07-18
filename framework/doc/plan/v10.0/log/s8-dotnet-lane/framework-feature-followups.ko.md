# S8-DN framework — compile-green 후 기능 업그레이드 follow-up (S8-04A/06A/lifecycle)

dotnet framework compile-green 완료(`902bf9368`, Zlink.Framework 0 error). 아래는 compile-preserving
stub로 남긴 런타임 동작 항목 — E2E 통과·DOTNET REVIEW CLEAN 전에 실제 구현 필요.

- **S8-04A actor transfer**: pump의 `TransferControl` 분기 no-op, `PrepareActorTransfer/Commit/Activate/
  Abort`가 transfer 상태기계에 미배선. framework handoff 상태기계 유지 위에 native transfer + Redis
  authority(participant-set CAS·transfer token·lease·prepared/commit/abort crash recovery) 구현.
- **S8-06A Streams**: bind/unbind가 `MeshOperationId` completion await 안 함; metadata/relay-allowlist
  규칙 미적용; 각 stream node가 자체 `IMeshNode`를 mint(단일 노드 session service로 정렬 필요).
- **bound-session straggler relay**: `ReplyActorNoBind`/`BindRemoteActorBoundSession` no-op,
  `ForwardActorBoundSessionPart`는 `SendBoundSession` best-effort(10.0.0 fine-grained target 없음).
- **node lifecycle/channels**: `node.Start()` lazy(첫 spot 사용 시); `AddChannel`/`SetChannelWeight`
  sequencing·pub/sub-rid/role-config 옵션 미threaded.
- **ROUTER SendToSpot/RequestToSpot**: 현재 NotSupported/ZLinkConfigurationException throw(cross-node
  spot 주소는 MeshNode spot plane으로 이동). spec 대조로 정당성 확인 필요.

처리: 기능 업그레이드 → samples(S8-09)/E2E(S8-10) → DOTNET REVIEW CLEAN(S8-13/14/15). cpp/jvm/node
framework 미러의 참조.
