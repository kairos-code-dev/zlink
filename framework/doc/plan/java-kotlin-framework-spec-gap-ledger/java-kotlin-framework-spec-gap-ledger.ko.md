# Java/Kotlin Framework spec gap ledger

상태: audit ledger. 구현 완료를 의미하지 않으며, 이 조사에서는 구현 코드, test,
common spec, E2E runner를 수정하지 않았다.

조사 기준일: 2026-08-02

이 ledger는 Java framework와 Kotlin framework를 하나의 작업 단위로 관리한다. Java와
Kotlin의 exact interface, feature-map, runner, 현재 실패 원인은 각 항목에서
분리한다. 목표는 기능 이름이 존재하는지 확인하는 것이 아니라, common spec이 정의한
계약과 실제 production call path, role process E2E, common sample 실행 결과가 같은
동작을 증명하는지 확인하는 것이다.

## 1. 목적과 완료 조건

현재 checkout에서는 Java와 Kotlin compile, 일부 sample build, 일부 feature-map이
통과한다. 그러나 그 결과만으로는 전체 계약을 만족한다고 판정할 수 없다. 현재
확인된 대표적인 visible failure는 다음과 같다.

- Java SpotActorTransfer/ST-A1은 role process를 시작하기 전에
  zlink.framework.binding.internal module을 찾지 못해 compile에서 중단된다.
- Kotlin SpotActorTransfer/ST-A1은 getOrCreate 인자 수, toCompletableFuture,
  context 구현 계약이 현재 public interface와 맞지 않아 compile에서 중단된다.
- Java stream queue는 full 상태에서 기존 message를 제거하고 새 message를 넣는
  경로를 사용한다. common spec은 새로 도착한 Send를 버리고 기존 queue를 보존하도록
  요구한다.
- Java TicTacToe sample은 이전 synchronous HTTP terminal을 호출해 process 시작
  전에 실패한다. Kotlin TicTacToe의 단일 실행은 통과했지만 여섯 sample 전체를
  증명하지는 않는다.
- Java aggregate runner는 Config 12와 Config 14를 실행하지 않고, Kotlin aggregate
  runner는 Config 12, 13, 14를 실행하지 않는다.
- samples/run_samples.sh의 fake backend gate는 현재 checkout에 존재하지 않는 test
  method를 지정해 aggregate를 시작하기 전에 실패한다.

완료는 다음 조건을 모두 만족할 때만 판정한다.

1. common spec과 Java/Kotlin exact interface의 public contract가 source export,
   package export, API snapshot과 일치한다. 함수·클래스 이름뿐 아니라 parameter,
   return type, default/optional 값, error type/code, timeout, cancellation,
   callback, ownership/disposal, serialization/content type, HTTP status/body까지
   확인한다.
2. runtime의 실제 call path가 lifecycle, admission/preflight, authority/owner
   변경, callback exactly-once, queue/replay, deadline/cancellation, rollback,
   cleanup, recovery/takeover/replay, terminal error, concurrent shutdown의 계약을
   증명한다.
3. common E2E의 374개 scenario ID가 Java와 Kotlin feature-map 및 dispatch selector에
   exact ID로 연결된다. alias, typo, source-only, diagnostic-only, partial,
   unimplemented 항목은 all 성공으로 계산하지 않는다.
4. 각 process E2E가 실제 client public API와 분리된 role server endpoint를 사용하고,
   client-visible result와 role server evidence를 함께 확인한다. terminal reason,
   callback count, owner, generation, cleanup을 필요한 scenario에서 직접 assertion
   한다.
5. Java/Kotlin의 여섯 common sample이 compile/build만이 아니라 실제 process, client
   self-check, server evidence, async terminal, package mode까지 통과한다.
6. contract test, regression test, package consumer, API snapshot, CI path filter와
   skip list가 이 기준을 자동으로 유지한다.
7. 각 단계가 끝날 때 POSD와 DDD 관점의 codex sol agent review가 승인되고, 승인된
   범위만 별도 commit과 push로 남는다. 현재 audit 단계에서는 commit과 push를
   수행하지 않는다.

### 판정 표기

- gap: 현재 source, 실행 결과 또는 process 증거가 목표 계약과 다르거나 증명되지
  않았다.
- contract 선행: 구현 방향을 정하기 전에 common spec 또는 exact interface를 먼저
  확정해야 한다. 다른 언어 구현이나 common E2E만으로 public API를 추가하지 않는다.
- 충족: 현재 source 또는 새로 실행한 결과가 해당 좁은 조건을 직접 증명한다. 좁은
  compile이나 feature-map 존재만으로 전체 scenario 충족을 표시하지 않는다.
- historical: 이전 log나 snapshot의 정보이다. 현재 tree의 증거로 사용하지 않는다.

## 2. 조사 범위와 authoritative source

### 2.1 계약과 source의 우선순위

계약 해석 순서는 다음과 같다.

1. framework/doc/framework/common/spec/의 common framework contract
2. framework/doc/framework/common/spec/<package>/languages/java/와
   .../languages/kotlin/의 exact interface
3. framework/doc/contract-inventory/jvm-public-contract-source-owners.json에
   기록된 Java/Kotlin source owner와 module boundary
4. 실제 Java/Kotlin public source, module-info.java, package metadata, API snapshot
5. contract test, unit/integration test, E2E process, sample process 결과

common E2E 문서는 검증해야 할 scenario와 evidence를 정하는 기준이다. 다른 언어의
구현이나 common E2E 문서만으로 Java/Kotlin public API를 추가하지 않는다. 계약 근거가
없는 기능은 contract 선행으로 남기고 spec/guide/feature-map의 설계 후보로 분리한다.

### 2.2 확인한 문서와 코드

공통 계약은 다음 범위를 확인했다.

- framework/doc/framework/common/spec/00-public-contract-governance.ko.md
- framework/doc/framework/common/spec/01-*.ko.md부터 32-*.ko.md까지의 framework,
  server, stream connector, HTTP client 계약
- framework/doc/framework/common/spec/server/languages/java/
- framework/doc/framework/common/spec/server/languages/kotlin/
- framework/doc/framework/common/spec/stream-connector/languages/java/
- framework/doc/framework/common/spec/http-client/languages/java/java-http-client.ko.md
- framework/doc/framework/common/spec/http-client/languages/kotlin/kotlin-http-client.ko.md
- framework/doc/contract-inventory/jvm-public-contract-source-owners.json

정확한 Java server interface는 다음 문서가 소유한다.

- framework/doc/framework/common/spec/server/languages/java/01-system-structure.ko.md
- framework/doc/framework/common/spec/server/languages/java/02-handler-interfaces.ko.md
- framework/doc/framework/common/spec/server/languages/java/03-location-store.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/actors.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/channel-messaging.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/common-runtime.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/location-maintenance.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/monitoring.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/spots.ko.md
- framework/doc/framework/common/spec/server/languages/java/interfaces/stream-session.ko.md

정확한 Kotlin server interface는 다음 문서가 소유한다.

- framework/doc/framework/common/spec/server/languages/kotlin/02-handler-interfaces.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/03-location-store.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/actors.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/channel-messaging.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/common-runtime.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/configuration-host.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/location-maintenance.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/monitoring.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/spots.ko.md
- framework/doc/framework/common/spec/server/languages/kotlin/interfaces/stream-session.ko.md

공통 E2E는 framework/doc/framework/common/e2e/config-1-location-messaging.ko.md부터
config-14-instance-spot.ko.md까지 확인했다. 현재 heading 기준 inventory는 다음과
같다.

    Config 1  RM   17
    Config 2  SM   66
    Config 3  PS   24
    Config 4  RC   12
    Config 5  RL   39
    Config 6  SF   28
    Config 7  MON  12
    Config 8  TD   32
    Config 9  TA    7
    Config 10 ST   43
    Config 11 OBS  22
    Config 12 CH   16
    Config 13 SA   20
    Config 14 IS   36
    total          374

sample 계약과 실행 대상은 다음 범위다.

- framework/doc/framework/common/sample/README.ko.md
- framework/doc/framework/common/sample/bingo/README.ko.md
- framework/doc/framework/common/sample/tictactoe/README.ko.md
- framework/doc/framework/common/sample/supportchat/README.ko.md
- framework/doc/framework/common/sample/deliverydispatch/README.ko.md
- framework/doc/framework/common/sample/event/shoppingmall.ko.md
- framework/doc/framework/common/sample/event/gamequest.ko.md
- framework/languages/java/samples/java/
- framework/languages/java/samples/kotlin/

Java와 Kotlin 모두 common sample의 Bingo, TicTacToe, SupportChat, DeliveryDispatch,
ShoppingMall, GameQuest 여섯 개를 선언한다. ZoneWorld는 common sample 문서상 .NET과
Node 대상이므로 Java/Kotlin 누락으로 계산하지 않는다.

production source와 검증 진입점은 다음 범위다.

- framework/languages/java/zlink-framework-core/
- framework/languages/java/zlink-framework-kotlin/
- framework/languages/java/zlink-stream-connector/
- framework/languages/java/zlink-http-client/
- framework/languages/java/zlink-framework-locations-redis/
- framework/languages/java/zlink-framework-spring-boot-starter/
- framework/languages/java/e2e/
- framework/languages/java/e2e-kotlin/
- framework/languages/java/samples/
- framework/languages/java/e2e/run_e2e_all.sh
- framework/languages/java/e2e-kotlin/run_e2e_all.sh
- framework/languages/java/samples/run_samples.sh
- framework/languages/java/scripts/verify_packaged_contract.sh
- framework/languages/java/validate_sample_e2e_configuration_policy.sh
- framework/languages/java/build.gradle.kts

## 3. 현재 검증 결과

이 절의 결과는 2026-08-02 현재 working tree에서 직접 실행했다. 이전 log나 snapshot은
현재 결과와 섞지 않았다.

| 검증 | 결과 | 의미 |
|---|---|---|
| ./gradlew --no-daemon --no-parallel :zlink-framework-core:compileJava :zlink-framework-kotlin:compileKotlin :zlink-stream-connector:compileJava | PASS, 26초 | 좁은 compile만 증명한다. 전체 contract와 process E2E는 증명하지 않는다. |
| ./gradlew --no-daemon --no-parallel :zlink-framework-core:contractTest :zlink-framework-kotlin:contractTest :zlink-stream-connector:test | FAIL | Java JavaDocumentationRegressionTest가 common Config 14 fixture/aggregate 누락을 보고했다. |
| ./scripts/verify_packaged_contract.sh java | FAIL | clean consumer가 zlink-framework-provider-abstractions:0.1.0-SNAPSHOT와 zlink-http-client:0.3.1을 resolve하지 못했다. |
| ./scripts/verify_packaged_contract.sh kotlin | FAIL | 같은 clean consumer blocker가 Kotlin package 검증에서도 발생했다. |
| ./gradlew --no-daemon --no-parallel -p samples buildAllSamples | PASS, 53초, 312 tasks | 여섯 sample의 build 결과이다. process와 evidence는 증명하지 않는다. |
| find e2e e2e-kotlin samples -name '*.sh' ... bash -n | PASS | shell syntax만 증명한다. process 동작은 증명하지 않는다. |
| timeout 300s env ZLINK_SAMPLE_FILTER=TicTacToe ./samples/run_samples.sh | FAIL | 지정한 fake backend test method가 현재 test source에 없어 aggregate gate에서 중단됐다. |
| timeout 240s ./samples/java/TicTacToe/run_sample.sh | FAIL | Java client가 synchronous fetch(...).toCompletableFuture().join()을 사용해 async terminal policy에서 중단됐다. |
| timeout 240s ./samples/kotlin/TicTacToe/run_sample.sh | PASS | Kotlin TicTacToe 한 scenario의 process 결과만 증명한다. |
| timeout 300s ./e2e/SpotActorTransfer/run_e2e.sh ST-A1 | FAIL | role process 전 Java compile에서 zlink.framework.binding.internal module을 찾지 못했다. |
| timeout 300s ./e2e-kotlin/SpotActorTransfer/run_e2e.sh ST-A1 | FAIL | Kotlin E2E compile에서 getOrCreate, toCompletableFuture, context 계약 오류가 발생했다. |
| ./validate_sample_e2e_configuration_policy.sh | FAIL | Java/Kotlin sample/E2E application source가 environment 또는 JVM property를 읽는 항목을 보고했다. |

contract test의 구체적인 report는
framework/languages/java/zlink-framework-core/build/test-results/contractTest/TEST-systems.zlink.framework.JavaDocumentationRegressionTest.xml에
있다. 해당 결과는 실패를 숨기는 근거가 아니라, Config 14 inventory를 현재 Java
fixture/runner가 소유하지 않는다는 audit signal이다.

Java focused E2E의 과거 log에서 보였던 HTTP 500은 현재 실행 결과가 아니다. 현재 tree의
실행은 module compile blocker에서 먼저 중단되므로, HTTP 500을 현재 runtime 증거로
기록하지 않는다.

현재 working tree는 Java E2E, Node, .NET, 공통 guide/spec/sample, workflow에 사용자
변경이 있는 dirty 상태였다. 이 audit는 그 파일을 되돌리거나 덮어쓰지 않았다. 새로
추가하는 대상은 이 ledger 하나뿐이다.

## 4. 현재 충족 판정

다음 범위는 현재 source 또는 실행 결과가 좁은 조건을 직접 증명하므로 충족으로
기록한다.

- framework/doc/contract-inventory/jvm-public-contract-source-owners.json이 Java
  artifact, Kotlin artifact, public package, runtime internal package와 boundary gate를
  구분한다.
- Java와 Kotlin production module의 좁은 compile이 통과했다.
- Java/Kotlin sample manifest와 Gradle source set은 각각 여섯 common sample을 가리킨다.
- 여섯 Java sample과 여섯 Kotlin sample에 run_sample.sh가 존재한다.
- shell runner syntax 검사는 통과했다.
- Java stream options source에는 common exact interface가 요구하는 observer option
  field와 기본값이 존재한다. 다만 Kotlin compression copy가 해당 값을 보존하는지와
  queue semantics는 별도 gap으로 남긴다.
- static import scan에서 application E2E client가
  systems.zlink.framework.runtime.internal 또는 ZLinkJavaRaw를 직접 import하는
  결과는 발견되지 않았다. 이것은 role evidence와 process 실행을 증명하지 않는다.
- Kotlin TicTacToe 단일 process 실행은 통과했다. 여섯 sample 전체의 결과로 확대하지
  않는다.

다음 범위는 아직 충족으로 올릴 수 없다.

- feature-map에 scenario ID가 적힌 것과 exact selector가 실제 runner에 연결되는 것
- child runner 하나가 exit 0을 반환하는 것과 common inventory 전체가 실행되는 것
- compile/build 통과와 role server endpoint, client-visible result, server evidence
- public option이 존재하는 것과 실제 production call path에서 timeout, cancellation,
  HWM, owner, generation, cleanup이 적용되는 것
- historical log, source type 존재, unit test, 부분, 미구현, diagnostic_only,
  source_only 항목

## 5. JK-IMP-* production implementation gap

각 항목은 common contract, Java/Kotlin source, 현재 동작, 목표 동작, 수정 순서와
완료 evidence를 함께 기록한다. public surface가 common spec 또는 exact interface와
다르면 spec을 목표 계약으로 둔다.

### JK-IMP-001 — Stream HWM의 drop 방향과 drop evidence

- 상태: gap. common stream spec이 목표 동작을 이미 고정했으므로 contract 선행은
  필요하지 않다.
- 계약 경로:
  framework/doc/framework/common/spec/32-stream-connector.ko.md:532-539,
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:127-160.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamDispatchQueue.java:42-50,93-113,
  관련 test는 ConnectorDispatchTest.java와 ZLinkStreamConnectorTest.java이다.
- 확인한 동작과 기대 동작:
  packetName이 있고 수신 message 수가 HWM 이상이면 dropOldestReceivedMessage를
  먼저 호출한 뒤 새 item을 queue에 넣는다. common spec은 handler-less Send를
  queue에 보존하고, full queue에서는 새로 도착한 Send를 버리며
  ReceivedMessageDropped evidence를 남기도록 요구한다.
- 판정 근거:
  source branch가 기존 item 제거 후 새 item enqueue를 선택한다. 현재 test는
  receivedCount를 확인하지만 새 message drop event와 기존 queue 보존을 process
  계약으로 증명하지 않는다.
- 수정 목록:
  bounded queue의 drop policy와 event type/reason/count를 common contract에
  맞춘다. 호출자가 queue policy를 우회하는 helper나 raw frame API는 추가하지 않는다.
- 필요한 회귀 test: JK-REG-001. HWM 직전의 기존 message, HWM 초과의 새 Send,
  ReceivedMessageDropped, queue order와 receivedCount를 Java/Kotlin 양쪽에서
  확인한다.
- 선행 조건과 작업 순서: R0 contract review → R1 queue design review →
  runtime unit/integration → focused stream process E2E.
- 구현 완료 evidence: 새 message가 처리되지 않고 기존 queue sequence가 유지되며
  exactly-once drop evidence가 observer 또는 지정된 client-visible 경로에 나타난다.

### JK-IMP-002 — handler-less receive와 waitFor의 queue ownership

- 상태: gap.
- 계약 경로:
  framework/doc/framework/common/spec/32-stream-connector.ko.md:532-560,
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:109-125.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamReceiveDispatcher.java:157-184,
  ZLinkStreamDispatchQueue.java,
  framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt:78-115.
- 확인한 동작과 기대 동작:
  dispatcher는 등록된 handler가 없으면 return한다. 따라서 handler를 등록하지 않은
  상태에서 들어온 Send가 waitFor가 소비할 queue item으로 보존되는지 source path가
  보장하지 않는다. common contract는 handler-less Send를 queue에 넣고 waitFor가
  dispatch mode와 무관하게 아직 소비되지 않은 packet을 소비하도록 요구한다.
- 판정 근거:
  receive path의 빈 handler early return과 queue enqueue path가 분리되어 있다.
  compile과 receivedCount test만으로 handler-less wait, timeout, expectNone,
  sequence ordering을 증명하지 못한다.
- 수정 목록:
  receive queue를 handler registration보다 먼저 소유하도록 call path를 재배치한다.
  handler dispatch와 wait consumer가 같은 item을 중복 소비하지 않도록 ownership과
  exactly-once 규칙을 명시하고 timeout/cancellation 뒤 cleanup을 확인한다.
- 필요한 회귀 test: JK-REG-002. handler 없는 waitFor, 늦은 handler 등록,
  MANUAL과 automatic dispatch, timeout, expectNone, sequence order를 양 언어로
  실행한다.
- 선행 조건과 작업 순서: JK-IMP-001의 queue policy 결정 후 R1에서 수행한다.
- 구현 완료 evidence: handler가 없을 때도 Send가 queue에 남고 한 consumer만 item을
  소비하며, timeout과 close가 계약된 terminal error로 완료한다.

### JK-IMP-003 — Stream public surface와 Kotlin option copy drift

- 상태: gap. public surface 결정은 contract 선행으로 분리한다.
- 계약 경로:
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:50-160,
  framework/doc/framework/common/spec/server/languages/java/interfaces/stream-session.ko.md,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/stream-session.ko.md.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamConnector.java:5-15,
  DefaultZLinkStreamConnector.java:105-107,
  framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt:50-76,78-98,
  ZLinkStreamConnectorOptions.java:6-29,73-118.
- 확인한 동작과 기대 동작:
  Java stream exact interface의 공개 목록에는 없는 receivedCount(String)가
  ZLinkStreamConnector와 Kotlin wrapper에 공개되어 있다. options record에는
  maxInboundObserverNotifications와 maxInboundObserverPayloadPreviewBytes가
  존재하지만 Kotlin copyStreamCompression은 구형 constructor를 호출해 두 값을
  보존하지 않는다. exact interface는 observer option과 기본값을 계약에 포함한다.
- 판정 근거:
  source, exact interface, Kotlin wrapper의 public shape가 동일하지 않다. 다른
  언어 API나 E2E를 근거로 receivedCount를 추가하거나 제거하지 않고 common/exact
  contract와 API snapshot owner를 먼저 결정한다.
- 수정 목록:
  exact contract의 option field가 모든 Kotlin copy/DSL path에서 보존되도록
  constructor/copy path를 정리한다. receivedCount는 contract 포함 또는
  제거/비공개화를 review 뒤 결정하고 결정 전 caller usage를 만들지 않는다.
- 필요한 회귀 test: JK-REG-005. Java/Kotlin public method 목록 snapshot, options
  copy 후 observer limit 보존, default value와 builder signature를 확인한다.
- 선행 조건과 작업 순서: R0 exact interface 결정 → R1 runtime adapter 정리 →
  module boundary와 package consumer.
- 구현 완료 evidence: Java/Kotlin API snapshot이 exact interface와 같고
  compression copy 후 모든 option 값이 보존된다.

### JK-IMP-004 — inbound content type과 codec error mapping

- 상태: gap.
- 계약 경로:
  framework/doc/framework/common/spec/06-framework-api.ko.md:411-438,
  framework/doc/framework/common/spec/04-async-execution-policy.ko.md와 exact
  handler interface의 error/terminal 규칙.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/internal/codec/ZLinkCodecRegistration.java:121-194,
  internal/dispatch/ZLinkChannelHandlerInvoker.java:83-124,161-181,231-262,
  internal/dispatch/ZLinkMeshApplicationDispatcher.java:131-152,247-284,
  public context ZLinkMessageContext.java:13.
- 확인한 동작과 기대 동작:
  common contract는 JSON을 default codec으로 사용하되, inbound envelope이 등록되지
  않은 non-JSON content type을 선언하면 JSON으로 fallback하지 않고 ProtocolError로
  완료하도록 요구한다. 현재 dispatcher는 packet name, payload, metadata를 추출한
  뒤 invoker를 호출하지만 wire content type을 decode decision에 전달하지 않는다.
  invoker는 등록된 Java type과 fallback serializer를 기준으로 deserialize한다.
- 판정 근거:
  실제 inbound call path에서 wire content type을 검사하는 지점이 보이지 않고
  serializerWithFallback가 JSON fallback을 선택할 수 있다. unknown non-JSON,
  malformed payload, handler 미실행, error callback, terminal completion을 하나의
  process 계약으로 확인한 test도 현재 evidence에서 찾지 못했다.
- 수정 목록:
  dispatcher에서 wire content type을 보존해 codec registry에 전달한다. 등록되지 않은
  type은 JSON parser로 넘기지 않고 지정된 ProtocolError와 terminal reason으로
  집약한다. codec은 payload bytes와 type mapping만 책임지고 caller에 parse/decode
  처리를 추가하지 않는다.
- 필요한 회귀 test: JK-REG-003. JSON default, registered Protobuf/MessagePack,
  unknown non-JSON, malformed payload, handler non-execution, error callback count,
  request/send terminal mapping을 양 언어 process에서 확인한다.
- 선행 조건과 작업 순서: R0 error mapping review → runtime call path → codec
  unit/integration → RegistrationCodec E2E → sample.
- 구현 완료 evidence: unknown content type이 JSON decode 없이 정확한 ProtocolError로
  끝나고 callback/error evidence가 exactly-once이며 정상 extension codec은 유지된다.

### JK-IMP-005 — Actor local join의 authority와 callback 순서

- 상태: gap.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/actors.ko.md:63-84,105-126,
  framework/doc/framework/common/spec/15-spot-actor.ko.md:29-47,
  Kotlin actor exact interface의 owner/generation/join 규칙.
- 구현 경로:
  framework/languages/java/zlink-framework-spring-boot-starter/src/main/java/systems/zlink/framework/spring/internal/actor/ZLinkActorSpotAdmission.java:246-300,
  같은 파일의 commitRoutedActor와 commitDeferredJoinRelocation 경로:392-472.
- 확인한 동작과 기대 동작:
  completeLocalJoinFromCaller는 pending join을 꺼낸 뒤
  runtime.leaveSourceForLocalMove(actor)를 먼저 실행하고 markJoined, joined
  callback, commitJoinedLocation, remote move completion 순으로 진행한다. common
  actor contract는 target restore/reservation과 owner/membership CAS를 먼저 확정하고,
  callback은 committed join 결과에 대해 한 번 실행하며 source cleanup 실패가
  target authority를 되돌리는 숨은 경계가 되지 않도록 요구한다. routed transfer는
  target commit 뒤 materialize/session bind/callback으로 가는 별도 path이므로
  같은 invariant를 독립적으로 확인한다.
- 판정 근거:
  local move path의 source cleanup이 target authority 확정과 callback보다 앞선다.
  callback failure, CAS loser, stale generation, cleanup failure, retry와 restart에서
  owner/generation/queue가 보존되는 현재 process evidence가 없다.
- 수정 목록:
  admission preflight, target reservation, owner/generation CAS, callback, source
  cleanup, durable commit, replay 책임을 분리한다. infrastructure callback과
  application callback을 구분하고 exactly-once를 집약한다. local과 routed path가
  같은 domain invariant를 사용하게 한다.
- 필요한 회귀 test: JK-REG-004. callback success/failure, CAS loser, stale handle,
  cleanup failure, retry, process restart/takeover, backlog replay와 terminal reason을
  owner/generation evidence로 확인한다.
- 선행 조건과 작업 순서: R0 DDD authority model → R2 admission design review →
  runtime integration → SpotActorTransfer와 ToActorMessaging E2E.
- 구현 완료 evidence: target authority와 generation이 정해진 순서로 한 번만
  변경되고 callback count가 정확히 한 번이며 failure cleanup과 replay evidence가
  contract에 맞는다.

### JK-IMP-006 — global ActorId dispatch와 builder signature

- 상태: gap. public signature는 contract 선행이 필요하다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:521,
  framework/doc/framework/common/spec/server/languages/java/interfaces/stream-session.ko.md:10,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/stream-session.ko.md:6-12.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamNodeBuilder.java:23-27,
  StreamBuilders.java:107-108, StreamNodeRegistration.java:120와 Kotlin stream DSL.
- 확인한 동작과 기대 동작:
  exact interface는 enableActorDispatch() no-argument로 선언하고 ActorId가 현재
  authority/Mesh를 선택하도록 한다. Java source는
  enableActorDispatch(String meshName)을 공개해 호출자에게 MeshName을 요구한다.
- 판정 근거:
  parameter 수와 owner 선택 경계가 다르다. MeshName을 caller에게 요구하는 것은
  exact contract의 global ActorId 의미와 다른 public decision이다.
- 수정 목록:
  common/exact contract가 no-argument surface를 목표로 하는지 R0에서 확정한다.
  목표가 유지되면 builder, registration, dispatch resolver에서 MeshName을 내부
  authority resolution으로 이동한다. contract 변경이 필요하면 먼저 exact docs와
  API snapshot을 바꾸고 implementation gap으로 재기록한다.
- 필요한 회귀 test: JK-REG-005와 JK-REG-010. no-argument compile, 서로 다른
  Mesh의 동일 ActorId dispatch, current owner와 stale owner error를 확인한다.
- 선행 조건과 작업 순서: contract 선행 → R2 builder/runtime → Kotlin compile →
  actor dispatch E2E.
- 구현 완료 evidence: caller가 MeshName을 전달하지 않아도 현재 authority로
  dispatch되고 stale/relocated owner는 contract error로 끝난다.

### JK-IMP-007 — Fanout setRoutingIdPrefix public member 누락

- 상태: gap. exact Java interface에 근거가 있으므로 implementation parity가
  우선이다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:202-214.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/FanoutChannelBuilder.java:5-37,
  MeshNodeRegistration.java:329-333.
- 확인한 동작과 기대 동작:
  exact configuration contract는 Fanout channel builder가 routing ID prefix를
  설정할 수 있다고 선언한다. 현재 같은 setter는 mesh node registration 쪽에는
  있으나 FanoutChannelBuilder에는 없다.
- 판정 근거:
  public builder에서 contract member가 누락되었고 caller가 다른 계층의 registration
  API로 우회해야 한다.
- 수정 목록:
  기존 builder 책임 안에 routing prefix를 보존하고 channel registration에 전달한다.
  node-level setter를 호출부 workaround로 노출하지 않는다. package export와
  Java/Kotlin API snapshot을 같은 결정에 맞춘다.
- 필요한 회귀 test: JK-REG-006. builder signature/default, generated routing ID,
  duplicate policy, reconnect와 fanout publish 경계를 확인한다.
- 선행 조건과 작업 순서: R0 API snapshot review → runtime registration →
  contract test → PubSub/SubmitAdmission E2E.
- 구현 완료 evidence: exact method가 clean consumer에서 호출되고 routing prefix가
  server evidence와 API snapshot에 동일하게 나타난다.

### JK-IMP-008 — RouteMesh runtime option의 extra public surface

- 상태: gap, contract 선행.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/channel-messaging.ko.md,
  Kotlin 대응 channel-messaging 문서, 00-public-contract-governance.ko.md:75-101.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/ZLinkRouteMeshRuntimeOptions.java:3-10,
  Java module exports, Kotlin public package, API contract test.
- 확인한 동작과 기대 동작:
  exact channel-messaging surface가 소유하는 mesh(String)와 channel(String) 외에
  source에는 meshNode(String)와 channel(String meshName, String channelName)이
  공개되어 있다. 이 overload가 caller-visible routing/authority 결정을 넓히는지
  common contract와 snapshot에서 확인되지 않았다.
- 판정 근거:
  source에 존재한다는 사실은 계약 근거가 아니다. extra surface를 언어별 임의
  public API로 승인하지 않는다.
- 수정 목록:
  각 extra method의 contract 근거를 찾거나 contract 선행 설계 후보로 분리한다.
  근거가 없으면 public export에서 제거/비공개화하고 내부 module port로 이동한다.
- 필요한 회귀 test: JK-REG-012. Java/Kotlin exported package와 public API snapshot을
  exact inventory와 비교하고 forbidden internal package가 consumer에 노출되지
  않는지 확인한다.
- 선행 조건과 작업 순서: R0 contract inventory review → API decision → runtime
  cleanup → package consumer.
- 구현 완료 evidence: extra method가 contracted 또는 non-public으로 명시되고
  clean consumer와 snapshot이 같은 결과를 낸다.

### JK-IMP-009 — application-wide inbound HWM의 production 적용 증거

- 상태: gap. 구현 불일치 여부는 runtime call-path 검증 후 확정한다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:217-221,
  common backpressure/HWM spec, Kotlin configuration exact interface.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/dispatch/ZLinkInboundDispatchOptions.java:5-21,
  ZLinkInboundDispatchRegistration.java, ZLinkFrameworkRuntimeStatus.java:10-27,
  관련 runtime admission, queue, shutdown test source.
- 확인한 동작과 기대 동작:
  public option에는 host-wide inbound Application byte budget과 profile 설정이
  존재한다. 그러나 현재 source/test search에서 option이 실제 handler admission,
  pause/resume, completion permit, runtime status, process shutdown과 연결되는
  production evidence와 ApplicationHwm 회귀 test를 확인하지 못했다. common contract는
  application 전체 byte budget, threshold, completion과 cancellation 경계를 요구한다.
- 판정 근거:
  setter와 record의 존재는 HWM semantics 적용 증거가 아니다. 현재는 미구현이라고
  단정하지 않고 production call path가 목표를 충족하는지 증거가 없는 gap으로
  관리한다.
- 수정 목록:
  HWM authority를 process-wide admission layer에 두고 중복 accounting을 피한다.
  threshold 진입·해제, deadline/cancellation, callback completion, concurrent
  shutdown, status/observability를 하나의 state machine으로 연결한다.
- 필요한 회귀 test: JK-REG-007. byte budget saturation, new admission rejection,
  accepted completion, cancellation release, shutdown in-flight operation, status
  transition과 owner evidence를 확인한다.
- 선행 조건과 작업 순서: R0 HWM invariant review → R2 runtime call-path audit →
  integration/fake backend → monitoring/SubmitAdmission E2E.
- 구현 완료 evidence: process-wide byte accounting이 production path에서 한 번만
  적용되고 permit release와 terminal reason이 status snapshot 및 role evidence와
  일치한다.

## 6. JK-E2E-IMP-* Java/Kotlin E2E implementation gap

공통 E2E의 구현됨 표기는 process runner가 exact ID를 dispatch하고, 실제 role endpoint를
호출하고, client result와 server evidence를 assertion한 경우에만 완료로 승격한다.

### JK-E2E-IMP-001 — Java feature-map의 exact ID, alias, 누락

- 상태: gap.
- 기준 경로: framework/doc/framework/common/e2e/config-1-location-messaging.ko.md부터
  config-14-instance-spot.ko.md.
- 구현 경로: framework/languages/java/e2e/*/feature-map.ko.md,
  framework/languages/java/e2e/*/Client, 각 run_e2e.sh.
- 현재 비교:
  - Config 1 RM은 common ID와 일치하지만 common에 없는 RM-C10이 extra이다.
  - Config 2 SM은 SM-B0A, SM-G5A, SM-G5B가 없고 SM-G5 alias가 있다.
  - Config 3 PS는 PS-D7A, PS-D7B, PS-E2A, PS-E2B, PS-E2C가 없고
    PS-D, PS-D7, PS-E2 alias가 있다.
  - Config 4 RC, Config 5 RL, Config 6 SF, Config 8 TD, Config 9 TA, Config 13
    SA는 common ID가 map에 있다. SA의 부분·미구현 상태는 all 성공으로 계산하지
    않는다.
  - Config 7 MON은 MON-A4A, MON-A4B, MON-D1A, MON-D1B가 없고 MON-A4, MON-D1
    alias가 있다.
  - Config 10 ST는 ST-E1B, ST-E1C가 없다.
  - Config 11 OBS는 OBS-C9A, OBS-C9B가 없고 OBS-C9 alias가 있다.
  - Config 12 CH-E2E-01, 02, 03, 04A, 04B, 04C, 05, 06, 07A, 07B, 07C, 08, 09,
    10, 11, 12의 16개가 feature-map/runner에 없다.
  - Config 14 IS-E2E-32부터 IS-E2E-36이 없고 현재 Java는 feature-map만 있으며
    runner가 없다. IS-E2E-01부터 31은 map 존재일 뿐 process 완료 증거가 아니다.
- 판정 근거:
  alias가 존재해도 common scenario ID와 selector가 exact match하지 않으면 같은
  scenario로 취급하지 않는다. Config 12와 14는 presence 자체가 부족하다.
- 수정 목록:
  common inventory를 입력으로 exact ID diff를 만들고 alias를 common ID별 row로
  확장한다. 각 row에 implemented, partial, unimplemented, diagnostic_only,
  source_only와 process evidence 경로를 고정한다.
- 필요한 회귀 test: JK-REG-008. common heading ID와 Java feature-map, selector,
  runner file을 exact set으로 비교하고 missing/extra/alias를 각각 실패로 출력한다.
- 선행 조건과 작업 순서: runtime 완료 → E0 inventory freeze → Java fixture/selector
  → focused process → aggregate.
- 구현 완료 evidence: Java Config 1–14 exact ID diff가 empty이고 각 ID가 실제
  client dispatch와 role endpoint evidence로 연결된다.

### JK-E2E-IMP-002 — Kotlin feature-map의 exact ID, alias, 누락

- 상태: gap.
- 기준 경로: framework/doc/framework/common/e2e/config-1-location-messaging.ko.md부터
  config-14-instance-spot.ko.md.
- 구현 경로: framework/languages/java/e2e-kotlin/*/feature-map.ko.md,
  framework/languages/java/e2e-kotlin/*/Client, 각 run_e2e.sh.
- 현재 비교:
  - Config 1 RM은 RM-A3, RM-A7이 누락되었다.
  - Config 2 SM은 SM-A9, SM-A10, SM-A11, SM-A12, SM-A13, SM-B0, SM-B0A,
    SM-B10, SM-B11, SM-G5A, SM-G5B가 누락되었고 common에 없는 SM-Q9가 extra이다.
  - Config 3 PS는 PS-D7A, PS-D7B, PS-E2A, PS-E2B, PS-E2C, PS-F1, PS-F2,
    PS-F3, PS-F4, PS-F5가 누락되었고 PS-D, PS-D7, PS-E2가 alias이다.
  - Config 4 RC는 RC-B6이 누락되었다.
  - Config 5 RL은 RL-E1부터 RL-E5, RL-F1부터 RL-F14가 누락되었다.
  - Config 6 SF는 SF-B3, SF-C3, SF-C4, SF-C5, SF-F1부터 SF-F11, SF-G1부터
    SF-G3가 누락되었다.
  - Config 7 MON은 MON-A4A, MON-A4B, MON-A6, MON-D1A, MON-D1B가 누락되었고
    MON-A4, MON-D1이 alias이다.
  - Config 8 TD는 TD-D4, TD-D5, TD-D6, TD-E2A, TD-F5A가 누락되었다.
  - Config 9 TA는 common ID가 있으나 TA-A2-, TA-B1-처럼 trailing hyphen token이
    extra이다.
  - Config 10 ST는 ST-E1B, ST-E1C, ST-G2, ST-G3, ST-G4, ST-G5, ST-G6, ST-H2,
    ST-H3, ST-H4, ST-H4A, ST-H4B, ST-H5가 누락되었다.
  - Config 11 OBS는 OBS-A5, OBS-C6, OBS-C7, OBS-C8, OBS-C9A, OBS-C9B, OBS-C10,
    OBS-C11, OBS-C12가 누락되었다.
  - Config 12의 16개, Config 13의 20개, Config 14의 36개가 모두 누락되고 해당
    Kotlin runner도 없다.
- 판정 근거:
  Kotlin aggregate에 DiscoveryRegistryHa 같은 추가 suite가 있어도 common ID
  coverage를 대신하지 않는다. alias와 trailing hyphen은 selector contract를
  증명하지 않는다.
- 수정 목록:
  Java와 같은 inventory generator를 사용하되 Kotlin source path, suspend/Flow
  callback, role evidence를 별도 schema로 기록한다. common ID마다 selector
  dispatch와 terminal assertion을 연결한다.
- 필요한 회귀 test: JK-REG-008에 Kotlin exact set을 추가하고 JK-REG-010에서
  suspend/Flow client result와 server evidence 순서를 확인한다.
- 선행 조건과 작업 순서: Kotlin public/runtime compile drift 해결 → E0 inventory
  → fixture/selector → focused process → aggregate.
- 구현 완료 evidence: Kotlin Config 1–14 exact ID diff가 empty이고 extra suite는
  common inventory와 별도 extension으로 표시되며 all pass에 포함되지 않는다.

### JK-E2E-IMP-003 — aggregate runner 누락과 false all success

- 상태: gap.
- 공통 기준 경로:
  framework/doc/framework/common/e2e/config-12-channel-egress-routing.ko.md,
  config-13-submit-admission.ko.md, config-14-instance-spot.ko.md.
- Java 경로: framework/languages/java/e2e/run_e2e_all.sh:9-22. default list에는
  12 suite가 있으나 ChannelEgressRouting과 InstanceSpot이 없다.
- Kotlin 경로: framework/languages/java/e2e-kotlin/run_e2e_all.sh:7-19. default
  list에는 11 suite가 있으나 Config 12, 13, 14가 없다.
- 확인한 동작과 기대 동작:
  두 aggregate는 child runner exit code를 기준으로 pass를 계산하고 retry는 bind
  pattern 중심이다. common inventory, feature-map status, selector, evidence
  schema를 읽어 partial이나 누락을 거부하지 않는다. 따라서 실행하지 않은 suite가
  있어도 aggregate가 passed를 출력할 수 있다.
- 판정 근거:
  all은 전체 common E2E를 뜻하지만 현재 runner는 subset을 실행한다. Config 13
  Java map의 부분·미구현 row도 runner가 별도로 차단하지 않는다.
- 수정 목록:
  aggregate가 common inventory와 language map을 먼저 exact diff하고 missing,
  extra, alias, status를 실패로 출력하게 한다. all은 exact ID selector를 모두
  dispatch하고 client result, role evidence, terminal reason, callback count,
  owner/generation/cleanup을 포함한다.
- 필요한 회귀 test: JK-REG-009. missing suite, alias, partial, diagnostic_only,
  source_only, child exit 0/no evidence, retry 후 중복 callback을 aggregate failure로
  검증한다.
- 선행 조건과 작업 순서: JK-E2E-IMP-001/002 inventory → E1 runner contract →
  focused ID → Java/Kotlin aggregate.
- 구현 완료 evidence: all이 374개 common ID를 exact dispatch하고 한 ID라도 누락,
  status 미충족, evidence 부족이면 non-zero와 machine-readable reason을 반환한다.

### JK-E2E-IMP-004 — focused process가 role server 전에 compile에서 중단

- 상태: gap. 현재 환경/build graph blocker와 Kotlin E2E implementation gap을
  함께 기록한다.
- 기준 경로:
  framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md,
  config-9-to-actor-messaging.ko.md, config-14-instance-spot.ko.md.
- Java 경로:
  framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh,
  SpotActorTransfer/Client, SpotActorTransfer/Server,
  framework/languages/java/zlink-framework-core/src/main/java/module-info.java:4.
- Kotlin 경로:
  framework/languages/java/e2e-kotlin/SpotActorTransfer/run_e2e.sh,
  ActorNodeHttpServer.kt:94-96, TransferComponents.kt:136-141,181-190.
- 확인한 동작과 기대 동작:
  Java ST-A1은 zlink.framework.binding.internal module을 찾지 못해 role process가
  시작되지 않는다. Kotlin ST-A1은 getOrCreate 인자 수, toCompletableFuture,
  non-exhaustive when, abstract/final context override 오류로 compile되지 않는다.
  기대 동작은 client, source role, target role, store/evidence endpoint가 실제로
  시작되고 selector가 public API만으로 transfer를 실행하는 것이다.
- 판정 근거:
  현재 ST-A1에는 client-visible result나 role server evidence가 없다. 과거 process
  log를 현재 성공으로 대체하지 않는다.
- 수정 목록:
  Java module graph와 package/local dependency를 clean build에서 재현 가능하게
  정리하되 application이 internal module을 import하는 우회는 금지한다. Kotlin은
  exact public interface에 맞게 call builder, coroutine terminal, context
  implementation을 정리하고 readiness barrier와 bounded cleanup을 확인한다.
- 필요한 회귀 test: JK-REG-010. focused process에서 transfer terminal reason,
  callback count, old/new owner, generation, target cleanup, replay를 client와 role
  evidence 양쪽에서 assertion한다.
- 선행 조건과 작업 순서: runtime actor contract JK-IMP-005/006 → Java/Kotlin
  compile → focused ST-A1/ST-A2 → aggregate.
- 구현 완료 evidence: compile failure 없이 role이 시작되고 process 종료 뒤 모든
  role이 bounded time 안에 종료되며 client result와 server evidence가 같은
  generation/owner transition을 가리킨다.

### JK-E2E-IMP-005 — client architecture와 configuration policy 위반

- 상태: gap.
- 기준 경로:
  framework/doc/framework/common/e2e/config-1-location-messaging.ko.md,
  config-7-monitoring.ko.md, config-10-spot-actor-relocation.ko.md,
  framework/doc/framework/common/sample/README.ko.md.
- 구현 경로:
  framework/languages/java/validate_sample_e2e_configuration_policy.sh,
  Java/Kotlin e2e/*/Client, e2e-kotlin/*/Client, 각 Shared/Env.java와 Env.kt.
- 확인한 동작과 기대 동작:
  policy script는 현재 System.getenv, System.getProperty,
  ZLINK_JAVA_*, ZLINK_KOTLIN_*를 application source에서 읽는 항목을 보고해
  exit 1을 반환한다. 대표 경로는 Java e2e/SpotActorTransfer/Shared/Env.java,
  Kotlin e2e-kotlin/SpotService/Shared/Env.kt, RegistrationCodec/Shared/Env.kt,
  RuntimeMonitoring/Shared/Env.kt, ResilienceLifecycle/Shared/Env.kt이다. Java
  RuntimeMonitoring의 MonBPublishMonitoringAbsenceScenario.java:4와
  AutomaticTurnDispatch support의 java.lang.reflect 사용은 private access 우회인지
  목적을 직접 확인해야 한다.
- 판정 근거:
  internal package import의 static 위반은 발견하지 않았지만 policy gate 자체가
  실패하고 configuration 책임이 application source로 들어가 있다. reflection은
  review가 끝나기 전까지 충족으로 올리지 않는다.
- 수정 목록:
  runner/testkit가 process argument 또는 명시된 role configuration을 주입하고
  application client는 public API와 scenario input만 사용하도록 정리한다. private/
  internal API, reflection workaround, raw frame, test-only adapter를 추가하지
  않는다. evidence 수집은 server role endpoint에 둔다.
- 필요한 회귀 test: JK-REG-009와 JK-REG-010에 environment/JVM property,
  internal import, reflection private access, raw frame 검사를 포함한다.
- 선행 조건과 작업 순서: E0 role/config boundary review → E1 runner injection →
  focused process → aggregate.
- 구현 완료 evidence: policy script가 통과하고 client source에 금지된 access가
  없으며 모든 scenario가 client result와 role evidence를 출력한다.

### JK-E2E-IMP-006 — role evidence와 terminal assertion schema 부족

- 상태: gap.
- 기준 경로:
  common E2E 1–14의 evidence 절, 특히 config-7-monitoring.ko.md,
  config-10-spot-actor-relocation.ko.md, config-13-submit-admission.ko.md,
  config-14-instance-spot.ko.md.
- 구현 경로:
  Java/Kotlin feature-map, Client/Scenario*, Server/*/Evidence*,
  run_e2e.sh, run_e2e_all.sh.
- 확인한 동작과 기대 동작:
  feature-map에는 부분, 미구현, source-only 설명이 섞여 있고 Java Config 14는
  map만 존재한다. SubmitAdmission Java map의 SA-E2E-01, 04, 05, 08, 09, 14, 20은
  부분 구현으로 기록되어 있다. aggregate는 이 상태를 machine-readable result로
  읽지 않는다. 기대 동작은 client-visible terminal reason과 role server의 callback
  count, owner, generation, cleanup, queue/replay evidence를 하나의 scenario result로
  비교하는 것이다.
- 판정 근거:
  historical log, source type 존재, unit test만으로 process E2E 완료를 표시할 수
  없다. 현재 ST-A1 compile failure 때문에 실제 evidence가 생성되지도 않았다.
- 수정 목록:
  scenario result에 status, terminalReason, clientAssertions, roleEvidence,
  callbackCount, owner, generation, cleanup, attempts를 필수로 둔다. evidence가
  없는 implemented를 aggregate pass로 허용하지 않는다.
- 필요한 회귀 test: JK-REG-010. 정상, timeout, cancellation, stale owner,
  relocation, shutdown, cleanup failure를 terminal reason과 evidence로 비교하고
  callback count가 0/1/2 이상일 때 정확히 실패한다.
- 선행 조건과 작업 순서: runtime evidence schema → E1 role server adapter →
  focused scenario → aggregate.
- 구현 완료 evidence: 각 common ID에 독립 evidence artifact가 있고 client 결과와
  server evidence가 같은 sequence/generation을 가리키며 evidence 누락은 non-zero로
  끝난다.

## 7. JK-SAMPLE-IMP-* sample gap

Java/Kotlin sample은 common sample 문서의 public API 예제이다. E2E나 sample을
통과시키기 위해 내부 runtime, raw codec, 임시 adapter, 호출부 parse/decode를
추가하지 않는다.

### JK-SAMPLE-IMP-001 — aggregate sample gate의 stale fake backend selector

- 상태: gap.
- 기준 경로: framework/doc/framework/common/sample/README.ko.md와 각 sample README의
  self-check, smoke, completion 절.
- 구현 경로:
  framework/languages/java/samples/run_samples.sh:43-50,
  framework/languages/java/zlink-framework-testkit/src/test,
  framework/languages/java/samples/sample-manifest.env.
- 확인한 동작과 기대 동작:
  runner는 SampleReleaseGateContractTest 뒤
  ActorRuntimeFakeBackendTest.entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback
  method를 --tests로 지정한다. 현재 Gradle test discovery는 이 method를 찾지 못해
  sample process 전에 실패했다. 기대 동작은 현재 test inventory와 일치하는 gate
  뒤 manifest의 12 process sample을 실행하는 것이다.
- 판정 근거:
  aggregate runner와 test source의 selector가 일치하지 않는다. buildAllSamples
  통과는 이 gate를 대체하지 않는다.
- 수정 목록:
  test method와 runner selector의 source owner를 하나로 정리하고 method가 없으면
  명확한 inventory failure를 낸다. stale selector를 임의의 다른 test로 바꾸어
  green으로 만들지 않는다.
- 필요한 회귀 test: JK-REG-011. sample gate selector가 실제 test를 찾고 manifest
  6+6개가 모두 실행되며 compile-only/skip이면 실패하는지 확인한다.
- 선행 조건과 작업 순서: E2E/runtime gate → sample gate repair → direct process →
  aggregate.
- 구현 완료 evidence: fake backend와 release gate가 통과하고 12 process가 각각
  client self-check와 server evidence를 남긴다.

### JK-SAMPLE-IMP-002 — Java TicTacToe의 asynchronous HTTP terminal drift

- 상태: gap.
- 기준 경로:
  framework/doc/framework/common/sample/tictactoe/README.ko.md,
  framework/doc/framework/common/spec/http-client/languages/java/java-http-client.ko.md,
  common async execution policy.
- 구현 경로:
  framework/languages/java/samples/java/TicTacToe/Client/src/main/java/systems/zlink/samples/tictactoe/client/TicTacToeClientScenario.java:34,
  Kotlin 대응 sample과 sample policy gate.
- 확인한 동작과 기대 동작:
  Java client가 fetch(CreateGameHttpRes.class).toCompletableFuture().join()을
  사용해 current asynchronous HTTP terminal policy에서 시작 전에 실패했다.
  Kotlin TicTacToe는 단일 process 실행이 통과했다. 기대 동작은 양 언어 client가
  exact HTTP public call builder와 asynchronous terminal을 사용하고 timeout,
  cancellation, error body/status를 계약대로 확인하는 것이다.
- 판정 근거:
  Java sample은 common sample public usage example로서 current exact HTTP contract와
  맞지 않는다. Kotlin 단일 성공으로 Java contract를 면제할 수 없다.
- 수정 목록:
  Java TicTacToe를 exact async terminal로 옮기고 여섯 Java sample을 같은 stale
  synchronous call pattern으로 scan한다. raw JSON parse나 internal HTTP adapter는
  호출부에 넣지 않는다.
- 필요한 회귀 test: JK-REG-011에 HTTP request, status, response body, timeout,
  cancellation, process cleanup을 포함한다.
- 선행 조건과 작업 순서: HTTP exact surface/package consumer → Java sample migration
  → TicTacToe focused → six Java/Kotlin aggregate.
- 구현 완료 evidence: 양 언어 여섯 sample이 async terminal을 사용하고 expected
  HTTP status/body와 server evidence를 직접 assertion한다.

### JK-SAMPLE-IMP-003 — sample/E2E application source의 환경 설정 경계

- 상태: gap.
- 기준 경로:
  framework/doc/framework/common/sample/README.ko.md,
  framework/doc/framework/common/sample/*/README.ko.md,
  framework/languages/java/validate_sample_e2e_configuration_policy.sh.
- 구현 경로:
  Java/Kotlin samples/*/Server, samples/*/Client, shared Env.java/Env.kt,
  samples/run_samples.sh.
- 확인한 동작과 기대 동작:
  common policy gate는 application source의 System.getenv와 System.getProperty
  사용을 금지한다. 현재 E2E shared source에서 해당 위반이 보고된다. sample
  process는 runner가 endpoint와 role 설정을 전달하고 application code는 public
  Framework API로 동작해야 한다.
- 판정 근거:
  configuration을 source helper가 직접 읽으면 role boundary와 재현 가능한 process
  입력이 불명확해지고 policy gate가 실패한다.
- 수정 목록:
  runner-owned configuration을 process argument 또는 명시된 config object로
  전달한다. common sample 사용법을 바꾸기 위해 내부 runtime 설정을 public sample
  API로 노출하지 않는다.
- 필요한 회귀 test: JK-REG-011과 policy static gate에서 금지된 configuration
  access, internal package, raw frame, per-message codec workaround를 검사한다.
- 선행 조건과 작업 순서: E2E configuration boundary review → sample launcher →
  여섯 sample direct run → aggregate.
- 구현 완료 evidence: policy script가 통과하고 sample application source는 public
  contract만 사용하며 동일 입력으로 양 언어 process를 재현할 수 있다.

### JK-SAMPLE-IMP-004 — 여섯 sample의 process self-check와 server evidence

- 상태: gap.
- 기준 경로:
  framework/doc/framework/common/sample/bingo/README.ko.md,
  deliverydispatch/README.ko.md, event/gamequest.ko.md,
  event/shoppingmall.ko.md, supportchat/README.ko.md, tictactoe/README.ko.md.
- 구현 경로:
  framework/languages/java/samples/java/*/run_sample.sh,
  framework/languages/java/samples/kotlin/*/run_sample.sh,
  각 sample Client, Server, Shared, sample-manifest.env.
- 확인한 동작과 기대 동작:
  buildAllSamples는 통과했지만 Java TicTacToe는 process 전에 실패했고 aggregate는
  stale fake backend selector에서 중단됐다. Kotlin TicTacToe 하나만 PASS했다.
  common sample 문서는 client self-check, role server result, smoke output,
  completion evidence를 요구한다.
- 판정 근거:
  compile/build 또는 단일 sample 성공은 12 process sample의 public contract와
  evidence를 증명하지 않는다.
- 수정 목록:
  각 sample client assertion과 server evidence marker를 manifest schema에 연결한다.
  readiness와 cleanup을 bounded barrier로 관리하고 어느 role/terminal/evidence가
  부족한지 출력한다.
- 필요한 회귀 test: JK-REG-011. Bingo, TicTacToe, SupportChat, DeliveryDispatch,
  ShoppingMall, GameQuest의 양 언어 12 process와 package mode를 실행한다.
- 선행 조건과 작업 순서: JK-SAMPLE-IMP-001부터 003 → sample별 focused run →
  aggregate.
- 구현 완료 evidence: 12 sample이 README self-check와 completion 조건을 충족하고
  compile-only나 source-only로 pass되지 않는다.

## 8. JK-TEST-* audit, regression, CI gap

### JK-TEST-001 — common inventory diff가 첫 실패만 보고

- 상태: gap.
- 기준 경로: framework/doc/framework/common/e2e/ 전체.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/JavaDocumentationRegressionTest.java,
  Java/Kotlin feature-map과 aggregate runner.
- 확인한 동작과 기대 동작:
  current contractTest는 Config 14 fixture/aggregate 누락에서 실패한다. 그러나
  Config 1–14 exact diff, alias, extra, status, runner/evidence 누락을 한 결과로
  제공하지 않는다. 기대 동작은 첫 failure에서 끝나지 않고 모든 차이를
  machine-readable report로 내는 것이다.
- 판정 근거:
  test failure는 유효하지만 missing ID 일부만 보고하면 후속 작업 순서를 잘못
  정할 수 있다. test를 약화해 green으로 만들지 않는다.
- 수정 목록:
  common heading parser와 feature-map parser를 공유하고 missing, extra, alias,
  status, runner, evidence를 모두 출력한다. Java와 Kotlin을 같은 gate에서
  비교하되 language-specific path는 보존한다.
- 필요한 회귀 test: JK-REG-008, JK-REG-009.
- 선행 조건과 작업 순서: E0 inventory schema → audit test 개선 → contractTest.
- 구현 완료 evidence: common 374 ID의 language별 exact diff를 모두 출력하고,
  실패 결과가 aggregate runner와 일치한다.

### JK-TEST-002 — Java/Kotlin CI workflow와 path filter coverage

- 상태: gap.
- 기준 경로:
  .github/workflows/docs.yml, build.yml, framework-node.yml,
  framework-dotnet.yml, Java/Kotlin aggregate runner와 sample runner.
- 확인한 동작과 기대 동작:
  현재 workflow 목록에는 별도 Java/Kotlin framework workflow가 없다. docs.yml은
  framework/doc/framework/** path에서 document contract와 doc example compile을
  실행하지만 Java/Kotlin runtime, E2E aggregate, sample aggregate 전체를 실행하는
  gate로 확인되지 않았다. Node와 .NET workflow의 path filter는 Java/Kotlin
  implementation 변경을 소유하지 않는다.
- 판정 근거:
  Java/Kotlin 변경이 문서 workflow의 일부 compile만 통과하고 runtime/E2E/sample
  gate를 건너뛸 수 있다. 필요한 gate와 path ownership 연결이 명시되지 않았다.
- 수정 목록:
  Java/Kotlin CI job, path filter, dependency/package preparation, timeout,
  cleanup, skip-list 검사를 명시한다. all을 호출하되 partial, diagnostic_only,
  source_only를 skip하지 못하게 한다.
- 필요한 회귀 test: JK-REG-009, JK-REG-011, JK-REG-012를 CI에서 실행하고 path
  matrix 변경 시 job 선택 여부를 workflow validation으로 확인한다.
- 선행 조건과 작업 순서: runtime/package gate → E2E aggregate → sample aggregate
  → workflow review.
- 구현 완료 evidence: Java/Kotlin production, package consumer, focused/aggregate
  E2E, 12 sample process가 해당 path filter에서 자동 실행되고 skip은 blocker
  report로만 허용된다.

### JK-TEST-003 — API snapshot과 clean package consumer gate

- 상태: gap. 현재 환경 dependency blocker가 있다.
- 기준 경로:
  framework/doc/contract-inventory/jvm-public-contract-source-owners.json,
  framework/doc/framework/common/spec/00-public-contract-governance.ko.md:130-146.
- 구현 경로:
  framework/languages/java/scripts/verify_packaged_contract.sh,
  framework/languages/java/zlink-framework-core/src/main/java/module-info.java,
  core, Kotlin, stream connector, HTTP client package와 clean consumer.
- 확인한 동작과 기대 동작:
  package script가 temporary Maven repository를 사용했지만
  systems.zlink:zlink-framework-provider-abstractions:0.1.0-SNAPSHOT와
  systems.zlink:zlink-http-client:0.3.1을 resolve하지 못했다. 따라서 public
  package export, module boundary, transitive dependency, API snapshot을 clean
  consumer가 모두 확인한 결과가 없다.
- 판정 근거:
  source compile이 통과해도 배포 package consumer가 실패하면 public contract
  완료가 아니다. 이 실패는 runtime gap과 별도의 packaging/CI blocker다.
- 수정 목록:
  package script가 declared dependency를 temporary repository에 공급하거나 정확한
  external repository를 사용하게 한다. Java internal module은 application export가
  아니라 source-owner JSON의 qualified consumer boundary를 유지한다.
- 필요한 회귀 test: JK-REG-012. Java/Kotlin clean consumer, module boundary,
  package metadata, API snapshot, HTTP/stream transitive dependency를 확인한다.
- 선행 조건과 작업 순서: source/API inventory → dependency publication → package
  consumer → runtime/E2E/sample.
- 구현 완료 evidence: clean consumer가 Java와 Kotlin artifact를 resolve하고
  compile하며 package export와 API snapshot diff가 empty이다.

## 9. 작업 순서

작업 순서는 반드시 runtime → E2E → sample이다. 앞 단계의 contract 또는 production
call path가 확정되지 않은 상태에서 뒤 단계의 sample/client 코드를 복잡하게 만들어
통과시키지 않는다.

### 0단계 — audit baseline과 작업 경계 고정

1. dirty working tree의 status와 대상 파일 manifest를 보존한다. unrelated 변경을
   staging하거나 되돌리지 않는다.
2. common spec, Java/Kotlin exact interface, source-owner JSON, common E2E 374 ID,
   common sample six-set을 diff artifact로 고정한다.
3. codex sol agent의 실제 사용 가능 여부와 review 결과 형식을 확인한다. 지정 agent를
   사용할 수 없으면 review를 blocked로 기록하고 다른 agent로 대체하지 않는다.
4. JK-REG-008 inventory report와 JK-TEST-001 audit output을 먼저 만든다.

### 1단계 — runtime parity와 public contract

순서는 queue admission, codec/content type, actor authority, HWM/monitoring,
public surface/package 순서다.

1. JK-IMP-001, JK-IMP-002: stream queue ownership, HWM, handler-less wait를
   고친다.
2. JK-IMP-004: inbound content type, codec registry, ProtocolError mapping과
   callback/terminal semantics를 고친다.
3. JK-IMP-005, JK-IMP-006: actor join/relocation authority, owner/generation,
   global ActorId dispatch와 builder signature를 고친다.
4. JK-IMP-009: application-wide HWM, admission/preflight, deadline,
   cancellation, shutdown, status evidence를 연결한다.
5. JK-IMP-003, JK-IMP-007, JK-IMP-008: exact public surface, Fanout builder,
   RouteMesh options, Kotlin wrapper, API snapshot을 정리한다.
6. 각 항목의 regression test를 먼저 추가하거나 현재 test를 목표 계약에 맞춘 뒤
   compile, unit, integration, fake backend, contract test를 실행한다.
7. Java와 Kotlin packaged consumer가 통과할 때까지 E2E 수정으로 넘어가지 않는다.

runtime 단계 완료 조건:

- Java/Kotlin exact interface와 source export/API snapshot diff가 정리되었다.
- HWM, codec, actor, stream production call path가 직접 실행된 regression test를
  가진다.
- concurrent shutdown, in-flight operation, cancellation, retry/rollback/cleanup의
  terminal reason이 test evidence에 있다.
- verifyPublicContractModuleBoundary, core/Kotlin/stream test, package consumer가
  통과한다.

### 2단계 — E2E process와 aggregate

1. JK-E2E-IMP-001, JK-E2E-IMP-002의 exact inventory를 해결한다. common ID를
   alias로 합치지 않고 Java와 Kotlin row를 각각 만든다.
2. JK-E2E-IMP-003의 aggregate runner를 고쳐 Config 1–14를 모두 소유하게 한다.
   child exit 0만 보지 말고 machine-readable result와 evidence completeness를
   검증한다.
3. JK-E2E-IMP-004의 Java module graph와 Kotlin public API compile blocker를
   해결하고 role readiness barrier를 확인한다.
4. JK-E2E-IMP-005의 client configuration, public API, role boundary를 정리한다.
5. JK-E2E-IMP-006의 evidence schema를 도입한다. terminal reason, callback count,
   owner, generation, cleanup, replay, attempt를 role server와 client에서 함께
   확인한다.
6. focused scenario를 실행한 뒤 Java aggregate와 Kotlin aggregate를 실행한다.
   timeout/hang은 skip이 아니라 blocker evidence로 남긴다.

E2E 단계 완료 조건:

- Java/Kotlin 각각 common 374 ID exact inventory diff가 empty이다.
- 모든 all runner가 Config 12–14를 포함한다.
- actual role server endpoint와 client public API가 process에서 연결된다.
- client result와 server evidence가 모두 있고 하나라도 없으면 실패한다.
- partial, unimplemented, diagnostic_only, source_only는 all 성공에 포함되지 않는다.

### 3단계 — sample process와 package mode

1. JK-SAMPLE-IMP-001의 stale fake backend selector와 release gate를 고친다.
2. JK-SAMPLE-IMP-002의 Java async HTTP terminal을 고치고 여섯 Java sample을 같은
   stale call pattern으로 점검한다.
3. JK-SAMPLE-IMP-003의 runner-owned configuration 경계를 고친다.
4. 각 sample의 client self-check, server evidence, readiness, cleanup을 확인한다.
5. Java 여섯 개와 Kotlin 여섯 개를 direct 실행하고 package mode 뒤 aggregate를
   실행한다.

sample 단계 완료 조건:

- Java/Kotlin 12 sample process가 실제로 실행된다.
- HTTP status/body, Framework terminal, handler/callback, actor/spot evidence를
  sample 문서의 self-check와 함께 확인한다.
- build-only, source-only, historical log-only 결과가 pass로 계산되지 않는다.
- package mode와 clean package consumer가 같은 public surface를 사용한다.

## 10. POSD·DDD review와 commit/push gate

이 절은 구현자가 지켜야 할 작업 절차이다. 현재 audit에서는 agent review, commit,
push를 실행하지 않았다.

### 10.1 review round

각 round에서 codex sol agent는 candidate SHA 또는 변경 manifest를 기준으로 다음을
검토한다.

| round | 시점 | POSD 확인 | DDD 확인 | 통과 조건 |
|---|---|---|---|---|
| R0 | runtime contract/public surface 전 | shallow/pass-through API, 정보 누출, caller에게 transport/codec/authority 결정을 넘기는지 | Runtime, Transport, Codec, Location Store bounded context와 public port | contract 선행 항목의 결정이 spec에 고정됨 |
| R1 | stream/codec/HWM runtime 후보 후 | queue·codec·admission 책임이 아래 계층에 흡수되는지, 중복 option/helper가 없는지 | message와 transport payload, HWM invariant owner | production call path와 regression evidence 일치 |
| R2 | actor/relocation/lifecycle 후보 후 | 시간적 분해, 숨은 rollback, callback pass-through 제거 | Actor/Spot aggregate, ActorId/SpotId/generation value, owner boundary | owner/CAS/callback/replay 순서 일치 |
| E0 | E2E runner 전 | special-case dispatch와 application 내부 결정이 없는지 | client와 role server bounded context 및 port/adapter 경계 | exact inventory와 evidence schema 확정 |
| E1 | focused E2E 후 | evidence adapter가 caller에게 내부 지식을 새지 않는지 | role server가 authority evidence를 소유하는지 | focused와 aggregate가 같은 evidence contract 사용 |
| S0 | sample 변경 전 | codec/transport/registry workaround가 없는지 | sample domain/application/infrastructure 경계 | common public usage 유지 |
| S1 | 12 sample 후 | 반복 launcher/helper와 shallow adapter 정리 | domain event와 transport message 경계 | direct, package, aggregate 결과 일치 |
| F0 | 최종 audit | 전체 diff, API snapshot, package metadata, CI path 누락 | contract owner와 runtime/evidence owner 일치 | CLEAN 승인 후 commit/push |

DDD review에서 확인할 invariant:

- Actor identity, current owner, location, generation, store version, lease는 하나의
  authority model로 다룬다.
- Spot/Actor admission은 예약, preflight, owner 변경, callback, cleanup, replay의
  순서를 application caller에게 노출하지 않는다.
- transport message와 domain event를 같은 DTO로 재사용해 ownership을 흐리지 않는다.
- codec, HTTP client, stream connector는 framework port/adapter 책임을 갖고 sample
  caller에게 bytes parsing을 넘기지 않는다.
- Location Store는 authority record를 소유하고 route cache는 authority를 대신하지
  않는다.

### 10.2 codex sol agent review 결과 형식

각 review는 다음 항목을 포함한다.

    reviewer: codex sol agent
    round: R0 | R1 | R2 | E0 | E1 | S0 | S1 | F0
    candidate: <commit-or-manifest>
    scope: <paths>
    decision: DESIGN_ACCEPTED | CLEAN | NOT_CLEAN | BLOCKED
    contract_evidence: <spec/exact-interface/API-snapshot paths>
    runtime_evidence: <source call path and test/process results>
    e2e_evidence: <scenario IDs and role evidence>
    sample_evidence: <sample IDs and self-check>
    findings:
      - severity: blocker | major | minor
        category: POSD | DDD | contract | runtime | e2e | sample | CI
        location: <path:line>
        expected: <target behavior>
        observed: <current behavior>
        action: <required change>
    verification: <commands and result>

BLOCKED는 환경 또는 agent availability를 기록하는 상태이며 성공으로 승격하지
않는다. agent가 unavailable이면 대체 agent를 사용하지 않고 사용자의 결정 또는
환경 복구를 기다린다.

### 10.3 commit과 push 절차

1. dirty working tree의 기존 변경을 다시 status로 확인하고 이번 stage의 변경
   manifest만 만든다.
2. codex sol agent review가 DESIGN_ACCEPTED 또는 CLEAN이어야 한다.
3. path-limited staging만 사용한다. git add -A, unrelated file staging, 기존
   사용자 변경을 포함하는 commit은 금지한다.
4. stage별 commit을 하나의 책임으로 만든다. 예시는 다음과 같다.
   - fix(java-kotlin): align stream admission with common contract
   - test(java-kotlin): enforce exact e2e inventory evidence
   - test(java-kotlin): verify six samples in process mode
5. commit 후 같은 revision으로 focused test, aggregate, package consumer를 다시
   실행한다.
6. push 전 F0 review에서 commit diff와 remote branch target을 확인한다. push는
   사용자가 정한 branch와 권한이 확인된 경우에만 수행한다.
7. push 뒤 CI path filter가 실제 Java/Kotlin job을 선택했는지 확인하고 skip이나
   timeout을 pass로 기록하지 않는다.

## 11. 기존 regression test의 유지·변경·추가 목록

### 유지할 항목

- JavaDocumentationRegressionTest의 common spec ownership 검증
- LocationContractTest의 forbidden old public symbol과 Java/Kotlin package boundary
  검증
- zlink-stream-connector의 connector lifecycle, options default, dispatch,
  receivedCount 관련 현재 test
- zlink-framework-kotlin의 KotlinPublicSurfaceContractTest
- 각 E2E run_e2e.sh의 readiness, bounded cleanup, process exit 검증
- 각 sample run_sample.sh, sample-manifest.env, release/package gate

유지한다는 뜻은 현재 assertion을 그대로 승인한다는 뜻이 아니다. exact contract와
common E2E inventory에 맞지 않는 assertion은 아래 변경 ID로 재검토한다.

### 변경할 항목

- JavaDocumentationRegressionTest: 첫 failure에서 중단하지 않고 Java/Kotlin
  common 374 ID의 missing/extra/alias/status/runner/evidence diff를 출력한다.
- aggregate runner: child exit 0만 보지 않고 machine-readable scenario result와
  evidence completeness를 확인한다.
- stream test: queue count를 drop direction, handler-less wait, drop event,
  sequence, timeout/cancellation assertion으로 확장한다.
- actor/relocation test: callback count뿐 아니라 owner, generation, cleanup,
  store record, replay order를 확인한다.
- package verification: temporary repository dependency와 clean consumer/API
  snapshot을 같은 gate에서 확인한다.
- sample aggregate: stale fake backend selector를 현재 test inventory와 맞추고
  12 process 결과를 compile-only와 분리한다.

### 추가할 regression ID

아래 12개는 이 ledger가 제안하는 regression ID이다. 구현이 끝났다는 뜻이 아니라
각 gap의 완료 evidence를 고정하기 위한 작업 항목이다.

- JK-REG-001: Stream HWM full queue의 새 Send drop, 기존 queue order,
  ReceivedMessageDropped exactly-once
- JK-REG-002: handler-less waitFor, expectNone, timeout, manual/automatic dispatch,
  sequence
- JK-REG-003: unknown non-JSON ProtocolError, JSON/extension codec 정상 경로,
  handler/error callback count
- JK-REG-004: actor join/relocation authority CAS, callback failure, stale generation,
  retry, cleanup, replay
- JK-REG-005: Java/Kotlin stream options copy, builder signature, public method snapshot
- JK-REG-006: Fanout routing prefix, generated routing ID, duplicate/reconnect behavior
- JK-REG-007: application-wide HWM byte accounting, permit release, cancellation,
  shutdown, runtime status
- JK-REG-008: common 374 ID와 Java/Kotlin feature-map exact diff
- JK-REG-009: aggregate rejection of missing, alias, partial, diagnostic-only,
  source-only, evidence-less child
- JK-REG-010: role server/client evidence schema와 terminal reason, callback, owner,
  generation, cleanup, replay
- JK-REG-011: Java/Kotlin 여섯 sample의 HTTP async terminal, self-check, server
  evidence, package mode, aggregate
- JK-REG-012: API snapshot, module boundary, clean Java/Kotlin package consumer,
  HTTP/stream transitive dependency

## 12. 완료 판정 checklist

### Runtime

- [ ] common spec과 Java/Kotlin exact interface를 읽고 source owner를 지정했다.
- [ ] 함수·클래스·interface 이름, parameter, return type, optional/default 값을
  Java/Kotlin 양쪽에서 비교했다.
- [ ] error type/code, timeout, cancellation reason, callback exactly-once,
  ownership/disposal, serialization/content type, HTTP status/body를 비교했다.
- [ ] stream HWM은 새 message drop과 event semantics를 증명한다.
- [ ] handler-less Send가 queue에 보존되고 waitFor가 올바른 consumer가 된다.
- [ ] codec은 wire content type을 확인하고 unknown non-JSON을 JSON으로 parse하지
  않는다.
- [ ] actor/spot admission의 preflight, authority, owner/generation, callback,
  rollback, cleanup, replay 순서가 common contract와 같다.
- [ ] application-wide HWM, deadline, cancellation, concurrent shutdown,
  in-flight operation terminal state를 증명한다.
- [ ] public extra/missing surface를 contract 선행 또는 exact implementation gap으로
  분류했다.
- [ ] API snapshot, module boundary, package export와 clean consumer가 통과했다.
- [ ] JK-REG-001부터 JK-REG-007, JK-REG-012가 통과했다.

### E2E

- [ ] common Config 1–14의 374 scenario ID를 inventory로 고정했다.
- [ ] Java feature-map의 missing/extra/alias와 Kotlin feature-map의
  missing/extra/alias를 모두 해소했다.
- [ ] Java aggregate에 Config 12와 Config 14가 포함된다.
- [ ] Kotlin aggregate에 Config 12, Config 13, Config 14가 포함된다.
- [ ] exact selector가 실제 scenario dispatch와 runner process에 연결된다.
- [ ] client는 public client API만 사용하고 internal runtime/store, private
  reflection, raw-frame 우회를 사용하지 않는다.
- [ ] role server endpoint가 실제로 호출되고 readiness/cleanup이 bounded하다.
- [ ] client-visible result와 role server evidence를 함께 확인한다.
- [ ] terminal reason, callback count, owner, generation, cleanup, replay를
  필요한 scenario에서 직접 assertion한다.
- [ ] historical log, source-only, unit-only, partial, unimplemented,
  diagnostic_only 항목은 all 성공으로 계산되지 않는다.
- [ ] JK-REG-008부터 JK-REG-010이 통과한다.

### Sample와 CI

- [ ] Java/Kotlin Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall,
  GameQuest가 각각 실제 process로 실행된다.
- [ ] Java sample의 synchronous HTTP terminal drift가 없다.
- [ ] sample/E2E application source의 environment/JVM property policy gate가
  통과한다.
- [ ] sample별 client self-check, server evidence, smoke/completion 결과가 있다.
- [ ] fake backend/release gate selector가 실제 test inventory와 일치한다.
- [ ] buildAllSamples와 samples/run_samples.sh aggregate가 통과한다.
- [ ] package mode와 clean package consumer가 통과한다.
- [ ] Java/Kotlin CI job, path filter, skip list가 runtime/E2E/sample gate를 빠뜨리지
  않는다.
- [ ] POSD/DDD review round R0, R1, R2, E0, E1, S0, S1, F0의 결과가 있다.
- [ ] 각 stage commit은 path-limited이고 unrelated 사용자 변경을 포함하지 않는다.
- [ ] push 후 CI 결과가 같은 revision의 증거로 기록된다.

## 13. 이 문서의 현재 작업량과 미해결 blocker

이 ledger가 추가하는 gap ID는 다음과 같다.

- production implementation/runtime: JK-IMP-001부터 JK-IMP-009까지 9개
- Java/Kotlin E2E implementation/runner/process evidence:
  JK-E2E-IMP-001부터 JK-E2E-IMP-006까지 6개
- sample implementation/runner/process evidence:
  JK-SAMPLE-IMP-001부터 JK-SAMPLE-IMP-004까지 4개
- audit/regression/CI gate: JK-TEST-001부터 JK-TEST-003까지 3개
- 합계: 22개

추가·변경할 regression ID는 JK-REG-001부터 JK-REG-012까지 12개이다.

현재 해결하지 않은 blocker는 다음과 같다.

- Java focused E2E가 zlink.framework.binding.internal module compile에서 중단된다.
- Kotlin focused E2E가 getOrCreate, coroutine terminal, context 구현 계약 compile에서
  중단된다.
- Java/Kotlin packaged consumer가 provider abstractions와 HTTP client dependency를
  temporary repository에서 resolve하지 못한다.
- samples/run_samples.sh가 현재 없는 fake backend test method를 지정한다.
- Java TicTacToe가 asynchronous HTTP terminal policy를 위반한다.
- Java/Kotlin sample/E2E configuration policy gate가 environment/JVM property
  access를 보고한다.
- Java/Kotlin 전용 CI workflow와 runtime/E2E/sample aggregate의 path ownership이
  현재 명시적으로 연결되어 있지 않다.

이 문서는 위 blocker를 해결했다고 표시하지 않는다. 구현 단계에서 먼저 runtime,
그 다음 E2E, 마지막으로 sample 순서를 지키고, 각 단계의 codex sol agent POSD·DDD
review와 commit/push evidence가 확보된 뒤 다음 단계로 진행한다.
