# `recv-with-callback` 계획 문서

이 디렉터리는 `callback-to-recv` 축소 방향을 되돌려, `recv`와 `callback`을
다시 공통 규칙으로 정렬하는 계획 문서를 모은다.

이번 계획의 핵심은 "소켓별 예외 매트릭스" 대신 아래 두 규칙으로 public contract를
설명 가능하게 만드는 것이다.

중요한 점은 `receive_callback`과 `send_ready`를 하나의 callback mode로 묶지
않는다는 점이다. 둘은 별개 축이며, 영향 범위도 다르다.

- receive callback이 붙은 subject는 sync `recv` 계열 호출과 data-plane
  `POLLIN` poller 사용이 `EBUSY`로 막힌다.
- `zlink_send_ready_handler()`가 붙은 subject는 writable readiness를 callback으로만
  노출하고 data-plane `POLLOUT` poller 사용이 `EBUSY`로 막힌다.

이번 디렉터리에서 고정하는 기본 방향은 아래와 같다.

- receive surface
  - raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway`는 multipart callback
    (`zlink_recv_handler`)을 다시 연다.
  - raw `SUB`, `XSUB`, `SPOT`, `SPOT node`는 topic-aware callback
    (`zlink_subscribe_handler`)을 다시 연다.
  - socket monitor, service monitor는 기존 dual-mode를 유지한다.
- writable readiness surface
  - send-capable subject는 `zlink_send_ready_handler()`를 공통 규칙으로 연다.
  - send-ready callback이 활성인 동안에는 `poller ZLINK_POLLOUT`를 data-plane
    writable signal로 쓰지 않는다.
  - receive callback 사용 여부와 무관하게 send-ready는 독립적으로 attach할 수
    있다.
- perf canonical lane
  - single perf는 callback 모드만 테스트한다.
  - multi perf는 recv 모드만 테스트한다.
  - single의 dual-mode 예외는 `SPOT`만 둔다.
  - multi의 dual-mode 예외는 `SPOT`, `STREAM`만 둔다.
  - monitor는 perf pattern이 아니며, 관련 검증은 모두 callback 기준으로 고정한다.

이번 계획의 기본 가정:

- 이미 public ABI에서 사라진 별도 callback 함수는 v1 롤백 범위에 넣지 않는다.
  예를 들어 raw `XPUB` subscription-event callback 전용 ABI를 새로 부활시키지는
  않는다.
- 대신 현재 남아 있는 public entrypoint를 다시 넓게 열고, 공통 errno/poller
  규칙을 명확히 하는 쪽을 우선한다.
- 설계 원칙은 POSD 기준으로 "지원 조합 표"를 줄이고 "설명 가능한 규칙"을 늘리는
  것이다.

문서 목록:

- [core-surface-restoration-plan.ko.md](core-surface-restoration-plan.ko.md)
  - `core/include/`, `core/src/`, `core/tests/` 기준 public callback/recv
    contract 복원 계획
- [perf-lane-realignment-plan.ko.md](perf-lane-realignment-plan.ko.md)
  - `core/perf/`와 `doc/perf/`를 single=callback, multi=recv canonical lane으로
    정렬하는 계획
- [regression-and-doc-alignment-plan.ko.md](regression-and-doc-alignment-plan.ko.md)
  - 기존 축소 문서, 회귀 테스트, API/guide/perf 문서를 새 contract로 갈아타는
    순서와 완료 기준
