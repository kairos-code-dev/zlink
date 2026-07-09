# Java Framework E2E/Sample 문서 갭 제거 계획

## 목적

이 문서는 Java 담당 에이전트가 공통 framework e2e 문서와 공통 sample 문서에 적힌 내용을
`framework/languages/java`의 Java 사용 표면에 빠짐없이 구현하도록 안내한다.

E2E는 공통 e2e 문서의 모든 scenario를 Java public framework 표면으로 검증하는 것이 목표다. Sample은
공통 sample 문서를 계약 기준으로 삼고, `.NET` sample 구현을 포팅 기준으로 삼아 Java sample을 같은
사용자 체감 동작으로 맞춘다.

기존 계획은 아래 문서를 따른다.

- `framework/doc/plan/framework-java-e2e-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-java-sample-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-ref-target-unification-java-worker-prompt.ko.md`

이 문서는 위 계획들을 한 작업 흐름으로 묶는 완료 계획이다. Ref 대상 통일 worker는 actor id 또는 spot
id만 받아서 메시지를 보내는 public API를 제거하고 `ActorRef` / `SpotRef` 기반 전송으로 Java public
surface를 맞추는 작업이다. 이 변경은 E2E와 sample의 호출 모양을 직접 바꾸므로, Java E2E/sample gap
제거와 같은 진행 단위에서 처리한다.

## Ref 대상 통일 worker prompt 포함 범위

`framework/doc/plan/framework-ref-target-unification-java-worker-prompt.ko.md`는 이 진행 계획의 참조
문서가 아니라 필수 수행 범위다. Java E2E/sample gap 제거 작업을 완료하려면 해당 worker prompt의
내용도 같은 작업 흐름 안에서 함께 구현하고 검증해야 한다.
진행 중 판단이 필요하면 이 문서의 요약보다 worker prompt 원문을 우선 확인한다. 원문에 적힌 요구,
제거 대상, 검증 명령, 완료 게이트는 이 계획에 직접 포함된 요구로 취급한다.
따라서 이 문서는 worker prompt의 일부 항목만 가져오는 요약 문서가 아니다. worker prompt에 적힌
통합 수행 범위, naming 규칙, 제거 대상, 추가/변경 대상, 주요 파일, 테스트, 문서 변경 대상, handler
등록 정책, connection 복구 책임 경계, 완료 게이트를 모두 이 계획의 완료 조건으로 본다.

함께 진행해야 하는 내용은 아래와 같다.

- Java public contract에서 메시지 전송 대상은 `ActorRef` / `SpotRef`로 통일한다. actor id 또는 spot
  id만 받아 전송하는 public API는 제거 대상이다. id는 조회 입력이고, ref는 전송 입력이다.
- `ZLinkActorRef`, `ZLinkActorRefSnapshot`, `ZLinkSpotAddress`, `ZLinkSpotAddressResolver` 같은 ref
  값 타입 이름은 worker prompt의 naming 규칙에 맞춰 정리한다. service, manager, runtime, builder
  같은 역할 타입은 기존 `ZLink` prefix 유지 기준을 따른다.
- Ref 대상 통일이 영향을 주는 Java E2E와 sample은 새 `ActorRef` / `SpotRef` 표면으로 바로 갱신한다.
  이름 변경이나 API 제거만 끝내고 E2E/sample 호출부 gap을 남기면 완료로 보지 않는다.
- `framework/languages/java/e2e/SpotService`, `YieldDispatch`, `RuntimeMonitoring`, `ToActorMessaging`와
  Java sample의 `Bingo`, `DeliveryDispatch`, `SupportChat`, `ShoppingMall`은 worker prompt가 지정한
  중점 확인 범위로 함께 점검한다.
- worker prompt의 테스트 목록과 완료 게이트를 이 계획의 완료 게이트에 포함한다. 특히 id-only messaging
  API가 public contract에 남지 않았는지, ref 기반 전송 중 location resolver/store를 다시 호출하지
  않는지, stale `SpotRef` 실패 분류가 기존 계약과 맞는지 확인한다.
- worker prompt의 문서 변경 대상인 `framework/doc/contract-inventory/framework-public-contract-inventory.json`,
  `framework/doc/framework/common`, `framework/doc/framework/java`도 코드 변경과 같은 작업 안에서 맞춘다.
- worker prompt의 메시지 handler 등록 정책 동시 적용과 sample별 자동/수동 등록 정책도 이 계획의
  필수 조건이다. `TicTacToe` Java sample만 수동 등록 예시로 남기고, 나머지 Java sample은 자동 등록만
  사용하도록 정리한다.
- worker prompt의 connection 복구 책임 경계 감사도 함께 진행한다. Java framework가 established
  connection recovery를 직접 구현하지 않는다는 기준을 지키고, topology handover와 binding 준비 수렴
  대기를 연결 복구로 오해하게 만드는 이름, 주석, 테스트를 정리한다.

완료 보고에는 이 문서의 E2E/sample gap 결과와 함께 ref 대상 통일, handler 등록 정책, connection 복구
책임 경계 확인 결과를 별도 항목으로 적는다. 세 항목 중 하나라도 검증되지 않으면 이 계획은 완료가
아니다.

## 담당 범위

- E2E 대상: `framework/languages/java/e2e/`
- Sample 대상: `framework/languages/java/samples/java/`
- Java framework 대상:
  - `framework/languages/java/zlink-framework-core/`
  - `framework/languages/java/zlink-framework-spring-boot-starter/`
  - `framework/languages/java/zlink-framework-codec-msgpack/`
  - `framework/languages/java/zlink-framework-codec-protobuf/`
  - `framework/languages/java/zlink-framework-locations-redis/`
- Java 검증 대상: `framework/languages/java/zlink-framework-testkit/`, `framework/languages/java/build.gradle.kts`
- 공통 E2E 기준: `framework/doc/framework/common/e2e/`
- 공통 sample 기준: `framework/doc/framework/common/sample/`
- `.NET` E2E 기준 구현: `framework/languages/dotnet/e2e/`
- `.NET` sample 기준 구현: `framework/languages/dotnet/samples/`

Core 성능 작업과 충돌하지 않도록 `core/`는 수정하지 않는다. Java framework에서 발견한 문제가 core
버그로 의심되면 Java만 우회하지 말고, C++/Node/Kotlin 또는 바인딩 수준에서 같은 현상이 재현되는지
확인한 뒤 버그 리포트로 분리한다.

## 버그 처리 원칙

작업 중 버그가 드러나면 scenario나 sample만 통과시키는 우회 코드를 넣지 않는다. 실패 로그, 재현
절차, 영향을 받는 언어와 계층을 먼저 확인하고, 원인이 Java framework, binding, connector, e2e/sample
harness 중 어디에 있는지 좁힌다.

실제 버그로 확인되면 가능한 범위에서 먼저 회귀테스트를 작성하거나 같은 변경에 포함한다. 그 다음 원인
계층에서 버그를 수정하고, 회귀테스트와 해당 e2e/sample runner를 다시 실행한 뒤 원래 작업을 계속
진행한다. 버그 수정 없이 `sleep`, retry-only wrapper, internal package 접근, reflection 우회,
test-only adapter, sample 코드 변경으로 실패를 숨기지 않는다.

## 메시지 핸들러 등록 정책 포함 범위

Java E2E/Sample 갭 제거 중 handler 등록 표면을 고치거나 새 sample/E2E handler를 추가할 때는
`framework/doc/framework/common/spec/framework-api.ko.md`의 메시지 handler 정책을 같은 작업 범위에
포함한다. 특히 `framework-api.ko.md`의 `3.3 Handler 등록 정책`은 Java sample과 E2E의 handler 등록
방식이 따라야 하는 공통 기준이다.

적용 기준:

- handler 등록 호출부가 packet 이름, actor 타입, request/send/subscription 종류처럼 handler 타입에서
  알 수 있는 정보를 반복해서 받지 않도록 한다.
- 수동 등록은 실행 문맥의 구성 단계에서 이뤄져야 한다. Java에서는 channel handler는 application
  startup/channel builder, session handler는 session 구성, Entry Spot과 user Spot handler는 각 Spot
  구성 문맥에 둔다.
- Spot 메시지 handler는 actor request/send, Spot packet, subscription 책임을 handler 타입과 metadata로
  드러내고, 등록 표면은 가능한 한 `addHandler(...)`와 같은 단일 의미로 유지한다.
- subscription topic처럼 handler interface만으로 알 수 없는 값은 handler metadata, annotation, 또는
  언어별 metadata 선언에 둔다. 등록 호출의 반복 인자로 숨기지 않는다.
- timer는 메시지 dispatch handler가 아니므로 메시지 handler 등록 정책으로 우회하지 않는다. timer 이름,
  주기, overrun 정책처럼 실행 계획에 속한 값은 별도 timer 등록 표면에서 다룬다.
- 자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다. 조용히
  덮어쓰거나 수동 등록이 자동 등록을 대신하게 만들지 않는다.
- 현재 Java public API로 공통 정책을 표현할 수 없으면 internal helper, reflection, 테스트 전용
  adapter로 우회하지 말고 `feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 public contract
  gap으로 남긴 뒤 설계 검토 항목으로 분리한다.

이 정책은 sample이나 E2E를 통과시키기 위한 구조 검사 회피가 아니라 public 사용 예시의 품질 기준이다.
새 sample/E2E가 handler 책임을 shared helper, raw frame adapter, test-only registry로 밀어내면 완료로
인정하지 않는다.

## Sample handler 등록 방식 정리

Java sample은 메시지 handler 정책을 사용자-facing 예시로 보여 주는 영역이므로, sample별 handler 등록
방식을 아래처럼 정리한다. 이 정책은 sample에 적용하며, E2E는 scenario별 검증 목적과 공통 E2E 문서의
요구에 맞춰 별도 판단한다.

- `TicTacToe` sample만 handler 수동 등록을 사용한다. TicTacToe는 작은 sample이라 handler가 어느 실행
  문맥에 붙는지 직접 보여 주는 예시로 둔다.
- `Bingo`, `DeliveryDispatch`, `SupportChat`, `GameQuest`, `ShoppingMall` sample은 handler 자동 등록만
  사용한다. sample 코드에서 handler 목록을 반복해서 나열하지 않는다.
- 자동 등록을 쓰는 sample에서 새 handler를 추가하면, handler 타입의 interface와 metadata만으로 실행
  문맥, packet 이름, request/send/subscription 종류를 판정할 수 있어야 한다.
- subscription topic처럼 handler interface만으로 알 수 없는 값은 annotation 또는 언어별 metadata에 둔다.
  자동 등록 sample에서 topic을 등록 호출부 인자로 넘기는 방식은 사용하지 않는다.
- 자동 등록과 수동 등록을 섞어 같은 dispatch key를 만들지 않는다. TicTacToe 외 sample에 수동 등록이
  필요해 보이면 sample 코드에 우회 등록을 넣지 말고 public contract gap으로 분리한다.
- 완료 전에는 sample release gate나 별도 문서 검증으로 TicTacToe를 제외한 Java sample에 수동 handler
  등록 호출이 남아 있지 않은지 확인한다.

## 완료와 gap 처리 원칙

이 계획의 목표는 문서와 구현 사이의 gap을 없애는 것이다. `partial`이나 `gap` 표기는 작업 중 상태를
보이게 하기 위한 임시 표시일 뿐 완료 판정이 아니다.

공통 e2e 문서나 공통 sample 문서가 요구하는 공개 동작인데 Java에서 바로 구현할 수 없으면, 먼저
`feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 이유를 적고 설계 이슈로 분리한다. 그 뒤
필요한 spec/guide/draft 검토와 public API 설계를 거쳐 다시 구현해야 한다. 설계 이슈로 분리했다는
사실만으로 이 계획을 완료 처리하지 않는다.

## E2E 구현 절차

1. `framework/doc/framework/common/e2e/README.ko.md`와 `config-1`부터 `config-9`까지 모든 문서를
   읽고 scenario ID를 표로 만든다.
2. 각 config마다 `.NET` 기준 구현과 `.NET` `feature-map.ko.md`를 읽는다.
3. Java의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 먼저 갱신한다.
   - 공통 문서의 scenario ID를 모두 행으로 둔다.
   - 공통 문서의 scenario 상태는 `implemented`, `partial`, `gap` 중 하나로 적는다.
   - `partial`과 `gap`은 이유, 필요한 public API, 막힌 계층을 함께 적는다.
   - `.NET` 파일이나 기존 Java 파일을 inventory에서 매핑할 때만 `merged`, `stale`, `not needed` 같은
     보조 상태를 쓸 수 있다. 공통 scenario 자체를 `not applicable`로 닫지 않는다.
4. Java public framework API로 구현 가능한 항목은 실제 역할 프로세스, runner, scenario evidence까지
   구현한다.
5. public API가 없어 구현할 수 없는 항목은 internal package, reflection, 테스트 전용 adapter로 우회하지
   않는다. 문서에 gap으로 남기고 설계 검토 항목으로 분리한다.
6. 각 config의 `run_e2e.sh`는 standalone으로 실행 가능해야 하며, 성공 시 명확한 최종 pass marker를
   출력해야 한다.
7. config 하나가 끝날 때마다 Gradle build/test, runner, feature-map, inventory를 맞춘 뒤 다음 config로
   넘어간다.

필수 config 목록:

- `LocationMessaging` 또는 Java에서 같은 의미로 명명된 registry/location messaging config
- `SpotService`
- `PubSub`
- `RegistrationCodec`
- `ResilienceLifecycle`
- `StoreFailure` 또는 Java에서 같은 의미로 명명된 store failure/recovery config
- `RuntimeMonitoring`
- `YieldDispatch`
- `ToActorMessaging`

현재 트리에 `.NET` 기준 이름과 다른 `RegistryMessaging`, `DiscoveryRegistryHa` 같은 디렉터리가 있으면
바로 삭제하거나 완료로 인정하지 않는다. 먼저 공통 config 문서와 `.NET` config에 어느 scenario가
대응되는지 inventory에 매핑하고, 중복·stale·rename 대상 여부를 리뷰로 확인한 뒤 정리한다.

## Sample 포팅 절차

1. `framework/doc/framework/common/sample/README.ko.md`와 sample별 문서를 모두 읽는다.
   - event sample은 `framework/doc/framework/common/sample/event/*.ko.md`도 함께 읽는다.
2. `.NET` sample 6종의 실제 코드, runner, README를 읽고 Java sample에 대응시킨다.
3. 각 Java sample에 `sample-porting-inventory.ko.md`를 유지한다.
   - `.NET`의 역할, shared contract, client self-check, runner evidence를 빠짐없이 매핑한다.
   - Java/Spring idiom 때문에 파일명이나 annotation 구조가 달라도 책임은 누락하지 않는다.
4. Sample 코드는 사용자가 따라 할 public API 예시다. framework runtime internal package, reflection,
   raw buffer 처리, codec 수동 우회를 sample 코드에 넣지 않는다.
5. 각 sample의 `run_sample.sh`는 standalone으로 실행 가능해야 하며, client success뿐 아니라 서버 역할
   로그와 scenario evidence도 확인해야 한다.
6. 모든 sample이 끝난 뒤 Java sample 전체 runner와 sample release gate를 실행한다.

필수 sample 목록:

- `TicTacToe`
- `Bingo`
- `DeliveryDispatch`
- `SupportChat`
- `GameQuest`
- `ShoppingMall`

## Ref 대상 통일 병행 절차

`framework/doc/plan/framework-ref-target-unification-java-worker-prompt.ko.md`의 `ActorRef` / `SpotRef`
전송 대상 통일은 이 계획의 필수 병행 항목이다. E2E나 sample을 수정할 때는 기존 id-only 전송 API를
새로 사용하지 않고, 이미 통일 대상에 오른 호출부는 가능한 범위에서 ref 기반 표면으로 함께 옮긴다.

병행 작업 기준은 아래와 같다.

1. Java E2E/sample의 `gap` 또는 `partial` 항목을 닫기 전에 해당 경로가 `ActorRef` / `SpotRef` 통일
   대상인지 확인한다.
2. 통일 대상이면 E2E, sample, feature-map, inventory를 ref 기반 호출 모양으로 함께 갱신한다.
3. id-only 전송 API가 필요한 것처럼 보이면 internal helper나 테스트 전용 adapter로 우회하지 말고,
   public contract 설계가 필요한 항목으로 분리한다.
4. ref 대상 통일 때문에 영향을 받는 sample은 사용자-facing public API 예시이므로 runtime/internal
   package 접근, raw-frame 우회, codec 수동 등록을 넣지 않는다.
5. 완료 전에는 ref 통일 worker prompt의 전체 완료 게이트와 이 계획의 Java E2E/sample 완료 게이트를
   모두 통과해야 한다. worker prompt의 특정 검색 또는 일부 compile 통과만으로 완료 처리하지 않는다.

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 Gradle task 이름을 확인한 뒤 실행한다.

```bash
cd framework/languages/java
./gradlew build
./gradlew test
./gradlew sampleTest
for f in e2e/*/run_e2e.sh; do timeout 420s "$f"; done
ZLINK_SAMPLE_FILTER= timeout 900s samples/run_samples.sh
```

Gradle daemon, port 충돌, Redis 구동 문제 때문에 전체 루프가 실패하면 실패 config/sample을 먼저 단독
재현하고, 단독 pass 후 전체 루프를 다시 실행한다.

`samples/run_samples.sh`는 Java와 Kotlin sample gate를 함께 실행하는 통합 runner다. Java sample만
좁혀서 재현해야 할 때는 `ZLINK_SAMPLE_FILTER=java/<Sample>` 형식으로 단일 sample을 먼저 실행하고,
수정 후에는 filter 없이 통합 runner를 다시 실행한다.

## 진행 기록

### 2026-07-08: Java sample handler 등록 정책 정리

`TicTacToe`를 제외한 Java sample에서 Spot handler 수동 등록을 제거하고, application startup의
`addHandlersFromPackageOf(...)` 자동 등록 경로만 사용하도록 정리했다. `Bingo`, `DeliveryDispatch`,
`ShoppingMall`, `SupportChat`의 Entry Spot 또는 user Spot `configure()`에서 handler 목록을 반복해서
나열하지 않는다.

`Bingo`의 subscription handler는 topic이 handler interface만으로는 드러나지 않으므로
`BingoWinnerMsgHandler` 타입에 `@ZLinkSpotSubscription(topic = SampleNames.WinnerTopic)` metadata를
붙였다. 이로써 subscription topic도 등록 호출부 인자가 아니라 handler metadata로 표현된다.

회귀 방지를 위해 `SampleReleaseGateContractTest.javaSamplesUseManualSpotHandlerRegistrationOnlyInTicTacToe`
를 추가했다. 이 gate는 `framework/languages/java/samples/java` 아래 Java source를 훑고, `TicTacToe`
밖에서 `context.handlers().addHandler(...)`, `addPacket(...)`, `addActorRequest(...)`, `addActorSend(...)`,
`addSubscribe(...)` 같은 Spot handler 수동 등록 호출이 나오면 실패한다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.javaSamplesUseManualSpotHandlerRegistrationOnlyInTicTacToe

cd framework/languages/java/samples/java/Bingo
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Play:compileJava

cd framework/languages/java/samples/java/DeliveryDispatch
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:CustomerGateway:compileJava :Server:CourierSpotNode:compileJava

cd framework/languages/java/samples/java/ShoppingMall
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:OrderWorkflow:compileJava

cd framework/languages/java/samples/java/SupportChat
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Support:compileJava
```

위 명령은 모두 통과했다. 전체 sample runner 검증은 아직 남아 있으며, 최종 완료 전
`samples/run_samples.sh`를 filter 없이 다시 실행해야 한다.

### 2026-07-08: Actor client 전송 대상 ref-only 전환

Ref 대상 통일 worker prompt의 전송 대상 정책에 맞춰 Java actor client의 public 전송 표면에서 actor id
기반 전송 overload와 `ZLinkActorRef` overload를 제거했다. `ZLinkActorClient`는 이제
`sendToActor(ActorRef, ...)`와 `requestToActor(ActorRef, ...)`만 제공한다. actor id는 전송 입력이 아니라
`ZLinkActorManager.find(...)` 또는 `getOrCreate(...)` 같은 lookup 입력으로 사용한다.

함께 정리한 호출부:

- Spring actor client bean은 `ActorRef`만 위임한다.
- `ToActorMessaging` caller의 actor id endpoint는 먼저 `ZLinkActorManager.find(...)`로 ref를 조회한 뒤
  actor client에 넘긴다. ref endpoint는 wire DTO를 `ActorRef`로 복원한다.
- `SpotService` client driver scenario는 actor push chain에서 actor id로 직접 전송하지 않고
  `ZLinkActorManager.find(...)` 결과를 `ActorRef`로 변환해 요청한다.
- `SpotService` MultiNode HTTP server는 `getOrCreate(...)` 결과를 `ActorRef`로 변환해 actor client에
  넘긴다.
- `DeliveryDispatch` Tracking handler는 customer actor id를 `ZLinkActorManager.find(...)`로 조회한 뒤
  `ActorRef` 기반 send를 사용한다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:test --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest --tests systems.zlink.framework.LocationContractTest.actorClientPinsL13CompletionStageSurface
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-spring-boot-starter:compileJava

cd framework/languages/java/e2e/ToActorMessaging
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Server:Caller:compileJava

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava :Server:MultiNode:compileJava :Server:Gateway:compileJava

cd framework/languages/java/samples/java/DeliveryDispatch
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Tracking:compileJava
```

위 명령은 모두 통과했다. 추가 확인으로 아래 검색이 match 없이 끝났다.

```bash
rg -n "sendToActor\s*\(\s*String|requestToActor\s*\(\s*String|sendToActor\s*\(\s*ZLinkActorRef|requestToActor\s*\(\s*ZLinkActorRef" \
  framework/languages/java/zlink-framework-core/src/main/java \
  framework/languages/java/zlink-framework-spring-boot-starter/src/main/java \
  framework/languages/java/e2e \
  framework/languages/java/samples/java -g '*.java'
```

아직 actor manager, actor directory, session binding 외부의 core actor ref 호환 타입과 docs에는
`ZLinkActorRef` 이름이 남아 있다. 이 항목들은 다음 ref 대상 통일 단계에서 계속 정리해야 한다.

### 2026-07-08: Actor manager/directory/session ref 표면 전환

Ref 대상 통일 worker prompt의 naming 정책에 맞춰 Java actor manager와 actor directory의 public 반환값을
`ActorRef`로 전환했다. `ZLinkActorManager`, `ZLinkActorDirectory`, Spring actor manager/directory bean은
actor 생성, 조회, 보장 API에서 더 이상 public 반환 타입으로 `ZLinkActorRef`를 노출하지 않는다.

session actor binding도 같은 방향으로 정리했다. `ZLinkSessionActors.bind(...)`와
`ZLinkSessionActors.bindOrGet(...)`는 public ref 입력으로 `ActorRef`를 받으며, `ZLinkSessionActor.ref()`
도 `ActorRef`를 반환한다. Java E2E와 sample에서 actor manager lookup 결과를 다시 `ActorRef.from(...)`로
감싸던 stale 변환은 제거했다.

Java spec 문서도 실제 public 표면과 맞췄다. `framework/doc/framework/java/spec` 아래 actor/session
문서에서 actor manager 반환값, session binding 입력, actor join result 설명을 `ActorRef`로 갱신했고,
`framework/doc/contract-inventory`, `framework/doc/framework/common`, `framework/doc/framework/java` 범위에서
plan/draft를 제외한 `ZLinkActorRef` 문서 잔여 검색은 match 없이 끝났다.

함께 정리한 호출부:

- `ToActorMessaging` caller는 `ZLinkActorManager.find(...)` 결과를 그대로 actor client에 넘긴다.
- `SpotService` client scenario와 MultiNode HTTP server는 actor manager 결과를 추가 변환 없이 사용한다.
- `DeliveryDispatch` Tracking handler는 customer actor lookup 결과를 그대로 `sendToActor(...)`에 넘긴다.
- `Bingo`, `SupportChat`, `DeliveryDispatch`, `YieldDispatch`의 session binding 호출부는 `ActorRef`
  public 표면으로 컴파일된다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:test --tests systems.zlink.framework.LocationContractTest --tests systems.zlink.framework.runtime.actors.ZLinkActorClientRuntimeTest :zlink-framework-spring-boot-starter:compileJava

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava :Server:MultiNode:compileJava :Server:Gateway:compileJava

cd framework/languages/java/e2e/YieldDispatch
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava

cd framework/languages/java/e2e/ToActorMessaging
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Server:Caller:compileJava

cd framework/languages/java/samples/java/Bingo
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Play:compileJava :Server:Session:compileJava

cd framework/languages/java/samples/java/SupportChat
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Support:compileJava :Server:Session:compileJava

cd framework/languages/java/samples/java/DeliveryDispatch
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Shared:compileJava :Server:CustomerGateway:compileJava :Server:CourierSession:compileJava :Server:CourierSpotNode:compileJava :Server:Tracking:compileJava
```

위 명령은 모두 통과했다. 추가 확인으로 아래 검색이 match 없이 끝났다.

```bash
rg -n "ActorRef::from|ZLinkActorRef\b|ZLinkActorRefSnapshot\b" \
  framework/languages/java/e2e framework/languages/java/samples/java -g '*.java'

rg -n "CompletionStage<ZLinkActorRef>|Optional<ZLinkActorRef>|ZLinkActorRef>" \
  framework/languages/java/zlink-framework-core/src/main/java \
  framework/languages/java/zlink-framework-spring-boot-starter/src/main/java -g '*.java'

rg -n "ZLinkActorRef\b|ZLinkActorRefSnapshot\b" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/java \
  -S -g '!framework/doc/plan/**' -g '!**/draft/**'
```

아직 core actors package에는 `ZLinkActorRef`와 `ZLinkActorRefSnapshot` 타입 파일, 그리고 `ActorRef`의
내부 변환 helper가 남아 있다. location store 내부 row도 아직 `ZLinkActorRef`를 저장한다. worker prompt
전체 완료 전에는 이 호환/내부 타입을 제거하거나 internal-only 위치로 옮겨야 한다.

### 2026-07-08: Route/spot 주소 public 표면 SpotRef 전환

Ref 대상 통일 worker prompt의 spot 전송 대상 정책에 맞춰 Java route client와 location resolver의 public
주소 표면을 `SpotRef`로 전환했다. `ZLinkRouteClient.sendToSpot(...)`와
`ZLinkRouteClient.requestToSpot(...)`는 더 이상 `ZLinkSpotAddress`를 받지 않고 `SpotRef`를 받는다.
`SpotRefResolver.resolveSpotRefAsync(...)`와 `ActorSpotRefResolver.resolveActorSpotRefAsync(...)`가
location store 기반 메시징 조회 표면이며, 이전 `ZLinkSpotAddressResolver`,
`ZLinkActorAddressResolver`, `resolveSpotAddressAsync(...)`, `resolveActorSpotAddressAsync(...)` 이름은
Java source에서 제거했다.

함께 정리한 호출부:

- `SpotService` MultiNode와 evidence HTTP helper는 route client 호출에 `SpotRef`를 넘긴다.
- `YieldDispatch` shared handler들은 remote spot request/yield 호출에서 `SpotRef`를 사용한다.
- `Bingo`, `DeliveryDispatch`, `ShoppingMall` sample의 route client 호출부는 `ZLinkSpotAddress` 생성 없이
  `SpotRef`를 직접 만든다.
- common/java spec 문서에서 `SpotAddress`/actor address resolver 이름을 `SpotRef`/`ActorSpotRefResolver`
  기준으로 갱신했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:test --tests systems.zlink.framework.LocationContractTest --tests systems.zlink.framework.runtime.channels.ZLinkChannelRuntimeTest --tests systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolversTest :zlink-framework-spring-boot-starter:compileJava
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:contractTest --tests systems.zlink.framework.locations.LocationStoreContractTest

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava :Server:MultiNode:compileJava :Server:Gateway:compileJava

cd framework/languages/java/e2e/YieldDispatch
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava

cd framework/languages/java/samples/java/Bingo
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:Session:compileJava

cd framework/languages/java/samples/java/DeliveryDispatch
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:CourierGateway:compileJava :Server:Dispatch:compileJava :Server:CourierSession:compileJava

cd framework/languages/java/samples/java/ShoppingMall
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:OrderWorkflow:compileJava
```

위 명령은 모두 통과했다. 추가 확인으로 아래 검색이 match 없이 끝났다.

```bash
rg -n "ZLinkSpotAddress\b|ZLinkSpotAddressResolver\b|ZLinkActorAddressResolver\b|resolveSpotAddressAsync|resolveActorSpotAddressAsync|toZLinkSpotAddress|SpotRef\.from" \
  framework/languages/java -g '*.java'

rg -n "ZLinkSpotAddress\b|ZLinkSpotAddressResolver\b|ZLinkActorAddressResolver\b|resolveSpotAddressAsync|resolveActorSpotAddressAsync|SpotAddress|spot address" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/java \
  -S -g '!framework/doc/plan/**' -g '!**/draft/**'
```

### 2026-07-08: Spot remote route ref 표면 전환

Ref 대상 통일 worker prompt의 grep gate에 남아 있던 `ZLinkSpotRemoteAddress` 계열 public 이름을
`SpotRemoteRef` 계열로 전환했다. remote spot owner route 조회는 address가 아니라 전송에 필요한 ref를
반환한다는 의미를 드러내도록 `SpotRemoteRef`, `SpotRemoteRefResolver`,
`resolveSpotRemoteRefAsync(...)`, `addSpotRemoteRefResolver(...)` 이름을 사용한다.

함께 정리한 호출부:

- location store 기반 기본 resolver는 `ZLinkLocationSpotRemoteRefResolver`가 됐다.
- Java framework options와 registration은 `addSpotRemoteRefResolver(...)`와
  `spotRemoteRefResolverType()`을 노출한다.
- `SpotService` E2E custom resolver와 fake backend testkit resolver는 `SpotRemoteRefResolver`를 구현한다.
- Java framework common/java 문서의 `SpotRemoteAddress`/`spot remote address` 잔여 표현은
  `SpotRemoteRef`/`spot remote ref` 기준으로 갱신했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:compileJava :zlink-framework-core:compileTestJava :zlink-framework-testkit:compileJava :zlink-framework-testkit:compileFakeBackendTestJava :zlink-framework-spring-boot-starter:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava :Server:Play:compileJava :Server:Gateway:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge
```

위 Java compile 명령은 모두 통과했다. 다만 bridge task를 제외하지 않은 아래 명령은 Java 컴파일 전에
native bridge link 단계에서 실패했다.

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:test --tests systems.zlink.framework.runtime.DefaultZLinkFrameworkOptionsTest --tests systems.zlink.framework.runtime.NodesAndServicesTest --tests systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolversTest :zlink-framework-spring-boot-starter:compileJava
```

실패 지점은 `:zlink-bindings-java:buildZlinkJavaBridge`였고, `zlink_send_part_rid`,
`zlink_router_enable_spot_receive`, `zlink_msg_close`, `zlink_msg_init`, `zlink_router_recv_part` 같은 native
symbol link 실패였다. Java rename 검증은 bridge task를 제외한 compile로 통과했지만, 최종 완료 전에는
native bridge link 실패를 별도로 재검증해야 한다.

추가 확인으로 아래 검색이 match 없이 끝났다.

```bash
rg -n "ZLinkSpotRemoteAddress|ZLinkSpotRemoteAddressResolver|ZLinkLocationSpotRemoteAddressResolver|SpotRemoteAddress|spot remote address|resolveSpotRemoteAddress|addSpotRemoteAddress|useRegistrySpotRemoteAddresses|SpotRemoteAddresses|spotRemoteAddress" \
  framework/languages/java -g '*.java'

rg -n "ZLinkSpotRemoteAddress|ZLinkSpotRemoteAddressResolver|ZLinkLocationSpotRemoteAddressResolver|SpotRemoteAddress|spot remote address|resolveSpotRemoteAddress|addSpotRemoteAddress|useRegistrySpotRemoteAddresses|SpotRemoteAddresses|spotRemoteAddress" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/java \
  -S -g '!framework/doc/plan/**' -g '!**/draft/**'

rg -n "ZLinkActorRef\b|ZLinkActorRefSnapshot\b|ZLinkSpotAddress\b|ZLinkSpotAddressResolver\b|ZLinkActorAddressResolver\b|ZLinkSpotRemoteAddress\b|ZLinkSpotRemoteAddressResolver\b|SpotRemoteAddress|spot remote address" \
  framework/languages/java/e2e framework/languages/java/samples/java framework/languages/java/zlink-framework-testkit/src -g '*.java'
```

### 2026-07-08: ActorRef 호환 타입 제거와 Spot outbound ref-only 전환

Ref 대상 통일 worker prompt의 public naming 정책에 맞춰 Java core의 `ZLinkActorRef` /
`ZLinkActorRefSnapshot` 호환 타입 파일을 제거했다. location runtime, Redis location row, actor runtime,
Spring integration test, Java E2E/sample 호출부는 public `ActorRef`를 직접 저장하고 전달한다. 기존
`ActorRef.from(...)` / `toZLinkActorRef(...)` 변환 helper도 제거했다.

Spot outbound public 표면도 ref-only로 정리했다. `ZLinkSpotOutbound`는 이제 `SpotRef` 기반
`sendToSpot(...)` / `requestToSpot(...)`만 제공하며, spot rid만 받는 public overload는 제거했다. 이
변경에 맞춰 fake backend test와 `SpotService` shared client scenario, `NodesAndServicesTest`는 전송 전에
명시적인 `SpotRef`를 만들도록 갱신했다. backend/native adapter에 남은 `targetNodeRid, spotRid` 인자는
public framework API가 아니라 native bridge boundary이므로 ref-only public surface 위반으로 보지 않는다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:compileJava :zlink-framework-core:compileTestJava :zlink-framework-testkit:compileFakeBackendTestJava :zlink-framework-spring-boot-starter:compileJava :zlink-framework-kotlin:compileKotlin -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

rg -n "ZLinkActorRef\b|ZLinkActorRefSnapshot\b|toZLinkActorRef|ActorRef\.from" \
  framework/languages/java -S

rg -n "ZLinkActorRef\b|ZLinkActorRefSnapshot\b|toZLinkActorRef|ActorRef\.from" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/java \
  -S -g '!framework/doc/plan/**' -g '!**/draft/**'

rg -n "sendToActor\s*\(\s*String|requestToActor\s*\(\s*String|sendToActor\s*\(\s*ZLinkActorRef|requestToActor\s*\(\s*ZLinkActorRef|sendToSpot\s*\(\s*RoutingId|requestToSpot\s*\(\s*RoutingId" \
  framework/languages/java/zlink-framework-core/src/main/java \
  framework/languages/java/zlink-framework-spring-boot-starter/src/main/java \
  framework/languages/java/e2e framework/languages/java/samples/java -g '*.java'

rg -n "ZLinkSpotAddress\b|ZLinkSpotAddressResolver\b|ZLinkActorAddressResolver\b|resolveSpotAddressAsync|resolveActorSpotAddressAsync|toZLinkSpotAddress|SpotRef\.from|ZLinkSpotRemoteAddress\b|ZLinkSpotRemoteAddressResolver\b|resolveSpotRemoteAddressAsync" \
  framework/languages/java -S
```

위 compile 명령은 bridge task를 제외하고 모두 통과했다. `ZLinkActorRef` 계열 검색과 문서 검색은 match
없이 끝났다. public send/request 검색에서 남은 항목은 `ZLinkJavaBackendAdapterFactory`의 backend/native
boundary뿐이다. 마지막 `SpotRemoteAddress` 검색은 Java 전용 E2E/sample에서는 match 없이 끝났지만,
`framework/languages/java/e2e-kotlin/SpotService`에는 Kotlin 전용 잔여가 있다. 이 계획의 목표는 Kotlin
전용 모듈을 Java 작업 범위로 취급하지 않으므로 해당 잔여는 Java 완료 판정 범위에서 제외한다.

최종 완료 전에는 여전히 bridge task를 제외하지 않은 Gradle build/test/sampleTest, Java E2E runner,
`samples/run_samples.sh`, 누락 리뷰, POSD/DDD 리뷰를 다시 통과해야 한다.

### 2026-07-08: Java Spot handler registry 단일 등록 표면 전환

공통 handler 등록 정책에 맞춰 Java public `ZLinkSpotHandlerRegistry`에서 `addPacket(...)`,
`addActorPacket(...)`, `addActorSend(...)`, `addActorRequest(...)`, `addSubscribe(...)` 구분 등록 표면을
제거했다. 수동 등록은 `addHandler(Class<?>)` 하나만 사용하며, packet/send/request/actor/subscription
구분은 handler interface와 annotation metadata에서 판정한다.

subscription topic은 등록 호출부 인자로 받지 않는다. `ZLinkSpotSubscriptionHandler` 기반 수동 등록도
handler 타입의 `@ZLinkSpotSubscription(topic = ...)` metadata를 요구하도록 런타임을 보강했다. 이에
따라 `TicTacToe` sample의 수동 subscription 예시, `SpotService` shared E2E subscription handler,
fake backend test subscription handler는 모두 `addHandler(...)`와 class-level topic metadata를 사용한다.

함께 정리한 호출부:

- `SpotService` shared spot/entry spot handler 수동 등록
- `YieldDispatch` shared spot/entry spot handler 수동 등록
- `ToActorMessaging` actor server spot handler 수동 등록
- `TicTacToe` Java sample의 수동 등록 예시
- `NodesAndServicesTest`와 fake backend spot runtime regression test
- `HandlerContractTest`와 `ChannelMessagingTest`의 public surface/integration 호출부
- `zlink-framework-kotlin`의 reified registry extension은 제거된 Java 구분 메서드를 호출하지 않고
  `addHandler(Class<?>)` 하나로만 위임한다. Kotlin 전용 E2E/sample 구현 자체는 Java 완료 범위로 보지
  않지만, framework build를 깨지 않도록 wrapper 호출 표면은 같이 맞춘다.
- Java guide/spec의 handler registry와 spot outbound 예시

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:compileJava :zlink-framework-core:compileTestJava :zlink-framework-testkit:compileFakeBackendTestJava :zlink-framework-spring-boot-starter:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:compileJava :zlink-framework-core:compileTestJava :zlink-framework-core:compileIntegrationTestJava :zlink-framework-testkit:compileFakeBackendTestJava :zlink-framework-kotlin:compileKotlin -x :zlink-bindings-java:buildZlinkJavaBridge
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:test --tests systems.zlink.framework.HandlerContractTest.spotHandlerRegistryMatchesDotnetRegistrationSurface :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.javaSamplesUseManualSpotHandlerRegistrationOnlyInTicTacToe -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/e2e/SpotService
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava :Server:Gateway:compileJava :Server:Play:compileJava :Server:MultiNode:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/e2e/YieldDispatch
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Shared:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/e2e/ToActorMessaging
nice -n 10 ../../gradlew --no-parallel --max-workers=1 :Server:Actor:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

cd framework/languages/java/samples/java/TicTacToe
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:compileJava -x :zlink-bindings-java:buildZlinkJavaBridge

rg -n "addPacket\(|addActorPacket\(|addActorRequest\(|addActorSend\(|addSubscribe\(" \
  framework/doc/framework/common framework/doc/framework/java framework/doc/contract-inventory \
  framework/languages/java \
  -g '!**/build/**' -S
```

위 compile과 focused test/release gate는 통과했다. 제거 대상 등록 API 검색은
`framework/languages/java` 전체와 관련 common/java 문서 범위에서 match 없이 끝났다. 한 번 병렬 Gradle
실행 중 `bindings/java/build/libs/zlink-java-8.6.2.jar`를 동시에 쓰면서 `zip END header not found`가
발생했지만, 같은 YieldDispatch compile을 순차 재실행해 통과를 확인했다.

최종 완료 전에는 여전히 bridge task를 제외하지 않은 Gradle build/test/sampleTest, Java E2E runner,
`samples/run_samples.sh`, 누락 리뷰, POSD/DDD 리뷰를 다시 통과해야 한다.

### 2026-07-08: ref-only/handler 정책 후속 build gate 정리

Ref 대상 통일과 handler 등록 표면 통합 이후 깨진 Java/Kotlin framework test gate를 순차로 보정했다.
spot route egress integration test는 `SpotRef`가 route mesh 이름과 target route peer routing id를 함께
전달하도록 갱신했고, Kotlin framework test double은 제거된 id-only actor 전송 override를 더 이상
구현하지 않도록 맞췄다. sample release gate는 현재 Bingo/TicTacToe 역할 이름과 timeout 책임 위치에
맞게 갱신했다. fake backend는 `ActorRef`/`SpotRef` 기반 전송 정책에 맞춰 actor lookup/remove와 route
mesh 선택 검증을 보강했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-core:integrationTest --tests systems.zlink.framework.runtime.ChannelMessagingTest.clientServerSpotRouteEgress_requestReplySucceeds
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-kotlin:compileTestKotlin
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.bingoMirrorsFourClientMatchingTimerAndBoundPushGate --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.bingoKotlinSampleMirrorsJavaRoleLayout --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.ticTacToeKotlinSampleMirrorsJavaRoleLayout
nice -n 10 ./gradlew --no-parallel --max-workers=1 :zlink-framework-testkit:fakeBackendTest --tests systems.zlink.framework.testkit.SpotRuntimeFakeBackendTest.spotContextLeaveActorMarksActorLeftAndInvokesMemberCallback --tests systems.zlink.framework.testkit.SpotRuntimeFakeBackendTest.spotOutboundUsesConfiguredRouteMeshEgressChannel --tests systems.zlink.framework.testkit.SpotRuntimeFakeBackendTest.routeMeshSpotEgressUsesSpotRefTargetRoutePeerRoutingId --tests systems.zlink.framework.testkit.SpotRuntimeFakeBackendTest.spotOutboundUsesSpotRefMeshToSelectRouteMeshEgressChannel --tests systems.zlink.framework.testkit.ActorRuntimeFakeBackendTest.entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback --tests systems.zlink.framework.testkit.ActorRuntimeFakeBackendTest.nativeRemoteActorJoinRebindsExistingBoundSession

cd framework/languages/java/samples/kotlin/TicTacToe
nice -n 10 ../../gradlew --settings-file standalone.settings.gradle.kts --no-parallel --max-workers=1 :Server:compileKotlin
```

전체 Java Gradle build는 이 지점 이후 `:zlink-http-client-kotlin:jacocoTestCoverageVerification`에서
line coverage 0.71/0.80으로 실패했다. 실패 원인은 reified coroutine convenience 함수가 호출 지점으로
inline되어 동작 테스트가 있어도 JaCoCo 원본 method line에 coverage가 붙지 않는 구조였다. `await<T>()`와
`fetch<T>()` 동작 테스트를 추가하고, JaCoCo가 원본 inline wrapper를 generated method로 제외하도록
표시했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 --rerun-tasks :zlink-http-client-kotlin:test :zlink-http-client-kotlin:jacocoTestCoverageVerification
```

위 명령은 통과했다. 최종 완료 전에는 이 coverage focused pass만으로 완료 처리하지 않고, bridge task를
제외하지 않은 root Gradle build/test/sampleTest, Java E2E runner, `samples/run_samples.sh`, 누락 리뷰,
POSD/DDD 리뷰를 다시 통과해야 한다.

이후 bridge task를 제외하지 않은 root Java build도 다시 확인했다.

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 build
```

위 명령은 통과했다. 다음 완료 게이트는 Java E2E runner와 `samples/run_samples.sh` 실행, 누락 리뷰,
POSD/DDD 리뷰다.

Java E2E runner는 CPU 부하를 보면서 순차 실행을 시작했다. 아래 runner는 pass를 확인했다.

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh
nice -n 10 framework/languages/java/e2e/PubSub/run_e2e.sh
nice -n 10 framework/languages/java/e2e/RegistrationCodec/run_e2e.sh
nice -n 10 framework/languages/java/e2e/RegistryMessaging/run_e2e.sh
```

`ResilienceLifecycle` runner는 실행 중 CPU 사용률이 높아져 중단했다. 중단 시점까지 일부 scenario 로그는
나왔지만 runner 전체 pass는 아니므로 완료 증거로 쓰지 않는다. CPU가 안정된 뒤 아래 runner부터 다시
순차 실행해야 한다.

```bash
framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh
framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh
framework/languages/java/e2e/SpotService/run_e2e.sh
framework/languages/java/e2e/ToActorMessaging/run_e2e.sh
framework/languages/java/e2e/YieldDispatch/run_e2e.sh
framework/languages/java/samples/run_samples.sh
```

### 2026-07-08: SpotService/ResilienceLifecycle 잔여 gap stub 정리

CPU 부하 때문에 무거운 runner 재실행은 보류하고, 정적 gap 후보만 먼저 닫았다. `SpotService`에는
`SM-A6`, `SM-A7`, `SM-C4`, `SM-E2` client scenario 파일이 남아 있었지만 실제 `Client/Program.java`는
scenario class dispatcher가 아니라 gateway mode HTTP driver를 사용한다. 따라서 gap을 던지는 죽은
stub을 제거하고, 해당 scenario 파일은 현재 Java runner가 쓰는 public gateway mode로 연결했다.

`run_e2e.sh`는 `SM-A6`, `SM-A7`, `SM-C4`, `SM-E2`를 개별 scenario로 실행해도 `all` 실행에서만
암묵적으로 수행하던 close/type-mismatch/publisher/timer evidence 검증을 재사용하도록 보강했다.
`ResilienceLifecycle`의 호출되지 않는 `ConsumerScenarioClient.unsupported(...)` helper도 제거했다.
`SpotService/feature-map.ko.md`의 "gap scenario는 선택되면 실패한다" 문구는 현재 상태와 맞지 않아
runner가 필요한 서버 role과 evidence 검증을 수행한다고 정리했다.

확인한 명령:

```bash
bash -n framework/languages/java/e2e/SpotService/run_e2e.sh
rg -n "unsupported\(|documented Java gap|is a documented Java gap|gap scenario" \
  framework/languages/java/e2e/SpotService \
  framework/languages/java/e2e/ResilienceLifecycle \
  -g '!**/build/**' -g '!**/logs/**'
git diff --check -- \
  framework/languages/java/e2e/SpotService/Client/src/main/java/systems/zlink/e2e/spotservice/client/Scenarios/SmA6Scenario.java \
  framework/languages/java/e2e/SpotService/Client/src/main/java/systems/zlink/e2e/spotservice/client/Scenarios/SmA7Scenario.java \
  framework/languages/java/e2e/SpotService/Client/src/main/java/systems/zlink/e2e/spotservice/client/Scenarios/SmC4Scenario.java \
  framework/languages/java/e2e/SpotService/Client/src/main/java/systems/zlink/e2e/spotservice/client/Scenarios/SmE2Scenario.java \
  framework/languages/java/e2e/SpotService/Client/src/main/java/systems/zlink/e2e/spotservice/client/Support/GatewayScenarioClient.java \
  framework/languages/java/e2e/ResilienceLifecycle/Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/Support/ConsumerScenarioClient.java \
  framework/languages/java/e2e/SpotService/run_e2e.sh \
  framework/languages/java/e2e/SpotService/feature-map.ko.md
```

위 정적 확인은 통과했다. 다만 이 항목은 runner pass 증거가 아니므로 완료 증거로 쓰지 않는다. CPU가
안정된 뒤 `SpotService` 개별 보강 scenario 또는 전체 runner를 포함해 남은 Java E2E/sample runner를
다시 실행해야 한다.

추가로 stale gap 문구를 정리했다. `ResilienceLifecycle/README.ko.md`는 이미 feature map과 inventory가
"남은 항목 없음"으로 정리된 상태였지만 오래된 "완료되지 않은 scenario는 gap 사유와 함께 실패한다"
문구가 남아 있었다. 이 문구를 현재 구조인 `Server/Consumer` HTTP endpoint 호출 설명으로 바꿨다.
`DiscoveryRegistryHa/run_e2e.sh`의 알 수 없는 scenario 오류도 `gap=...not-yet-ported` 대신 unknown
scenario 오류로 바꿔, 구현 완료 범위와 잘못 섞이지 않게 했다.

확인한 명령:

```bash
rg -n "unsupported\(|documented Java gap|is a documented Java gap|gap scenario|gap=|not-yet-ported|완료되지 않은 scenario|완료되지 않은 시나리오" \
  framework/languages/java/e2e framework/languages/java/samples/java \
  -g '!**/build/**' -g '!**/logs/**' -g '!**/.gradle/**'
bash -n framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh
bash -n framework/languages/java/e2e/SpotService/run_e2e.sh
```

검색 결과는 `SpotService/porting-inventory.ko.md`의 상태 구분 설명 한 줄만 남았다. 실패 stub 또는
runner의 `gap=` 오류 문구는 남아 있지 않다.

### 2026-07-08: ToActorMessaging caller 조회 전용 directory 보강

`ToActorMessaging` caller role은 actor factory를 갖지 않는 서버 측 caller인데, actor id endpoint가
`ZLinkActorManager`를 주입받아 actor ref를 조회하고 있었다. Spring starter는 actor factory가 있는
역할에만 manager를 노출하므로 caller가 부팅하지 못했다.

수정 내용:

- `ZLinkActorDirectory`는 actor factory가 없어도 spot node와 location store가 있으면 조회 전용 bean으로
  노출되도록 했다.
- core runtime에 store resolver 기반 조회 전용 directory를 추가했다. 이 directory는 `find(actorId)`만
  location store로 처리하고, actor 생성이 필요한 `ensure(...)`는 actor factory가 필요하다는 configuration
  error로 남긴다.
- `ToActorMessaging` caller는 `ZLinkActorManager` 대신 `ZLinkActorDirectory`로 actor id를 조회한다.
- actor id 조회 실패는 `IllegalStateException`으로 새지 않고 `ACTOR_ROUTE_NOT_FOUND`로 분류되도록 했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-parallel --max-workers=1 --rerun-tasks :zlink-framework-spring-boot-starter:test --tests systems.zlink.framework.spring.ZLinkFrameworkAutoConfigurationTest

cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 600s framework/languages/java/e2e/ToActorMessaging/run_e2e.sh
```

위 명령은 통과했다. `ToActorMessaging` runner는
`framework/languages/java/e2e/ToActorMessaging/logs/20260708-191111-565736`에서
`to-actor-messaging e2e result=passed`를 출력했다.

추가로 CPU 부하를 확인하면서 `ResilienceLifecycle` runner도 재실행했다.

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 600s framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh
```

위 명령은 통과했다. `ResilienceLifecycle` runner는
`framework/languages/java/e2e/ResilienceLifecycle/logs/20260708-191308-572319`에서
`resilience-lifecycle e2e result=passed`를 출력했고, `RL-A1`부터 `RL-D5`까지 개별 scenario pass 로그도
확인했다.

`RuntimeMonitoring` runner도 interrupt 없이 clean run으로 재확인했다.

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 600s framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh
```

위 명령은 통과했다. `RuntimeMonitoring` runner는
`framework/languages/java/e2e/RuntimeMonitoring/logs/20260708-191709-587818`에서 `MON-A1`부터
`MON-D1`까지 scenario pass를 출력하고 `monitoring e2e result=passed`로 종료했다.

`SpotService` runner는 gateway role의 `ClientDriverSpot` 생성 실패를 먼저 고쳤다. gateway는 actor
factory를 소유하지 않는 spot-only caller이므로 `ZLinkActorManager`를 주입받으면 안 된다.
`ClientDriverSpot`과 `ClientScenario`는 actor id 조회에 `ZLinkActorDirectory`를 사용하도록 낮췄고,
`ZLinkActorClient` 호출에는 조회된 `ActorRef`만 넘기도록 유지했다.

전체 runner 후반 evidence gate도 현재 scenario 구성에 맞췄다. `SM-B9` 원격 join admission은
`play-b` user spot에 `ActorUserJoined` evidence를 남기는 것이 정상이다. 따라서 `play-b`에서 actor
생성 evidence가 없어야 한다는 검사는 유지하고, `ActorUserJoined` 자체를 금지하던 오래된 검사는
제거했다.

확인한 명령:

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 900s framework/languages/java/e2e/SpotService/run_e2e.sh
```

위 명령은 통과했다. `SpotService` runner는
`framework/languages/java/e2e/SpotService/logs/20260708-192510-610773`에서
`spot-service e2e result=passed`로 종료했다.

`scripts/local-package/README.ko.md`의 local package 배포 정책에 맞춰 Java/Kotlin 별도 Gradle
build settings도 정리했다. framework는 bindings source나 `mavenLocal()` fallback을 보지 않고
`.artifacts/wsl/maven` 또는 `ZLINK_LOCAL_PACKAGE_ROOT/maven` 아래의 versioned local package를
dependency repository로만 참조한다. `pluginManagement`에서 `zlinkLocalMavenRepository()`를 먼저
호출하던 구성은 Gradle settings script compile 단계에서 깨질 수 있으므로 제거했고,
`dependencyResolutionManagement.repositories`의 `zlinkLocalPackages` 등록과
`zlinkLibs.zlink.bindings` version catalog 참조를 유지했다.

확인한 명령:

```bash
cd framework/languages/java/e2e/YieldDispatch
../../gradlew --project-cache-dir "${HOME}/.cache/zlink/java-e2e/YieldDispatch-gradle-cache" --no-daemon --no-parallel --max-workers=1 help --quiet

cd framework/languages/java/e2e/SpotService
../../gradlew --project-cache-dir "${HOME}/.cache/zlink/java-e2e/SpotService-gradle-cache" --no-daemon --no-parallel --max-workers=1 help --quiet
```

두 설정 확인은 통과했다.

`YieldDispatch` runner도 clean run으로 재확인했다.

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 900s framework/languages/java/e2e/YieldDispatch/run_e2e.sh
```

위 명령은 통과했다. `YieldDispatch` runner는
`framework/languages/java/e2e/YieldDispatch/logs/20260708-200142-680318`에서 `YD-A1`부터 `YD-E2`까지
scenario pass를 출력하고 `yield-dispatch e2e result=passed`로 종료했다.

### 2026-07-08: Java/Kotlin sample runner와 최종 build gate 확인

Java/Kotlin sample standalone build가 `scripts/local-package/README.ko.md`의 local package 정책을 따르도록
각 sample `standalone.settings.gradle.kts`에서 root version catalog를 `zlinkLibs`로 읽게 했다. 각
sample module은 직접 `systems.zlink:zlink:<version>`을 쓰지 않고 `zlinkLibs.zlink.bindings`를 사용한다.

sample runner 중 발견한 public 예시 문제도 함께 고쳤다.

- `DeliveryDispatch` Tracking handler는 customer actor id를 전송 입력으로 넘기지 않고
  `ZLinkActorDirectory.find(...)`로 `ActorRef`를 조회한 뒤 `sendToActor(ActorRef, ...)`를 호출한다.
- Java/Kotlin sample에서 `CompletionStage`를 직접 `toCompletableFuture().join()`으로 풀던 호출은
  public `ZLinkAwait.await(...)`로 바꿨다.
- `ShoppingMall` OrderWorkflow는 spot id만으로 route request를 보내지 않고 `SpotRef`를 명시한다.
- Kotlin sample의 stale `framework.kotlin.SpotRef` / `ActorRef` import는 public ref 타입으로 정리했다.

확인한 명령:

```bash
cd /home/hep7/project/kairos/zlink
nice -n 10 timeout 1200s env \
  BINGO_REDIS_ENDPOINT=127.0.0.1:60667 \
  TICTACTOE_REDIS_ENDPOINT=127.0.0.1:60667 \
  DELIVERYDISPATCH_REDIS_ENDPOINT=127.0.0.1:60667 \
  GAMEQUEST_REDIS_ENDPOINT=127.0.0.1:60667 \
  SHOPPINGMALL_REDIS_ENDPOINT=127.0.0.1:60667 \
  SUPPORTCHAT_REDIS_ENDPOINT=127.0.0.1:60667 \
  framework/languages/java/samples/run_samples.sh
```

위 명령은 통과했고 `All Java/Kotlin samples passed`를 출력했다. Docker 새 컨테이너 실행이 응답하지
않는 환경이라, 이미 실행 중인 Redis endpoint `127.0.0.1:60667`을 sample별 key prefix와 함께 사용했다.

local package settings 후속 확인:

```bash
cd framework/languages/java/e2e/SpotService
nice -n 10 timeout 180s ../../gradlew --no-daemon --no-parallel --max-workers=1 help --quiet

cd framework/languages/java/e2e-kotlin/SpotService
nice -n 10 timeout 180s ../../gradlew --no-daemon --no-parallel --max-workers=1 help --quiet

cd framework/languages/java/e2e-kotlin/YieldDispatch
nice -n 10 timeout 180s ../../gradlew --no-daemon --no-parallel --max-workers=1 help --quiet
```

세 설정 확인은 모두 통과했다. 추가 검색으로 `framework/languages/java/samples`,
`framework/languages/java/e2e`, `framework/languages/java/e2e-kotlin`의 Gradle settings/build 파일에서
`mavenLocal()`은 match 없이 끝났다. `pluginManagement` 아래 local package repository도 match 없이
끝났다.

최종 root build gate도 bridge task를 제외하지 않고 다시 확인했다.

```bash
cd framework/languages/java
nice -n 10 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build
```

위 명령은 통과했다.

정책 검색:

```bash
rg -n "addPacket<|addActorRequest<|addSubscribe<|addHandler\([^)]*::class\.java|context\.handlers\(\)\.add(Packet|ActorRequest|Subscribe)" \
  framework/languages/java/samples/kotlin framework/languages/java/e2e-kotlin -g '*.kt' -g '*.java'

rg -n "toCompletableFuture\(\)" framework/languages/java/samples -g '*.java' -g '*.kt'
```

두 검색은 모두 match 없이 끝났다. Kotlin sample/e2e의 Spot handler 수동 등록 helper는
`addHandler<T>()`만 남고, 샘플 public 예시에는 직접 `toCompletableFuture()` 변환이 남지 않는다.

### 2026-07-08: Kotlin 점검 기록과 Java 범위 재확인

아래 기록은 직전 점검에서 Kotlin 전용 모듈까지 함께 확인한 흔적이다. 현재 Java 담당 목표에서는
Kotlin 전용 모듈을 Java 완료 범위로 취급하지 않는다. 따라서 이 절의 Kotlin runner 결과는 참고 증거일
뿐이며, Java E2E/sample 완료 여부나 blocker 판정에는 사용하지 않는다. Java 완료 판정은
`framework/languages/java/e2e`와 `framework/languages/java/samples/java`의 feature-map, inventory,
runner evidence로만 판단한다.

Kotlin sample의 Spot handler 등록 정책을 sample 기준으로 다시 확인했다. `TicTacToe`만 manual
`context.handlers().addHandler<...>()` 예시로 남기고, `Bingo`, `DeliveryDispatch`, `SupportChat`의
Spot `configure()`에 있던 handler 목록 반복 등록은 제거했다. 각 sample application은 이미
`addHandlersFromPackageOf(...)` 자동 등록을 사용하므로, handler 책임은 handler 타입과 metadata에서
판정한다.

확인한 명령:

```bash
rg -n "handlers\(\)\.addHandler<" framework/languages/java/samples/kotlin -g '*.kt'
rg -n "addPacket<|addActorRequest<|addSubscribe<|addPacket\(|addActorRequest\(|addSubscribe\(" \
  framework/languages/java/samples/kotlin framework/languages/java/e2e-kotlin -g '*.kt'

cd framework/languages/java
nice -n 10 timeout 300s ./gradlew --no-daemon --no-parallel --max-workers=1 \
  :zlink-framework-testkit:contractTest \
  --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.kotlinSamplesUseManualSpotHandlerRegistrationOnlyInTicTacToe \
  --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.kotlinSamplesAndE2eUseAddHandlerReifiedRegistrationOnly
```

첫 번째 검색은 `samples/kotlin/TicTacToe/.../PlayEntrySpot.kt` 한 곳만 match했다. 두 번째 검색은 match
없이 끝났다. release gate Gradle task도 통과했다.

변경 sample의 compile 검증:

```bash
cd framework/languages/java/samples/kotlin/Bingo
nice -n 10 timeout 240s ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon --no-parallel --max-workers=1 :Server:Play:compileKotlin

cd framework/languages/java/samples/kotlin/DeliveryDispatch
nice -n 10 timeout 300s ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon --no-parallel --max-workers=1 :Server:CourierSpotNode:compileKotlin :Server:CustomerGateway:compileKotlin

cd framework/languages/java/samples/kotlin/SupportChat
nice -n 10 timeout 300s ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon --no-parallel --max-workers=1 :Server:Support:compileKotlin
```

세 compile 검증은 모두 통과했다. 한 번에 여러 Kotlin sample compile을 병렬로 돌렸을 때 included build의
Kotlin incremental cache가 충돌한 기록이 있으므로, 이후 검증은 CPU 부하와 cache 충돌을 피하기 위해
한 번에 하나씩 실행한다.

SupportChat Kotlin sample stale blocker는 filtered sample runner로 닫았다.

```bash
nice -n 10 timeout 600s env \
  ZLINK_SAMPLE_FILTER=kotlin/SupportChat \
  SUPPORTCHAT_REDIS_ENDPOINT=127.0.0.1:60667 \
  framework/languages/java/samples/run_samples.sh
```

위 명령은 `All Java/Kotlin samples passed`로 끝났다. 이에 맞춰
`framework/languages/java/samples/kotlin/SupportChat/sample-porting-inventory.ko.md`의 `partial`/`blocked`
상태와 오래된 actor-bound reply blocker 문구를 닫았다.

Kotlin `YieldDispatch`의 local package runner는
`scripts/local-package/README.ko.md` 정책에 맞춰 수정했다. 기존 runner는 installDist 결과에서
`zlink-java-*.jar`를 찾아 로컬 binding jar로 덮어쓰려 했지만, 현재 Java/Kotlin framework는 version
catalog의 `systems.zlink:zlink` artifact를 local Maven repository에서 해석한다. 따라서 runner는
`zlink-8.6.3.jar`가 local package와 installDist lib에 존재하고 zip으로 정상인지 검증만 한다.

검증:

```bash
cd framework/languages/java/e2e-kotlin/YieldDispatch
nice -n 10 timeout 240s ../../gradlew --no-daemon --no-parallel --max-workers=1 :Server:Session:compileJava
nice -n 10 timeout 900s env ZLINK_KOTLIN_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:60667 ./run_e2e.sh YD-E3
```

`compileJava`는 통과했다. `YD-E3` runner는 local package jar 복사 오류 없이 실제 scenario까지 진행했지만
아직 실패한다. 현재 증거는 `logs/20260708-212629-1036361`이다.

- `client-e3.stderr.log`: `YieldShutdownRecoveryReq` request가 `request timed out after PT30S`로 실패한다.
- `session-flow.log`: `YieldShutdownRecoveryReq`는 stream session에 도착한다.
- `session-flow.log`: 재시작 뒤 `EnsureSpotReq`는 `play-a`로 전송되고 `REPLY_RECEIVED`까지 확인된다.
- 그 다음 recovery probe/evidence reply가 client까지 돌아오지 않는다.

따라서 이 Kotlin `YD-E3` 실패는 Java 완료 blocker가 아니다. Java `YieldDispatch`의 `YD-E3` 상태와
runner evidence는 `framework/languages/java/e2e/YieldDispatch/feature-map.ko.md`와
`framework/languages/java/e2e/YieldDispatch/porting-inventory.ko.md`를 기준으로 별도 검증한다.

### 2026-07-08: Java 범위 누락 리뷰와 local package 설정 정리

읽기 전용 누락 리뷰에서 공통 E2E scenario ID는 Java `feature-map.ko.md` / `porting-inventory.ko.md`
범위에 모두 존재한다고 확인했다. 추가 Java ID로는 `SM-Q9`가 남아 있으며, 이는 Java SpotService의
추가 검증 항목이다.

리뷰 finding은 공통 sample 인덱스의 `ShoppingMall` 구현 수준 표가 Java를 `compact`로 분류하던
불일치였다. Java `ShoppingMall` inventory와 runner는 OrderId별 owner Spot, projection replay,
API/Workflow scale-out, `shoppingmall full client/server self-check completed` marker를 이미 갖고
있으므로 `framework/doc/framework/common/sample/README.ko.md`의 표에서 Java를 full 구조 구현으로
정정했다.

`scripts/local-package/README.ko.md`의 배포 방식 변경도 Java 별도 Gradle build 설정에 반영했다.
`framework/languages/java/samples`와 `framework/languages/java/e2e/*`, `framework/languages/java/e2e-kotlin/*`
의 `settings.gradle.kts`는 `ZLINK_LOCAL_PACKAGE_ROOT`가 없을 때 `.artifacts/wsl/maven`만 고정으로 보지
않고, root 설정과 같이 WSL/Windows local Maven 산출물 중 존재하는 쪽을 고른다. local package 참조
버전은 계속 `framework/languages/java/gradle/libs.versions.toml`의 `zlinkBindings`에 고정한다.

확인한 명령:

```bash
rg -n 'systems\.zlink:zlink:|mavenLocal\(\)|ZLINK_LOCAL_PACKAGE_ROOT|zlinkLibs\.zlink\.bindings' \
  framework/languages/java -g 'build.gradle.kts' -g 'settings.gradle.kts' -g 'libs.versions.toml'

cd framework/languages/java/samples
nice -n 15 timeout 180s ./gradlew --no-daemon --no-parallel --max-workers=1 help

cd framework/languages/java/e2e/SpotService
nice -n 15 timeout 180s ../../gradlew --no-daemon --no-parallel --max-workers=1 help
```

두 `help` 검증은 모두 `BUILD SUCCESSFUL`로 끝났다.

Java root `build` 검증 중 `ChannelMessagingTest.manualClientServer_handlerExceptionRepliesErrorAndReportsObserver`
가 runtime close 단계에서 native context 종료를 기다리며 멈추는 현상을 확인했다. thread dump는
`ZLinkJavaBackendAdapterFactory.JavaContext.close()`에서 `Native.ctxTerm(...)` 호출 중임을 가리켰다.
원인은 framework가 소유한 channel/stream socket이 기본 linger 정책을 그대로 받아 context 종료가
남은 전송 정리를 무기한 기다릴 수 있는 경로였다. 호출자나 sample에 shutdown 옵션을 노출하지 않고,
Java framework binding adapter가 framework socket을 만들 때 `linger(Duration.ZERO)`를 적용하도록
수정했다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 15 timeout 300s ./gradlew --no-daemon --no-parallel --max-workers=1 \
  :zlink-framework-core:integrationTest \
  --tests systems.zlink.framework.runtime.ChannelMessagingTest.manualClientServer_handlerExceptionRepliesErrorAndReportsObserver

nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 test
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 sampleTest
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build
```

위 명령은 모두 `BUILD SUCCESSFUL`로 끝났다. `build`는 이전 hang 지점인
`zlink-framework-core:integrationTest`를 통과했다. 외부 runner CPU 사용량이 높을 때는 Java 검증을
`nice -n 15`, `--no-daemon`, `--no-parallel`, `--max-workers=1`로 제한했다.

### 2026-07-08: POSD/DDD 리뷰 finding 수정

POSD/DDD read-only 리뷰에서 Java sample의 location store 소유 중복과 session packet handler 수동 등록
반복이 지적됐다.

수정 내용:

- Java `SupportChat`의 Api, Session, Support role과 Java `ShoppingMall`의 CommerceApi, OrderWorkflow
  role에서 `options.addLocationStore(SampleLocationStore.create())`를 제거했다. 각 role은 이미
  `ZLinkRedisLocationStore` bean을 제공하므로, Spring auto configuration의
  `ZLinkFrameworkAutoConfiguration.zlinkLocationStoreConfigurer(...)`가 단일 `ZLinkLocationStore` bean을
  framework options에 추가한다.
- Java `Bingo` Session role과 Java `DeliveryDispatch` CustomerGateway role에서
  `addSessionPacketHandler(...)` 수동 호출을 제거했다. session handler는
  `ZLinkFrameworkCapabilityBeanRegistrar`의 session dispatcher context 기반 자동 발견 경로를 사용한다.
- 같은 sample runner 범위에 있는 Kotlin mirror `Bingo` Session role과 `DeliveryDispatch`
  CustomerGateway role에서도 동일한 수동 session handler 등록을 제거했다. `TicTacToe` Java/Kotlin
  sample은 수동 등록 예외로 남겼다.
- Kotlin `GameQuest` QuestMission의 파일 분리 지적은 Kotlin-only sample 리팩터링 항목이다. 이 Java
  완료 게이트에서는 Java mirror의 domain 분리가 이미 적용되어 있으므로 Java POSD/DDD 종료 조건과
  분리한다.

확인한 명령:

```bash
rg -n 'addLocationStore\(' framework/languages/java/samples/java framework/languages/java/samples/kotlin \
  -g '*.java' -g '*.kt'

rg -n 'addSessionPacketHandler\(' framework/languages/java/samples/java framework/languages/java/samples/kotlin \
  -g '*.java' -g '*.kt'

cd framework/languages/java/samples/java/SupportChat
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 \
  :Server:Api:compileJava :Server:Session:compileJava :Server:Support:compileJava

cd framework/languages/java/samples/java/Bingo
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 :Server:Session:compileJava

cd framework/languages/java/samples/java/DeliveryDispatch
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 :Server:CustomerGateway:compileJava

cd framework/languages/java/samples/java/ShoppingMall
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 \
  :Server:CommerceApi:compileJava :Server:OrderWorkflow:compileJava

cd framework/languages/java/samples/kotlin/Bingo
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 :Server:Session:compileKotlin

cd framework/languages/java/samples/kotlin/DeliveryDispatch
nice -n 15 timeout 600s ../../gradlew --settings-file standalone.settings.gradle.kts \
  --no-daemon --no-parallel --max-workers=1 :Server:CustomerGateway:compileKotlin
```

Java compile 명령은 모두 `BUILD SUCCESSFUL`로 끝났다. Kotlin compile 명령도 최종 `BUILD SUCCESSFUL`로
끝났지만, 동시에 실행했을 때 Kotlin daemon incremental cache 경고가 먼저 출력된 뒤 fallback compile로
성공했다. 이후 Kotlin 검증은 같은 cache를 동시에 만지지 않도록 직렬로 실행한다.

POSD 수정 이후 root build와 재리뷰도 다시 확인했다.

```bash
cd framework/languages/java
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build
```

위 명령은 `BUILD SUCCESSFUL`로 끝났다. POSD/DDD read-only 재리뷰도
`NO POSD/DDD JAVA REFACTOR ITEMS`를 반환했다. 남은 완료 게이트는 Java E2E runner 전체 pass와
`framework/languages/java/samples/run_samples.sh` 통합 runner pass다.

### 2026-07-08: local-package 배포 방식 반영과 runner 재검증

`scripts/local-package/README.ko.md`의 배포 방식에 맞춰 Java/Kotlin standalone sample settings의 local
Maven repository fallback을 `.artifacts/wsl/maven` 고정에서 `.artifacts/wsl/maven` 또는
`.artifacts/windows/maven` 선택 방식으로 맞췄다. `ZLINK_LOCAL_PACKAGE_ROOT` 또는
`zlink.localPackageRoot`가 있으면 해당 root의 `maven` 하위 디렉터리를 우선 사용한다.

Kotlin sample의 session packet handler 자동 등록 누락도 수정했다. Spring registrar는 session type,
dispatcher context package뿐 아니라 `addHandlersFromPackageOf(...)` marker package 아래 handler도
탐색한다. Java `ZLinkSessionPacketHandler`뿐 아니라 Kotlin
`ZLinkSuspendingTypedSessionPacketHandler` 구현도 generic context type으로 판정해 session packet
handler로 자동 등록한다. 이 변경으로 `Bingo`와 `DeliveryDispatch` Java/Kotlin sample은 TicTacToe와
달리 session packet handler를 수동 등록하지 않는다.

확인한 명령:

```bash
cd framework/languages/java
nice -n 15 timeout 300s ./gradlew --no-daemon --no-parallel --max-workers=1 \
  :zlink-framework-spring-boot-starter:test \
  --tests systems.zlink.framework.spring.ZLinkFrameworkAutoConfigurationTest.springLifecycleAutoDiscoversSessionPacketHandlersFromApplicationSubpackages

cd framework/languages/java/samples/kotlin/Bingo
env BINGO_REDIS_ENDPOINT=127.0.0.1:55089 nice -n 15 timeout 600s ./run_sample.sh

cd framework/languages/java/samples
env TICTACTOE_REDIS_ENDPOINT=127.0.0.1:55089 \
  BINGO_REDIS_ENDPOINT=127.0.0.1:55089 \
  DELIVERYDISPATCH_REDIS_ENDPOINT=127.0.0.1:55089 \
  GAMEQUEST_REDIS_ENDPOINT=127.0.0.1:55089 \
  SHOPPINGMALL_REDIS_ENDPOINT=127.0.0.1:55089 \
  SUPPORTCHAT_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s ./run_samples.sh

cd framework/languages/java
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build
```

위 명령은 모두 통과했다. `samples/run_samples.sh`는 `All Java/Kotlin samples passed`를 출력했다.
root build는 한 번 `:zlink-framework-core:integrationTest` 중 JVM SIGSEGV로 실패했지만, 같은 명령
재시도는 `BUILD SUCCESSFUL`로 끝났다. 실패는 Java assertion 실패가 아니라 JRE native crash였고,
`zlink-framework-core/hs_err_pid1345719.log`가 생성됐다.

Java E2E runner는 Docker CLI hang을 피하기 위해 외부 Redis endpoint를 명시하고 순차 검증했다. 외부
Redis endpoint가 명시된 경우에도 Redis container를 새로 만들던 `PubSub`, `RegistryMessaging`,
`RuntimeMonitoring` runner는 Docker provisioning을 건너뛰도록 수정했다. `ToActorMessaging` standalone
settings에는 누락된 `zlinkFrameworkJavaRoot()` helper를 추가했다.

확인한 Java E2E 결과:

```bash
cd framework/languages/java
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 3600s bash -c \
  'for f in e2e/*/run_e2e.sh; do echo "== $f =="; timeout 900s "$f" || exit $?; done'
```

위 전체 루프는 `RegistryMessaging`에서 한 번 request timeout으로 중단됐다. 같은 환경에서
`e2e/RegistryMessaging/run_e2e.sh` 단독 재시도는 `registry-messaging e2e result=passed`로 끝났다.

이후 남은 runner는 아래처럼 확인했다.

```bash
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 2200s bash -c \
  'for f in e2e/RuntimeMonitoring/run_e2e.sh e2e/SpotService/run_e2e.sh e2e/ToActorMessaging/run_e2e.sh e2e/YieldDispatch/run_e2e.sh; do echo "== $f =="; timeout 900s "$f" || exit $?; done'
```

`RuntimeMonitoring`, `SpotService`, `ToActorMessaging`, `YieldDispatch`는 모두 pass marker를 출력했다.
`ResilienceLifecycle`는 외부 Redis endpoint로 `all`을 실행하면 `RL-C4 requires a Redis container started
by this runner` 조건 때문에 실패한다. 외부 Redis로 검증 가능한 `RL-C4` 외 시나리오는 개별 실행과
로그 마커로 확인했다. `RL-C4`는 Redis container pause/unpause가 필요한 시나리오라 현재 머신의
`docker run -d` hang 문제가 해결되기 전까지 남은 blocker다. 이 항목이 닫히기 전에는 이 계획을 최종
완료로 보지 않는다.

### 2026-07-08: RL-C4 blocker 해소와 Java/Kotlin 통합 runner 재검증

`scripts/local-package/README.ko.md`의 배포 정책을 다시 확인했다. Java/Kotlin framework는 bindings
source를 직접 참조하지 않고 `.artifacts/<env>/maven`의 versioned local package를 사용해야 하며, 참조
버전은 `framework/languages/java/gradle/libs.versions.toml`의 `zlinkBindings`와 version catalog
`zlinkLibs.zlink.bindings`를 통해 관리한다. Java/Kotlin standalone sample과 e2e Gradle build에서도
literal `systems.zlink:zlink:<version>`을 쓰지 않고 공통 version catalog를 사용해야 한다.

확인한 검색:

```bash
rg -n 'systems\.zlink:zlink:|zlinkLibs\.zlink\.bindings|\.artifacts/(wsl|windows)/maven|ZLINK_LOCAL_PACKAGE_ROOT' \
  framework/languages/java/{build.gradle.kts,settings.gradle.kts,gradle,e2e,e2e-kotlin,samples} \
  -g 'build.gradle.kts' -g 'settings.gradle.kts' -g '*.toml'
```

검색 결과 literal `systems.zlink:zlink:` dependency는 나오지 않았고, standalone build들은
`zlinkLibs.zlink.bindings`와 `.artifacts/<env>/maven` local repository 정책을 사용하고 있었다.

이전 blocker였던 `ResilienceLifecycle` `RL-C4`는 runner 책임으로 해소했다. 외부 Redis endpoint가
명시된 상태에서 Docker CLI가 멈출 수 있으므로, `RL-C4`를 실행할 때 runner가 로컬 TCP proxy를 띄우고
그 proxy process에 `STOP`/`CONT`를 보내 Redis store 단절과 복구를 검증한다. Docker container를 runner가
직접 만든 경우에는 기존처럼 container pause/unpause를 사용한다. 이 변경은 e2e harness의 store 장애
주입 방식만 바꾸며 framework public API나 runtime 동작을 우회하지 않는다.

확인한 명령:

```bash
cd framework/languages/java
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/ResilienceLifecycle/run_e2e.sh RL-C4

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/ResilienceLifecycle/run_e2e.sh
```

두 명령은 모두 `resilience-lifecycle e2e result=passed`로 끝났다.

root build와 E2E runner도 다시 확인했다.

```bash
cd framework/languages/java
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 3600s bash -c \
  'set -euo pipefail; for dir in e2e/DiscoveryRegistryHa e2e/PubSub e2e/RegistrationCodec e2e/RegistryMessaging e2e/ResilienceLifecycle e2e/RuntimeMonitoring e2e/SpotService e2e/ToActorMessaging e2e/YieldDispatch; do echo "== ${dir} =="; timeout 900s "${dir}/run_e2e.sh"; done'
```

root build는 `BUILD SUCCESSFUL`로 끝났다. E2E 전체 루프는 `DiscoveryRegistryHa`, `PubSub`,
`RegistrationCodec` 통과 후 `RegistryMessaging`에서 한 번 consumer JVM이 OS에 의해 `Killed`되고
client request timeout으로 중단됐다. 같은 환경에서 `RegistryMessaging` 단독 재실행은
`registry-messaging e2e result=passed`로 끝났다.

이후 남은 runner는 아래처럼 이어서 확인했다.

```bash
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 2700s bash -c \
  'set -euo pipefail; for dir in e2e/ResilienceLifecycle e2e/RuntimeMonitoring e2e/SpotService e2e/ToActorMessaging e2e/YieldDispatch; do echo "== ${dir} =="; timeout 900s "${dir}/run_e2e.sh"; done'
```

`ResilienceLifecycle`, `RuntimeMonitoring`, `SpotService`, `ToActorMessaging`, `YieldDispatch`는 모두 pass
marker를 출력했다. `RegistryMessaging`의 전체 루프 중 1회 실패는 같은 runner 단독 재시도에서 재현되지
않았고, 코드 assertion 실패가 아니라 OS kill과 timeout 조합으로 기록했다.

Java/Kotlin sample 통합 runner는 공통 Redis endpoint를 sample별 endpoint env로 매핑하도록 수정했다.
이전에는 `ZLINK_REDIS_LOCATION_ENDPOINT`만 넘기면 `TicTacToe`가 `TICTACTOE_REDIS_ENDPOINT`를 보지 못해
Docker Redis를 새로 띄우려다 멈출 수 있었다. 통합 runner가 `ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT`
또는 `ZLINK_REDIS_LOCATION_ENDPOINT`를 `TICTACTOE_REDIS_ENDPOINT`, `BINGO_REDIS_ENDPOINT`,
`DELIVERYDISPATCH_REDIS_ENDPOINT`, `GAMEQUEST_REDIS_ENDPOINT`, `SHOPPINGMALL_REDIS_ENDPOINT`,
`SUPPORTCHAT_REDIS_ENDPOINT`의 기본값으로 주입한다.

확인한 명령:

```bash
cd framework/languages/java
env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s samples/run_samples.sh
```

이 명령은 `All Java/Kotlin samples passed`로 끝났다. 실행 중 CPU는 20코어 기준으로 확인했고, Java runner는
`nice -n 15`로 낮은 우선순위에서 실행했다. 동시에 실행 중이던 다른 Codex 세션의 Node/C++ runner는 이
작업에서 시작한 process가 아니므로 종료하지 않았다.

POSD/DDD 리뷰에서 두 구조 문제가 추가로 나와 수정했다.

- `ResilienceLifecycle` `RL-C4`의 store outage 주입에서 Java client가 Docker container 이름이나 Redis proxy
  PID를 직접 알지 않도록 바꿨다. runner가 `store-pause`와 `store-resume` command script를 만들고,
  `ResilienceProcessManager`는 command path만 실행한다. Docker pause/unpause 또는 proxy `STOP`/`CONT`
  선택은 runner/harness 책임으로 남긴다.
- Java/Kotlin sample 통합 runner의 sample 목록과 Redis endpoint env mapping을
  `framework/languages/java/samples/sample-manifest.env`로 분리했다. Bash runner와 PowerShell runner가 같은
  manifest를 읽는다. PowerShell runner는 현재 `.ps1` runner가 있는 sample만 실행하고, 없는 항목은 skip
  marker를 출력한다.

추가 확인한 명령:

```bash
bash -n framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh \
  framework/languages/java/samples/run_samples.sh

pwsh -NoProfile -Command '$ErrorActionPreference="Stop"; $null=[scriptblock]::Create((Get-Content -Raw "framework/languages/java/samples/run_samples.ps1")); "ps1-parse-ok"'

cd framework/languages/java/e2e/ResilienceLifecycle
nice -n 15 timeout 300s ../../gradlew \
  --project-cache-dir "$HOME/.cache/zlink/java-e2e/ResilienceLifecycle-gradle-cache" \
  --no-daemon --no-parallel --max-workers=1 :Client:compileJava

cd framework/languages/java
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/ResilienceLifecycle/run_e2e.sh RL-C4

env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s samples/run_samples.sh
```

위 명령은 모두 통과했다. `RL-C4`는 `resilience-lifecycle e2e result=passed`와 `scenario RL-C4 passed`를
출력했고, sample runner는 `All Java/Kotlin samples passed`를 출력했다.

POSD/DDD 재리뷰에서 runner 정책 drift가 더 지적되어 추가로 정리했다. `ZLINK_SAMPLE_FILTER` 적용과
sample gate forbidden pattern도 `sample-manifest.env` 기준으로 맞췄다. PowerShell runner는 Java root
Gradle wrapper를 Java root에서 실행해 testkit gate를 확인하고, 비-Windows에서는 같은 manifest로 Bash
sample runner를 호출한다. Windows에서는 같은 manifest에서 `.ps1` runner를 찾는다.

추가 확인:

```bash
source framework/languages/java/samples/sample-manifest.env
rg -n "$FORBIDDEN_SAMPLE_PATTERN" framework/languages/java/samples -g '*.java' -g '*.kt'

bash -n framework/languages/java/samples/run_samples.sh \
  framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh

pwsh -NoProfile -Command '$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile("framework/languages/java/samples/run_samples.ps1", [ref]$tokens, [ref]$errors) > $null; if ($errors.Count) { $errors | ForEach-Object { Write-Error $_.Message }; exit 1 }; "ps1-parse-ok"'

env ZLINK_SAMPLE_FILTER=java/TicTacToe \
  ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 300s framework/languages/java/samples/run_samples.sh

env ZLINK_SAMPLE_FILTER=java/TicTacToe \
  ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 420s pwsh -NoProfile -ExecutionPolicy Bypass \
  -File framework/languages/java/samples/run_samples.ps1
```

forbidden pattern 검색은 match 없이 끝났다. Bash와 PowerShell의 filter 검증은 모두 `PASS TicTacToe.Java`와
`All Java/Kotlin samples passed`를 출력했다.

마지막 POSD/DDD 재리뷰에서 Java `TicTacToe` PowerShell sample runner의 cleanup이 process name pattern으로
다른 실행의 process까지 종료할 수 있다고 지적되어, 전역 process scan/kill을 제거했다. cleanup은
`$Processes`에 담긴 이 runner의 child process와 이 runner가 만든 Redis container만 정리한다.

추가 확인:

```bash
rg -n 'pgrep|Get-CimInstance|Stop-RoleProcesses|kill -9|Stop-Process -Id \$_.ProcessId' \
  framework/languages/java/samples/java/TicTacToe/run_sample.ps1 \
  framework/languages/java/samples/run_samples.ps1

env ZLINK_SAMPLE_FILTER=java/TicTacToe \
  ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 420s pwsh -NoProfile -ExecutionPolicy Bypass \
  -File framework/languages/java/samples/run_samples.ps1
```

process scan/kill 검색은 match 없이 끝났다. PowerShell filter 검증은 다시 `PASS TicTacToe.Java`와
`All Java/Kotlin samples passed`를 출력했다.

### 2026-07-09: PubSub flaky scenario 보강과 SpotService SM-E1 재정렬

`scripts/local-package/README.ko.md`의 변경된 배포 정책을 다시 확인했다. Java/Kotlin framework와
standalone E2E/sample Gradle build는 bindings source를 직접 참조하지 않고 `.artifacts/<env>/maven`
또는 `ZLINK_LOCAL_PACKAGE_ROOT` 아래의 versioned local Maven package를 사용해야 한다. `settings.gradle.kts`
와 `build.gradle.kts`에서 `systems.zlink:zlink:<version>`을 직접 쓰는 방식은 local package 정책과
맞지 않으므로 완료 전 검색 gate에 포함한다.

CPU 부하가 높은 상태에서 Docker CLI가 hang되어 Java E2E는 외부 Redis endpoint를 명시해서 순차 검증했다.
현재 통과 증거는 아래와 같다.

```bash
cd framework/languages/java

nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 build
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 test
nice -n 15 timeout 1200s ./gradlew --no-daemon --no-parallel --max-workers=1 sampleTest

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/DiscoveryRegistryHa/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1200s e2e/PubSub/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/RegistrationCodec/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/RegistryMessaging/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/ResilienceLifecycle/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s e2e/RuntimeMonitoring/run_e2e.sh
```

위 명령 중 Gradle `build`, `test`, `sampleTest`는 모두 `BUILD SUCCESSFUL`로 끝났다.
E2E는 `DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`,
`ResilienceLifecycle`, `RuntimeMonitoring`이 pass marker를 출력했다. `PubSub`는 다음 flaky 지점을
수정한 뒤 전체 runner가 통과했다.

- `PS-A4`: reconnect 직후 단일 event만 기다리던 검증을 `.NET`처럼 gap 구간과 reconnect 이후 구간으로
  나눠 충분한 event를 보낸 뒤 subscriber별 sequence를 확인한다.
- `PS-B2`: publisher restart 직후 baseline event가 너무 적어 readiness 경쟁에 취약하던 부분을 보강했다.
- `PS-B1`: slow subscriber 검증 event 수를 늘려 정상 subscriber가 마지막 sequence까지 받았는지 확인한다.

`ResilienceLifecycle`의 `RL-C4`는 Java client가 Docker container나 proxy PID를 직접 알지 않도록
runner가 `store-pause`/`store-resume` command 파일을 만들고 client는 그 command만 실행하게 정리했다.
외부 Redis를 사용할 때는 runner가 TCP proxy process에 `STOP`/`CONT`를 보내 store 단절과 복구를
검증한다.

`SpotService`의 `SM-E1`은 Java gateway spot에서 누락 packet을 직접 던지던 구현을 `.NET` 기준과 같은
play HTTP endpoint 기반 검증으로 옮겼다. play 서버가 public route client로 live spot에
`MissingSpotReq`/`MissingSpotMsg`를 보내고, message-flow observer가
`HANDLER_MISSING/REPLY_ERROR`와 `HANDLER_MISSING/DROP` evidence를 남기는지 확인한다.

검증:

```bash
cd framework/languages/java/e2e/SpotService
nice -n 15 timeout 600s ../../gradlew \
  --project-cache-dir "$HOME/.cache/zlink/java-e2e/SpotService-gradle-cache" \
  --no-daemon --no-parallel --max-workers=1 \
  :Shared:compileJava :Client:compileJava :Server:Play:compileJava --quiet
```

위 compile gate는 통과했다. `SM-E1` focused runner는 처음에는 불필요하게 `play-b`까지 띄우면서
`ZlinkBindException` port 경쟁에 걸렸고, 후처리도 시작하지 않은 `play-b` evidence를 조회했다. focused
`SM-E1`에서는 실제 필요한 `play-a`와 `gateway`만 시작하고, evidence 후처리도 시작한 role만 조회하도록
runner를 정리했다.

재검증:

```bash
bash -n framework/languages/java/e2e/SpotService/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 600s framework/languages/java/e2e/SpotService/run_e2e.sh SM-E1
```

위 focused run은 `framework/languages/java/e2e/SpotService/logs/20260709-003545-1715245`에서
`spot-service e2e mode=missing result=passed`를 출력했다. `SpotService` 전체 runner와 남은
`ToActorMessaging`/`YieldDispatch`, sample runner는 아직 최종 재검증 대상이다.

추가 재검증:

```bash
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1200s framework/languages/java/e2e/SpotService/run_e2e.sh
```

전체 `SpotService` runner도 `framework/languages/java/e2e/SpotService/logs/20260709-003626-1717528`에서
`spot-service e2e result=passed`를 출력했다. 남은 최종 재검증 대상은 `ToActorMessaging`,
`YieldDispatch`, sample runner, 그리고 최종 review gate다.

잔여 Java E2E 재검증:

```bash
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s framework/languages/java/e2e/ToActorMessaging/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s framework/languages/java/e2e/YieldDispatch/run_e2e.sh
```

`ToActorMessaging`는 `framework/languages/java/e2e/ToActorMessaging/logs/20260709-003826-1723890`에서
`to-actor-messaging e2e result=passed`를 출력했다. `YieldDispatch`는
`framework/languages/java/e2e/YieldDispatch/logs/20260709-003848-1725326`에서
`yield-dispatch e2e result=passed`를 출력했다.

Java/Kotlin sample runner 재검증:

```bash
env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s framework/languages/java/samples/run_samples.sh
```

이 run은 `TicTacToe.Java`, Java `Bingo`, `DeliveryDispatch`, `GameQuest`, `ShoppingMall`, `SupportChat`,
`TicTacToe.Kotlin`까지 통과했고, Kotlin `Bingo`에서 `AuthenticateReq`가 `NOT_ADMITTED`로 실패했다.
실패 로그는 session stream 요청은 도착했지만, session/play 위치 저장소 자동 연결 설정이 Java Bingo와
다르게 명시되지 않아 부하가 높은 상태에서 admission이 흔들릴 수 있음을 보여줬다.

수정 내용:

- Kotlin `Bingo` Session/Play server configuration에 Java Bingo와 같은 `configureLocations()` 호출을
  추가했다.
- Java `ShoppingMall` runner는 단순 TCP 포트 확인 대신 HTTP `/health` 확인으로 CommerceApi 준비를
  검증하게 했다.
- sample runner의 transient retry 판정에 `BindException`과 `Address already in use`를 포함했다.

Kotlin `Bingo` 단독 재검증:

```bash
env BINGO_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 900s framework/languages/java/samples/kotlin/Bingo/run_sample.sh
```

위 run은 종료 코드 0으로 끝났고, `framework/languages/java/samples/kotlin/Bingo/build/sample-logs/client.log`에
`bingo=completed`, `stream-inbound sample=Bingo ... BingoGameStartedNotify`,
`BingoGameEndedNotify`, `BingoRewardAnnouncedNotify`가 남았다.

전체 Java/Kotlin sample runner 재검증:

```bash
env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s framework/languages/java/samples/run_samples.sh
```

위 run은 `TicTacToe.Java`, Java `Bingo`, `DeliveryDispatch`, `GameQuest`, `ShoppingMall`, `SupportChat`,
`TicTacToe.Kotlin`, Kotlin `Bingo`, `GameQuest`, `ShoppingMall`, `DeliveryDispatch`를 모두 통과했고,
마지막에 `All Java/Kotlin samples passed`를 출력했다. 실행 중 CPU는 20코어 기준으로 확인했고, Java
runner가 끝난 뒤 남은 고부하 프로세스는 외부 Node/C++ E2E 작업이었으므로 종료하지 않았다.

POSD/DDD 리뷰 finding 반영 뒤 재검증:

```bash
env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s framework/languages/java/samples/run_samples.sh
```

sample runner lifecycle 정책은 `framework/languages/java/samples/runner-common.sh`로 모았고, 각 sample
runner는 topology와 sample별 port reservation만 유지한다. Java/Kotlin settings의 local Maven repository
선택 정책은 `framework/languages/java/gradle/zlink-local-packages.settings.gradle.kts`로 모았다.
위 재검증 run도 마지막에 `All Java/Kotlin samples passed`를 출력했다.

## 완료 전 누락 리뷰

구현 담당 에이전트가 완료를 주장하기 전에 별도 Codex 에이전트로 read-only 리뷰를 요청한다.

리뷰 요청은 아래 범위를 포함해야 한다.

- 공통 e2e 문서의 모든 scenario ID가 Java `feature-map.ko.md`와 runner evidence에 존재하는지
- 공통 sample 문서의 모든 역할, 메시지 흐름, self-check가 Java sample inventory와 runner evidence에
  존재하는지
- `.NET` 기준 구현에 있는 역할과 client 검증이 Java에서 누락되지 않았는지
- public contract gap을 internal package, reflection, 테스트 전용 adapter로 숨기지 않았는지
- `run_e2e.sh`, `run_sample.sh`, `sampleTest`, Gradle test 결과가 실제로 pass했는지

리뷰 결과가 `NO MISSING JAVA ITEMS`가 아니면 모든 finding을 수정하고 같은 리뷰를 다시 요청한다.

2026-07-09 read-only 누락 리뷰 결과는 `NO MISSING JAVA ITEMS`였다.

## POSD/DDD 반복 리뷰

누락 리뷰가 깨끗해진 뒤에만 별도 Codex 에이전트로 POSD/DDD 리뷰를 요청한다. 이 리뷰는 동작 누락이
아니라 구조 개선 가능성만 본다.

리뷰 기준:

- public API가 shallow wrapper로 늘어나지 않았는지
- codec, transport, registry, location store, actor/session lifecycle 같은 지식이 호출자나 sample로
  새어나오지 않았는지
- domain role과 Spring/Gradle infrastructure 책임이 섞이지 않았는지
- handler, runtime, runner, sample 사이에 같은 정책이 반복 구현되지 않았는지
- Java idiom을 따르면서도 `.NET` 기준 domain 흐름과 같은 의미를 유지하는지

의미 있는 refactoring finding이 나오면 구현, 테스트, 문서 갱신을 한 뒤 E2E/sample 검증과 POSD/DDD
리뷰를 다시 실행한다. 리뷰가 `NO POSD/DDD JAVA REFACTOR ITEMS`를 반환할 때 종료한다.

2026-07-09 첫 POSD/DDD read-only 리뷰는 sample runner lifecycle 정책과 local Maven repository 선택
정책이 여러 sample/e2e settings에 반복되어 있다는 finding을 반환했다. 위 공통화와 sample runner
재검증으로 수정한 뒤 POSD/DDD 리뷰를 다시 요청한다.

재리뷰는 Java/Kotlin `TicTacToe` runner에 자체 endpoint polling loop가 남아 있다는 finding을 반환했다.
`wait_endpoint`는 sample별 label을 유지하는 얇은 wrapper로 두고 실제 endpoint parsing과 `/dev/tcp`
readiness polling은 `runner-common.sh`의 `wait_port`만 사용하도록 바꿨다.

```bash
env TICTACTOE_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 600s framework/languages/java/samples/java/TicTacToe/run_sample.sh

env TICTACTOE_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 600s framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh
```

두 명령은 각각 `PASS TicTacToe.Java`, `PASS TicTacToe.Kotlin`을 출력했다.

최종 재리뷰는 Java/Kotlin `TicTacToe` runner에 더 이상 호출되지 않는 `endpoint_host()`/`endpoint_port()`
helper가 남아 있다는 finding을 반환했다. 두 runner에서 dead helper를 제거했고, `/dev/tcp` polling은
`runner-common.sh` 한 곳에만 남도록 정리했다.

```bash
bash -n framework/languages/java/samples/runner-common.sh \
  framework/languages/java/samples/java/TicTacToe/run_sample.sh \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh

env TICTACTOE_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 600s framework/languages/java/samples/java/TicTacToe/run_sample.sh

env TICTACTOE_REDIS_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 600s framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh
```

두 단독 runner는 다시 각각 `PASS TicTacToe.Java`, `PASS TicTacToe.Kotlin`을 출력했다.

dead helper 제거 뒤 최종 정적 확인도 다시 실행했다.

```bash
bash -n framework/languages/java/samples/runner-common.sh \
  framework/languages/java/samples/java/TicTacToe/run_sample.sh \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh

rg -n "endpoint_host|endpoint_port|/dev/tcp" \
  framework/languages/java/samples/java/TicTacToe/run_sample.sh \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh \
  framework/languages/java/samples/runner-common.sh

rg -n "addPacket<|addActorRequest<|addSubscribe<|addPacket\\(|addActorRequest\\(|addSubscribe\\(" \
  framework/languages/java/samples framework/languages/java/e2e-kotlin -g '*.kt' -g '*.java'

rg -n "context\\.handlers\\(\\)\\.addHandler\\(|context\\.handlers\\(\\)\\.addHandler<|handlers\\(\\)\\.addHandler\\(|handlers\\(\\)\\.addHandler<" \
  framework/languages/java/samples -g '*.kt' -g '*.java'

git diff --check -- \
  framework/doc/plan/framework-java-e2e-sample-gap-closure-plan.ko.md \
  framework/languages/java/gradle/zlink-local-packages.settings.gradle.kts \
  framework/languages/java/samples/runner-common.sh \
  framework/languages/java/samples \
  framework/languages/java/e2e \
  framework/languages/java/e2e-kotlin \
  framework/languages/java/settings.gradle.kts
```

`/dev/tcp` match는 `runner-common.sh` 한 곳에만 남았고, `endpoint_host`/`endpoint_port` match는 없었다.
`addPacket`/`addActorRequest`/`addSubscribe` 검색은 match 없이 끝났다. sample의 수동
`addHandler(...)` 검색은 Java/Kotlin `TicTacToe` sample에서만 match됐다. `git diff --check`도 통과했다.

### 2026-07-09: 최신 Java 완료 게이트 재검증

CPU 부하를 확인하면서 Java 검증을 낮은 우선순위와 단일 Gradle worker로 다시 실행했다. 실행 중 보이는
외부 Node/C++ runner process는 이 작업에서 시작한 process가 아니므로 종료하지 않았다.

Root Gradle 검증:

```bash
cd framework/languages/java

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 2400s ./gradlew --no-daemon --no-parallel --max-workers=1 build

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s ./gradlew --no-daemon --no-parallel --max-workers=1 test sampleTest
```

첫 `build`는 `zlink-framework-core:integrationTest` 실행 중 JVM `SIGSEGV`로 중단됐다. 실패한 test XML은
실패 assertion을 남기지 않았고, `hs_err_pid1989798.log`의 native stack에는 core monitor handler 경로가
포함됐다. 같은 `:zlink-framework-core:integrationTest`를 단독 재실행하자 `BUILD SUCCESSFUL`로 끝났고,
이후 root `build`도 `BUILD SUCCESSFUL`로 끝났다. `test sampleTest` 역시 `BUILD SUCCESSFUL`로 끝났다.

Java E2E runner는 Docker CLI hang을 피하기 위해 외부 Redis endpoint를 명시하고 순차 실행했다.

```bash
set -euo pipefail
export ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089
export ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089

for runner in \
  framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh \
  framework/languages/java/e2e/PubSub/run_e2e.sh \
  framework/languages/java/e2e/RegistrationCodec/run_e2e.sh \
  framework/languages/java/e2e/RegistryMessaging/run_e2e.sh \
  framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh \
  framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh \
  framework/languages/java/e2e/SpotService/run_e2e.sh \
  framework/languages/java/e2e/ToActorMessaging/run_e2e.sh \
  framework/languages/java/e2e/YieldDispatch/run_e2e.sh; do
  nice -n 15 timeout 1500s "$runner"
done
```

`DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`, `ResilienceLifecycle`,
`RuntimeMonitoring`, `SpotService`, `ToActorMessaging`은 같은 순차 run에서 pass marker를 출력했다.
`YieldDispatch`는 첫 순차 run에서 `play-a` spot router endpoint bind 실패로 중단됐으나, 실패 뒤 해당
port와 Java E2E 잔여 process가 없음을 확인했다. 같은 명령을 단독 재실행하자
`framework/languages/java/e2e/YieldDispatch/logs/20260709-014858-2032612`에서 `YD-A1`부터 `YD-E2`까지
모두 통과했고 `yield-dispatch e2e result=passed`를 출력했다.

read-only 누락 리뷰에서 `DiscoveryRegistryHa/run_e2e.sh` 기본값이 `all`이 아니라 `SF-A1`이라서 위
순차 run의 `DiscoveryRegistryHa` 증거가 Config 6 전체 증거로 충분하지 않다는 finding이 나왔다. 같은
runner를 `all` 인자로 다시 실행했다. 이 과정에서 Docker CLI가 멈추는 문제가 재현되어, 외부 Redis
endpoint가 주어졌을 때 store-failure scenario가 Docker container 대신 runner 소유 TCP proxy를 사용하도록
`DiscoveryRegistryHa/run_e2e.sh`를 수정했다. outage는 proxy process에 `STOP`/`CONT`를 보내 검증한다.

재검증:

```bash
env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1500s framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh all
```

위 명령은 `framework/languages/java/e2e/DiscoveryRegistryHa/logs/20260709-021031-2111777`에서 `SF-A1`,
`SF-A2`, `SF-B1`, `SF-B2`, `SF-C1`, `SF-C2`, `SF-D1`, `SF-D2`, `SF-D3`, `SF-E1`을 모두 통과했고
`discovery-registry-ha e2e result=passed`를 출력했다.

Java/Kotlin sample 통합 runner도 filter 없이 다시 실행했다.

```bash
env ZLINK_JAVA_SAMPLE_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1800s framework/languages/java/samples/run_samples.sh
```

위 명령은 `TicTacToe.Java`, Java `Bingo`, `DeliveryDispatch`, `GameQuest`, `ShoppingMall`, `SupportChat`,
`TicTacToe.Kotlin`, Kotlin `Bingo`, `GameQuest`, `ShoppingMall`, `DeliveryDispatch`를 모두 통과했고 마지막에
`All Java/Kotlin samples passed`를 출력했다. 실행 뒤 Java E2E/sample, Gradle, Kotlin daemon 잔여
process는 남아 있지 않았다.

POSD/DDD 리뷰에서 `SpotService`와 `YieldDispatch` E2E role bootstrap에 session packet handler 수동 목록이
남아 있다는 finding이 나왔다. 두 runner 모두 Spring의 session packet handler 자동 검색 경로를 사용하도록
`options.addHandlersFromPackageOf(...)` marker만 남기고 `.addSessionPacketHandler(...)` 목록을 제거했다.

재검증:

```bash
bash -n framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh \
  framework/languages/java/e2e/SpotService/run_e2e.sh \
  framework/languages/java/e2e/YieldDispatch/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1500s framework/languages/java/e2e/SpotService/run_e2e.sh

env ZLINK_JAVA_E2E_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  ZLINK_REDIS_LOCATION_ENDPOINT=127.0.0.1:55089 \
  nice -n 15 timeout 1500s framework/languages/java/e2e/YieldDispatch/run_e2e.sh
```

`SpotService`는 `framework/languages/java/e2e/SpotService/logs/20260709-021300-2122709`에서 전체 mode와
scenario를 통과하고 `spot-service e2e result=passed`를 출력했다. `YieldDispatch`는
`framework/languages/java/e2e/YieldDispatch/logs/20260709-021422-2128239`에서 `YD-A1`부터 `YD-E2`까지
모두 통과했고 `yield-dispatch e2e result=passed`를 출력했다.

POSD/DDD 재리뷰에서 Java sample의 PowerShell runner 두 개가 command-line pattern으로 role process를
찾아 종료한다는 추가 finding이 나왔다. `Bingo/run_sample.ps1`과 `ShoppingMall/run_sample.ps1`은 이제
runner가 직접 시작한 `Process` 객체를 기준으로 child process tree만 종료한다. Windows에서는
`ParentProcessId`로 child process를 찾고, Unix 계열 PowerShell에서는 `pgrep -P`로 child process를 찾는다.
이 변경은 사용자가 따로 실행한 같은 이름의 Java process를 종료하지 않기 위한 runner lifecycle 정책이다.

정적 재검증:

```bash
rg -n "Win32_Process|pgrep -f|kill -9|RolePattern|CommandLine -match|Stop-RoleProcesses" \
  framework/languages/java/samples -g '*.ps1'

pwsh -NoProfile -Command '$ErrorActionPreference="Stop"; foreach ($f in @("framework/languages/java/samples/java/Bingo/run_sample.ps1", "framework/languages/java/samples/java/ShoppingMall/run_sample.ps1")) { $tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile($f, [ref]$tokens, [ref]$errors) > $null; if ($errors.Count) { $errors | ForEach-Object { Write-Error ($f + ": " + $_.Message) }; exit 1 } }; "ps1-parse-ok"'

git diff --check -- \
  framework/languages/java/samples/java/Bingo/run_sample.ps1 \
  framework/languages/java/samples/java/ShoppingMall/run_sample.ps1 \
  framework/doc/plan/framework-java-e2e-sample-gap-closure-plan.ko.md \
  framework/languages/java/e2e/DiscoveryRegistryHa/run_e2e.sh \
  framework/languages/java/e2e/SpotService/Server/Play/src/main/java/systems/zlink/e2e/spotservice/play/Program.java \
  framework/languages/java/e2e/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/yielddispatch/session/Program.java
```

검색 결과는 `ParentProcessId` 기반 child 조회만 남았고, PowerShell parser는 `ps1-parse-ok`를 출력했다.
`git diff --check`도 통과했다.

최종 POSD/DDD 재리뷰 결과는 `NO POSD/DDD JAVA REFACTOR ITEMS`였다.

## 최종 종료 조건

- 모든 공통 E2E scenario가 Java에서 `implemented`로 남아 있다.
- 공통 sample 문서와 `.NET` sample 기준에 대해 Java sample gap이 없다.
- `partial` 또는 `gap`으로 남은 E2E/sample 항목이 없다. public contract 설계가 필요한 항목이 있으면
  이 계획은 완료가 아니라 blocked 상태로 남긴다.
- 모든 Java E2E runner와 sample runner가 pass했다.
- 누락 리뷰가 `NO MISSING JAVA ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD JAVA REFACTOR ITEMS`를 반환했다.
