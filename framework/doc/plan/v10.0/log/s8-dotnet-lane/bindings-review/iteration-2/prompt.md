# RouteMesh 10.0.0 S8 DOTNET bindings 전환 리뷰 — iteration 2 공통 prompt

너는 S8 DOTNET bindings 전환 리뷰 iteration 2의 독립 리뷰어다. byte 단위 동일 prompt(R1 Codex·R2 Sonnet). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `115c3d73d` (iteration-1 finding 수정 반영)
- Scope: `git ls-files bindings/dotnet/src bindings/dotnet/samples` 중 `native/`·`/obj/`·`/bin/` 제외
- Scope 파일 수: 208
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `d6acf3e49cdb1f96aac8d92e6b403d79502f2f65866687581b0dc4308ad4c048`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iteration-1 finding 해소
`../iteration-1/finding-ledger.ko.md`와 두 리뷰(`../iteration-1/{codex,claude-sonnet}/review.ko.md`)를 읽고 commit `115c3d73d`를 소스 대조해 해소 판정:
DF1 RequiredExportNames(10.0.0 ABI 부분집합·로드 gate), DF2 typed ActorJoinResult ReplyJoin/Accept/Reject via actorJoinReply, DF3 SetRoutingId+samples, DF4 caller-init struct_size/version, DF5 actor transfer API, DF6 StreamSocket native routing-id, DF7 finalizers, DF8 511 endpoint validator, DI2-1 stream_session actor 소유, DI2-2 typed kind_data record, DI3-1 msg_gets/router-spot/RouteBridge 제거. 해소된 finding은 새 반례 없이 재개하지 마라.

## 이후: 전체 scope 재검토(3축)
- **I1 계약 일치**: Core C API와 C#·P/Invoke 계약. 매핑·marshalling(struct_size/version, ownership)·pull dispatch(IDisposable/finalizer 수명)·join-reply route·metadata·transfer·필수 pre-start 설정. raw 계층 드리프트(예: `zlink_subscribe_handler` 제거 — Core 미export이나 P/Invoke·사용 잔존 시 파손) 판정.
- **I2 POSD·DDD**. **I3 정리 완결성**(폐기 잔재·제거 심볼 dead P/Invoke·no-hit).

## 절차
산출물은 review 디렉터리 `progress.md`·`review.ko.md`만. build/실행 금지(정적 대조). 실행 증거는 manifest의 coordinator 결과(csproj+samples green, no-hit 0, RequiredExportNames⊆ABI)만. iteration 2: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-1 finding 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 폐기 no-hit 판정 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
