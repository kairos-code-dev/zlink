# RouteMesh 10.0.0 S8 DOTNET bindings 전환 리뷰 — iteration 3 공통 prompt

너는 S8 DOTNET bindings 전환 리뷰 iteration 3의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `481221b24` (iter-1·iter-2 finding 수정 반영)
- Scope: `git ls-files bindings/dotnet/src bindings/dotnet/samples` 중 `native/`·`/obj/`·`/bin/` 제외
- 파일 수: 208
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `58926717c4236c6770b52cb51b4166686735bef6468d07a613741b8a9938653d`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-1·iter-2 finding 해소
- iter-1: `../iteration-1/finding-ledger.ko.md` DF1-DF8·DI2·DI3·metadata.
- iter-2: `../iteration-2/finding-ledger.ko.md` D2F1(router_recv_part 6param)·D2F2(stream_detach 제거·Core는 packet_handler 모델·attach/detach 없음)·D2F3(msg_refcnt error_out)·D2I3-1(dead stream_attach_raw/subscribe_handler 제거).
소스 대조로 해소 확인. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1 계약 일치(P/Invoke⊆Core 10.0.0 ABI, marshalling, pull dispatch 수명, join-reply, metadata/transfer, raw-layer 드리프트 잔존 여부), I2 POSD·DDD, I3 정리(폐기 no-hit·dead P/Invoke).

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(csproj+samples green, no-hit 0, P/Invoke⊆ABI)만. iteration 3: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-1·iter-2 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 폐기 no-hit 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
