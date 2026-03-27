# Node Socket Surface 실행 가이드

> 상태: 완료
> 기준 문서: 이 실행 가이드 하나로 고정
> 대상 범위: `bindings/node/`
> 목적: Node raw socket surface를 POSD 기준으로 분리하고 `Socket` compat 축소까지 끝까지 진행하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 목적

이 문서는 `bindings/node` socket 분리 작업의 유일한 실행 authority다.

이전 상세 설계 문서에 있던 의도는 이 가이드에 흡수한다.
실행 중 설계 판단이 필요하면 먼저 이 가이드를 고치고, 그 다음 코드를 수정한다.

이번 실행의 최종 목표는 아래다.

- `Socket` 단일 광역 surface를 canonical path에서 제거한다.
- 공통 구현을 `BaseSocket` 계층으로 모으고, public surface는 concrete socket
  facade로 제한한다.
- `SendSocket`, `DuplexSocket`, `SubscriberSocket` 계층과 concrete type을 실제
  코드와 `.d.ts`에 반영한다.
- README, examples, tests를 concrete socket 기준으로 옮긴다.
- compat `Socket`은 deprecated 상태로 축소한다.

## 2. 대상 범위

이번 실행 범위:

- `bindings/node/src/`
- `bindings/node/tests/`
- `bindings/node/examples/`
- `bindings/node/README.md`
- `bindings/node/plan/socket/`

이번 실행 제외 범위:

- `core/`
- `core/tests/`
- `bindings/cpp/`
- `bindings/node/prebuilds/`

정책:

- `core` bug fix 요청이 아닌 이상 native capability가 부족해도 `core/`를 건드리지 않는다.
- native addon extension이 필요하더라도 이번 loop의 기본 목표는 "현재 Node addon
  계약 안에서 가능한 socket surface 분리"다.
- 현재 addon 계약으로 불가능한 항목은 compat 잔류 또는 후속 항목으로 정리하고,
  문서에 명시한 뒤 다음 작업으로 넘어간다.

## 3. 고정 설계

이번 실행에서 canonical raw socket 구조는 아래로 고정한다.

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

고정 결정:

- `PubSocket` / `XPubSocket` canonical API는 `send` 계열이다.
- `SubSocket` / `XSubSocket` canonical API는 `subscribe` + `recv` 계열이다.
- `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`은 `DuplexSocket`
  계층에 둔다.
- `StreamSocket`은 현재 canonical surface에서 active stream helper를 열지 않는다.
- `streamAttach`, `streamDetach`, `streamPeerRoutingId`, `streamSend`,
  `recv(size, flags)`는 compat `Socket`에만 남긴다.
- raw socket TLS convenience helper, raw publish(topic, payload), unbind/disconnect는
  이번 실행의 완료 조건이 아니다.

## 4. 금지 규칙

아래는 금지한다.

- `Socket` compat에 새 기능을 덧붙이는 것
- concrete facade를 만들면서 내부 구현을 파일마다 복제하는 것
- README와 examples에 generic `new Socket(ctx, SocketType.X)`를 canonical path로
  남기는 것
- `.d.ts`에 런타임에서 unsupported인 새 메서드를 먼저 노출하는 것
- 테스트만 통과시키고 가이드의 남은 구현 항목을 미완료 상태로 두는 것
- 설계 문서를 늘려서 authority를 분산시키는 것

## 5. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 현재 가이드만으로 해결할 수 없는 Node public API 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/node/`만으로 처리할 수 없는 blocker

위 경우가 아니면 아래 순서로 진행한다.

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 테스트/예제/문서 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 가이드 상태와 체크리스트를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

commit / push는 사용자 지시가 있을 때만 수행한다.

로그 운영 규칙:

- wrapper 실행 로그는 `bindings/node/plan/socket/logs/` 아래 세션 디렉터리를 기준으로 본다.
- 각 iteration의 `*_prompt.txt`, `*_codex.log`, `*_last_message.txt`는 해당 세션의
  작업 증거로 유지한다.
- wrapper smoke나 실제 loop 실행 중 이상 종료가 나면 가장 최근 세션 디렉터리를 먼저
  확인한다.
- 가이드 갱신, 검증 실패 원인, 후속 조치가 있으면 같은 세션에서 이해 가능하도록
  commit 없이도 가이드 본문에 남긴다.

## 6. 기본 검증 명령

기본 검증:

```bash
cd bindings/node && npm test

cd bindings/node && node --test tests/*.test.js
```

상세 검증:

```bash
cd bindings/node && node --test tests/pair.test.js
cd bindings/node && node --test tests/multipart.test.js
cd bindings/node && node --test tests/dealer_router.test.js
cd bindings/node && node --test tests/pubsub.test.js
cd bindings/node && node --test tests/xpub_xsub.test.js
cd bindings/node && node --test tests/version.test.js
```

example smoke:

```bash
cd bindings/node && node examples/pair-recv.js
cd bindings/node && node examples/pair-handler.js
cd bindings/node && node examples/pubsub-recv.js
cd bindings/node && node examples/pubsub-handler.js
cd bindings/node && node examples/dealer-router-recv.js
cd bindings/node && node examples/stream-handler.js
cd bindings/node && node examples/spot-recv.js
cd bindings/node && node examples/discovery-service-view.js
```

wrapper smoke:

```bash
./bindings/node/plan/socket/run_node_socket_surface_execution.sh --max-iterations 0
```

주의:

- 현재 repository 상태에 따라 example 실행이 slice 중간에는 일시적으로 깨질 수 있다.
- slice 완료 판정은 "해당 slice가 바꾼 public surface와 검증 자산이 같이 정렬됐는지"
  기준으로 한다.
- `stream-handler.js`는 compat 경계를 검증하는 example로 취급한다.
- `spot-recv.js`, `discovery-service-view.js`는 raw socket canonical surface가 아니라
  service 계층 비회귀 smoke로 취급한다.
- 따라서 raw socket slice가 끝나더라도 service example 회귀가 생기면 완료로 닫지 않는다.

## 7. 작업 레지스터

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 7.1 Slice 1. 공통 socket 기반 추출

상태: `완료`

대상:

- `bindings/node/src/index.js`
- `bindings/node/src/index.d.ts`
- `bindings/node/src/socket/`

작업:

- `NativeSocketHandle`, `BaseSocket`를 새 파일로 추출한다.
- 공통 lifecycle, endpoint, option, monitor, routing id helper를 `BaseSocket`로
  이동한다.
- close 시 `streamDetach` safe cleanup은 공통 경계에 둔다.
- 기존 `Socket` 본체 구현은 `src/index.js`에서 분리한다.

완료 기준:

- 공통 로직이 `BaseSocket`에 모인다.
- `BaseSocket`이 data-plane public API를 직접 열지 않는다.
- 기존 동작을 깨지 않는 최소 smoke가 유지된다.

### 7.2 Slice 2. 의미 facade 계층 추가

상태: `완료`

대상:

- `bindings/node/src/socket/send_socket.js`
- `bindings/node/src/socket/duplex_socket.js`
- `bindings/node/src/socket/subscriber_socket.js`
- `bindings/node/src/index.d.ts`

작업:

- `SendSocket`, `DuplexSocket`, `SubscriberSocket`를 추가한다.
- payload normalization과 multipart handling을 중복 없이 재배치한다.
- compat `recv(size, flags)` overload는 새 facade에 넣지 않는다.

완료 기준:

- send-only / duplex / subscriber 경계가 실제 코드에 드러난다.
- 새 facade 간 구현 중복이 없다.
- `.d.ts`가 새 계층 구조를 반영한다.

### 7.3 Slice 3. concrete socket class 도입

상태: `완료`

대상:

- `bindings/node/src/socket/socket_types.js`
- `bindings/node/src/index.js`
- `bindings/node/src/index.d.ts`

작업:

- `PairSocket`, `DealerSocket`, `RouterSocket`, `StreamSocket`를 추가한다.
- `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`를 추가한다.
- constructor에서 native socket type을 고정한다.
- `src/index.js`는 concrete type re-export 중심으로 축소한다.

완료 기준:

- canonical 생성 경로가 `new PairSocket(ctx)` 식으로 정리된다.
- 각 concrete type이 자기 계층의 메서드만 보인다.

### 7.4 Slice 4. compat `Socket` 축소

상태: `완료`

대상:

- `bindings/node/src/socket/compat_socket.js`
- `bindings/node/src/index.js`
- `bindings/node/src/index.d.ts`

작업:

- 기존 `Socket`을 deprecated compat surface로 재구성한다.
- 내부적으로 concrete facade 생성/위임 또는 제한된 adapter 역할만 하게 만든다.
- legacy `stream*` helper와 `recv(size, flags)`를 compat에만 남긴다.
- 새 기능은 compat `Socket`에 추가하지 않는다.

완료 기준:

- canonical path와 compat path가 코드 구조상 분리된다.
- compat가 concrete facade를 우회하는 새 public 진입점이 되지 않는다.

### 7.5 Slice 5. tests / examples / README / typings 정렬

상태: `완료`

대상:

- `bindings/node/tests/*.test.js`
- `bindings/node/examples/*.js`
- `bindings/node/README.md`
- `bindings/node/src/index.d.ts`

작업:

- 테스트를 concrete socket 생성 경로 기준으로 옮긴다.
- README canonical raw API를 concrete facade 기준으로 다시 쓴다.
- 예제를 concrete socket 기준으로 바꾼다.
- surface 제한이 `.d.ts`에도 그대로 보이게 만든다.

완료 기준:

- README, examples, tests가 generic `Socket`을 대표 surface로 쓰지 않는다.
- `PubSocket`에 `recv`가 없고 `SubSocket`에 `send`가 없는 surface 검증이 들어간다.
- `StreamSocket` canonical surface에 active stream helper가 없음을 검증한다.

### 7.6 Slice 6. 잔여 compat / 후속 항목 정리

상태: `완료`

대상:

- `bindings/node/README.md`
- `bindings/node/plan/socket/node-socket-surface-execution-guide.ko.md`

작업:

- 이번 실행에서 끝내지 않는 항목을 명시적으로 고정한다.
- 후속 native 확장 항목을 compat 잔류 항목과 분리해 적는다.
- 실제 구현 결과에 맞게 종료 조건과 잔여 리스크를 정리한다.

이번 실행 결과:

- compat 잔류 항목:
  `Socket.recv(size, flags)`, `Socket.streamAttach`,
  `Socket.streamAttachRaw`, `Socket.streamAttachLen32be`,
  `Socket.streamDetach`, `Socket.streamPeerRoutingId`, `Socket.streamSend`
- compat STREAM helper 구현 상태:
  `streamAttach*`와 `streamSend`는 compat 전용 native addon 경계로 다시 연결했고,
  `streamPeerRoutingId(index)`는 attach 이후 callback으로 관측된 peer routing id만
  노출하며 미관측 index는 `null`을 돌려준다.
- 후속 native 확장 필요 항목:
  raw socket TLS convenience helper, raw publish(topic, payload),
  raw socket unbind/disconnect helper
- canonical raw surface는 concrete socket으로 고정했고 README와 typings에도 같은
  경계를 반영했다.

완료 기준:

- "지금 가능한 분리"와 "후속 native 확장 필요 항목"이 혼동 없이 정리된다.
- 남은 항목이 있으면 `미적용 사항이 없습니다.`를 출력하지 않는다.

### 7.7 Slice 7. POSD 기반 마무리 리팩토링

상태: `완료`

대상:

- `bindings/node/src/`
- `bindings/node/src/socket/`
- `bindings/node/src/index.d.ts`
- 필요 시 `bindings/node/tests/`

작업:

- 구현이 모두 반영된 뒤 POSD 기준으로 마지막 구조 리뷰를 수행한다.
- change amplification, hidden coupling, temporal decomposition, shallow wrapper,
  중복 normalization/dispatch, compat 의존 누수 여부를 다시 찾는다.
- 찾은 리팩토링 대상은 영향 범위가 작은 것부터 실제 코드로 반영하고 검증한다.
- 리팩토링으로 public surface 계약이 바뀌면 README, tests, typings도 함께 정렬한다.
- 한 번의 정리로 끝내지 말고, 리팩토링 후 다시 읽어서 다음 리팩토링 대상이 남아
  있는지 반복 확인한다.

완료 기준:

- POSD 기준으로 설명하기 어려운 얕은 wrapper나 중복 책임이 남아 있지 않다.
- compat와 canonical 경계가 구조적으로 더 단순해지지 않는 상태까지 정리된다.
- 추가 리팩토링 후보가 보여도 "지금 고치면 전체 복잡도가 더 낮아진다"는 근거가
  더 이상 나오지 않는다.
- 더 이상 정당한 리팩토링 대상이 없을 때만 이 slice를 `완료`로 바꾼다.

마지막 POSD 점검 결과:

- `Socket` compat는 `DuplexSocket` 상속으로 축소해 send/recv 중복 구현을 제거했다.
- payload normalization, multipart normalization, recv dispatch는
  `src/socket/socket_support.js`에 모아 change amplification을 줄였다.
- canonical concrete socket이 compat helper에 의존하지 않는 구조를 유지한다.
- 현재 범위 안에서 추가 리팩토링을 해도 전체 복잡도를 더 낮출 근거가 남아 있지 않다.

## 8. 검증 운영 규칙

- slice별로 관련 테스트를 먼저 돌리고, 큰 변경 경계마다 `npm test`와 전체
  `node --test tests/*.test.js`를 다시 돌린다.
- 문서/타입/예제 변경만으로 끝내지 말고 실제 runtime 검증을 남긴다.
- 실패가 나면 원인 slice를 바로 고치고 같은 검증 명령으로 재확인한다.
- 검증 명령과 결과는 이 가이드 진행 메모에 남길 수 있으면 남긴다.
- 마지막 POSD 리팩토링 slice에서도 동일하게 검증을 생략하지 않는다.

검증 메모:

- 2026-03-27: `cd bindings/node && node-gyp rebuild`
- 2026-03-27: `cd bindings/node && node --test tests/version.test.js`
- 2026-03-27: `cd bindings/node && node --test tests/pair.test.js tests/multipart.test.js tests/dealer_router.test.js tests/pubsub.test.js tests/version.test.js tests/xpub_xsub.test.js`
- 2026-03-27: `cd bindings/node && npm test`
- 2026-03-27: `cd bindings/node && node --test tests/*.test.js`
- 2026-03-27: `cd bindings/node && node examples/pair-recv.js`
- 2026-03-27: `cd bindings/node && node examples/pair-handler.js`
- 2026-03-27: `cd bindings/node && node examples/pubsub-recv.js`
- 2026-03-27: `cd bindings/node && node examples/pubsub-handler.js`
- 2026-03-27: `cd bindings/node && node examples/dealer-router-recv.js`
- 2026-03-27: `cd bindings/node && node examples/stream-handler.js`
- 2026-03-27: `cd bindings/node && node examples/spot-recv.js`
- 2026-03-27: `cd bindings/node && node examples/discovery-service-view.js`
- 2026-03-27: `./bindings/node/plan/socket/run_node_socket_surface_execution.sh --max-iterations 0`

실패 복구 절차:

1. 실패한 명령 하나를 다시 단독 실행해 재현을 고정한다.
2. 이번 slice가 건드린 파일과 public surface 차이를 먼저 확인한다.
3. 코드 또는 가이드를 고친 뒤 같은 명령으로 재확인한다.
4. slice 경계 문제면 slice 관련 세부 검증을 모두 다시 돌린다.
5. 큰 변경 경계에서 실패했다면 `npm test`와 전체 `node --test tests/*.test.js`까지 복구 확인한다.
6. wrapper smoke 실패면 가장 최근 `logs/` 세션 디렉터리의 `*_codex.log`와
   `*_last_message.txt`를 먼저 확인한 뒤 wrapper/guide 경로 문제부터 정리한다.

## 9. 종료 조건

아래가 모두 만족되면 종료다.

- `BaseSocket`, `SendSocket`, `DuplexSocket`, `SubscriberSocket`, concrete socket
  class가 실제 코드에 존재한다.
- canonical raw API가 concrete socket 생성 경로 기준으로 재구성된다.
- compat `Socket`은 deprecated 경계로 축소된다.
- README, examples, tests, `.d.ts`가 새 surface를 반영한다.
- 현재 Node addon 계약상 미구현 기능은 compat 또는 후속 항목으로 명확히 정리된다.
- POSD 기준 마지막 리팩토링 반복이 끝났고, 더 진행할 정당한 리팩토링 대상이 남아
  있지 않다.
- 더 진행할 구체 작업이 이 가이드에 남아 있지 않다.

현재 판정:

- 위 종료 조건을 모두 만족한다.
- compat 잔류 항목과 후속 native 확장 필요 항목은 7.6에 고정했다.
- 이 가이드 기준 다음 작업은 없다.

종료 메시지 규칙:

- 모든 작업이 끝났을 때만 정확히 `미적용 사항이 없습니다.`
- 사용자 결정이 꼭 필요할 때만 `사용자 입력 필요: ...`
- 그 외에는 항상 `계속 진행 필요`
