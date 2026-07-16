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

1. 공통 E2E 시나리오와 언어별 실제 runner 디렉터리
2. 공통 sample 설명과 언어별 실제 sample 디렉터리
3. E2E·sample runner template과 일괄 실행 진입점
4. bindings package 버전의 언어별 단일 owner
5. 문서·runner·package 검증의 완료 증거

정식 계약의 owner와 언어별 exact public interface는
[`S2 Framework 정식 계약 전환 inventory`](./s2-framework-contract-transition-inventory.ko.md)가
소유한다. 이 문서는 같은 계약을 다시 정의하지 않고, 검증 대상과 필요한 결과만 연결한다.

## 2. 파일 상태와 충돌 처리

상태는 2026-07-16 live checkout의 `git status --short`를 기준으로 한다.

| 표시 | 의미 | S2 처리 |
|---|---|---|
| `clean` | index와 worktree에 변경이 없음 | 필요한 절만 수정하고 실행 증거를 남긴다 |
| `M` | worktree에만 변경이 있음 | 현재 diff의 목적을 확인하고 기존 변경을 보존한다 |
| `MM` | index와 worktree에 모두 변경이 있음 | staged·unstaged diff를 각각 읽고 합쳐 쓰지 않는다 |
| `new` | 새 파일 또는 새 검증 진입점 | owner, 호출 위치와 실패 조건을 함께 기록한다 |

### 2.1 현재 충돌 위험 파일

| 파일 | 상태 | S2 관련 범위 | 주의사항 |
|---|---:|---|---|
| `framework/doc/framework/common/sample/README.ko.md` | `M` | sample 공통 목적과 언어별 지원 범위 | 현재 sample inventory 변경을 먼저 읽고 필요한 절만 수정한다 |
| `framework/doc/framework/common/sample/event/gamequest.ko.md` | `MM` | GameQuest topology와 runner 설명 | staged·unstaged 예제와 marker를 모두 보존한다 |
| `scripts/local-package/README.ko.md` | `M` | package owner와 local package 검증 | 현재 package 정책 변경과 10.0.0 pin을 한 의미로 합의한 뒤 수정한다 |

공통 E2E 문서와 E2E runner는 현재 `clean`이다. 이 상태는 계약 반영이 끝났다는 뜻이 아니라,
수정 전 충돌 위험이 발견되지 않았다는 뜻이다.

## 3. 공통 E2E와 실제 실행 lane

### 3.1 공통 owner

`framework/doc/framework/common/e2e/README.ko.md`는 Config 1~11의 목적, 공통 환경 조건,
언어별 실행 진입점과 전체 성공 조건을 연결한다. 개별 Config의 상세 계약은 아래 파일이 소유한다.
언어별 `feature-map.ko.md`는 공통 시나리오 ID가 해당 runner에서 어떤 marker와 assertion으로
검증되는지 기록한다.

| S2 ID | Config·공통 owner | 필요한 10.0.0 검증 변경 | 실제 언어별 lane | 실행·증거 | 상태·위험 |
|---|---|---|---|---|---|
| S2-20, S2-29 | Config 1 `config-1-location-messaging.ko.md` | MeshName·ChannelName 구성, direct RID, select-one, weighted round-robin, no-member, descriptor와 manual peer validation | .NET `LocationMessaging`; C++·Java·Kotlin·Node.js `RegistryMessaging` | 각 lane `run_e2e.sh`, `feature-map.ko.md`, selection 분포와 실패 marker | `clean`; topology spec `MM`과 의미를 먼저 맞춘다 |
| S2-21, S2-29 | Config 2 `config-2-spot-service.ko.md` | MeshNode 기반 Spot direct call, node-local match, Logical Multicast, NoDrop, timer, Actor·STREAM owner와 bridge 부재 | 모든 언어 `SpotService` | publish 대상 node 수, local subscription 수, complete message ref 공유와 session marker | `clean`; multicast를 classic fanout으로 검증하지 않는다 |
| S2-22, S2-29 | Config 3 `config-3-pubsub.ko.md` | classic fanout이 RouteMesh Logical Multicast와 독립된 공개 기능임을 검증 | 모든 언어 `PubSub` | publisher/subscriber topology, topic fanout, independent socket path marker | `clean`; Config 2와 합치지 않는다 |
| S2-19, S2-29 | Config 4 `config-4-registration-codec.ko.md` | canonical metadata codec, byte 상한, hostile ingress rejection, immutable handler snapshot과 typed message codec 유지 | 모든 언어 `RegistrationCodec` | 공통 byte corpus, invalid frame, metadata와 typed payload assertion | `clean`; metadata와 payload codec 책임을 혼합하지 않는다 |
| S2-23, S2-29 | Config 5 `config-5-resilience-lifecycle.ko.md` | drain 이후 새 submit 거부, sealed work 완료, cancellation, late reply, full-mesh peer 재연결과 종료 순서 | 모든 언어 `ResilienceLifecycle` | lifecycle marker 순서와 terminal result, process exit code | `clean`; sleep 기반 통과를 증거로 사용하지 않는다 |
| S2-23A, S2-29 | Config 6 `config-6-store-failure-recovery.ko.md` | 분산 기능의 Redis 필수 조건, 명시 등록, descriptor와 location row 분리, startup failure, manual admission 예외 | .NET·Java `StoreFailure`; C++·Kotlin·Node.js `DiscoveryRegistryHa` | dependency failure·recovery marker, Redis key 확인과 startup exit code | `clean`; lane 이름 차이를 기능 차이로 해석하지 않는다 |
| S2-23, S2-29 | Config 7 `config-7-monitoring.ko.md` | MeshNode·peer·channel readiness, Logical Multicast backpressure·drop과 runtime health | 모든 언어 `RuntimeMonitoring` | monitor event, bounded label, readiness transition과 counter assertion | `clean`; 공개 monitor API만 사용한다 |
| S2-19, S2-29 | Config 8 `config-8-execution-turn.ko.md` | application turn과 infrastructure pump 분리, completion·send-ready 독립 진행, timer generation과 shutdown freeze | 모든 언어 `AutomaticTurnDispatch` | turn ordering, completion, timer와 shutdown marker | runner는 `clean`; Node.js `feature-map.ko.md`가 없어 추가해야 한다 |
| S2-23A, S2-29 | Config 9 `config-9-to-actor-messaging.ko.md` | Actor owner direct messaging, stale·disconnected·no-bind 결과, bound session dispatch와 one-shot reply | 모든 언어 `ToActorMessaging` | Actor owner·generation, session bind와 failure result assertion | `clean`; Actor payload를 Spot callback으로 전달하지 않는다 |
| S2-23, S2-29 | Config 10 `config-10-spot-actor-transfer.ko.md` | participant-set CAS, token·lease, transfer state, failure point recovery, stale fence와 session FIFO | 모든 언어 `SpotActorTransfer` | 각 failure point의 durable state, successor recovery와 callback ordering | runner는 `clean`; Kotlin `feature-map.ko.md`가 없고 `23-spot-actor.ko.md`는 `MM`이다 |
| S2-23, S2-29 | Config 11 `config-11-observability-ops.ko.md` | RouteMesh metric·flow·drain, multicast correlation과 bounded label | 모든 언어 `ObservabilityOps` | metric catalog, flow tree, drain snapshot과 label cardinality assertion | `clean`; 문자열 marker만으로 완료 판정하지 않는다 |

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
| S2-24/25/30 | `common/sample/README.ko.md` | RouteMesh, MeshNode, ChannelName, Logical Multicast와 classic fanout을 sample별 목적에 연결하고 지원 언어를 실제 디렉터리와 맞춘다 | 아래 일곱 sample 전체 | 언어별 `run_samples.sh`, 상대 link와 디렉터리 inventory | `M`; 기존 inventory 변경 보존 |
| S2-24/25/30 | `common/sample/bingo/README.ko.md` | direct RID·channel select-one·Spot·Actor 중 sample이 사용하는 공개 경로를 명시한다 | 다섯 언어 `Bingo` | `run_sample.sh`, 라운드 결과와 종료 marker | `clean` |
| S2-24/25/30 | `common/sample/deliverydispatch/README.ko.md` | weighted round-robin, no-member와 graceful drain을 업무 흐름에 연결한다 | 다섯 언어 `DeliveryDispatch` | dispatch 분포, drain과 terminal result | `clean` |
| S2-24/25/30 | `common/sample/event/README.ko.md` | event sample이 사용하는 ChannelName, Spot Logical Multicast와 classic fanout의 경계를 설명한다 | `GameQuest`, `ShoppingMall` | 두 sample runner와 topic·target assertion | `clean` |
| S2-24/25/30 | `common/sample/event/gamequest.ko.md` | MeshNode 구성, node-local Spot match와 NoDrop publish를 실제 코드와 맞춘다 | 다섯 언어 `GameQuest` | quest publish 대상과 local delivery count, exit code | `MM`; staged·unstaged 예제 보존 |
| S2-24/25/30 | `common/sample/event/shoppingmall.ko.md` | channel service와 fanout event를 별도 interaction으로 설명한다 | 다섯 언어 `ShoppingMall` | order selection과 event fanout assertion | `clean` |
| S2-24/25/30 | `common/sample/languages/README.ko.md` | 다섯 언어의 sample 이름, 실행 명령과 package 준비 절차를 실제 경로와 맞춘다 | 언어별 sample root | command existence, relative link와 package pin 검사 | `clean` |
| S2-24/25/30 | `common/sample/supportchat/README.ko.md` | Actor owner, Spot membership, bound session과 transfer 시나리오를 공개 API로 표현한다 | 다섯 언어 `SupportChat` | conversation lifecycle, session push와 transfer marker | `clean` |
| S2-24/25/30 | `common/sample/tictactoe/README.ko.md` | Actor turn, request completion, stale fence와 종료 경계를 설명한다 | 다섯 언어 `TicTacToe` | ordered move, winner와 terminal result | `clean` |
| S2-24/25/30 | `common/sample/tictactoe/implementation-prompt.ko.md` | 생성 지침의 API 이름과 금지 surface를 정식 interface와 맞춘다 | 다섯 언어 `TicTacToe` | generated-code scoped no-hit와 build | `clean`; 현재 계약 밖 helper 생성 금지 |
| S2-24/25/30 | `common/sample/zoneworld/README.ko.md` | zone Spot, Logical Multicast, STREAM session과 Actor owner의 관계를 설명한다 | .NET `ZoneWorld`; Node.js `ZoneWorld` | 두 runner와 기능 차이의 명시적 지원 표 | `clean`; 없는 언어의 구현을 있다고 표시하지 않는다 |

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
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/redis-common.template.sh` | Redis readiness, namespace 격리, 정리와 failure reporting의 공통 계약 | Config 1·6·10에서 template 사용 여부와 실패 exit code 확인 | `clean` |
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/run_e2e.template.sh` | package pin 출력, timeout, child cleanup, marker와 실패 보존 | 다섯 언어 실제 runner와 구조 비교, shell syntax 검사 | `clean` |
| S2-26/31 | `framework/doc/framework/common/e2e/runner-templates/run_e2e_all.template.sh` | Config 1~11 순서, fail-fast와 최종 summary | 다섯 `run_e2e_all.sh`의 실행 집합 비교 | `clean` |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/redis-common.template.sh` | sample Redis 준비·격리·정리 | Redis 사용 sample runner와 구조 비교 | `clean` |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/run_sample.template.sh` | package pin 출력, timeout, cleanup와 업무 결과 marker | 모든 `run_sample.sh`의 필수 단계 검사 | `clean` |
| S2-26/31 | `framework/doc/framework/common/sample/runner-templates/run_samples.template.sh` | 지원 sample 전수 실행과 실패 보존 | 다섯 `run_samples.sh`와 §4 inventory 집합 비교 | `clean` |

template은 그대로 복사할 코드 조각이 아니라 runner의 공통 안전 조건을 소유한다. 언어별 도구 차이는
runner가 처리하되, package pin·timeout·cleanup·exit code·marker 보장은 빠지면 안 된다.

## 6. Bindings package 버전 owner

Framework는 bindings source를 직접 참조하지 않고 10.0.0 package artifact를 사용한다. 다음 파일만
언어별 version pin을 소유한다.

| S2 ID | 언어 | 단일 owner와 key | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-26 | .NET | `framework/languages/dotnet/Directory.Packages.props`의 `ZLinkBindingsPackageVersion` | restore graph, package lock과 E2E 출력의 resolved version 비교 | `clean` |
| S2-26 | Java·Kotlin | `framework/languages/java/gradle/libs.versions.toml`의 `zlinkBindings` | Gradle dependency graph와 두 언어 runner의 resolved module 비교 | `clean`; 두 언어가 같은 owner를 사용한다 |
| S2-26 | Node.js | `framework/languages/node/package.json`의 `@zlink-systems/zlink` dependency | lockfile, 설치 결과와 package tarball 내용 비교 | `clean` |
| S2-26 | C++ | `framework/languages/cpp/CMakeLists.txt`의 `ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION` | configure 출력과 `tests/Zlink.Framework.PackageTests/install_consumer.cmake` consumer 확인 | `clean` |
| S2-26 | 공통 local package 정책 | `scripts/local-package/README.ko.md` | 언어별 중앙 pin, package 생성 위치, WSL 경로와 실제 script 비교 | `M`; 기존 정책 변경 보존 |

package 검증은 source tree의 header나 assembly를 우연히 참조한 성공을 인정하지 않는다. runner는
resolved artifact의 이름, version과 실제 경로를 로그에 출력하고, package consumer 검증을 통과해야 한다.

## 7. S2 산출물과 검증 owner

| S2 ID | 산출물 owner | 필요한 결과 | 검증 | 상태·위험 |
|---|---|---|---|---|
| S2-19 | 공통 E2E Config 1~11과 언어별 `feature-map.ko.md` | 모든 scenario의 영향·비영향·신규 검증 분류 | scenario ID exact-once, 다섯 언어 lane 존재와 runner source 대응 | Node.js Config 8과 Kotlin Config 10의 `feature-map.ko.md`가 없음 |
| S2-20~23A | §3의 Config별 owner와 실제 lane | LocationMessaging, SpotService, PubSub, lifecycle·transfer·monitoring, Actor route와 store failure 영향 고정 | 관련 Config의 normal·boundary·failure 행과 언어별 marker 대응 | 공통 문서와 runner는 `clean`; 누락 feature map 두 개는 별도 gap |
| S2-24 | 공통 sample 11개 문서와 실제 sample 디렉터리 | 변경·삭제·신규·비영향과 지원 언어 분류 | 문서 집합, 실제 디렉터리와 support matrix 비교 | README `M`, GameQuest `MM` |
| S2-25 | 공통 sample 예제와 실제 source | 제거 API, endpoint 배선, raw helper와 내부 API 사용처 전수 기록 | 금지 surface scoped no-hit, package build와 scenario 실행 | source별 검사가 필요함 |
| S2-26 | §5·§6의 runner와 package owner | local package, clean consumer와 E2E·sample runner 변경점 기록 | dependency graph, artifact 내용, consumer와 runner smoke | policy 문서 `M` |
| S2-27 | framework 언어별 guide·internals root와 이 문서 | 구현 후 수정할 문서의 독자, 질문, 정본과 검증 기준 mapping | `framework/doc/framework/{dotnet,cpp,java,kotlin,node}/` 실제 파일 inventory와 owner 중복 검사 | 별도 경로별 mapping이 필요함 |
| S2-27A | 정식 spec·guide·internals와 `framework/doc/plan/v10.0/` | current-state 문서와 실행 blueprint의 성격 분리 | 계획 표현의 정식 문서 유입 scoped no-hit와 독자·질문 검사 | S2 review에서 독립 검증 |
| S2-28 | S3 review manifest의 검증 명령 | S2 정식 spec link, anchor, 금지 surface와 .NET machine inventory 검증 | 같은 frozen scope에서 같은 결과와 nonzero failure 재현 | S3 iteration마다 기록 |
| S2-28A | 언어별 doc compile fixture와 package API snapshot 계획 | 코드 예제와 exact signature를 실제 artifact에 검증할 파일·명령·실패 조건 | .NET fixture는 S8, C++·Java·Kotlin·Node.js fixture는 S9의 구현 전 exact interface gate에 연결 | S2에서는 설계만 고정 |
| S2-29 | 공통 E2E Config 1~11 | 10.0.0 topology, 입력, 관찰 결과와 failure scenario의 변경·비변경·신규 검증 분류 | common 문서와 언어별 feature map·runner의 영향 파일 대응 | §3 inventory 완료 |
| S2-30 | 공통 sample 문서 | target API 예제, Redis extension, 지정된 manual topology와 runner의 변경·비변경 분류 | support matrix, code example fixture와 runner 영향 파일 대응 | §4 inventory 완료 |
| S2-31 | §5의 runner template과 실제 runner | topology setup, package 입력과 result marker 변경점을 파일별 고정 | template diff, shell syntax와 실패 주입 | 현재 파일은 `clean` |

## 8. 충돌 방지와 검증 순서

1. `M`·`MM` 파일은 staged·unstaged diff를 각각 읽고 기존 목적과 S2 변경을 대조한다.
2. 정식 spec과 언어별 exact interface가 확정되기 전에 E2E나 sample이 새 public API를 정의하지 않는다.
3. common E2E는 검증 요구를 소유하지만, public contract를 추가하는 근거로 단독 사용하지 않는다.
4. sample과 runner의 helper로 framework나 bindings의 공개 기능 누락을 우회하지 않는다.
5. package pin을 중앙 owner 이외의 project 파일이나 runner에 중복 기록하지 않는다.
6. Config 1~11, sample 지원 표와 실제 directory 집합을 script로 비교한다.
7. 문서 link·anchor·금지 surface·code fixture 검증 후 package consumer와 runner를 실행한다.
8. 실제 적용과 실행 로그는 .NET S8, C++·Java·Kotlin·Node.js S9의 진행표에 연결한다.

## 9. S2 완료 점검

- [ ] Config 1~11의 공통 문서, 언어별 lane과 `feature-map.ko.md` 영향이 파일 단위로 대응한다.
- [ ] 각 Config의 normal, boundary와 failure scenario에 필요한 변경·비변경·신규 검증이 분류되어 있다.
- [ ] 공통 sample 문서와 실제 언어별 sample 지원 집합의 적용 사항이 분류되어 있다.
- [ ] sample public API, topology와 Redis 등록 변경을 검증할 source·runner가 식별되어 있다.
- [ ] E2E·sample runner의 package pin, timeout, cleanup, exit code와 marker 적용 사항이 식별되어 있다.
- [ ] 언어별 10.0.0 bindings package 중앙 owner와 구현 stage 검증 명령이 식별되어 있다.
- [ ] doc contract와 언어별 code fixture를 실행할 stage, 파일과 실패 조건이 고정되어 있다.
- [ ] `M`·`MM` 파일의 기존 변경을 보존했다.
- [ ] Codex와 Claude Fable review에서 중요한 미해결 finding이 없다.
