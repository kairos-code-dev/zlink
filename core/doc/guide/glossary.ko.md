[한국어](glossary.ko.md)

[가이드 목록](README.ko.md)

# 용어집

zlink 가이드 전반에서 쓰는 용어를 모았다. 개념의 정식 정의는 각 항목이 가리키는
챕터에 있고, 여기서는 한 줄 정의와 링크만 둔다.

## 메시징 기본

- **Context** — 프로세스의 런타임 진입점. I/O 스레드를 소유하고 모든 소켓·서비스를
  생성한다. 보통 프로세스당 하나다. ([02 Core API](02-core-api.ko.md))
- **Socket** — 메시징 엔드포인트. 8종(PAIR / PUB · SUB / XPUB · XSUB / DEALER ·
  ROUTER / STREAM)이 있다. ([03-0 소켓 패턴](03-0-socket-patterns.ko.md))
- **Transport** — 바이트가 흐르는 하부 계층. `tcp` / `ipc` / `inproc` / `ws` /
  `wss` / `tls`. ([04 Transport](04-transports.ko.md))
- **Message** — 하나의 페이로드 프레임. 보낼 때 소비된다(zero-copy 이동).
  ([09 메시지 API](09-message-api.ko.md))
- **Multipart** — 여러 프레임으로 이뤄진 한 메시지. **전체가 함께 전달되거나
  전체가 실패**한다(원자성). ([09 메시지 API](09-message-api.ko.md))
- **Routing ID** — node·session·spot 주소에 쓰는 1~255바이트 바이너리 안전 값.
  ROUTER·STREAM이 이 값을 전제로 동작한다. (Actor는 `zlink_actor_ref_t`의 `actor_id`
  문자열 + `node_rid` 조합으로 식별한다.) ([08 Routing ID](08-routing-id.ko.md))

## 흐름 제어

- **HWM (High Water Mark)** — 소켓 큐의 상한. 송신 HWM에 도달하면 블로킹 또는
  backpressure 신호가 발생한다. ([12 소켓 옵션](12-socket-options.ko.md),
  [신뢰성](reliability.ko.md))
- **Auto HWM** — 연결 수에 맞춰 큐 상한을 자동 조정하는 기본 동작. 프로필은
  `COMPACT` / `LOW_LATENCY` / `BALANCED`(기본) / `THROUGHPUT`.
  ([10 성능](10-performance.ko.md))
- **Backpressure** — 수신 측 처리 속도를 넘겨 밀어 넣지 못하도록 흐름을 조절하는
  메커니즘. zlink에서는 송신이 `BACKPRESSURED`를 반환하는 형태로 나타난다.
  ([신뢰성](reliability.ko.md))
- **LINGER** — 소켓을 닫을 때 미전송 메시지를 얼마나 기다릴지 정하는 시간.
  ([12 소켓 옵션](12-socket-options.ko.md))

## 서비스 계층

- **SPOT** — 동적으로 생성·소멸되는 논리 단위(room·stage·zone 등)로 메시지를
  라우팅하는 추상 단위. 한 단위로 들어온 메시지를 **단일 실행 큐로 직렬 처리**한다.
  ([07-3 SPOT](07-3-spot.ko.md))
- **MeshNode** — MeshName 하나로 mesh에 참여하는 노드. Spot·Actor의 수명과
  transport를 소유한다. ([07-3 SPOT](07-3-spot.ko.md))
- **Entry Spot** — MeshNode가 항상 갖는 기본 Spot. Actor가 처음 속하는 잘 알려진
  지점. ([07-4 Actor](07-4-actor.ko.md))
- **Actor** — Spot에 합류해 메시지를 받는 상태 보유 엔티티. 세션 위치와 무관하게
  같은 엔티티로 이어진다(재접속 이전성). ([07-4 Actor](07-4-actor.ko.md))
- **Capability** — 한 노드(channel·spot 등)가 외부에 노출하는 역할 단위(server·
  subscriber·publisher 등). ([07-0 서비스](07-0-services.ko.md))
- **Channel** — 논리 이름을 키로 메시지를 주고받는 단위. 서비스 계층 호출의 기준.
  ([07-0 서비스](07-0-services.ko.md))

## 내부 / 프로토콜

- **ZMP** — zlink의 wire protocol. 8바이트 고정 헤더(MAGIC `0x5A`, VERSION `0x01`)
  위에 프레임을 싣는다. ZMTP(ZeroMQ 프로토콜)와 호환되지 않는 별개 프로토콜이다.
  ([ZMP 프로토콜 레퍼런스](zmp-protocol.ko.md))
- **VSM (Very Small Message)** — 작은 메시지(64-bit에서 41바이트 이하)를 힙 할당 없이
  메시지 객체 안에 inline 저장하는 **메모리 최적화**. wire 형식은 그대로다.
  ([설계 근거](design-rationale.ko.md))
- **YPipe** — 락 없이 CAS 연산으로 구현한 스레드 간 FIFO 큐.
  ([설계 근거](design-rationale.ko.md))
- **Proactor** — I/O 완료 이벤트를 핸들러로 전달하는 비동기 설계(Boost.Asio 기반).
  ([설계 근거](design-rationale.ko.md))
- **I/O 스레드** — Context가 소유하는 백그라운드 스레드. raw socket receive 콜백이
  여기서 실행된다(SPOT dispatch 콜백은 별도 SPOT dispatch worker에서 실행).
  ([11 스레드 안전성](11-thread-safety.ko.md))
