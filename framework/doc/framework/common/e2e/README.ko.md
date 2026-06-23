<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [공통 스펙](../README.ko.md) | [공통 샘플](../sample/README.ko.md)
<!-- framework-adapter-nav:end -->

# Framework Scenario E2E 테스트

이 문서는 ZLink Framework의 언어별 구현이 **실제 배포와 똑같이 생긴 서버 위에서도 제대로
도는지**를 확인하는 e2e 테스트를 정리한 것이다.

같은 검증이라도 e2e는 contract 테스트나 샘플과 결이 다르다.

- contract 테스트는 API 하나하나의 약속을 in-process로 빠르게 못 박는다.
- 샘플은 사용자가 그대로 따라 할 수 있는 정상 흐름을 보여 준다.
- e2e는 거기서 한발 더 나아간다. 실제 registry를 띄우고, 주소를 실제로 resolve하고, provider를
  여러 개 두고, 프로세스 경계까지 진짜로 나눈 상태 — 즉 **배포 현장과 같은 조건**에서 기능이
  의도대로 도는지를 본다.

## 1. 분류 원칙 — config 중심

e2e는 기능을 평면으로 죽 나열하지 않는다. **실제 배포처럼 생긴 서버 구성(config)을 하나의
단위로** 두고, 그 위에서 세부 동작을 실 사용자처럼 검증한다. 각 config는 sample 프로젝트처럼
독립 실행 앱이고, 서버 구성을 한 번 띄운 뒤 여러 client 시나리오를 그 위에서 돌린다.

### 선정 기준

시나리오는 "기존 테스트와 안 겹치는가"로 고르지 않는다. **현실적인 배포 구성에서 실 사용자가
하는 흐름인가**로 고른다.

- 기존 unit/contract/in-process 테스트와 단언이 겹쳐도 된다. 차별점은 단언의 새로움이 아니라
  현실적인 배포 컨텍스트와 sample 수준 public API 사용이다.
- 같은 기능이라도 실 registry·실 resolve·다중 노드·프로세스 경계가 끼면 다르게 동작할 수 있다.
  바로 그 지점을 본다.

### 코드 작성 규칙

- 각 client 시나리오는 messaging을 helper 뒤로 숨기지 않는다. **public contract 함수를 직접
  호출**해서 실제 API 사용이 한눈에 들어오게 쓴다.
- 검증은 샘플처럼 `ensure` 구문으로 직접 표현한다.
- 연결 부트스트랩(host 구성)은 샘플의 `HostFactory`/`CreateClient`처럼 얇게 분리해도 되지만,
  request/send/publish/resolve 같은 framework 호출은 시나리오 안에 직접 둔다.

## 2. 표준 프로젝트 구조

각 config는 언어별 표준 위치에 sample과 분리된 e2e 앱으로 둔다. 예(`.NET`):

```text
framework/languages/dotnet/e2e/RegistryMessaging/
|-- Shared/        server·client 공유 contracts
|-- Server/        서버 앱(고정 구성) — registry/provider 역할
|-- Client/        시나리오 앱
|   |-- Program.cs        시나리오 이름으로 분기 실행
|   `-- Scenarios/        config 시나리오별 파일 (public API 직접 호출)
|-- run_e2e.sh     서버 1회 구동 → client 시나리오 순차 실행
`-- SCENARIOS 문서는 framework/doc/framework/common/e2e/config-*.ko.md
```

실행 방식은 sample smoke와 비슷하다. test framework가 같은 프로세스 안에서 host를 직접 만드는
게 아니라, `run_e2e.*`가 서버 프로세스를 순서대로 띄우고 포트 readiness를 확인한 뒤 client
시나리오를 실행한다. scale·failover 같은 시나리오는 같은 스크립트가 프로세스를 추가로 띄우거나
종료한다.

client는 framework public client(channel/route), `zlink-http client`, public DI/container API만
쓴다. 테스트를 쉽게 만들겠다고 framework 내부 helper, private API, reflection, server/test-only
state에 손대지 않는다.

## 3. config 목록

각 config는 현실적인 서버 구성 하나를 단위로, 그 위에서 messaging·연결·spot·codec 등 세부
동작을 검증한다.

| Config | 서버 구성 | 다루는 것 |
|--------|-----------|-----------|
| [Config 1 — Registry messaging](config-1-registry-messaging.ko.md) | registry + api 노드 2 + client-server/route channel | 자동/수동/custom resolve, connection control, scale-out/in, same-rid failover, dealer mesh·weighted·round-robin, request·send·timeout·decode·미등록, 메시지 크기·backpressure |
| [Config 2 — Spot 서비스](config-2-spot-service.ko.md) | registry + entry/user spot + actor + session | spot↔channel·spot↔spot messaging, actor join(local/remote)·lifecycle callback·실행순서, session bind/relay(local/remote/다중)·재접속 이전성, owner routing, timer·idle close, stream(heartbeat/TLS), channel↔spot route bridge, stateful 장애·복구(노드 crash·owner 이동·경합) |
| [Config 3 — Pub/Sub 이벤트](config-3-pubsub.ko.md) | registry + publisher + subscriber 3 | fanout, topic filter, late subscriber, subscriber 격리, publish negative, subscriber 재연결·publisher 재시작 |
| [Config 4 — 등록·codec 변주](config-4-registration-codec.ko.md) | 단순 channel 구성 2 | 자동/선언/수동 등록, startup 검증, DI lifecycle, ordering, json/protobuf/msgpack codec, codec 격리, peer 간 codec 불일치 |
| [Config 5 — Resilience/lifecycle](config-5-resilience-lifecycle.ko.md) | 다중 노드 + registry | restart, reconnect, cancellation, in-flight crash, shutdown, 런타임 drain/restore, gray failure, partition 복구, flapping, 혼합 soak, wire 호환 |
| [Config 6 — Discovery·Registry HA](config-6-discovery-registry-ha.ko.md) | registry 1~3 cluster + provider 2 | registry 다중화 동등성, registry scale-out/in, registry 장애 중 discovery, 충돌 광고·peer flapping, embedded/standalone 배포, topology 조회 |
| [Config 7 — Monitoring](config-7-monitoring.ko.md) | registry + service 2 + monitor | socket/registry/spot 이벤트 runtime 관찰, 가용성 전이(failover/drain)·장애 중 관측, 다중 source 격리 |

## 4. 우선순위

| 우선순위 | 의미 | 구현 기준 |
|----------|------|-----------|
| `P0` | config의 핵심 기능을 주장하려면 반드시 있어야 하는 검증 | 모든 언어에서 구현한다 |
| `P1` | 특정 기능을 지원한다고 문서화한 언어가 통과해야 하는 검증 | 지원 언어에서 구현한다 |
| `P2` | 운영 규모·rolling update처럼 비용이 큰 검증 | release gate에 선택 적용, 미구현 이유를 남긴다 |

## 5. 공통 실행 원칙

- 테스트는 독립된 임시 작업 디렉토리와 로그 디렉토리를 쓴다.
- 서버 프로세스는 config가 선언한 역할대로 띄운다. registry가 필요한 config는 registry도 별도
  프로세스로 띄운다.
- port, routing id, Redis key prefix, store path는 실행마다 격리한다.
- 서버 준비 여부는 sleep만으로 판단하지 않고, 포트 readiness 또는 readiness marker로 확인한다.
- 성공 기준은 client 반환값, server evidence endpoint, 로그 marker를 조합한다. registry를 쓰는
  config는 topology도 성공 기준에 넣는다.
- 실패하면 각 프로세스의 stdout/stderr, framework 로그, client 마지막 요청 정보를 남긴다.
- 실패 시 먼저 원인 레이어를 분리한다. `core-capi`, `bindings`, `framework`, `sample`,
  `harness` 중 어디인지 evidence로 판정하고, 고친 레이어에 회귀 테스트를 둔다. framework 테스트를
  통과시키려고 C API나 bindings 버그를 framework에서 우회하지 않는다.
- 같은 시나리오는 언어별 public API 모양만 달라지고, 의미와 marker는 같아야 한다.

## 6. 로깅과 메시지 흐름 추적 (필수 공통)

모든 e2e는 **파일 로깅과 메시지 흐름 추적을 반드시 켜고** 작성·디버깅한다. ad-hoc `printf`나
콘솔 스크롤로 때우지 않는다. 트레이싱은 "메시지가 도착했나 / 핸들러로 갔나 / 응답이 나갔나"를
표준 기능으로 찍어 주므로, 테스트를 만들면서 1차 디버깅 도구로 쓴다.
(기능 스펙: [메시지 흐름 추적과 dispatch 관측](../spec/message-flow-tracing.ko.md))

### 6.1 모든 로그를 파일로 (`log/` 폴더)

- 각 서버/호스트·client 프로세스는 모든 framework 로그를 **실행별 `log/` 폴더 아래 파일**로
  출력한다. 콘솔 출력만으로 끝내지 않는다.
- 로그 디렉토리는 실행마다 격리하고(§5), VCS에서 제외한다(`.gitignore`). (C++ Bingo 예:
  `samples/Bingo/logs/`, `run_sample.sh`가 `BINGO_LOG_DIR`를 export.)
- 파일 sink는 부모 디렉토리를 자동 생성하는 API를 쓴다(C++ `app.logging().use_file(...)`/
  `use_rotating_file(...)`; `.NET`/Java/Node도 동일 의미 옵션). 디렉토리가 없다고 조용히 실패하면
  안 된다.
- 프로세스마다 파일을 분리해(예: `registry.log`, `play-a.log`, `session-a.log`, `client.log`)
  어느 노드 로그인지 바로 보이게 한다.

### 6.2 메시지 흐름 추적 켜기 (디버깅 1차 도구)

- e2e 실행 시 message flow 모드를 **최소 `key_transitions`**로 켠다. 그러면 한 메시지의
  인바운드(`received`→`dispatched`/`replied`)와 아웃바운드(`sent`→`reply_received`)가 한 줄씩
  찍힌다. 실패(`dropped`/error)는 같은 stream에 같은 `corr=`로 찍혀, 정상·실패가 하나의 흐름으로
  읽힌다.
- 로그 라인 토큰: `zlink flow: phase=… surface=… kind=… packet=… channel=… topic=… corr=…
  src=… spot=… actor=… [size=]`.
- **`corr=<id>`로 grep해 한 요청의 생애주기를 추적**한다. 노드 간에는 corr이 전파되는 경로
  (channel request↔reply, stream request↔reply echo, route 전파)에서 이어진다. (주의: corr은
  프로세스 전역 단조값이라 노드별 카운터가 독립이다 — 숫자만 같고 다른 메시지일 수 있으니, 노드 간
  연결은 corr이 실제 전파되는 경로에서만 신뢰한다. spot 구독/actor/publish 경로는 corr 대신
  spot/actor id로 키잉된다.)
- 트레이싱 로그는 앱 로그와 한 파일로 통합하거나(앱 로거 sink) 전용 파일로 분리할 수 있다
  (C++ `diagnostics.log_file`). 어느 쪽을 쓰든 §6.1대로 **파일로 남긴다**.
- 런타임 토글이 가능하면(C++ `app.set_message_flow_mode(...)`, `.NET` `IZLinkMessageFlowControl`)
  평소엔 `errors_only`로 두고 특정 시나리오 디버깅 때 올렸다 내릴 수 있다. 단 e2e 기본 캡처는 최소
  `key_transitions`로 둔다.
- 트레이싱은 **관측이지 제어가 아니다.** 켜도 기능 동작·성공 기준이 달라지면 안 되고, observer/trace
  실패가 메시지 처리나 테스트 판정을 바꾸면 안 된다.

### 6.3 실패 evidence에 포함

- 시나리오 실패 시 §5의 stdout/stderr·client 정보에 더해 위 **파일 로그(흐름 추적 포함)**를
  evidence로 남긴다. 원인 레이어 분리(`core-capi`/`bindings`/`framework`/`sample`/`harness`)도
  `corr=` 흐름으로 먼저 좁힌다.

### 6.4 언어별 적용 시점

message flow tracing은 언어별로 들어오는 중이다(2026-06-22 기준 C++·`.NET` 완료, Java/Kotlin/Node
미착수). 트레이싱이 아직 없는 언어에서도 **파일 로깅(§6.1)은 필수**이며, 트레이싱(§6.2)은 해당
언어 지원이 들어오는 대로 적용한다.

## 7. 시나리오 ID 규칙

ID는 `config 접두사 - 트랙 - 번호`를 쓴다. 예: `RM-A1`(Registry messaging, Track A, 1번).

| 접두사 | config |
|--------|--------|
| `RM` | Registry messaging |
| `SM` | Spot messaging |
| `PS` | Pub/Sub |
| `RC` | 등록·codec |
| `RL` | Resilience/lifecycle |
| `DR` | Discovery·Registry HA |
| `MON` | Monitoring |

테스트 이름은 언어 관례에 맞게 바꿔도 되지만, 리포트에는 config id와 시나리오 id가 드러나야 한다.

## 8. 완료 기준

- 각 config의 `P0` 시나리오는 모두 구현되어야 한다.
- `P1`은 해당 기능을 지원한다고 문서화한 언어에서 구현되어야 한다.
- 지원하지 않는 기능은 test skip이 아니라 feature map과 문서에 이유가 있어야 한다.
- 실패 경로는 client-visible 결과와 server-side 로그 또는 observer evidence를 함께 검증한다.
- 실패 원인 분류가 끝나지 않은 상태에서 workaround를 넣은 테스트는 완료로 보지 않는다.
