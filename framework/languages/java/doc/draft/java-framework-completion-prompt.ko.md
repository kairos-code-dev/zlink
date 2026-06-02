<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [Java 묶음](./README.ko.md) | [실행 계획](./implementation-execution-plan.ko.md)
<!-- framework-adapter-nav:end -->

# Draft -- Java/Kotlin Framework Completion Prompt

아래 프롬프트는 Java/Kotlin framework 포팅 작업을 끝까지 수행할 때 사용한다.
대상 문서는 [implementation-execution-plan](./implementation-execution-plan.ko.md),
[handler-interfaces](./handler-interfaces.ko.md),
[sample-implementation-plan](./sample-implementation-plan.ko.md),
[regression-test-matrix](./internals/regression-test-matrix.ko.md)이다.

```text
/home/hep7/project/kairos/zlink 에서 Java/Kotlin zlink framework 포팅을 끝까지 완료해줘.

기준 문서:
- framework/languages/java/doc/draft/implementation-execution-plan.ko.md
- framework/languages/java/doc/draft/handler-interfaces.ko.md
- framework/languages/java/doc/draft/sample-implementation-plan.ko.md
- framework/languages/java/doc/draft/internals/regression-test-matrix.ko.md

완료 기준:
- `.NET` framework와 구조, 기능, 사용성, sample 4축이 동등해야 한다.
- Gradle artifact와 Java package는 implementation-execution-plan §0 표를 단일 기준으로 따른다.
- Maven 좌표는 `systems.zlink:<artifact>:<version>` 형식이다.
- publish repository URL은 `MAVEN_REPOSITORY_URL`을 우선 사용하고, 없으면
  `https://maven.pkg.github.com/${GITHUB_REPOSITORY}`를 사용하며, 둘 다 없으면
  로컬 `build/repo` Maven repository를 사용한다.
- framework/languages/java 코드는 Java binding의 public API만 호출해야 한다.
- binding 기능이 부족하면 bindings/java에 public API를 추가하고 framework에서 그 API를 사용한다.
- `systems.zlink.runtime` 또는 `systems.zlink.internal` import, reflection으로 internal/private 접근,
  sleep 기반 readiness 우회, sample-only route store, sample-only metadata store,
  session relay JSON packet, in-memory route channel replacement를 만들지 않는다.
- Kotlin은 Java runtime 위의 thin wrapper로만 구현하고 Kotlin 전용 runtime 의미를 만들지 않는다.

작업 순서:
1. implementation-execution-plan §0의 Gradle multi-module skeleton, package 경계,
   maven-publish 설정을 확인하고 누락을 구현한다.
2. handler-interfaces 문서의 public contract를 컴파일 가능한 Java API로 닫는다.
3. backend adapter 계층을 통해 channel, registry, monitoring, Spot, actor/session,
   stream runtime을 구현한다.
4. 필요한 Java binding gap은 bindings/java public API와 contract test로 먼저 닫는다.
5. Spring Boot starter, SmartLifecycle, handler scanner, DI capability exposure,
   validation failure 의미를 `.NET` 기준으로 맞춘다.
6. stream connector와 JSON, MessagePack, Protobuf codec helper, lifecycle event,
   manual dispatch, request timeout, reconnect 의미를 구현한다.
7. Kotlin coroutine/Flow wrapper를 Java connector/framework 의미가 바뀌지 않게 구현한다.
8. sample-implementation-plan의 TicTacToe, TicTacToe.SessionGateway, Bingo,
   StreamingClient, Async sample을 `samples/java/*`와 `samples/kotlin/*` 양쪽에
   public API만 사용해서 구현한다.
9. regression-test-matrix의 unit, contract, fake backend, integration-single-process,
   integration-multi-process, sample regression 행을 실제 테스트로 닫는다.
10. Java 서비스와 `.NET`/C++/Node 서비스 중 최소 한 경로의 cross-language 상호호출
    release 시나리오를 추가하고 통과시킨다. Phase 10 최소 gate는
    `JavaNodeStreamInteropTest.nodeConnector_decodesJavaRequestFrame_andJavaDecodesNodeResponse`다.
11. 구현된 public API와 테스트에 존재하는 계약만 정식 spec/guide/internals로 승격하고,
    draft에는 후속 항목만 명확히 남긴다.
12. POSD 기준으로 phase별 위험 신호를 다시 검토하고, 얕은 모듈, 패스스루,
    정보 누출, 시간적 분해를 제거한다.

검증 명령:
- cd framework/languages/java && gradle build
- ./framework/languages/java/samples/run_samples.sh
- cd bindings/java && ./gradlew test
- Java framework/source/sample에서 금지 패턴 검색:
  `rg -n "import systems\\.zlink\\.(runtime|internal)|System\\.Reflection|Thread\\.sleep|sleep\\(|RouteStore|MetadataStore|RemoteAddressResolver" framework/languages/java`
- `git diff --check`

반복 방식:
- 각 phase 구현 후 regression-test-matrix의 해당 행이 실제 테스트와 연결되는지 확인한다.
- 문서와 코드 이름이 어긋나면 `.NET` 코드와 현재 Java public API를 기준으로 문서를 고친다.
- 실패를 숨기는 대기나 임시 store를 넣지 말고, 드러난 readiness/relay/dispatch 버그를 실제 구현으로 수정한다.
- 모든 gate가 통과하고 unrelated dirty tree를 분리해 확인한 뒤에만 Java 포팅 범위 변경을 commit/push한다.
```
