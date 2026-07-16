# RouteMesh 10.0.0 S0 범위·기준선 기록

## 0. 문서 상태와 목적

이 문서는 RouteMesh 10.0.0 작업을 시작하기 전에 Core와 framework 정식 spec의 변경 범위, 삭제 검사,
리뷰 입력과 현재 checkout 기준선을 고정하는 실행 기록이다. 현재 공개 계약을 설명하는 spec이 아니라 S0 단계의
검증 증거다.

대상 독자는 S1~S3의 문서 작업을 수행하고 다음 단계 진입을 승인하는 개발자와 reviewer다. 이 문서는
“어떤 문서를 어떤 기준으로 변경하고, 현재 상태와 비교할 증거를 어디에서 확인하는가?”에 답한다.
작성과 검토에는
[`기술문서 작성 원칙`](../../../../doc/principal/documentation/documentation-principles.ko.md)을 적용한다.

## 1. 기준 revision과 공개 계약 원본

| 항목 | 값 |
|---|---|
| 기준 commit | `cbc1b693d75b8944a9624373e04fae1b748146fd` |
| 적용 계약 version | `10.0.0` |
| 공개 C 계약 원본 | `core/include/zlink.h`와 이 header가 include하는 `core/include/zlink/**/*.h` |
| 현재 checkout ABI 입력 | `core/build/lib/libzlink.so` |
| framework 계약 원본 | `framework/doc/framework/spec/` |
| 계획 원본 | `framework/doc/plan/v10.0/`의 세 설계 문서와 실행 진행표 |

기준 commit의 working tree에는 이 작업과 무관한 변경이 있으므로 S1~S3 review manifest는 commit만 적지
않고 review 대상 파일의 SHA-256과 전체 working tree diff 범위를 함께 기록한다. reviewer는 manifest에
기록되지 않은 source와 문서를 수정하지 않는다.

## 2. 결정 상태

다음 결정 집합은 모두 확정 상태다. S1과 S2에서는 방향을 다시 선택하지 않고 exact signature, result,
ownership과 lifecycle 계약으로 옮긴다.

| 결정 집합 | 원본 | 상태 |
|---|---|---|
| D-01~D-27 | `framework-route-mesh-messaging-consolidation.ko.md` §21 | 확정 |
| MN-D01~MN-D24 | `mesh-node-core-api-review.ko.md` §14 | 확정 |
| FD-01~FD-39 | `mesh-node-framework-dispatch-design.ko.md` §16 | 확정 |

2026-07-16 결정에서 location store는 D-27과 FD-39의 추천 방향을 그대로 채택했다. 분산 location
runtime은 공식 Redis extension을 production 기본 구현으로 사용하고 application이 package와 연결 설정을
명시적으로 등록한다. 자동 discovery, 분산 Spot·Actor 주소 조회 또는 분산 Actor transfer authority를
사용하면서 store를 등록하지 않으면 startup에서 실패한다. framework는 production 구성에 process-local
in-memory store를 자동으로 추가하지 않으며, in-memory 구현은 process 하나에서 실행하는 contract test
지원으로만 제공한다.

Actor transfer의 권한 결정과 durable 상태는 location store가 소유한다. Core는 framework와 같은 process의
신뢰 경계 안에서 prepare 시 64-byte sealed token을 발급하고, commit 시 이 token과 정확히 다음 membership
epoch를 검증한다. application이 만든 임의의 authority bytes나 외부 검증 callback은 Core 공개 계약에
포함하지 않는다.

별도 선택이 필요했던 나머지 항목도 세 계획 문서의 추천 방향을 채택했다. 따라서 S0에는 선택하지 않은
정책이 남아 있지 않다. 계획 문서에 남은 `후보` 표기는 정책 미결정을 뜻하지 않는다. S1에서 공개 C
함수의 정확한 signature, 구조체 크기, result와 ownership을 Core 정식 spec으로 옮기기 전의 이름
표식이다.

S1·S2 문서에서 위 결정과 다른 계약이 필요해지면 구현으로 우회하지 않는다. S0을 다시 열고 decision
record, 영향 범위와 red gate를 함께 수정한다.

## 3. Core 정식 spec 위치와 구현 일치 지도

이번 작업은 service 계약 전체를 10.0.0으로 고정한다. `AGENTS.md`의 RouteMesh 10.0.0
예외에 따라 공개 계약을 다음 정식 spec에 먼저 기록한다.

- `core/doc/spec/core/service/mesh-node.ko.md`와 영문 문서
- `core/doc/spec/core/service/dispatch.ko.md`와 영문 문서
- `core/doc/spec/core/service/spot.ko.md`와 영문 문서
- `core/doc/spec/core/service/actor.ko.md`와 영문 문서
- `core/doc/spec/core/service/stream-session.ko.md`와 영문 문서
- `core/doc/spec/core/socket/router.ko.md`와 영문 문서
- `core/doc/spec/core/socket/stream.ko.md`와 영문 문서
- `core/doc/spec/core/polling.ko.md`와 영문 문서
- `core/doc/spec/core/monitoring.ko.md`와 영문 문서
- `core/doc/spec/core/errno-map.ko.md`, `errors.ko.md`와 영문 문서
- `core/doc/spec/core/00-public-contract-governance.ko.md`와 영문 문서

각 정식 문서는 Core 10.0.0의 현재 공개 계약만 설명한다. 현재 checkout 구현과의 차이는 정식 문서가 아닌
[`Core 구현 일치 추적`](./s1-core-implementation-tracking.ko.md)에 기록한다.

| 계약 책임 | 정식 owner 문서 |
|---|---|
| MeshNode lifecycle, identity, membership와 peer admission | `core/doc/spec/core/service/mesh-node.ko.md`와 영문 문서, service index |
| ready callback, claim, batch, operation ID와 reply token | `core/doc/spec/core/service/dispatch.ko.md`와 영문 문서 |
| Spot lifecycle, Logical Multicast와 local subscription | `core/doc/spec/core/service/spot.ko.md`와 영문 문서 |
| Actor mailbox, ActorRef와 transfer | `core/doc/spec/core/service/actor.ko.md`와 영문 문서 |
| Actor와 STREAM session 결합 | `core/doc/spec/core/service/stream-session.ko.md`와 영문 문서 |
| generic STREAM 비변경 경계 | `core/doc/spec/core/socket/stream.ko.md`와 영문 문서 |
| ROUTER 공유와 raw ROUTER 비변경 경계 | `core/doc/spec/core/socket/router.ko.md`와 영문 문서 |
| poller에서 MeshNode handle을 사용하는 의미 | `core/doc/spec/core/polling.ko.md`와 영문 문서 |
| status, source와 monitor event | `core/doc/spec/core/monitoring.ko.md`와 영문 문서 |
| 신규 result와 errno | `core/doc/spec/core/errno-map.ko.md`, `errors.ko.md`와 영문 문서 |
| version과 ABI | service index와 `errors` |
| 공개 계약 governance | `core/doc/spec/core/00-public-contract-governance.ko.md`와 영문 문서 |
| 현재 checkout 구현 차이 | 임시 `s1-core-implementation-tracking.ko.md` |

S4 구현 완료 조건은 다음과 같다.

1. `core/include/zlink.h`를 통해 새 symbol, type, enum과 macro가 공개된다.
2. 모든 result, ownership, callback, timeout과 close 계약에 red/green contract test가 있다.
3. 설치 header와 실제 shared library export가 정식 spec의 exact 목록과 일치한다.
4. 한국어·영문 정식 spec이 같은 heading, signature와 의미를 가진다.
5. 임시 구현 일치 추적 문서의 모든 항목을 완료 증거와 함께 닫는다.

## 4. Framework spec 변경 지도

### 4.1 공통 계약

| 책임 | 변경할 정식 spec |
|---|---|
| public contract 절차 | `framework/doc/framework/spec/00-public-contract-governance.ko.md` |
| 개요와 interaction | `01-overview.ko.md`, `02-interaction-model.ko.md`, `05-framework-api.ko.md` |
| topology와 messaging | `server/10-channel-topology.ko.md`, `server/11-channel-messaging.ko.md` |
| MeshNode와 Spot | `server/21-mesh-node.ko.md`를 MeshNode 계약으로 재구성, `server/20-spot-messaging.ko.md` |
| Actor와 Spot 주소 | `server/22-actor-model.ko.md`, `23-spot-actor.ko.md`, `24-spot-address-messaging.ko.md` |
| STREAM session | `server/30-stream-session.ko.md`, `31-session-actor-dispatch.ko.md` |
| location과 Redis | `server/40-location-runtime.ko.md`, `41-location-store-redis.ko.md` |
| 운영·관측 | `server/50-runtime-monitoring.ko.md`~`54-graceful-drain-handoff.ko.md` |
| 구현·적용 추적 | 임시 `framework/doc/plan/v10.0/s2-framework-contract-transition-inventory.ko.md` |

### 4.2 언어별 공개 interface

`.NET`, C++, Java, Kotlin과 Node.js의 exact public interface는
`framework/doc/framework/spec/server/languages/<lang>/`에서 고정한다. `.NET`은
`02-handler-interfaces.ko.md`를 기준 투영으로 사용하고 다른 언어는 같은 기능을 각 언어의 관용적인
형태로 표현한다. 어떤 언어도 raw frame, bindings internal API 또는 임시 helper로 계약 차이를 메우지
않는다.

## 5. Inventory 추출 기준

### 5.1 Core API

S1 inventory는 다음 집합을 각각 추출하고 모든 항목을 유지, 이름 변경, 제거 또는 신규 대체 가운데
하나로 정확히 한 번 분류한다.

- `ZLINK_EXPORT`가 붙은 공개 함수
- public callback typedef
- enum type과 모든 enumerator
- struct type과 공개 field
- `ZLINK_` public macro
- 현재 checkout shared library의 `zlink_*` export

공개 header 선언에 없지만 shared library에 export된 symbol은 public API로 승격하지 않는다. S1 exported
symbol appendix에서 internal export로 따로 분류하고 10.0.0 export 정책을 결정한다.

### 5.2 E2E, sample과 package

| 범위 | 추출 기준 |
|---|---|
| 공통 E2E | `framework/doc/framework/common/e2e/`의 README와 config 문서 전부 |
| 언어별 E2E | `.NET`, C++, Java, Kotlin, Node.js의 config별 source, feature map과 `run_e2e*.sh` |
| 공통 sample | `framework/doc/framework/common/sample/`의 README와 sample 문서 전부 |
| 언어별 sample | 다섯 framework 언어의 source, README와 `run_sample*`/`run_samples*` |
| package consumer | bindings와 framework의 clean consumer, API snapshot과 native payload 검사 |
| 증거 형식 | 파일, scenario ID, 변경/삭제/신규/비영향 분류, 필요한 public API, runner 변경과 검증 명령 |

2026-07-16 기준 공통 inventory 입력은 E2E 문서 12개, sample 문서 11개다. S2 inventory는 문서만 세지
않고 각 문서의 scenario와 실제 언어별 runner를 대응시킨다.

## 6. 삭제 및 금지 구현 검색 목록

다음 문자열은 S1~S3의 검색 manifest에 포함한다. 임시 계획과 review log는 검색 범위에서 제외하고
정식 current-state spec, source, generated binding, sample와 package snapshot에서 검사한다.

| 분류 | 검색 문자열 또는 symbol family |
|---|---|
| 이전 node 이름 | `SpotNode`, `spot_node`, `zlink_spot_node_*` |
| mode와 PUB/SUB plane | `ZLINK_SPOT_NODE_MODE_*`, `mesh_pub`, `mesh_xsub`, `set_pub_bind`, `set_pub_routing_id`, `set_sub_routing_id` |
| bridge | `zlink_spot_route_bridge_*`, `RouteBridge` |
| part 단위 service API | Spot·Actor·Actor–STREAM의 `*_part` send/request/reply/recv |
| 이전 dispatch | `zlink_spot_dispatch_event_handler`, `SpotDispatchHandler`, Core dispatch worker option |
| remote subject | `zlink_spot_node_subjects`, remote subscription control protocol |
| 이전 completion | channel dealer event, per-request service callback와 전용 reply drain |
| framework topology | `AddClientServerChannel`, `AddRouteMeshChannel`, `AddSpotMesh`, `ConnectRouter`, `ConnectPeerPub` |
| production in-memory location | `UseInMemoryLocationStores`와 자동 fallback 등록 |
| 우회 구현 | reflection non-public access, raw frame adapter, test-only topology helper, handler별 codec 등록 |

`zlink_socket_set_channel_name`과 `zlink_socket_get_channel_name`은 모든 raw socket에 적용되는 metadata API이므로
삭제 목록에 넣지 않는다.

## 7. Review manifest와 종료 문구

S3 review는 [`log/templates/manifest.ko.md`](./log/templates/manifest.ko.md)를 복사해 iteration별 입력을
고정한다. R1과 R2는 같은 file hash와 diff를 읽고 서로의 첫 결과를 보지 않는다.

| 리뷰어 | 종료 문구 |
|---|---|
| Codex agent | `DOC REVIEW CLEAN` |
| Claude Fable | `DOC REVIEW CLEAN` |

결과에 provider/model, invocation ID, 시작·종료 시각, exit status, 대상 SHA-256과 raw output checksum이
없으면 clean 판정을 인정하지 않는다.

## 8. 현재 checkout 기준선

### 8.1 재현 환경

| 항목 | 값 |
|---|---|
| OS | Linux 6.6.87.2-microsoft-standard-WSL2 |
| CPU | Intel Core Ultra 7 265K, 20 cores |
| Core runtime | `core/build/lib/libzlink.so` |
| Runtime SHA-256 | `ba55783a1971e3d28ebf6f3b367cfae289bdadc3d3e8fd52275b7833acc7e754` |
| Build freshness | 최신 `core/src`보다 runtime mtime이 19.56초 뒤임 |

### 8.2 API와 ABI 수량

| 항목 | 결과 |
|---|---:|
| public header | 11 |
| `ZLINK_EXPORT` 공개 함수 선언 | 183 |
| 실제 `zlink_*` export | 213 |
| enum type | 48 |
| enumerator | 311 |
| struct type | 28 |
| `ZLINK_` macro | 56 |

이 수치는 누락 검출 기준이며 API 계약을 대신하지 않는다. S1 inventory는 각 이름과 disposition을 기록한다.

### 8.3 Test 기준선

```text
command: ctest --test-dir core/build --output-on-failure
result: 114/114 passed
total real time: 115.81 seconds
```

### 8.4 대표 성능 기준선

```text
command: PERF_MULTI_RUN_COOLDOWN_MS=0 \
         PERF_MULTI_TRANSPORT_TRANSITION_MS=0 \
         PERF_MULTI_PATTERN_TRANSITION_MS=0 \
         ./bindings/c/perf/run_benchmarks_multi.sh \
         --pattern ROUTER_ROUTER_REQREP,SPOT_REQREP,SPOT_SENDSEND \
         --transports tcp --msg-sizes 64,1024 --duration 1 \
         --clients 100 --runs 1 --results-tag v10_s0_baseline
result: success 6, unsupported 0, skip 0, fail 0
```

| Pattern | Size | Throughput | p99 |
|---|---:|---:|---:|
| ROUTER_ROUTER_REQREP | 64 B | 223,109 ops/s | 0.341 ms |
| ROUTER_ROUTER_REQREP | 1,024 B | 176,699 ops/s | 0.468 ms |
| SPOT_REQREP | 64 B | 253,427 ops/s | 0.329 ms |
| SPOT_REQREP | 1,024 B | 231,236 ops/s | 0.302 ms |
| SPOT_SENDSEND | 64 B | 292,372 ops/s | 0.259 ms |
| SPOT_SENDSEND | 1,024 B | 251,859 ops/s | 0.345 ms |

원본 결과는
`bindings/c/perf/results/multi/report/perf_c_multi_linux_20260716_195728_v10_s0_baseline.txt`이며
SHA-256은 `0500f3b76c84f0ec6f661b6f17d3db41254e4b0a2b4b1a7d58eb56607306fcf9`다.

S4의 같은 100-client 대표 case는 throughput이 각 기준의 90% 이상이고 p99가 120% 이하여야 한다. 1천·1만
MeshNode full-mesh, mixed request/multicast와 memory 기준은 구현 단계에서 추가 측정한다. 현재 checkout에는
같은 단일-ROUTER MeshNode topology가 없으므로 서로 다른 topology 수치를 직접 비교하지 않는다.

## 9. S0 완료 판정

- [x] Core 정식 spec 경로와 구현 일치 지도를 고정했다.
- [x] D, MN-D와 FD 결정에 미결정 상태가 없다.
- [x] framework spec, E2E, sample와 package inventory 범위를 고정했다.
- [x] 삭제 symbol과 금지 구현 검색 목록을 고정했다.
- [x] review manifest 형식과 clean 문구를 고정했다.
- [x] 현재 checkout API·ABI·test·대표 성능 기준선을 기록했다.
- [x] 모든 후속 문서가 따라야 하는 기술문서 작성·검증 원칙을 연결했다.
