# .NET Framework E2E 문서 갭 제거 계획

## 목적

이 문서는 .NET 담당 에이전트가 공통 framework e2e 문서에 적힌 모든 scenario를
`framework/languages/dotnet/e2e`에 빠짐없이 구현하도록 안내한다.

이 문서는 sample 작업을 포함하지 않는다. Sample gap 제거는 별도 sample 계획 문서에서 다룬다.

## 담당 범위

- E2E 대상: `framework/languages/dotnet/e2e/`
- .NET framework 대상: `framework/languages/dotnet/src/Zlink.Framework/`
- .NET codec 대상: `framework/languages/dotnet/src/Zlink.Framework.Codecs.*/`
- .NET 검증 대상: `framework/languages/dotnet/tests/`
- 공통 E2E 기준: `framework/doc/framework/common/e2e/`
- .NET E2E feature-map: `framework/languages/dotnet/e2e/*/feature-map.ko.md`

Core 성능 작업과 충돌하지 않도록 `core/`는 수정하지 않는다. .NET framework에서 발견한 문제가 core
버그로 의심되면 .NET만 우회하지 말고, C++/Node/Java/Kotlin 또는 바인딩 수준에서 같은 현상이 재현되는지
확인한 뒤 버그 리포트로 분리한다.

## 버그 처리 원칙

작업 중 버그가 드러나면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 로그, 재현 절차, 영향을
받는 언어와 계층을 먼저 확인하고, 원인이 .NET framework, binding, connector, e2e harness 중 어디에
있는지 좁힌다.

실제 버그로 확인되면 가능한 범위에서 먼저 회귀테스트를 작성하거나 같은 변경에 포함한다. 그 다음 원인
계층에서 버그를 수정하고, 회귀테스트와 해당 e2e runner를 다시 실행한 뒤 원래 작업을 계속 진행한다.
버그 수정 없이 `sleep`, retry-only wrapper, reflection 우회, internal 접근, raw frame 조작,
test-only adapter, e2e scenario 변경으로 실패를 숨기지 않는다.

## 완료와 gap 처리 원칙

이 계획의 목표는 공통 e2e 문서와 .NET E2E 구현 사이의 gap을 없애는 것이다. `partial`이나 `gap` 표기는
작업 중 상태를 보이게 하기 위한 임시 표시일 뿐 완료 판정이 아니다.

공통 e2e 문서가 요구하는 공개 동작인데 .NET에서 바로 구현할 수 없으면, 먼저 `feature-map.ko.md`에
이유를 적고 설계 이슈로 분리한다. 그 뒤 필요한 spec/guide/draft 검토와 public API 설계를 거쳐 다시
구현해야 한다. 설계 이슈로 분리했다는 사실만으로 이 계획을 완료 처리하지 않는다.

## E2E 구현 절차

1. `framework/doc/framework/common/e2e/README.ko.md`와 `config-1`부터 `config-9`까지 모든 문서를
   읽고 scenario ID를 표로 만든다.
2. 각 config마다 .NET `feature-map.ko.md`, `Client/Scenarios`, `Server/<Role>`, `Shared`,
   `run_e2e.sh`를 대조한다.
3. .NET `feature-map.ko.md`를 먼저 갱신한다.
   - 공통 문서의 scenario ID를 모두 행으로 둔다.
   - 공통 문서의 scenario 상태는 `implemented`, `partial`, `gap` 중 하나로 적는다.
   - `partial`과 `gap`은 이유, 필요한 public API, 막힌 계층을 함께 적는다.
   - 기존 .NET 파일을 inventory 성격으로 매핑할 때만 `merged`, `stale`, `not needed` 같은 보조 상태를
     쓸 수 있다. 공통 scenario 자체를 `not applicable`로 닫지 않는다.
4. .NET public framework API로 구현 가능한 항목은 실제 역할 프로세스, runner, scenario evidence까지
   구현한다.
5. public API가 없어 구현할 수 없는 항목은 reflection, internal 접근, raw frame, 테스트 전용 adapter로
   우회하지 않는다. 문서에 gap으로 남기고 설계 검토 항목으로 분리한다.
6. 각 config의 `run_e2e.sh`는 standalone으로 실행 가능해야 하며, 성공 시 명확한 최종 pass marker를
   출력해야 한다.
7. config 하나가 끝날 때마다 build, test, runner, feature-map을 맞춘 뒤 다음 config로 넘어간다.

필수 config 목록:

- `LocationMessaging`
- `SpotService`
- `PubSub`
- `RegistrationCodec`
- `ResilienceLifecycle`
- `StoreFailure`
- `RuntimeMonitoring`
- `YieldDispatch`
- `ToActorMessaging`

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 solution, test project, runner를 확인한 뒤 실행한다.

```bash
dotnet build framework/languages/dotnet/Zlink.Framework.sln
dotnet test framework/languages/dotnet/Zlink.Framework.sln
for f in framework/languages/dotnet/e2e/*/run_e2e.sh; do timeout 420s "$f"; done
```

실행 환경이나 port 충돌 때문에 전체 루프가 실패하면 실패 config를 먼저 단독 재현하고, 단독 pass 후
전체 루프를 다시 실행한다.

## 완료 전 누락 리뷰

구현 담당 에이전트가 완료를 주장하기 전에 별도 Codex 에이전트로 read-only 리뷰를 요청한다.

리뷰 요청은 아래 범위를 포함해야 한다.

- 공통 e2e 문서의 모든 scenario ID가 .NET `feature-map.ko.md`와 runner evidence에 존재하는지
- 공통 e2e 문서의 역할, 메시지 흐름, evidence, failure/recovery 조건이 .NET E2E에 빠짐없이 구현됐는지
- public contract gap을 reflection, internal 접근, 테스트 전용 adapter로 숨기지 않았는지
- `run_e2e.sh`, `dotnet build`, `dotnet test` 결과가 실제로 pass했는지

리뷰 결과가 `NO MISSING DOTNET E2E ITEMS`가 아니면 모든 finding을 수정하고 같은 리뷰를 다시 요청한다.

## POSD/DDD 반복 리뷰

누락 리뷰가 깨끗해진 뒤에만 별도 Codex 에이전트로 POSD/DDD 리뷰를 요청한다. 이 리뷰는 동작 누락이
아니라 구조 개선 가능성만 본다.

리뷰 기준:

- public API가 shallow wrapper로 늘어나지 않았는지
- codec, transport, registry, location store, actor/session lifecycle 같은 지식이 호출자나 E2E scenario로
  새어나오지 않았는지
- domain role과 ASP.NET hosting, DI, process harness 책임이 섞이지 않았는지
- handler, runtime, runner 사이에 같은 정책이 반복 구현되지 않았는지
- .NET idiom을 따르면서도 공통 E2E 문서의 domain 흐름과 같은 의미를 유지하는지

의미 있는 refactoring finding이 나오면 구현, 테스트, 문서 갱신을 한 뒤 E2E 검증과 POSD/DDD 리뷰를
다시 실행한다. 리뷰가 `NO POSD/DDD DOTNET E2E REFACTOR ITEMS`를 반환할 때 종료한다.

## 최종 종료 조건

- 모든 공통 E2E scenario가 .NET에서 `implemented`로 남아 있다.
- `partial` 또는 `gap`으로 남은 E2E 항목이 없다. public contract 설계가 필요한 항목이 있으면 이 계획은
  완료가 아니라 blocked 상태로 남긴다.
- 모든 .NET E2E runner가 pass했다.
- 누락 리뷰가 `NO MISSING DOTNET E2E ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD DOTNET E2E REFACTOR ITEMS`를 반환했다.
