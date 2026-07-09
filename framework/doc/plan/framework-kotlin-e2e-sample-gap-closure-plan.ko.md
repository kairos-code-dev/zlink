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
- `framework/doc/plan/framework-ref-target-unification-plan.ko.md`
- `framework/doc/plan/framework-ref-target-unification-kotlin-worker-prompt.ko.md`

이 문서는 위 계획들을 한 작업 흐름으로 묶는 완료 계획이다.

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
- ActorRef/SpotRef 전송 대상 통일 기준과 Kotlin 작업 지시:
  `framework/doc/plan/framework-ref-target-unification-plan.ko.md`
  `framework/doc/plan/framework-ref-target-unification-kotlin-worker-prompt.ko.md`

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

## 메시지 핸들러 등록 정책 포함 범위

Kotlin E2E/Sample 갭 제거 중 handler 등록 표면을 고치거나 새 sample/E2E handler를 추가할 때는
`framework/doc/framework/common/spec/framework-api.ko.md`의 메시지 handler 정책을 같은 작업 범위에
포함한다. 특히 `framework-api.ko.md`의 `3.3 Handler 등록 정책`은 Kotlin sample과 E2E의 handler 등록
방식이 따라야 하는 공통 기준이다.

적용 기준:

- handler 등록 호출부가 packet 이름, actor 타입, request/send/subscription 종류처럼 handler 타입에서
  알 수 있는 정보를 반복해서 받지 않도록 한다.
- 수동 등록은 실행 문맥의 구성 단계에서 이뤄져야 한다. Kotlin에서는 channel handler는 application
  startup/channel builder, session handler는 session 구성, Entry Spot과 user Spot handler는 각 Spot
  구성 문맥에 둔다.
- Spot 메시지 handler는 actor request/send, Spot packet, subscription 책임을 handler 타입과 metadata로
  드러낸다. Kotlin sample과 E2E의 Spot handler 등록 표면은
  `context.handlers().addHandler<MyHandler>()` 하나만 사용한다.
- `context.handlers().addPacket<MyHandler>()`,
  `context.handlers().addActorRequest<MyHandler>()`,
  `context.handlers().addActorSend<MyHandler>()`,
  `context.handlers().addSubscribe<MyHandler>("topic")` 같은 세부 등록 함수는 Kotlin sample/E2E 표준
  표면으로 사용하지 않는다.
- subscription topic처럼 handler interface만으로 알 수 없는 값은 handler metadata, annotation, 또는
  언어별 metadata 선언에 둔다. 등록 호출의 반복 인자로 숨기지 않는다.
- timer는 메시지 dispatch handler가 아니므로 메시지 handler 등록 정책으로 우회하지 않는다. timer 이름,
  주기, overrun 정책처럼 실행 계획에 속한 값은 별도 timer 등록 표면에서 다룬다.
- 자동 등록과 수동 등록이 같은 dispatch key를 만들면 startup validation 오류로 처리한다. 조용히
  덮어쓰거나 수동 등록이 자동 등록을 대신하게 만들지 않는다.
- 현재 Kotlin/Java public API로 공통 정책을 표현할 수 없으면 Java internal package, coroutine helper,
  테스트 전용 adapter로 우회하지 말고 `feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 public
  contract gap으로 남긴 뒤 설계 검토 항목으로 분리한다.

이 정책은 sample이나 E2E를 통과시키기 위한 구조 검사 회피가 아니라 public 사용 예시의 품질 기준이다.
새 sample/E2E가 handler 책임을 shared helper, raw frame adapter, test-only registry로 밀어내면 완료로
인정하지 않는다.

## Sample handler 등록 방식 정리

Kotlin sample은 메시지 handler 정책을 사용자-facing 예시로 보여 주는 영역이므로, sample별 handler 등록
방식을 아래처럼 정리한다. 이 정책은 sample에 적용하며, E2E는 scenario별 검증 목적과 공통 E2E 문서의
요구에 맞춰 별도 판단한다.

- `TicTacToe` sample만 handler 수동 등록을 사용한다. TicTacToe는 작은 sample이라
  `context.handlers().addHandler<MyHandler>()`가 어느 실행 문맥에 붙는지 직접 보여 주는 예시로 둔다.
- `Bingo`, `DeliveryDispatch`, `SupportChat`, `GameQuest`, `ShoppingMall` sample은 handler 자동 등록만
  사용한다. sample 코드에서 handler 목록을 반복해서 나열하지 않는다.
- 자동 등록을 쓰는 sample에서 새 handler를 추가하면, handler 타입의 interface와 metadata만으로 실행
  문맥, packet 이름, request/send/subscription 종류를 판정할 수 있어야 한다.
- subscription topic처럼 handler interface만으로 알 수 없는 값은 annotation 또는 언어별 metadata에 둔다.
  자동 등록 sample에서 topic을 등록 호출부 인자로 넘기는 방식은 사용하지 않는다.
- 자동 등록과 수동 등록을 섞어 같은 dispatch key를 만들지 않는다. TicTacToe 외 sample에 수동 등록이
  필요해 보이면 sample 코드에 우회 등록을 넣지 말고 public contract gap으로 분리한다.
- 완료 전에는 sample release gate나 별도 문서 검증으로 TicTacToe를 제외한 Kotlin sample에
  `context.handlers().addHandler<...>()` 같은 수동 handler 등록 호출이 남아 있지 않은지 확인한다.

### 분리된 bug report: 재접속 actor-bound session reply

Kotlin SupportChat runner에서 재접속 agent가 닫힌 대화 검증을 통과한 뒤
`SetAgentAvailableReq(false)`를 보내면, support actor handler는 reply하지만 stream client가 reply frame을
받지 못한다.

확인된 증거:

- `framework/languages/java/samples/kotlin/SupportChat/sample-porting-inventory.ko.md`의
  `Blocker: 재접속 actor-bound session reply`.
- `/tmp/tmp.OVoz83uF8G/sample-logs/flow-session.log`: `SetAgentAvailableReq corr=5`는 session에서
  `RECEIVED`까지만 기록된다.
- `/tmp/tmp.OVoz83uF8G/sample-logs/flow-support.log`: 같은 `SetAgentAvailableReq corr=5 actor=agent-1`은
  support actor에서 `RECEIVED`와 `REPLIED`가 모두 기록된다.
- `/tmp/tmp.867Qdp2xxR/logs/client.log`: stream trace 실행에서 corr=5 request write 뒤 reply frame 없이
  `TimeoutException`이 난다.
- `/tmp/tmp.6zOPg638eh/logs/support.log`: 재접속 뒤 actor-bound source가 `sourceSession=6`으로 갱신되고
  support 쪽 bound-session send 호출은 성공으로 반환된다. 그런데 client에는 corr=5 reply frame이 없다.
- `/tmp/tmp.txphDtjFaH/logs/client.log`: actor request reply를 source session으로 직접 반환하는 실험은
  초기 `SetAgentAvailableReq`부터 timeout되어 core/binding reply 계약과 맞지 않았다. 이 우회는 남기지
  않는다.
- Java SupportChat 기본 runner는 2026-07-07 trace 실행에서 통과했지만, 재접속 뒤
  `SetAgentAvailableReq(false)` no-agent 경로를 포함하지 않으므로 이 blocker를 닫는 증거가 아니다.

이 문제는 Kotlin sample domain이나 coroutine helper만의 문제가 아니라 Java 공용 framework의 actor-bound
session reply 경로, Java binding, root core stream actor binding까지 이어질 수 있다. `core/`를 수정하지
않는 현재 작업에서는 Kotlin 샘플 우회로 닫지 말고, Java/C++/Node 또는 binding 수준 재현을 추가로
확인한 뒤 별도 core/framework bug fix 작업으로 처리한다.

상세 재현 기록은
[`framework-kotlin-supportchat-actor-bound-reply-bug.ko.md`](framework-kotlin-supportchat-actor-bound-reply-bug.ko.md)에
분리했다.

### 분리된 bug report: YieldDispatch BindActors session bind

Kotlin `YD-D1` local topology 검증을 강화하는 과정에서 공통 actor setup인 `BindActorsReq`가
Java/Kotlin 양쪽 YieldDispatch runner에서 timeout되는 현상을 확인했다. Kotlin 로그
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-232341-3929581`와 Java 로그
`framework/languages/java/e2e/YieldDispatch/logs/20260707-232452-3934593` 모두 session이 route mesh
reply를 받은 뒤 client reply가 완료되지 않는다.

이 문제는 Kotlin scenario만 통과시키는 우회로 닫지 않는다. 현재 checkout에서는 Java framework
auto-connect 변경이 반영된 뒤 Kotlin `YD-B1` focused runner
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-233558-3987976`와 `YD-D1` focused
runner `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-233650-3991306`가 통과했다.
상세 재현 기록은
[`framework-java-kotlin-yd-bindactors-session-bind-bug.ko.md`](framework-java-kotlin-yd-bindactors-session-bind-bug.ko.md)에
분리했다.

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

## ActorRef/SpotRef 전송 대상 통일

Kotlin extension, Kotlin sample, Kotlin e2e는 Java framework의 전송 대상 이름 통일을 함께 따라간다.
Kotlin은 Java public contract 위에 coroutine helper를 제공하므로, 값 개념 이름은 Java와 다르게
유지하지 않는다.

이 작업은 E2E와 sample gap 제거의 일부로 완료한다.
`framework-ref-target-unification-kotlin-worker-prompt.ko.md`의 Kotlin 작업 지시는 이 섹션에 흡수한
완료 조건으로 취급한다. 별도 worker prompt로 남겨 두지 않고, 아래 항목이 모두 끝나야 이 계획을 완료할
수 있다.

이름은 아래처럼 정리한다.

| 이전 이름 | 최종 이름 |
|-----------|-----------|
| `ZLinkActorRef` | `ActorRef` |
| `ZLinkActorRefSnapshot` | `ActorRefSnapshot` |
| `ZLinkSpotAddress` | `SpotRef` |

Kotlin 전용 helper도 값 개념에는 `ZLink` 접두어를 붙이지 않는다. Manager, client, store, runtime처럼
service나 role을 나타내는 타입은 Java 이름을 따른다.

필수 코드 변경 범위:

- `zlink-framework-kotlin`의 actor messaging helper는 `ActorRef` 인자로 제공한다.
- `zlink-framework-kotlin`의 spot messaging helper는 `SpotRef` 인자로 제공한다.
- actor id만 받는 `sendToActorAwait(actorId, ...)`와 `requestToActorAwait(actorId, ...)` helper는 public
  사용 표면에서 제거한다.
- spot 주소를 낡은 이름이나 id 조합으로 받는 `sendToSpot`과 `requestToSpot` helper는 `SpotRef` 인자로
  바꾼다.
- `resolveSpotAddress`는 `resolveSpotRef`로 바꾼다.
- `resolveActorSpotAddress`는 `resolveActorSpotRef`로 바꾼다.
- Kotlin sample/e2e의 `ZLinkSpotAddress(...)` 생성은 `SpotRef(...)` 생성으로 바꾼다.
- Kotlin sample/e2e의 `ZLinkActorRef` import는 `ActorRef` import로 바꾼다.

필수 확인 파일은 아래 범위를 포함한다.

```text
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkFrameworkExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkLocationExtensions.kt
framework/languages/java/zlink-framework-kotlin/src/test/kotlin/systems/zlink/framework/kotlin/KotlinFrameworkExtensionsContractTest.kt
framework/languages/java/e2e-kotlin/SpotService/Server/MultiNode/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/multinode/MultiNodeApplication.kt
framework/languages/java/e2e-kotlin/SpotService/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/SpotRouteResolver.kt
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/RemoteSpotYieldReqRouteHandler.java
framework/languages/java/e2e-kotlin/YieldDispatch/Server/Session/src/main/java/systems/zlink/e2e/kotlin/yielddispatch/SpotMsgRouteHandler.java
framework/languages/java/samples/kotlin/Bingo/Server/Session/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/BindCourierHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CourierGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/couriergateway/handlers/OfferDeliveryHandler.kt
framework/languages/java/samples/kotlin/DeliveryDispatch/Server/CustomerGateway/src/main/kotlin/systems/zlink/samples/kotlin/deliverydispatch/server/customergateway/handlers/EnsureCustomerActorHandler.kt
```

위 목록에 없더라도 `framework/languages/java/e2e-kotlin/SpotService/`,
`framework/languages/java/e2e-kotlin/YieldDispatch/`, `framework/languages/java/samples/kotlin/` 안에서
이전 이름이나 id-only helper를 쓰는 파일은 같은 작업에서 정리한다.

최소 테스트 범위는 아래 파일을 포함한다.

```text
framework/languages/java/zlink-framework-kotlin/src/test/kotlin/systems/zlink/framework/kotlin/KotlinFrameworkExtensionsContractTest.kt
framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh
framework/languages/java/e2e-kotlin/YieldDispatch/run_e2e.sh
```

문서도 같은 작업에서 갱신한다. 사용자-facing 문서에는 `ActorRef`와 `SpotRef` 기반 전송만 남기고,
Kotlin 문서가 Java API를 설명하는 경우에도 이전 Java 이름을 남기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common/
framework/doc/framework/kotlin/
```

### 2026-07-07 진행 상태

현재 checkout에서는 Java 공용 framework에 `ActorRef`, `ActorRefSnapshot`, `SpotRef` 값 타입을 추가했고,
Kotlin coroutine helper가 이 이름을 사용하도록 바꾸었다. `resolveSpotRef`와 `resolveActorSpotRef`
helper도 추가되어 Kotlin 호출 표면에서는 이전 `Address` 이름을 쓰지 않는다. `ZLinkSpotOutbound`도
`SpotRef` 인자 전송과 request를 제공하므로, spot 안이나 framework client driver spot에서도 보유한
spot 주소를 다시 resolve하지 않고 넘길 수 있다. 기존 Java 구현과 저장소 row는 아직 내부 호환 타입을
유지하므로, `ZLinkActorLocation.fromActorRef(...)`처럼 의도를 드러내는 factory를 통해 새 값 타입에서
내부 row로 변환한다.

검증:

```bash
cd framework/languages/java
nice -n 10 ./gradlew --no-daemon --no-parallel --max-workers=1 :zlink-framework-core:compileJava :zlink-framework-spring-boot-starter:compileJava :zlink-framework-kotlin:compileKotlin
cd e2e-kotlin/SpotService
nice -n 10 ../../gradlew --no-daemon --no-parallel --max-workers=1 :Client:compileKotlin
cd ../YieldDispatch
nice -n 10 ../../gradlew --no-daemon --no-parallel --max-workers=1 :Server:Play:compileJava :Server:Session:compileJava
```

결과: 2026-07-07 현재 checkout에서 통과했다.

같은 시점의 완료 검색에서 `zlink-framework-kotlin`, `e2e-kotlin`, `samples/kotlin`에는 이전 이름과
id-only helper 패턴이 남지 않았다. 이 검색 통과는 이름 통일 범위의 compile proof다. SpotService는
`SM-F2`/`SM-F5`를 각각 `logs/20260707-174845-2908845/client.stdout.log`,
`logs/20260707-174936-2911529/client.stdout.log`로 확인했고, remote actor-session 묶음인
`SM-B2`/`SM-B4`/`SM-D2`는 `logs/20260707-180549-2974527/client.stdout.log`에서 확인했다.
YieldDispatch 전체 scenario는 runner evidence로 별도 확인해야 완료된다.
SpotService는 `logs/20260707-181906-3021418`에서 `SM-G4` / `bound-push-load` focused runner를 통과해
다수 bound session push와 오배달 방지 증거를 확보했다. 이 실행은 외부 core 성능 빌드와
`core/build/lib` 파일 갱신 충돌을 피하려고, 정상 `libzlink.so`를 `/tmp/zlink-kotlin-corebuild.GOFJa8`로
복사한 뒤 `ZLINK_CORE_BUILD_DIR`와 `ZLINK_LIBRARY_PATH`를 그 사본으로 지정해서 수행했다. repo의
`core/` 파일은 수정하지 않았다.
SpotService는 `logs/20260707-183051-3064142`에서 `SM-G2` / `owner-remap` focused runner도 통과했다.
이 검증은 Play HTTP 제어가 public `ZLinkSpotManager.getOrCreate`로 각 owner spot을 만들고, public
RouteMesh `requestToSpot` 경로가 remap 전후 owner evidence를 play-a/play-b에 분리해 남기는지 확인한다.
SpotService는 `logs/20260707-183939-3102578`에서 `SM-G3` / `join-leave-race` focused runner도 통과했다.
이 검증은 public stream connector 2개가 같은 user spot에 join한 뒤 동시에 actor request와 leave
request를 실행하고, play-a evidence에 actor별 `ActorUserJoined`, `ActorUserRequest`, `ActorUserLeft`가
남는지 확인한다.
SpotService는 `logs/20260707-184546-3126591`에서 `SM-D14` / `stream-tls` focused runner도 통과했다.
이 검증은 public `ZLinkStreamNodeBuilder.setTlsServer(...)`로 self-signed TLS stream endpoint를 열고,
strict certificate validation 실패와 skip-validation actor auth/request/push 성공 경로를 확인한다.
SpotService는 `logs/20260707-185134-3145895`에서 `SM-G1` / `play-crash-recovery` focused runner도
통과했다. 이 검증은 runner가 직접 시작한 play-a PID만 종료한 뒤, client가 장애 중 request 실패를
확인하고 play-b의 state request 성공을 확인한 다음 play-a를 같은 public framework role로 재시작해
stream auth, actor join, actor request를 다시 수행하는지 확인한다. 이 실행도 repo `core/`를 수정하거나
native core를 다시 빌드하지 않고 `/tmp/zlink-kotlin-corebuild.fXBcEQ`의 `libzlink.so` 사본을 사용했다.
SpotService는 `logs/20260707-185955-3178546`에서 `SM-A5` / `stage-wrapper` focused runner도 통과했다.
Kotlin framework public API를 새로 만들지 않고, E2E 내부 `ScenarioStage` wrapper가 public spot request와
timer 표면을 감싸는 방식으로 검증했다. 이 실행은 `/tmp/zlink-kotlin-corebuild.9Xh6k9`의 `libzlink.so`
사본을 사용했고 repo `core/`는 수정하지 않았다.
`SM-D12`는 `session-transfer` 재현 mode까지 작성했지만 완료로 처리하지 않았다. `play-a`의
`EnsureActorReq`는 정상 reply되지만, Kotlin/Java framework의 public `ZLinkSessionActors.bind(ActorRef)`가
remote actor ref에 대해 완료되지 않아 stream request가 timeout된다. 다른 언어 비교와 재현 절차는
[`framework-kotlin-sm-d12-remote-session-bind-bug.ko.md`](framework-kotlin-sm-d12-remote-session-bind-bug.ko.md)로
분리했다.

ToActorMessaging은 2026-07-07 현재 checkout에서 `nice -n 10 timeout 420s ./run_e2e.sh`가 통과했다.
증거 로그는 `framework/languages/java/e2e-kotlin/ToActorMessaging/logs/20260707-172116-2831364/client.log`이고,
최종 marker는 `to-actor-messaging e2e result=passed`다. 이 runner는 public `ZLinkActorClient`,
Kotlin coroutine await extension, framework public location store API를 사용하며, raw frame 조작이나
테스트 전용 adapter를 쓰지 않는다.

YieldDispatch `YD-E3`은 focused 재현 mode를 추가했지만 완료로 처리하지 않았다. runner가 pending yield
marker를 확인한 뒤 자신이 시작한 `play-a` PID만 종료하고 같은 routing id로 재시작하면, 기존 stream
session의 recovery `ProbeReq`가 session에는 도착하지만 Play handler까지 전달되지 않아 timeout된다.
재현 로그는 `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-192818-3296525`이고, 버그
리포트는 [`framework-kotlin-yd-e3-route-recovery-bug.ko.md`](framework-kotlin-yd-e3-route-recovery-bug.ko.md)로
분리했다.

Kotlin sample runner와 E2E runner는 2026-07-07에 cleanup 정책을 점검했다. Bingo, DeliveryDispatch,
GameQuest, ShoppingMall, TicTacToe sample runner와 `e2e-kotlin/*/run_e2e.sh`에서 role 이름 pattern으로
기존 process를 찾아 종료하던 cleanup을 제거했고, runner가 직접 기록한 PID와 runner가 만든 Redis
container만 정리하도록 맞췄다. 이 변경은 사용자가 실행 중인 다른 sample/e2e/core 성능 작업과 충돌하지
않게 하기 위한 선행 조건이다.

검증:

```bash
rg -n "pgrep -f|pkill|Stop-RoleProcesses|Get-CimInstance Win32_Process|Stop-Process.*CommandLine|role_pattern|RolePattern" \
  framework/languages/java/samples/kotlin -g 'run_sample.sh' -g 'run_sample.ps1' \
  framework/languages/java/e2e-kotlin -g 'run_e2e.sh'
bash -n framework/languages/java/e2e-kotlin/*/run_e2e.sh \
  framework/languages/java/samples/kotlin/Bingo/run_sample.sh \
  framework/languages/java/samples/kotlin/ShoppingMall/run_sample.sh \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh \
  framework/languages/java/samples/kotlin/DeliveryDispatch/run_sample.sh \
  framework/languages/java/samples/kotlin/GameQuest/run_sample.sh
pwsh -NoProfile -Command '$files=@("framework/languages/java/samples/kotlin/Bingo/run_sample.ps1","framework/languages/java/samples/kotlin/ShoppingMall/run_sample.ps1","framework/languages/java/samples/kotlin/TicTacToe/run_sample.ps1","framework/languages/java/samples/kotlin/GameQuest/run_sample.ps1"); foreach ($f in $files) { $null = [scriptblock]::Create((Get-Content -Raw $f)) }'
git diff --check -- framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh \
  framework/languages/java/e2e-kotlin/RegistrationCodec/run_e2e.sh \
  framework/languages/java/e2e-kotlin/RuntimeMonitoring/run_e2e.sh \
  framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh \
  framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/run_e2e.sh \
  framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh \
  framework/languages/java/e2e-kotlin/ResilienceLifecycle/run_e2e.sh \
  framework/languages/java/samples/kotlin/Bingo/run_sample.sh \
  framework/languages/java/samples/kotlin/Bingo/run_sample.ps1 \
  framework/languages/java/samples/kotlin/ShoppingMall/run_sample.sh \
  framework/languages/java/samples/kotlin/ShoppingMall/run_sample.ps1 \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh \
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.ps1 \
  framework/languages/java/samples/kotlin/DeliveryDispatch/run_sample.sh \
  framework/languages/java/samples/kotlin/GameQuest/run_sample.sh \
  framework/languages/java/samples/kotlin/GameQuest/run_sample.ps1
```

결과: 모두 통과했다. 이 검증은 runner 안전성의 정적 proof이며, sample/e2e 완료 proof는 아니다.
동시에 다른 세션에서 C++/core native build와 .NET E2E가 CPU를 사용 중이어서 Kotlin runner 재실행은
미뤘다.

GameQuest Kotlin sample은 2026-07-07 현재 checkout에서 `nice -n 10 timeout 600s ./run_sample.sh`가
통과했다. runner는 GameApi 2개, QuestMission 2개, Client, 실행별 Redis를 띄웠고, 출력에서
`topology=ready`, `gamequest-server-evidence=completed`, `gamequest=completed`,
`gamequest kotlin full client/server self-check completed`를 확인했다. 증거 파일은
`framework/languages/java/samples/kotlin/GameQuest/build/sample-logs/client.log`와
`framework/languages/java/samples/kotlin/GameQuest/logs/flow-*.log`이다. 이 실행은 Gradle
`--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지 않았다.

ShoppingMall Kotlin sample은 2026-07-07 현재 checkout에서 `nice -n 10 timeout 600s ./run_sample.sh`가
통과했다. runner는 CommerceApi 2개, OrderWorkflow 2개, Client, 실행별 Redis를 띄웠고, 출력에서
`shoppingmall-server-evidence=completed`를 확인했다. `build/sample-logs/client.log`에는
`shoppingmall-concurrent=completed`, `shoppingmall-pending=completed`, `shoppingmall-resume=completed`,
`shoppingmall-inventory-failure=completed`, `shoppingmall-payment-failure=completed`,
`shoppingmall-rebuild=completed`, `shoppingmall-consistency=completed`, `shoppingmall-scaleout=completed`,
`shoppingmall-server-evidence=completed`, `shoppingmall=completed`가 남았다. 추가 증거 파일은
`framework/languages/java/samples/kotlin/ShoppingMall/build/sample-logs/api-a.log`,
`api-b.log`, `workflow-a.log`, `workflow-b.log`, `logs/flow-*.log`이다. 이 실행은 Gradle
`--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지 않았다.

TicTacToe Kotlin sample은 2026-07-07 현재 checkout에서 runner의 Gradle 호출을
`--no-parallel --max-workers=1`로 제한한 뒤 `nice -n 10 timeout 600s ./run_sample.sh`가 통과했다.
처음 실행은 `PASS TicTacToe.Kotlin`이었지만 flow log에 `LeaveGameMsg`가 Entry Spot으로 돌아간 actor에
전달되어 `HANDLER_MISSING`으로 drop되는 기록이 있었다. 공통 TicTacToe 문서와 `.NET` 기준은 최종
상태 확인 후 client의 명시적인 leave 요청이 room Spot의 actor send handler를 거쳐 room leave와 actor
destroy를 일으키므로, `TicTacToeGame.tick()`의 terminal-state 자동 leave를 제거했다. 재실행도
`PASS TicTacToe.Kotlin`으로 통과했고, `logs/flow-play-node-1.log`, `logs/flow-play-node-2.log`,
`logs/flow-api-50479.log`에서 `PlayerWinMilestoneMsg` pub/sub fan-out과 양쪽 stream의 `LeaveGameMsg`
dispatch를 확인했다. `rg -n "HANDLER_MISSING|ERROR" framework/languages/java/samples/kotlin/TicTacToe/logs -g '*.log'`는
no-hit이다. 이 실행도 native `core/` build는 실행하지 않았다.

ResilienceLifecycle `RL-A4`는 2026-07-07 현재 checkout에서 `nice -n 10 timeout 600s ./run_e2e.sh RL-A4`가
통과했다. runner는 provider-b를 drain하고 기존 process를 종료한 뒤 같은 routing id의 green endpoint를
시작했다. client scenario는 public topology에서 green endpoint를 확인하고, green provider의 evidence에
해당 request marker가 남는지 확인했다. 이후 green endpoint를 종료하고 원래 provider-b endpoint를 다시
띄운 뒤 public topology와 request evidence가 원래 endpoint로 복구되는지도 확인했다. 실행 출력은
`scenario RL-A4 passed`와 `resilience-lifecycle kotlin e2e result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/ResilienceLifecycle/logs/20260707-215053-3533357`이다. 이 실행은
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다.

ResilienceLifecycle `RL-D2`는 2026-07-07 현재 checkout에서 Java/Kotlin framework monitoring event를
사용해 observer failure 보고까지 확인하도록 닫았다. `ZLinkMessageFlowTracer`가 dispatch-error observer
예외를 `ZLinkRuntimeErrorEvent`로 publish하고, Provider role의 public `ZLinkRuntimeEventHandler`가 이
event를 evidence로 기록한다. client scenario는 `MESSAGE_FLOW_OBSERVER_FAILED`,
`message-flow-observer`, `dispatch observer failure` evidence를 확인한 뒤 후속 request가 정상 처리되는지
검증한다. `nice -n 10 timeout 600s ./run_e2e.sh RL-D2`가 통과했고, 실행 출력은
`scenario RL-D2 passed`와 `resilience-lifecycle kotlin e2e result=passed`였다. 증거 디렉터리는
`framework/languages/java/e2e-kotlin/ResilienceLifecycle/logs/20260707-235045-4053936`이다. 이 실행도
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다.

SpotService `SM-F4`는 공통 Config 2 문서와 `.NET` feature-map 기준에 맞춰 완료 항목으로 정리했다.
공통 문서는 malformed relay packet 주입을 public route client 표면으로 만들 수 없으므로 public E2E가
직접 주입하지 않는다고 분리한다. Kotlin runner 증거는
`framework/languages/java/e2e-kotlin/SpotService/logs/20260704-045322-67016/client-route-mesh.stdout.log`의
`scenario SM-F4-missing-route passed`와 `spot-service kotlin e2e mode=route-mesh result=passed`다.
따라서 Kotlin feature-map에서 `SM-F4`를 E2E/harness 대기 항목에서 구현 완료 항목으로 옮겼다.

SpotService `SM-F6`는 2026-07-07 현재 checkout에서 `nice -n 10 timeout 360s ./run_e2e.sh SM-F6`가
통과했다. runner는 RouteMesh를 등록하지 않은 MultiNode A/B를 `spot-only-mesh` mode로 띄우고, source
spot의 `SpotRef` 기반 request/send와 entry spot의 public actor join 경로가 remote target spot에
도달하는지 확인한다. 실행 출력은 `scenario SM-F6 passed`,
`spot-service kotlin e2e mode=spot-only-mesh result=passed`,
`spot-service kotlin e2e focused modes=spot-only-mesh result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/SpotService/logs/20260707-221028-3618449`이다. 이 실행은
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다.

YieldDispatch `YD-A3`는 2026-07-07 현재 checkout에서 공통 Config 8 문서의 의미에 맞춰 다시 정렬했다.
client scenario는 stream connector로 `YieldMsg`를 보내고, Play evidence의 `yield-started`,
`yield-released`, `yield-resumed`, `yield-completed`가 같은 request id, target spot rid, correlation id를
유지하는지 확인한다. `nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-A3`가 통과했고,
실행 출력은 `scenario YD-A3 passed`와 `yield-dispatch kotlin e2e result=passed`였다. 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-222104-3660094`이다. 동시에
YieldDispatch runner는 자신이 띄운 process만 bounded wait 뒤 cleanup하도록 고쳤고, 외부 부하에서 role
startup이 3초를 넘는 경우가 있어 readiness 대기를 10초로 늘렸다. 이 실행은 Gradle `--no-parallel
--max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지 않았다. 당시 외부 core
build와 `core/build/lib/libzlink.so` 교체가 겹쳐 default runtime load가 한 차례 실패했으므로, 재실행은
이미 존재하던 안정된 native runtime 파일을 `ZLINK_LIBRARY_PATH`로 명시해 core build 충돌을 피했다.

YieldDispatch `YD-B2`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
`YdB2SameActorReentryScenario` 의미에 맞춰 다시 정렬했다. client scenario는 같은 actor에
`ActorYieldReq`와 `ActorFastReq`를 이어서 보내고, Play evidence의 `actor-yield-started`,
`actor-yield-released`, `actor-yield-resumed`, `actor-yield-completed`, `actor-fast-started`,
`actor-fast-completed` 순서로 같은 actor mailbox가 yield 중 재진입하지 않는지 확인한다.
`nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-B2`가 통과했고, 실행 출력은
`scenario YD-B2 passed`와 `yield-dispatch kotlin e2e result=passed`였다. 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-222754-3681782`이다. 이 실행도
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다.

YieldDispatch `YD-B3`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
`YdB3ActorJoinYieldScenario` 의미에 맞춰 다시 정렬했다. client scenario는 새 actor A/B를 stream
session에 bind한 뒤 actor A의 Entry Spot handler가 public `joinSpot(...).yield()`로 target spot
admission을 기다리게 하고, actor B의 `ActorFastReq`가 join continuation보다 먼저 완료되는지 확인한다.
Play evidence 순서는 `actor-join-yield-started`, `actor-join-yield-released`, `actor-fast-started`,
`actor-fast-completed`, `actor-join-yield-resumed`, `actor-join-yield-completed`이다.
`nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-B3`가 통과했고, 실행 출력은
`scenario YD-B3 passed`와 `yield-dispatch kotlin e2e result=passed`였다. 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-224427-3751196`이다. 이 실행도
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다. 동시에 YieldDispatch runner는 port collision을 줄이기 위해 port 예약을 Gradle installDist
이후로 늦췄다.

YieldDispatch `YD-C1`과 `YD-C2`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
timer scenario 의미에 맞춰 다시 정렬했다. `YD-C1` client scenario는 unique timer Spot을 만든 뒤
yield timer와 fast timer를 시작하고, Play evidence에서 `timer-yield-started`,
`timer-yield-released`, `timer-fast-started`, `timer-fast-completed`, `timer-yield-resumed`,
`timer-yield-completed` 순서를 확인한다. `YD-C2` client scenario는 같은 timer의 다음 tick이 첫 yield
continuation과 완료 뒤 처리되는지 `timer-yield-*`와 `timer-next-*` marker 순서로 확인한다. timer
evidence에는 spot rid, timer 이름, timer mailbox, tick id를 함께 남겨 같은 timer mailbox의 재진입
여부를 확인할 수 있게 했다.

검증은 `nice -n 10`과 Gradle `--no-parallel --max-workers=1`로 진행했고, native `core/` build는
실행하지 않았다. `:Client:compileJava :Server:Play:compileJava`가 통과했고,
`nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-C1`과 같은 `YD-C2` focused
runner가 각각 통과했다. 실행 출력은 `scenario YD-C1 passed`, `scenario YD-C2 passed`,
`yield-dispatch kotlin e2e result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-225303-3789894`와
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-225337-3792863`이다.

YieldDispatch `YD-C3`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
`YdC3ActorTimerIsolationScenario` 의미에 맞춰 추가했다. client scenario는 unique Spot에 actor A/B를
bind하고 join한 뒤, actor A yield 중 같은 Spot의 fast timer tick이 먼저 완료되는지 확인한다. 이어서
timer yield 중 actor B fast request가 먼저 완료되는지 같은 Play evidence로 확인한다. 모든 시작 packet은
client stream connector에서 Session gateway로 들어가며, timer와 actor handler evidence는 공통 marker
순서를 따른다. `:Client:compileJava`가 통과했고,
`nice -n 10 timeout 600s ./run_e2e.sh YD-C3` focused runner가 통과했다. 실행 출력은
`scenario YD-C3 passed`와 `yield-dispatch kotlin e2e result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-235604-4071087`이다. 이 실행도
Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로 진행했고, native `core/` build는 실행하지
않았다.

YieldDispatch `YD-E1`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
`YdE1TimeoutScenario` 의미에 맞춰 다시 정렬했다. client scenario는 unique timeout Spot을 만든 뒤
`YieldTimeoutReq`를 stream request로 보내고, public reply의 `timedOut` 결과와 request/spot id를
확인한다. 그 다음 Play evidence에서 `timeout-yield-completed`와 error marker를 기다리고, 같은 Spot에
post-timeout `ProbeMsg`를 보내 `timeout-yield-started`, `timeout-yield-released`,
`timeout-yield-completed`, `probe-started`, `probe-completed` 순서를 확인한다.

검증은 `nice -n 10`과 Gradle `--no-parallel --max-workers=1`로 진행했고, native `core/` build는
실행하지 않았다. `:Client:compileJava :Shared:compileJava :Server:Play:compileJava
:Server:Session:compileJava`가 통과했고,
`nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-E1` focused runner가 통과했다.
실행 출력은 `scenario YD-E1 passed`와 `yield-dispatch kotlin e2e result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-230235-3830539`이다.

YieldDispatch `YD-D2`와 `YD-D3`도 2026-07-07 현재 checkout에서 공통 Config 8 문서와 `.NET`
remote/route-bridge scenario 의미에 맞춰 다시 확인했다. `YD-D2` client scenario는 `play-a` owner
Spot이 public `requestToSpot(...).yield(...)`로 `play-b` target Spot을 기다리고, owner evidence와
target evidence가 분리되며 continuation reply가 `play-a`로 돌아오는지 확인한다. `YD-D3` client
scenario는 stream connector packet이 Session route mesh를 거쳐 `play-b` target Spot으로 전달되고,
target Spot의 yield 중 같은 target Spot의 probe가 먼저 실행되는지 확인한다.

검증은 `nice -n 10`과 Gradle `--no-parallel --max-workers=1`로 진행했고, native `core/` build는
실행하지 않았다. `:Client:compileJava :Shared:compileJava :Server:Play:compileJava
:Server:Session:compileJava`가 통과했고, `Server/Session`의 `EnsureSpotReq` route timeout은 `.NET`과
다른 Kotlin client 경로의 30초 대기와 맞췄다. 이 변경은 play-b route auto-connect가 고부하에서 5초를
넘길 수 있는 것을 반영한 harness timeout 정렬이며, scenario-only sleep이나 retry wrapper가 아니다.
`nice -n 10 timeout 360s env ZLINK_LIBRARY_PATH=... ./run_e2e.sh YD-D2`와 같은 `YD-D3` focused runner가
각각 통과했다. 실행 출력은 `scenario YD-D2 passed`, `scenario YD-D3 passed`,
`yield-dispatch kotlin e2e result=passed`였고, 증거 디렉터리는
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-231120-3878253`와
`framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260707-231217-3884460`이다.

YieldDispatch `YD-D1`은 focused mode가 A/B/C/D4/E1 local topology scenario를 실제로 실행하고 각
scenario의 기존 evidence assertion이 통과한 뒤에만 D1 marker를 출력하도록 조정했다. `:Client:compileJava`는
통과했고, `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260708-000525-4125144`에서
`YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-B2`, `YD-B3`, `YD-C1`, `YD-C2`, `YD-C3`,
`YD-D4`, `YD-E1`, `YD-D1` marker와 최종 pass marker를 확인했다.

YieldDispatch `YD-D4`도 2026-07-08 현재 checkout에서 공통 Config 8 문서와 `.NET`
`YdD4SessionRelayActorYieldScenario` 의미에 맞춰 닫았다. client scenario는 stream session에서
actor-bound relay로 `ActorPushYieldReq`를 보내고, Play actor handler는 public
`boundSession().send(...)`로 bound connector에만 `ActorPushNotify`를 보낸다. client는 stream
connector `waitFor(...)`로 bound push를 확인하고 unbound connector의
`receivedCount("ActorPushNotify") == 0`으로 오배달이 없음을 확인한다. `:Client:compileJava
:Server:Play:compileJava`가 통과했고, `nice -n 10 timeout 600s ./run_e2e.sh YD-D4` focused runner가
통과했다. 실행 출력은 `scenario YD-D4 passed`와 `yield-dispatch kotlin e2e result=passed`였고,
증거 디렉터리는 `framework/languages/java/e2e-kotlin/YieldDispatch/logs/20260708-000440-4120944`이다.
이어서 `nice -n 10 timeout 600s ./run_e2e.sh YD-D1`도 통과해 D4가 local topology aggregate와
충돌하지 않는 것을 확인했다. 이 실행도 Gradle `--no-parallel --max-workers=1`과 `nice -n 10`으로
진행했고, native `core/` build는 실행하지 않았다.

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 Gradle task 이름을 확인한 뒤 실행한다.

```bash
cd framework/languages/java
./gradlew build
./gradlew test
./gradlew sampleTest
for f in e2e-kotlin/*/run_e2e.sh; do timeout 420s "$f"; done
ZLINK_SAMPLE_FILTER= timeout 900s samples/run_samples.sh
./gradlew :zlink-framework-kotlin:test
./e2e-kotlin/SpotService/run_e2e.sh
./e2e-kotlin/YieldDispatch/run_e2e.sh
```

Gradle daemon, port 충돌, Redis 구동 문제 때문에 전체 루프가 실패하면 실패 config/sample을 먼저 단독
재현하고, 단독 pass 후 전체 루프를 다시 실행한다.

`samples/run_samples.sh`는 Java와 Kotlin sample gate를 함께 실행하는 통합 runner다. Kotlin sample만
좁혀서 재현해야 할 때는 `ZLINK_SAMPLE_FILTER=kotlin/<Sample>` 형식으로 단일 sample을 먼저 실행하고,
수정 후에는 filter 없이 통합 runner를 다시 실행한다.

ActorRef/SpotRef 전송 대상 통일을 마치기 전에는 아래 검색에서 정식 코드와 사용자-facing 문서에 남은
이전 이름이나 id-only helper가 없어야 한다. `doc/plan/`과 `draft` 문서는 과거 계획이나 구현 전 초안을
담을 수 있으므로 이 완료 검색에서 제외한다.

```bash
cd framework/languages/java
rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|ZLinkSpotAddress|resolveSpotAddress|resolveActorSpotAddress|sendToActorAwait\\([^)]*actorId|requestToActorAwait\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  zlink-framework-kotlin e2e-kotlin samples/kotlin \
  -S -g '!**/build/**'

rg -n "ZLinkActorRef|ZLinkActorRefSnapshot|SpotAddress|spot address|SpotRemoteAddress|spot remote address|sendToActorAwait\\([^)]*actorId|requestToActorAwait\\([^)]*actorId|sendToSpot\\([^)]*spotRid|requestToSpot\\([^)]*spotRid" \
  ../../doc/contract-inventory ../../doc/framework/common ../../doc/framework/kotlin \
  -S -g '!../../doc/plan/**' -g '!../../doc/**/draft/**'
```

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
- ActorRef/SpotRef 전송 대상 통일이 Kotlin framework, Kotlin E2E, Kotlin sample, 사용자-facing 문서에
  반영되어 있다.
- `partial` 또는 `gap`으로 남은 E2E/sample 항목이 없다. public contract 설계가 필요한 항목이 있으면
  이 계획은 완료가 아니라 blocked 상태로 남긴다.
- 모든 Kotlin E2E runner와 sample runner가 pass했다.
- 누락 리뷰가 `NO MISSING KOTLIN ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD KOTLIN REFACTOR ITEMS`를 반환했다.
