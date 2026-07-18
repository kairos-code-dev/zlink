# S8 DOTNET bindings 전환 리뷰 manifest — iteration 1

## 1. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S8-DOTNET bindings / 1 (참조 lane) |
| 대상 commit | `29151802f` (`s8-dotnet(bindings): convert 12 samples to MeshNode/pull-dispatch`) |
| Scope | `git ls-files bindings/dotnet/{src,samples}` 중 `native/`·`/obj/`·`/bin/` 제외 |
| Scope 파일 수 | 206 (src 157, samples 49) |
| Scope aggregate SHA-256 | `c9e0aef9e4d386a058282d611f76892530ffe190d1a7f076b4040597f7f9a66b` |
| 공통 prompt | `prompt.md` |
| R1 / R2 | Codex / Claude Sonnet |
| 규칙 | 리뷰어 산출물은 progress.md·review.ko.md만, 실행 작업 금지. iteration 1=세 축 finding 0이어야 CLEAN. tests/Zlink.Tests는 scope 제외(별도 test 변환 트랙) |

## 2. Coordinator 실행 증거 (리뷰 전 확보)

- `dotnet build src/Zlink/Zlink.csproj -c Release`: **Build succeeded, 0 error, 0 warning**.
- `dotnet build samples/Zlink.Samples.sln -c Release`: **Build succeeded, 0 error, 0 warning** (18 sample 프로젝트).
- 폐기 개념 no-hit(Contracts/Service): SpotNode/RouteBridge/spot_node 각 0.
- 참조 C# 표면 확정: pull dispatch(SetReadyHandler/DrainReady/TakeClaim/Receive/RetainMessage/Reply-token)·SubmitResult+MeshOperationId direct·IStreamSessionService.

## 3. 종료 검증 목록 (두 clean 후 coordinator)

- 라이브러리+samples clean 재빌드, 공개 API surface 대조, 로컬 package smoke(해당 시).

## 4. 알려진 관찰 (리뷰어 독립 판정 대상)

- join-admission이 편의 메서드 없이 ready-index drain + `MeshReceiveRecord.Reply`로만 도달 가능(저수준). cpp lane도 동형 관찰. I1/I2 여부는 리뷰어 판단.
- `tests/Zlink.Tests` 16 에러(제거 타입 참조) — scope 밖, 별도 test 변환.

## 5. Session 기록

| 항목 | R1 Codex | R2 Claude Sonnet |
|---|---|---|
| 결과 | `codex/review.ko.md` | `claude-sonnet/review.ko.md` |
| 종료 상태 | 기록 예정 | 기록 예정 |
