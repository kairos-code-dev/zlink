# .NET Framework runtime unit와 Actor Join E2E 진행 기록

## 기록 범위

- 기록 시각: 2026-08-03 09:28:06 KST
- 기준 `HEAD`: `ec9455858008becd58edd13aa44c5a6b285f7a51`
- branch: `agent/framework-contract-runtime-update`
- 대상: `.NET Framework` runtime source, runtime unit test와
  `SpotActorTransfer` process E2E
- 다른 언어 workstream의 dirty 변경은 범위에서 제외하고 보존했다.

## Runtime 변경과 unit evidence

이번 round에서 bound Session relocation의 target staged route를 inbound relay가 사용할 수
있도록 했고, Message Follow가 bound one-way operation ID와 target fence를 유지하도록 했다.
이 변경은 source나 sample의 우회 경로를 추가하지 않고 runtime state와 route projection의
책임 안에서 처리했다.

| 검증 | 결과 |
|---|---:|
| `Zlink.Framework.UnitTests.csproj --filter FullyQualifiedName~Runtime --no-restore` | `740/740` 통과, failed `0`, skipped `0` |
| bound-session·entry dispatch targeted filter | `136/136` 통과 |
| staged route·Message Follow·canonical targeted filter | `10/10` 통과 |
| `git diff --check` (.NET 범위) | 통과 |
| ST-D2 standalone | `PASS`, `logs/20260803-084725-333595` |
| ST-C1 | `PASS`, `logs/20260803-085024-355416` |
| ST-C2 | `PASS`, `logs/20260803-085048-357430` |
| ST-C3 | `PASS`, `logs/20260803-085102-358663` |
| ST-F1 | `PASS`, `logs/20260803-090103-388762` |
| ST-F2 | `PASS`, `logs/20260803-090312-396206` |
| ST-F3 | `PASS`, `logs/20260803-092034-460372` |
| ST-F6 | `PASS`, `logs/20260803-092320-469311` |

Runtime unit 결과는 runtime implementation gate가 현재 source에서 통과했음을 보여준다.
이 결과만으로 process E2E, package consumer 또는 독립 audit 완료를 주장하지 않는다.

## Aggregate 실행과 남은 E2E gap

`framework/languages/dotnet/e2e/SpotActorTransfer/run_e2e.sh all`은
`logs/20260803-092339-471190`에서 첫 번째 scenario batch와 ST-F6까지 통과한 뒤
ST-B2에서 중단됐다.

ST-B2의 현재 fixture는 source `OnLeaveActorAsync`가 반환하기 전까지 target의
`success_reply`와 application handler를 허용하지 않는다고 가정한다. 그러나 공통 Actor
계약은 target `OnJoinedActorAsync` 뒤 source leave notification을 one-way으로 보내며,
source resource cleanup의 완료를 target Join completion barrier로 사용하지 않는다.
따라서 현재 runtime에서 target의 `success_reply`가 source cleanup gate가 열린 뒤가 아니라
그 전에 기록된 것은 runtime regression이 아니라 fixture와 공통 계약의 불일치다.

실패 evidence:

- actor-a: `source_cleanup_wait|entry-spot-leave`
- actor-b: `transfer_in`, `joined`, `ST-B2 success_reply` 순서
- client: `ST-B2 admitted application work before source cleanup became durable.`

근거 문서는 [Actor Join completion 계약](../../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)의
source leave와 completion 설명이다. 이 fixture를 통과시키기 위해 source cleanup을
completion barrier로 되돌리지 않는다. ST-B2는 공통 계약에 맞춰 no takeover/no replay와
process 종료 경계를 검증하는 별도 E2E 수정 대상으로 남긴다.

## 현재 판정

- DN-IMP runtime source와 unit gate: 현재 round 기준 통과.
- ST-C1/C2/C3, ST-D2, ST-F1/F2/F3/F6: actual process evidence 통과.
- ST-B2: fixture 계약 불일치로 열린 E2E gap.
- Phase A: process 분모, package clean consumer와 독립 final audit이 남아 있어 미완료.
- Phase B: Phase A 완료 전 시작하지 않는다.

다음 재개 지점은 ST-B2 fixture와 feature-map을 공통 spec의 completion·process-failure
의미에 맞추고, 그 뒤 aggregate를 다시 실행하는 것이다. Runtime source에 E2E 기대를 맞추기
위한 우회 코드는 추가하지 않는다.
