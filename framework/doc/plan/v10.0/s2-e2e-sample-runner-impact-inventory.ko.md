# S2 Framework 10.0.0 E2E·sample·runner 영향 inventory

> 이 문서는 Framework 10.0.0 정식 계약을 E2E, sample, runner와 package 검증에 반영하는
> 담당자가 어떤 파일과 실행 경로를 확인해야 하는지 답하는 실행 inventory다. 현재 동작을
> 설명하는 정식 spec이나 guide가 아니며, 이 문서의 완료 표시는
> [`RouteMesh 10.0.0 실행 진행표`](./route-mesh-10.0.0-execution-ledger.ko.md)의 S2 상태를
> 대신하지 않는다.

## 1. 기준과 범위

이 inventory는 다음 문서를 입력으로 사용한다.

- [`S0 범위와 기준선`](./s0-scope-baseline.ko.md)
- [`S2 Framework 정식 계약 전환 inventory`](./s2-framework-contract-transition-inventory.ko.md)
- [`RouteMesh messaging 통합 계획`](./framework-route-mesh-messaging-consolidation.ko.md)
- [`MeshNode framework dispatch 설계`](./mesh-node-framework-dispatch-design.ko.md)
- Core와 Framework 10.0.0 정식 spec

이 문서가 다루는 범위는 다음과 같다.

1. 공통 E2E 문서와 .NET·C++·Java·Kotlin·Node 언어별 E2E 문서, public 예제와 실제 runner 디렉터리
2. 공통 sample 문서와 다섯 언어별 sample 문서, public 예제와 실제 sample 디렉터리
3. E2E·sample runner template과 일괄 실행 진입점
4. bindings package 버전의 언어별 단일 owner
5. 문서·runner·package 검증의 완료 증거

정식 계약의 owner와 언어별 exact public interface는
[`S2 Framework 정식 계약 전환 inventory`](./s2-framework-contract-transition-inventory.ko.md)가
소유한다. 이 문서는 같은 계약을 다시 정의하지 않고, 검증 대상과 필요한 결과만 연결한다.

## 2. 고정 범위와 충돌 보존

이 inventory의 완료 근거는 특정 시점의 `git status`가 아니라 §10~§17의 파일 경로, 영향 분류와
후속 stage다. 작업을 시작할 때는 아래 절차로 live checkout을 다시 확인한다.

1. 수정할 파일의 staged·unstaged diff를 각각 읽고 현재 변경 목적을 확인한다.
2. 이 inventory가 고정한 10.0.0 변경과 무관한 사용자 변경은 그대로 보존한다.
3. 같은 문단이나 예제가 겹치면 자동으로 합치지 않고 두 변경의 계약 의미를 먼저 대조한다.
4. `git status`의 `M`, `MM`, 새 파일 여부는 실행 중 충돌 방지 정보로만 사용하며 S2 완료 증거로
   기록하지 않는다.
5. S2 완료는 경로별 영향 분류, 문서 계약 검증과 독립 review 결과로 판정한다. source·runner·package
   실행 결과는 표에 지정한 S8·S9 stage에서 확보한다.

## 3. 공통 E2E와 실제 실행 lane

### 3.1 공통 owner

`framework/doc/framework/common/e2e/README.ko.md`는 Config 1~11의 목적, 공통 환경 조건,
언어별 실행 진입점과 전체 성공 조건을 연결한다. 개별 Config의 상세 계약은 아래 파일이 소유한다.
언어별 `feature-map.ko.md`는 공통 시나리오 ID가 해당 runner에서 어떤 marker와 assertion으로
검증되는지 기록한다.

| S2 ID | Config·공통 owner | 필요한 10.0.0 검증 변경 | 실제 언어별 lane | 실행·증거 | 상태·위험 |
|---|---|---|---|---|---|
| S2-20, S2-29 | Config 1 `config-1-location-messaging.ko.md` | MeshName·ChannelName 구성, direct RID, select-one, weighted round-robin, no-member, descriptor와 manual peer validation | .NET `LocationMessaging`; C++·Java·Kotlin·Node.js `RegistryMessaging` | 각 lane `run_e2e.sh`, `feature-map.ko.md`, selection 분포와 실패 marker | 공통 목표 scenario 반영; source·runner 적용은 S8·S9 |
| S2-21, S2-29 | Config 2 `config-2-spot-service.ko.md` | MeshNode 기반 Spot direct call, node-local match, Logical Multicast, ROUTER backpressure, timer, Actor·STREAM owner와 bridge 부재 | 모든 언어 `SpotService` | publish 대상 node 수, local subscription 수, complete message ref 공유와 session marker | 공통 목표 scenario 반영; multicast를 classic fanout으로 검증하지 않는다 |
| S2-22, S2-29 | Config 3 `config-3-pubsub.ko.md` | classic fanout publisher가 실제 endpoint와 고유 RID를 location store에 게시하고, endpoint를 지정하지 않은 subscriber가 같은 ChannelName의 publisher만 자동으로 발견하는지 검증한다. Manual endpoint 방식과 RouteMesh Logical Multicast의 독립성도 함께 확인한다 | 모든 언어 `PubSub` | descriptor·owner lease·publisher 추가/제거·재등록·port 0 재시작·store 장애 복구·manual endpoint 회귀 marker와 실제 connection set assertion | fanout 자동 발견 계약을 추가했다. Source·runner 적용은 S8·S9의 언어별 fanout 항목에서 진행하며 Config 2와 합치지 않는다 |
| S2-19, S2-29 | Config 4 `config-4-registration-codec.ko.md` | canonical metadata codec, byte 상한, hostile ingress rejection, immutable handler snapshot과 typed message codec 유지 | 모든 언어 `RegistrationCodec` | 공통 byte corpus, invalid frame, metadata와 typed payload assertion | 비영향 확인; metadata와 payload codec 책임을 혼합하지 않는다 |
| S2-23, S2-29 | Config 5 `config-5-resilience-lifecycle.ko.md` | drain 이후 새 submit 거부, sealed work 완료, cancellation, late reply, full-mesh peer 재연결과 종료 순서 | 모든 언어 `ResilienceLifecycle` | lifecycle marker 순서와 terminal result, process exit code | 영향 확인; source·runner 재검증은 S8·S9 |
| S2-23A, S2-29 | Config 6 `config-6-store-failure-recovery.ko.md` | 분산 기능의 Redis 필수 조건, 명시 등록, descriptor와 location row 분리, startup failure, manual admission 예외 | .NET·Java `StoreFailure`; C++·Kotlin·Node.js `DiscoveryRegistryHa` | dependency failure·recovery marker, Redis key 확인과 startup exit code | 비영향 확인; lane 이름 차이를 기능 차이로 해석하지 않는다 |
| S2-23, S2-29 | Config 7 `config-7-monitoring.ko.md` | MeshNode·peer·channel readiness, Logical Multicast backpressure·drop과 runtime health | 모든 언어 `RuntimeMonitoring` | monitor event, bounded label, readiness transition과 counter assertion | 영향 확인; 공개 monitor API만 사용해 S8·S9에서 재검증 |
| S2-19, S2-29 | Config 8 `config-8-execution-turn.ko.md` | application turn과 infrastructure pump 분리, completion·send-ready 독립 진행, timer generation과 shutdown freeze | 모든 언어 `AutomaticTurnDispatch` | turn ordering, completion, timer와 shutdown marker | Node.js target `feature-map.ko.md`를 추가했으며 source·runner 적용은 S9로 deferred |
| S2-23A, S2-29 | Config 9 `config-9-to-actor-messaging.ko.md` | Actor owner direct messaging, stale·disconnected·no-bind 결과, bound session dispatch와 one-shot reply | 모든 언어 `ToActorMessaging` | Actor owner·generation, session bind와 failure result assertion | 영향 확인; Actor payload를 Spot callback으로 전달하지 않는다 |
| S2-23, S2-29 | Config 10 `config-10-spot-actor-transfer.ko.md` | participant-set CAS, token·lease, transfer state, failure point recovery, stale fence와 session FIFO | 모든 언어 `SpotActorTransfer` | 각 failure point의 durable state, successor recovery와 callback ordering | Kotlin target `feature-map.ko.md`를 추가했으며 source·runner 적용은 S9로 deferred |
| S2-23, S2-29 | Config 11 `config-11-observability-ops.ko.md` | RouteMesh metric·flow·drain, multicast correlation, bounded label과 client connector reconnect 계기 | 모든 언어 `ObservabilityOps`와 언어별 Stream Connector metric provider·sink | server·connector metric catalog, flow tree, drain snapshot과 label cardinality assertion | server session이 reconnect를 추측하지 않고 trigger connector의 public reader가 직접 검증; 문자열 marker만으로 완료 판정하지 않는다 |

.NET, C++, Java와 Node.js lane은 각각 `framework/languages/{dotnet,cpp,java,node}/e2e/` 아래에
있다. Kotlin lane은 Java와 같은 build를 사용하므로 `framework/languages/java/e2e-kotlin/` 아래에
있다. 각 lane root의 `run_e2e_all.sh`는 Config 1~11을 순서대로 실행하고, 중간 실패를 최종 성공으로
덮어쓰지 않아야 한다.

### 3.2 구현 stage의 E2E 완료 조건

S2는 각 Config에 필요한 변경과 검증 파일을 식별한다. 실제 framework 구현 stage에서는 다음 증거를
함께 확보한다.

1. 공통 문서의 scenario ID와 언어별 `feature-map.ko.md`의 exact mapping
2. 공개 API만 사용하는 실행 가능한 E2E source
3. `run_e2e.sh`의 성공·실패 exit code와 grep 가능한 marker
4. package consumer가 실제 10.0.0 bindings artifact를 사용했다는 출력
5. 해당 언어 `run_e2e_all.sh`의 최종 성공 로그

## 4. 공통 sample과 실제 실행 lane

### 4.1 sample owner mapping

| S2 ID | 공통 owner | 필요한 10.0.0 변경 | 실제 언어별 sample | 실행·증거 | 상태·위험 |
|---|---|---|---|---|---|
| S2-24/25/30 | `common/sample/README.ko.md` | RouteMesh, MeshNode, ChannelName, Logical Multicast와 classic fanout을 sample별 목적에 연결하고 지원 언어를 실제 디렉터리와 맞춘다 | 아래 일곱 sample 전체 | 언어별 `run_samples.sh`, 상대 link와 디렉터리 inventory | 공통 목표 설명 반영; 실제 runner 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/bingo/README.ko.md` | direct RID·channel select-one·Spot·Actor 중 sample이 사용하는 공개 경로를 명시한다 | 다섯 언어 `Bingo` | `run_sample.sh`, 라운드 결과와 종료 marker | 목표 topology 반영; source·runner 적용은 S8·S9 |
| S2-24/25/30 | `common/sample/deliverydispatch/README.ko.md` | weighted round-robin, no-member와 graceful drain을 업무 흐름에 연결한다 | 다섯 언어 `DeliveryDispatch` | dispatch 분포, drain과 terminal result | 목표 ChannelName 선택 의미 반영; 실행 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/event/README.ko.md` | event sample이 사용하는 ChannelName, Spot Logical Multicast와 classic fanout의 경계를 설명한다 | `GameQuest`, `ShoppingMall` | 두 sample runner와 topic·target assertion | 목표 기능 경계 반영; 실행 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/event/gamequest.ko.md` | MeshNode 구성, node-local Spot match와 대상별 publish 결과를 실제 코드와 맞춘다 | 다섯 언어 `GameQuest` | quest publish 대상과 local delivery count, exit code | 목표 Logical Multicast 의미 반영; 실행 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/event/shoppingmall.ko.md` | ChannelName service와 fanout event를 별도 interaction으로 설명한다 | 다섯 언어 `ShoppingMall` | order selection과 event fanout assertion | 목표 기능 경계 반영; 실행 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/languages/README.ko.md` | 다섯 언어의 sample 이름, 실행 명령과 package 준비 절차를 실제 경로와 맞춘다 | 언어별 sample root | command existence, relative link와 package pin 검사 | 비영향 확인; 실제 package pin 검증은 S8·S9 |
| S2-24/25/30 | `common/sample/supportchat/README.ko.md` | Actor owner, Spot membership, bound session과 transfer 시나리오를 공개 API로 표현한다 | 다섯 언어 `SupportChat` | conversation lifecycle, session push와 transfer marker | 영향 확인; source·runner 적용은 S8·S9 |
| S2-24/25/30 | `common/sample/tictactoe/README.ko.md` | Actor turn, request completion, stale fence와 종료 경계를 설명한다 | 다섯 언어 `TicTacToe` | ordered move, winner와 terminal result | 목표 topology 반영; source·runner 적용은 S8·S9 |
| S2-24/25/30 | `framework/doc/plan/v10.0/tictactoe-sample-implementation-prompt.ko.md` | 작업자용 생성 지침의 API 이름과 금지 surface를 정식 interface와 맞춘다 | C++, Java, Kotlin, Node `TicTacToe` | generated-code scoped no-hit와 build | 임시 작업 문서로 분리; 정식 sample 문서에서 참조하지 않는다 |
| S2-24/25/30 | `common/sample/zoneworld/README.ko.md` | zone Spot, Logical Multicast, STREAM session과 Actor owner의 관계를 설명한다 | .NET `ZoneWorld`; Node.js `ZoneWorld` | 두 runner와 기능 차이의 명시적 지원 표 | 목표 topology 반영; 없는 언어의 구현을 있다고 표시하지 않는다 |

.NET과 C++의 sample은 각 언어의 `samples/` 아래에서 표의 이름을 사용한다. Java와 Kotlin은
`framework/languages/java/samples/{java,kotlin}/` 아래에서 표의 이름을 사용한다. 두 언어의 공통
진입점은 `framework/languages/java/samples/run_samples.sh`다. Node.js는 `Bingo.Ts`,
`DeliveryDispatch.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`, `SupportChat.Ts`, `TicTacToe.Ts`를
사용하고 ZoneWorld는 `ZoneWorld`를 사용한다. C++·Java·Kotlin에는 ZoneWorld 디렉터리가 없다.

### 4.2 구현 stage의 sample 완료 조건

sample은 사용자가 따라 하는 public contract 예제다. S2는 변경 파일과 검증 기준을 식별하고, 해당 언어
구현 stage는 다음 조건을 모두 만족해야 한다.

1. 공통 문서의 설명과 실제 source가 같은 public API를 사용한다.
2. sample source에 raw frame, internal helper, reflection 또는 test-only adapter가 없다.
3. 각 `run_sample.sh`가 package artifact를 사용해 build와 실제 시나리오를 수행한다.
4. 언어별 `run_samples.sh`가 지원 sample을 누락 없이 실행한다.
5. 공통 문서의 지원 언어 표가 실제 디렉터리와 일치한다.

## 5. Runner와 template owner

| S2 ID | owner | 필요한 10.0.0 변경 | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/redis-common.template.sh` | Redis readiness, namespace 격리, 정리와 failure reporting의 공통 계약 | Config 1·6·10에서 template 사용 여부와 실패 exit code 확인 | 영향 분류 완료; 실제 runner 반영은 S8·S9 |
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/run_e2e.template.sh` | package pin 출력, timeout, child cleanup, marker와 실패 보존 | 다섯 언어 실제 runner와 구조 비교, shell syntax 검사 | 변경 요구 고정; 실제 runner 반영은 S8·S9 |
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/run_e2e_all.template.sh` | Config 1~11 순서, fail-fast와 최종 summary | 다섯 `run_e2e_all.sh`의 실행 집합 비교 | 영향 분류 완료; 실제 runner 반영은 S8·S9 |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/redis-common.template.sh` | sample Redis 준비·격리·정리 | Redis 사용 sample runner와 구조 비교 | 영향 분류 완료; 실제 runner 반영은 S8·S9 |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/run_sample.template.sh` | package pin 출력, timeout, cleanup와 업무 결과 marker | 모든 `run_sample.sh`의 필수 단계 검사 | 변경 요구 고정; 실제 runner 반영은 S8·S9 |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/run_samples.template.sh` | 지원 sample 전수 실행과 실패 보존 | 다섯 `run_samples.sh`와 §4 inventory 집합 비교 | 영향 분류 완료; 실제 runner 반영은 S8·S9 |

template은 그대로 복사할 코드 조각이 아니라 runner의 공통 안전 조건을 소유한다. 언어별 도구 차이는
runner가 처리하되, package pin·timeout·cleanup·exit code·marker 보장은 빠지면 안 된다.

## 6. Bindings package 버전 owner

Framework는 bindings source를 직접 참조하지 않고 10.0.0 package artifact를 사용한다. 다음 파일만
언어별 version pin을 소유한다.

| S2 ID | 언어 | 단일 owner와 key | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-26 | .NET | `framework/languages/dotnet/Directory.Packages.props`의 `ZLinkBindingsPackageVersion` | restore graph, package lock과 E2E 출력의 resolved version 비교 | S8에서 10.0.0 artifact로 검증 |
| S2-26 | Java·Kotlin | `framework/languages/java/gradle/libs.versions.toml`의 `zlinkBindings` | Gradle dependency graph와 두 언어 runner의 resolved module 비교 | S9에서 같은 owner의 10.0.0 artifact로 검증 |
| S2-26 | Node.js | `framework/languages/node/package.json`의 `@zlink-systems/zlink` dependency | lockfile, 설치 결과와 package tarball 내용 비교 | S9에서 10.0.0 artifact로 검증 |
| S2-26 | C++ | `framework/languages/cpp/CMakeLists.txt`의 `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION` | configure 출력과 `tests/Zlink.Framework.PackageTests/install_consumer.cmake` consumer 확인 | S9에서 10.0.0 artifact로 검증 |
| S2-26 | 공통 local package 정책 | `scripts/local-package/README.ko.md` | 언어별 중앙 pin, package 생성 위치, WSL 경로와 실제 script 비교 | 정책 owner 확인; 실제 package 배포 검증은 S7~S9 |

package 검증은 source tree의 header나 assembly를 우연히 참조한 성공을 인정하지 않는다. runner는
resolved artifact의 이름, version과 실제 경로를 로그에 출력하고, package consumer 검증을 통과해야 한다.

## 7. S2 산출물과 검증 owner

| S2 ID | 산출물 owner | 필요한 결과 | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-19 | 공통 E2E Config 1~11과 언어별 E2E 문서·`feature-map.ko.md`·public 예제 | 모든 scenario의 영향·비영향·신규 검증 분류 | scenario ID exact-once, 다섯 언어 lane 존재와 runner source 대응 | §11의 55개 lane과 신규 feature map 두 개를 고정했고 source·runner는 S8·S9로 deferred |
| S2-20~23A | §3의 Config별 owner와 실제 lane | LocationMessaging, SpotService, PubSub, lifecycle·transfer·monitoring, Actor route와 store failure 영향 고정 | 관련 Config의 normal·boundary·failure 행과 언어별 marker 대응 | 공통 목표 문서 반영과 신규 feature map 두 개 작성 완료; source·runner는 S8·S9 |
| S2-24 | 공통 sample 11개 문서, 언어별 sample 문서와 실제 sample 디렉터리 | 변경·삭제·신규·비영향과 지원 언어 분류 | 문서 집합, 실제 디렉터리와 support matrix 비교 | §12의 32개 lane과 언어별 루트 안내 4개 분류 완료; 구현 적용은 S8·S9 |
| S2-25 | 공통·언어별 sample public 예제와 실제 source | 제거 API, endpoint 배선, raw helper와 내부 API 사용처 전수 기록 | 금지 surface scoped no-hit, package build와 scenario 실행 | 문서 예제 분류 완료; source scoped 검사는 S8·S9 |
| S2-26 | §5·§6의 runner와 package owner | local package, clean consumer와 E2E·sample runner 변경점 기록 | dependency graph, artifact 내용, consumer와 runner smoke | owner와 요구 출력 고정; package 실행 증거는 S7~S9 |
| S2-27 | framework 언어별 guide·internals root와 이 문서 | 구현 후 수정할 문서의 독자, 질문, 정본과 검증 기준 mapping | `framework/doc/framework/{dotnet,cpp,java,kotlin,node}/` 실제 파일 inventory와 owner 중복 검사 | §14의 81개 실제 경로 mapping 완료; 수정은 S8·S9 |
| S2-27A | 정식 목표 spec과 `framework/doc/plan/v10.0/` | current-state 목표 계약과 실행 blueprint의 성격 분리 | 정식 목표 spec의 plan 참조·구현 이력 no-hit와 문서별 독자·질문 검사 | guide·internals 본문 current-state 전환은 §14에 따라 S8·S9에서 검증 |
| S2-28 | S3 review manifest의 검증 명령 | S2 정식 spec link, anchor, 금지 surface와 다섯 언어 machine inventory 검증 | 같은 frozen scope에서 같은 결과와 nonzero failure 재현 | S3 iteration마다 기록 |
| S2-28A | 언어별 doc compile fixture와 package API snapshot 계획 | S2에서 고정한 코드 예제와 exact signature를 실제 artifact에 검증할 파일·명령·실패 조건 | .NET fixture는 S8, C++·Java·Kotlin·Node.js fixture는 S9의 구현 red/green gate에 연결 | S2에서는 다섯 언어의 exact 계약과 fixture 설계를 고정 |
| S2-29 | 공통 E2E Config 1~11 | 10.0.0 topology, 입력, 관찰 결과와 failure scenario의 변경·비변경·신규 검증 분류 | common 문서와 언어별 feature map·runner의 영향 파일 대응 | §11의 55개 lane mapping 완료; 구현 증거는 deferred |
| S2-30 | 공통 sample 문서 | target API 예제, Redis extension, 지정된 manual topology와 runner의 변경·비변경 분류 | support matrix, code example fixture와 runner 영향 파일 대응 | §12의 32개 lane과 언어별 루트 안내 4개 mapping 완료; 구현 증거는 deferred |
| S2-31 | §5의 runner template과 실제 runner | topology setup, package 입력과 result marker 변경점을 파일별 고정 | template diff, shell syntax와 실패 주입 | §13의 96개 runner mapping 완료; 실제 수정은 S8·S9로 deferred |

## 8. 충돌 방지와 검증 순서

1. 작업 시점의 staged·unstaged diff를 각각 읽고 기존 목적과 S2 변경을 대조한다.
2. 정식 spec과 언어별 exact interface가 확정되기 전에 E2E나 sample이 새 public API를 정의하지 않는다.
3. common E2E는 검증 요구를 소유하지만, public contract를 추가하는 근거로 단독 사용하지 않는다.
4. sample과 runner의 helper로 framework나 bindings의 공개 기능 누락을 우회하지 않는다.
5. package pin을 중앙 owner 이외의 project 파일이나 runner에 중복 기록하지 않는다.
6. Config 1~11, sample 지원 표와 실제 directory 집합을 script로 비교한다.
7. 문서 link·anchor·금지 surface·code fixture 검증 후 package consumer와 runner를 실행한다.
8. 실제 적용과 실행 로그는 .NET S8, C++·Java·Kotlin·Node.js S9의 진행표에 연결한다.

## 9. S2 완료 점검

- Config 1~11의 공통 E2E 문서, 다섯 언어별 E2E 문서·lane·`feature-map.ko.md`·public 예제 영향이 파일 단위로 대응한다.
- 각 Config의 normal, boundary와 failure scenario에 필요한 변경·비변경·신규 검증이 분류되어 있다.
- 공통·언어별 sample 문서와 실제 언어별 sample 지원 집합의 적용 사항이 분류되어 있다.
- 다섯 언어의 sample public 예제, topology와 Redis 등록 변경을 검증할 source·runner가 식별되어 있다.
- E2E·sample runner의 package pin, timeout, cleanup, exit code와 marker 적용 사항이 식별되어 있다.
- 언어별 10.0.0 bindings package 중앙 owner와 구현 stage 검증 명령이 식별되어 있다.
- doc contract와 언어별 code fixture를 실행할 stage, 파일과 실패 조건이 고정되어 있다.
- 작업 시점에 겹친 기존 변경을 확인하고 보존했다.

Codex와 Claude Sonnet의 독립 리뷰 및 반복 종료 조건은 실행 진행표의 S3 gate가 관리한다.


## 10. 파일 단위 영향 분류 규칙

아래 표의 분류는 `git status`가 아니라 10.0.0 계약 적용 영향을 뜻한다.

| 분류 | 의미 |
|---|---|
| `changed` | 이 문서 단계에서 목표 계약과 검증 설명을 갱신한다. |
| `unchanged` | 10.0.0 topology와 공개 API 변경이 파일의 책임에 영향을 주지 않는다. |
| `new` | 기존 파일이 없어 이 단계에서 새 검증 문서를 만든다. |
| `deferred` | 영향은 확인했지만 source·runner 또는 구현과 함께 S8·S9에서 바꿔야 한다. 현재 구현 완료로 서술하지 않는다. |

`deferred`는 누락이나 비영향을 뜻하지 않는다. 각 행의 stage에서 package artifact를 사용해 검증한 뒤
`changed`와 완료 증거로 전환한다.

## 11. Config 1~11 × 다섯 언어 E2E lane

공통 owner는 §3의 Config 문서다. 각 행은 실제 lane, feature map과 runner를 고정한다. public source는
표의 lane 디렉터리 아래 `Client/`, `Server/`, `Shared/` 전체이며, 파일 추가·삭제를 포함한 source
목록은 해당 구현 stage의 scoped inventory 결과로 남긴다.

| Config | 언어 | 실제 lane | feature map | runner | 분류 | stage·이유 |
|---:|---|---|---|---|---|---|
| 1 | .NET | `framework/languages/dotnet/e2e/LocationMessaging/` | `framework/languages/dotnet/e2e/LocationMessaging/feature-map.ko.md` | `framework/languages/dotnet/e2e/LocationMessaging/run_e2e.sh` | `deferred` | S8: MeshName·ChannelName, select-one, direct RID와 manual peer topology; package artifact 기반 source·marker 재검증 |
| 1 | C++ | `framework/languages/cpp/e2e/RegistryMessaging/` | `framework/languages/cpp/e2e/RegistryMessaging/feature-map.ko.md` | `framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9: MeshName·ChannelName, select-one, direct RID와 manual peer topology; package artifact 기반 source·marker 재검증 |
| 1 | Java | `framework/languages/java/e2e/RegistryMessaging/` | `framework/languages/java/e2e/RegistryMessaging/feature-map.ko.md` | `framework/languages/java/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9: MeshName·ChannelName, select-one, direct RID와 manual peer topology; package artifact 기반 source·marker 재검증 |
| 1 | Kotlin | `framework/languages/java/e2e-kotlin/RegistryMessaging/` | `framework/languages/java/e2e-kotlin/RegistryMessaging/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh` | `deferred` | S9: MeshName·ChannelName, select-one, direct RID와 manual peer topology; package artifact 기반 source·marker 재검증 |
| 1 | Node.js | `framework/languages/node/e2e/RegistryMessaging/` | `framework/languages/node/e2e/RegistryMessaging/feature-map.ko.md` | `framework/languages/node/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9: MeshName·ChannelName, select-one, direct RID와 manual peer topology; package artifact 기반 source·marker 재검증 |
| 2 | .NET | `framework/languages/dotnet/e2e/SpotService/` | `framework/languages/dotnet/e2e/SpotService/feature-map.ko.md` | `framework/languages/dotnet/e2e/SpotService/run_e2e.sh` | `deferred` | S8: Spot direct·Logical Multicast·ROUTER backpressure와 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 2 | C++ | `framework/languages/cpp/e2e/SpotService/` | `framework/languages/cpp/e2e/SpotService/feature-map.ko.md` | `framework/languages/cpp/e2e/SpotService/run_e2e.sh` | `deferred` | S9: Spot direct·Logical Multicast·ROUTER backpressure와 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 2 | Java | `framework/languages/java/e2e/SpotService/` | `framework/languages/java/e2e/SpotService/feature-map.ko.md` | `framework/languages/java/e2e/SpotService/run_e2e.sh` | `deferred` | S9: Spot direct·Logical Multicast·ROUTER backpressure와 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 2 | Kotlin | `framework/languages/java/e2e-kotlin/SpotService/` | `framework/languages/java/e2e-kotlin/SpotService/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh` | `deferred` | S9: Spot direct·Logical Multicast·ROUTER backpressure와 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 2 | Node.js | `framework/languages/node/e2e/SpotService/` | `framework/languages/node/e2e/SpotService/feature-map.ko.md` | `framework/languages/node/e2e/SpotService/run_e2e.sh` | `deferred` | S9: Spot direct·Logical Multicast·ROUTER backpressure와 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 3 | .NET | `framework/languages/dotnet/e2e/PubSub/` | `framework/languages/dotnet/e2e/PubSub/feature-map.ko.md` | `framework/languages/dotnet/e2e/PubSub/run_e2e.sh` | `deferred` | S8-FO-DN: 전용 publisher descriptor와 endpoint 없는 subscriber 공개 표면을 구현하고 PS-D1~D6·PS-E1~E2를 package artifact로 검증 |
| 3 | C++ | `framework/languages/cpp/e2e/PubSub/` | `framework/languages/cpp/e2e/PubSub/feature-map.ko.md` | `framework/languages/cpp/e2e/PubSub/run_e2e.sh` | `deferred` | S9-FO-CPP: 기존 generic peer row를 전용 publisher descriptor 계약에 맞추고 PS-D1~D6·PS-E1~E2를 package artifact로 검증 |
| 3 | Java | `framework/languages/java/e2e/PubSub/` | `framework/languages/java/e2e/PubSub/feature-map.ko.md` | `framework/languages/java/e2e/PubSub/run_e2e.sh` | `deferred` | S9-FO-JVM: Java와 Kotlin의 기존 generic peer row를 전용 publisher descriptor 계약에 맞추고 두 lane에서 PS-D1~D6·PS-E1~E2를 package artifact로 검증 |
| 3 | Kotlin | `framework/languages/java/e2e-kotlin/PubSub/` | `framework/languages/java/e2e-kotlin/PubSub/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh` | `deferred` | S9-FO-JVM: Java와 공유하는 전용 publisher descriptor 구현을 사용하고 Kotlin 공개 DSL로 PS-D1~D6·PS-E1~E2를 package artifact로 검증 |
| 3 | Node.js | `framework/languages/node/e2e/PubSub/` | `framework/languages/node/e2e/PubSub/feature-map.ko.md` | `framework/languages/node/e2e/PubSub/run_e2e.sh` | `deferred` | S9-FO-NODE: endpoint 없는 subscriber와 전용 publisher descriptor를 연결하고 PS-D1~D6·PS-E1~E2를 package artifact로 검증 |
| 4 | .NET | `framework/languages/dotnet/e2e/RegistrationCodec/` | `framework/languages/dotnet/e2e/RegistrationCodec/feature-map.ko.md` | `framework/languages/dotnet/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S8: metadata·payload codec와 hostile ingress; package artifact 기반 source·marker 재검증 |
| 4 | C++ | `framework/languages/cpp/e2e/RegistrationCodec/` | `framework/languages/cpp/e2e/RegistrationCodec/feature-map.ko.md` | `framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9: metadata·payload codec와 hostile ingress; package artifact 기반 source·marker 재검증 |
| 4 | Java | `framework/languages/java/e2e/RegistrationCodec/` | `framework/languages/java/e2e/RegistrationCodec/feature-map.ko.md` | `framework/languages/java/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9: metadata·payload codec와 hostile ingress; package artifact 기반 source·marker 재검증 |
| 4 | Kotlin | `framework/languages/java/e2e-kotlin/RegistrationCodec/` | `framework/languages/java/e2e-kotlin/RegistrationCodec/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/RegistrationCodec/run_e2e.sh` | `deferred` | S9: metadata·payload codec와 hostile ingress; package artifact 기반 source·marker 재검증 |
| 4 | Node.js | `framework/languages/node/e2e/RegistrationCodec/` | `framework/languages/node/e2e/RegistrationCodec/feature-map.ko.md` | `framework/languages/node/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9: metadata·payload codec와 hostile ingress; package artifact 기반 source·marker 재검증 |
| 5 | .NET | `framework/languages/dotnet/e2e/ResilienceLifecycle/` | `framework/languages/dotnet/e2e/ResilienceLifecycle/feature-map.ko.md` | `framework/languages/dotnet/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S8: MeshNode drain·reconnect·shutdown; package artifact 기반 source·marker 재검증 |
| 5 | C++ | `framework/languages/cpp/e2e/ResilienceLifecycle/` | `framework/languages/cpp/e2e/ResilienceLifecycle/feature-map.ko.md` | `framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9: MeshNode drain·reconnect·shutdown; package artifact 기반 source·marker 재검증 |
| 5 | Java | `framework/languages/java/e2e/ResilienceLifecycle/` | `framework/languages/java/e2e/ResilienceLifecycle/feature-map.ko.md` | `framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9: MeshNode drain·reconnect·shutdown; package artifact 기반 source·marker 재검증 |
| 5 | Kotlin | `framework/languages/java/e2e-kotlin/ResilienceLifecycle/` | `framework/languages/java/e2e-kotlin/ResilienceLifecycle/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9: MeshNode drain·reconnect·shutdown; package artifact 기반 source·marker 재검증 |
| 5 | Node.js | `framework/languages/node/e2e/ResilienceLifecycle/` | `framework/languages/node/e2e/ResilienceLifecycle/feature-map.ko.md` | `framework/languages/node/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9: MeshNode drain·reconnect·shutdown; package artifact 기반 source·marker 재검증 |
| 6 | .NET | `framework/languages/dotnet/e2e/StoreFailure/` | `framework/languages/dotnet/e2e/StoreFailure/feature-map.ko.md` | `framework/languages/dotnet/e2e/StoreFailure/run_e2e.sh` | `deferred` | S8: MeshNode descriptor·owner lease와 Redis failure recovery; package artifact 기반 source·marker 재검증 |
| 6 | C++ | `framework/languages/cpp/e2e/DiscoveryRegistryHa/` | `framework/languages/cpp/e2e/DiscoveryRegistryHa/feature-map.ko.md` | `framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9: MeshNode descriptor·owner lease와 Redis failure recovery; package artifact 기반 source·marker 재검증 |
| 6 | Java | `framework/languages/java/e2e/StoreFailure/` | `framework/languages/java/e2e/StoreFailure/feature-map.ko.md` | `framework/languages/java/e2e/StoreFailure/run_e2e.sh` | `deferred` | S9: MeshNode descriptor·owner lease와 Redis failure recovery; package artifact 기반 source·marker 재검증 |
| 6 | Kotlin | `framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/` | `framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9: MeshNode descriptor·owner lease와 Redis failure recovery; package artifact 기반 source·marker 재검증 |
| 6 | Node.js | `framework/languages/node/e2e/DiscoveryRegistryHa/` | `framework/languages/node/e2e/DiscoveryRegistryHa/feature-map.ko.md` | `framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9: MeshNode descriptor·owner lease와 Redis failure recovery; package artifact 기반 source·marker 재검증 |
| 7 | .NET | `framework/languages/dotnet/e2e/RuntimeMonitoring/` | `framework/languages/dotnet/e2e/RuntimeMonitoring/feature-map.ko.md` | `framework/languages/dotnet/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S8: MeshNode·peer·channel readiness와 backpressure 관측; package artifact 기반 source·marker 재검증 |
| 7 | C++ | `framework/languages/cpp/e2e/RuntimeMonitoring/` | `framework/languages/cpp/e2e/RuntimeMonitoring/feature-map.ko.md` | `framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9: MeshNode·peer·channel readiness와 backpressure 관측; package artifact 기반 source·marker 재검증 |
| 7 | Java | `framework/languages/java/e2e/RuntimeMonitoring/` | `framework/languages/java/e2e/RuntimeMonitoring/feature-map.ko.md` | `framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9: MeshNode·peer·channel readiness와 backpressure 관측; package artifact 기반 source·marker 재검증 |
| 7 | Kotlin | `framework/languages/java/e2e-kotlin/RuntimeMonitoring/` | `framework/languages/java/e2e-kotlin/RuntimeMonitoring/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9: MeshNode·peer·channel readiness와 backpressure 관측; package artifact 기반 source·marker 재검증 |
| 7 | Node.js | `framework/languages/node/e2e/RuntimeMonitoring/` | `framework/languages/node/e2e/RuntimeMonitoring/feature-map.ko.md` | `framework/languages/node/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9: MeshNode·peer·channel readiness와 backpressure 관측; package artifact 기반 source·marker 재검증 |
| 8 | .NET | `framework/languages/dotnet/e2e/AutomaticTurnDispatch/` | `framework/languages/dotnet/e2e/AutomaticTurnDispatch/feature-map.ko.md` | `framework/languages/dotnet/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S8: turn 진행과 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 8 | C++ | `framework/languages/cpp/e2e/AutomaticTurnDispatch/` | `framework/languages/cpp/e2e/AutomaticTurnDispatch/feature-map.ko.md` | `framework/languages/cpp/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9: turn 진행과 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 8 | Java | `framework/languages/java/e2e/AutomaticTurnDispatch/` | `framework/languages/java/e2e/AutomaticTurnDispatch/feature-map.ko.md` | `framework/languages/java/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9: turn 진행과 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 8 | Kotlin | `framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/` | `framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9: turn 진행과 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 8 | Node.js | `framework/languages/node/e2e/AutomaticTurnDispatch/` | `framework/languages/node/e2e/AutomaticTurnDispatch/feature-map.ko.md` | `framework/languages/node/e2e/AutomaticTurnDispatch/run_e2e.sh` | `new` | S9: turn 진행과 route bridge 제거; package artifact 기반 source·marker 재검증 |
| 9 | .NET | `framework/languages/dotnet/e2e/ToActorMessaging/` | `framework/languages/dotnet/e2e/ToActorMessaging/feature-map.ko.md` | `framework/languages/dotnet/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S8: Actor owner direct route와 bound session; package artifact 기반 source·marker 재검증 |
| 9 | C++ | `framework/languages/cpp/e2e/ToActorMessaging/` | `framework/languages/cpp/e2e/ToActorMessaging/feature-map.ko.md` | `framework/languages/cpp/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9: Actor owner direct route와 bound session; package artifact 기반 source·marker 재검증 |
| 9 | Java | `framework/languages/java/e2e/ToActorMessaging/` | `framework/languages/java/e2e/ToActorMessaging/feature-map.ko.md` | `framework/languages/java/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9: Actor owner direct route와 bound session; package artifact 기반 source·marker 재검증 |
| 9 | Kotlin | `framework/languages/java/e2e-kotlin/ToActorMessaging/` | `framework/languages/java/e2e-kotlin/ToActorMessaging/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/ToActorMessaging/run_e2e.sh` | `deferred` | S9: Actor owner direct route와 bound session; package artifact 기반 source·marker 재검증 |
| 9 | Node.js | `framework/languages/node/e2e/ToActorMessaging/` | `framework/languages/node/e2e/ToActorMessaging/feature-map.ko.md` | `framework/languages/node/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9: Actor owner direct route와 bound session; package artifact 기반 source·marker 재검증 |
| 10 | .NET | `framework/languages/dotnet/e2e/SpotActorTransfer/` | `framework/languages/dotnet/e2e/SpotActorTransfer/feature-map.ko.md` | `framework/languages/dotnet/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S8: generation fence·handoff·FIFO와 MeshNode owner; package artifact 기반 source·marker 재검증 |
| 10 | C++ | `framework/languages/cpp/e2e/SpotActorTransfer/` | `framework/languages/cpp/e2e/SpotActorTransfer/feature-map.ko.md` | `framework/languages/cpp/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9: generation fence·handoff·FIFO와 MeshNode owner; package artifact 기반 source·marker 재검증 |
| 10 | Java | `framework/languages/java/e2e/SpotActorTransfer/` | `framework/languages/java/e2e/SpotActorTransfer/feature-map.ko.md` | `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9: generation fence·handoff·FIFO와 MeshNode owner; package artifact 기반 source·marker 재검증 |
| 10 | Kotlin | `framework/languages/java/e2e-kotlin/SpotActorTransfer/` | `framework/languages/java/e2e-kotlin/SpotActorTransfer/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/SpotActorTransfer/run_e2e.sh` | `new` | S9: generation fence·handoff·FIFO와 MeshNode owner; package artifact 기반 source·marker 재검증 |
| 10 | Node.js | `framework/languages/node/e2e/SpotActorTransfer/` | `framework/languages/node/e2e/SpotActorTransfer/feature-map.ko.md` | `framework/languages/node/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9: generation fence·handoff·FIFO와 MeshNode owner; package artifact 기반 source·marker 재검증 |
| 11 | .NET | `framework/languages/dotnet/e2e/ObservabilityOps/` | `framework/languages/dotnet/e2e/ObservabilityOps/feature-map.ko.md` | `framework/languages/dotnet/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S8: RouteMesh metric·flow·drain과 bounded label; package artifact 기반 source·marker 재검증 |
| 11 | C++ | `framework/languages/cpp/e2e/ObservabilityOps/` | `framework/languages/cpp/e2e/ObservabilityOps/feature-map.ko.md` | `framework/languages/cpp/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9: RouteMesh metric·flow·drain과 bounded label; package artifact 기반 source·marker 재검증 |
| 11 | Java | `framework/languages/java/e2e/ObservabilityOps/` | `framework/languages/java/e2e/ObservabilityOps/feature-map.ko.md` | `framework/languages/java/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9: RouteMesh metric·flow·drain과 bounded label; package artifact 기반 source·marker 재검증 |
| 11 | Kotlin | `framework/languages/java/e2e-kotlin/ObservabilityOps/` | `framework/languages/java/e2e-kotlin/ObservabilityOps/feature-map.ko.md` | `framework/languages/java/e2e-kotlin/ObservabilityOps/run_e2e.sh` | `deferred` | S9: RouteMesh metric·flow·drain과 bounded label; package artifact 기반 source·marker 재검증 |
| 11 | Node.js | `framework/languages/node/e2e/ObservabilityOps/` | `framework/languages/node/e2e/ObservabilityOps/feature-map.ko.md` | `framework/languages/node/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9: RouteMesh metric·flow·drain과 bounded label; package artifact 기반 source·marker 재검증 |

새로 만든 두 feature map은 목표 계약과 현재 runner를 분리해 기록한다.

- `framework/languages/node/e2e/AutomaticTurnDispatch/feature-map.ko.md`
- `framework/languages/java/e2e-kotlin/SpotActorTransfer/feature-map.ko.md`

나머지 53개 feature map도 10.0.0 목표 topology와 검증 의도를 설명한다. 기존 실행 marker는 source와
runner가 아직 이 계약에 도달했다는 증거로 사용하지 않는다. S8·S9에서는 실제 source·runner를 목표
계약에 맞춘 뒤 각 feature map의 scenario, marker와 실행 결과를 다시 대조한다.

## 12. 다섯 언어 sample lane 32개

언어별 README가 없는 sample은 새 문서를 임의로 만들지 않고 공통 owner를 사용한다. public source는
lane 아래 `Client/`, `Server/`, `Shared/`이며 runner 변경과 함께 S8·S9에서 전수 검사한다.

| 언어 | sample lane | 공통 public owner | runner | 분류 | stage·이유 |
|---|---|---|---|---|---|
| .NET | `framework/languages/dotnet/samples/Bingo/` | `framework/doc/framework/common/sample/bingo/README.ko.md` | `framework/languages/dotnet/samples/Bingo/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/DeliveryDispatch/` | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `framework/languages/dotnet/samples/DeliveryDispatch/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/GameQuest/` | `framework/doc/framework/common/sample/event/gamequest.ko.md` | `framework/languages/dotnet/samples/GameQuest/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/ShoppingMall/` | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `framework/languages/dotnet/samples/ShoppingMall/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/SupportChat/` | `framework/doc/framework/common/sample/supportchat/README.ko.md` | `framework/languages/dotnet/samples/SupportChat/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/TicTacToe/` | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | `framework/languages/dotnet/samples/TicTacToe/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| .NET | `framework/languages/dotnet/samples/ZoneWorld/` | `framework/doc/framework/common/sample/zoneworld/README.ko.md` | `framework/languages/dotnet/samples/ZoneWorld/run_sample.sh` | `deferred` | S8: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/Bingo/` | `framework/doc/framework/common/sample/bingo/README.ko.md` | `framework/languages/cpp/samples/Bingo/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/DeliveryDispatch/` | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/GameQuest/` | `framework/doc/framework/common/sample/event/gamequest.ko.md` | `framework/languages/cpp/samples/GameQuest/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/ShoppingMall/` | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `framework/languages/cpp/samples/ShoppingMall/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/SupportChat/` | `framework/doc/framework/common/sample/supportchat/README.ko.md` | `framework/languages/cpp/samples/SupportChat/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| C++ | `framework/languages/cpp/samples/TicTacToe/` | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | `framework/languages/cpp/samples/TicTacToe/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/Bingo/` | `framework/doc/framework/common/sample/bingo/README.ko.md` | `framework/languages/java/samples/java/Bingo/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/DeliveryDispatch/` | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `framework/languages/java/samples/java/DeliveryDispatch/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/GameQuest/` | `framework/doc/framework/common/sample/event/gamequest.ko.md` | `framework/languages/java/samples/java/GameQuest/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/ShoppingMall/` | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `framework/languages/java/samples/java/ShoppingMall/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/SupportChat/` | `framework/doc/framework/common/sample/supportchat/README.ko.md` | `framework/languages/java/samples/java/SupportChat/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Java | `framework/languages/java/samples/java/TicTacToe/` | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | `framework/languages/java/samples/java/TicTacToe/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/Bingo/` | `framework/doc/framework/common/sample/bingo/README.ko.md` | `framework/languages/java/samples/kotlin/Bingo/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/DeliveryDispatch/` | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `framework/languages/java/samples/kotlin/DeliveryDispatch/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/GameQuest/` | `framework/doc/framework/common/sample/event/gamequest.ko.md` | `framework/languages/java/samples/kotlin/GameQuest/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/ShoppingMall/` | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `framework/languages/java/samples/kotlin/ShoppingMall/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/SupportChat/` | `framework/doc/framework/common/sample/supportchat/README.ko.md` | `framework/languages/java/samples/kotlin/SupportChat/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Kotlin | `framework/languages/java/samples/kotlin/TicTacToe/` | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | `framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/Bingo.Ts/` | `framework/doc/framework/common/sample/bingo/README.ko.md` | `framework/languages/node/samples/Bingo.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/DeliveryDispatch.Ts/` | `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | `framework/languages/node/samples/DeliveryDispatch.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/GameQuest.Ts/` | `framework/doc/framework/common/sample/event/gamequest.ko.md` | `framework/languages/node/samples/GameQuest.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/ShoppingMall.Ts/` | `framework/doc/framework/common/sample/event/shoppingmall.ko.md` | `framework/languages/node/samples/ShoppingMall.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/SupportChat.Ts/` | `framework/doc/framework/common/sample/supportchat/README.ko.md` | `framework/languages/node/samples/SupportChat.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/TicTacToe.Ts/` | `framework/doc/framework/common/sample/tictactoe/README.ko.md` | `framework/languages/node/samples/TicTacToe.Ts/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |
| Node.js | `framework/languages/node/samples/ZoneWorld/` | `framework/doc/framework/common/sample/zoneworld/README.ko.md` | `framework/languages/node/samples/ZoneWorld/run_sample.sh` | `deferred` | S9: MeshNode/ChannelName 적용, 제거 API·기능별 endpoint·raw bindings helper no-hit, package 실행 |

### 12.1 언어별 sample 루트 안내 4개

sample lane별 문서와 별도로, 각 언어의 지원 목록·공통 실행 방법·10.0.0 topology를 설명하는 루트
안내도 public sample 문서다. 이 네 파일은 S3 문서 리뷰 범위에 포함하고, 실제 runner와 package
검증은 해당 구현 stage에서 수행한다.

| 언어 | 루트 안내 | 분류 | stage·검증 조건 |
|---|---|---|---|
| C++ | `framework/languages/cpp/samples/README.ko.md` | `changed` | S3: 6개 lane, MeshNode·ChannelName·location store·classic fanout 설명 검토. S9: package runner와 support matrix 대조 |
| .NET | `framework/languages/dotnet/samples/README.md` | `changed` | S3: 7개 lane과 10.0.0 topology 설명 검토. S8: package runner와 support matrix 대조 |
| Java·Kotlin | `framework/languages/java/samples/README.md` | `changed` | S3: Java 6개·Kotlin 6개 lane과 공통 topology 설명 검토. S9: 두 언어 package runner와 support matrix 대조 |
| Node.js | `framework/languages/node/samples/README.ko.md` | `changed` | S3: 7개 lane과 브라우저·Node client 경계 검토. S9: package runner와 support matrix 대조 |

## 13. runner 96개 파일별 적용 지도

이 절의 runner는 이 문서 단계에서 수정하지 않는다. 모두 실제 10.0.0 bindings package를 사용하는
S8·S9에서 변경하므로 `deferred`다. 각 runner는 최소한 resolved package 이름과 version, package 또는
native payload의 실제 경로, topology 준비 결과와 scenario marker, cleanup 뒤 최종 exit code를 출력한다.

| runner | 분류 | stage | 변경·검증 조건 |
|---|---|---|---|
| `framework/languages/cpp/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/PubSub/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/SpotService/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/e2e/run_e2e_all.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/cpp/samples/Bingo/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/DeliveryDispatch/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/GameQuest/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/ShoppingMall/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/SupportChat/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/TicTacToe/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/cpp/samples/run_samples.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/dotnet/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/LocationMessaging/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/PubSub/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/SpotService/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/StoreFailure/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/e2e/run_e2e_all.sh` | `deferred` | S8 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/dotnet/samples/Bingo/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/DeliveryDispatch/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/GameQuest/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/ShoppingMall/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/SupportChat/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/TicTacToe/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/ZoneWorld/run_sample.sh` | `deferred` | S8 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/dotnet/samples/run_samples.sh` | `deferred` | S8 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/ObservabilityOps/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/RegistrationCodec/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/RegistryMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/SpotActorTransfer/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/SpotService/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/ToActorMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e-kotlin/run_e2e_all.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/java/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/PubSub/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/SpotService/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/StoreFailure/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/e2e/run_e2e_all.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/java/samples/java/Bingo/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/java/DeliveryDispatch/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/java/GameQuest/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/java/ShoppingMall/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/java/SupportChat/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/java/TicTacToe/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/Bingo/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/DeliveryDispatch/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/GameQuest/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/ShoppingMall/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/SupportChat/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/kotlin/TicTacToe/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/java/samples/run_samples.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/node/e2e/AutomaticTurnDispatch/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/ObservabilityOps/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/PubSub/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/RegistrationCodec/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/RegistryMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/ResilienceLifecycle/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/RuntimeMonitoring/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/SpotActorTransfer/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/SpotService/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/ToActorMessaging/run_e2e.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/e2e/run_e2e_all.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |
| `framework/languages/node/samples/Bingo.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/DeliveryDispatch.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/GameQuest.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/ShoppingMall.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/SupportChat.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/TicTacToe.Ts/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/ZoneWorld/run_sample.sh` | `deferred` | S9 | 10.0.0 package name/version/path, topology 준비, result marker와 cleanup |
| `framework/languages/node/samples/run_samples.sh` | `deferred` | S9 | 지원 lane 전수 실행, fail-fast, package 요약과 최종 exit code |

현재 중앙 pin은 구현 전 버전이므로 runner가 이를 10.0.0으로 간주해서는 안 된다. version owner는 §6의
네 파일만 사용하고 runner에 version literal을 중복하지 않는다. clean package consumer는 C++ 기존
`framework/languages/cpp/tests/Zlink.Framework.PackageTests/install_consumer.cmake`와 S8·S9에서 만들거나
확정할 .NET·Java/Kotlin·Node.js 검증 진입점을 사용한다.

## 14. guide·internals 81개 파일별 적용 지도

S2에서는 대상과 검증 기준만 고정하며 guide·internals 본문은 수정하지 않는다. `deferred` 파일은 구현과
package가 green인 뒤 현재 동작으로 갱신한다. `unchanged` 파일도 link와 용어 no-hit 검사는 다시 실행한다.

| 파일 | 분류 | stage | 이유·검증 조건 |
|---|---|---|---|
| `framework/doc/framework/cpp/guide/01-overview.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/02-getting-started.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/03-concepts.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/04-di-container.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/cpp/guide/05-configuration.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/06-http-hosting.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/cpp/guide/07-channel-messaging.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/08-spot.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/09-actor-session.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/10-stream.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/11-registry.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/12-monitoring.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/13-interface-catalog.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/14-samples-map.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/15-feature-map.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/guide/16-grpc-alternative.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/cpp/guide/README.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/internals/backend-dependency-policy.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/internals/regression-test-matrix.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/cpp/internals/runtime-architecture.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/01-overview.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/02-getting-started.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/03-concepts.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/04-feature-map.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/05-channel-messaging.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/06-spot.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/07-actor-spot.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/08-actor-session.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/09-stream.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/10-location.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/11-monitoring.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/12-operations.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/guide/14-grpc-alternative.ko.md` | `unchanged` | S8 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/dotnet/internals/backend-dependency-policy.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/internals/runtime-execution.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/dotnet/internals/runtime-lifecycle.ko.md` | `deferred` | S8 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/01-overview.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/02-getting-started.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/03-concepts.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/04-channel-messaging.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/05-spot.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/06-actor-session.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/07-stream.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/08-registry.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/09-monitoring.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/10-feature-map.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/11-interface-catalog.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/guide/12-grpc-alternative.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/java/internals/backend-dependency-policy.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/internals/regression-test-matrix.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/java/internals/runtime-lifecycle.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/01-overview.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/02-getting-started.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/03-concepts.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/04-channel-messaging.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/05-spot.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/06-actor-session.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/07-stream.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/08-registry.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/09-monitoring.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/10-feature-map.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/11-interface-catalog.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/kotlin/guide/12-grpc-alternative.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/node/guide/01-overview.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/02-getting-started.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/03-concepts.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/04-channel-messaging.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/05-spot.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/06-actor-session.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/07-stream.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/08-registry.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/09-monitoring.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/10-feature-map.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/11-interface-catalog.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/guide/12-grpc-alternative.ko.md` | `unchanged` | S9 | MeshNode topology를 설명하지 않음; link·용어 회귀만 검사 |
| `framework/doc/framework/node/internals/actor-execution-serialization.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/internals/backend-dependency-policy.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/internals/regression-test-matrix.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |
| `framework/doc/framework/node/internals/runtime-lifecycle.ko.md` | `deferred` | S9 | 구현 green 뒤 MeshNode·ChannelName·Logical Multicast와 제거 surface no-hit 확인 |

Kotlin에는 현재 `framework/doc/framework/kotlin/internals/` 문서가 없다. 새 internals 문서 필요 여부는
Java runtime 공유 구조와 Kotlin 전용 DSL·coroutine 경계 중 별도 유지보수자 설명이 필요한지 S9 review에서
결정하며, 필요하면 `new` 파일로 별도 승인한다.

## 15. 오래된 topology와 raw helper 검사 기준

| 범주 | 제거·변경 대상 | 유지할 수 있는 대상 | 검증 조건 |
|---|---|---|---|
| node 이름 | Framework node 의미의 `SpotNode`, `SpotMesh` | 업무 고유 이름은 MeshNode 의미로 바꿀 수 있는지 별도 판단 | public 문서와 예제에서 이전 builder/type no-hit |
| routed path | `route bridge`, channel socket을 통한 Spot 우회 | MeshNode direct, ChannelName select-one, Spot direct | bridge helper/type와 자동 attach 설명 no-hit |
| endpoint | Spot 전용 ROUTER·PUB/SUB와 channel별 ROUTER endpoint | MeshNode ROUTER endpoint 하나, manual peer, classic PUB/SUB, HTTP·STREAM endpoint | 역할별 config와 runner port 수를 target topology와 비교 |
| raw helper | bindings socket/message를 업무 sample에서 직접 사용하는 helper | HTTP client의 `AsyncRaw`, `asyncRaw`, `submitRaw` 응답 API | bindings concrete type·raw frame helper scoped no-hit |

공통 E2E와 sample 문서는 목표 scenario를 설명할 수 있지만 아직 구현되지 않은 결과를 통과했다고 쓰지
않는다. 언어별 feature map의 기존 성공 marker는 현재 topology의 증거로 보존하고, S8·S9 재실행 뒤에만
10.0.0 완료 증거로 갱신한다.

## 16. code fixture와 package snapshot 연결

S2-28A의 실행 파일·명령·실패 조건은
[`S2 Framework 정식 계약 전환 inventory §7`](./s2-framework-contract-transition-inventory.ko.md#7-link와-contract-검증)가
소유한다. 이 문서의 E2E·sample code block은 그 fixture 입력에 포함한다.

| stage | 실행 owner | 실패 조건 |
|---|---|---|
| S8 .NET | `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj`, `framework/languages/dotnet/scripts/verify_packaged_contract.sh` | target signature 누락, 제거 surface 존재, NuGet export·example compile 차이 |
| S9 C++ | `framework/languages/cpp/scripts/verify_packaged_contract.sh` | header compile, package export, public example 또는 clean consumer 차이 |
| S9 Java/Kotlin | `framework/languages/java/scripts/verify_packaged_contract.sh` | Java/Kotlin compile fixture, package consumer, example signature 차이 |
| S9 Node.js | `framework/languages/node/scripts/verify_packaged_contract.sh` | TypeScript declaration compile, npm export, clean consumer와 public example 차이 |


## 17. 구현 추적 기록 처리

아래 파일은 구현 이력을 추적하기 위한 기록이며 10.0.0 current public contract의 owner가 아니다.
source·runner가 10.0.0으로 전환되기 전에는 내용을 새 topology의 완료 증거로 고쳐 쓰지 않는다.
S8·S9에서 계속 필요한 scenario 근거는 current feature map과 실행 로그로 옮기고, 중복된 기록은
문서 owner 원칙에 따라 삭제하거나 별도로 승인한 보관 위치로 이동한다.

| 파일 | 분류 | 후속 stage | 종료 조건 |
|---|---|---|---|
| `framework/languages/cpp/e2e/AutomaticTurnDispatch/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/cpp/e2e/SpotService/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/cpp/samples/ShoppingMall/sample-porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/cpp/samples/TicTacToe/sample-porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/e2e-kotlin/AutomaticTurnDispatch/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/e2e-kotlin/SpotService/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/e2e/SpotService/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/e2e/ToActorMessaging/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/samples/java/DeliveryDispatch/sample-porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/java/samples/kotlin/DeliveryDispatch/sample-porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/node/e2e/SpotService/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
| `framework/languages/node/e2e/ToActorMessaging/porting-inventory.ko.md` | `deferred` | S9 | scenario ID와 필요한 과거 evidence를 새 feature map에 보존하고 target API·runner green 뒤 삭제 또는 archive 승인 |
