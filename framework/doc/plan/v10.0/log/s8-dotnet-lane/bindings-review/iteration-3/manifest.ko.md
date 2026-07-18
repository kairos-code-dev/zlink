# S8 DOTNET bindings 리뷰 manifest — iteration 3

| 항목 | 값 |
|---|---|
| Stage/iteration | S8-DOTNET / 3 (참조 lane) |
| 대상 commit | `481221b24` |
| Scope | `bindings/dotnet/{src,samples}` minus native/obj/bin |
| 파일 수 | 208 |
| hash | `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d` |
| R1/R2 | opus / Claude Sonnet |

## 2. Coordinator 실행 증거
- iter-1(DF*)·iter-2(D2*) finding 전량 수정. 각 finding-ledger 참조.
- `dotnet build` csproj·samples: Build succeeded 0/0.
- no-hit 8종 0. 모든 P/Invoke 심볼이 `nm -D libzlink.so.10.0.0`에 존재(제거 심볼 stream_detach/attach_raw/subscribe_handler 0). router_recv_part 6-param.

## 3. Session 기록
| | R1 opus | R2 Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
