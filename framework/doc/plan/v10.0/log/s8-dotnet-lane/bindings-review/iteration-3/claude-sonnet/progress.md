# S8 DOTNET bindings 전환 리뷰 iteration-3 — R2 (Claude Sonnet) progress

- byte 단위 동일 prompt(`../prompt.md`) 정독. 다른 리뷰어(codex) 결과·coordinator 해석 미참조.
- Scope 시작 확인: `git ls-files bindings/dotnet/src bindings/dotnet/samples | grep -vE 'native/|/obj/|/bin/'`
  → 208개 파일, aggregate SHA-256 `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d` (prompt 값과 일치).
- 대상 commit `481221b24`이 현재 `HEAD`(`6eeb596e1`, iteration-3 freeze 커밋)의 ancestor임을 `git merge-base
  --is-ancestor`로 확인, `git diff 481221b24 HEAD -- bindings/dotnet/src bindings/dotnet/samples`가 0줄임을
  확인 — HEAD에서 검토해도 대상 commit과 완전히 동일한 내용.
- iteration-1 finding-ledger(`../../iteration-1/finding-ledger.ko.md`), iteration-2 finding-ledger
  (`../../iteration-2/finding-ledger.ko.md`) 정독. iteration-2에서 D2F1(router_recv_part)·D2F2
  (stream_detach)·D2F3(msg_refcnt)·D2I3-1(dead P/Invoke) 4건이 신규로 지목됐음을 확인.
- iteration-3 manifest(`../manifest.ko.md`) 정독: coordinator 실행 증거(csproj+samples build green,
  no-hit 0, P/Invoke⊆ABI, router_recv_part 6-param) 확인. 재실행 안 함(요구사항).
- D2F1~D2I3-1 소스 대조 해소 판정:
  - D2F1: `NativeMethods.Socket.cs:57-67` `zlink_router_recv_part`/`_nowait` 6-파라미터로 수정 확인
    (Core `socket/api.h:271-277`과 1:1), 호출부(`SocketKernel.Receive.cs:100-109`,
    `SocketKernel.ReceiveCore.cs:181-187`)도 6-인자 정합.
  - D2F2: `stream_detach`/`DetachStream` 전체 scope grep 0건. `SocketKernel.Lifecycle.cs:19-27`
    `Dispose()`가 Core 10.0.0에 detach entry point가 없다는 주석과 함께 managed 콜백 상태만 정리하는
    방식으로 재작성됨.
  - D2F3: `NativeMethods.Core.cs:287-289` `zlink_msg_refcnt(ref ZlinkMsg msg, out int errorOut)` —
    Core(`message/api.h:116`) 2-파라미터와 일치, 호출부(`Message.Native.cs:57-59`)가 `error`를
    `ZlinkException.ThrowConfigIfError`로 소비.
  - D2I3-1: `stream_attach_raw`/`AttachStreamRaw`/`subscribe_handler`/`SubscribeHandler` 전체 scope
    grep 0건.
  - `git diff --stat 115c3d73d 481221b24 -- bindings/dotnet/src bindings/dotnet/samples`로 iteration-2
    이후 변경분이 14개 파일(순삭 -313줄)로, D2F1-D2F3/D2I3-1 관련 stream/socket/msg 파일에만 한정됨을
    확인 — 무관한 드리프트(drive-by 수정) 없음.
- 신선한 전체 scope I1 재검토: `bindings/dotnet/src/Zlink/Runtime/Native/*.cs`의 197개 P/Invoke 선언
  전부를 자동 추출(파이썬 1회성 분석 스크립트, 결과만 인용·scope 파일 미수정)해 파라미터 개수를
  Core 헤더(`core/include/zlink/**/*.h`) 196개 export 시그니처와 자동 대조. 결과: 186개 고유 심볼
  전부 Core export에 존재(불일치 0), 197개 선언 전부 파라미터 개수 일치(불일치 0).
- `RequiredExportNames`(`NativeMethods.Core.cs:12-195`, 182개) 전부 Core `.so`
  (`core/build/lib/libzlink.so.10.0.0`, `nm -D --defined-only`) export 196개에 존재 확인(불일치 0).
  `.vers`와 `.so`의 export 목록도 diff 0.
- 대표 복합 시그니처(`zlink_send_part_rid`, `zlink_subscribe_part`) 타입 수준 대조 — 파라미터 순서·
  포인터/값 전달 방식 전부 일치.
- I2: 파일 크기 분포 재확인(최대 `TypedExceptions.cs` 610줄, god-file 없음), scope 전체
  TODO/FIXME/HACK grep 0건.
- I3: 프롬프트·과거 ledger가 지목한 13개 폐기 패턴(SpotNode/RouteBridge/spot_node/subjects/
  internal_sockets/pub-sub rid/dispatch_workers/recv_actor_part/msg_gets/stream_attach_raw/
  subscribe_handler/stream_detach/sourceSpotRoutingId) 전부 208개 파일 scope 재실행 → 전부 0건.
- 산출물: 본 progress.md + `review.ko.md`. build/실행 없음(정적 대조만, nm -D는 기존 스테이징된 .so
  파일 검사이며 빌드/실행이 아님). scope 파일 수정 없음.
- Scope 종료 확인: 시작과 동일 명령 재실행 결과 208개 파일, 동일 hash. `git status`로 scope 내 무수정
  확인.
