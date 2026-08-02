# Java/Kotlin Framework spec gap ledger

상태: 진행 중. 구현·test·E2E·package gate에 반영된 변경은 각 항목의 실행 증거로
기록하고, common contract와 실제 process evidence가 없는 항목은 완료로 표시하지
않는다.

마지막 갱신일: 2026-08-02

이 ledger는 Java framework와 Kotlin framework를 하나의 작업 단위로 관리한다. Java와
Kotlin의 exact interface, feature-map, runner, 현재 실패 원인은 각 항목에서
분리한다. 목표는 기능 이름이 존재하는지 확인하는 것이 아니라, common spec이 정의한
계약과 실제 production call path, role process E2E, common sample 실행 결과가 같은
동작을 증명하는지 확인하는 것이다.

## 공통 실행 규칙 — 네 ledger 동시 진행

이 문서의 Java/Kotlin 작업은 C++, .NET, Node.js 작업과 동시에 진행한다. 현재 시스템 시각
`2026-08-02 23:16 KST (+09:00)` 기준 마감은 `2026-08-03 10:00 KST (+09:00)`이다. 마감 시점에
완료하지 못한 항목은 완료로 표시하지 않고, 현재 조건과 blocker를 기록한 뒤 다음 결정을 기다린다.

이 절에서 고정하는 것은 작업 간 경계, 하위 layer bug 처리, CPU·마감·log 위치처럼 지켜야 하는
조건이다. 구체적인 test 순서와 범위, review model·reasoning level, 도움 요청 시점, commit 단위와
push 시점은 진행 중 evidence와 dependency를 보고 workstream owner가 정한다. 처음 정한 방식이
맞지 않으면 작업을 멈추기보다 이유와 새 선택을 `log/`에 남기고 조정한다. 한 항목의 결정이
끝나지 않아도 독립적으로 진행할 수 있는 조사·재현·test 준비는 계속한다.

### 작업 경계와 CPU 제한

- 네 작업은 서로 독립된 workstream으로 진행한다. 이 Java/Kotlin workstream은 이 ledger와 Java/Kotlin
  Framework의 source, test, E2E, package와 그에 대응하는 진행 기록만 수정한다.
- 다른 workstream의 source, test, E2E, 문서, package version, lockfile 또는 진행 기록을 수정하거나
  정리하지 않는다. 비교를 위한 read-only 확인은 가능하지만, 다른 작업의 내용을 이 candidate와
  commit에 섞지 않는다.
- Core 또는 bindings처럼 여러 workstream이 사용하는 공통 파일을 수정해야 하면 먼저 해당 하위
  layer의 owner와 변경 manifest를 정한다. owner가 하나의 candidate로 수정·검증하고, 다른
  workstream은 그 의존성만 자기 log에 기록한다. 같은 Core·bindings 변경을 각 작업이 중복 수정하지
  않는다.
- 시스템의 20 CPU를 네 작업이 나누어 사용하므로 작업 하나가 build, test, review agent와 보조
  process를 합쳐 점유하는 CPU는 최대 5개를 넘지 않는다. 병렬 실행 옵션도 작업당 `5` 이하로
  제한하고, 추가 worker를 생성해 이 제한을 우회하지 않는다.

### Core·bindings bug 처리와 local package 배포

Core 또는 bindings 수준의 bug를 발견하면 Framework나 sample에서 회피하지 않는다. 호출부의 raw
frame 해석, private/internal API 호출, reflection, test-only adapter, 상태 복제, 별도 retry 경로를
추가해 하위 layer의 실패를 숨기는 방식은 완료로 인정하지 않는다.

1. 최소 재현으로 원인을 소유한 layer를 확정한다. 원인과 재현 조건이 더 분명해지는 방식이라면
   test와 fix candidate를 함께 준비할 수 있지만, 최종 candidate에는 수정 전 동작을 잡는
   regression test가 남아 있어야 한다.
2. Core bug는 Core test에, bindings bug는 해당 bindings test에 regression test를 추가하고, 그
   test가 확인하는 책임을 해당 layer의 수정으로 해결한다.
3. test·fix·영향받는 gate의 실행 순서는 이슈의 재현 조건과 의존성에 맞춰 정한다. 순서를 바꾸면
   그 이유와 아직 닫히지 않은 조건을 `log/`에 남긴다.
4. 수정한 Core 또는 bindings를 local package로 배포할 때는 반드시 package version을 올린다.
   `scripts/local-package/README.ko.md`의 절차에 따라 새 runtime·archive를 만들고, 필요한 Core
   library 동기화와 stale cache 제거를 끝낸다.
5. Framework는 새 version의 local package를 실제로 resolve하는지 clean consumer, package contract와
   관련 process E2E로 확인한다. source tree나 이전 version cache를 사용한 결과는 새 package의
   증거로 인정하지 않는다.

원인 layer, regression test, fix, 올린 version, package 경로·hash, consumer 결과는 이 문서 본문에
진행 log로 나열하지 않고 아래 `log/` 규칙에 기록한다. 공통 Core·bindings candidate의 commit은
owner workstream에서만 만들며, 이 Java/Kotlin workstream의 commit에는 다른 언어 작업의 변경을
포함하지 않는다.

### Review agent 선택

Review agent의 model과 reasoning level은 언어별로 고정하지 않고, review를 요청하는 시점에
[OpenAI 공식 model guidance](https://developers.openai.com/api/docs/guides/latest-model)를 확인해
review 위험도에 따라 결정한다. 이 문서 갱신 시점의 guide는 `gpt-5.6-sol`을 frontier capability,
`gpt-5.6-terra`를 intelligence와 cost의 균형, `gpt-5.6-luna`를 효율적인 high-volume 작업의
예시로 설명한다. 이 model ID는 영구 pin이 아니며, guide가 바뀌면 새 선택을 따른다.

- heading·link·manifest 같은 기계 검사는 guide가 정한 balanced 또는 efficient model을 출발점으로
  삼는다. 실제 범위가 달라지면 더 적절한 model과 level을 선택할 수 있다.
- public contract, ABI, runtime semantics, lifecycle, concurrency, package와 process E2E를 판단하는
  review는 guide의 frontier model을 우선 검토한다. `high`, `xhigh`, `max` 중 어느 수준이 필요한지는
  candidate의 위험도와 실제 evidence에 따라 진행 중 정한다.
- 전체 closure와 어려운 cross-layer race의 최종 audit은 충분한 capability의 독립 reviewer가
  수행해야 한다. 특정 model ID나 level을 형식적으로 채우는 것보다 review 범위와 결과의 충분성을
  우선하며, 선택을 조정한 근거를 `log/`에 남긴다.
- 실제 model ID, guide 확인 URL와 날짜, 선택 근거, reasoning level과 결과는 이 문서의 `log/`에
  기록한다. 필요한 reviewer를 바로 사용할 수 없으면 해당 review만 pending으로 두고 독립적으로
  진행할 수 있는 작업을 계속한다. review가 필요한 완료 판정은 reviewer 결과 전까지 완료로
  표시하지 않는다.

### 미해결 이슈의 독립 도움 요청

작업 중 같은 이슈가 재현된 뒤에도 원인이나 수정 방향이 닫히지 않으면, 최신 guide가 정한 높은
수준의 Codex model 또는 `Claude Fable`에 독립 도움을 요청해 다음 선택을 정한다. 어느 시점에
어떤 경로로 요청할지는 재현 가능성, 영향 범위와 진행 dependency를 보고 owner가 정한다. 요청에는
판단에 필요한 candidate manifest, 재현 명령과 결과, 현재 가설, 책임 경계와 남은 제약을 포함한다.
도움은 설계·진단 입력이며 해결 판정이 아니다. 실제 owner layer의 regression test, fix, 새 package와
관련 gate가 닫힌 뒤에만 이슈를 해결로 표시한다. 도움 요청과 응답, 선택한 조치, 미해결 조건은
이 문서의 `log/`에만 기록한다. 지원 model을 사용하더라도 workstream 경계와 작업당 5 CPU 제한을
유지한다.

본문에 남은 model ID, level, round 순서와 표는 과거 evidence 또는 시작 profile이다. 별도 계약으로
고정하지 않은 세부 선택은 진행 중 candidate의 위험도와 evidence에 맞춰 조정할 수 있다. 이 공통
규칙과 review 요청 시점의 공식 guide를 기준으로 선택하고, 변경 이유만 `log/`에 남긴다.

### Commit·push와 진행 기록

- 하나의 bounded card 또는 하위 layer 수정의 commit 경계는 책임 범위, rollback 가능성과 review
  흐름을 보고 owner가 정한다. 필요한 검증을 마친 단위는 path-limited commit으로 남기고 적절한
  branch로 push한 뒤 다음 card로 진행한다. 중간 commit이 필요하면 candidate로 표시하고 최종
  완료와 구분한다.
- 최종·고위험 변경은 가능하면 push 전에 독립 review를 거치지만, review와 무관한 조사·재현·준비
  작업까지 기다리게 하지는 않는다.
- `git add -A`나 unrelated 변경을 포함한 commit은 금지한다. commit과 push 전후의 SHA, branch,
  package version과 gate 결과는 `log/`에 기록한다.
- 진행 log는 이 ledger 본문에 절대 남기지 않는다. 명령, exit code, review finding, commit·push,
  package 배포와 blocker는 이 문서가 있는 디렉토리의 `log/` 안에만 기록하고, 본문에는 현재 판정과
  필요한 log 링크만 둔다.

## 1. 목적과 완료 조건

현재 checkout에서는 Java와 Kotlin의 일부 production test, package consumer, focused
process가 통과한다. 그러나 그 결과만으로는 전체 계약을 만족한다고 판정할 수 없다.
현재 확인된 대표적인 visible failure와 미완료 조건은 다음과 같다.

- Java·Kotlin ST-A1과 일부 RuntimeMonitoring focused process는 통과했지만, ST-A2 이후
  relocation과 Config 10·14 전체 process evidence는 아직 없다.
- Stream HWM, handler-less wait, flow metadata는 focused test까지 통과했지만 process
  E2E와 API snapshot은 아직 없다.
- Java exact inventory는 `expected=374`, `sourceIds=220`, `missingIds=154`이고,
  Kotlin exact inventory는 `sourceIds=183`, `missingIds=191`이다.
- Aggregate runner는 canonical suite를 preflight하도록 바뀌었지만 Java는
  `ChannelEgressRouting`, Kotlin은 `StoreFailure`, `ChannelEgressRouting`,
  `SubmitAdmission`, `InstanceSpot`이 없어 명시적으로
  `aggregate_incomplete`로 중단한다.
- sample/E2E configuration policy는 application source의 환경 변수·JVM property
  조회와 reflection 경로를 보고한다.

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
7. 각 단계가 끝날 때 최신 OpenAI guide에 따라 선택한 review agent 또는 `Claude Fable`의
   POSD·DDD review가 승인되고, 승인된 범위만 다른 workstream과 분리한 별도 commit과 push로
   남긴 뒤 다음 단계로 진행한다. 선택한 agent를 사용할 수 없으면 낮은 수준으로 대체하지
   않고 해당 review만 `대기`로 기록한다. 독립적으로 진행할 수 있는 작업은 계속한다.

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
- framework/languages/java/zlink-http-client-kotlin/
- framework/languages/java/zlink-framework-codec-protobuf/
- framework/languages/java/zlink-framework-codec-msgpack/
- framework/languages/java/zlink-framework-locations-redis/
- framework/languages/java/zlink-framework-spring-boot-starter/
- framework/languages/java/zlink-framework-provider-abstractions/
- framework/languages/java/zlink-framework-binding-internal/
- framework/languages/java/zlink-framework-testkit/
- framework/languages/java/e2e/
- framework/languages/java/e2e-kotlin/
- framework/languages/java/samples/
- framework/languages/java/e2e/run_e2e_all.sh
- framework/languages/java/e2e-kotlin/run_e2e_all.sh
- framework/languages/java/samples/run_samples.sh
- framework/languages/java/scripts/verify_packaged_contract.sh
- framework/languages/java/validate_sample_e2e_configuration_policy.sh
- framework/languages/java/build.gradle.kts
- framework/languages/java/settings.gradle.kts:20-34

`zlink-framework-provider-abstractions`와 `zlink-framework-binding-internal`은 application
호출 surface가 아니라 production dependency와 module boundary를 검증하기 위한 범위다.
`zlink-framework-testkit`은 sample/E2E gate의 test support 범위다. 아래 Kotlin HTTP
source/test는 조사 대상 경로이며, 정식 source owner inventory에 등록되었다는 뜻이
아니다. 현재 owner JSON의 범위와 owner 부재는 JK-TEST-004에서 별도로 판정한다.

- framework/languages/java/zlink-http-client-kotlin/src/main/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutines.kt:50-69
- framework/languages/java/zlink-http-client-kotlin/src/test/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutineTest.kt:67

## 3. 현재 검증 결과

이 절은 `cwd=framework/languages/java`에서 2026-08-02 23:16 현재 working tree를 기준으로
실행한 결과다. `HEAD`는 `9efee01aa39ace3db8e0f50c46ba9c12864f2cc2`이고, 전체 dirty
status manifest는 확인 시점 기준 1000개 항목이다. 다른 workstream의 변경은 되돌리거나
정리하지 않았다.

| 검증 | 결과 | 현재 의미 |
|---|---|---|
| `:zlink-framework-core:test`, `:zlink-framework-testkit:test`, `:zlink-framework-kotlin:test`, `:zlink-stream-connector:test`, `:zlink-http-client:test`, `:zlink-http-client-kotlin:test` | PASS, 41 actionable tasks | 최신 Poller wrapper·HWM·STREAM 변경을 포함한 production/unit module이 통과한다. process E2E, API snapshot, CI path는 포함하지 않는다. |
| Java/Kotlin runtime `integrationTest` | PASS. Java `:zlink-framework-core:integrationTest` 8 actionable tasks, Kotlin `:zlink-framework-kotlin:integrationTest` 22 actionable tasks | 일반 Router request와 RouteMesh raw Mesh request를 포함한 현재 Java integration과 Kotlin integration이 통과했다. 이 결과는 common E2E aggregate·role evidence·API snapshot을 포함하지 않는다. |
| `:zlink-framework-core:compileJava`와 Fanout contract test | PASS | `FanoutChannelBuilder.setRoutingIdPrefix`의 public builder와 fake backend 경로를 확인한다. |
| `JvmPublicContractSourceOwnerTest` | PASS | `jvm-public-contract-source-owners.json`의 server/client artifact, source owner, public package, module-info와 settings module 연결을 검사한다. API snapshot 비교는 포함하지 않는다. |
| Java/Kotlin stream focused tests | PASS | HWM drop-newest, drop error, handler-less wait, timeout·cancellation 뒤 late message 보존, pending request correlation cleanup, typed wait/sequence/request payload cleanup, observer option copy, inbound flow metadata와 Kotlin wrapper를 확인한다. 최신 `ZLinkStreamRuntimeIngressTest`는 `recv` 전에 readiness wait가 실행되고, HWM pause/resume, heartbeat send failure, session construction failure, ignored notification, empty-part malformed peer isolation을 유지하는 것도 확인한다. process stream E2E는 별도다. |
| `bindings/java` `SocketPollingContractTest.pollerTracksStreamSocketReadableRawRecord`, `pollerTracksRouterSocketReadableRecord` | PASS, fresh Core 11.1.0 prefix | public `Poller`가 `StreamSocket`과 `RouterSocket`의 `POLLIN`을 관찰한 뒤 public `recv`가 raw record를 수신하는지 확인한다. |
| Java/Kotlin Framework socket receive poller regression | PASS, 최신 module test 41 actionable tasks·Java integration 8·Kotlin integration 22 | `ZLinkJavaSocketReceivePoller`를 통해 `StreamSocket`, `RouterSocket`, `DealerSocket`, `SubscriberSocket`과 raw service `RouterSocket`의 receive path를 readiness 확인 뒤에 실행한다. .NET의 `ZLinkBackendSocketPoller`·`ZLinkChannelReceiveLoop`와 같은 `poller wait → capacity 확인 → DONT_WAIT recv/subscribe` 순서를 사용하며, readiness는 `POLLIN|POLLERR|POLLPRI`를 받는다. Dealer와 raw service는 request completion이 필요할 때 `POLLIN|POLLCOMPLETION`을 하나의 public poller에 등록한다. Poller registration은 Framework가 bind/connect와 socket option 설정을 끝낸 뒤 첫 readiness 호출에서 수행하고, raw mesh service는 bind 직후 명시적으로 registration한다. Framework production path에는 STREAM packet callback 등록이 없다. poller close와 blocking receive-loop 종료 순서는 관련 module/integration test에서 확인하고, socket receive와 close의 동시 실행은 binding wrapper에서 직렬화한다. |
| fresh Java binding package와 clean consumer | PASS | 승인된 Core 11.1.0 prefix와 `core-package-20260801.json`으로 binding package `11.1.1`을 다시 만들고, isolated Gradle consumer가 public API를 compile했으며 package가 native Core 11.1.0을 포함하는지 확인했다. evidence는 `.artifacts/v11/evidence/V11-M4-BIND-JVM/java-binding-consumer-20260802-r3.json`이다. |
| `e2e/RegistrationCodec/run_e2e.sh all`, `e2e-kotlin/RegistrationCodec/run_e2e.sh all` | PASS, Java·Kotlin 모두 RC-A1~RC-A6·RC-B1~RC-B5 | Java·Kotlin Spring role process에서 registration, DI lifecycle, filter order, JSON/Protobuf/MessagePack coexistence와 unknown codec rejection/recovery를 모두 확인한다. Config 4 전체 및 다른 common Config의 aggregate 완료를 의미하지 않는다. |
| `e2e/PubSub/run_e2e.sh all`, `e2e-kotlin/PubSub/run_e2e.sh all` (2026-08-02 최신 실행) | PARTIAL. Kotlin은 PS-A1~A4·PS-B1~B2·PS-C1 PASS. Java는 PS-A1~A4·PS-B1·PS-C1 PASS 후 PS-B2 timeout으로 FAIL했고, `run_e2e.sh PS-B2` 단독 재실행도 같은 `publisher row` timeout으로 FAIL | Java PS-B2는 `PublisherRestartScenario`의 `waitPublisherRow`에서 publisher topology row를 확인하지 못했다. evidence는 `framework/languages/java/e2e/PubSub/logs/20260802-204336-3230`이다. Kotlin 전체 selector는 통과했다. Java·Kotlin fanout publish topic, typed subscriber dispatch, late subscriber, missing packet recovery와 일부 restart 경로를 확인하지만 Java publisher restart process evidence는 미완료다. Config 3 전체와 PS-D/PS-E 전환 대상은 포함하지 않는다. |
| `e2e/SpotActorTransfer/run_e2e.sh ST-A1 --start-order {forward,reverse}`, `e2e-kotlin/SpotActorTransfer/run_e2e.sh ST-A1 --start-order {forward,reverse}` | PASS, 네 실행 모두 `scenario ST-A1 passed` | Java·Kotlin의 기본·reverse start-order가 ST-A1 전용 placement fixture에서 모두 통과했다. fixture는 `actor-a`를 유일한 User Spot·Actor placement target으로 두어 common contract의 “같은 node의 Join” 전제를 고정하고, public actor request 뒤 target handler evidence를 확인한다. role `/health`는 Framework와 expected peer readiness를 함께 확인한다. raw Actor/User Spot create retry와 ST-A2 이후 전체 relocation은 별도 증거다. |
| `e2e/RuntimeMonitoring/run_e2e.sh MON-A2`, `MON-A4A`, `MON-A4B`, `MON-D1A`, `MON-D1B` | PASS | restart, crash replacement, unknown mesh, repeated recovery 경로를 확인한다. Config 7 전체는 아니다. |
| `./scripts/verify_packaged_contract.sh java` 및 `kotlin` | PASS | temporary repository의 declared artifacts와 clean consumer가 HTTP·Stream public type 및 Stream flow model까지 compile한다. HTTP version은 `HttpClientVersion.VERSION`에서 읽는다. |
| `ZLinkLocationLifecycleTest.actorJoinUpdatesDurableAuthorityWithStoreVersionCas`, CAS loser/stale generation regression 및 `ZLinkActorContextStateRelocationTest.failedMoveClearsMovingStateAndCompletesTheMoveStageExceptionally` | PASS | local actor join이 READY Actor authority row를 읽고 target Spot authority를 확인한 뒤 `StoreVersion` CAS로 Actor location/generation을 갱신하는 경로와 CAS conflict/stale generation rejection, failed move terminal state를 확인한다. restart/takeover와 process-level authority evidence는 포함하지 않는다. |
| HWM registration/dispatch/spot/actor focused tests와 위 6개 module 전체 test | PASS | startup max-message validation, completion-send permit, channel/mesh dispatcher, Spot route/subscription/local Actor handler accounting, resource cleanup과 runtime shutdown 시 completion admission close를 확인한다. `ZLinkMeshDispatchRecord`에 raw mailbox HWM lease를 연결하고 dispatcher가 같은 lease를 재사용하며, `ZLinkJavaRawMeshNode`가 budget saturation 중 application receive를 멈췄다가 record 종료 뒤 재개하는 회귀도 통과했다. 대기 중 subscription을 보유한 raw `Spot` 종료가 parts와 admission lease를 함께 해제하는 회귀와 `ZLinkInboundDispatchBudgetTest.allowsAnOversizedMessageThatStartedWithAnEmptyBudget`의 단일 message overshoot 경계도 통과했다. RawMesh precise overshoot, cancellation과 process HWM evidence는 포함하지 않는다. |
| `HttpClientCoroutineTest.cancelling coroutine wait does not cancel submitted stage` | PASS | Kotlin coroutine 대기 취소가 이미 제출한 `CompletionStage`를 취소하지 않는 adapter 경계를 확인한다. retry, body-read deadline, client lease와 HTTP role process evidence는 포함하지 않는다. |
| `env ZLINK_SAMPLE_FILTER=TicTacToe ./samples/run_samples.sh` | PASS | Java·Kotlin TicTacToe process가 모두 종료 조건까지 통과한다. 12개 sample 전체는 아니다. |
| fake-backend public-manager gate | PASS | runner selector를 현재 test inventory에 맞췄다. 이것은 12개 sample process 결과가 아니다. |
| `find e2e e2e-kotlin samples -type f -name '*.sh' ... bash -n` | PASS | shell syntax만 확인한다. |
| `git diff --check` (ledger와 Java/Kotlin 변경 경로) | PASS | 이번 진행 범위의 Markdown·Java source/test에 trailing whitespace 오류가 없다. |
| `git diff --check` (전체 working tree) | PASS | 최신 working tree의 tracked diff에서 trailing whitespace 오류가 발견되지 않았다. |
| `JavaDocumentationRegressionTest` Java exact inventory | FAIL, `expected=374`, `sourceIds=220`, `missingIds=154`, `missingSuites=Config 12/14` | 현재 Java source/runner가 common Config 1–14 전체를 소유하지 않는다는 정확한 blocker다. 전체 ID는 XML report에 기록된다. |
| `JavaDocumentationRegressionTest` Kotlin exact inventory | FAIL, `expected=374`, `sourceIds=183`, `missingIds=191`, `missingSuites=Config 6/12/13/14` | Kotlin source와 canonical runner가 common Config 1–14 전체를 소유하지 않는다는 정확한 blocker다. |
| Java/Kotlin aggregate preflight | FAIL, `aggregate_incomplete` | canonical suite directory와 `run_e2e.sh`가 없으면 subset을 all 성공으로 보고하지 않는다. Java는 `ChannelEgressRouting`, Kotlin은 `StoreFailure`에서 중단한다. |
| `./validate_sample_e2e_configuration_policy.sh` | FAIL | application source에 `System.getenv`/`System.getProperty`와 관련 reflection 경로가 남아 있다. |

exact inventory 결과는
`framework/languages/java/zlink-framework-core/build/test-results/contractTest/TEST-systems.zlink.framework.JavaDocumentationRegressionTest.xml`에 있다. parser는 이제 두 번째 hyphen을 포함한 `IS-E2E-*` ID를 추출하며, 실패는 parser 오류가 아니라 현재 source/runner 누락을 보고한다. `CH-E2E-*` 16개와 `IS-E2E-*` 36개 suite가 없고, 나머지 누락 ID도 같은 report의 `missingIds`에 포함된다.

`buildAllSamples` 또는 좁은 unit test의 성공은 common E2E·sample 전체 완료로 확대하지 않는다. Java/Kotlin Config 1–14 aggregate, 12개 sample process, API snapshot, dedicated CI workflow는 아직 실행·승인하지 않았다.

## 4. 현재 충족 판정

다음 범위는 현재 source 또는 실행 결과가 좁은 조건을 직접 증명하므로 충족으로
기록한다.

- framework/doc/contract-inventory/jvm-public-contract-source-owners.json이 server뿐
  아니라 Java HTTP, Kotlin HTTP, Stream Connector, codec extension의 artifact·public
  package·source owner·boundary gate를 기록하고, `JvmPublicContractSourceOwnerTest`가
  live source/module/settings 연결을 검사한다. 이것은 API snapshot이 통과했다는 뜻은
  아니다.
- Java와 Kotlin production module test가 통과했다.
- Java/Kotlin packaged consumer가 HTTP와 Stream public type을 clean classpath에서
  compile했다.
- Java/Kotlin sample manifest와 Gradle source set은 각각 여섯 common sample을 가리킨다.
- 여섯 Java sample과 여섯 Kotlin sample에 run_sample.sh가 존재한다.
- shell runner syntax 검사는 통과했다.
- Java stream options source와 Kotlin compression copy가 common exact interface의
  observer option field와 기본값을 보존하고, `ZLinkStreamMessage`가 inbound flow
  metadata를 typed decode까지 보존한다. HWM·handler-less queue는 focused unit test까지
  확인했고 process E2E와 API snapshot은 별도 gap이다.
- static import scan에서 application E2E client가
  systems.zlink.framework.runtime.internal 또는 ZLinkJavaRaw를 직접 import하는
  결과는 발견되지 않았다. 이것은 role evidence와 process 실행을 증명하지 않는다.
- Java·Kotlin TicTacToe 두 process와 fake-backend public-manager gate는 통과했다.
  여섯 sample 전체의 결과로 확대하지 않는다.
- Java ST-A1과 RuntimeMonitoring의 지정된 focused scenario가 통과했다. 각 Config
  전체의 결과로 확대하지 않는다.
- Java·Kotlin aggregate runner가 canonical suite directory와 executable runner를
  preflight하고, 누락 시 `aggregate_incomplete`를 출력하는 것을 확인했다. 이는
  누락된 suite를 구현한 것이 아니라 false all success를 차단하는 gate다.
- Java/Kotlin Framework의 native zlink Socket receive path가 public `Poller` readiness
  확인 뒤에만 `recv` 또는 `subscribe`를 호출하도록 정렬되었다. `StreamSocket`,
  `RouterSocket`, `DealerSocket`, `SubscriberSocket`과 raw service `RouterSocket`은
  socket별 poller를 사용하며, STREAM ingress는 packet callback을 등록하지 않는다.
  .NET 구현과 동일하게 `POLLIN|POLLERR|POLLPRI`를 receive wake-up으로 처리하고,
  completion이 필요한 Dealer/raw service는 하나의 `POLLIN|POLLCOMPLETION` poller를
  사용한다.
  Spot local mailbox, Socket monitor event, binding-internal Mesh claim은 native zlink
  Socket receive가 아니므로 별도 경로로 분류했다. 최종 Codex read-only review에서
  이 receive/poller 범위의 Critical·High·Medium finding은 남지 않았다. 첫 review에서
  지적된 Low 소유권 항목은 STREAM frame admission과 shutdown이 겹칠 때 frame을 닫지
  않을 수 있는 조기 반환이었다. `ZLinkStreamRuntime.tryAdmitFrame()`가
  `retainForRetry`를 사용해 HWM 재시도 두 경로에서만 frame을 보류하고, closed·draining·
  malformed decode·dispatch failure에서는 `finally`로 frame을 닫도록 수정했다.
  후속 Codex review는 이 수정 뒤 Critical·High·Medium·Low finding이 없다고 확인했다.
  기존 completion owner 동시 초기화 관찰도 binding의 단일 owner 직렬화와 회귀로 닫혔다. 이 판정은 현재 source,
  fresh binding regression, Java/Kotlin module/integration test와 부분적인 PubSub
  process E2E까지의 좁은 evidence에 한정하며, 전체 gap·aggregate E2E 완료로 올리지
  않는다.

다음 범위는 아직 충족으로 올릴 수 없다.

- feature-map에 scenario ID가 적힌 것과 exact selector가 실제 runner에 연결되는 것
- child runner 하나가 exit 0을 반환하는 것과 common inventory 전체가 실행되는 것
- compile/build 통과와 role server endpoint, client-visible result, server evidence
- public option이 존재하는 것과 실제 production call path에서 content type,
  cancellation, application-wide HWM, owner, generation, cleanup이 적용되는 것
- historical log, source type 존재, unit test, 부분, 미구현, diagnostic_only,
  source_only 항목

## 5. JK-IMP-* production implementation gap

각 항목은 common contract, Java/Kotlin source, 현재 동작, 목표 동작, 수정 순서와
완료 evidence를 함께 기록한다. public surface가 common spec 또는 exact interface와
다르면 spec을 목표 계약으로 둔다.

### JK-IMP-001 — Stream HWM의 drop 방향과 drop evidence

- 상태: 부분 구현. common stream spec의 drop 방향과 error code는 source와 focused
  Java/Kotlin regression에 반영했지만, process-level observer/evidence와 stream
  process E2E는 아직 완료하지 않았다.
- 계약 경로:
  framework/doc/framework/common/spec/stream-connector/32-stream-connector.ko.md:532-539,
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:127-160.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamDispatchQueue.java:42-50,93-113,
  framework/languages/java/zlink-stream-connector/src/test/java/systems/zlink/stream/connector/ConnectorDispatchTest.java,
  framework/languages/java/zlink-stream-connector/src/test/java/systems/zlink/stream/connector/ZLinkStreamConnectorTest.java.
- 확인한 동작과 기대 동작:
  현재 queue는 HWM에 도달하면 새로 도착한 Send를 닫고
  `RECEIVED_MESSAGE_DROPPED` error를 publish한다. 기존 queue item은 유지하며
  `receivedCount`를 감소시키지 않는다. 이는 common spec의 drop-newest 의미와
  일치한다.
- 판정 근거:
  `ConnectorDispatchTest`가 기존 queue order, drop count와 error code를 확인하고,
  `ZLinkStreamTestHelperTest.timedOutWaiterDoesNotConsumeALaterMessage`가 timeout 뒤
  도착한 message를 다음 waiter가 소비하는지 확인한다. Kotlin
  `KotlinConnectorWrapperTest.timedOutWaiterDoesNotConsumeALaterMessage`도 같은
  경계를 확인하며 full production module test가 통과했다. 다만 observer가 받는
  application evidence와 Java/Kotlin process E2E가 아직 없다.
- 수정 목록:
  현재 queue 구현은 유지한다. 남은 작업은 connector error observer와 process evidence가
  common `ReceivedMessageDropped` 규칙을 exactly-once로 확인하는 regression이다.
- 필요한 회귀 test: JK-REG-001. HWM 직전의 기존 message, HWM 초과의 새 Send,
  ReceivedMessageDropped, queue order와 receivedCount를 Java/Kotlin 양쪽에서
  확인한다.
- 선행 조건과 작업 순서: R0 contract review → R1 queue design review →
  runtime unit/integration → focused stream process E2E.
- 구현 완료 evidence: 새 message가 처리되지 않고 기존 queue sequence가 유지되며
  exactly-once drop evidence가 observer 또는 지정된 client-visible 경로에 나타난다.

### JK-IMP-002 — handler-less receive와 waitFor의 queue ownership

- 상태: 부분 구현. handler-less Send를 queue에 보존하고 direct waiter가 소비하는
  source path를 유지하며, Java/Kotlin focused test에서 timeout·cancellation 뒤
  late message가 기존 queue consumer에게 전달되는 것을 확인했다. typed wait·sequence·request
  결과의 원본 waiter 취소와 raw payload cleanup, pending request correlation 제거도
  반영했지만 cancellation 경쟁의 process evidence와 process E2E의 전체 조합은 아직 완료하지 않았다.
- 계약 경로:
  framework/doc/framework/common/spec/stream-connector/32-stream-connector.ko.md:532-560,
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:109-125.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamReceiveDispatcher.java:157-184,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamDispatchQueue.java,
  framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt:78-115.
- 확인한 동작과 기대 동작:
  dispatcher는 handler 등록 여부와 무관하게 수신 message를 queue에 넘긴다. 등록된
  handler가 없는 packet도 queue에 남으며 `waitFor` direct waiter가 dispatch mode와
  무관하게 소비한다. dispatch가 먼저 소유한 message는 handler dispatch가 소유하고,
  waiter가 매칭한 message는 queue에서 제거한다.
- 판정 근거:
  `ConnectorDispatchTest.handlerlessManualMessageRemainsAvailableToWaitFor`와
  `ZLinkStreamTestHelperTest.timedOutWaiterDoesNotConsumeALaterMessage`,
  `cancelledWaiterDoesNotConsumeALaterMessage`, Kotlin wrapper의 sequence/expectNone
  및 timeout/cancellation regression이 handler-less direct wait와 queue ownership을
  확인한다. queue close 시 보관 message와 waiter를 정리하도록 했지만 process
  evidence는 남아 있다.
- 수정 목록:
  typed wait·sequence·request 결과가 원본 waiter 또는 request stage까지 cancellation을
  전파하고, decode 완료·실패·취소 경쟁에서 raw payload를 한 번 정리하도록 ownership
  path를 보강했다. 남은 regression에서 handler dispatch와 wait consumer의 exactly-once
  및 process-level cleanup을 확인한다.
- 필요한 회귀 test: JK-REG-002. handler 없는 waitFor, 늦은 handler 등록,
  MANUAL과 automatic dispatch, timeout, expectNone, sequence order를 양 언어로
  실행한다.
- 선행 조건과 작업 순서: JK-IMP-001의 queue policy 결정 후 R1에서 수행한다.
- 구현 완료 evidence: handler가 없을 때도 Send가 queue에 남고 한 consumer만 item을
  소비하며, timeout과 close가 계약된 terminal error로 완료한다.

### JK-IMP-003 — Stream public surface와 Kotlin option copy drift

- 상태: 부분 구현. Java/Kotlin exact stream docs에 `receivedCount(String)`와 observer
  option을 반영했고, `ZLinkStreamMessage`의 `flowId`·`flowOrigin` public model과
  Kotlin compression copy도 source에 반영했다. checked-in API snapshot과
  package-level public surface 비교는 아직 남아 있다.
- 계약 경로:
  framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md:50-160,
  framework/doc/framework/common/spec/server/languages/java/interfaces/stream-session.ko.md,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/stream-session.ko.md.
- 구현 경로:
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamConnector.java:5-15,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamMessage.java,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamFlow.java,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkFlowOrigin.java,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamReceiveDispatcher.java:157-191,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/DefaultZLinkStreamConnector.java:105-107,
  framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt:50-76,78-98,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/ZLinkStreamConnectorOptions.java:6-29,73-118,
  framework/languages/java/zlink-framework-kotlin/src/contractTest/kotlin/systems/zlink/framework/kotlin/KotlinPublicSurfaceContractTest.kt,
  framework/languages/java/zlink-stream-connector/src/test/java/systems/zlink/stream/connector/ZLinkStreamConnectorTest.java.
- 확인한 동작과 기대 동작:
  Java `ZLinkStreamConnector`, Kotlin wrapper와
  `framework/doc/framework/common/spec/stream-connector/languages/java/03-stream-connector.ko.md`
  가 `receivedCount(String)`, observer limit과 flow metadata를 함께 선언한다. Inbound
  frame은 effective flow를 message에 담고, typed handler·wait decode 경로도 두 field를
  보존한다. Kotlin `copyStreamCompression`은 `maxInboundObserverNotifications`와
  `maxInboundObserverPayloadPreviewBytes`를 보존한다.
- 판정 근거:
  `:zlink-stream-connector:test`가 flow metadata round-trip 및 inbound callback
  propagation을 포함해 통과했고, focused Java stream test와
  `KotlinConnectorWrapperTest`가 option copy와 wrapper surface를 확인한다. 다만
  reflection/API snapshot으로 exported method 전체를 비교한 결과는 없어 public
  contract 최종 판정은 보류한다.
- 수정 목록:
  현재 source와 exact docs의 결정을 유지한다. API snapshot owner를 확정하고 Java와
  Kotlin exported method 목록, default 값, package consumer를 같은 gate에서 비교한다.
- 필요한 회귀 test: JK-REG-005. Java/Kotlin public method 목록 snapshot, options
  copy 후 observer limit 보존, default value와 builder signature를 확인한다.
- 선행 조건과 작업 순서: R0 exact interface 결정 → R1 runtime adapter 정리 →
  module boundary와 package consumer.
- 구현 완료 evidence: Java/Kotlin API snapshot이 exact interface와 같고
  compression copy 후 모든 option 값이 보존된다.

### JK-IMP-004 — inbound content type과 codec error mapping

- 상태: 부분 구현. Java direct channel과 service envelope 경로가 wire
  `contentType`을 보존하고, 수신 registry가 알 수 없는 non-JSON type을 JSON으로
  fallback하지 않도록 연결했다. Java unit/regression과 RegistrationCodec RC-B1~B5
  process E2E가 통과했고 Kotlin도 같은 RC-A1~A6·RC-B1~B5 process 회귀가 통과했다.
  다만 route/spot의 모든 wire path와 package/API snapshot은 아직 별도 확인이 필요하다.
- 계약 경로:
  framework/doc/framework/common/spec/06-framework-api.ko.md:411-438,
  framework/doc/framework/common/spec/05-async-execution-policy.ko.md와 exact
  handler interface의 error/terminal 규칙.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/configuration/ZLinkCodecRegistration.java:121-194,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelHandlerInvoker.java:83-124,161-181,231-262,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkMeshApplicationDispatcher.java:131-152,247-284,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/ZLinkMessageContext.java:13.
- 확인한 동작과 기대 동작:
  common contract는 JSON을 default codec으로 사용하되, inbound envelope이 등록되지
  않은 non-JSON content type을 선언하면 JSON으로 fallback하지 않고 ProtocolError로
  완료하도록 요구한다. Java `ReceiveRecord`, direct channel content-type frame,
  dispatcher와 invoker가 wire type을 수신 codec 선택까지 전달하며, 등록되지 않은
  type은 `REQUEST_PROTOCOL_ERROR`를 담은 framework error reply로 종료한다. 정상 JSON,
  Protobuf, MessagePack path는 기존 serializer를 계속 사용한다.
- 판정 근거:
  `ZLinkCodecRegistrationTest`는 JSON default, 등록된 extension 선택, unknown
  non-JSON의 `REQUEST_PROTOCOL_ERROR`를 확인한다. `ZLinkMeshApplicationDispatcherTest`는
  handler 전에 unknown type을 거부하고 error reply kind를 확인한다. RegistrationCodec
  `run_e2e.sh all`은 RC-B5에서 JSON-only peer의 `StringValue` handler가 실행되지 않고
  client-visible `REQUEST_PROTOCOL_ERROR`가 반환된 뒤 JSON request가 회복되는 것을
  확인하며, RC-B1~B4는 기존 JSON/Protobuf/MessagePack 회귀를 확인한다. Kotlin
  `e2e-kotlin/RegistrationCodec/run_e2e.sh all`도 RC-A1~A6·RC-B1~B5와 같은
  client-visible error/recovery 경계를 확인한다. flow diagnostic의
  `PAYLOAD_DECODE_FAILED`는 dispatch telemetry reason이고 wire reply의 framework
  error kind와 구분된다. route/spot의 모든 wire path와 package/API snapshot은 아직
  별도 확인이 필요하다.
- 수정 목록:
  Java direct/service path의 수정은 유지한다. 남은 작업은 Kotlin과 route/spot의
  content-type propagation을 같은 contract test와 process evidence로 확인하고, common
  envelope과 package consumer/API snapshot을 정렬하는 것이다. codec은 payload bytes와
  type mapping만 책임지고 caller에 parse/decode 처리를 추가하지 않는다.
- 필요한 회귀 test: JK-REG-003. JSON default, registered Protobuf/MessagePack,
  unknown non-JSON, malformed payload, handler non-execution, error callback count,
  request/send terminal mapping을 양 언어 process에서 확인한다.
- 선행 조건과 작업 순서: R0 error mapping review → runtime call path → codec
  unit/integration → RegistrationCodec E2E → sample.
- 구현 완료 evidence: Java direct/service path에서 unknown content type이 JSON decode
  없이 정확한 ProtocolError로 끝나고 callback/error evidence가 exactly-once이며 정상
  extension codec은 유지된다. 양 언어와 전체 route/spot path까지 같은 결과를 확인하면
  충족으로 승격한다.

### JK-IMP-005 — Actor local join의 authority와 callback 순서

- 상태: 부분 구현. local join의 source capture → target Actor authority CAS와 location
  renewal → joined marker/callback → source `OnLeaveActor` one-way notification → move
  completion 순서를 production path에 반영했고, durable `StoreVersion` CAS unit test,
  source notification 지연·실패 회귀와 Java/Kotlin ST-A1 focused process가 통과했다. CAS
  loser, stale generation, cleanup failure, retry/restart/takeover, routed transfer와
  role-level authority evidence는 아직 남아 있다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/actors.ko.md:63-84,105-126,
  framework/doc/framework/common/spec/15-spot-actor.ko.md:29-47,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/actors.ko.md:63-84,105-126.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/ZLinkActorSpotAdmission.java:282-300,392-472
  (completeLocalJoinFromCaller, commitRoutedActor, commitDeferredJoinRelocation),
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/ZLinkActorRuntime.java:1453-1480
  (local source notification ownership).
- 확인한 동작과 기대 동작:
  completeLocalJoinFromCaller는 source를 capture한 뒤 target Actor authority를
  durable `StoreVersion` CAS로 갱신하고 target location을 renew한다. 그 다음 joined
  marker와 callback을 실행한 뒤 source `OnLeaveActor` notification을 기다리지 않고
  move completion을 끝낸다. source notification의 완료 또는 실패가 이미 확정한 target
  authority의 Join completion을 바꾸지 않도록 one-way ownership을 분리했다. common actor contract는 target restore/reservation과
  owner/membership CAS를 먼저 확정하고 callback은 committed join 결과에 대해 한 번
  실행하도록 요구한다. routed transfer는 별도 path이므로 같은 invariant를 독립적으로
  확인해야 한다.
- 판정 근거:
  `ZLinkLocationLifecycleTest.actorJoinUpdatesDurableAuthorityWithStoreVersionCas`가
  Actor authority payload의 target spot id, spot object generation, mesh/node와
  store-version CAS를 확인한다. 추가 regression은 CAS loser와 stale local
  generation에서 authority row를 바꾸지 않는지, failed move가 moving state를
  해제하고 move stage를 exceptional terminal로 완료하는지 확인한다.
  `ZLinkActorRelocationStagingTest.localJoinSourceNotificationDoesNotBlockTargetCompletion`은
  지연되거나 exceptional로 끝난 source notification이 target completion을 막지 않는지
  확인한다. Java와 Kotlin
  ST-A1 process는 client-visible transfer terminal을 확인하지만 callback failure,
  cleanup failure, retry/restart/takeover의 owner/generation/queue evidence는
  확인하지 않는다.
- 수정 목록:
  현재 local CAS와 callback 순서를 유지한다. admission preflight, target reservation,
  owner/generation CAS, callback, one-way source notification, durable commit, replay 책임을
  분리하고, CAS loser·stale generation·cleanup failure·restart/takeover·routed path를
  같은 domain invariant로 검증한다. infrastructure callback과 application callback의
  exactly-once 경계를 process evidence로 추가한다.
- 필요한 회귀 test: JK-REG-004. callback success/failure, CAS loser, stale handle,
  cleanup failure, retry, process restart/takeover, backlog replay와 terminal reason을
  owner/generation evidence로 확인한다.
- 선행 조건과 작업 순서: R0 DDD authority model → R2 admission design review →
  runtime integration → SpotActorTransfer와 ToActorMessaging E2E.
- 구현 완료 evidence: target authority와 generation이 정해진 순서로 한 번만
  변경되고 callback count가 정확히 한 번이며 failure cleanup과 replay evidence가
  contract에 맞는다.

### JK-IMP-006 — global ActorId dispatch와 builder signature

- 상태: 부분 해결. Java builder와 stream registration은 exact contract의
  `enableActorDispatch()` no-argument surface로 정렬했고, stream runtime이 Actor
  authority mesh를 내부에서 resolve한다. exact contract test와 최신 Java/Kotlin
  runtime matrix가 통과했지만 서로 다른 Mesh의 global ActorId process evidence와
  package/API snapshot은 아직 남아 있다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:521,
  framework/doc/framework/common/spec/server/languages/java/interfaces/stream-session.ko.md:10,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/stream-session.ko.md:6-12.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/configuration/ZLinkStreamNodeBuilder.java:25,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/streams/StreamBuilders.java:107-108,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/streams/StreamNodeRegistration.java:120,
  framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/ZLinkConnectorExtensions.kt:50-115.
  현재 Kotlin file에는 별도 `enableActorDispatch` DSL member가 없고 Java builder/runtime
  surface를 재사용하는지 확인해야 한다.
- 확인한 동작과 기대 동작:
  exact interface는 enableActorDispatch() no-argument로 선언하고 ActorId가 현재
  authority/Mesh를 선택하도록 한다. Java builder와 registration은 MeshName을
  호출자에게 받지 않으며 stream runtime의 authority resolver가 내부 Mesh를 선택한다.
- 판정 근거:
  `JavaTargetContractGapTest.streamActorDispatchUsesTheGlobalAuthorityContract`가
  no-argument method와 추가 인자 overload 부재를 확인하고, 전체 runtime matrix가
  source/registration 변경 뒤 통과했다. global ActorId의 서로 다른 Mesh process
  dispatch와 stale owner 결과는 아직 확인하지 않았다.
- 수정 목록:
  현재 no-argument surface와 내부 authority resolution을 유지한다. 남은 작업은
  서로 다른 Mesh의 global ActorId dispatch, stale/relocated owner error, clean
  package/API snapshot을 같은 contract gate에서 확인하는 것이다.
- 필요한 회귀 test: JK-REG-005와 JK-REG-010. no-argument compile, 서로 다른
  Mesh의 동일 ActorId dispatch, current owner와 stale owner error를 확인한다.
- 선행 조건과 작업 순서: contract 선행 → R2 builder/runtime → Kotlin compile →
  actor dispatch E2E.
- 구현 완료 evidence: caller가 MeshName을 전달하지 않아도 현재 authority로
  dispatch되고 stale/relocated owner는 contract error로 끝난다.

### JK-IMP-007 — Fanout setRoutingIdPrefix public member 누락

- 상태: 구현 완료(현재 source·focused test와 선택 process 기준). exact Java interface의
  method를 Fanout builder와 channel registration에 연결했고 fake backend public-manager
  gate와 Java·Kotlin PubSub PS-A1~A4·PS-B1~B2·PS-C1 process가 통과했다.
  SubmitAdmission process E2E와 API snapshot은 별도 미검증이다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:202-214.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/configuration/FanoutChannelBuilder.java:5-37,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/mesh/MeshNodeRegistration.java:329-333.
- 확인한 동작과 기대 동작:
  `FanoutChannelBuilder.setRoutingIdPrefix(String)`가 channel registration에 prefix를
  보존하고 generated publisher RID를 prefix+UUID로 만든다. explicit routing ID가
  함께 설정되면 prefix가 우선하도록 registration validation을 적용한다.
- 판정 근거:
  `JavaTargetContractGapTest`, `DefaultZLinkFrameworkOptionsTest`와 fake backend
  public-manager gate가 method 존재와 registration path를 확인한다. Java·Kotlin
  `PubSub` process는 public topic overload와 publisher restart readiness를 확인하지만,
  generated routing ID prefix 자체와 clean consumer/API snapshot은 package/E2E gate에서
  계속 확인한다.
- 수정 목록:
  구현은 완료로 두고, package consumer와 PubSub/SubmitAdmission focused E2E에서
  generated routing ID와 duplicate/reconnect semantics를 추가 확인한다. node-level
  setter를 호출부 workaround로 사용하지 않는다.
- 필요한 회귀 test: JK-REG-006. builder signature/default, generated routing ID,
  duplicate policy, reconnect와 fanout publish 경계를 확인한다.
- 선행 조건과 작업 순서: R0 API snapshot review → runtime registration →
  contract test → PubSub/SubmitAdmission E2E.
- 구현 완료 evidence: exact method가 clean consumer에서 호출되고 routing prefix가
  server evidence와 API snapshot에 동일하게 나타난다.

### JK-IMP-008 — RouteMesh runtime option의 extra public surface

- 상태: 부분 정렬. Java source와 Java channel-messaging exact docs에는
  `meshNode(String)` 및 `channel(String meshName, String channelName)`의 근거를
  반영했다. Kotlin exact interface, exported API snapshot과 clean consumer의 양 언어
  비교는 아직 남아 있다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/channel-messaging.ko.md,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/channel-messaging.ko.md,
  framework/doc/framework/common/spec/00-public-contract-governance.ko.md:75-101.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/ZLinkRouteMeshRuntimeOptions.java:3-10,
  framework/languages/java/zlink-framework-core/src/main/java/module-info.java,
  framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/channels/ZLinkRouteMeshRuntimeOptionsRuntimeTest.java,
  framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/JavaTargetContractGapTest.java,
  framework/languages/java/zlink-framework-kotlin/src/contractTest/kotlin/systems/zlink/framework/kotlin/KotlinPublicSurfaceContractTest.kt.
- 확인한 동작과 기대 동작:
  `ZLinkRouteMeshRuntimeOptions`는 기본 `mesh(String)`·`channel(String)`과 함께
  mesh를 명시하는 두 overload를 제공한다. Java exact doc와 runtime monitoring/source
  사용이 이 표면과 정렬되었지만, Kotlin projection에 같은 선택이 기록되었는지는
  아직 확인하지 않았다.
- 판정 근거:
  source에 존재한다는 사실은 계약 근거가 아니다. extra surface를 언어별 임의
  public API로 승인하지 않는다.
- 수정 목록:
  Kotlin exact interface와 common governance에 두 overload의 언어별 표현을 명시할지
  결정하고, Java/Kotlin API snapshot과 package consumer를 같은 결과로 맞춘다. 근거가
  확정되기 전 새 overload 사용을 추가하지 않는다.
- 필요한 회귀 test: JK-REG-012. Java/Kotlin exported package와 public API snapshot을
  exact inventory와 비교하고 forbidden internal package가 consumer에 노출되지
  않는지 확인한다.
- 선행 조건과 작업 순서: R0 contract inventory review → API decision → runtime
  cleanup → package consumer.
- 구현 완료 evidence: extra method가 contracted 또는 non-public으로 명시되고
  clean consumer와 snapshot이 같은 결과를 낸다.

### JK-IMP-009 — application-wide inbound HWM의 production 적용 증거

- 상태: 부분 구현. host-wide byte budget, queued/active status, completion-send permit,
  startup listener limit validation을 구현했고 channel receive loop, Mesh application
  dispatcher, Spot route/subscription과 local Actor handler path에 accounting을
  연결했다. 실제 production backend인 `ZLinkJavaRawMeshNode`의 service mailbox에는
  application dispatch lease를 연결하고 dispatcher가 같은 lease를 재사용하도록 했다.
  raw pump도 application receive 전에 budget gate를 확인하므로 raw Spot/Actor/Instance
  Spot dispatch가 같은 admission 경계를 사용한다. 다만 precise overshoot 방지,
  shutdown/cancellation 및 process E2E evidence는 아직 닫히지 않았다. runtime
  shutdown 순서에는 이미 completion admission close를 연결했다. Auto mode는 이제
  명시한 `ProcessMemoryLimitBytes`를 우선하고, 생략하면 cgroup/OS limit과
  `Runtime.maxMemory()` 중 유한한 작은 값을 선택한 뒤 physical memory를 fallback으로
  사용한다. nested cgroup membership path도 확인하며, profile별 floor 계산과
  unlimited·negative·zero-result 경계는 focused test로 고정했다.
- 계약 경로:
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md:217-221,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/configuration-host.ko.md:45-58,
  framework/doc/framework/common/spec/05-async-execution-policy.ko.md.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/configuration/ZLinkInboundDispatchOptions.java:10-21,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/configuration/ZLinkInboundDispatchRegistration.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/monitoring/ZLinkFrameworkRuntimeStatus.java:10-27,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkMeshApplicationDispatcher.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/configuration/ZLinkFrameworkRegistration.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/internal/dispatch/ZLinkInboundDispatchBudget.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaRawMeshNode.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/internal/backend/ZLinkMeshDispatchRecord.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/mesh/ZLinkMeshNodesRuntime.java,
  framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/runtime/messaging/ZLinkAdmissionRuntimeTest.java,
  framework/languages/java/zlink-framework-core/src/test/java/systems/zlink/framework/RuntimeMonitoringContractTest.java.
- 확인한 동작과 기대 동작:
  public option의 host-wide inbound Application byte budget과 profile 설정을
  `ZLinkInboundDispatchBudget`으로 해석한다. budget은 queued/active payload bytes와
  receive pause 상태를 runtime status에 노출하고, application request completion에는
  별도 bounded permit을 적용한다. listener max-message-size 기본값과 HWM 사용 시
  유한한 listener limit을 startup에서 검증한다. channel receive loop와 Mesh
  dispatcher, Spot route/subscription/local Actor handler는 lease를 terminal
  completion까지 유지한다. 현재 실제 backend의 raw service mailbox는 별도 64 MiB
  mailbox와 자체 claim loop를 사용하지만, raw pump가 application receive 전에
  budget gate를 확인하고 mailbox dispatch record가 host-wide lease를 보유한다.
  raw Instance Spot request는 target peer admission race를 caller timeout 안에서
  재확인한다. 공통 budget의 빈 상태에서 시작한 단일 message overshoot는
  `ZLinkInboundDispatchBudgetTest.allowsAnOversizedMessageThatStartedWithAnEmptyBudget`
  로 확인했지만, raw Spot/Actor 경로의 precise overshoot는
  별도 증거가 없다. common contract는 application 전체
  byte budget, threshold,
  completion과 cancellation 경계를 요구한다.
- 판정 근거:
  `ZLinkInboundDispatchBudgetTest`, registration validation, channel/mesh dispatcher와
  Spot/Actor focused test 및 전체 관련 module test가 통과했다. raw mailbox dispatch
  record가 보유한 lease가 dispatcher에서 중복 생성되지 않는 회귀와
  `ZLinkJavaRawMeshNodeM6ATest.applicationHwmPausesRawReceiveUntilDispatchRecordCloses`
  가 통과했다. 후자는 application receive가 budget saturation에서 멈추고 record
  terminal close 뒤 재개되는 경계를 확인한다. 또한
  `ZLinkJavaRawMeshNodeM6ATest.closingRawSpotReleasesQueuedSubscriptionAdmissionLease`
  가 대기 중 subscription 종료 뒤 pending bytes가 0으로 돌아오는 것을 확인한다.
  completion admission의 대기 permit cancellation이 다음 waiter를 막지 않고 pending
  count를 되돌리는 focused regression도 통과했다. `ZLinkInboundDispatchBudgetTest`는
  Compact 2%, LowLatency 5%, Balanced 10%, Throughput 20%, managed heap·OS limit의
  작은 값 선택, physical fallback과 explicit limit 우선을 확인한다. 그러나 full budget에서
  precise overshoot가 없는지와 shutdown/cancellation release가 process에서 확인되지는
  않았다.
  setter와 status record의 존재만으로 전체 HWM 계약을 충족했다고 판정하지 않는다.
- 수정 목록:
  HWM authority를 process-wide admission layer에 유지하고 raw service mailbox와
  application receiver 사이의 한 번의 lease 소유권을 유지한다. 현재 연결한 mailbox
  lease와 raw pump gate를 기준으로 Spot/Actor direct path, full-budget overshoot,
  mailbox queued bytes와 active handler bytes의 중복 accounting을 추가 점검한다.
  threshold 진입·해제, deadline/cancellation, callback completion과 concurrent
  shutdown을 process evidence로 검증한다.
- 필요한 회귀 test: JK-REG-007. byte budget saturation, new admission rejection,
  accepted completion, cancellation release, shutdown in-flight operation, status
  transition과 owner evidence를 확인한다.
- 선행 조건과 작업 순서: R0 HWM invariant review → R2 runtime call-path audit →
  integration/fake backend → monitoring/SubmitAdmission E2E.
- 구현 완료 evidence: process-wide byte accounting이 production path에서 한 번만
  적용되고 permit release와 terminal reason이 status snapshot 및 role evidence와
  일치한다.

### JK-IMP-010 — Kotlin HTTP server builder의 `yield` exact surface와 cancellation

- 상태: runtime/public source 부분 해결. Kotlin exact interface와 common HTTP language
  interface가 요구하는 `yield<T>()`로 source를 정렬했고, removed `yieldAwait`의
  부재를 exact facade test로 고정했다. clean package/API snapshot과 HTTP process
  evidence는 아직 남아 있다.
- 계약 경로:
  framework/doc/framework/common/spec/http-client/languages/kotlin/kotlin-http-client.ko.md:59-61,74-75,
  framework/doc/framework/common/spec/http-client/language-interfaces.ko.md:58-68.
- 구현 경로:
  framework/languages/java/zlink-http-client-kotlin/src/main/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutines.kt:50-69,
  framework/languages/java/zlink-http-client-kotlin/src/test/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutineTest.kt:67.
- 확인한 동작과 기대 동작:
  현재 Kotlin coroutine extension은 `ZLinkHttpServerRequestBuilder`에 reified
  `yield<T>()`를 제공하고 Java builder의 `yield(Class<T>)` 결과를
  cancellation-non-propagating coroutine bridge로 기다린다. `yieldAwait`는 source와
  compiled facade에 없다. `HttpClientCoroutineTest`의 execution-turn, typed response,
  one-way await와 exact facade test가 통과했으며, coroutine cancellation의
  process-level terminal evidence는 JK-IMP-011에서 별도로 판정한다.
- 판정 근거:
  `HttpClientCoroutineTest.server coroutine facade exposes yield and not the removed
  yieldAwait`가 public `yield`와 `yieldAwait` 부재를 확인하고, 최신 HTTP Kotlin
  module test와 전체 runtime matrix가 통과했다. API snapshot과 clean consumer의
  exact method set 비교는 아직 없다.
- 수정 목록:
  현재 `yield<T>()` 표면, typed response, one-way await와 execution-turn ownership을
  유지한다. API snapshot/package consumer와 HTTP process E2E에서 동일한 method set,
  return type, terminal error를 확인한다. cancellation 범위와 terminal reason은
  JK-IMP-011의 non-propagating stage ownership으로 분리해 유지한다.
- 필요한 회귀 test: JK-REG-013. Kotlin clean consumer의 exact method 목록,
  `yield`와 `yieldAwait` 부재, typed response와 one-way return type,
  coroutine cancellation,
  timeout, HTTP status/body와 terminal error를 확인한다.
- 선행 조건과 작업 순서: R0 Kotlin HTTP contract decision → API snapshot/package
  consumer → coroutine runtime test → HTTP process E2E → sample.
- 구현 완료 evidence: exact Kotlin public surface와 common table이 일치하고 clean
  consumer/API snapshot이 통과한다. `yield` execution turn, cancellation propagation,
  timeout, error/status/body가 client-visible result와 role server evidence에 같은
  terminal reason으로 기록된다.

### JK-IMP-011 — Kotlin HTTP cancellation과 CompletionStage runtime path

- 상태: 부분 구현. Kotlin coroutine bridge가 cancellation callback을 등록하지 않아
  caller 대기만 끝내고 이미 제출한 `CompletionStage`를 취소하지 않도록 바꿨다. Java
  HTTP builder의 timeout도 공통 계약의 유한한 `1..INT_MAX` millisecond 범위로 검증하고
  sub-millisecond 값을 1ms로 정규화했으며 focused regression이 통과했다. Java runtime의
  retry/body-read deadline, client lease와 server execution turn이 coroutine cancellation과
  경쟁할 때의 process evidence는 아직 없다.
- 계약 경로:
  framework/doc/framework/common/spec/05-async-execution-policy.ko.md:223-251,
  framework/doc/framework/common/spec/http-client/languages/kotlin/kotlin-http-client.ko.md:71-75,
  framework/doc/framework/common/spec/http-client/languages/java/java-http-client.ko.md:80-90.
- 구현 경로:
  framework/languages/java/zlink-http-client-kotlin/src/main/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutines.kt:27-69,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/ZLinkHttpServerRequestBuilder.java:31-61,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/ZLinkHttpRequestBuilder.java:137-184,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/ZLinkFrameworkHttpExecutionTurn.java:7-16,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/internal/HttpClientRuntime.java:44-51,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/internal/HttpClientText.java:27-58,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/ZLinkHttpClientBuilder.java:34-38,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/ZLinkHttpRequestBuilder.java:74-78,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/internal/RetryPolicy.java:39-55,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/internal/RequestPerformer.java:61-86,141-156.
- 확인한 동작과 기대 동작:
  Kotlin `await()`는 `submit`, `yield`, `fetch`가 반환한 `CompletionStage`를 coroutine
  continuation에 연결한다. 이제 내부 bridge는 cancellation handler를 등록하지 않아
  caller coroutine의 취소가 underlying stage를 취소하지 않는다. Java server builder는
  `ZLinkFrameworkHttpExecutionTurn`을 거쳐 `ZLinkHttpRequestBuilder`의 stage를 만들고,
  그 stage는 `HttpClientRuntime`의 retry policy와 `RequestPerformer.sendAsync`/
  body-read deadline을 통과한다. common contract의 기대 동작은 caller 대기를 끝내되
  공유 stage와 이미 시작한 operation을 취소하지 않고, pending admission·deadline·shutdown
  결과를 하나의 terminal state로 정리하며, cancellation이 retry를 새로 시작하지
  않게 하는 것이다.
- 판정 근거:
  `HttpClientCoroutineTest.cancelling coroutine wait does not cancel submitted stage`가
  cancelled waiter 뒤에도 stage가 취소되지 않고 정상 완료할 수 있음을 확인한다. exact
  Kotlin HTTP public surface는 `yield`와 `yieldAwait` 부재로 source/test에 정렬되었고,
  `HttpClientTextTest`와 `HttpClientContractTest.validationRejectsInvalidConfiguration`가
  timeout 상한과 sub-millisecond 정규화를 확인한다. 다만
  cancellation 뒤 underlying request 지속, retry attempt 수, body-read deadline과
  client lease cleanup의 process evidence는 아직 없다.
- 수정 목록:
  비전파 cancellation 결정을 JK-IMP-010의 public surface 결정과 분리해 유지한다.
  caller coroutine completion, shared `CompletionStage`, retry attempt, `sendAsync`,
  body-read deadline, client lease와 execution turn의 terminal 상태를 한 adapter/state
  machine에서 기록한다. process test에서 cancellation이 새 retry를 시작하지 않고
  이미 시작한 request가 정상/timeout/shutdown 결과로 한 번 완료되는지 확인한다.
- 필요한 회귀 test: JK-REG-014. Kotlin coroutine cancellation 전후의 caller result,
  underlying request, retry attempt 수, timeout/deadline, body-read cleanup, client lease,
  server execution turn과 terminal reason을 role evidence에서 확인한다.
- 선행 조건과 작업 순서: R0 cancellation contract decision → HTTP runtime stage
  audit → coroutine/Java integration test → focused HTTP process E2E → sample.
- 구현 완료 evidence: cancellation의 caller-visible result와 underlying operation의
  상태가 common contract에 맞고, retry/cleanup/turn release가 exactly-once다. timeout,
  cancellation, shutdown 경쟁에서 client result, HTTP status/body/error와 role server
  evidence가 같은 terminal reason을 가리킨다.

### JK-IMP-012 — Framework Socket receive의 public Poller readiness와 STREAM recv ingress

- 상태: 구현·focused 검증·Codex read-only review 완료. Java와 Kotlin Framework가
  공유하는 Java runtime에서 native zlink Socket을 수신하는 경로를 socket별 public
  `Poller` readiness 확인으로 통일했다. 최종 review에서는 이 범위의
  Critical·High·Medium finding이 남지 않았다. 첫 review에서 발견된 STREAM frame
  ownership Low 항목은 `retainForRetry` 보강으로 수정했으며, 수정 후 follow-up review도
  Critical·High·Medium·Low finding 없음으로 판정했다.
  Codex read-only review는 `gpt-5.6-luna`, `reasoning_effort=low`로 수행했다. 최초
  범위 evidence는 `.artifacts/v11/evidence/codex-java-poller-review-20260802-r4.md`,
  ownership 수정 후 follow-up evidence는
  `.artifacts/v11/evidence/codex-java-poller-review-20260802-r5.md`이다. follow-up
  판정은 `CLEAN`이다.
  전체 ledger와 runtime stage의 완료 판정은 남은 API gap과
  aggregate E2E 조건과 분리한다.
- 계약 경로:
  framework/doc/framework/common/spec/03-interaction-model.ko.md,
  framework/doc/framework/common/spec/19-stream-session.ko.md,
  framework/doc/framework/common/guide/server/04-backpressure.ko.md,
  framework/doc/framework/common/spec/server/languages/java/interfaces/configuration-host.ko.md,
  framework/doc/framework/common/spec/server/languages/kotlin/interfaces/configuration-host.ko.md.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaSocketReceivePoller.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaStreamSocket.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaRouterSocket.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaDealerSocket.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaSubscriberSocket.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/ZLinkJavaRawServicePort.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/streams/ZLinkStreamRuntime.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelReceiveLoops.java,
  framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/channels/ZLinkChannelSocketRegistry.java.
- 확인한 동작과 기대 동작:
  Framework binding wrapper는 public `Zlink.createPoller()`, `Poller.add(socket, slot,
  POLLIN)`과 `Poller.wait(...)`를 사용한다. `StreamSocket`, `RouterSocket`,
  `DealerSocket`, `SubscriberSocket` 수신은 readiness 확인 뒤 `DONT_WAIT` receive를
  실행하고, raw service port도 RouterSocket별 poller를 사용한다. STREAM ingress는
  `onPacket`을 등록하지 않고 raw part를 `recv`로 받아 frame을 조립한 뒤 application
  admission을 수행한다. HWM이 가득 차면 다음 receive를 발행하지 않고 capacity 회복
  뒤 loop를 재개한다. source routing id, multipart 경계, segmented frame과 한 raw part의
  multiple frame은 기존 receive buffer 경로에서 보존한다.
- 경로 분류:
  `ZLinkJavaSocketMonitor.recv()`는 Socket payload receive가 아닌 monitor event
  수신이다. `ZLinkJavaRawSpot`·`ZLinkJavaMeshSpot`의 `subscribe`/`recvRoute`는
  Framework local mailbox이고, `ZLinkJavaMeshDispatchSource`의 `drainReady`와
  `Claim.recvBatch`는 binding-internal Mesh claim API다. 이 세 경로를 zlink Socket
  `recv`로 잘못 계산하지 않으며, public Socket receive 호출은 모두 socket poller
  readiness 경계를 통과한다.
- 판정 근거:
  `SocketPollingContractTest.pollerTracksStreamSocketReadableRawRecord`,
  `SocketPollingContractTest.pollerTracksRouterSocketReadableRecord`,
  `ZLinkStreamRuntimeIngressTest`, `ZLinkInboundDispatchBudgetTest`,
  `ZLinkStreamReceiveBufferTest`와 관련 fake backend regression이 통과했다. 최종
  수정에는 socket receive와 `close()`의 동시 실행을 직렬화하는 ownership 경계와,
  routing id는 있으나 message part가 없는 malformed record의 peer isolation 회귀가
  포함된다. 최신 전체 module test 41 actionable tasks, Java
  `:zlink-framework-core:integrationTest` 8 actionable tasks, Kotlin
  `:zlink-framework-kotlin:integrationTest` 22 actionable tasks, fresh Core 11.1.0
  prefix 기반 Java binding 전체 test와 Java/Kotlin packaged consumer가 통과했다.
  Java PubSub PS-B2 aggregate와 단독 selector는 publisher topology row timeout으로
  여전히 실패했고, Kotlin PubSub의 확인된 selector는 통과했다. 이 process 실패는
  현재 Socket Poller·STREAM recv runtime 경로와 다른 PubSub restart evidence blocker로
  분리했다. Codex follow-up review에서 Poller readiness, recv-only STREAM ingress,
  HWM admission, ownership/disposal 범위의 Critical·High·Medium·Low finding은 남지 않았다.
- 남은 조건:
  Java `ZLinkStreamNodeBuilder`의 MaxMessageSize public configuration은 기존 exact
  interface gap으로 남아 있으며 이 항목에서 새 public API를 추가하지 않았다. common
  374 scenario aggregate, API snapshot, CI gate와 PubSub PS-B2 process
  evidence도 이 좁은 Socket poller 판정과 별도로 남아 있다. Codex review evidence와
  STREAM ownership follow-up 결과는 위 경로에 보존했다.
- 회귀 test: JK-REG-016. 모든 Framework native Socket receive call path가 동일 Socket의
  public Poller readiness를 먼저 확인하고, STREAM callback mode와 recv mode를 섞지 않으며,
  poller close·HWM pause·capacity resume·malformed peer isolation을 유지하는지 확인했다.
- 완료한 작업 순서: public binding Poller/recv 확인 → socket별 readiness wrapper 연결 →
  bind/connect 이후 readiness 시점 registration → blocking loop와 shutdown race 회귀 →
  binding contract test와 fresh package consumer → module/integration test → Java/Kotlin
  process E2E 확인 → Codex read-only review.
- 완료 evidence: production의 native Socket receive 경로가 public Poller readiness 경계를
  사용하고, STREAM ingress가 recv-only로 동작하며, queue admission·ownership 경계와
  malformed peer isolation 회귀가 고정되었다. 이 항목의 좁은 runtime 판정은 완료했지만,
  Java PubSub PS-B2 process evidence와 전체 API/E2E/CI gate는 별도 blocker로 남아 있다.

## 6. JK-E2E-IMP-* Java/Kotlin E2E implementation gap

공통 E2E의 구현됨 표기는 process runner가 exact ID를 dispatch하고, 실제 role endpoint를
호출하고, client result와 server evidence를 assertion한 경우에만 완료로 승격한다.

### JK-E2E-IMP-001 — Java feature-map의 exact ID, alias, 누락

- 상태: gap.
- 기준 경로: framework/doc/framework/common/e2e/config-1-location-messaging.ko.md부터
  config-14-instance-spot.ko.md.
- 구현 경로:
  framework/languages/java/e2e/RegistryMessaging/feature-map.ko.md:107-109,
  framework/languages/java/e2e/RegistryMessaging/run_e2e.sh,
  framework/languages/java/e2e/AutomaticTurnDispatch/feature-map.ko.md:9-40,
  framework/languages/java/e2e/AutomaticTurnDispatch/run_e2e.sh:217-256,517-545,
  framework/languages/java/e2e/InstanceSpot/feature-map.ko.md,
  framework/languages/java/e2e/run_e2e_all.sh:9-22,
  framework/languages/java/e2e/StoreFailure/feature-map.ko.md와 framework/languages/java/e2e/StoreFailure/run_e2e.sh,
  framework/languages/java/e2e/RegistrationCodec/feature-map.ko.md와 framework/languages/java/e2e/RegistrationCodec/run_e2e.sh,
  framework/languages/java/e2e/RegistryMessaging/feature-map.ko.md와 framework/languages/java/e2e/RegistryMessaging/run_e2e.sh,
  framework/languages/java/e2e/PubSub/feature-map.ko.md와 framework/languages/java/e2e/PubSub/run_e2e.sh,
  framework/languages/java/e2e/SpotService/feature-map.ko.md와 framework/languages/java/e2e/SpotService/run_e2e.sh,
  framework/languages/java/e2e/RuntimeMonitoring/feature-map.ko.md와 framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh,
  framework/languages/java/e2e/ResilienceLifecycle/feature-map.ko.md와 framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh,
  framework/languages/java/e2e/AutomaticTurnDispatch/feature-map.ko.md와 framework/languages/java/e2e/AutomaticTurnDispatch/run_e2e.sh,
  framework/languages/java/e2e/ToActorMessaging/feature-map.ko.md와 framework/languages/java/e2e/ToActorMessaging/run_e2e.sh,
  framework/languages/java/e2e/SpotActorTransfer/feature-map.ko.md와 framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh,
  framework/languages/java/e2e/ObservabilityOps/feature-map.ko.md와 framework/languages/java/e2e/ObservabilityOps/run_e2e.sh,
  framework/languages/java/e2e/SubmitAdmission/feature-map.ko.md와 framework/languages/java/e2e/SubmitAdmission/run_e2e.sh 및 각
  경로의 Client scenario, Server endpoint/evidence를 대조한다.
- 현재 비교:
  - current Java inventory는 common 374개 중 source/runner ID 220개를 찾고 154개를
    missing으로 보고한다. Config 12 `ChannelEgressRouting` 16개와 Config 14
    `InstanceSpot` 36개는 suite directory와 aggregate runner 자체가 없다.
  - 현재 working tree에서 추가된 MON-A4A/A4B와 MON-D1A/D1B, ST-A1은 focused
    process가 통과했으므로 이전의 alias-only 기록에서 분리한다. MON-A6/MON-C1,
    ST-E1A/ST-E1B/ST-E1C 등 나머지 missing ID는 그대로 blocker다.
  - Config 2, 3, 5, 6, 8, 11, 13의 remaining ID는 SM·PS·RL·SF·TD·OBS·SA
    그룹으로 report에 모두 출력된다. 특히 `TD-*`와 `ATD-*`는 승인된 alias manifest
    없이는 같은 ID로 취급하지 않는다.
  - `framework/languages/java/zlink-framework-core/build/test-results/contractTest/TEST-systems.zlink.framework.JavaDocumentationRegressionTest.xml`의
    `missingIds`가 전체 목록의 단일 evidence다. feature-map row 존재만으로 selector,
    child process, role evidence 완료를 인정하지 않는다.
- 확인한 동작과 기대 동작:
  현재 Java feature-map의 row가 common ID와 이름이 다르거나 상태만 기록해도 실제
  selector와 runner dispatch가 연결되었다고 증명하지 못한다. 기대 동작은 common
  inventory의 각 ID가 정확히 하나의 Java feature-map row, selector, child process,
  role server endpoint와 client/server evidence assertion으로 연결되는 것이다. alias,
  source-only, partial row는 exact ID 완료를 대신하지 않는다.
- 판정 근거:
  feature-map row가 존재해도 exact selector와 client dispatch가 없으면 process
  coverage가 아니다. `TD-*`와 `ATD-*`는 이름이 다르므로 자동 alias로 취급하지
  않는다. Config 1의 `RM-A7`, Config 12와 14는 selector 또는 fixture presence가
  부족하다.
- 수정 목록:
  common inventory를 입력으로 feature-map set, selector set, client dispatch set,
  role endpoint/evidence set을 별도로 만든다. `TD-*`를 canonical selector로 사용할지
  `ATD-*`를 명시적 alias로 승인할지 contract review에서 정하고, 승인되지 않은
  marker는 제거한다. 각 row에 implemented, partial, unimplemented, diagnostic_only,
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
- 구현 경로:
  framework/languages/java/e2e-kotlin/RegistryMessaging/feature-map.ko.md,
  framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh,
  framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/feature-map.ko.md:9-35,
  framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/run_e2e.sh:381-429,
  framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/feature-map.ko.md,
  framework/languages/java/e2e-kotlin/run_e2e_all.sh:7-19,
  framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/feature-map.ko.md와 framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/run_e2e.sh,
  framework/languages/java/e2e-kotlin/RegistrationCodec/feature-map.ko.md와 framework/languages/java/e2e-kotlin/RegistrationCodec/run_e2e.sh,
  framework/languages/java/e2e-kotlin/RegistryMessaging/feature-map.ko.md와 framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh,
  framework/languages/java/e2e-kotlin/PubSub/feature-map.ko.md와 framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh,
  framework/languages/java/e2e-kotlin/SpotService/feature-map.ko.md와 framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh,
  framework/languages/java/e2e-kotlin/RuntimeMonitoring/feature-map.ko.md와 framework/languages/java/e2e-kotlin/RuntimeMonitoring/run_e2e.sh,
  framework/languages/java/e2e-kotlin/ResilienceLifecycle/feature-map.ko.md와 framework/languages/java/e2e-kotlin/ResilienceLifecycle/run_e2e.sh,
  framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/feature-map.ko.md와 framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/run_e2e.sh,
  framework/languages/java/e2e-kotlin/ObservabilityOps/feature-map.ko.md와 framework/languages/java/e2e-kotlin/ObservabilityOps/run_e2e.sh,
  framework/languages/java/e2e-kotlin/ToActorMessaging/feature-map.ko.md와 framework/languages/java/e2e-kotlin/ToActorMessaging/run_e2e.sh,
  framework/languages/java/e2e-kotlin/SpotActorTransfer/feature-map.ko.md와 framework/languages/java/e2e-kotlin/SpotActorTransfer/run_e2e.sh 및
  각 경로의 exact Client scenario, Server endpoint/evidence를 대조한다.
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
  - Config 8 TD는 TD-D4, TD-D5, TD-D6, TD-E2A, TD-F5A가 누락되었다. 그 외
    feature-map에 있는 `TD-*`의 실제 runner selector도 `ATD-*` 이름을 사용하므로
    Java와 같은 exact selector check가 필요하다. Kotlin runner에서 추가로 exact
    selector가 없는 common ID는 `TD-A5`, `TD-B4`, `TD-C4`, `TD-C5`, `TD-D5`,
    `TD-D6`, `TD-E2A`, `TD-F1`, `TD-F2`, `TD-F3`, `TD-F4`, `TD-F5`, `TD-F5A`,
    `TD-F6`, `TD-G1`이다.
  - Config 9 TA는 common ID가 있으나 TA-A2-, TA-B1-처럼 trailing hyphen token이
    extra이다.
  - Config 10 ST는 ST-E1B, ST-E1C, ST-G2, ST-G3, ST-G4, ST-G5, ST-G6, ST-H2,
    ST-H3, ST-H4, ST-H4A, ST-H4B, ST-H5가 누락되었다.
  - Config 11 OBS는 OBS-A5, OBS-C6, OBS-C7, OBS-C8, OBS-C9A, OBS-C9B, OBS-C10,
    OBS-C11, OBS-C12가 누락되었다.
  - Config 12의 16개, Config 13의 20개, Config 14의 36개가 모두 누락되고 해당
    Kotlin runner도 없다.
- Config 6의 common `SF-*` row는 현재
  framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/feature-map.ko.md에
  기록되어 있지만 aggregate selector 이름은
  framework/languages/java/e2e-kotlin/run_e2e_all.sh:8의 `DiscoveryRegistryHa`다.
  이는 common `StoreFailure`와의 suite ownership/alias를 명시하지 않은 selector
  불일치다.
- 확인한 동작과 기대 동작:
  `DiscoveryRegistryHa`가 Config 6의 별칭으로 의도되었는지 현재 문서와 runner만으로
  확정할 수 없다. 기대 동작은 common Config 6의 `SF-*` 각 ID에 대해 승인된 suite
  이름과 selector mapping을 manifest에 명시하고, 실제 client dispatch, role server
  endpoint, client-visible result와 server evidence를 모두 확인하는 것이다. 승인되지
  않은 추가 suite는 common coverage를 대신하지 않는다.
- 판정 근거:
  Kotlin aggregate에 DiscoveryRegistryHa 같은 추가 suite가 있어도 common ID
  coverage를 대신하지 않는다. `StoreFailure`와 `DiscoveryRegistryHa`의 alias 여부가
  선언되지 않은 상태에서 alias와 trailing hyphen은 selector contract를 증명하지
  않는다.
- 수정 목록:
  Java와 같은 inventory generator를 사용하되 Kotlin source path, suspend/Flow
  callback, role evidence를 별도 schema로 기록한다. 먼저 `StoreFailure`와
  `DiscoveryRegistryHa` 중 하나를 canonical suite로 정하거나 명시적 alias manifest를
  추가하고, common ID마다 selector dispatch와 terminal assertion을 연결한다.
- 필요한 회귀 test: JK-REG-008에 Kotlin exact set을 추가하고 JK-REG-010에서
  suspend/Flow client result와 server evidence 순서를 확인한다.
- 선행 조건과 작업 순서: Kotlin public/runtime compile drift 해결 → E0 inventory
  → fixture/selector → focused process → aggregate.
- 구현 완료 evidence: Kotlin Config 1–14 exact ID diff가 empty이고 extra suite는
  common inventory와 별도 extension으로 표시되며 all pass에 포함되지 않는다.

### JK-E2E-IMP-003 — aggregate runner 누락과 false all success

- 상태: 부분 해결. Java와 Kotlin aggregate가 Config 1–14 canonical suite 목록을
  선언하고, 선택된 suite directory 또는 `run_e2e.sh`가 없으면
  `aggregate_incomplete`로 non-zero 종료한다. exact 374 ID dispatch, feature-map
  status, role evidence와 terminal assertion은 아직 미완료다.
- 공통 기준 경로:
  framework/doc/framework/common/e2e/config-12-channel-egress-routing.ko.md,
  config-13-submit-admission.ko.md, config-14-instance-spot.ko.md.
- Java 경로: framework/languages/java/e2e/run_e2e_all.sh의 default list가 14개
  canonical suite를 선언하지만 현재 `ChannelEgressRouting` directory와 runner가
  없어 preflight에서 중단한다. `InstanceSpot`도 feature-map만 있어 별도 blocker다.
- Kotlin 경로: framework/languages/java/e2e-kotlin/run_e2e_all.sh의 default list가
  14개 canonical suite를 선언하지만 현재 Config 6 `StoreFailure`, Config 12–14의
  directory와 runner가 없어 preflight에서 중단한다. 기존 `DiscoveryRegistryHa`는
  common `StoreFailure`의 alias로 승인되지 않았으므로 canonical coverage를 대신하지
  않는다.
- 확인한 동작과 기대 동작:
  preflight는 선택된 suite directory와 executable `run_e2e.sh`를 먼저 확인하고,
  없으면 machine-readable한 `aggregate_incomplete reason=missing_suite`를 출력한다.
  이로써 실행하지 않은 suite가 있어도 aggregate가 passed를 출력하는 경로는 차단했다.
  아직 common inventory, feature-map status, selector, evidence schema를 exact
  dispatch에 연결하지 않았으므로 374개 scenario 전체를 실행한다고 판정할 수 없다.
- 판정 근거:
  preflight 실행 결과 Java는 `ChannelEgressRouting`, Kotlin은 `StoreFailure`에서
  중단했다. canonical 목록은 선언되었지만 실제 suite와 scenario implementation은
  여전히 subset이다. Config 13 Java map의 부분·미구현 row도 runner가 별도로 차단하지
  않는다.
- 수정 목록:
  현재 preflight를 유지하고, 다음 단계에서 common inventory와 language map을 먼저
  exact diff해 missing, extra, alias, status를 실패로 출력하게 한다. all은 exact ID
  selector를 모두 dispatch하고 client result, role evidence, terminal reason,
  callback count, owner/generation/cleanup을 포함해야 한다.
- 필요한 회귀 test: JK-REG-009. missing suite, alias, partial, diagnostic_only,
  source_only, child exit 0/no evidence, retry 후 중복 callback을 aggregate failure로
  검증한다.
- 선행 조건과 작업 순서: JK-E2E-IMP-001/002 inventory → E1 runner contract →
  focused ID → Java/Kotlin aggregate.
- 구현 완료 evidence: all이 374개 common ID를 exact dispatch하고 한 ID라도 누락,
  status 미충족, evidence 부족이면 non-zero와 machine-readable reason을 반환한다.

### JK-E2E-IMP-004 — focused process가 role server 전에 compile에서 중단

- 상태: 부분 해결. Java·Kotlin ST-A1 focused process의 compile, role startup와
  client terminal assertion이 기본·reverse start-order에서 모두 통과했다. 최신
  실행에서는 raw Actor/User Spot create의 일시적인 `NOT_ADMITTED` 제출 실패를
  operation intent deadline 안에서 재시도하고, role `/health`가 expected peer
  readiness를 확인한다. Kotlin runner는 추가 start-order 인자를 하위 runner로
  전달한다. ST-A2 이후 relocation semantics와 전체 Config 10/14 process evidence는
  아직 미완료다.
- 기준 경로:
  framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md,
  config-9-to-actor-messaging.ko.md, config-14-instance-spot.ko.md.
- Java 경로:
  framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh,
  framework/languages/java/e2e/SpotActorTransfer/Client/src/main/java/systems/zlink/e2e/spotactortransfer/client/Program.java,
  framework/languages/java/e2e/SpotActorTransfer/Server/ActorNode/src/main/java/systems/zlink/e2e/spotactortransfer/actor/ActorNodeHttpServer.java,
  framework/languages/java/zlink-framework-core/src/main/java/module-info.java:4.
- Kotlin 경로:
  framework/languages/java/e2e-kotlin/SpotActorTransfer/run_e2e.sh,
  framework/languages/java/e2e-kotlin/SpotActorTransfer/Server/ActorNode/src/main/kotlin/systems/zlink/e2e/kotlin/spotactortransfer/actor/ActorNodeHttpServer.kt:94-96,
  framework/languages/java/e2e-kotlin/SpotActorTransfer/Server/ActorNode/src/main/kotlin/systems/zlink/e2e/kotlin/spotactortransfer/actor/TransferComponents.kt:136-141,181-190.
- 확인한 동작과 기대 동작:
  현재 Java·Kotlin ST-A1은 `scenario ST-A1 passed`와
  `spot-actor-transfer e2e result=passed`를 반환한다. Kotlin actor call builder,
  CompletionStage terminal, context override와 Java module graph를 현재 focused path에
  맞췄다. 기대 동작은 나머지 transfer scenario에서도 client, source role, target role,
  store/evidence endpoint가 public API만으로 같은 terminal/evidence 계약을 유지하는
  것이다.
- 판정 근거:
  최신 Java·Kotlin ST-A1 네 실행은 각각 `scenario ST-A1 passed`와
  `spot-actor-transfer e2e result=passed`를 남겼고 client-visible result와 role
  process marker를 확인한다. 이 결과는 ST-A1의 start-order와 readiness/admission
  경계를 증명하지만 ST-A2 이후 owner/generation/replay 및 전체 relocation을
  증명하지 않는다.
- 수정 목록:
  ST-A1 retry/readiness 변경은 유지한다. Java module graph와 package/local dependency를
  clean build에서 확인하고, ST-A2 이후 scenario를 exact selector와 role evidence까지
  확장한다. application이 internal module을 import하는 우회는 금지한다.
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
  framework/languages/java/e2e/SpotActorTransfer/Shared/src/main/java/systems/zlink/e2e/spotactortransfer/shared/Env.java,
  framework/languages/java/e2e/RuntimeMonitoring/Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonBPublishMonitoringAbsenceScenario.java,
  framework/languages/java/e2e/AutomaticTurnDispatch/Client/src/main/java/systems/zlink/e2e/automaticturn/client/Support/AutomaticTurnDispatchScenarioSupport.java,
  framework/languages/java/e2e-kotlin/SpotService/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/Env.kt,
  framework/languages/java/e2e-kotlin/RegistrationCodec/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/registrationcodec/Env.kt,
  framework/languages/java/e2e-kotlin/RuntimeMonitoring/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/runtimemonitoring/Env.kt,
  framework/languages/java/e2e-kotlin/ResilienceLifecycle/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/resiliencelifecycle/Env.kt.
- 확인한 동작과 기대 동작:
  policy script는 현재 System.getenv, System.getProperty,
  ZLINK_JAVA_*, ZLINK_KOTLIN_*를 application source에서 읽는 항목을 보고해
  exit 1을 반환한다. 대표 경로는
  framework/languages/java/e2e/SpotActorTransfer/Shared/src/main/java/systems/zlink/e2e/spotactortransfer/shared/Env.java:7-10,
  framework/languages/java/e2e-kotlin/SpotService/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/spotservice/Env.kt:10,
  framework/languages/java/e2e-kotlin/RegistrationCodec/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/registrationcodec/Env.kt:5,
  framework/languages/java/e2e-kotlin/RuntimeMonitoring/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/runtimemonitoring/Env.kt:10,
  framework/languages/java/e2e-kotlin/ResilienceLifecycle/Shared/src/main/kotlin/systems/zlink/e2e/kotlin/resiliencelifecycle/Env.kt:6,10이다. 또한
  Java RuntimeMonitoring client의
  framework/languages/java/e2e/RuntimeMonitoring/Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonBPublishMonitoringAbsenceScenario.java:3-4,75-97와
  AutomaticTurnDispatch support의
  framework/languages/java/e2e/AutomaticTurnDispatch/Client/src/main/java/systems/zlink/e2e/automaticturn/client/Support/AutomaticTurnDispatchScenarioSupport.java:966-969에서 application-side reflection을 사용한다. 공개 member만
  조회하더라도 common E2E의 no-reflection 기준에는 맞지 않으므로 private access
  여부와 관계없이 별도 수정 대상으로 둔다.
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
  framework/languages/java/e2e/SubmitAdmission/feature-map.ko.md,
  framework/languages/java/e2e/SubmitAdmission/run_e2e.sh,
  framework/languages/java/e2e/SpotActorTransfer/feature-map.ko.md,
  framework/languages/java/e2e/SpotActorTransfer/Server/ActorNode/src/main/java/systems/zlink/e2e/spotactortransfer/actor/EvidenceStore.java,
  framework/languages/java/e2e/SpotActorTransfer/Client/src/main/java/systems/zlink/e2e/spotactortransfer/client/Program.java,
  framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh,
  framework/languages/java/e2e/InstanceSpot/feature-map.ko.md,
  framework/languages/java/e2e/run_e2e_all.sh,
  framework/languages/java/e2e-kotlin/SpotActorTransfer/feature-map.ko.md,
  framework/languages/java/e2e-kotlin/SpotActorTransfer/Client/src/main/kotlin/systems/zlink/e2e/kotlin/spotactortransfer/client/Program.kt,
  framework/languages/java/e2e-kotlin/run_e2e_all.sh.
- 확인한 동작과 기대 동작:
  feature-map에는 부분, 미구현, source-only 설명이 섞여 있고 Java Config 14는
  map만 존재한다. SubmitAdmission Java map의 SA-E2E-01, 04, 05, 08, 09, 14, 20은
  부분 구현으로 기록되어 있다. aggregate는 이 상태를 machine-readable result로
  읽지 않는다. 기대 동작은 client-visible terminal reason과 role server의 callback
  count, owner, generation, cleanup, queue/replay evidence를 하나의 scenario result로
  비교하는 것이다.
- 판정 근거:
  historical log, source type 존재, unit test만으로 process E2E 완료를 표시할 수
  없다. ST-A1 focused 실행은 현재 evidence를 만들지만, Config 14 전체와 나머지
  scenario의 required role evidence schema를 대체하지 않는다.
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

- 상태: 부분 해결. stale selector를 현재 fake-backend public-manager test로 교체했고
  focused gate가 통과한다. 12개 sample process와 release gate aggregate는 아직
  완료하지 않았다.
- 기준 경로: framework/doc/framework/common/sample/README.ko.md와 각 sample README의
  self-check, smoke, completion 절.
- 구현 경로:
  framework/languages/java/samples/run_samples.sh:43-50,
  framework/languages/java/zlink-framework-testkit/src/contractTest/java/systems/zlink/framework/testkit/SampleReleaseGateContractTest.java,
  framework/languages/java/zlink-framework-testkit/src/fakeBackendTest/java/systems/zlink/framework/testkit/,
  framework/languages/java/samples/sample-manifest.env.
- 확인한 동작과 기대 동작:
  runner는 현재 존재하는
  `CurrentManagerFakeBackendTest.actorAndSpotManagersExposeCurrentFluentCallsAgainstFakeBackend`
  를 지정하고 fake-backend public-manager gate를 통과한다. 기대 동작은 이 gate 뒤
  manifest의 12 process sample을 실제로 실행하는 것이다.
- 판정 근거:
  현재 selector와 test source가 일치한다. buildAllSamples와 focused fake gate는
  sample process self-check와 server evidence를 대체하지 않는다.
- 수정 목록:
  현재 selector를 유지하고, manifest 6+6 process와 self-check/evidence가 실제로
  실행되는지 aggregate gate에 연결한다. method가 없어지면 inventory failure를 낸다.
- 필요한 회귀 test: JK-REG-011. sample gate selector가 실제 test를 찾고 manifest
  6+6개가 모두 실행되며 compile-only/skip이면 실패하는지 확인한다.
- 선행 조건과 작업 순서: E2E/runtime gate → sample gate repair → direct process →
  aggregate.
- 구현 완료 evidence: fake backend와 release gate가 통과하고 12 process가 각각
  client self-check와 server evidence를 남긴다.

### JK-SAMPLE-IMP-002 — Java TicTacToe의 asynchronous HTTP terminal drift

- 상태: 부분 해결. Java TicTacToe의 `fetch(...).toCompletableFuture().join()`을
  `submit(...).toCompletableFuture().join().body()`로 바꾸고 Java·Kotlin focused process가
  통과한다. 여섯 sample의 전체 terminal audit은 남아 있다.
- 기준 경로:
  framework/doc/framework/common/sample/tictactoe/README.ko.md,
  framework/doc/framework/common/spec/http-client/languages/java/java-http-client.ko.md,
  framework/doc/framework/common/spec/05-async-execution-policy.ko.md.
- 구현 경로:
  framework/languages/java/samples/java/TicTacToe/Client/src/main/java/systems/zlink/samples/tictactoe/client/TicTacToeClientScenario.java:34,
  framework/languages/java/samples/kotlin/TicTacToe/Client/src/main/kotlin/systems/zlink/samples/kotlin/tictactoe/client/TicTacToeClientScenario.kt,
  framework/languages/java/validate_sample_e2e_configuration_policy.sh.
- 확인한 동작과 기대 동작:
  Java client는 typed response를 비동기 `submit` stage로 종료하고 body를 읽는다.
  Java·Kotlin TicTacToe process가 통과했다. 기대 동작은 여섯 sample 양 언어 client가
  exact HTTP public call builder와 asynchronous terminal을 사용하고 timeout,
  cancellation, error body/status를 계약대로 확인하는 것이다.
- 판정 근거:
  TicTacToe의 현재 public usage는 exact HTTP contract와 맞는다. 다른 Java sample의
  `fetch` 사용은 public convenience일 수 있으므로 같은 변환을 기계적으로 적용하지
  않고 각 sample의 terminal contract를 별도로 확인한다.
- 수정 목록:
  TicTacToe 변경은 유지한다. 나머지 sample은 synchronous terminal 위반인지 public
  convenience 사용인지 source와 common guide를 대조한 뒤 필요한 경우에만 고친다.
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
  framework/languages/java/samples/java/TicTacToe/Client/src/main/java/systems/zlink/samples/tictactoe/client/Program.java,
  framework/languages/java/samples/kotlin/TicTacToe/Client/src/main/kotlin/systems/zlink/samples/kotlin/tictactoe/client/Program.kt,
  framework/languages/java/samples/java/TicTacToe/run_sample.sh,
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh,
  framework/languages/java/samples/run_samples.sh,
  framework/languages/java/validate_sample_e2e_configuration_policy.sh.
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
  framework/doc/framework/common/sample/deliverydispatch/README.ko.md,
  framework/doc/framework/common/sample/event/gamequest.ko.md,
  framework/doc/framework/common/sample/event/shoppingmall.ko.md,
  framework/doc/framework/common/sample/supportchat/README.ko.md,
  framework/doc/framework/common/sample/tictactoe/README.ko.md.
- 구현 경로:
  framework/languages/java/samples/java/Bingo/run_sample.sh,
  framework/languages/java/samples/java/DeliveryDispatch/run_sample.sh,
  framework/languages/java/samples/java/GameQuest/run_sample.sh,
  framework/languages/java/samples/java/ShoppingMall/run_sample.sh,
  framework/languages/java/samples/java/SupportChat/run_sample.sh,
  framework/languages/java/samples/java/TicTacToe/run_sample.sh,
  framework/languages/java/samples/kotlin/Bingo/run_sample.sh,
  framework/languages/java/samples/kotlin/DeliveryDispatch/run_sample.sh,
  framework/languages/java/samples/kotlin/GameQuest/run_sample.sh,
  framework/languages/java/samples/kotlin/ShoppingMall/run_sample.sh,
  framework/languages/java/samples/kotlin/SupportChat/run_sample.sh,
  framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh,
  framework/languages/java/samples/sample-manifest.env.
- 확인한 동작과 기대 동작:
  Java·Kotlin TicTacToe process와 fake-backend public-manager gate는 통과했다.
  나머지 10개 process와 aggregate는 아직 실행하지 않았다. common sample 문서는
  client self-check, role server result, smoke output, completion evidence를 요구한다.
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

- 상태: 부분 해결. Java contract test가 common heading의 모든 exact ID를 수집하고
  Java와 Kotlin 각각의 source ID, missing ID, missing suite, aggregate runner 누락을
  함께 보고한다. 현재 결과는 Java `expected=374`, `sourceIds=220`, `missingIds=154`,
  Kotlin `expected=374`, `sourceIds=183`, `missingIds=191`로 정확히 실패한다. role
  evidence schema와 feature-map status 비교는 아직 없다.
- 기준 경로: framework/doc/framework/common/e2e/ 전체.
- 구현 경로:
  framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/JavaDocumentationRegressionTest.java:22-180,
  framework/languages/java/zlink-framework-kotlin/src/contractTest/kotlin/systems/zlink/framework/kotlin/KotlinPublicSurfaceContractTest.kt,
  framework/languages/java/e2e/run_e2e_all.sh:9-110,
  framework/languages/java/e2e-kotlin/run_e2e_all.sh:7-90,
  framework/languages/java/e2e/AutomaticTurnDispatch/feature-map.ko.md,
  framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/feature-map.ko.md.
- 확인한 동작과 기대 동작:
  current contractTest는 `IS-E2E-*`를 포함한 모든 common ID를 읽고 Java `.java`·`.sh`와
  Kotlin `.kt`·`.java`·`.sh` source, 14개 suite directory, aggregate runner mapping을
  언어별로 분리해 검사한다. 현재 report는 Java Config 12·14와 154개 source ID,
  Kotlin Config 6·12·13·14와 192개 source ID가 없음을 함께 출력한다. feature-map
  status와 role evidence는 아직 같은 gate에 포함하지 않았다.
- 판정 근거:
  test failure는 유효하지만 missing ID 일부만 보고하면 후속 작업 순서를 잘못
  정할 수 있다. test를 약화해 green으로 만들지 않는다.
- 수정 목록:
  현재 Java/Kotlin gate를 기반으로 feature-map status·role evidence를 추가하되,
  누락 ID를 marker로 채워 green으로 만들지 않는다.
- 필요한 회귀 test: JK-REG-008, JK-REG-009.
- 선행 조건과 작업 순서: E0 inventory schema → audit test 개선 → contractTest.
- 구현 완료 evidence: common 374 ID의 language별 exact diff를 모두 출력하고,
  실패 결과가 aggregate runner와 일치한다.

### JK-TEST-002 — Java/Kotlin CI workflow와 path filter coverage

- 상태: gap.
- 기준 경로:
  .github/workflows/docs.yml, build.yml, framework-node.yml,
  framework-dotnet.yml, Java/Kotlin aggregate runner와 sample runner.
- 구현 경로:
  .github/workflows/docs.yml, .github/workflows/build.yml,
  .github/workflows/framework-node.yml, .github/workflows/framework-dotnet.yml,
  framework/languages/java/e2e/run_e2e_all.sh,
  framework/languages/java/e2e-kotlin/run_e2e_all.sh,
  framework/languages/java/samples/run_samples.sh.
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
- 필요한 회귀 test: JK-REG-009, JK-REG-011, JK-REG-012, JK-REG-013, JK-REG-014,
  JK-REG-015를 CI에서 실행하고 path
  matrix 변경 시 job 선택 여부를 workflow validation으로 확인한다.
- 선행 조건과 작업 순서: runtime/package gate → E2E aggregate → sample aggregate
  → workflow review.
- 구현 완료 evidence: Java/Kotlin production, package consumer, focused/aggregate
  E2E, 12 sample process가 해당 path filter에서 자동 실행되고 skip은 blocker
  report로만 허용된다.

### JK-TEST-003 — API snapshot과 clean package consumer gate

- 상태: 부분 해결. Java/Kotlin package script가 declared artifact를 temporary
  repository에 publish하고 HTTP version을 source에서 읽은 뒤 HTTP·Stream public type을
  clean consumer에서 compile한다. Kotlin contract test에는 checked-in JVM descriptor
  hash가 있지만 Java·HTTP·Stream 전체 artifact를 대상으로 하는 checked-in API
  snapshot과 diff gate는 아직 없다.
- 기준 경로:
  framework/doc/contract-inventory/jvm-public-contract-source-owners.json,
  framework/doc/framework/common/spec/00-public-contract-governance.ko.md:130-146.
- 구현 경로:
  framework/languages/java/scripts/verify_packaged_contract.sh:19-45,
  framework/languages/java/zlink-framework-core/src/main/java/module-info.java,
  framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/JavaDocumentationRegressionTest.java,
  framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/JavaTargetContractGapTest.java,
  framework/languages/java/zlink-framework-kotlin/src/contractTest/kotlin/systems/zlink/framework/kotlin/KotlinPublicSurfaceContractTest.kt,
  framework/languages/java/zlink-http-client/, framework/languages/java/zlink-http-client-kotlin/와
  clean consumer.
- 확인한 동작과 기대 동작:
  package script는 provider/binding/core/starter/location/stream/HTTP/codec artifact와
  Kotlin 선택 artifact를 publish한다. Java consumer는 framework·actor·spot·session,
  Stream factory/options/connector와 HTTP client를 import하고, Kotlin consumer는
  Kotlin framework·Stream factory·HTTP coroutine DSL을 compile한다. 두 consumer가
  통과하고 Kotlin 일부 facade의 JVM descriptor hash도 검사하지만, checked-in API
  snapshot artifact 또는 Java·HTTP·Stream 전체의 생성·비교 명령은 아직 없다.
  snapshot 부재를 empty diff로 해석하지 않는다.
- 판정 근거:
  source compile과 clean consumer가 통과해도 API snapshot이 없으면 public contract
  완료가 아니다. 현재 `package_dependency_publication`과 `clean_consumer`는 통과했고
  `api_snapshot_not_found`는 남아 있다.
- 수정 목록:
  현재 package script와 source-owner inventory를 유지한다. API snapshot의 authoritative
  artifact와 생성·비교 명령을 정한 뒤 source-owner JSON 및 exact interface와 diff를
  비교한다. evidence schema는 `package_dependency_publication`, `clean_consumer`,
  `module_boundary`, `api_snapshot_presence`, `api_snapshot_diff`를 분리한다.
- 필요한 회귀 test: JK-REG-012와 JK-REG-013. Java/Kotlin clean consumer, module
  boundary, package metadata, HTTP/stream transitive dependency, Kotlin HTTP exact
  surface와 API snapshot 상태를 확인한다.
- 선행 조건과 작업 순서: source/API inventory → dependency publication → package
  consumer → API snapshot → runtime/E2E/sample.
- 구현 완료 evidence: clean consumer가 Java/Kotlin/HTTP artifact와 모든 declared
  dependency를 resolve하고 compile하며, module boundary가 source-owner JSON과
  일치한다. API snapshot artifact와 비교 명령이 존재하고 exact contract diff가
  의도된 변경 외에는 empty이며, Kotlin HTTP `yield`와 `yieldAwait` 부재도 같은
  evidence에 포함된다.

### JK-TEST-004 — HTTP·stream package source-owner inventory 누락

- 상태: 부분 해결. source-owner JSON에 Java HTTP, Kotlin HTTP, Stream Connector와
  Protobuf/MessagePack artifact의 public owner와 boundary gate를 추가했고,
  `JvmPublicContractSourceOwnerTest`가 inventory와 live source/module/settings 연결을
  자동 검사한다. API snapshot과 exact interface 비교는 아직 없다.
- 기준 경로:
  framework/doc/contract-inventory/jvm-public-contract-source-owners.json:3-65,
  framework/doc/framework/common/spec/00-public-contract-governance.ko.md:130-146,
  framework/languages/java/settings.gradle.kts:20-34.
- 구현 경로:
  framework/doc/contract-inventory/jvm-public-contract-source-owners.json,
  framework/languages/java/zlink-http-client/src/main/java/systems/zlink/httpclient/,
  framework/languages/java/zlink-http-client-kotlin/src/main/kotlin/systems/zlink/httpclient/kotlin/,
  framework/languages/java/zlink-stream-connector/src/main/java/systems/zlink/stream/connector/,
  framework/languages/java/zlink-framework-codec-protobuf/,
  framework/languages/java/zlink-framework-codec-msgpack/,
  framework/languages/java/zlink-framework-core/src/contractTest/java/systems/zlink/framework/JvmPublicContractSourceOwnerTest.java와 package contract test.
- 확인한 동작과 기대 동작:
  JSON의 `clientArtifacts`가 Java/Kotlin HTTP, Stream Connector, codec artifact,
  public package/module, exact contract owner와 boundary gate를 기록한다. package
  script는 이 artifact set을 publish하고 clean consumer가 일부 public type을
  compile한다. `JvmPublicContractSourceOwnerTest`는 artifact project, settings
  module, source owner, public package와 Java module-info 연결을 검사한다. 기대 동작은
  이 inventory와 API snapshot이 같은 owner set을 사용하는 것이다.
- 판정 근거:
  현재 inventory-to-source/module/settings 연결 test는 통과하지만, `settings.gradle.kts`에
  module이 존재하거나 source/test 파일이 있다는 사실만으로 API snapshot과 exact
  interface 일치를 증명하지는 않는다.
- 수정 목록:
  `JvmPublicContractSourceOwnerTest`에서 `clientArtifacts` schema, settings module 목록,
  public package/source path와 Java module-info 연결을 검사한다. 다음 단계에서 같은
  owner set을 API snapshot과 exact interface 비교에 연결한다.
- 필요한 회귀 test: JK-REG-015. settings module 목록과 source-owner inventory의
  public artifact set을 비교하고, 각 package의 exact interface, export, clean consumer,
  boundary gate가 owner entry를 통해 추적되는지 확인한다.
- 선행 조건과 작업 순서: inventory ownership decision → JSON/schema 또는 별도
  package inventory → API snapshot/package consumer → runtime/E2E/sample.
- 구현 완료 evidence: public server, HTTP Java/Kotlin, stream, codec artifact가
  authoritative owner entry와 정확히 연결되고, owner 없는 package는 별도 blocker로
  보고된다. contract test와 package consumer가 같은 inventory를 사용한다.

## 9. 작업 순서

작업 순서는 반드시 runtime → E2E → sample이다. 앞 단계의 contract 또는 production
call path가 확정되지 않은 상태에서 뒤 단계의 sample/client 코드를 복잡하게 만들어
통과시키지 않는다.

### 0단계 — audit baseline과 작업 경계 고정

1. dirty working tree의 status와 대상 파일 manifest를 보존한다. unrelated 변경을
   staging하거나 되돌리지 않는다.
2. common spec, Java/Kotlin exact interface, source-owner JSON, common E2E 374 ID,
   common sample six-set을 diff artifact로 고정한다.
3. review 요청 시점에 현재 OpenAI guide를 확인하고, 필요한 Codex reviewer 또는 `Claude Fable`의
   사용 가능 여부와 결과 형식을 확인한다. reviewer가 바로 unavailable이면 해당 review만 대기하고
   독립적으로 진행할 수 있는 audit·test 준비를 계속한다.
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
4. JK-IMP-009, JK-IMP-011: application-wide HWM, admission/preflight, deadline,
   cancellation, shutdown, status evidence와 HTTP CompletionStage path를 연결한다.
5. JK-IMP-003, JK-IMP-007, JK-IMP-008, JK-IMP-010: exact public surface, Fanout
   builder, RouteMesh options, Kotlin wrapper, Kotlin HTTP coroutine surface와 API
   snapshot을 정리한다.
6. JK-TEST-004: public package source-owner inventory를 확정하고 package/API gate가
   같은 owner set을 사용하게 한다.
7. 각 항목의 regression test를 먼저 추가하거나 현재 test를 목표 계약에 맞춘 뒤
   compile, unit, integration, fake backend, contract test를 실행한다.
8. Java와 Kotlin packaged consumer가 통과할 때까지 E2E 수정으로 넘어가지 않는다.

runtime 단계 완료 조건:

- Java/Kotlin exact interface와 source export/API snapshot diff가 정리되었다.
- HWM, codec, actor, stream production call path가 직접 실행된 regression test를
  가진다.
- concurrent shutdown, in-flight operation, cancellation, retry/rollback/cleanup의
  terminal reason이 test evidence에 있다.
- verifyPublicContractModuleBoundary, core/Kotlin/stream/HTTP test, package consumer와
  source-owner inventory gate가 통과한다.

### 2단계 — E2E process와 aggregate

1. JK-E2E-IMP-001, JK-E2E-IMP-002의 exact inventory와 Config 6
   `StoreFailure`/`DiscoveryRegistryHa` alias decision을 해결한다. common ID를
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

이 절은 구현자가 지켜야 할 작업 절차이다. 하나의 stage가 필요한 test와 독립 review를
통과하면 path-limited commit과 push를 수행하고 다음 stage로 진행한다. 문서 본문의 진행
log는 갱신하지 않으며, commit·push와 review 결과는 대응하는 `log/`에 기록한다.

public contract, production call path, concurrent lifecycle, E2E evidence 경계를 함께
판정하는 round(R0, R1, R2, E0, E1, F0)는 review 요청 시점의 OpenAI 공식 guide가 정한
frontier model과 위험도에 맞는 `high` 이상 reasoning을 사용한다. 필요하면 `Claude Fable`에
독립적인 설계·진단 도움을 요청한다. 단순한 manifest·경로·Markdown 기계 검사는 별도 local
gate로 수행하며, 그 결과를 contract/runtime review의 대체 근거로 사용하지 않는다. sample
변경을 검토하는 S0/S1도 public API와 process evidence를 판단하므로 같은 선택 규칙을 유지한다.

### 10.1 review round

각 round에서 선택한 review agent는 candidate SHA 또는 변경 manifest를 기준으로 다음을
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

### 10.2 review agent 결과 형식

각 review는 다음 항목을 포함한다.

    reviewer: Codex review agent | Claude Fable
    model: <actual model ID selected from the current OpenAI guide, or Claude Fable>
    reasoning_effort: <actual level, if applicable>
    review_mode: read-only
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
않는다. 선택한 높은 수준의 Codex model을 사용할 수 없으면 `Claude Fable`에 도움을
요청할 수 있다. 두 경로 모두 바로 사용할 수 없으면 해당 review를 대기하고, 독립적으로
가능한 조사·test 준비를 계속한다. 완료 판정은 적절한 review evidence가 마련된 뒤에 한다.

### 10.3 commit과 push 절차

1. dirty working tree의 기존 변경을 다시 status로 확인하고 이번 stage의 변경
   manifest만 만든다.
2. 선택한 review agent의 review가 DESIGN_ACCEPTED 또는 CLEAN이어야 한다.
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

- JavaDocumentationRegressionTest: Java common 374 ID의 source/runner/suite missing
  diff를 한 번에 출력하도록 parser와 report를 고쳤다. Kotlin feature-map/status와
  role evidence diff는 아직 추가해야 한다.
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

아래 16개는 이 ledger가 제안하는 regression ID이다. 구현이 끝났다는 뜻이 아니라
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
- JK-REG-013: Kotlin HTTP exact public surface의 `yield`와 `yieldAwait` 부재,
  typed/one-way return type, coroutine cancellation, timeout, HTTP status/body와
  terminal error
- JK-REG-014: Kotlin HTTP coroutine cancellation과 CompletionStage, retry attempt,
  sendAsync, body-read deadline, client lease, execution turn의 terminal/cleanup 관계
- JK-REG-015: Java/Kotlin server·HTTP·stream·codec public artifact와 source-owner
  inventory, module boundary, exact interface, package consumer의 일치
- JK-REG-016: Framework native Socket별 public Poller readiness, STREAM recv-only ingress,
  HWM pause/resume, poller close와 malformed peer isolation

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
- [ ] Framework가 사용하는 모든 native zlink Socket receive가 동일 Socket의 public
  Poller readiness 뒤에 실행되고, STREAM ingress가 packet callback을 등록하지 않는다.
- [ ] public extra/missing surface를 contract 선행 또는 exact implementation gap으로
  분류했다.
- [ ] API snapshot, module boundary, package export와 clean consumer가 통과했다.
- [ ] JK-REG-001부터 JK-REG-007, JK-REG-012부터 JK-REG-016이 통과했다.
- [ ] HTTP·stream·codec public package의 source-owner inventory가 존재하거나 owner
  부재가 별도 blocker로 보고된다.

### E2E

- [ ] common Config 1–14의 374 scenario ID를 inventory로 고정했다.
- [ ] Java feature-map의 missing/extra/alias와 Kotlin feature-map의
  missing/extra/alias를 모두 해소했다.
- [ ] Java aggregate에 Config 12와 Config 14가 포함된다.
- [ ] Kotlin aggregate에 Config 12, Config 13, Config 14가 포함된다.
- [ ] Kotlin Config 6의 `StoreFailure`와 `DiscoveryRegistryHa` suite ownership 또는
  승인된 alias mapping이 manifest와 aggregate selector에 명시된다.
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
- [ ] API snapshot artifact와 비교 명령이 존재하고 `api_snapshot_not_found`를
  empty diff로 기록하지 않는다.
- [ ] Java/Kotlin CI job, path filter, skip list가 runtime/E2E/sample gate를 빠뜨리지
  않는다.
- [ ] POSD/DDD review round R0, R1, R2, E0, E1, S0, S1, F0의 결과가 있다.
- [ ] 각 stage commit은 path-limited이고 unrelated 사용자 변경을 포함하지 않는다.
- [ ] push 후 CI 결과가 같은 revision의 증거로 기록된다.

## 13. 이 문서의 현재 작업량과 미해결 blocker

이 ledger가 추가하는 gap ID는 다음과 같다.

- production implementation/runtime: JK-IMP-001부터 JK-IMP-012까지 12개
- Java/Kotlin E2E implementation/runner/process evidence:
  JK-E2E-IMP-001부터 JK-E2E-IMP-006까지 6개
- sample implementation/runner/process evidence:
  JK-SAMPLE-IMP-001부터 JK-SAMPLE-IMP-004까지 4개
- audit/regression/CI gate: JK-TEST-001부터 JK-TEST-004까지 4개
- 합계: 26개

추가·변경할 regression ID는 JK-REG-001부터 JK-REG-016까지 16개이다.

현재 해결하지 않은 blocker는 다음과 같다.

- Java/Kotlin ST-A1 focused E2E는 통과했지만 ST-A2 이후 transfer, Config 10 전체,
  Config 14 process fixture는 아직 없다.
- Java/Kotlin packaged consumer는 현재 provider, HTTP, Stream artifact를 resolve하고
  public type을 compile한다. API snapshot과 전체 transitive contract는 아직 별도
  gate가 필요하다.
- Kotlin HTTP exact spec과 common language interface, production source는 `yield<T>()`와
  `yieldAwait` 부재로 정렬되었다. clean API snapshot과 coroutine cancellation
  propagation evidence는 아직 증명되지 않았다.
- Kotlin HTTP cancellation은 coroutine `await()`에서 server builder, retry,
  `sendAsync`, body-read deadline까지 이어지는 production path의 stage ownership과
  cleanup evidence가 없다.
- API snapshot artifact 또는 생성·비교 명령을 live tree에서 찾지 못해 public surface
  snapshot 상태가 미검증이다. 이를 empty diff로 간주하지 않는다.
- `JavaDocumentationRegressionTest`는 parser 문제를 넘어서 현재 Java source/runner의
  `expected=374`, `sourceIds=220`, `missingIds=154`와 Kotlin source/runner의
  `expected=374`, `sourceIds=183`, `missingIds=191`을 보고한다. Config 8의 `TD-*`와
  `ATD-*` selector mapping도 아직 exact contract가 아니다.
- source-owner JSON에는 HTTP Java/Kotlin, Stream Connector, codec artifact가 기록되고
  `JvmPublicContractSourceOwnerTest`가 exact owner-to-source/module/settings 연결을
  검사한다. API snapshot과 exact method set 비교는 아직 남아 있다.
- Java/Kotlin sample/E2E configuration policy gate가 environment/JVM property
  access와 일부 reflection 경로를 보고한다.
- Java/Kotlin aggregate는 canonical suite preflight를 수행하지만 Java의
  `ChannelEgressRouting`, Kotlin의 `StoreFailure`, `ChannelEgressRouting`,
  `SubmitAdmission`, `InstanceSpot` suite가 없어 `aggregate_incomplete`로 중단한다.
  12 sample process와 Config 1–14의 실제 실행을 완료하지 않았다.
- Java PubSub aggregate는 최신 실행에서 PS-B2의 publisher topology row 대기에서
  timeout으로 실패했고, 같은 selector의 단독 재실행도 같은 `publisher row` timeout으로
  실패했다. Kotlin PubSub aggregate는 PS-A1~A4, PS-B1~B2, PS-C1이 통과했다. Java
  전체 process evidence는 안정된 aggregate PASS로 승격하지 않는다.
- Java/Kotlin 전용 CI workflow와 runtime/E2E/sample aggregate의 path ownership이
  현재 명시적으로 연결되어 있지 않다.

이 문서는 위 blocker를 해결했다고 표시하지 않는다. 구현 단계에서 먼저 runtime,
그 다음 E2E, 마지막으로 sample 순서를 지키고, 각 단계의 codex sol agent POSD·DDD
review와 commit/push evidence가 확보된 뒤 다음 단계로 진행한다.
