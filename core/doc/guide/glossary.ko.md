# Core 용어

- **Context** — I/O thread와 socket resource의 수명을 관리하는 최상위 handle이다.
- **Socket pattern** — peer 선택, 송수신 방향과 message 분배 규칙을 정의한다.
- **Routing ID** — ROUTER와 STREAM에서 연결된 peer를 식별하는 byte sequence다.
- **Multipart message** — 하나의 논리적 message를 구성하는 하나 이상의 part다.
- **HWM** — queue에 유지할 byte를 제한해 backpressure를 적용하는 값이다.
- **Backpressure** — downstream이 처리할 수 없을 때 sender의 추가 제출을 제한하는 동작이다.
- **Poller** — socket, file descriptor와 generic timer의 readiness를 함께 기다리는 handle이다.
- **Socket monitor** — raw socket의 transport와 protocol event를 보고하는 별도 handle이다.
- **ZMP** — zlink socket 간 handshake와 message frame을 전달하는 wire protocol이다.
- **STREAM** — 외부 byte-stream peer와 통신하는 raw socket pattern이다.
