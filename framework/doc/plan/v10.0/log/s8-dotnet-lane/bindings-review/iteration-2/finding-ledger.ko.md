# S8 DOTNET bindings 리뷰 iteration-2 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-2(snapshot `115c3d73d`, 208파일 `d6acf3e4`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. **iteration-1 finding(DF1-DF8, DI2-1/2, DI3-1, metadata)은 두 리뷰어
모두 전량 RESOLVED 확인.** 신규는 전부 raw socket-layer ABI 드리프트(`log/s8-common-raw-layer-drift.ko.md`).

## I1 계약 구현 일치

### D2F1. router_recv_part/_nowait 파라미터 수 불일치 [blocker]
Core `zlink_router_recv_part`는 6파라미터(router, source_node_rid_out, request_seq_out, part_out,
has_more_out, flags — `core/include/zlink/socket/api.h:272-277`). dotnet P/Invoke는 7파라미터
(`NativeMethods.Socket.cs:58-61,66-69`): 사이에 `out IntPtr sourceSpotRoutingId`(10.0.0에서 제거된
spot_rid) 잔존. SysV 레지스터가 밀려 native가 64바이트 `zlink_msg_t`를 8바이트 슬롯에 기록 →
ROUTER recv마다 스택 손상. (cpp·node lane은 이미 수정.) → 여분 `sourceSpotRoutingId` 제거,
recv 경로 caller 정합.

### D2F2. IStreamSocket.DetachStream → 제거 심볼 [blocker]
`IStreamSocket.DetachStream()`(`IStreamSocket.cs:44`→`StreamSocket.cs:38`→
`SocketKernel.Stream.cs:136`)가 `zlink_stream_detach`에 배선. Core 10.0.0 미export(`libzlink.vers`·
`nm -D` 부재, alias 없음) → `EntryPointNotFoundException`. `Dispose`(`SocketKernel.Lifecycle.cs:25`)도
stream 소켓 teardown마다 발화(빈 catch로 삼킴). 3개 stream 샘플이 `OnPacket`으로 `_streamAttached`
설정. → Core 10.0.0 raw STREAM API 기준으로 detach 경로 정렬(제거/재매핑). Core stream 콜백 모델
확인 후 정합.

### D2F3. zlink_msg_refcnt 파라미터 누락 [high]
`NativeMethods.Core.cs:288` `zlink_msg_refcnt`가 Core 필수 `error_out_` 파라미터 누락. 공개
`Message.RefCount`로 도달 → 미설정 레지스터 통한 UB write. → Core 시그니처에 맞춰 error_out 추가.

## I3 정리 완결성

### D2I3-1. 제거 심볼 dead P/Invoke [high]
`zlink_stream_attach_raw`(`AttachStreamRaw` ×2), `zlink_subscribe_handler`
(`SocketKernel.SubscribeHandler` + XPUB subscribe-notification capability chain) — Core 10.0.0 미export.
공개 caller는 없으나 존재하는 dead 참조. `zlink_subscribe_handler`의 10.0.0 대체는 `zlink_xpub_recv_part`
모델. → 제거(및 지원 delegate/handler chain). `zlink_poller_wait_pinned`는 **결함 아님**
(`EntryPoint="zlink_poller_wait"` alias, 정상).

## 처리 방침
coordinator 격리 수정. Core socket/api.h·stream·msg 시그니처 정합, dead 제거 심볼 정리. csproj+
samples 빌드 green, no-hit 유지, RequiredExportNames⊆ABI 유지. Core 10.0.0의 raw STREAM 콜백/detach
모델을 확인해 D2F2를 정확히 매핑(추측 금지). 완료 후 iteration-3.
