<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [공통 스펙](../README.ko.md) | [공통 샘플](../sample/README.ko.md)
<!-- framework-adapter-nav:end -->

# Framework Scenario E2E 테스트

이 문서는 ZLink Framework의 언어별 구현을 **실제 배포처럼 생긴 서버 구성** 위에서
검증하는 e2e 테스트를 정의한다. 여기서 말하는 e2e는 contract 테스트나 샘플과 다르다.
contract 테스트는 개별 API 계약을 in-process로 빠르게 고정하고, 샘플은 사용자가 따라 할
정상 흐름을 보여 준다. e2e는 **실 registry, 실 resolve, 다중 provider, 실제 프로세스
경계**가 모두 붙은 배포 형상에서 기능이 의도대로 도는지를 본다.

## 1. 분류 원칙 — config 중심

e2e는 기능을 평면 나열하지 않는다. **realistic한 서버 구성(config)을 단위로** 두고, 그 위에서
세부 동작을 실 사용자처럼 검증한다. 각 config는 sample 프로젝트처럼 독립 실행 앱이며,
서버 구성을 한 번 띄우고 여러 client 시나리오를 그 위에서 실행한다.

### 선정 기준

시나리오는 "기존 테스트와 겹치지 않는가"로 고르지 않는다. **realistic한 배포 구성에서
실 사용자가 하는 흐름인가**로 고른다.

- 기존 unit/contract/in-process 테스트와 단언이 겹쳐도 된다. 차별점은 단언의 새로움이
  아니라 현실적인 배포 컨텍스트와 sample 수준 public API 사용이다.
- 같은 기능이라도 실 registry·실 resolve·다중 노드·프로세스 경계가 끼면 다르게 동작할 수
  있고, 그 지점을 본다.

### 코드 작성 규칙

- 각 client 시나리오는 helper 뒤로 messaging을 숨기지 않는다. **public contract 함수를
  직접 호출**해서 실제 API 사용이 한눈에 들어오게 작성한다.
- 검증은 샘플처럼 `ensure` 구문으로 직접 표현한다.
- 연결 부트스트랩(host 구성)은 샘플의 `HostFactory`/`CreateClient`처럼 얇게 분리할 수
  있지만, request/send/publish/resolve 같은 framework 호출은 시나리오 안에 직접 둔다.

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

실행은 sample smoke와 비슷하다. test framework가 같은 프로세스 안에서 host를 직접 만드는
방식이 아니라, `run_e2e.*`가 서버 프로세스를 순서대로 띄우고 포트 readiness를 확인한 뒤
client 시나리오를 실행한다. scale·failover 같은 시나리오는 같은 스크립트가 추가 프로세스를
띄우거나 종료한다.

client는 framework public client(channel/route), `zlink-http client`, public DI/container
API만 사용한다. 테스트를 쉽게 만들려고 framework 내부 helper, private API, reflection,
server/test-only state에 직접 접근하지 않는다.

## 3. config 목록

각 config는 realistic한 서버 구성 하나를 단위로, 그 위에서 messaging·연결·spot·codec 등
세부 동작을 검증한다.

| Config | 서버 구성 | 다루는 것 |
|--------|-----------|-----------|
| [Config 1 — Registry messaging](config-1-registry-messaging.ko.md) | registry + api 노드 2 + client-server/route channel | 자동/수동/custom resolve, connection control, scale-out/in, same-rid failover, dealer mesh·weighted·round-robin, request·send·timeout·decode·미등록 |
| [Config 2 — Spot 서비스](config-2-spot-service.ko.md) | registry + entry/user spot + actor + session | spot↔channel·spot↔spot messaging, actor join(local/remote)·lifecycle callback·실행순서, session bind/relay(local/remote/다중)·재접속 이전성, owner routing, timer·idle close, stream(heartbeat/TLS) |
| [Config 3 — Pub/Sub 이벤트](config-3-pubsub.ko.md) | registry + publisher + subscriber 3 | fanout, topic filter, late subscriber, subscriber 격리, publish negative |
| [Config 4 — 등록·codec 변주](config-4-registration-codec.ko.md) | 단순 channel 구성 2 | 자동/선언/수동 등록, startup 검증, DI lifecycle, ordering, json/protobuf/msgpack codec, codec 격리 |
| [Config 5 — Resilience/lifecycle](config-5-resilience-lifecycle.ko.md) | 다중 노드 + registry | restart, reconnect, cancellation, in-flight crash, shutdown, partition 복구, flapping, wire 호환 |
| [Config 6 — Discovery·Registry HA](config-6-discovery-registry-ha.ko.md) | registry 1~3 cluster + provider 2 | registry 다중화 동등성, registry scale-out/in, registry 장애 중 discovery, embedded/standalone 배포, topology 조회 |
| [Config 7 — Monitoring](config-7-monitoring.ko.md) | registry + service 2 + monitor | socket/registry/spot 이벤트 runtime 관찰, 다중 source 격리 |

## 4. 우선순위

| 우선순위 | 의미 | 구현 기준 |
|----------|------|-----------|
| `P0` | config의 핵심 기능을 주장하려면 반드시 필요한 검증 | 모든 언어에서 구현한다 |
| `P1` | 특정 기능을 지원한다고 문서화한 언어가 통과해야 하는 검증 | 지원 언어에서 구현한다 |
| `P2` | 운영 규모·rolling update처럼 비용이 큰 검증 | release gate에 선택 적용, 미구현 이유를 남긴다 |

## 5. 공통 실행 원칙

- 테스트는 독립된 임시 작업 디렉토리와 로그 디렉토리를 사용한다.
- 서버 프로세스는 config가 선언한 역할대로 띄운다. registry가 필요한 config는 registry도
  별도 프로세스로 띄운다.
- port, routing id, Redis key prefix, store path는 실행마다 격리한다.
- 서버 준비는 sleep만으로 판단하지 않고 포트 readiness 또는 readiness marker로 확인한다.
- 성공 기준은 client 반환값, server evidence endpoint, 로그 marker를 조합한다. registry를
  쓰는 config는 topology도 성공 기준에 포함한다.
- 실패 시 각 프로세스의 stdout/stderr, framework 로그, client 마지막 요청 정보를 남긴다.
- 실패하면 먼저 원인 레이어를 분리한다. `core-capi`, `bindings`, `framework`, `sample`,
  `harness` 중 어디인지 evidence로 판정하고, 수정한 레이어에 회귀 테스트를 둔다. framework
  테스트를 통과시키려고 C API나 bindings 버그를 framework에서 우회하지 않는다.
- 같은 시나리오는 언어별 public API 모양만 달라지고 의미와 marker는 같아야 한다.

## 6. 시나리오 ID 규칙

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

테스트 이름은 언어 관례에 맞게 바꿀 수 있지만, 리포트에는 config id와 시나리오 id가
드러나야 한다.

## 7. 완료 기준

- 각 config의 `P0` 시나리오는 모두 구현되어야 한다.
- `P1`은 해당 기능을 지원한다고 문서화한 언어에서 구현되어야 한다.
- 지원하지 않는 기능은 test skip이 아니라 feature map과 문서에 이유가 있어야 한다.
- 실패 경로는 client-visible 결과와 server-side 로그 또는 observer evidence를 함께 검증한다.
- 실패 원인 분류가 끝나지 않은 상태에서 workaround를 넣은 테스트는 완료로 보지 않는다.
