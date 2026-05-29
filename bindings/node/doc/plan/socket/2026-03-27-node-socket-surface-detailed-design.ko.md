# Node Socket Surface 상세 설계

작성일: 2026-03-27

## 1. 목적

이 문서는 `bindings/node`의 raw socket public surface를 재정의한다.

목표는 세 가지다.

- 사용자가 socket 타입별 허용 동작을 API 표면만 보고 이해하게 만들 것
- 공통 lifecycle, option, monitor, native handle 처리를 하나의 깊은 모듈에 모을 것
- `Socket` 하나에 모든 기능을 우겨넣은 구조를 compat 계층으로 축소할 것

즉, C++와 같은 방향으로 "공통 구현은 깊게 유지하고, public surface는 의미별
facade로 나누는" 구조를 Node에도 도입한다.

단, Node는 현재 native addon과 aligned public API 제약이 있으므로 C++ 문서의
shape를 그대로 복제하지 않는다. 이 문서는 "지금 바로 구현 가능한 분리"와
"후속 native 확장이 있어야 가능한 분리"를 명확히 구분한다.

## 2. 설계 원칙

- 타입별 클래스는 새 구현체가 아니라 제한된 facade다.
- native handle ownership, 공통 endpoint API, 공통 option API, monitor 연결은
  하나의 깊은 공통 모듈에 둔다.
- 동적 언어인 Node에서는 compile-time 제약 대신 export surface와 constructor
  경로로 unsupported 동작을 원천 차단한다.
- 타입 판별 `if (type === ...)` 분기는 compat 계층과 제한된 factory에만 남기고,
  주요 public 클래스 내부에는 퍼뜨리지 않는다.
- 타입별 option domain은 concrete facade에만 노출한다.
- `core/include/zlink.h`에 실제 존재하는 native socket type만 concrete class로
  만든다.
- TypeScript 선언은 런타임 public surface와 정확히 일치해야 한다.
- POSD 기준으로 shallow wrapper를 늘리지 않는다. 공통 로직을 한 번만 설명할 수
  있어야 한다.
- 현재 native addon이 막아 둔 기능은 facade 이름만 먼저 만들지 않는다.
- 문서의 canonical surface는 테스트로 바로 옮길 수 있어야 한다.

## 3. 현재 문제 진단

현재 [`src/index.js`](/home/hep7/project/kairos/zlink/bindings/node/src/index.js)는
`Socket` 하나에 아래 책임을 함께 담고 있다.

- lifecycle: `close`
- endpoint: `bind`, `connect`
- raw transport: `send`, `sendParts`, `sendFrom`, `recv`, `recvInto`,
  `recvMsgInto`
- common option: `setOption`, `getOption`, `setRoutingId`, `getRoutingId`
- topic API: `subscribe`, `unsubscribe`
- monitor API: `monitorOpen`
- stream legacy API: `streamAttach`, `streamDetach`, `streamPeerRoutingId`,
  `streamSend`

이 구조의 문제는 명확하다.

- `PUB` socket에도 `recv`, `subscribe`, `streamAttach`가 같은 표면에 보인다.
- `SUB` socket에도 `send`, `sendParts`, `streamSend`가 같은 표면에 보인다.
- 타입별 허용 인터페이스를 문서가 아니라 사용자가 암기해야 한다.
- 테스트와 예제가 모두 `new Socket(ctx, SocketType.X)`에 묶여 있어 migration
  비용이 later phase에 몰린다.
- TypeScript 선언도 같은 광역 표면을 복제해 unsupported 동작을 조기에 막지
  못한다.

즉, 지금 구조는 사용자가 너무 많은 것을 알아야 하고, 구현도 하나의 넓은
wrapper가 모든 의미를 떠안는 shallow interface 상태다.

## 4. 구현 가능성 제약

현재 Node binding의 실제 제약은 아래와 같다.

- raw socket에는 `bind`, `connect`, `send`, `sendParts`, `sendFrom`, `recv`,
  `recvInto`, `recvMsgInto`, `setOption`, `getOption`, `monitorOpen`이 있다.
- `subscribe` / `unsubscribe`는 `SUB` / `XSUB`용 option helper로 이미 동작한다.
- `streamAttach`와 `streamSend`는 native addon이 명시적으로 unsupported error를
  던진다.
- `streamPeerRoutingId`는 현재 `null`만 돌려준다.
- `streamDetach`만 safe cleanup 용도로 동작한다.
- raw socket용 TLS convenience helper는 JS public API에 아직 없다.
- raw `PUB` / `XPUB` 전용 `publish(topic, payload)` helper는 아직 없다.
- raw socket에는 `unbind`, `disconnect` wrapper도 아직 없다.

따라서 이번 문서의 canonical 설계는 아래를 따른다.

- 즉시 구현 범위:
  - generic `Socket` 분해
  - 공통 `BaseSocket` 추출
  - 타입별 concrete socket class 추가
  - `SUB` / `XSUB` 전용 subscription surface 제한
  - `PUB` / `XPUB` 전용 send-only surface 제한
- 후속 확장 범위:
  - raw publish facade
  - stream attach/send 활성화
  - raw socket TLS convenience helper
  - unbind/disconnect 추가

## 5. native 기준 확정 범위

Node raw socket facade는 최신 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)
기준으로 아래 8종만 다룬다.

- `PAIR`
- `PUB`
- `SUB`
- `DEALER`
- `ROUTER`
- `XPUB`
- `XSUB`
- `STREAM`

이번 설계에서 제외하는 타입:

- `PUSH`
- `PULL`
- `SCATTER`
- `GATHER`
- `REQ`
- `REP`

이유:

- native socket type이 없다.
- Node에서만 가상 facade를 만들면 surface와 native 계약이 어긋난다.

## 6. 최종 계층 구조

```text
NativeSocketHandle
  ^
  |
BaseSocket
  ^
  +-- SendSocket
  |     +-- PubSocket
  |     +-- XPubSocket
  |
  +-- DuplexSocket
  |     +-- PairSocket
  |     +-- DealerSocket
  |     +-- RouterSocket
  |     +-- StreamSocket
  |
  +-- SubscriberSocket
        +-- SubSocket
        +-- XSubSocket
```

정책:

- `NativeSocketHandle`은 최소 ownership wrapper다.
- `BaseSocket`이 실제 깊은 공통 모듈이다.
- `SendSocket`은 send-only raw transport facade다.
- `DuplexSocket`은 send/recv raw transport facade다.
- `SubscriberSocket`은 receive + subscription 의미를 묶는 facade다.
- concrete type은 public surface 제한과 타입별 option 공개만 담당한다.

## 7. 파일 배치

최종 배치는 아래로 고정한다.

- `src/socket/native_socket_handle.js`
- `src/socket/base_socket.js`
- `src/socket/send_socket.js`
- `src/socket/duplex_socket.js`
- `src/socket/subscriber_socket.js`
- `src/socket/socket_types.js`
- `src/socket/compat_socket.js`
- `src/index.js`
- `src/index.d.ts`

정책:

- `src/index.js`는 상수 export, service 계층 export, socket facade re-export만 담당한다.
- `socket_types.js`는 concrete type export를 모은다.
- 기존 `Socket` 클래스 구현은 `src/index.js`에 그대로 두지 않는다.
- compat 목적의 `Socket`은 `compat_socket.js`로 격리한다.
- TypeScript 선언도 `BaseSocket`, `SendSocket`, `DuplexSocket`,
  `SubscriberSocket`, concrete type을 그대로 반영한다.

## 8. 클래스별 책임

### 8.1 `NativeSocketHandle`

역할:

- raw native socket handle 소유
- close idempotency 보장
- own/non-own ownership 보관

고정 인터페이스:

```js
class NativeSocketHandle {
  constructor(nativeHandle, own = true)
  valid()
  close()
}
```

제약:

- 일반 사용자의 주 surface가 아니다.
- `bind`, `send`, `subscribe` 같은 domain API를 두지 않는다.

### 8.2 `BaseSocket`

역할:

- 공통 lifecycle API
- 공통 endpoint API
- 공통 option API
- monitor 연결
- routing id helper
- close 전 stream detach 안전 정리

고정 인터페이스:

```js
class BaseSocket extends NativeSocketHandle {
  bind(endpoint)
  connect(endpoint)
  close()

  setOption(option, value)
  getOption(option)

  setRoutingId(routingId)
  getRoutingId()

  monitorOpen(events)
}
```

정책:

- `BaseSocket`은 직접 export하지 않는다.
- `send`, `recv`, `subscribe`, `streamAttach`를 public에 두지 않는다.
- 공통 동작은 여기서 구현하되, data-plane은 하위 facade가 연다.
- raw socket TLS convenience helper는 이번 slice에 넣지 않는다.

### 8.3 `SendSocket`

역할:

- send-only raw transport facade
- 송신 전용 socket 표면 제한

대상 타입:

- `PUB`
- `XPUB`

고정 인터페이스:

```js
class SendSocket extends BaseSocket {
  send(message, flags = 0)
  sendParts(parts, flags = 0)
  sendFrom(buffer, length, flags = 0)
}
```

정책:

- raw `PUB` / `XPUB`는 현재 aligned API 기준으로 `send` 계열을 canonical로 유지한다.
- `publish(topic, payload)` helper는 넣지 않는다.
- topic-aware publish facade는 native framing contract가 정리된 뒤 별도 slice로 다룬다.

### 8.4 `DuplexSocket`

역할:

- raw message transport facade
- point-to-point / routed message 송수신 공개

대상 타입:

- `PAIR`
- `DEALER`
- `ROUTER`
- `STREAM`

고정 인터페이스:

```js
class DuplexSocket extends BaseSocket {
  send(message, flags = 0)
  sendParts(parts, flags = 0)
  sendFrom(buffer, length, flags = 0)

  recv(flags = 0)
  recvInto(buffer, flags = 0)
  recvMsgInto(buffer, flags = 0)
}
```

정책:

- compat 때문에 남아 있는 `recv(size, flags)` overload는 새 facade에 넣지 않는다.
- binary payload normalization은 공통 helper를 재사용한다.
- `routingId`가 포함된 `Received` 표현은 계속 유지한다.

### 8.5 `SubscriberSocket`

역할:

- receive + subscription facade
- subscription 관리와 수신 공개

대상 타입:

- `SUB`
- `XSUB`

고정 인터페이스:

```js
class SubscriberSocket extends BaseSocket {
  subscribe(filter)
  unsubscribe(filter)
  recv(flags = 0)
  recvInto(buffer, flags = 0)
  recvMsgInto(buffer, flags = 0)
}
```

정책:

- raw `send` 계열은 열지 않는다.
- `recv()` 결과는 `Received`를 유지하되, topic frame 해석 helper는 이번 범위에
  넣지 않는다.
- `subscribePattern` 같은 service-level 의미 확장은 raw socket 계층에 넣지 않는다.

### 8.6 `StreamSocket`

`STREAM`은 `DuplexSocket` 계층에 속한다.

즉시 구현 범위의 고정 인터페이스:

```js
class StreamSocket extends DuplexSocket {}
```

정책:

- 현재 aligned API에서 stream 능동 기능은 지원하지 않으므로 새 canonical method를
  만들지 않는다.
- 기존 `streamAttach`, `streamDetach`, `streamPeerRoutingId`, `streamSend`는
  compat `Socket`에만 남긴다.
- `StreamSocket`은 우선 "STREAM type을 가진 duplex socket"까지만 의미를 가진다.
- stream attach/send 활성화는 native addon 확장 후 별도 설계 문서로 진행한다.

## 9. 구체 타입 facade 정의

### 9.1 message 계열

```js
class PairSocket extends DuplexSocket {}
class DealerSocket extends DuplexSocket {}
class RouterSocket extends DuplexSocket {}
class StreamSocket extends DuplexSocket {}
```

### 9.2 pub/sub 계열

```js
class PubSocket extends SendSocket {}
class XPubSocket extends SendSocket {}
class SubSocket extends SubscriberSocket {}
class XSubSocket extends SubscriberSocket {}
```

정책:

- concrete type은 constructor에서 native type을 고정한다.
- 사용자가 `SocketType` 숫자를 직접 넘기지 않아도 된다.
- 문서, 예제, 테스트는 concrete type 이름으로 의미를 드러낸다.

## 10. 허용 인터페이스 매트릭스

| 클래스 | bind/connect | raw send/recv | subscription | stream helper | typed option |
|---|---|---|---|---|---|
| `PairSocket` | O | `send/recv` | X | X | common |
| `DealerSocket` | O | `send/recv` | X | X | common + dealer(set only) |
| `RouterSocket` | O | `send/recv` | X | X | common + router |
| `StreamSocket` | O | `send/recv` | X | compat only | common |
| `PubSocket` | O | `send` only | X | X | common |
| `SubSocket` | O | `recv` only | `subscribe/unsubscribe` | X | common + sub |
| `XPubSocket` | O | `send` only | X | X | common + xpub |
| `XSubSocket` | O | `recv` only | `subscribe/unsubscribe` | X | common + sub |

의미:

- `PubSocket`에는 `recv`와 `subscribe`가 없다.
- `SubSocket`에는 `send`가 없다.
- `StreamSocket`의 stream-specific helper는 현재 canonical surface에 없다.

## 11. 타입별 option 노출 규칙

### 11.1 공통 option

`BaseSocket`에 둔다.

예:

- `LINGER`
- `SNDHWM`, `RCVHWM`
- `SNDBUF`, `RCVBUF`
- `SNDTIMEO`, `RCVTIMEO`
- `RECONNECT_IVL`, `RECONNECT_IVL_MAX`
- `HEARTBEAT_*`
- `ROUTING_ID`

### 11.2 타입별 option

해당 concrete facade에만 typed helper를 둔다.

- `DealerSocket`
  - dealer 전용 set-only option helper
- `RouterSocket`
  - `ROUTER_MANDATORY`
  - `ROUTER_HANDOVER`
  - `PROBE_ROUTER`
  - `CONNECT_ROUTING_ID`
- `XPubSocket`
  - `XPUB_VERBOSE`
  - `XPUB_VERBOSER`
  - `XPUB_MANUAL`
  - `XPUB_*`
- `SubSocket` / `XSubSocket`
  - `SUBSCRIBE`
  - `UNSUBSCRIBE`
  - subscription 관련 option helper

정책:

- 범용 `setOption`은 유지하되, user-facing guidance는 typed helper를 우선한다.
- typed helper는 option enum domain을 드러내기 위한 facade이며 구현은 공통 helper를
  재사용한다.
- TS 선언에서도 타입별 helper만 노출한다.
- `StreamSocket` typed helper는 native contract가 정리되기 전까지 추가하지 않는다.

## 12. 생성 정책

새 public 권장 경로에서는 generic constructor를 금지한다.

금지:

```js
const s = new Socket(ctx, SocketType.DEALER)
```

허용:

```js
const dealer = new DealerSocket(ctx)
const sub = new SubSocket(ctx)
const pub = new PubSocket(ctx)
const stream = new StreamSocket(ctx)
```

이유:

- 타입 의미가 생성 시점부터 드러난다.
- facade 제한이 실제로 작동한다.
- 문서와 예제가 단순해진다.
- unsupported 동작을 "호출 후 native error"가 아니라 "메서드 자체가 없음"으로
  바꿀 수 있다.

## 13. compat 정책

기존 `Socket`은 즉시 삭제하지 않는다.

최종 정책:

- `Socket`은 deprecated compat surface로 축소한다.
- 내부적으로 concrete facade를 생성해 위임하거나, 제한된 factory 역할만 수행한다.
- 새 기능은 `Socket`에 추가하지 않는다.
- README, examples, tests, `.d.ts`의 대표 surface는 모두 concrete facade 기준으로
  바꾼다.

compat 단계에서 허용하는 legacy:

- `new Socket(ctx, SocketType.X)`
- `streamAttach`, `streamDetach`, `streamPeerRoutingId`, `streamSend`
- `recv(size, flags)`

compat 단계에서 금지하는 것:

- compat `Socket`에 새 타입별 helper를 계속 덧붙이는 것
- README와 예제에서 compat surface를 canonical path로 유지하는 것

## 14. 서비스 계층과의 관계

아래 클래스는 본 문서의 raw socket facade와 별도 계층으로 유지한다.

- `Registry`
- `RegistryQueryClient`
- `Discovery`
- `SpotNode`
- `Spot`

정책:

- `Spot`은 `PubSocket`/`SubSocket`의 얕은 별칭이 아니다.
- raw socket facade 분리와 service 계층 분리를 섞지 않는다.
- service 계층의 topic language와 raw socket의 transport language는 계속 분리한다.

## 15. TypeScript 선언 정책

[`src/index.d.ts`](/home/hep7/project/kairos/zlink/bindings/node/src/index.d.ts)는
런타임 surface와 같이 재구성한다.

- `Socket`은 deprecated compat class로 별도 표시
- `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket` 선언 추가
- `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket` 선언 추가
- `BaseSocket` 계층은 내부 class로 유지하거나, 선언상 `abstract`로 표기
- unsupported 메서드는 선언에서 제거

핵심 원칙:

- 문서와 타입 선언이 같은 제약을 표현해야 한다.
- "런타임에서는 실패하지만 선언상 보이는 메서드"를 남기지 않는다.

## 16. migration 기준

현재 코드를 아래처럼 옮긴다.

- `new Socket(ctx, SocketType.PAIR)` -> `new PairSocket(ctx)`
- `new Socket(ctx, SocketType.DEALER)` -> `new DealerSocket(ctx)`
- `new Socket(ctx, SocketType.ROUTER)` -> `new RouterSocket(ctx)`
- `new Socket(ctx, SocketType.STREAM)` -> `new StreamSocket(ctx)`
- `new Socket(ctx, SocketType.PUB)` -> `new PubSocket(ctx)`
- `new Socket(ctx, SocketType.SUB)` -> `new SubSocket(ctx)`
- `new Socket(ctx, SocketType.XPUB)` -> `new XPubSocket(ctx)`
- `new Socket(ctx, SocketType.XSUB)` -> `new XSubSocket(ctx)`

메서드 migration:

- `socket.send(...)` on `PUB` / `XPUB` -> `pub.send(...)` / `xpub.send(...)`
- `socket.subscribe(...)` on `SUB` / `XSUB` -> `sub.subscribe(...)` / `xsub.subscribe(...)`
- `socket.recv(...)` on `SUB` / `XSUB` -> `sub.recv(...)` / `xsub.recv(...)`
- `stream.*` legacy helpers -> compat `Socket`에만 잔류, canonical path에서는 사용 중단

## 17. 구현 순서

### Slice 1. 공통 기반 분리

- `NativeSocketHandle`, `BaseSocket` 추출
- 현재 `Socket`의 lifecycle, endpoint, common option, monitor 로직 이동
- `src/index.js`는 re-export 중심으로 축소

완료 조건:

- 기존 테스트가 유지된다
- `BaseSocket`이 data-plane public API를 노출하지 않는다

### Slice 2. 의미 facade 추가

- `SendSocket`, `DuplexSocket`, `SubscriberSocket` 추가
- payload normalization과 multipart handling을 의미 계층별로 배치

완료 조건:

- send-only, duplex, subscriber 구현 경계가 분리된다
- 공통 로직 중복 없이 facade가 붙는다

### Slice 3. concrete socket class 추가

- `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket` 추가
- `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket` 추가

완료 조건:

- 테스트와 예제 일부가 concrete facade로 전환된다
- `SocketType` 숫자를 넘기지 않는 canonical 생성 경로가 생긴다

### Slice 4. typed option helper 정리

- concrete type별 option helper 추가
- routing id helper의 공통 위치 고정

완료 조건:

- 타입별 샘플이 자기 타입 option helper만 사용한다
- TS 선언이 surface 제한을 반영한다

### Slice 5. compat 축소

- README, examples, tests를 concrete facade로 이동
- `Socket`을 deprecated compat로 축소
- `recv(size, flags)`와 stream legacy alias를 compat에만 남긴다

완료 조건:

- 사용자-facing canonical 문서에 generic `Socket` 생성이 남지 않는다
- 새 public surface가 concrete facade 기준으로 설명된다

### Slice 6. 후속 native 확장 검토

- raw `STREAM` attach/send 기능을 addon에서 실제 지원할지 결정
- raw `PUB` / `XPUB`용 topic-aware publish facade가 필요한지 결정
- raw socket TLS convenience helper 추가 여부 결정
- raw socket `unbind` / `disconnect` wrapper 추가 여부 결정

완료 조건:

- 후속 확장이 필요 없는 범위와 필요한 범위가 분리된다
- 구현팀이 현재 slice와 후속 slice를 혼동하지 않는다

## 18. 검증 계획

테스트는 아래 순서로 옮긴다.

- `pair.test.js`, `multipart.test.js` -> `PairSocket`
- `dealer_router.test.js` -> `DealerSocket`, `RouterSocket`
- `pubsub.test.js` -> `SubSocket`
- `xpub_xsub.test.js` -> `XPubSocket`, `XSubSocket`
- `version.test.js` 중 routing id / stream 관련 검증 -> `DealerSocket`,
  `StreamSocket`

추가해야 할 회귀 검증:

- `PubSocket` 인스턴스에 `recv`가 없는 surface 검증
- `SubSocket` 인스턴스에 `send`가 없는 surface 검증
- `StreamSocket` canonical surface에 stream helper가 노출되지 않는지 검증
- deprecated compat `Socket`이 concrete facade와 동일 동작으로 위임되는지 검증
- `.d.ts` 기준 public 예제가 타입 에러 없이 통과하는지 검증

## 19. 완료 기준

아래가 모두 만족되면 본 설계를 구현 완료로 본다.

- `Socket` 단일 광역 surface가 canonical path에서 사라진다
- concrete facade 목록이 최신 native socket type과 정확히 일치한다
- 타입별 facade가 실제 public surface를 제한한다
- send-only / duplex / subscriber 계열이 분리된다
- stream-specific helper는 compat에만 남거나 최종 제거된다
- 타입별 option helper가 socket family 기준으로 노출된다
- README, examples, tests, `.d.ts`가 concrete facade 기준으로 재작성된다
- compat `Socket`은 deprecated 상태로 축소되거나 최종 제거된다

## 20. 보류 사항

이번 설계에서는 아래를 넣지 않는다.

- async iterator 기반 socket receive facade
- Promise 기반 send/recv wrapper
- EventEmitter 상속 기반 socket class
- native addon API 전체 재작성
- raw `STREAM` attach/send 구현
- raw publish(topic, payload) helper 도입
- service 계층까지 한 번에 같은 구조로 분해하는 작업

이유:

- 현재 목표는 raw socket public surface 정리다.
- 비동기 모델과 native 기능 확장을 동시에 바꾸면 책임이 커지고 migration risk가
  커진다.

## 21. 최종 결론

이번 Node socket 계층은 아래 방향으로 고정한다.

- 구현은 `BaseSocket`에 집중
- public surface는 concrete socket facade로 분리
- `send-only`, `duplex`, `subscriber`를 의미 계층 기준으로 분리
- stream helper는 우선 compat에만 남기고 canonical path에서 제거
- generic `Socket`은 compat로 축소 후 제거
- 타입별 option과 타입 선언까지 함께 정리

즉, Node에서도 "generic socket 하나에 모든 기능을 넣는 구조"를 끝내고, 공통
구현은 깊게 유지하면서 사용자 표면만 의미 중심으로 나누는 POSD 방향으로
전환한다.
