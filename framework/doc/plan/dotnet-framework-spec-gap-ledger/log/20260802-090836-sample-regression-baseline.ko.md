# .NET common sample gap baseline log

## 기록 범위

- 대상 ledger: [`../dotnet-framework-spec-gap-ledger.ko.md`](../dotnet-framework-spec-gap-ledger.ko.md)
- 기록 시각: 2026-08-02 09:08:36 KST
- 선행 기준 commit: `ac5335acb2843ab73e23b34d8afd33203b388d60`
- 목적: sample ledger의 현재 정적 regression 결과와 그 결과가 닫지 못한 gap을 고정한다.

이 log는 gap ledger의 실행 근거를 보관한다. 본문에는 실행 output이나 review 이력을 반복하지 않고,
gap ID와 다음 판정에 필요한 링크만 둔다.

## Working tree 조건

실행 시점에는 선행 commit 이후 다음 변경이 working tree에 있었다.

- `framework/doc/plan/dotnet-framework-spec-gap-ledger/dotnet-framework-spec-gap-ledger.ko.md`의 Phase B
  sample section
- `framework/doc/plan/node-framework-spec-gap-ledger/node-framework-sample-spec-gap-ledger.ko.md`
- `framework/languages/node/test/contract/actor-handoff.test.js`
- `framework/languages/node/test/contract/actor-manager.test.js`
- `framework/languages/node/test/contract/backend-contract.test.js`

따라서 이 결과는 위 변경을 포함한 현재 working tree의 baseline이며, clean package consumer나 실제
process sample E2E의 결과가 아니다.

## 실행 명령과 결과

```text
cd framework/languages/dotnet
dotnet test tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj --no-restore
```

결과: `134 passed, 0 failed, 134 total`.

이전 `DotNet_Docs_Keep_Actor_Destroy_Entry_Owned` failure는 공통 TicTacToe 문서의 actor destroy
설명이 현재 regression과 일치하여 이 baseline에서는 재현되지 않았다. 이 pass는 기존 정적 규칙이
통과했다는 뜻이며, 공통 sample 7종의 message·field·flow·wire·process evidence가 일치했다는 뜻은
아니다.

2026-08-02 09:12:05 KST에 같은 명령을 재실행해 `134 passed, 0 failed, 134 total`을 확인했다.
이 재검증도 source sample process E2E나 clean package consumer lane을 포함하지 않는다.

## Gap에 미치는 의미

- `DS-IMP-001`~`DS-IMP-009`의 gap 판정은 유지한다. 정적 regression pass만으로 gap을 `충족` 또는
  `수정 완료`로 바꾸지 않는다.
- `DS-REG-001`은 공통 sample 7종의 exact message·field·direction·transport inventory를 추가해야
  한다.
- `DS-REG-004`는 TicTacToe의 one-way leave와 Entry Spot destroy semantics를 문서·source·실행
  evidence로 직접 대조해야 한다.
- `DS-IMP-007`과 `DS-IMP-008`은 package version 설명과 shell·PowerShell runner inventory를
  각각 owner에서 확인해야 한다.
- source sample process E2E와 clean package consumer lane은 이 baseline 결과와 별도로 실행한다.

## 다음 작업

1. `G0` 선행 Framework ledger와 package gate를 확인한다.
2. `G1`에서 공통 sample contract의 message·field·transport·runner 범위를 동결한다.
3. `G2`에서 `DS-REG-001`~`DS-REG-003`과 sample별 실패 regression을 먼저 고정한다.
4. 각 card의 실행 명령, 결과, working-tree 조건과 review 결과는 이 폴더의 새 timestamp log에
   기록한다.
