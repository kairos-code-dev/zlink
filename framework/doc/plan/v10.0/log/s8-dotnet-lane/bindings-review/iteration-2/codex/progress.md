# S8 DOTNET bindings 리뷰 iteration-2 — R1 (Codex) progress

## Scope 확인
- 대상 commit: `115c3d73d` (HEAD `a9e6b521b`와 `bindings/dotnet/{src,samples}` diff 없음 → 대상 상태 동일)
- 시작 파일 수: 208
- 시작 aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `d6acf3e49cdb1f96aac8d92e6b403d79502f2f65866687581b0dc4308ad4c048` (manifest 일치)
- 종료 재확인: 208 / `d6acf3e49cdb...` (변경 없음, scope 파일 무수정)
- build/test/실행 없음. coordinator 실행 증거(csproj+samples green, no-hit 0, RequiredExportNames⊆ABI)만 인정.

## 절차 로그
1. prompt.md·manifest·iter-1 ledger·양 리뷰(codex/sonnet)·s8-common-raw-layer-drift.ko.md 정독.
2. Core 10.0.0 ABI 기준선: `core/src/libzlink.vers` + staged `bindings/dotnet/native/linux-x64/libzlink.so.10.0.0`(`nm -D --defined-only`) 읽기 전용 대조.
3. iter-1 finding(DF1-DF8, DI2-1/2, DI3-1) 소스 대조 해소 판정.
4. 전 P/Invoke entrypoint를 ABI와 자동 대조(python 스크립트) → missing-symbol P/Invoke 3건 확정.
5. 각 missing-symbol의 공개 도달성(public contract → kernel → P/Invoke) 추적.
6. no-hit 8종 재sweep, I2 god-file 분포 확인.

## iter-1 해소 판정 (전부 RESOLVED)
- DF1 RequiredExportNames: 182개, 전부 `libzlink.vers`에 존재(스크립트 missing=[]).
- DF2 typed join-reply: `ReplyJoin/AcceptJoin/RejectJoin`+`ActorJoinResult` enum, `zlink_actor_join_reply` 배선(MeshDispatch.cs:282,296).
- DF3 IMeshNode.SetRoutingId 추가(IMeshNode.cs:20).
- DF4 struct_size/version: caller-init 경로 초기화 확인.
- DF5 transfer API: `PrepareActorTransfer` 등 4함수+typed record(MeshNode.Actors.cs:228+).
- DF6 StreamSocket.SetRoutingId → native SetOption(StreamSocket.cs:20).
- DF7 finalizer: `~MeshReadyBatch/~MeshClaim/~MeshReceiveBatch` 존재.
- DF8 511 endpoint validator: `ValidateMeshEndpoint`(BoundaryValidation.cs:13=511).
- DI2-1 raw IStreamSocket actor bind/unbind/send/list 제거(현 IStreamSocket=Options/SetRoutingId/OnPacket/DetachStream/DisconnectRid만).
- DI2-2 typed kind_data: `DecodeKindData`(MeshDispatchRuntime.cs:307)가 KindData/KindDataSize 소비.
- DI3-1 제거 심볼/개념 no-hit: SpotNode·RouteBridge·spot_node·subjects·internal_sockets·pub/sub rid·dispatch_workers·recv_actor_part·msg_gets 전부 0. `IRouterSocket.*ToSpot`→ISpot 이관(present `zlink_spot_send_to_spot`).

## 신규 finding (raw 계층 드리프트, manifest §3 self-judge)
- Core 10.0.0에서 제거된 심볼에 literal 바인딩된 P/Invoke 3건: `zlink_stream_detach`, `zlink_stream_attach_raw`, `zlink_subscribe_handler` (ABI·so 모두 부재).
- `zlink_poller_wait_pinned`는 오탐 아님: `EntryPoint="zlink_poller_wait"`(present)로 alias → 정상.
- I1-1: public `IStreamSocket.DetachStream()`→`zlink_stream_detach`(제거) → 런타임 EntryPointNotFoundException. Dispose 경로도 호출(catch{}로 삼킴, 샘플 3종 OnPacket 사용으로 매 teardown 발화).
- I3-1: `zlink_stream_attach_raw`(+AttachStreamRaw 2 overload), `zlink_subscribe_handler`(+SubscribeHandler capability path) — 제거 심볼 dead P/Invoke·dead 내부 경로.

## Verdict
- I1 NOT CLEAN (1) / I2 CLEAN (0) / I3 NOT CLEAN (1)
- 최종: BINDINGS REVIEW NOT CLEAN
