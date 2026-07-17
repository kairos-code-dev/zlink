# Sample Policy

## 목적
- 이 문서는 `core/samples/`와 `bindings/*/samples/`의 샘플 제작 규칙을 정의한다.
- 샘플은 문서이자 실행 가능한 검증 수단이어야 한다.
- 샘플은 사용자 onboarding, 언어 간 비교, public contract 설명을 위한 정답
  세트다.
- 샘플은 가이드 문서 코드의 **단일 출처**다. 가이드의 코드 예제(특히 7언어 탭
  문서)는 샘플의 실제 API·흐름을 따른다 (Documentation Source Rules).
- 같은 샘플은 모든 언어에서 같은 시나리오·식별자·값·순서를 쓴다
  (Cross-Language Uniformity Rules). 표현만 언어 관용을 따른다.

## 적용 범위
- `core/samples/`
- `bindings/<언어>/samples/` (네이티브 바인딩: C, C++, C#/.NET, Java, Node, Python, Go, Rust)
- `bindings/kotlin/samples/`, `bindings/javascript/samples/` (런타임 공유 언어)
- 샘플 실행 스크립트
- 샘플 전용 helper

## 계약 상태
- 이 문서는 샘플 작성 가이드가 아니라 샘플 공개 계약이다.
- `core/samples/`와 공개 배포되는 `bindings/*/samples/`는 이 문서의 규칙을
  `Required`로 따른다.
- 공개 샘플 runner, CI sample smoke, 샘플 정렬 테스트는 이 계약을 기준으로
  실패해야 한다.
- "대체로 이렇게 하는 것이 좋다" 수준의 권고로 해석하지 않는다.
- 계약과 현재 샘플 구현이 다르면 샘플 구현을 계약에 맞춘다.

## 기본 원칙
- 샘플은 canonical public API만 사용해야 한다.
- deprecated, legacy, raw option bag, raw flags 경로를 샘플에서 사용하면
  안 된다.
- 샘플은 실제 메시징을 수행하고 결과를 확인해야 한다.
- 샘플은 사용자가 실행 결과를 보고 메시지 흐름을 이해할 수 있어야 한다.
- 샘플은 "컴파일되는 예제"가 아니라 "실행 관찰 가능한 예제"여야 한다.
- 샘플은 역할, 토폴로지, 송수신 결과가 로그나 종료 검증으로 드러나야 한다.
- 샘플은 canonical sample 세트만 둔다.
- canonical sample 세트에 정의되지 않은 sample은 원칙적으로 만들지 않는다.
- 다만 서비스 계층 컴포넌트를 구현한 표면은 해당 서비스 계층 샘플을 canonical
  세트에 포함할 수 있다.

## Canonical Sample Set
- 각 샘플 표면은 다음 canonical sample 세트를 기준으로 맞춘다.
  - `request_reply_async_sample`
  - `pair_recv_sample`
  - `pubsub_recv_sample`
  - `dealer_router_recv_sample`
  - `stream_recv_sample`
  - `spot_recv_sample`
  - `spot_request_async_sample`
  - `stream_packet_callback_sample`
  - `monitor_recv_sample`
  - `actor_room_server_sample`
  - `actor_gateway_relay_sample`
  - `actor_single_player_queue_sample`
- service/spot 계열이 없는 표면은 `spot_*` 샘플을 제외할 수 있다.
- request-reply wrapper 표면이 없는 바인딩은 `request_reply_*` 샘플을 제외할 수
  있다.
- `core/samples/`는 request-reply wrapper sample을 canonical set에 포함하지
  않는다. 코어는 `request_reply_async_sample` 대상이 아니다.
- `bindings/*/samples/`의 request-reply wrapper 구현 표면은
  `request_reply_async_sample`을 포함해야 한다. callback 기반 request-reply
  sample은 포함하지 않는다.
- 서비스 계층 컴포넌트를 구현하는 표면은 다음 샘플을 추가로 포함한다.
  - `discovery_registry_sample` (Discovery + Registry end-to-end)
- Actor dispatch 표면을 구현하는 표면은 다음 샘플을 추가로 포함한다.
  - `actor_room_server_sample` (Spot dispatch에서 Actor별 unread state drain)
  - `actor_gateway_relay_sample` (STREAM session에서 remote Actor로 relay)
  - `actor_single_player_queue_sample` (한 사용자 queue를 Actor로 직렬화)
- 각 샘플은 해당 컴포넌트를 구현한 경우에만 적용된다.
  - Discovery + Registry 미구현 → `discovery_registry_sample` 제외
  - Actor dispatch 미구현 → `actor_*` 샘플 제외

## Official Runner Contract
- 공개 sample runner와 README에서 안내하는 sample 실행 경로는 canonical sample
  세트만 실행해야 한다.
- runner는 canonical sample 누락을 허용하면 안 된다.
- runner는 canonical set 밖의 샘플을 "공식 샘플"로 실행하면 안 된다.
- compatibility 또는 migration 목적의 임시 샘플이 남아 있더라도, 공식 runner와
  README onboarding 경로에는 포함하지 않는다.
- runner가 샘플 수를 요약할 때, 그 분모는 canonical sample 세트와 정확히
  일치해야 한다.

## Sample Structure Rules
- recv 버전과 STREAM packet callback 버전은 반드시 개별 파일로 분리한다.
- `spot_recv_sample` 과 `spot_request_async_sample` 은 반드시 개별 파일로
  분리한다.
- 한 파일이 여러 수신 모델을 동시에 설명하면 안 된다.
- 샘플 파일명은 canonical 이름을 그대로 사용한다.
- recv sample 이름 패턴은 `*_recv_sample` 로 고정한다.
- packet callback sample 이름 패턴은 `*_packet_callback_sample` 로 고정한다.
- async/coroutine sample 이름 패턴은 `*_async_sample` 로 고정한다.
- monitor sample 이름 패턴은 `*_monitor_recv_sample` 또는 단일
  `monitor_recv_sample` 로 고정한다.
- 각 샘플은 하나의 핵심 패턴만 설명해야 한다.
- 혼합형 샘플은 두지 않는다.
- 다만 `spot_recv_sample` 은 예외다.
  spot은 publish/subscribe, send/recv, timer event를 함께 제공하는 표면 특성을
  보여 주기 위해 통합 흐름을 한 파일에 담을 수 있다.

## Sample Content Rules
- 샘플은 핵심 로직이 한눈에 보이게 작성한다.
- 샘플은 문서에 그대로 옮겨도 흐름이 유지되는 짧고 직접적인 코드여야 한다.
- 보일러플레이트와 과도한 helper 의존을 줄인다.
- 핵심 메시징 흐름은 샘플 본문에서 직접 보여야 한다.
- bind/connect, event 등록, send/publish, recv/subscribe, 출력은 샘플
  본문에서 직접 보여야 한다.
- endpoint 확보, monitor readiness 대기, raw TCP peer 보조 같은 인프라성
  코드는 얇은 sample helper로 분리할 수 있다.
- helper는 허용하지만 샘플의 핵심 메시징 의미를 가리면 안 된다.
- helper를 쓰더라도 샘플 본문만 읽으면 전체 흐름을 따라갈 수 있어야 한다.
- helper는 endpoint 확보, bounded readiness wait, test fixture 정리처럼
  인프라성 작업만 감싸야 한다.
- helper가 sender/receiver 역할 배치, 메시지 흐름, 핵심 API 호출 순서를 숨기면
  안 된다.
- 샘플은 최소한 다음 흐름을 드러내야 한다.
  - context/socket 생성
  - endpoint bind/connect
  - subscription 설정이 필요한 경우 subscription 설정
  - send/publish
  - recv/subscribe 또는 예외적으로 필요한 event callback 설치
  - 수신 결과 확인
  - 종료/정리
- 샘플은 마지막에 사용자가 눈으로 읽을 수 있는 결과 줄을 출력해야 한다.
  출력에는 최소한 "무엇을 보냈고 무엇을 받았는지" 또는 "어떤 이벤트가
  확인됐는지"가 드러나야 한다.

## Sample Classification Rules
- `recv sample` 은 direct receive 계열만 사용한다.
- `async sample` 은 future/task/coroutine 또는 그와 동등한 async completion
  surface만 사용한다.
- `packet callback sample` 은 packet callback delivery 계열만 사용한다.
- `monitor sample` 은 monitor `recv` 계열을 기본으로 사용한다.
- 바인딩이 non-blocking monitor receive를 공개하면 `tryRecv` empty 경로를 함께
  보여 줄 수 있다.
- `spot_recv_sample` 은 `Spot` 의 canonical subscribe recv surface를 중심으로
  설명한다. `dispatch_event` callback을 쓰는 바인딩은 activation signal로만
  사용할 수 있다.
- `spot_request_async_sample` 은 spot request/reply를 coroutine 또는 async
  completion 표면으로 설명한다.
- 한 샘플 안에서 recv, packet callback, monitor 수신 모델을 섞지 않는다.
- data plane sample과 monitor sample 목적을 섞지 않는다.
- 샘플 본문은 해당 샘플 분류의 API 표면만 설명해야 한다.

## Recv Sample Rules
- `recv sample` 은 packet callback 등록 API를 사용하지 않는다.
- `send/publish` 후 direct receive 결과를 직접 확인한다.
- `spot_recv_sample` 은 `Spot.subscribe()` 또는 그와 동등한 canonical
  subscribe recv surface로 데이터를 확인한다.
- `spot_recv_sample` 은 다음 흐름을 함께 보여 줄 수 있다.
  - publish 후 subscribe 성격의 메시지 수신
  - send 후 direct recv
  - timer event 발생 후 관련 recv 또는 후속 동작 확인
- recv sample 본문은 recv surface를 배우는 데 필요한 코드만 남긴다.

## Spot Sample Rules
- `spot_recv_sample` 은 spot이 여러 상호작용 모델을 한 표면에서 제공한다는
  점을 보여 주는 대표 샘플이다.
- 이 샘플은 단일 패턴 예제가 아니라 spot event and recv overview로 작성한다.
- 따라서 한 파일 안에 publish, send, timer 흐름을 함께 둘 수 있다.
- 다만 각 흐름은 순서대로 분리해서 보여야 한다.
  사용자가 어떤 호출이 어떤 결과를 만드는지 바로 따라갈 수 있어야 한다.
- 수신 확인은 `Spot` 의 canonical recv surface로 일관되게 처리한다.
- monitor event, packet callback, 별도 async completion 표면은 여기에 섞지
  않는다.
- helper를 쓰더라도 본문에서 다음이 직접 보여야 한다.
  - publish 호출
  - send 호출
  - timer 등록 또는 시작
  - 필요하면 dispatch handler 등록
  - 실제 수신 확인용 subscribe 또는 recv 호출
- `spot` 의 request/reply는 `spot_recv_sample` 에 넣지 않는다.
  바인딩 샘플에서는 `spot_request_async_sample` 으로 분리해서 coroutine 또는
  async completion 흐름으로 설명한다.

## Async Sample Rules
- `async sample` 은 async request lifecycle이 핵심 표면인 기능에만 적용한다.
- request-reply wrapper를 제공하는 바인딩 표면은 `request_reply_async_sample`
  을 canonical sample에 포함해야 한다.
- `core/samples/`는 이 규칙에서 제외한다. 코어는 request-reply wrapper sample을
  canonical set에 두지 않는다.
- `request_reply_async_sample` 은 `DealerSocket.request()` /
  `RouterSocket.request()` async/coroutine 변형을 직접 사용해야 한다.
  (별도 `RequestDealer` / `RequestRouter` wrapper 클래스는 policy 상 금지됨 —
  `doc/spec/bindings/README.md` Socket Type Capability Policy 참조.)
- raw `DealerSocket`/`RouterSocket` 의 recv/send 샘플을 재포장해서는 안 된다.
- async sample은 event callback completion으로 결과를 대신 설명하면 안 된다.
- reply 완료 확인은 `await`, `future.get`, `Task`, `channel receive`,
  `std::future` 등 언어별 async completion 표면으로 보여야 한다.
- `spot` 이 request/reply async surface를 제공하는 바인딩 표면은
  `spot_request_async_sample` 을 canonical sample에 포함해야 한다.
- `core/samples/`는 `spot_request_async_sample` 대상이 아니다.

## Request Pattern Rules
- 일반 request-reply wrapper 샘플의 기본 역할 모델은 `dealer -> router` 다.
- 샘플 문서와 출력은 요청 측을 requester 또는 client로, 응답 측을 responder
  또는 server로 설명할 수 있다.
- 다만 일반 socket request-reply 토폴로지 표기는 가능하면 `dealer -> router`
  의미가 드러나게 적는다.
- `router -> router` 토폴로지는 특수 구현이나 내부 구조 설명에는 쓸 수 있어도,
  일반 canonical request 패턴의 기본 표기로 쓰지 않는다.
- `spot_request_async_sample` 의 request 흐름도 바깥에서 보이는 의미는
  requester -> responder 로 설명한다.
  이는 service-layer request 패턴이며 일반 socket request-reply와 같은 의미를
  갖지만 토폴로지 표현은 binding public surface에 맞춰
  `router requester -> spotnode/router -> spot responder` 또는
  `spot requester -> channel responder` 로 적을 수 있다.

## Packet Callback Sample Rules
- packet callback sample은 STREAM에만 적용한다.
- `stream_packet_callback_sample` 은 packet callback 등록과 callback delivery만으로
  수신을 설명해야 한다.
- 본문에서 `recv()` 로 packet callback 결과를 대신 확인하지 않는다.
- packet callback sample이 recv sample처럼 보이면 안 된다.
- callback 완료 검증은 결정적 동기화로 작성한다.
- 허용 패턴:
  - `CountDownLatch`, `CompletableFuture`, `Event`, `condition variable`,
    `channel`, `Promise`, `semaphore` 등 언어별 일회성 signal 메커니즘
- 금지 패턴:
  - `sleep`, `Thread.sleep`, `time.sleep`, busy-wait loop,
    `lock`/`mutex` 기반 polling
- 대기에는 반드시 hard timeout을 건다 (예: 5초).
- timeout 초과 시 실패로 종료한다 (non-zero exit code 또는 예외).
- packet callback이 호출되면 signal을 보내고, 메인 스레드는 signal을 대기한다.

## Actor Dispatch Sample Rules
- Actor dispatch sample은 session message를 Actor id로 나누어 처리하는 흐름을
  보여준다.
- Actor가 socket이나 endpoint를 직접 소유한다고 설명하면 안 된다.
- `actor_room_server_sample` 은 여러 Actor가 같은 Spot dispatch stream에서
  `ACTOR_READABLE` subject로 구분되는 흐름을 보여준다.
- `actor_gateway_relay_sample` 은 gateway STREAM session의 part가 remote Actor로
  relay되는 흐름을 보여준다.
- `actor_single_player_queue_sample` 은 한 Actor의 unread state가 도착 순서대로
  drain되는 흐름을 보여준다.
- sample은 `zlink_stream_session_bind_actor()`,
  `zlink_stream_session_send_to_actor()`와 claim receive batch
  (`zlink_mesh_claim_recv_batch()`)를 기준으로 작성한다.
  generic route lookup을 Actor 주소 조회 sample에 쓰지 않는다.

## Actor Sample Scenario Rules
- Actor 샘플은 단순 send/recv가 아니라 시나리오 시퀀스를 보인다. 각 샘플은 아래
  고정 시나리오를 모든 언어에서 같은 순서·같은 식별자로 따른다.
- `actor_room_server_sample`:
  - actor id `"room-player-1"` 가 user Spot에 `"enter-room"` 으로 join 한다.
  - Spot은 join을 `"accepted"` 로 수락한다.
  - STREAM session에서 `"move:north"` payload가 bound actor로 전달된다.
  - Spot dispatch의 `ACTOR_READABLE` subject로 그 payload를 drain해 확인한다.
- `actor_gateway_relay_sample`:
  - actor id `"play-session-actor"` 가 play node의 user Spot에 `"join-play"` 로 join한다.
  - gateway STREAM session에서 `"client-input"` payload가 bound actor로 relay된다.
  - Spot dispatch의 `ACTOR_READABLE` 로 그 payload를 확인한다.
- `actor_single_player_queue_sample`:
  - actor id `"single-player"` 가 첫 user Spot에 `"join-first"` 로 join한다.
  - actor가 leave한 사이 도착한 메시지(`"before"`, `"between"`)가 유실되지 않고
    큐잉된다.
  - actor가 `"join-second"` 로 다른 Spot에 rejoin하면 큐된 메시지를 도착 순서대로
    수신한다.
  - 이 샘플의 의미는 **재접속 이전성**(actor가 세션 위치와 무관하게 같은 엔티티로
    이어지고, 그 사이 메시지가 보존됨)이다.
- join 수락 payload는 `"accepted"` 를 기본으로 한다.
- 세 샘플 모두 actor가 socket이나 endpoint를 직접 소유한다고 설명하지 않는다.
- 기준 구현은 cpp/go/rust 샘플이며, 다른 언어 샘플은 이 값·순서에 맞춘다.

## Sample Transport Rules
- 샘플의 endpoint는 `tcp://127.0.0.1:<port>` 를 기본으로 사용한다.
- `inproc://`는 사용하지 않는다.
  - inproc은 callback이 동작하지 않는 경우가 있고, 실제 사용 환경과 다르다.
- port 할당은 OS ephemeral port를 사용한다 (bind to port 0 → 실제 port 획득).
- stream 샘플처럼 raw TCP client를 직접 연결하는 경우도 동일한 방식을 따른다.

## Sample Execution Topology Rules
- canonical sample의 기본 실행 형태는 프로세스 1개와 스레드 2개다.
- 한 스레드는 sender, publisher, client 같은 요청 측 역할을 맡고, 다른
  스레드는 receiver, subscriber, server, service 같은 수신 측 역할을 맡는다.
- 이 기본 형태를 쓰는 이유는 역할 분리를 분명하게 보여 주면서도 실행, 종료,
  검증 절차를 단순하게 유지하기 위해서다.
- 한 스레드에 양쪽 역할을 모두 넣는 방식은 원칙적으로 피한다.
  흐름 제어가 복잡해지고, 실제 사용 예보다 인위적인 코드가 늘어나기 쉽기
  때문이다.
- 프로세스를 둘 이상으로 나누는 방식도 canonical sample의 기본으로 쓰지
  않는다.
  실행 스크립트, 포트 전달, 종료 정리, 실패 처리까지 함께 복잡해지기 때문이다.
- 단일 스레드 샘플은 monitor처럼 한 흐름 안에서 의미가 유지되고 역할 분리가
  크게 중요하지 않은 경우에만 예외적으로 허용한다.
- 다중 프로세스 샘플은 canonical sample이 아니라 integration sample 또는
  end-to-end 검증 용도로만 둔다.
- `spot_recv_sample` 은 다른 canonical sample보다 단순한 local overview 예제로
  유지한다.
  기본적으로 프로세스 1개를 사용하고, 스레드는 1개 또는 2개를 사용할 수 있다.
  이 샘플의 목적은 spot 표면에서 publish 후 subscribe 결과를 확인하는 흐름을
  짧게 보여 주는 것이다.
  외부 publisher가 필요하면 publisher handle을 만들고, 그렇지 않으면 같은
  `Spot` 표면의 `publish()` 를 직접 사용한다.
  `dispatch_event` callback은 필요할 때 activation signal로 사용할 수 있지만,
  모든 바인딩에 강제하지 않는다.
- `spot_request_async_sample` 도 기본적으로 프로세스 1개와 스레드 2개를
  사용한다.
  한쪽은 async requester, 다른 쪽은 responder 역할을 맡고 request/reply는
  coroutine 또는 async completion 표면으로 확인한다.
- `stream_recv_sample` 과 `stream_packet_callback_sample` 도 기본적으로
  프로세스 1개와 스레드 2개를 사용한다.
  한쪽 스레드는 raw TCP client, 다른 쪽 스레드는 zlink STREAM endpoint 역할을
  맡는다.

## Sample Connection Handshake Rules
- 샘플에서 `sleep`만으로 readiness를 해결하는 방식은 허용하지 않는다.
- readiness 확인은 "무엇이 준비되어야 하는가"에 맞춰 가장 단순한 표면을 쓴다.
  - raw socket connect readiness:
    monitor event 또는 그와 동등한 readiness surface
  - service-layer readiness (`spot`, `discovery`, `registry query`):
    status snapshot, peer snapshot, bounded query retry,
    bounded request submit retry 중 하나
- TCP bind/connect 직후 raw socket 메시징을 시작하는 샘플은 가능하면 monitor API로
  connection readiness를 확인한다.
- 다만 service-layer 샘플은 monitor API만으로 모든 준비 상태를 설명하려고 하지
  않는다.
  service route, topology propagation, attachment visibility처럼 monitor로 직접
  드러나지 않는 조건은 서비스 표면의 snapshot, event, query, retry로 확인해도
  된다.
- handshake는 샘플 의미를 흐리지 않는 가장 짧은 형태여야 한다.
  readiness 확인이 샘플 본문보다 더 복잡해지면 과도한 handshake로 본다.
- 허용 패턴:
  - hard timeout이 있는 monitor wait
  - hard timeout이 있는 snapshot polling
  - hard timeout이 있는 query/request retry
  - helper로 감싼 bounded readiness wait
- 금지 패턴:
  - 이유 없는 고정 `sleep`
  - timeout 없는 busy-wait
  - readiness 근거 없이 반복하는 sleep loop
- 목표는 샘플 본문만 보고도 bind/connect 이후 왜 바로 send/publish/request 하지
  않는지 이해할 수 있게 만드는 것이다.

## Sample Runtime Verification Rules
- 모든 샘플은 실제 메시지를 주고받아 동작을 확인해야 한다.
- compile-only 예제로 끝나면 안 된다.
- 샘플은 최소한 아래 중 해당되는 항목을 검증해야 한다.
  - expected payload 수신
  - expected topic 수신
  - expected routing id 수신
  - spot send 경로 실제 수신
  - routed request -> spot reply async 경로 실제 완료
  - spot timer event 실제 발생
  - STREAM packet callback 실제 호출
  - spot dispatcher event callback 경유 recv 동작
  - monitor event 실제 수신
  - monitor `tryRecv` empty 경로
- 샘플은 성공 시 명확한 성공 출력 또는 zero exit code를 가져야 한다.
- 실패 시 예외 또는 non-zero exit code로 실패를 드러내야 한다.

## Sample Output Format Rules
- 모든 샘플은 동일한 출력 포맷 규칙을 사용한다.
- 출력은 실행 시 "메시징이 실제로 일어났음"을 사용자에게 명확히 보여야 한다.
- 패턴의 메시징 방향에 따라 출력 포맷이 다르다.
  - **bidirectional** (pair, dealer-router, stream): `send/recv` 표현
  - **one-way** (pubsub): `publish/subscribe` 표현
  - **composite** (spot recv): publish, send, timer 흐름을 순서대로 표현
  - **async request** (spot/request): request/reply completion 표현
- one-way 패턴에서 echo처럼 보이는 self-subscribe 샘플을 만들면 안 된다.
  one-way 패턴은 발행과 구독이 별개 행위임을 출력에서 드러내야 한다.
- 패턴별 출력 예시:

```text
# bidirectional (echo / request-reply)
[dealer-router/request-reply/async] send: "ping" -> recv: "pong"
[pair/recv] send: "hello-pair" -> recv: "hello-pair"
[dealer-router/recv] send: "ping" -> recv: "pong"
[stream/recv] send: "hello-stream" -> recv: "hello-stream"
[stream/packet-callback] send: "hello-stream" -> recv: "hello-stream"

# one-way (publish -> subscribe)
[pubsub/recv] publish: "prices/101.25" -> subscribe: "prices/101.25"

# composite (spot recv overview)
[spot/recv] publish: "room:lobby/hello-spot" -> recv: "room:lobby/hello-spot"
[spot/recv] send: "hello-spot-send" -> recv: "hello-spot-send"
[spot/recv] timer: "tick-1" -> recv: "tick-1"

# async request (spot)
[spot/request/async] request: "spot-ping" -> reply: "spot-pong"

# monitor
[monitor/recv] recv: "connection-ready"

# service layer (discovery + registry)
[discovery-registry] service: "sample" -> discovered
[registry-query] service: "sample" -> snapshot: found
```

- assert만으로 성공/실패를 판단하는 출력 없는 샘플은 허용하지 않는다.
- `ok`, `done` 같은 단순 성공 메시지만 출력하는 것도 불충분하다.
- 보낸 값과 받은 값이 콘솔에 명시적으로 나타나야 한다.

## Canonical Sample Profiles

### Pair Sample Profile
- 대상 샘플: `pair_recv_sample`
- topology: 프로세스 1개, 스레드 2개, `pair <-> pair`
- roles: 한쪽은 sender, 다른 쪽은 receiver
- fixed content: payload는 `"hello-pair"` 를 사용한다.
- execution order:
  - monitor readiness 확인
  - sender가 `"hello-pair"` 전송
  - receiver가 `recv` 로 `"hello-pair"` 수신
- verification:
  - receiver payload가 `"hello-pair"` 와 정확히 일치해야 한다.
- output example:
  - `[pair/recv] send: "hello-pair" -> recv: "hello-pair"`

### PubSub Sample Profile
- 대상 샘플: `pubsub_recv_sample`
- topology: 프로세스 1개, 스레드 2개, `publisher -> subscriber`
- roles: 한쪽은 publisher, 다른 쪽은 subscriber
- fixed content:
  - topic은 `"prices"`
  - payload는 `"101.25"`
- execution order:
  - subscriber가 `"prices"` subscription 설정
  - monitor readiness 확인
  - publisher가 `"prices/101.25"` publish
  - subscriber가 `recv` 또는 subscribe recv surface로 수신
- verification:
  - 수신 topic이 `"prices"` 여야 한다.
  - 수신 payload가 `"101.25"` 여야 한다.
- output example:
  - `[pubsub/recv] publish: "prices/101.25" -> subscribe: "prices/101.25"`

### DealerRouter Sample Profile
- 대상 샘플: `dealer_router_recv_sample`
- topology: 프로세스 1개, 스레드 2개, `dealer -> router -> dealer`
- roles: 한쪽은 dealer requester, 다른 쪽은 router responder
- fixed content:
  - request payload는 `"ping"`
  - reply payload는 `"pong"`
- execution order:
  - monitor readiness 확인
  - dealer가 `"ping"` 전송
  - router가 `recv` 로 `"ping"` 수신
  - router가 `"pong"` reply 전송
  - dealer가 `recv` 로 `"pong"` 수신
- verification:
  - request payload가 `"ping"` 이어야 한다.
  - reply payload가 `"pong"` 이어야 한다.
  - routed messaging에 필요한 routing 정보가 유효해야 한다.
- output example:
  - `[dealer-router/recv] send: "ping" -> recv: "pong"`

### RequestReply Async Sample Profile
- 대상 샘플: `request_reply_async_sample`
- topology: 프로세스 1개, 스레드 2개, `dealer -> router`
- roles: 한쪽은 async dealer requester, 다른 쪽은 router responder
- fixed content:
  - request payload는 `"ping"`
  - reply payload는 `"pong"`
- execution order:
  - monitor readiness 확인
  - dealer requester가 async request `"ping"` 시작
  - router responder가 request 수신 후 `"pong"` reply 전송
  - dealer requester가 async completion surface에서 결과 수신
- verification:
  - async completion 결과가 `"pong"` 이어야 한다.
  - 결과 확인은 `await`, `future.get`, `Task` 같은 async surface로만 한다.
- output example:
  - `[dealer-router/request-reply/async] send: "ping" -> recv: "pong"`

### Stream Recv Sample Profile
- 대상 샘플: `stream_recv_sample`
- topology: 프로세스 1개, 스레드 2개, `tcp client -> zlink stream server`
- roles: 한쪽은 raw TCP client, 다른 쪽은 zlink STREAM receiver
- fixed content: payload는 `"hello-stream"` 를 사용한다.
- execution order:
  - monitor readiness 확인
  - raw TCP client가 `"hello-stream"` 전송
  - STREAM endpoint가 `recv` 로 `"hello-stream"` 수신
- verification:
  - STREAM recv payload가 `"hello-stream"` 이어야 한다.
- output example:
  - `[stream/recv] send: "hello-stream" -> recv: "hello-stream"`

### Stream Packet Callback Sample Profile
- 대상 샘플: `stream_packet_callback_sample`
- topology: 프로세스 1개, 스레드 2개, `tcp client -> zlink stream server`
- roles: 한쪽은 raw TCP client, 다른 쪽은 zlink STREAM packet callback receiver
- fixed content: payload는 `"hello-stream"` 를 사용한다.
- execution order:
  - monitor readiness 확인
  - STREAM endpoint에 packet callback 등록
  - raw TCP client가 `"hello-stream"` 전송
  - packet callback이 `"hello-stream"` delivery 처리
- verification:
  - packet callback이 실제로 호출되어야 한다.
  - callback으로 전달된 payload가 `"hello-stream"` 이어야 한다.
- output example:
  - `[stream/packet-callback] send: "hello-stream" -> recv: "hello-stream"`

### Spot Sample Profile
- 대상 샘플: `spot_recv_sample`
- topology:
  - 프로세스 1개
  - 기본 흐름은 local `spot publish -> spot subscribe`
  - 외부 publisher가 필요하면 publisher handle에서 publish하고 Spot subscribe로
    수신을 확인할 수 있다.
- roles:
  - 하나의 `Spot` 이 publish 와 subscribe 양쪽을 담당한다.
  - 전용 ingress `PUB` 를 붙이는 경우에도 수신 확인은 `Spot` 표면에서만 한다.
- fixed content:
  - service id는 `"sample"`
  - topic은 `"room:lobby"`
  - publish payload는 `"hello-spot"`
  - send payload와 timer tick 값은 확장 예가 필요할 때만 추가한다.
- execution order:
  - 필요하면 subscription 또는 dispatch handler 등록
  - `publish "room:lobby/hello-spot"`
  - `Spot.subscribe()` 또는 그와 동등한 canonical subscribe recv surface로 수신
  - 필요하면 같은 샘플 안에 `send` 또는 timer round를 확장 예로 추가할 수 있다.
- verification:
  - publish 경로 payload가 `"hello-spot"` 이어야 한다.
  - 수신 확인은 `Spot` 의 canonical subscribe recv surface에서 해야 한다.
  - `dispatch_event` callback을 쓰는 바인딩은 activation signal로만 사용하고,
    실제 payload 확인은 여전히 subscribe recv surface에서 해야 한다.
  - send 또는 timer를 넣으면 그 경로도 같은 규칙으로 별도 확인한다.
- output example:
  - `[spot/recv] service: "sample" tick: 1 publish: "room:lobby/hello-spot" -> recv: "room:lobby/hello-spot"`
  - 확장 예:
  - `[spot/recv] service: "sample" tick: 1 send: "hello-spot-send" -> recv: "hello-spot-send"`
  - `[spot/recv] service: "sample" tick: 1 timer: "tick-1" -> next-round`

### Spot Request Async Sample Profile
- 대상 샘플: `spot_request_async_sample`
- topology:
  - 프로세스 1개, 스레드 2개
  - binding public surface에 맞춰
    `router requester -> spotnode/router -> spot responder` 또는
    `spot requester -> channel responder`
- roles:
  - 한쪽은 async requester
  - 다른 쪽은 request를 받아 reply를 돌려주는 responder
- fixed content:
  - request payload는 `"spot-ping"`
  - reply payload는 `"spot-pong"`
- execution order:
  - 필요한 endpoint 또는 channel 준비를 확인
  - requester가 바인딩의 canonical async request surface
    (`request_to_spot`, `request_channel`, 그와 동등한 공개 표면)로
    `"spot-ping"` 시작
  - responder가 request 수신 후 `"spot-pong"` reply 전송
  - requester가 coroutine 또는 async completion surface에서 결과 수신
- verification:
  - async completion 결과가 `"spot-pong"` 이어야 한다.
  - 결과 확인은 `await`, `future.get`, `Task` 같은 async surface로만 한다.
- handshake note:
  - 이 샘플은 raw socket monitor만으로 service readiness를 보장하려고 하지
    않는다.
  - peer/status snapshot 또는 bounded submit retry를 사용할 수 있다.
  - 목적은 spot request/reply 의미를 보여 주는 것이지 복잡한 readiness
    choreography를 보여 주는 것이 아니다.
- output example:
  - `[spot/request/async] request: "spot-ping" -> reply: "spot-pong"`

### Discovery Registry Sample Profile
- 대상 샘플: `discovery_registry_sample`
- topology:
  - 프로세스 1개, 스레드 2개 또는 3개
  - `registry server <- service provider`
  - `discovery client -> registry server`
- roles:
  - registry server는 등록 상태를 유지한다.
  - service provider는 `service id = "sample"` 을 등록하거나 해제한다.
  - discovery client는 상태 변화를 event stream으로 관찰한다.
- fixed content:
  - service id는 `"sample"`
  - discovered 상태를 확인한다.
  - removal 흐름을 다루면 removed 상태를 확인한다.
- execution order:
  - registry bind
  - discovery client connect
  - service provider register
  - discovery client가 discovered event 수신
  - 필요하면 service provider unregister
  - 필요하면 discovery client가 removed event 수신
- verification:
  - register 뒤에 discovery event가 실제로 도착해야 한다.
  - event 안의 service id가 `"sample"` 이어야 한다.
  - unregister 흐름을 포함하면 removal event도 확인해야 한다.
- handshake note:
  - 이 샘플은 topology 전파를 기다리는 bounded event wait 또는 bounded polling을
    허용한다.
  - 단순 socket connect handshake보다 "service가 보였는가"가 더 중요한
    readiness다.
- output example:
  - `[discovery-registry] service: "sample" -> discovered`
  - `[discovery-registry] service: "sample" -> removed`

### Registry Query Sample Profile
- 대상 샘플: `registry_query_sample`
- topology:
  - 프로세스 1개, 스레드 2개 또는 3개
  - `registry server <- service provider`
  - `registry query client -> registry server`
- roles:
  - registry server는 등록 상태를 유지한다.
  - service provider는 `service id = "sample"` 을 등록한다.
  - registry query client는 현재 상태를 snapshot으로 조회한다.
- fixed content:
  - service id는 `"sample"`
  - snapshot result는 `found` 를 사용한다.
- execution order:
  - registry bind
  - service provider register
  - registry query client connect
  - query request 전송
  - snapshot response 수신
- verification:
  - snapshot 안에 `service id = "sample"` entry가 있어야 한다.
  - query 결과가 비어 있지 않아야 한다.
- handshake note:
  - 첫 query가 아직 실패하거나 빈 결과를 돌려줄 수 있으므로 bounded retry를
    허용한다.
  - 샘플 목적은 snapshot query contract를 보여 주는 것이므로 이 readiness 처리는
    짧고 결정적으로 유지해야 한다.
- output example:
  - `[registry-query] service: "sample" -> snapshot: found`

### Monitor Sample Profile
- 대상 샘플: `monitor_recv_sample`
- topology: 단일 흐름 또는 예외적 단일 스레드 허용
- roles: monitor consumer가 readiness/state event를 읽는다.
- fixed content:
  - recv event는 `"connection-ready"`
  - 바인딩이 non-blocking monitor receive를 공개하면 `tryRecv` 결과는 `empty`
- execution order:
  - monitor attach 또는 enable
  - connection readiness 유도
  - `recv` 로 `"connection-ready"` 확인
  - 가능하면 `tryRecv` 로 empty 경로 확인
- verification:
  - 첫 monitor event가 `"connection-ready"` 이어야 한다.
  - non-blocking monitor receive가 있으면 이후 `tryRecv` 는 empty 여야 한다.
- output example:
  - `[monitor/recv] recv: "connection-ready"`
  - 확장 예:
  - `[monitor/recv] recv: "connection-ready" -> tryRecv: empty`

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
| spot-recv | composite | `"room:lobby"` | service id: `"sample"`, publish: `"hello-spot"`, send: `"hello-spot-send"`, timer: `"tick-1"` |
| spot-request | async request | — | service id: `"sample"`, request: `"spot-ping"`, reply: `"spot-pong"` |
| actor-room-server | dispatch | — | actor id: `"room-player-1"`, join: `"enter-room"`, accept: `"accepted"`, stream payload: `"move:north"` |
| actor-gateway-relay | stream relay | — | actor id: `"play-session-actor"`, join: `"join-play"`, relay payload: `"client-input"` |
| actor-single-player-queue | queue order | — | actor id: `"single-player"`, join 시퀀스: `"join-first"` → leave → 큐잉 `"before"`/`"between"` → rejoin `"join-second"`, accept: `"accepted"` |

| monitor | event plane | — | recv: `"connection-ready"` |
| discovery-registry | service plane | — | service id: `"sample"`, state: `discovered`, remove: `removed` |
| registry-query | service plane | — | service id: `"sample"`, snapshot: `found` |

> Actor 샘플은 단순 send/recv가 아니라 **시나리오 시퀀스**(join → 처리 → leave/relay/queue)를
> 보인다. 위 값은 그 시퀀스의 고정 식별자·payload이며, 언어가 달라도 같은 값과
> 같은 순서를 따른다. 자세한 시나리오 의미는 [Actor Sample Scenario
> Rules](#actor-sample-scenario-rules)를 본다.

## Sample Coverage Expectations
- 각 표면은 canonical sample 세트를 공식 샘플 표면으로 유지한다.
- request-reply wrapper를 구현한 바인딩 표면:
  - `request_reply_async_sample`
- `core/samples/` (request-reply):
  - request-reply wrapper sample을 포함하지 않는다.
- direct recv 계열:
  - PAIR 또는 동등한 기본 send/recv
  - PUB/SUB 또는 동등한 topic publish/subscribe
  - ROUTER/DEALER 또는 동등한 routed messaging
  - STREAM direct recv
- packet callback 계열:
  - STREAM packet callback
- monitor 계열:
  - readiness/state event 확인 샘플
- service/spot 계열이 있는 표면은 `spot_recv_sample` 을 canonical에 포함한다.
- `spot_recv_sample` 은 `Spot` 의 canonical recv surface를 사용해 local
  publish/subscription 흐름을 확인한다.
- `spot_recv_sample` 은 spot event and recv overview 샘플로서 publish, send,
  timer 흐름을 함께 포함할 수 있다.
- `spot` request/reply async surface를 제공하는 바인딩 표면은
  `spot_request_async_sample` 을 canonical에 포함한다.
- Discovery + Registry를 구현한 표면은 `discovery_registry_sample` 을
  canonical에 포함한다.
  포함한다.
- Actor dispatch를 구현한 표면은 세 Actor sample을 canonical에 포함한다.

## Stream Socket Policy
- STREAM socket은 direct recv 방식과 packet callback 방식을 둘 다 지원해야
  한다.
- 따라서 각 표면은 STREAM에 대해 다음 둘을 모두 가져야 한다.
  - blocking/non-blocking direct receive surface
  - packet callback receive surface
- STREAM sample도 recv 버전과 packet callback 버전을 개별 파일로 제공하는 것을
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

## Runtime-Shared Language Sample Rules
- 일부 언어는 새 네이티브 바인딩 없이 기존 바인딩의 런타임·패키지를 그대로 쓴다.
  이를 **런타임 공유 언어**로 부른다.
  - **Kotlin** — JVM 런타임. 기존 java 바인딩(`systems.zlink.*`)을 그대로 import.
  - **JavaScript** — Node 런타임. 기존 node 바인딩(`@zlink-systems/zlink`)을 require.
- 런타임 공유 언어 샘플은 **별도 디렉토리**에 둔다.
  - `bindings/kotlin/samples/`, `bindings/javascript/samples/`.
  - 네이티브 바인딩 소스(`src/`)는 만들지 않는다. 샘플 빌드·실행에 필요한 최소
    설정(kotlin `build.gradle`, javascript 러너)만 둔다.
- 런타임 공유 언어 샘플도 canonical sample 세트와 Cross-Language Uniformity
  Rules를 **그대로 따른다**. 즉 같은 시나리오·식별자·payload·순서·출력을 쓴다.
- 코드 표현만 언어 관용을 따른다(Kotlin coroutine·null-safety, JavaScript
  no-types 등). 값과 흐름은 통일한다.
- 런타임 공유 언어가 canonical 샘플을 추가할 때, 그 런타임의 네이티브 바인딩
  샘플(Kotlin↔java, JavaScript↔node)의 값·순서를 그대로 가져온다.

## Cross-Language Uniformity Rules
- 같은 canonical 샘플은 모든 언어에서 **같은 시나리오·같은 식별자·같은 순서·같은
  출력**을 따른다. 언어별로 자유롭게 다른 값을 쓰지 않는다.
- `Sample Message Content Rules` 표의 값과 `Canonical Sample Profiles`의 fixed
  content가 그 단일 기준이다. 정책 값과 샘플 구현이 다르면 **샘플을 정책에
  맞춘다**(정책이 blueprint).
- 통일 대상에는 다음이 포함된다.
  - actor id, service id, topic, payload, 시퀀스 순서
  - 출력 라인 포맷(`[pattern] ... -> ...`)
- 언어 관용에 따라 다른 것은 허용한다(코드 스타일, 에러 처리, 소유권 표현, 메시지
  생성 팩토리 이름 등). **값과 흐름은 통일, 표현은 언어 관용.**
- 새 언어 바인딩이 canonical 샘플을 추가할 때, 기존 언어 샘플의 값·순서를 그대로
  가져온다. 새 값을 임의로 만들지 않는다.

## Documentation Source Rules
- 샘플은 가이드 문서 코드의 **단일 출처**다. 가이드(`doc/guide/`)의 코드 예제는
  샘플과 1:1로 대응하며, 손으로 베껴 쓴 코드가 샘플 API와 어긋나면 안 된다.
- 7언어 코드 탭 문서(예: actor 가이드)의 각 탭은 그 언어 canonical 샘플의 실제
  흐름을 따른다. 샘플에 없는 API를 문서가 지어내면 안 된다.
- 문서가 샘플 코드를 인용할 때, 테스트성 동기화·검증 코드(assert, channel,
  mutex, 고정 timeout 등)는 가독성을 위해 생략할 수 있으나, **핵심 메시징 API
  호출과 그 순서는 샘플과 같아야** 한다.
- 자세한 문서 측 규칙은 [문서화 원칙](../../../../doc/principal/documentation/documentation-principles.ko.md)을
  본다.

## Snippet Extraction Rules (Recommended)
- 가이드 탭 문서로 자동 추출할 핵심 흐름을 샘플에 표시하려면, 명명된 스니펫
  영역을 둔다.
  - 형식: `region guide:<snippet-name>` 시작, `endregion guide:<snippet-name>` 종료
    (언어별 주석 문법으로).
  - 예: C#은 `// #region guide:actor-create` / `// #endregion`, Rust는
    `// region guide:actor-create` / `// endregion`.
- 스니펫 이름은 모든 언어에서 같은 흐름에 같은 이름을 쓴다(예: `actor-create`,
  `actor-join`, `actor-recv`, `actor-leave`).
- 스니펫 영역 안의 코드는 그 자체로 핵심 메시징 흐름이어야 하며, 테스트 동기화
  코드는 영역 밖에 둔다.
- 이 규칙은 현재 `Recommended`다. 도입 전까지는 `Documentation Source Rules`의
  1:1 대응으로 정합성을 유지한다.

## Sample Verification Requirements
- 새 canonical sample 추가 시 다음을 같이 확인해야 한다.
  - 개별 샘플 단독 실행 성공
  - 전체 샘플 실행 스크립트 포함
  - 전체 샘플 실행 스크립트에서 성공
- sample review 시 다음을 확인한다.
  - canonical sample 세트가 모두 존재하는가
  - recv/async/packet-callback/monitor 분류가 정책에 맞는가
  - recv/packet-callback 버전이 필요한 경우 개별 파일로 분리되어 있는가
  - `spot_recv_sample` 과 `spot_request_async_sample` 이 분리되어 있는가
  - `spot_recv_sample` 예외가 문서화된 범위 안에서만 사용되는가
  - canonical API만 사용하는가
  - 핵심 로직이 helper 뒤에 숨지 않았는가
  - 실제 메시징을 하고 결과를 확인하는가
  - 전체 샘플 실행 스크립트에 포함되어 있는가
  - **모든 언어가 같은 시나리오·식별자·payload·순서·출력을 쓰는가** (Cross-Language
    Uniformity)
  - actor 샘플이 [Actor Sample Scenario Rules](#actor-sample-scenario-rules)의
    고정 시나리오를 따르는가
  - 가이드 문서가 인용하는 코드가 이 샘플의 실제 API와 1:1로 일치하는가
    (Documentation Source)
  - 불필요한 `lock` 기반 대기를 사용하지 않았는가
