# POSD 기반 `core/src` 리팩토링 계획

이 문서는 `core/src`를 John Ousterhout의
*A Philosophy of Software Design* (이하 POSD) 관점으로
리팩토링한다면 어떤 모습이 되어야 하는지에 대한
전체 방향과 단계별 실행 계획을 정리한다.

여기서 중요한 전제는 다음이다.

- 목표는 디렉터리 이름을 바꾸는 것이 아니다.
- 목표는 성능을 유지한 채 구조 복잡도를 낮추는 것이다.
- 목표는 `core/src`의 **복잡도를 낮추는 것**이다.
- 따라서 리팩토링 판단 기준은 POSD의 핵심인
  `deep module`, `information hiding`, `better-defined interfaces`,
  `special-case reduction`, `strategic programming`에 둔다.
- 단, 이 문서의 모든 구조 개선은
  `doc/perf/PERF_POLICY.md`와 각 single/multi 성능 정책을 만족하는 범위에서만 허용한다.

`core/src`는 현재 약 5.9만 라인 규모이며,
대략 다음 하위 영역으로 구성되어 있다.

- `api/`
- `core/`
- `sockets/`
- `protocol/`
- `engine/asio/`
- `transports/`
- `services/`
- `utils/`

이미 레이어는 나뉘어 있지만,
POSD 관점에서는 몇몇 경계가 여전히 너무 넓거나,
반대로 내부 복잡도를 충분히 숨기지 못하는 얕은 경계가 섞여 있다.

## 1. 리팩토링 목표

이 리팩토링의 목표 상태는 한 줄로 정리할 수 있다.

```text
"기능 추가 때 더 많은 파일을 건드리게 만드는 구조"에서
"핵심 정책이 깊은 모듈 안으로 내려가되, hot path 성능은 유지되는 구조"로 바꾼다.
```

구체적 목표는 다음과 같다.

- 구조 개선보다 성능 비퇴행을 우선한다.
- `socket_base_t`와 `asio_engine_t` 같은 고복잡도 허브를 축소한다.
- socket 의미, connection lifecycle, transport 기계적 처리, service workflow를 분리한다.
- public API와 service layer가 internal socket/transport 세부사항을 덜 알게 만든다.
- 옵션, 상태, 에러, 모니터링 경로를 일관된 표면으로 정리한다.
- 새 transport/protocol/service를 추가할 때 수정 범위를 좁힌다.
- 성능 최적화 코드를 정책 코드와 분리해 이해 비용을 낮춘다.

### 1.1 성능 우선 원칙

이 계획은 POSD 문서이지만, zlink의 `core/src` 리팩토링에서는
다음 원칙이 POSD 원칙보다 먼저 적용된다.

```text
성능 비퇴행 > 구조 개선 > 코드 미관
```

즉 구조가 더 좋아 보여도 다음 중 하나라도 발생하면
그 변경은 이 계획에서 실패로 본다.

- throughput 하락
- latency 상승
- CPU 사용률 상승
- hot path alloc/copy 증가
- callback churn 증가
- 기존 speculative fast path 약화

구조 개선은 성능을 해치지 않는 범위에서만 채택한다.
필요하다면 구조적 순수성보다 fast path 보존을 우선한다.

## 2. 현재 구조에서 보이는 복잡도 징후

### 2.1 `socket_base_t`가 너무 많은 역할을 가진다

현재 `core/src/sockets/socket_base.hpp`는 대략 다음을 한 클래스에 담고 있다.

- socket API 진입점
- bind/connect/term endpoint lifecycle
- poll 이벤트 연동
- pipe 이벤트 연동
- monitor event 생성/전송
- peer 상태 조회
- `stream`/`spot_sub` dispatch 제어
- inproc endpoint bookkeeping
- mailbox 연동

이 구조는 "base class 하나로 공통 기능을 재사용"하는 장점이 있지만,
POSD 기준으로는 상위 추상 하나가 너무 많은 독립 개념을 노출하고 있다.
그 결과 새 기능을 추가할 때
socket 의미 변경인지, connection 관리 변경인지, monitor 변경인지가
파일 구조만 보고 명확하지 않다.

### 2.2 `asio_engine_t`가 정책과 메커니즘을 동시에 가진다

현재 `core/src/engine/asio/asio_engine.hpp`는 다음을 동시에 다룬다.

- transport handshake
- protocol handshake
- heartbeat timer
- read/write buffer 관리
- speculative read/write
- gather write
- session push/pull 연동
- transport completion callback 처리
- stream 최적화 정책

이 조합은 성능상 이점은 있을 수 있지만,
문맥 전환 비용이 매우 높다.
POSD 관점에서는 "엔진"이 아니라
"엔진 + 파이프라인 정책 + 버퍼 전략 + transport orchestration"이
한 객체에 엉켜 있는 상태에 가깝다.

### 2.3 transport 계층의 생성/연결/수신 대기가 분산되어 있다

현재 transport 관련 코드는 다음처럼 퍼져 있다.

- `transports/tcp`, `transports/ipc`, `transports/ws`, `transports/tls`
- `asio_*_connecter`
- `asio_*_listener`
- `*_transport`
- address 파서/formatter

이 구조는 transport 종류별 파일 구분은 분명하지만,
상위 레벨에서 보면 "URI 하나를 받아 연결 전략을 만든다"는 큰 작업이
여러 파일과 타입으로 쪼개져 있다.
POSD 기준으로는 transport별 구현은 감추고,
상위에는 더 작은 개념 수만 보여야 한다.

### 2.4 service layer가 internal socket role을 과도하게 안다

`services/discovery/discovery.hpp` 같은 코드에는
`set_socket_option(socket_role, option, ...)` 형태가 보인다.
이것은 service API가 internal wiring을 사용자와 공유하고 있다는 뜻이다.

다만 이 문제는 service layer 전체가 동일하게 심각하다는 뜻은 아니다.
현재 코드 기준으로는 이미 다음과 같은 완화가 일부 진행되어 있다.

- `gateway_t`는 `set_option(...)`을 이미 제공한다.
- `spot_node_t`는 `set_pub_option(...)`, `set_sub_option(...)`을 이미 제공한다.
- 반면 `discovery_t`, `receiver_t`는 여전히 internal socket role 노출이 더 크다.

POSD 기준에서는 다음이 더 적절하다.

- service는 service 의미로만 말한다.
- internal socket 분해는 deep module 내부에 숨긴다.

즉 이 영역의 목표는 service layer 전체를 새로 만드는 것이 아니라,
이미 service 의미 API가 있는 부분은 강화하고,
socket role 노출이 남아 있는 부분만 수렴시키는 것이다.

### 2.5 API 진입점이 내부 조립 상세를 많이 가진다

`core/src/api/zlink.cpp`는 C API 진입점이면서
socket/service 생성, 옵션 전달, registry query helper 같은
여러 조립 성격 코드도 품고 있다.

POSD 관점에서는 API 표면은 얇아도 되지만,
그 아래에 깊은 facade가 있어야 한다.
지금은 일부 경로가 facade보다 직접 조립에 가깝다.

## 3. POSD 기준의 핵심 리팩토링 원칙

이 계획에서 적용할 POSD 원칙은 다음 다섯 가지다.

### 3.1 깊은 모듈을 만든다

모듈 수를 늘리는 것이 아니라,
상위 인터페이스 하나가 더 많은 내부 복잡도를 숨기게 만든다.

단, 깊은 모듈이 곧 런타임 오버헤드를 뜻해서는 안 된다.
`core/src`에서는 deep module 도입이
동적 할당, 추가 복사, 가상 호출 증가, hot path branch 증가로 이어지지 않게 설계해야 한다.

예:

- `socket_base_t`를 여러 얕은 helper로 찢는 대신
  `socket_runtime_t` 같은 깊은 협력 모듈을 두어
  endpoint, monitor, peer state, dispatch registration을 감춘다.
- `asio_engine_t`의 일부를
  `engine_pipeline_t` 같은 깊은 파이프라인 모듈로 내려
  handshake/timer/buffer/write 전략을 한 인터페이스 아래 묶는다.

### 3.2 정보 은닉을 강화한다

아래 정보는 상위 계층에 새지 않게 해야 한다.

- service 내부 socket topology
- transport별 connect/listen/handshake 상세
- monitor frame 구성 상세
- routing id 적용 시점과 peer bookkeeping 상세
- speculative I/O나 slab growth 같은 성능 정책 상세

중요한 점은 성능 정책을 "숨긴다"는 것이
"덜 중요하게 취급한다"는 뜻은 아니라는 점이다.
오히려 fast path 최적화는 내부 deep module 안에서 더 강하게 보존되어야 한다.

### 3.3 특수 케이스를 줄인다

현재 구조는 transport별 예외, socket별 예외, service별 예외가
여러 레이어에 분산될 가능성이 높다.
POSD 기준에서는 예외를 상위로 퍼뜨리지 않고
각 깊은 모듈 내부에서 정규화해야 한다.

### 3.4 이름을 책임 중심으로 바꾼다

이름은 구현 수단이 아니라 책임을 드러내야 한다.

예:

- `set_socket_option(socket_role, ...)`
  -> `set_option(...)` 또는 `set_publish_option(...)`
- `asio_*_connecter`
  -> 필요 시 `client_endpoint_factory` 계열 의미 이름
- `spot_node` 내부 control/data plane 섞임
  -> lifecycle, publish, subscribe, topology 역할 이름 분리

### 3.5 전략적 리팩토링으로 간다

POSD식 리팩토링은 "조금씩 정리"만으로 끝나지 않는다.
향후 변경 비용을 낮출 구조를 먼저 만들고,
그 위에 세부 최적화를 재배치해야 한다.

즉 이 문서는 단순 cleanup 계획이 아니라
`core/src`의 주된 변경 축을 다시 정의하는 문서다.

### 3.6 hot path는 구조보다 우선 보호한다

다음 경로는 리팩토링 과정에서 가장 보수적으로 다뤄야 한다.

- `asio_engine_t` read/write completion path
- speculative read / speculative write
- gather write
- stream dispatch fast path
- `inproc` message path
- socket send/recv hot loop

이 영역은 다음 원칙을 따른다.

- fast path에서 새 heap alloc 금지
- fast path에서 문자열/컨테이너 생성 금지
- 기존보다 branch 수를 늘리는 추상화 지양
- 가상 호출 추가는 명확한 측정 근거가 있을 때만 허용
- 단순 wrapper 계층 추가 금지

즉 hot path는 POSD의 "좋은 경계"를 만들되,
기계적인 객체 분해로 느려지지 않게 설계해야 한다.

## 4. 목표 아키텍처

목표 아키텍처는 다음처럼 요약할 수 있다.

```text
Current
-------
C API / Service API
    |
    v
socket_base_t / service classes
    |
    v
session_base_t / asio_engine_t / connectors / listeners
    |
    v
protocol / transports / utils


Target
------
C API Facades / Service Facades
    |
    v
Socket Semantics       Service Workflows
    |                  |
    +--------+---------+
             |
             v
      Connection Runtime
      - endpoint lifecycle
      - session attachment
      - monitor/peer state
      - transport factory
             |
             v
        Engine Pipeline
        - protocol handshake
        - transport handshake
        - buffering
        - heartbeat/timers
        - perf policy
             |
             v
      Transport Adapters
             |
             v
       Protocol Codecs
             |
             v
        Support Utils
```

핵심은 다음이다.

- socket/service는 "무엇을 하는가"를 담당한다.
- connection runtime은 "어떻게 붙고 끊기고 감시되는가"를 담당한다.
- engine pipeline은 "바이트가 메시지로 오가며 어떤 정책이 적용되는가"를 담당한다.
- transport adapter는 "각 전송 계층 상세"를 담당한다.

즉 의미와 메커니즘의 경계를 더 분명하게 만든다.

### 4.1 BE / TOBE 비교

아래 표는 현재 구조와 목표 구조를 POSD 관점에서 바로 비교한 것이다.

| 항목 | BE (현재) | TOBE (목표) |
|---|---|---|
| 상위 설계 인식 | `socket_base_t`, `asio_engine_t` 같은 허브 타입 중심으로 이해해야 한다 | socket semantics, connection runtime, engine pipeline 같은 책임 중심으로 이해한다 |
| socket 계층 | socket 의미와 endpoint/monitor/peer/dispatch가 한곳에 많이 모여 있다 | socket 의미는 `socket_base_t`, 운영 메커니즘은 `socket_runtime_t` 계열로 숨긴다 |
| session 계층 | session이 message contract와 reconnect/lifecycle을 함께 가진다 | session은 contract에 집중하고 lifecycle은 coordinator/runtime으로 이동한다 |
| engine 계층 | handshake, timer, buffering, speculative I/O, perf policy가 한 타입에 섞여 있다 | `asio_engine_t`는 facade로 남기고 내부 pipeline/policy가 복잡도를 흡수한다 |
| transport 계층 | transport별 connect/listen/address/adapter 흐름이 분산되어 있다 | factory/adapter 경계로 정리해 상위는 transport 종류를 덜 안다 |
| protocol 경계 | codec은 분리되어 있지만 handshake/control 의미가 engine에 많이 남아 있다 | codec과 protocol policy를 더 분리해 engine branching을 줄인다 |
| service API | 일부 service는 internal socket role을 여전히 노출한다 | service 의미 API로 수렴하고 internal wiring은 숨긴다 |
| C API 진입점 | API 파일이 validation과 내부 조립을 함께 가진다 | API는 facade delegation 중심으로 단순화한다 |
| 성능 보장 | 구조 개선이 성능 검증보다 앞서 읽힐 수 있다 | 각 phase는 perf gate 통과 전에는 완료로 간주하지 않는다 |
| 변경 비용 | 기능 변경 시 여러 레이어를 같이 건드리기 쉽다 | 변경 이유와 수정 위치가 더 직접적으로 대응한다 |
| 예외 처리 | transport/service/socket별 특수 케이스가 상위로 새기 쉽다 | 예외는 deep module 내부에서 정규화한다 |

짧게 요약하면 다음이다.

```text
BE   : 큰 타입 몇 개가 정책과 메커니즘을 함께 들고 있는 구조
TOBE : 책임은 상위에, 복잡도는 깊은 내부 모듈에 숨기되 성능 fast path는 유지하는 구조
```

## 5. 디렉터리 기준 목표 형태

이 절은 실제 rename 계획이라기보다
POSD 관점의 **논리적 레이어 목표**를 설명한다.
즉 아래 이름은 설명용이며,
초기 구현은 기존 `core/`, `sockets/`, `services/`, `transports/`, `utils/`
경로를 유지한 채 새 deep module을 추가하는 편이 더 현실적이다.

논리 구조는 대략 다음과 같은 형태가 바람직하다.

```text
core/src/
  api/
  runtime/        # ctx, object, thread, pipe, mailbox, timers
  socket/         # socket semantics, fq/lb/dist, socket-facing facade
  connection/     # session, endpoint runtime, monitor hub, peer registry
  engine/         # engine pipeline, transport-neutral I/O orchestration
  transport/      # tcp/ipc/ws/tls/pgm/inproc adapters + address parsing
  protocol/       # zmp/raw codec, metadata, frame policy
  service/        # discovery/gateway/spot workflows
  support/        # narrow utility surface only
```

중요한 점은 폴더 재배치 자체가 목표가 아니라는 것이다.
초기 단계에서는 기존 경로를 유지한 채
새 deep module을 추가하고 기존 타입을 점진적으로 위임 구조로 바꾸는 편이 더 안전하다.

## 6. 핵심 모듈 재구성 제안

### 6.1 `socket_base_t`를 socket 의미에 집중시킨다

목표는 `socket_base_t`가 다음만 직접 가지게 하는 것이다.

- socket type별 의미
- send/recv semantic hook
- pipe attach/detach semantic hook
- user-facing option hook

반대로 다음은 별도 deep module로 내린다.

- endpoint lifecycle
- monitor event emission
- peer bookkeeping
- stream/sub dispatch registration
- inproc endpoint map

권장 모양:

```text
socket_base_t
  -> socket_runtime_t
       - endpoint_catalog_t
       - monitor_hub_t
       - peer_registry_t
       - dispatch_registry_t
```

여기서 중요한 것은 helper를 10개 만드는 것이 아니라,
상위에서 볼 때 `socket_runtime_t` 하나만 알면 되게 만드는 것이다.

단, `socket_runtime_t` 추출은 hot path 바깥 책임부터 시작해야 한다.
예를 들어 monitor, endpoint catalog, peer bookkeeping은 먼저 내릴 수 있지만,
send/recv 직결 경로까지 무리하게 추상화하면 성능 비용이 커질 수 있다.

### 6.2 `session_base_t`를 connection contract로 명확히 만든다

현재 `session_base_t`는 socket과 engine 사이의 중재자이지만,
reconnect, linger, pipe clean-up, routing id 지연 적용 등
상당한 lifecycle 책임을 가진다.

POSD 기준 목표는 다음이다.

- `session_base_t`는 connection contract를 유지한다.
- reconnect/backoff/address resolve/listener accept 후 attach 같은
  lifecycle 정책은 `connection_runtime_t` 또는 `session_coordinator_t`로 이동한다.

즉 session은 "메시지와 상태 전달"에 집중하고,
연결 생성/재시도/복구는 coordinator가 맡는 구조가 바람직하다.

다만 이 분리는 control path 우선으로 진행해야 한다.
`pull_msg`/`push_msg` 경로에 새 wrapper나 상태 객체를 끼워 넣어
per-message 오버헤드를 만드는 방식은 피해야 한다.

### 6.3 `asio_engine_t`를 pipeline facade로 재구성한다

권장 방향은 `asio_engine_t`를 남기되,
내부에 다음 deep module을 도입하는 것이다.

- `engine_pipeline_t`
- `engine_buffer_strategy_t`
- `engine_timer_policy_t`
- `engine_perf_policy_t`

단, 이 타입들이 상위에 다 노출되면 POSD 위반이다.
상위는 `asio_engine_t` 또는 `i_engine`만 알고,
세부 정책 타입은 구현 파일 내부 또는 private 멤버 수준에 머물러야 한다.

실질적 변화는 다음과 같다.

- handshake 로직을 read/write 로직에서 분리
- heartbeat/timer 관리 분리
- speculative I/O와 buffer growth를 성능 정책 객체로 격리
- raw/zmp/ws/tls 특수 케이스를 pipeline 단계에서 정규화

중요한 제약:

- speculative write fast path는 유지되어야 한다.
- speculative read drain 최적화는 유지되어야 한다.
- gather write는 추상화 과정에서 scatter/gather 이점을 잃으면 안 된다.
- timer 분리는 hot callback 경로에서 추가 동적 분기를 만들지 않아야 한다.

### 6.4 transport 생성 경로를 factory 중심으로 통합한다

현재 connect/listen/address/transport 타입이 transport별로 흩어져 있다.
POSD 기준 목표는 상위가 다음 정도만 알게 하는 것이다.

- URI를 받는다
- transport capability를 가진 endpoint를 만든다
- connect/listen/accept를 수행한다

즉 상위 인터페이스는 다음처럼 단순해져야 한다.

```text
create_connector(uri, options)
create_listener(uri, options)
create_stream_transport(fd, options)
```

내부에서는 TCP/TLS/WS/WSS/IPC/PGM/inproc가 달라도 되지만,
상위 connection runtime은 transport 종류별 분기 수를 최소화해야 한다.

단, 여기서 "단일 factory"는 모든 transport가 완전히 동일한
lifecycle을 가진다는 뜻은 아니다.
특히 다음은 별도 취급이 필요할 수 있다.

- `inproc`: socket/session 내부 fast path 성격이 강하다.
- `pgm`: 다른 stream 계열 transport와 수명주기와 제약이 다르다.

따라서 목표는 공통점이 있는 경로를 강제로 하나로 합치는 것이 아니라,
상위 정책 코드가 transport 특수 케이스를 덜 알게 만드는 것이다.

또한 transport factory 정리는 setup/control path 개선이어야 하며,
송수신 hot path에 factory indirection이 남지 않게 해야 한다.

### 6.5 protocol을 codec과 session policy로 분리한다

현재 `protocol/`은 encoder/decoder/metadata 중심으로 정리되어 있다.
이는 비교적 나쁘지 않지만,
handshake/message classification/heartbeat command 처리까지
engine이 많이 알고 있으면 protocol 경계가 얕아진다.

목표는 다음이다.

- codec은 byte/frame/message 변환 책임을 가진다.
- protocol policy는 handshake/control frame/heartbeat 의미를 가진다.
- engine은 pipeline orchestration만 하고 protocol 세부의 branching을 줄인다.

### 6.6 service layer를 socket topology로부터 분리한다

`discovery`, `gateway`, `spot`은 domain workflow를 표현해야지
internal socket role을 설명하면 안 된다.

권장 방향:

- service별 option API는 service 의미를 따른다.
- service 내부에서 어떤 socket 조합을 쓰는지는 hidden wiring으로 둔다.
- monitor와 topology reporting도 service identity 기준으로 본다.

예를 들면 다음 방향이다.

```text
Before
------
discovery.set_socket_option(role, option, ...)
receiver.set_socket_option(role, option, ...)

After
-----
discovery.set_option(...)
receiver.set_option(...)
gateway.set_option(...)           # 기존 service-level surface 유지/정제
spot_node.set_pub_option(...)     # 기존 surface 유지, 의미만 더 분명화
spot_node.set_sub_option(...)
```

## 7. 리팩토링 후 기대되는 코드베이스 모습

POSD 기준으로 성공한 상태의 `core/src`는 대략 다음 특징을 가진다.

### 7.1 변경 이유와 수정 위치가 더 일치한다

예:

- publish backpressure 변경
  -> service option 또는 engine perf policy 쪽만 수정
- peer monitor 포맷 변경
  -> monitor hub 쪽만 수정
- transport 추가
  -> transport adapter + factory registration만 수정

즉 "왜 바꾸는가"와 "어디를 바꾸는가"가 더 잘 맞아야 한다.

### 7.2 헤더 파일이 더 작아지고 설명력이 높아진다

특히 다음 헤더는 의미 중심으로 축소되는 것이 바람직하다.

- `sockets/socket_base.hpp`
- `engine/asio/asio_engine.hpp`
- `core/session_base.hpp`
- 각 service public header

좋은 상태에서는 private 구현 세부가 header에서 덜 보이고,
공개 메서드 수와 멤버 수가 줄어든다.

### 7.3 주석이 "무엇을 왜 숨기는지"를 설명한다

POSD는 주석의 역할을
"코드가 이미 말하는 것을 반복"하는 데 두지 않는다.
따라서 새 구조에서는 다음 종류의 주석만 남기는 것이 좋다.

- 모듈의 책임 경계
- 불변식
- 성능 정책이 필요한 이유
- transport/protocol normalization 규칙

## 8. 단계별 실행 계획

### Phase 0. 기준선 고정

- `core/src` 모듈별 의존 관계를 간단히 시각화한다.
- `socket_base_t`, `asio_engine_t`, `session_base_t` 변경 전 테스트를 고정한다.
- transport별 연결/끊김/재연결/monitor/perf 회귀 시나리오를 수집한다.
- refactor 대상 hot path에 대해 alloc/copy/branch 관찰 포인트를 정한다.

산출물:

- dependency snapshot
- hotspot별 테스트 목록
- perf baseline
- perf guardrail checklist

### Phase 1. 경계 문서화와 deep module 식별

- 어떤 책임을 어디로 내릴지 ADR 수준으로 짧게 정리한다.
- `socket_runtime_t`, `connection_runtime_t`, `engine_pipeline_t` 같은
  새 경계의 책임을 먼저 문서화한다.
- 이 단계에서는 대규모 이동보다 이름과 경계를 먼저 고정한다.
- 각 경계에 대해 "hot path 포함 여부"를 명시한다.

산출물:

- boundary notes
- extraction order
- hot path exclusion list

### Phase 2. socket runtime 추출

- `socket_base_t`에서 endpoint/monitor/peer/dispatch 책임을 내린다.
- 기존 public behavior는 유지하되,
  내부 구현은 새 runtime 모듈에 위임한다.
- socket type별 클래스는 semantic override만 유지한다.
- per-message path가 아닌 bookkeeping 경로부터 먼저 이동한다.

완료 기준:

- `socket_base_t` public surface는 유지
- private 멤버와 private helper 수 감소
- monitor/peer/dispatch 관련 회귀 테스트 통과
- perf baseline 비퇴행

### Phase 3. connection/session orchestration 추출

- `session_base_t`에서 reconnect, attach sequencing, linger 종료 정책을
  coordinator로 이동한다.
- connect/listen/accept 후 session 조립 흐름을 단일 orchestration 경로로 묶는다.
- control plane 이동과 data plane hot path 이동을 분리한다.

완료 기준:

- session은 message contract 중심
- reconnect와 attach 흐름이 분리된 테스트로 검증 가능
- per-message CPU/latency 비퇴행

### Phase 4. engine pipeline 추출

- `asio_engine_t`에서 handshake/timer/buffer/perf policy를 분리한다.
- speculative I/O와 gather write를 engine 내부 deep policy로 격리한다.
- raw/zmp/ws/tls별 분기를 가능한 pipeline 단계로 정규화한다.
- 가장 먼저 성능 정책 인터페이스를 고정하고,
  실제 코드 이동은 micro-benchmark로 검증하면서 단계적으로 진행한다.

완료 기준:

- `asio_engine_t` 헤더 축소
- read/write callback 흐름이 더 짧아짐
- protocol/transport 특수 케이스 수 감소
- single/multi perf 정책 기준 비퇴행
- speculative/gather fast path 유지 확인

### Phase 5. transport factory 정리

- address parsing, connector/listener creation, accepted socket wrapping을
  factory와 adapter registry 중심으로 정리한다.
- transport 추가 시 수정해야 할 상위 분기 수를 줄인다.
- `inproc`와 `pgm`처럼 별도 lifecycle이 필요한 경로는
  공통 factory contract 바깥의 예외가 아니라,
  명시적 capability 차이로 문서화한다.

완료 기준:

- 상위 연결 코드에서 transport별 `if/switch` 감소
- TCP/TLS/WS/WSS/IPC 공통 lifecycle 경로 정리
- `inproc`/`pgm`의 예외 규칙이 상위 레이어가 아니라 adapter 경계에 머묾
- steady-state data path 비퇴행

### Phase 6. service facade 단순화

- `discovery`, `gateway`, `spot`의 option/config API를 service 의미 중심으로 바꾼다.
- service 내부 socket role은 숨기고,
  필요한 경우 dedicated config 객체를 둔다.

완료 기준:

- service API에서 internal socket role 노출 감소
- bindings/documentation이 service 의미를 그대로 사용
- service facade 추가로 인한 steady-state throughput 저하 없음

### Phase 7. API facade 및 문서 정리

- `api/zlink.cpp`의 조립성 코드를 facade/adapter로 분리한다.
- public C API는 validation + facade delegation 중심으로 남긴다.
- 새 구조를 `doc/internals`에 반영한다.

완료 기준:

- C API 진입점 파일의 책임 축소
- internal type knowledge가 facade 뒤로 이동
- public API wrapper 추가가 hot path 비용으로 남지 않음

## 9. 비목표

다음은 이 계획의 1차 목표가 아니다.

- 외부 API를 한 번에 전면 교체하는 것
- 성능 최적화 코드를 제거하는 것
- 추상화를 위해 hot path 호출 단계를 늘리는 것
- 모든 유틸리티를 새 폴더로 옮기는 것
- C++17 스타일로 광범위하게 현대화하는 것
- libzmq 호환 레이어를 다시 도입하는 것

POSD 리팩토링은 "예쁘게 보이는 구조"보다
"성능 비퇴행이 보장된 상태에서 변경 비용이 낮아지는 구조"를 우선한다.

## 10. 위험 요소와 대응

### 10.1 성능 회귀

엔진/transport 경계를 다시 자르다 보면
hot path에 간접 호출이나 추가 상태 객체가 생길 수 있다.

대응:

- perf baseline 유지
- `doc/perf/PERF_POLICY.md` 기준 single/multi 측정 유지
- 추출 전후 flamegraph 또는 샘플링 비교
- fast path는 inline/helper보다 deep policy 내부에서 유지
- alloc/copy/branch 증가가 관측되면 구조안을 되돌리거나 재설계
- phase 완료 조건에 perf gate를 명시적으로 포함

### 10.2 동작 회귀

특히 다음 경로가 위험하다.

- reconnect
- monitor event ordering
- routing id propagation
- heartbeat timeout
- stream dispatch
- spot/gateway/discovery background control path

대응:

- Phase별 회귀 테스트 고정
- transport별 통합 테스트 유지

### 10.3 shallow module 남발

POSD 리팩토링에서 흔한 실패는
"클래스 수만 늘고 실제 복잡도는 그대로"인 경우다.

대응:

- 새 타입을 만들 때 반드시
  "상위가 무엇을 덜 알아도 되는가"를 기준으로 평가
- 단순 위임만 하는 타입은 추가하지 않음

## 11. 성공 판단 기준

다음 지표를 동시에 만족해야 성공이다.

- 성능이 baseline 대비 비퇴행이다.
- single/multi perf 정책 기준 결과가 유지된다.
- 기능 하나를 바꿀 때 수정 파일 수가 줄어든다.
- `socket_base_t`, `asio_engine_t`, `session_base_t` 헤더와 구현 크기가 줄어든다.
- service API가 internal socket topology를 덜 노출한다.
- transport 추가 시 상위 레이어 수정이 국소화된다.
- 문서에서 설명해야 할 "예외 규칙" 수가 줄어든다.

여기서 성능 비퇴행은 최소한 다음을 포함한다.

- throughput 유지 또는 개선
- latency 유지 또는 개선
- CPU 사용률 유지 또는 개선
- hot path alloc/copy 증가 없음

## 12. 요약

POSD 기준으로 `core/src`를 리팩토링한다는 것은
파일을 잘게 쪼개는 작업이 아니다.

핵심은 다음 세 가지다.

- `socket`, `connection`, `engine`, `transport`, `service`의 책임 경계를 다시 잡는다.
- 상위 인터페이스는 단순하게 유지하고 내부에 더 많은 복잡도를 숨긴다.
- 성능 최적화와 도메인 정책을 같은 객체에 계속 쌓지 않되, fast path는 우선 보존한다.

가장 먼저 손대야 할 지점은 다음 셋이다.

1. `socket_base_t`
2. `session_base_t`
3. `asio_engine_t`

이 셋의 책임이 줄어들기 시작하면,
그 다음에 transport 정리와 service facade 단순화가 따라오기 쉬워진다.

단, 어떤 단계도 perf gate를 통과하기 전에는 완료로 간주하지 않는다.
