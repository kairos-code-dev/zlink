# S8 DOTNET bindings 리뷰 manifest — iteration 2

| 항목 | 값 |
|---|---|
| Stage/iteration | S8-DOTNET / 2 (참조 lane) |
| 대상 commit | `115c3d73d` |
| Scope | `bindings/dotnet/{src,samples}` minus native/obj/bin |
| 파일 수 | 208 |
| hash | `d6acf3e49cdb1f96aac8d92e6b403d79502f2f65866687581b0dc4308ad4c048` |
| R1/R2 | Codex / Claude Sonnet |

## 2. Coordinator 실행 증거
- iter-1 finding(DF1-DF8·DI2·DI3·metadata) 전량 수정. `../iteration-1/finding-ledger.ko.md`.
- `dotnet build src/Zlink/Zlink.csproj`·`samples/Zlink.Samples.sln`: Build succeeded 0/0.
- no-hit 8종 전량 0. RequiredExportNames 182 전부 `core/src/libzlink.vers`+`libzlink.so.10.0.0`에 존재(로드 gate 통과).

## 3. 알려진 관찰(독립 판정)
- raw 계층 드리프트(`log/s8-common-raw-layer-drift.ko.md`): `zlink_subscribe_handler`(Core 제거)를 dotnet SubscribeHandler가 16회 사용→raw SUB 파손 가능. dead P/Invoke `zlink_stream_attach_raw/detach`, `zlink_poller_wait_pinned`(호출 0). 이들이 I1/I3 finding인지 스스로 판정.
- `tests/Zlink.Tests` scope 밖.

## 4. Session 기록
| | R1 Codex | R2 Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
