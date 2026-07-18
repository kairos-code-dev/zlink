# S8 DOTNET bindings 전환 리뷰 — iteration 3 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `481221b24`
- 시작 시점: `git ls-files bindings/dotnet/src bindings/dotnet/samples | grep -vE 'native/|/obj/|/bin/'` →
  **208개** 파일, aggregate SHA-256 = `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d`
  (prompt 값과 일치)
- 종료 시점: 동일 명령 재실행 → **208개** 파일, 동일 hash(변경 없음), `git status`로 scope 파일
  무수정 확인
- 현재 `HEAD`(`6eeb596e1`, iteration-3 freeze 커밋)와 대상 commit `481221b24` 사이
  `bindings/dotnet/src`·`bindings/dotnet/samples` diff는 0줄(`481221b24`가 `HEAD`의 ancestor,
  `git diff 481221b24 HEAD -- ...`) — HEAD에서 검토해도 대상 commit과 완전히 동일한 내용.
- 검토 방법: iteration-1·iteration-2 finding-ledger 정독 → 각 finding을 소스 대조로 해소 판정 →
  Core 헤더(`core/include/zlink/**/*.h`, `core/include/zlink_errno.h`) 전량 + `core/src/libzlink.vers`
  (196개 export 정본) + 스테이징된 `core/build/lib/libzlink.so.10.0.0`의 `nm -D --defined-only` 결과를
  기준으로, dotnet **197개 P/Invoke 선언 전부**를 자동 추출해 Core 헤더 시그니처와 파라미터 개수를
  스크립트로 전수 대조(1회성 분석 스크립트, scope 파일은 미수정, 결과만 §3에 인용). build/실행은
  하지 않음. `nm -D`는 기존에 스테이징된 `.so`의 정적 심볼 테이블 조회이며 라이브러리 실행이 아니다.

## 2. iteration-1·iteration-2 finding 해소 판정

**전량 해소로 판정한다. 반례 없음.**

iteration-1(DF1-DF8, DI2-1/2, DI3-1)은 iteration-2의 두 리뷰어(opus, Claude Sonnet 본인)가 이미
소스 대조로 전량 RESOLVED 확인했고, iteration-2 이후 diff(`git diff --stat 115c3d73d 481221b24 --
bindings/dotnet/src bindings/dotnet/samples`, 14개 파일 -313줄)가 D2F1-D2F3/D2I3-1 관련 stream/
socket/msg 파일에만 한정돼 있어 iteration-1 finding을 재침해할 여지가 없음을 확인했다. 이번
iteration에서 iteration-1 범주도 새 반례 없이 재확인했다(§3의 전수 P/Invoke 대조가 DF1·DF5·DF6
관련 심볼도 포함).

| Finding | 판정 | 근거 |
|---|---|---|
| D2F1 `zlink_router_recv_part`/`_nowait` 파라미터 수 불일치 | **해소** | `NativeMethods.Socket.cs:57-67`가 `(router, out sourceNodeRoutingId, out requestSeq, ref part, out hasMore, int flags)` 6-파라미터로 Core `socket/api.h:271-277`(`router_, source_node_rid_out_, request_seq_out_, part_out_, has_more_out_, flags_`)와 1:1 일치. 여분 `sourceSpotRoutingId` 완전 제거 확인(`sourceSpotRoutingId` scope 전체 grep 0건). 호출부 `SocketKernel.Receive.cs:100-109`, `SocketKernel.ReceiveCore.cs:181-187`도 6-인자로 정합 |
| D2F2 `IStreamSocket.DetachStream()` → 제거 심볼 | **해소** | `stream_detach`\|`DetachStream` scope 전체 grep 0건. `IStreamSocket.cs`에 `DetachStream` 계약 자체가 삭제됨. `SocketKernel.Lifecycle.cs:19-27` `Dispose()`가 "Core 10.0.0 removed the raw STREAM detach entry point" 주석과 함께 managed 콜백 상태(`_streamAttached`/`ClearStream()`)만 정리하는 방식으로 재작성 |
| D2F3 `zlink_msg_refcnt` `error_out_` 파라미터 누락 | **해소** | `NativeMethods.Core.cs:287-289` `zlink_msg_refcnt(ref ZlinkMsg msg, out int errorOut)` — Core(`message/api.h:116`: `const zlink_msg_t *msg_, zlink_config_result_t *error_out_`) 2-파라미터와 일치. `zlink_config_result_t`(`zlink_errno.h:207-219`)는 unsigned 없는 plain C enum(최대값 709, 4바이트 `int`) → C# `int`로 정확히 대응. 호출부 `Message.Native.cs:57-59`가 `error`를 `ZlinkException.ThrowConfigIfError(error)`로 소비 |
| D2I3-1 제거 심볼 dead P/Invoke(`zlink_stream_attach_raw`, `zlink_subscribe_handler`) | **해소** | `stream_attach_raw`\|`AttachStreamRaw`, `subscribe_handler`\|`SubscribeHandler` scope 전체 grep 각 0건. 지원 delegate·콜백 레지스트리 필드·`SocketTypePolicy` capability 분기까지 전부 제거 확인(diff에서 `SocketKernel.Stream.cs` -169줄, `SocketKernel.Callbacks.cs` -72줄, `SocketCallbackRegistry.cs` -19줄, `SocketCallbacks.cs` -7줄, `SocketTypePolicy.cs` -3줄, `NativeMethods.cs` -6줄로 정확히 매칭) |

## 3. I1/I2/I3 Finding·Evidence·Verdict

### I1 — 계약 구현 일치 (P/Invoke⊆Core ABI, marshalling, pull-dispatch 수명, join-reply, metadata/transfer)

**방법**: `bindings/dotnet/src/Zlink/Runtime/Native/*.cs` 15개 파일에서 `DllImport`/`LibraryImport`
속성이 붙은 선언 197개 전부를 정규식 기반 스크립트로 추출(entry point명, 관리 함수명, 파라미터
개수, 파라미터 원문). Core 10개 헤더(`common.h`, `core/api.h`, `eventing/api.h`, `message/api.h`,
`service/actor.h`, `service/dispatch.h`, `service/mesh_node.h`, `service/spot.h`,
`service/stream_session.h`, `socket/api.h`)의 `ZLINK_EXPORT` 시그니처 196개 전부를 동일 방식으로
추출해 심볼명 기준 join, 파라미터 개수 자동 대조.

- **결과**: 186개 고유 P/Invoke 심볼 전부가 Core export 196개 중에 존재(불일치 0, `comm -23` 0줄).
  197개 P/Invoke 선언(오버로드 포함, 예: `zlink_msg_close`/`zlink_msg_size`/`zlink_poll`이 각 2회
  선언) 전부 대응 Core 시그니처와 파라미터 개수 정확히 일치(불일치 0).
- **RequiredExportNames**(로드 게이트, `NativeMethods.Core.cs:12-195`) 182개 전부 `core/src/libzlink.vers`
  export 196개(스테이징된 `libzlink.so.10.0.0`의 `nm -D --defined-only` 결과와 diff 0)에 존재 확인.
- **대표 복합 시그니처 타입 수준 대조**(개수 일치를 넘어 포인터/값 전달 방식까지):
  - `zlink_send_part_rid`(Core `socket/api.h:230-234`, `void*, const zlink_routing_id_t*, zlink_msg_t*,
    zlink_send_flags_t, zlink_part_flag_t`) ↔ dotnet(`NativeMethods.Socket.cs:81-92`,
    `IntPtr, ref ZlinkRoutingId, ref ZlinkMsg, int, ZlinkPartFlag`) — 순서·전달방식 일치.
  - `zlink_subscribe_part`(Core `socket/api.h:305-312`, 8-파라미터 `const zlink_routing_id_t**` 포함)
    ↔ dotnet(`NativeMethods.PubSub.cs:21-25`, 8-파라미터 `out IntPtr sourceRoutingId`) — `T**` 출력을
    opaque `IntPtr`로 받는 패턴이 recv 계열 전체에서 일관됨.
  - `zlink_router_recv_part`(D2F1 재확인, 6-파라미터 일치), `zlink_msg_refcnt`(D2F3 재확인, 2-파라미터
    일치) — 위 §2 참조.
- **pull-dispatch 수명**(DF7 계열): `MeshDispatchRuntime.cs`의 `MeshReadyBatch`/`MeshClaim`/
  `MeshReceiveBatch` 3종 전부 `GC.SuppressFinalize` + finalizer 유지(iteration-2 이후 diff에 해당
  파일 변경 없음, iteration-2 확인 그대로 유효).
- **join-reply**(DF2 계열): `MeshDispatch.cs`의 `AcceptJoin`/`RejectJoin`이 `zlink_actor_join_reply`
  전용 경로 사용(iteration-2 이후 diff에 해당 파일 변경 없음).
- **transfer**(DF5 계열): `IMeshNode.cs`/`MeshNode.Actors.cs`의 4종 transfer fence API·
  `NativeMeshModels.cs`의 transfer 구조체 필드 순서, iteration-2 이후 diff에 변경 없음 —
  §3의 전수 P/Invoke 대조에 4개 transfer 심볼(`zlink_mesh_node_actor_transfer_{prepare,commit,
  activate,abort}`, `NativeMethods.Actor.cs:77-91`)이 포함돼 파라미터 개수 일치 재확인됨.
- **metadata**: Core `message/api.h`에 별도 metadata get/set export가 없음(구 `zlink_msg_gets`는
  10.0.0에서 제거 확정, no-hit 0건). 현재 metadata에 해당하는 개념은 mesh dispatch record의
  typed kind_data(`MeshDispatchRuntime.cs`의 `DecodeKindData`)로 이관돼 있으며, 이 파일은
  iteration-2 이후 변경 없음(DI2-2 iteration-2 확인 그대로 유효).
- **raw-layer 드리프트 잔존 여부**(D2F1-D2F3와 동일 계열의 추가 잔재 탐색): §3 전수 대조가 raw
  socket/pubsub/stream 계층 전체(`NativeMethods.Socket.cs`, `NativeMethods.PubSub.cs`,
  `NativeMethods.Core.cs`의 msg/dealer/router 함수)를 포함하며 불일치 0 — D2F1급 잔존 드리프트 없음.

- Finding: 없음
- **Verdict I1: CLEAN**

### I2 — POSD·DDD

- 파일 크기 재확인: scope 208개 파일 중 최대는 `TypedExceptions.cs`(610줄, 공개 예외 계층이라
  크기가 자연스러움), 이어 `MeshNode.cs`(503줄), `SampleSupport.cs`(442줄, 샘플 공용 헬퍼),
  `Poller.cs`(405줄) — god-file 없음.
- `NativeMethods.*`/`SocketKernel.*`는 여전히 책임별 partial class 분할 유지.
- iteration-2 이후 diff는 순수 삭제(-313줄, 죽은 raw-attach/subscribe-handler 지원 코드 제거)만
  포함 — 신규 결합도 증가나 책임 혼재 없음. 오히려 D2I3-1 정리로 `SocketKernel.Stream.cs`가
  169줄 감소하며 단일 책임이 더 선명해짐.
- scope 전체 `TODO`/`FIXME`/`HACK` grep 0건.
- Finding: 없음
- **Verdict I2: CLEAN**

### I3 — 정리 완결성

프롬프트·iteration-1/2 ledger가 지목한 폐기 개념 전부(9 + 4 = 13개 패턴)를 208개 파일 전체
scope로 재실행했다.

| 개념 | grep 대상 | 결과 |
|---|---|---|
| SpotNode | `\bSpotNode\b` | 0건 |
| RouteBridge | `RouteBridge` | 0건 |
| spot_node | `spot_node` | 0건 |
| subjects | `\bsubjects\b` | 0건 |
| internal_sockets | `internal_sockets`\|`InternalSockets` | 0건 |
| pub-sub 별도 routing_id | `set_pub_routing_id`\|`set_sub_routing_id` | 0건 |
| dispatch_workers | `dispatch_workers`\|`DispatchWorkers` | 0건 |
| recv_actor_part | `recv_actor_part`\|`RecvActorPart` | 0건 |
| msg_gets | `msg_gets` | 0건 |
| stream_attach_raw | `stream_attach_raw`\|`AttachStreamRaw` | 0건 |
| subscribe_handler | `subscribe_handler`\|`SubscribeHandler` | 0건 |
| stream_detach | `stream_detach`\|`DetachStream` | 0건 |
| sourceSpotRoutingId(D2F1 잔재) | `sourceSpotRoutingId` | 0건 |

13개 패턴 전부 no-hit(PASS). §3의 P/Invoke 전수 대조에서도 Core export에 없는 심볼을 참조하는
dead/제거된 P/Invoke를 추가로 찾지 못했다(불일치 0).

- Finding: 없음
- **Verdict I3: CLEAN**

## 4. 폐기 no-hit 요약

§3(I3) 표의 13개 패턴 전부 0건. 추가로 `sourceSpotRoutingId`(D2F1이 남긴 잔재 파라미터명)도
0건으로 재확인했다.

## 5. 종합

iteration-1(DF1-DF8, DI2-1/2, DI3-1)과 iteration-2(D2F1-D2F3, D2I3-1) finding 전부 commit
`481221b24`에서 실제로 해소됐음을 소스 대조로 재확인했다 — 반례 없음. iteration-2 이후 변경분은
정확히 4개 finding에 대응하는 순수 삭제/수정(14개 파일, -313줄)에 한정돼 무관한 드리프트가 없다.

fresh 전체 scope 3축 재검토 결과, **197개 P/Invoke 선언 전부**를 Core 10.0.0 export 196개와
자동 전수 대조(iteration-2에서 D2F1을 발견했던 것과 동일한 방법론)한 결과 파라미터 개수 불일치
0건, 미해결 심볼 참조 0건이었다. I2(POSD/DDD)도 god-file·TODO 없이 CLEAN이며, I3(정리 완결성)의
13개 폐기 패턴도 전부 no-hit이다.

I1·I2·I3 전부 CLEAN, finding 0건.

BINDINGS REVIEW CLEAN
