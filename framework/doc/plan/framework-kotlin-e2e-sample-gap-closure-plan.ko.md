# Kotlin Framework E2E/Sample 문서 갭 제거 계획

## 목적

이 문서는 Kotlin 담당 에이전트가 공통 framework e2e 문서와 공통 sample 문서에 적힌 내용을
`framework/languages/java`의 Kotlin 사용 표면에 빠짐없이 구현하도록 안내한다.

E2E는 공통 e2e 문서의 모든 scenario를 Kotlin public framework 표면으로 검증하는 것이 목표다. Sample은
공통 sample 문서를 계약 기준으로 삼고, `.NET` sample 구현을 포팅 기준으로 삼아 Kotlin sample을 같은
사용자 체감 동작으로 맞춘다.

기존 계획은 아래 문서를 따른다.

- `framework/doc/plan/framework-kotlin-e2e-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-kotlin-sample-dotnet-porting-plan.ko.md`

이 문서는 위 두 계획을 한 작업 흐름으로 묶는 완료 계획이다.

## 담당 범위

- E2E 대상: `framework/languages/java/e2e-kotlin/`
- Sample 대상: `framework/languages/java/samples/kotlin/`
- Kotlin framework 대상: `framework/languages/java/zlink-framework-kotlin/`
- Kotlin이 사용하는 Java 공용 framework 대상:
  - `framework/languages/java/zlink-framework-core/`
  - `framework/languages/java/zlink-framework-spring-boot-starter/`
  - `framework/languages/java/zlink-framework-codec-msgpack/`
  - `framework/languages/java/zlink-framework-codec-protobuf/`
  - `framework/languages/java/zlink-framework-locations-redis/`
- 검증 대상: `framework/languages/java/zlink-framework-testkit/`, `framework/languages/java/build.gradle.kts`
- 공통 E2E 기준: `framework/doc/framework/common/e2e/`
- 공통 sample 기준: `framework/doc/framework/common/sample/`
- `.NET` E2E 기준 구현: `framework/languages/dotnet/e2e/`
- `.NET` sample 기준 구현: `framework/languages/dotnet/samples/`

Core 성능 작업과 충돌하지 않도록 `core/`는 수정하지 않는다. Kotlin framework에서 발견한 문제가 core
버그로 의심되면 Kotlin만 우회하지 말고, C++/Node/Java 또는 바인딩 수준에서 같은 현상이 재현되는지
확인한 뒤 버그 리포트로 분리한다.

## 버그 처리 원칙

작업 중 버그가 드러나면 scenario나 sample만 통과시키는 우회 코드를 넣지 않는다. 실패 로그, 재현
절차, 영향을 받는 언어와 계층을 먼저 확인하고, 원인이 Kotlin framework, Java 공용 framework, binding,
connector, e2e/sample harness 중 어디에 있는지 좁힌다.

실제 버그로 확인되면 가능한 범위에서 먼저 회귀테스트를 작성하거나 같은 변경에 포함한다. 그 다음 원인
계층에서 버그를 수정하고, 회귀테스트와 해당 e2e/sample runner를 다시 실행한 뒤 원래 작업을 계속
진행한다. 버그 수정 없이 `sleep`, retry-only wrapper, Java internal package 접근, coroutine helper
우회, test-only adapter, sample 코드 변경으로 실패를 숨기지 않는다.

## 완료와 gap 처리 원칙

이 계획의 목표는 문서와 구현 사이의 gap을 없애는 것이다. `partial`이나 `gap` 표기는 작업 중 상태를
보이게 하기 위한 임시 표시일 뿐 완료 판정이 아니다.

공통 e2e 문서나 공통 sample 문서가 요구하는 공개 동작인데 Kotlin에서 바로 구현할 수 없으면, 먼저
`feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 이유를 적고 설계 이슈로 분리한다. 그 뒤
필요한 spec/guide/draft 검토와 public API 설계를 거쳐 다시 구현해야 한다. 설계 이슈로 분리했다는
사실만으로 이 계획을 완료 처리하지 않는다.

## E2E 구현 절차

1. `framework/doc/framework/common/e2e/README.ko.md`와 `config-1`부터 `config-9`까지 모든 문서를
   읽고 scenario ID를 표로 만든다.
2. 각 config마다 `.NET` 기준 구현과 `.NET` `feature-map.ko.md`를 읽는다.
3. Kotlin의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 먼저 갱신한다.
   - 공통 문서의 scenario ID를 모두 행으로 둔다.
   - 공통 문서의 scenario 상태는 `implemented`, `partial`, `gap` 중 하나로 적는다.
   - `partial`과 `gap`은 이유, 필요한 public API, 막힌 계층을 함께 적는다.
   - `.NET` 파일이나 기존 Kotlin 파일을 inventory에서 매핑할 때만 `merged`, `stale`, `not needed` 같은
     보조 상태를 쓸 수 있다. 공통 scenario 자체를 `not applicable`로 닫지 않는다.
4. Kotlin public framework API로 구현 가능한 항목은 실제 역할 프로세스, runner, scenario evidence까지
   구현한다.
5. public API가 없어 구현할 수 없는 항목은 Java internal package, 테스트 전용 adapter, coroutine
   helper 우회로 메우지 않는다. 문서에 gap으로 남기고 설계 검토 항목으로 분리한다.
6. 각 config의 `run_e2e.sh`는 standalone으로 실행 가능해야 하며, 성공 시 명확한 최종 pass marker를
   출력해야 한다.
7. config 하나가 끝날 때마다 Gradle build/test, runner, feature-map, inventory를 맞춘 뒤 다음 config로
   넘어간다.

필수 config 목록:

- `LocationMessaging` 또는 Kotlin에서 같은 의미로 명명된 registry/location messaging config
- `SpotService`
- `PubSub`
- `RegistrationCodec`
- `ResilienceLifecycle`
- `StoreFailure` 또는 Kotlin에서 같은 의미로 명명된 store failure/recovery config
- `RuntimeMonitoring`
- `YieldDispatch`
- `ToActorMessaging`

현재 트리에 `.NET` 기준 이름과 다른 `RegistryMessaging`, `DiscoveryRegistryHa` 같은 디렉터리가 있으면
바로 삭제하거나 완료로 인정하지 않는다. 먼저 공통 config 문서와 `.NET` config에 어느 scenario가
대응되는지 inventory에 매핑하고, 중복·stale·rename 대상 여부를 리뷰로 확인한 뒤 정리한다.

## Sample 포팅 절차

1. `framework/doc/framework/common/sample/README.ko.md`와 sample별 문서를 모두 읽는다.
   - event sample은 `framework/doc/framework/common/sample/event/*.ko.md`도 함께 읽는다.
2. `.NET` sample 6종의 실제 코드, runner, README를 읽고 Kotlin sample에 대응시킨다.
3. 각 Kotlin sample에 `sample-porting-inventory.ko.md`를 유지한다.
   - `.NET`의 역할, shared contract, client self-check, runner evidence를 빠짐없이 매핑한다.
   - Kotlin coroutine/DSL idiom 때문에 파일명이나 handler 구조가 달라도 책임은 누락하지 않는다.
4. Sample 코드는 사용자가 따라 할 public API 예시다. Java runtime internal package, raw buffer 처리,
   codec 수동 우회, 테스트 전용 hook을 sample 코드에 넣지 않는다.
5. 각 sample의 `run_sample.sh`는 standalone으로 실행 가능해야 하며, client success뿐 아니라 서버 역할
   로그와 scenario evidence도 확인해야 한다.
6. 모든 sample이 끝난 뒤 Kotlin sample 전체 runner와 sample release gate를 실행한다.

필수 sample 목록:

- `TicTacToe`
- `Bingo`
- `DeliveryDispatch`
- `SupportChat`
- `GameQuest`
- `ShoppingMall`

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 Gradle task 이름을 확인한 뒤 실행한다.

```bash
cd framework/languages/java
./gradlew build
./gradlew test
./gradlew sampleTest
for f in e2e-kotlin/*/run_e2e.sh; do timeout 420s "$f"; done
ZLINK_SAMPLE_FILTER= timeout 900s samples/run_samples.sh
```

Gradle daemon, port 충돌, Redis 구동 문제 때문에 전체 루프가 실패하면 실패 config/sample을 먼저 단독
재현하고, 단독 pass 후 전체 루프를 다시 실행한다.

`samples/run_samples.sh`는 Java와 Kotlin sample gate를 함께 실행하는 통합 runner다. Kotlin sample만
좁혀서 재현해야 할 때는 `ZLINK_SAMPLE_FILTER=kotlin/<Sample>` 형식으로 단일 sample을 먼저 실행하고,
수정 후에는 filter 없이 통합 runner를 다시 실행한다.

## 완료 전 누락 리뷰

구현 담당 에이전트가 완료를 주장하기 전에 별도 Codex 에이전트로 read-only 리뷰를 요청한다.

리뷰 요청은 아래 범위를 포함해야 한다.

- 공통 e2e 문서의 모든 scenario ID가 Kotlin `feature-map.ko.md`와 runner evidence에 존재하는지
- 공통 sample 문서의 모든 역할, 메시지 흐름, self-check가 Kotlin sample inventory와 runner evidence에
  존재하는지
- `.NET` 기준 구현에 있는 역할과 client 검증이 Kotlin에서 누락되지 않았는지
- public contract gap을 Java internal package, 테스트 전용 adapter, coroutine helper 우회로 숨기지
  않았는지
- `run_e2e.sh`, `run_sample.sh`, `sampleTest`, Gradle test 결과가 실제로 pass했는지

리뷰 결과가 `NO MISSING KOTLIN ITEMS`가 아니면 모든 finding을 수정하고 같은 리뷰를 다시 요청한다.

## POSD/DDD 반복 리뷰

누락 리뷰가 깨끗해진 뒤에만 별도 Codex 에이전트로 POSD/DDD 리뷰를 요청한다. 이 리뷰는 동작 누락이
아니라 구조 개선 가능성만 본다.

리뷰 기준:

- public API가 shallow wrapper로 늘어나지 않았는지
- codec, transport, registry, location store, actor/session lifecycle 같은 지식이 호출자나 sample로
  새어나오지 않았는지
- domain role과 coroutine/Gradle infrastructure 책임이 섞이지 않았는지
- handler, runtime, runner, sample 사이에 같은 정책이 반복 구현되지 않았는지
- Kotlin idiom을 따르면서도 `.NET` 기준 domain 흐름과 같은 의미를 유지하는지

의미 있는 refactoring finding이 나오면 구현, 테스트, 문서 갱신을 한 뒤 E2E/sample 검증과 POSD/DDD
리뷰를 다시 실행한다. 리뷰가 `NO POSD/DDD KOTLIN REFACTOR ITEMS`를 반환할 때 종료한다.

## 최종 종료 조건

- 모든 공통 E2E scenario가 Kotlin에서 `implemented`로 남아 있다.
- 공통 sample 문서와 `.NET` sample 기준에 대해 Kotlin sample gap이 없다.
- `partial` 또는 `gap`으로 남은 E2E/sample 항목이 없다. public contract 설계가 필요한 항목이 있으면
  이 계획은 완료가 아니라 blocked 상태로 남긴다.
- 모든 Kotlin E2E runner와 sample runner가 pass했다.
- 누락 리뷰가 `NO MISSING KOTLIN ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD KOTLIN REFACTOR ITEMS`를 반환했다.
