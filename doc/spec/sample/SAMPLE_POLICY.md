# Sample Policy

## 목적
- 이 문서는 `core/samples/`와 `bindings/*/samples/`의 샘플 제작 규칙을 정의한다.
- 샘플은 문서이자 실행 가능한 검증 수단이어야 한다.
- 샘플은 사용자 onboarding, 언어 간 비교, public contract 설명을 위한 정답
  세트다.

## 적용 범위
- `core/samples/`
- `bindings/<언어>/samples/`
- 샘플 실행 스크립트
- 샘플 전용 helper

## 해석 규칙
- 샘플 정책은 기본적으로 `Recommended`다.
- 다만 공개적으로 배포되는 바인딩, 릴리즈 대상 바인딩, 또는 사용자 onboarding
  경로를 제공하는 바인딩에는 `Required`로 간주한다.
- `core/samples/`는 코어의 공식 샘플 표면이므로 이 문서의 규칙을 `Required`로
  따른다.

## 기본 원칙
- 샘플은 canonical public API만 사용해야 한다.
- deprecated, legacy, raw option bag, raw flags 경로를 샘플에서 사용하면
  안 된다.
- 샘플은 실제 메시징을 수행하고 결과를 확인해야 한다.
- 샘플은 canonical sample 세트만 둔다.
- canonical sample 세트에 정의되지 않은 sample은 원칙적으로 만들지 않는다.
- 다만 서비스 계층 컴포넌트를 구현한 표면은 해당 서비스 계층 샘플을 canonical
  세트에 포함할 수 있다.

## Canonical Sample Set
- 각 샘플 표면은 다음 canonical sample 세트를 기준으로 맞춘다.
  - `request_reply_async_sample`
  - `request_reply_callback_sample`
  - `pair_recv_sample`
  - `pair_callback_sample`
  - `pubsub_recv_sample`
  - `pubsub_callback_sample`
  - `dealer_router_recv_sample`
  - `dealer_router_callback_sample`
  - `stream_recv_sample`
  - `stream_callback_sample`
  - `spot_recv_sample`
  - `spot_callback_sample`
  - `monitor_recv_sample`
- service/spot 계열이 없는 표면은 `spot_*` 샘플을 제외할 수 있다.
- request-reply wrapper 표면이 없는 바인딩은 `request_reply_*` 샘플을 제외할 수
  있다.
- `core/samples/`의 request-reply 샘플은 `request_reply_callback_sample`만
  포함한다. `request_reply_async_sample`은 제외한다 (코어는 async completion
  표면을 직접 노출하지 않음).
- `bindings/*/samples/`의 request-reply wrapper 구현 표면은
  `request_reply_callback_sample`과 `request_reply_async_sample`을 모두
  포함한다 (callback 버전과 coroutine/async 버전 둘 다 제공).
- 서비스 계층 컴포넌트를 구현하는 표면은 다음 샘플을 추가로 포함한다.
  - `discovery_registry_sample` (Discovery + Registry end-to-end)
  - `registry_query_sample` (RegistryQueryClient로 토폴로지 조회)
- 각 샘플은 해당 컴포넌트를 구현한 경우에만 적용된다.
  - Discovery + Registry 미구현 → `discovery_registry_sample` 제외
  - RegistryQueryClient 미구현 → `registry_query_sample` 제외

## Sample Structure Rules
- recv/direct 버전과 callback 버전은 반드시 개별 파일로 분리한다.
- 한 파일이 두 수신 모델을 동시에 설명하면 안 된다.
- 샘플 파일명은 canonical 이름을 그대로 사용한다.
- recv sample 이름 패턴은 `*_recv_sample` 로 고정한다.
- callback sample 이름 패턴은 `*_callback_sample` 로 고정한다.
- async/coroutine sample 이름 패턴은 `*_async_sample` 로 고정한다.
- monitor sample 이름 패턴은 `*_monitor_recv_sample` 또는 단일
  `monitor_recv_sample` 로 고정한다.
- 각 샘플은 하나의 핵심 패턴만 설명해야 한다.
- 혼합형 샘플은 두지 않는다.

## Sample Content Rules
- 샘플은 핵심 로직이 한눈에 보이게 작성한다.
- 샘플은 문서에 그대로 옮겨도 흐름이 유지되는 짧고 직접적인 코드여야 한다.
- 보일러플레이트와 과도한 helper 의존을 줄인다.
- 핵심 메시징 흐름은 샘플 본문에서 직접 보여야 한다.
- bind/connect, callback 등록, send/publish, recv/subscribe, 출력은 샘플
  본문에서 직접 보여야 한다.
- endpoint 확보, monitor readiness 대기, raw TCP peer 보조 같은 인프라성
  코드는 얇은 sample helper로 분리할 수 있다.
- helper는 허용하지만 샘플의 핵심 메시징 의미를 가리면 안 된다.
- helper를 쓰더라도 샘플 본문만 읽으면 전체 흐름을 따라갈 수 있어야 한다.
- 샘플은 최소한 다음 흐름을 드러내야 한다.
  - context/socket 생성
  - endpoint bind/connect
  - subscription 설정이 필요한 경우 subscription 설정
  - send/publish
  - recv/subscribe 또는 callback 설치
  - 수신 결과 확인
  - 종료/정리

## Sample Classification Rules
- `recv sample` 은 direct receive 계열만 사용한다.
- `async sample` 은 future/task/coroutine 또는 그와 동등한 async completion
  surface만 사용한다.
- `callback sample` 은 callback receive 계열만 사용한다.
- `monitor sample` 은 monitor `recv/tryRecv` 계열만 사용한다.
- 한 샘플 안에서 recv와 callback 수신 모델을 섞지 않는다.
- data plane sample과 monitor sample 목적을 섞지 않는다.
- 샘플 본문은 해당 샘플 분류의 API 표면만 설명해야 한다.

## Recv Sample Rules
- `recv sample` 은 callback 등록 API를 사용하지 않는다.
- `send/publish` 후 direct receive 결과를 직접 확인한다.
- recv sample 본문은 recv surface를 배우는 데 필요한 코드만 남긴다.

## Async Sample Rules
- `async sample` 은 async request lifecycle이 핵심 표면인 기능에만 적용한다.
- request-reply wrapper를 제공하는 바인딩 표면은 `request_reply_async_sample`
  을 canonical sample에 포함해야 한다.
- `core/samples/`는 이 규칙에서 제외한다. 코어 request-reply 샘플은
  `request_reply_callback_sample`만 둔다.
- `request_reply_async_sample` 은 `DealerSocket.request()` /
  `RouterSocket.request()` async/coroutine 변형을 직접 사용해야 한다.
  (별도 `RequestDealer` / `RequestRouter` wrapper 클래스는 policy 상 금지됨 —
  `doc/spec/bindings/README.md` Socket Type Capability Policy 참조.)
- raw `DealerSocket`/`RouterSocket` 의 recv/send 샘플을 재포장해서는 안 된다.
- async sample은 callback completion으로 결과를 대신 설명하면 안 된다.
- reply 완료 확인은 `await`, `future.get`, `Task`, `channel receive`,
  `std::future` 등 언어별 async completion 표면으로 보여야 한다.

## Callback Sample Rules
- `callback sample` 은 callback 등록과 callback delivery만으로 수신을
  설명해야 한다.
- 본문에서 `receive()/subscribe()` 로 callback 결과를 대신 확인하지 않는다.
- callback sample이 recv sample처럼 보이면 안 된다.
- callback 완료 검증은 결정적 동기화로 작성한다.
- 허용 패턴:
  - `CountDownLatch`, `CompletableFuture`, `Event`, `condition variable`,
    `channel`, `Promise`, `semaphore` 등 언어별 일회성 signal 메커니즘
- 금지 패턴:
  - `sleep`, `Thread.sleep`, `time.sleep`, busy-wait loop,
    `lock`/`mutex` 기반 polling
- 대기에는 반드시 hard timeout을 건다 (예: 5초).
- timeout 초과 시 실패로 종료한다 (non-zero exit code 또는 예외).
- callback이 호출되면 signal을 보내고, 메인 스레드는 signal을 대기한다.

## Sample Transport Rules
- 샘플의 endpoint는 `tcp://127.0.0.1:<port>` 를 기본으로 사용한다.
- `inproc://`는 사용하지 않는다.
  - inproc은 callback이 동작하지 않는 경우가 있고, 실제 사용 환경과 다르다.
- port 할당은 OS ephemeral port를 사용한다 (bind to port 0 → 실제 port 획득).
- stream 샘플처럼 raw TCP client를 직접 연결하는 경우도 동일한 방식을 따른다.

## Sample Connection Handshake Rules
- 샘플에서 `sleep`을 이용한 connection 대기는 허용하지 않는다.
- TCP bind/connect 후 메시지를 주고받기 전에 monitor API로 connection
  readiness를 확인해야 한다.
- connection handshake는 monitor event 기반으로 작성한다.
- handshake 호출은 샘플 본문에서 보이게 두고, monitor polling 같은 반복 로직은
  helper로 감쌀 수 있다.
- 목표는 샘플 본문만 보고도 bind/connect 이후 왜 바로 send/publish 하지
  않는지 이해할 수 있게 만드는 것이다.

## Sample Runtime Verification Rules
- 모든 샘플은 실제 메시지를 주고받아 동작을 확인해야 한다.
- compile-only 예제로 끝나면 안 된다.
- 샘플은 최소한 아래 중 해당되는 항목을 검증해야 한다.
  - expected payload 수신
  - expected topic 수신
  - expected routing id 수신
  - callback 실제 호출
  - monitor event 실제 수신
  - monitor `tryRecv` empty 경로
- 샘플은 성공 시 명확한 성공 출력 또는 zero exit code를 가져야 한다.
- 실패 시 예외 또는 non-zero exit code로 실패를 드러내야 한다.

## Sample Output Format Rules
- 모든 샘플은 동일한 출력 포맷 규칙을 사용한다.
- 출력은 실행 시 "메시징이 실제로 일어났음"을 사용자에게 명확히 보여야 한다.
- 패턴의 메시징 방향에 따라 출력 포맷이 다르다.
  - **bidirectional** (pair, dealer-router, stream): `send/recv` 표현
  - **one-way** (pubsub, spot): `publish/subscribe` 표현
- one-way 패턴에서 echo처럼 보이는 self-subscribe 샘플을 만들면 안 된다.
  one-way 패턴은 발행과 구독이 별개 행위임을 출력에서 드러내야 한다.
- 패턴별 출력 예시:

```text
# bidirectional (echo / request-reply)
[dealer-router/request-reply/async] send: "ping" -> recv: "pong"
[dealer-router/request-reply/callback] send: "ping" -> recv: "pong"
[pair/recv] send: "hello-pair" -> recv: "hello-pair"
[pair/callback] send: "hello-pair" -> recv: "hello-pair"
[dealer-router/recv] send: "ping" -> recv: "pong"
[dealer-router/callback] send: "ping" -> recv: "pong"
[stream/recv] send: "hello-stream" -> recv: "hello-stream"
[stream/callback] send: "hello-stream" -> recv: "hello-stream"

# one-way (publish -> subscribe)
[pubsub/recv] publish: "prices/101.25" -> subscribe: "prices/101.25"
[pubsub/callback] publish: "prices/101.25" -> subscribe: "prices/101.25"
[spot/recv] publish: "room:lobby/hello-spot" -> subscribe: "room:lobby/hello-spot"
[spot/callback] publish: "room:lobby/hello-spot" -> subscribe: "room:lobby/hello-spot"

# monitor
[monitor/recv] recv: "connection-ready" -> tryRecv: empty

# service layer (discovery + registry)
[discovery-registry] registry: bind -> discovery: connect -> discover: "service-found"
[registry-query] connect -> snapshot: "topology-entry-found"
```

- assert만으로 성공/실패를 판단하는 출력 없는 샘플은 허용하지 않는다.
- `ok`, `done` 같은 단순 성공 메시지만 출력하는 것도 불충분하다.
- 보낸 값과 받은 값이 콘솔에 명시적으로 나타나야 한다.

## Sample Message Content Rules
- 동일 패턴 샘플은 언어와 구현 표면이 달라도 같은 메시지 내용을 사용한다.
- 언어 간 비교 시 동일한 출력이 나와야 한다.
- 통일 메시지 내용:

| 패턴 | 방향 | topic | payload |
|------|------|-------|---------|
| request-reply | bidirectional | — | request: `"ping"`, reply: `"pong"` |
| pair | bidirectional | — | `"hello-pair"` |
| dealer-router | bidirectional | — | request: `"ping"`, reply: `"pong"` |
| stream | bidirectional | — | `"hello-stream"` |
| pubsub | one-way | `"prices"` | `"101.25"` |
| spot | one-way | `"room:lobby"` | `"hello-spot"` |
| monitor | event plane | — | recv: `"connection-ready"`, tryRecv: `empty` |
| discovery-registry | service plane | — | discover: `"service-found"` |
| registry-query | service plane | — | snapshot: `"topology-entry-found"` |

## Sample Coverage Expectations
- 각 표면은 canonical sample 세트를 공식 샘플 표면으로 유지한다.
- request-reply wrapper를 구현한 바인딩 표면:
  - `request_reply_callback_sample`
  - `request_reply_async_sample`
- `core/samples/` (request-reply):
  - `request_reply_callback_sample`만 포함한다.
- direct recv 계열:
  - PAIR 또는 동등한 기본 send/recv
  - PUB/SUB 또는 동등한 topic publish/subscribe
  - ROUTER/DEALER 또는 동등한 routed messaging
  - STREAM direct recv
- callback 계열:
  - direct recv callback
  - topic subscribe callback
  - routed messaging callback
  - STREAM callback
- monitor 계열:
  - readiness/state event 확인 샘플
- service/spot 계열이 있는 표면은 spot recv/callback 샘플을 canonical에
  포함한다.

## Stream Socket Policy
- STREAM socket은 direct recv 방식과 callback 방식 둘 다 지원해야 한다.
- 따라서 각 표면은 STREAM에 대해 다음 둘을 모두 가져야 한다.
  - blocking/non-blocking direct receive surface
  - callback receive surface
- STREAM sample도 recv 버전과 callback 버전을 개별 파일로 제공하는 것을
  원칙으로 한다.
- STREAM payload는 zlink의 canonical message contract를 따른다.
- `len32be`, length-prefixed framing, 또는 그와 동등한 별도 프레이밍 규약은
  zlink public contract의 일부가 아니다.
- 존재하지 않는 framing 개념을 문서화하거나 sample에 암묵적으로 넣으면
  안 된다.

## Sample Execution Script Policy
- 각 샘플 표면은 전체 샘플을 실행해서 동작 확인할 수 있는 스크립트를 제공해야
  한다.
- 실행 스크립트는 샘플 디렉토리에 위치해야 한다.
- 상위 디렉토리에 `run_samples.*` wrapper를 두지 않는다.
- 스크립트는 repository 안에 두고 반복 실행 가능해야 한다.
- 스크립트는 샘플 목록을 명시적으로 실행해야 한다.
- 스크립트는 성공/실패를 요약해서 보여줘야 한다.
- 일부 샘플만 수동으로 돌리는 방식에 의존하면 안 된다.
- 이 항목은 샘플을 공식 제공하는 표면에서는 `Required`, 초기 단계 표면에서는
  `Recommended`다.
- 권장 위치:
  - `core/samples/run_samples.sh`
  - `bindings/<언어>/samples/run_samples.sh`
  - `bindings/<언어>/samples/run_samples.ps1`
  - language-specific task runner entry

## Sample Verification Requirements
- 새 canonical sample 추가 시 다음을 같이 확인해야 한다.
  - 개별 샘플 단독 실행 성공
  - 전체 샘플 실행 스크립트 포함
  - 전체 샘플 실행 스크립트에서 성공
- sample review 시 다음을 확인한다.
  - canonical sample 세트가 모두 존재하는가
  - recv/callback/monitor 분류가 섞이지 않았는가
  - recv/callback 버전이 개별 파일로 분리되어 있는가
  - canonical API만 사용하는가
  - 핵심 로직이 helper 뒤에 숨지 않았는가
  - 실제 메시징을 하고 결과를 확인하는가
  - 전체 샘플 실행 스크립트에 포함되어 있는가
  - 불필요한 `lock` 기반 대기를 사용하지 않았는가
