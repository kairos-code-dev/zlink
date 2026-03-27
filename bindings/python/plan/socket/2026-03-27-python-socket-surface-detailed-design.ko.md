# Python Socket Surface 상세 설계

작성일: 2026-03-27
대상: `bindings/python`
기준 문서:
- `bindings/cpp/plan/socket/2026-03-26-cpp-socket-surface-detailed-design.ko.md`
- `bindings/python/src/zlink/_core.py`
- `bindings/python/src/zlink/__init__.py`
- `bindings/python/tests/**`

## 1. 목적

이 문서는 `bindings/python`의 socket 계층 public surface를 재정의한다.

목표는 세 가지다.

- 사용자가 socket 타입별로 허용된 행위를 클래스 이름만 보고 이해하게 만들 것
- ctypes/native ownership, option dispatch, callback pinning, multipart helper 같은
  공통 메커니즘은 하나의 깊은 공통 모듈에 모아 change amplification을 줄일 것
- 기존 `Socket(ctx, SocketType.*)` 사용자 코드를 한 번에 깨지 않으면서도,
  최종적으로는 타입별 facade 중심 surface로 이동시킬 것

즉, Python에서도 타입별 클래스를 늘리되 구현을 각 클래스에 복제하지 않는다.
공통 구현은 한곳에 두고, concrete facade는 "허용된 API만 여는 surface 제한자"로
사용한다.

## 2. 현재 문제 요약

현재
[`_core.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_core.py)
의 `Socket` 하나가 아래 책임을 모두 가진다.

- native socket handle 생성/소유/종료
- bind/connect/unbind/disconnect lifecycle
- discovery attach
- raw `send`, `send_multipart`, `recv_message`, `recv_multipart`, `recv_into`
- topic `publish`, `recv_topic_message`, `subscribe`, `unsubscribe`
- callback registration
- router/dealer/pub/sub/stream option domain dispatch
- legacy `recv(size)` queue emulation
- monitor open 진입점

이 구조의 문제는 다음과 같다.

- public surface만 봐서는 어떤 socket이 어떤 메서드를 쓰는지 알기 어렵다.
- `publish()`와 `send()`가 같은 클래스에 섞여 semantic boundary가 흐려진다.
- option domain 메서드가 모든 socket에 다 열려 있어 unsupported 동작이 runtime으로만
  드러난다.
- `Socket`이 공통 심부 모듈인지, 사용자 facade인지, compat wrapper인지 역할이
  동시에 섞여 있다.
- 새 타입별 API를 추가할수록 `_core.py`의 조건 분기와 설명 비용이 계속 커진다.

POSD 관점에서 보면 지금 구조는 깊은 모듈 하나를 가진 것처럼 보이지만, 실제로는
"공통 메커니즘"과 "타입 의미"가 한 표면에 얽혀 있어서 사용자가 알아야 할 정보량이
많다. 이번 작업은 구현을 잘게 나누는 것이 아니라, 사용자에게 보이는 개념 경계를
줄이기 위한 분리다.

## 3. 설계 원칙

- socket 타입별 클래스는 새 구현체가 아니라 제한된 facade다.
- native handle ownership, endpoint lifecycle, option raw dispatch,
  callback pinning, monitor attach는 공통 심부 모듈에 둔다.
- raw message transport와 topic publish/subscribe를 public API에서 분리한다.
- `send/recv`와 `publish/recv_topic_message`를 같은 타입에 함께 열지 않는다.
- unsupported 동작은 문서 설명이나 runtime 오류보다 surface 제한을 우선한다.
- legacy generic `Socket`은 즉시 삭제하지 않고 compat entry point로 축소한다.
- 새 facade가 늘어나더라도 `_ffi.py`와 native ABI는 그대로 재사용한다.
- Python답게 out-parameter 래퍼를 public에 노출하지 않고, ownership이 있는 결과는
  `Received*` aggregate로 유지한다.
- borrow/copy, callback lifetime, close semantics는 메서드 이름과 클래스 책임만으로
  설명 가능해야 한다.

## 4. native 기준 확정 범위

이번 설계의 raw socket type 범위는 최신 `core`가 실제 제공하는 아래 8종으로
고정한다.

- `PAIR`
- `PUB`
- `SUB`
- `DEALER`
- `ROUTER`
- `XPUB`
- `XSUB`
- `STREAM`

제외 타입:

- `PUSH`
- `PULL`
- `SCATTER`
- `GATHER`
- `REQ`
- `REP`

제외 이유:

- 최신
  [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)
  의 canonical raw socket type이 아니다.
- Python에서만 가상 facade를 만들면 shallow wrapper와 문서 복잡도만 늘어난다.

## 5. 최종 계층 구조

```text
_SocketHandle
  ^
  |
_BaseSocket
  ^
  +-- Socket
        ^
        +-- MessageSocket
        |     +-- PairSocket
        |     +-- DealerSocket
        |     +-- RouterSocket
        |     +-- StreamSocket
        |
        +-- PublisherSocket
        |     +-- PubSocket
        |     +-- XPubSocket
        |
        +-- SubscriberSocket
              +-- SubSocket
              +-- XSubSocket
```

정책:

- `_SocketHandle`은 최소 ownership wrapper다.
- `_BaseSocket`은 공통 동작을 제공하는 실제 깊은 모듈이다.
- `Socket`은 public compat base class다.
- `MessageSocket`, `PublisherSocket`, `SubscriberSocket`는 의미 단위 facade다.
- concrete type은 socket type 고정과 타입별 option 노출만 담당한다.
- `Socket`은 직접 사용할 권장 surface가 아니라, 예전 생성 습관을 받아 새 concrete
  facade를 돌려주는 compat 생성 진입점이자 최소 공통 상위 타입으로 남긴다.

## 6. 모듈 배치

최종 파일 배치는 아래를 목표로 한다.

- `src/zlink/_socket_base.py`
- `src/zlink/_socket_types.py`
- `src/zlink/_core.py`
- `src/zlink/__init__.py`

역할:

- `_core.py`
  - low-level helper의 현재 소유 위치를 유지
  - `Context`
  - `Message`
  - `Received*`
  - socket 계층이 재사용하는 helper와 결과 타입 유지
- `_socket_base.py`
  - `_core.py`의 helper를 재사용하는 socket 공통 계층
  - `_SocketHandle`
  - `_BaseSocket`
  - `Socket`
  - `MessageSocket`
  - `PublisherSocket`
  - `SubscriberSocket`
- `_socket_types.py`
  - `PairSocket`
  - `DealerSocket`
  - `RouterSocket`
  - `StreamSocket`
  - `PubSocket`
  - `SubSocket`
  - `XPubSocket`
  - `XSubSocket`
  - compat dispatch map
- `__init__.py`
  - `Socket`을 `_socket_base.py`에서 import해 compat export로 유지
  - 새 concrete socket class export

추가 정책:

- 현재 `Socket` 구현을 새 파일로 옮긴 뒤 이름만 나누는 식으로 끝내지 않는다.
- `_core.py`는 "메시지/컨텍스트/공통 결과 타입" 중심으로 축소한다.
- generic socket 생성 분기는 한 곳에만 둔다.
- 새 예제와 새 테스트는 `Socket(ctx, SocketType.X)` 대신 concrete class 사용을
  기본으로 한다.
- 이번 refactor 범위에서는 `_raise_last_error`, buffer/msg helper,
  `Received*`, `_ReceivedPartsOwner`를 `_core.py`에 둔다.
- `_socket_base.py`와 `_socket_types.py`는 `_core.py`를 import할 수 있지만,
  `_monitor.py`, `_poller.py`, `_spot.py`, `_discovery.py`는 새 socket 모듈을
  직접 import하지 않는다.
- `_core.py`가 socket 계층을 재노출하기보다, `__init__.py`가 socket 계층을
  조립하는 방향을 우선한다. 이것이 실제 구현의 import cycle 방지 규칙이다.

## 7. 클래스별 책임

### 7.1 `_SocketHandle`

역할:

- raw native socket handle 소유
- `close()` idempotency 제공
- own / borrowed handle 구분

고정 인터페이스:

```python
class _SocketHandle:
    def __init__(self, handle, own=True): ...

    @classmethod
    def _from_handle(cls, handle, own=False): ...

    @property
    def closed(self) -> bool: ...

    def close(self) -> None: ...
```

제약:

- 일반 사용자가 직접 import할 public surface가 아니다.
- `Context`에서 새 socket 생성 로직은 이 클래스가 아니라 상위 facade가 맡는다.

### 7.2 `_BaseSocket`

역할:

- 공통 lifecycle API
- 공통 endpoint API
- discovery attach
- 공통 option set/get
- 타입별 option domain 구현 보관
- recv/send callback 공통 처리
- monitor open 진입점
- routing id / TLS helper 공통 처리

고정 인터페이스 초안:

```python
class _BaseSocket(_SocketHandle):
    socket_type: SocketType

    def bind(self, endpoint: str) -> None: ...
    def connect(self, endpoint: str) -> None: ...
    def unbind(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...

    def attach_discovery(self, discovery) -> None: ...
    def open_monitor(self, events=MonitorEvent.ALL): ...

    def set_option(self, option: int, value) -> None: ...
    def get_option(self, option: int, size: int = 256): ...

    def set_routing_id(self, routing_id) -> None: ...
    def get_routing_id(self) -> bytes: ...

    def on_send_ready(self, handler) -> None: ...
    def set_send_ready_handler(self, handler) -> None: ...

    def _set_router_option(self, option, value) -> None: ...
    def _get_router_option(self, option, size: int = 256): ...
    def _set_dealer_option(self, option, value) -> None: ...
    def _set_pub_option(self, option, value) -> None: ...
    def _get_pub_option(self, option, size: int = 256): ...
    def _set_sub_option(self, option, value) -> None: ...
    def _get_sub_option(self, option, size: int = 256): ...
    def _set_stream_option(self, option, value) -> None: ...
    def _get_stream_option(self, option, size: int = 256): ...
```

제약:

- `_BaseSocket`은 public export하지 않는다.
- `_BaseSocket`에는 `send`, `recv_message`, `publish`, `recv_topic_message`,
  `subscribe`를 public으로 두지 않는다.
- data-plane 동작은 하위 facade가 의미에 맞게 노출한다.

### 7.3 `Socket`

역할:

- public compat base class
- 공통 lifecycle / option / monitor / callback API 보유
- generic constructor 진입점 유지
- legacy `recv(size)`만 compat 메서드로 유지

고정 인터페이스 초안:

```python
class Socket(_BaseSocket):
    def __new__(cls, context, sock_type=None): ...

    def recv(self, size: int, flags: int = 0) -> bytes: ...
```

생성 규칙:

- `cls is Socket`일 때만 `sock_type` 기반 concrete facade dispatch를 수행한다.
- `PairSocket(ctx)` 같은 concrete 생성에서는 dispatch를 거치지 않는다.
- concrete facade는 모두 `Socket`의 하위 타입으로 둬서
  `isinstance(sock, Socket)` 호환을 유지한다.

제약:

- `Socket`은 새 코드에서 권장되는 직접 사용 타입이 아니다.
- `Socket`에는 `send`, `publish`, `recv_topic_message` 같은 의미별 data-plane API를
  다시 모아두지 않는다.

### 7.4 `MessageSocket`

역할:

- raw transport 계층의 canonical facade
- `send`, `send_multipart`, `recv_message`, `recv_multipart`, `recv_into`
- recv callback surface

고정 인터페이스 초안:

```python
class MessageSocket(Socket):
    def send(self, data, flags: int = 0): ...
    def send_multipart(self, parts, flags: int = 0): ...
    def recv_message(self, flags: int = 0) -> ReceivedMessage: ...
    def recv_multipart(self, flags: int = 0) -> ReceivedMultipart: ...
    def recv_into(self, buffer, flags: int = 0) -> int: ...
    def on_receive(self, handler) -> None: ...
```

정책:

- `MessageSocket`은 `on_receive()`를 canonical 이름으로 제공하고,
  `set_recv_handler()`는 deprecated alias로 유지한다.
- `recv(size)` legacy API는 `MessageSocket`에도 두지 않는다.
- `recv(size)`는 compat `Socket`에서만 deprecated 유지한다.

### 7.5 `PublisherSocket`

역할:

- topic publish 계층 facade
- `publish`
- send-ready callback

고정 인터페이스 초안:

```python
class PublisherSocket(Socket):
    def publish(self, topic, payload, flags: int = 0): ...
    def on_send_ready(self, handler) -> None: ...
```

정책:

- `PublisherSocket`에는 raw `send`를 두지 않는다.
- `PUB`와 `XPUB`의 공통 topic publish 의미만 연다.

### 7.6 `SubscriberSocket`

역할:

- topic subscribe 계층 facade
- `subscribe`, `unsubscribe`
- `recv_topic_message`
- subscribe callback

고정 인터페이스 초안:

```python
class SubscriberSocket(Socket):
    def subscribe(self, topic) -> None: ...
    def unsubscribe(self, topic) -> None: ...
    def recv_topic_message(self, flags: int = 0) -> ReceivedTopicMessage: ...
    def on_topic_message(self, handler) -> None: ...
```

정책:

- `SubscriberSocket`에는 raw `send`를 두지 않는다.
- `XSUB`의 publish-forwarding 성격을 Python public surface로 다시 노출하지
  않는다.
- Python 쪽 설명 복잡도를 줄이기 위해 `XSUB`도 subscriber 의미 facade로 고정한다.

### 7.7 concrete socket facade

각 concrete class는 socket type 고정과 option domain 노출만 담당한다.

- `PairSocket(MessageSocket)`
  - 추가 메서드 없음
- `DealerSocket(MessageSocket)`
  - `set_option` 외에 `set_dealer_option` public 노출
  - `set_routing_id`, `get_routing_id` 사용 가능
- `RouterSocket(MessageSocket)`
  - `set_router_option`, `get_router_option` public 노출
- `StreamSocket(MessageSocket)`
  - `set_stream_option`, `get_stream_option` public 노출
- `PubSocket(PublisherSocket)`
  - `set_pub_option`, `get_pub_option` public 노출
- `XPubSocket(PublisherSocket)`
  - `set_pub_option`, `get_pub_option` public 노출
  - `subscription_event` public 노출
- `SubSocket(SubscriberSocket)`
  - `set_sub_option`, `get_sub_option` public 노출
- `XSubSocket(SubscriberSocket)`
  - `set_sub_option`, `get_sub_option` public 노출

핵심 원칙:

- concrete class는 새로운 native downcall을 직접 가지지 않는다.
- concrete class는 `_BaseSocket` protected helper를 public으로 재노출하는 수준까지만
  책임진다.
- concrete class 메서드 body는 가급적 한 줄 forwarding이어야 하며, 정책 분기는
  `_BaseSocket` 또는 facade 공통 클래스에 둔다.

## 8. 공개 생성 모델

최종 사용 예시는 아래를 목표로 한다.

```python
with zlink.PairSocket(ctx) as a:
    with zlink.PairSocket(ctx) as b:
        ...

with zlink.PubSocket(ctx) as pub:
    with zlink.SubSocket(ctx) as sub:
        ...
```

구체 생성자 원칙:

- concrete class 생성자는 `(context)`만 받는다.
- socket type 인자는 concrete class가 내부적으로 고정한다.
- `_from_handle()`은 internal attach/monitor/poller interop 용도로만 남긴다.

compat 정책:

```python
sock = zlink.Socket(ctx, zlink.SocketType.PAIR)
assert isinstance(sock, zlink.PairSocket)
assert isinstance(sock, zlink.Socket)
```

즉, `Socket`은 class name을 유지한 compat base이며, 생성 시에는 factory처럼
동작한다.

### 8.1 `Socket` compat shim 규칙

- `Socket.__new__(context, sock_type)`는 concrete facade 인스턴스를 반환한다.
- `Socket.__new__` dispatch는 `cls is Socket`일 때만 수행한다.
- `Socket` 자체에는 generic `send/publish/subscribe` 구현을 두지 않는다.
- `isinstance(sock, Socket)` 호환은 유지한다.

이유:

- pure factory alias보다 구현이 단순하고 디버깅이 쉽다.
- public 이름 `Socket`의 타입 정체성을 완전히 지우지 않아 호환 리스크가 낮다.
- concrete facade에서 `Socket` 공통 호환 메서드를 재사용하기 쉽다.

## 9. option surface 재배치

현재 `Socket`은 모든 타입별 option 메서드를 다 가진다. 최종 surface는 다음처럼
좁힌다.

- 모든 socket 공통:
  - `set_option`
  - `get_option`
  - `set_routing_id`
  - `get_routing_id`
- `RouterSocket`만:
  - `set_router_option`
  - `get_router_option`
- `DealerSocket`만:
  - `set_dealer_option`
- `PubSocket`, `XPubSocket`만:
  - `set_pub_option`
  - `get_pub_option`
- `SubSocket`, `XSubSocket`만:
  - `set_sub_option`
  - `get_sub_option`
- `StreamSocket`만:
  - `set_stream_option`
  - `get_stream_option`
- `XPubSocket`만:
  - `subscription_event`

추가 정책:

- option numeric domain dispatch는 `_BaseSocket.set_option()`에 그대로 둔다.
- 타입별 public 메서드는 사용자가 domain-specific enum과 행위를 연결해서 이해하도록
  돕는 surface다.
- `SocketOption.ROUTING_ID`, `SUBSCRIBE`, `UNSUBSCRIBE` legacy alias는 유지하되,
  문서와 새 예제에서는 dedicated helper만 사용한다.

## 10. callback surface 정리

현재 메서드 이름:

- `set_recv_handler`
- `set_subscribe_handler`
- `set_send_ready_handler`

최종 방향:

- low-level 구현 이름은 유지 가능
- public canonical 이름은 의미 중심으로 축소

권장 public 이름:

- `MessageSocket.on_receive(handler)`
- `SubscriberSocket.on_topic_message(handler)`
- `PublisherSocket.on_send_ready(handler)`

호환 정책:

- 기존 `set_*_handler`는 deprecated alias로 유지한다.
- 새 문서/예제/테스트는 `on_*` 이름을 우선 사용한다.
- alias 구현은 `_BaseSocket`에 두고, facade는 자신의 의미에 맞는 이름만
  재노출한다.

이유:

- Python에서는 `set_*_handler`보다 event registration 의미가 더 직접적이다.
- concrete facade 의미와 callback 의미가 함께 읽혀 public surface 설명이 쉬워진다.

## 11. legacy `recv(size)` 처리

`recv(size)`는 현재 `Socket`에만 남아 있는 legacy shim이다.

처리 방침:

- 새 facade에는 추가하지 않는다.
- compat `Socket` 경로에서만 deprecated 유지한다.
- 새 예제와 테스트는 `recv_message`, `recv_multipart`, `recv_topic_message`
  만 사용한다.
- 제거 시점은 별도 major surface change 문서에서 정한다.

이유:

- `recv(size)`는 multipart/routing/topic 모델과 어울리지 않는다.
- 내부 `_legacy_recv_queue`는 generic socket과 legacy API 때문에만 존재한다.
- 새 facade 분리의 목적은 semantic surface를 줄이는 것이므로 legacy semantics를
  확장하면 안 된다.

## 12. 단계별 구현 계획

### Phase 1. 공통 심부 모듈 추출

범위:

- `_core.py`에서 `Socket` 구현을 `_socket_base.py`로 이동
- `Socket` compat base class를 `_socket_base.py`로 정의
- `_SocketHandle`, `_BaseSocket`, `MessageSocket`, `PublisherSocket`,
  `SubscriberSocket` 골격 추가
- `_core.py` helper 재사용 전제로 import cycle 정리

완료 조건:

- 현재 테스트가 동작하는 단일 generic `Socket` 유지
- public 동작 변화 없음
- 이 단계의 `Socket`은 아직 기존 full surface를 임시 유지할 수 있다.
- final compat 축소는 Phase 3에서만 수행한다.

검증:

- `python -m pytest -q tests/test_version.py tests/test_enums.py tests/test_core_api_alignment.py`

### Phase 2. concrete facade 추가

범위:

- `_socket_types.py` 추가
- `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`,
  `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket` 구현
- 타입별 option 메서드 public 노출

완료 조건:

- concrete facade로 기존 examples/tests 일부 전환 가능
- generic `Socket` 없이도 동일 기능 사용 가능
- 이 단계까지는 기존 `Socket` 메서드가 내부 forwarding 형태로 남아 있어도 된다.

검증:

- `python -m pytest -q tests/test_core_api_alignment.py tests/integration`

### Phase 3. compat `Socket` 축소

범위:

- `Socket`을 factory/shim으로 변경
- `Socket.__new__` dispatch 추가
- `recv(size)`와 `set_*_handler` alias만 compat layer에 유지
- deprecation warning 추가

완료 조건:

- `Socket(ctx, SocketType.X)`가 concrete facade를 반환
- 새 코드 경로는 concrete class 기준

검증:

- `python -m pytest -q`
- examples smoke 갱신

### Phase 4. 예제/테스트/문서 정렬

범위:

- `examples/`를 concrete facade 기준으로 재작성
- contract tests 추가
- `__init__.py` export 재정렬
- 문서 갱신

완료 조건:

- 공개 surface 설명이 concrete facade 기준으로 일관됨
- generic `Socket`은 compat appendix로만 다룸

검증:

- `python -m pytest -q`
- `python -m pytest -q tests/integration`
- 필요 시 `python -m pytest -q tests/test_bench_fastpath.py`

## 13. 테스트 전략

새 회귀 테스트는 아래를 추가한다.

- concrete class 생성 smoke
  - `PairSocket(ctx)` 생성 가능
  - `Socket(ctx, SocketType.PAIR)`가 `PairSocket` 반환
  - `isinstance(Socket(ctx, SocketType.PAIR), zlink.Socket)` 유지
- surface restriction
  - `PubSocket`에 `send` 없음
  - `SubSocket`에 `publish` 없음
  - `PairSocket`에 `subscribe` 없음
- option exposure
  - `RouterSocket`만 router option 메서드 보유
  - `StreamSocket`만 stream option 메서드 보유
- callback alias
  - `on_recv`, `on_subscribe`, `on_send_ready` 동작 확인
- xpub control-plane
  - `XPubSocket.subscription_event()` 동작 확인
- legacy compat
  - `Socket(...).recv(size)`는 여전히 동작하지만 warning 발생

테스트 원칙:

- sleep/retry 없이 기존 poller helper와 fail-fast 정책을 따른다.
- API 존재성 검증만 하지 않고 실제 transport 동작도 함께 확인한다.
- 새 facade test는 "메서드가 없다"를 `hasattr` 수준에서 확인해 surface 제한을
  명확히 고정한다.

## 14. 호환성 정책

즉시 유지:

- `zlink.Socket(ctx, SocketType.X)`
- `set_*_handler`
- `recv(size)`

즉시 비권장:

- 새 예제/문서에서 generic `Socket` 사용
- topic socket에 raw `send` 의미를 설명하는 문서
- raw socket에 topic 메서드를 설명하는 문서

즉시 추가:

- `PairSocket`
- `DealerSocket`
- `RouterSocket`
- `StreamSocket`
- `PubSocket`
- `SubSocket`
- `XPubSocket`
- `XSubSocket`

제거 후보:

- legacy `recv(size)`
- `set_*_handler` 이름
- generic `Socket`의 실체 구현

## 15. 주요 설계 결정

### 15.1 `XPUB`와 `XSUB`의 의미 분류

결정:

- `XPUB`는 `PublisherSocket` 계층에 둔다.
- `XSUB`는 `SubscriberSocket` 계층에 둔다.

이유:

- native 세부 의미까지 Python public API에 모두 반영하면 설명 비용이 커진다.
- 현재 Python 사용자 입장에서는 topic publish 쪽과 topic receive 쪽 구분이
  더 중요하다.
- `XPUB`의 subscription event는 publisher-side control plane으로 설명 가능하다.

### 15.2 intermediate base class 비공개

결정:

- `MessageSocket`, `PublisherSocket`, `SubscriberSocket`는 export하지 않는다.
- `Socket`과 concrete facade만 public export한다.

이유:

- intermediate base까지 public에 노출하면 사용자가 다시 계층 전체를 이해해야 한다.
- POSD 기준으로 깊은 모듈은 내부에 두고, public surface는 concrete type 몇 개만
  알면 되게 만드는 편이 낫다.

단, intermediate base export 필요성이 생기면 별도 문서에서 재결정한다.

### 15.3 `Context.socket()` 추가 여부

현재는 비목표로 둔다.

이유:

- `Context.socket(SocketType.X)`는 generic entry point를 하나 더 만드는 셈이다.
- 지금 단계의 핵심은 타입별 facade 정착이지 생성 경로 다양화가 아니다.

## 16. 리스크와 대응

- import cycle 리스크
  - `_core.py`, `_monitor.py`, `_poller.py`가 `Socket` concrete type 이름에 의존하지
    않도록 `_handle` 기반 프로토콜만 유지한다.
  - import 방향은 `_socket_base.py` / `_socket_types.py` -> `_core.py`,
    `__init__.py` -> socket 계층 조립 방향을 유지한다.
- compat break 리스크
  - `Socket` factory 전환 후에도 `isinstance(..., Socket)`를 유지해 break를 줄인다.
- 문서-코드 drift 리스크
  - examples와 contract tests를 concrete facade 기준으로 동시에 바꿔 drift를 막는다.
- shallow wrapper 리스크
  - concrete class에 로직이 쌓이기 시작하면 즉시 `_BaseSocket` 또는 facade common
    class로 다시 흡수한다.

## 17. 비목표

- `_ffi.py` ABI 재설계
- `Spot`, `Discovery`, `Registry` 계층 재구성
- 새 native socket type 추가
- pyright typing overhaul
- perf 전용 micro-optimization 작업

## 18. 최종 사용자 표면 예시

```python
with zlink.RouterSocket(ctx) as router:
    with zlink.DealerSocket(ctx) as dealer:
        dealer.set_routing_id(b"CLIENT")
        dealer.connect(endpoint)
        router.bind(endpoint)

        dealer.send(b"hello")
        with router.recv_multipart() as received:
            assert received.routing_id == b"CLIENT"

with zlink.PubSocket(ctx) as pub:
    with zlink.SubSocket(ctx) as sub:
        sub.connect(endpoint)
        sub.subscribe(b"prices")
        pub.bind(endpoint)
        pub.publish(b"prices", b"101.25")
```

사용자가 알아야 할 규칙은 여기서 끝나야 한다.

- raw transport면 `*Socket` concrete class에서 `send/recv_*`
- topic transport면 `*Socket` concrete class에서 `publish/subscribe/recv_topic_message`
- 공통 lifecycle과 option은 타입에 맞는 facade에만 존재

이 정도 설명으로 충분하면 이번 분리는 POSD 기준에서 성공이다.
