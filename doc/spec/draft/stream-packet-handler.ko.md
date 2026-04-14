[스펙 목차](../README.ko.md)

# Draft -- STREAM Packet Handler

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 raw `STREAM` 소켓 위에 packet 단위 callback 표면을 추가하는 방향을
정의한다.

핵심 목표는 아래와 같다.

- 호출자가 stream fragment를 직접 모아 packet을 조립하지 않아도 되게 한다.
- 고정 framing 규약을 core가 직접 해석해서 packet 단위 callback으로 올린다.
- 바인딩 라이브러리에서 중복되는 packet 조립, 버퍼 누적, 불필요한 복사를 줄인다.
- `STREAM`을 raw callback / raw recv / packet callback 중 하나의 수신 모델로
  사용할 수 있게 한다.

## 2. 배경

현재 `STREAM`은 raw transport 조각을 recv 또는 callback으로 직접 다루는 모델이다.

이 방식은 유연하지만, 실제 응용에서는 다음 부담이 반복된다.

- 길이 필드를 읽을 때까지 버퍼를 누적해야 한다.
- 길이 필드를 해석한 뒤 packet 전체가 도착할 때까지 다시 누적해야 한다.
- header와 body를 잘라서 상위 프로토콜에 넘겨야 한다.
- 이 로직이 바인딩마다 반복되고, 바인딩 쪽에서 추가 복사까지 일어나기 쉽다.

이 문서는 이 문제를 "고정 framing 규약을 STREAM 전용 packet callback으로 올린다"는
방식으로 해결하려고 한다.

## 3. framing 규약

이번 초안은 framing 규약을 아래처럼 고정한다.

- `2 bytes`: header size
- `4 bytes`: body size
- 그 뒤에 `header` payload
- 그 뒤에 `body` payload

즉 wire 순서는 아래와 같다.

```text
+----------------+----------------+----------------+----------------+
| header_size_u16| body_size_u32  | header bytes   | body bytes     |
+----------------+----------------+----------------+----------------+
```

이 문서는 길이 필드의 바이트 순서를 네트워크 바이트 순서(big-endian)로 고정한다.

### 3.1 packet 정의

한 packet은 아래 네 부분으로 구성된다.

- 고정 2바이트 header size
- 고정 4바이트 body size
- `header size`만큼의 header payload
- `body size`만큼의 body payload

### 3.2 빈 payload

이 초안은 아래 경우를 허용한다.

- header size = 0
- body size = 0

즉 header만 있는 packet, body만 있는 packet, 둘 다 빈 packet을 모두 허용한다.

## 4. 공개 API 방향

`STREAM` 전용 packet callback 등록 함수는 아래와 같이 둔다.

```c
typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);

ZLINK_EXPORT zlink_handler_result_t zlink_stream_packet_handler (
  void *stream_,
  zlink_stream_packet_handler_fn handler_,
  void *userdata_);
```

이 함수는 raw `STREAM` 소켓에만 적용한다.
callback에 전달되는 `header_`, `body_`는 길이가 0인 경우에도 항상 유효한
`zlink_msg_t` 객체로 전달한다.

## 5. 수신 모델과의 관계

`STREAM`은 이번 개정에서도 예외 타입으로 둔다. 다만 수신 모델은 아래 세 가지 중
하나만 선택한다.

- `zlink_recv()`:
  raw recv 모드
- `zlink_recv_handler()`:
  raw callback 모드
- `zlink_stream_packet_handler()`:
  packet callback 모드

즉 `STREAM`은 세 가지 receive surface를 제공하지만, 같은 handle에서 동시에 둘 이상
활성화할 수는 없다.

### 5.1 상호 배타 규칙

이 문서는 아래 규칙을 고정한다.

- `zlink_stream_packet_handler()`가 붙은 `STREAM` handle에서는
  `zlink_recv()`가 `EBUSY`로 실패한다.
- `zlink_stream_packet_handler()`가 붙은 `STREAM` handle에서는
  `zlink_recv_handler()`가 `EBUSY`로 실패한다.
- `zlink_recv_handler()`가 붙은 `STREAM` handle에서는
  `zlink_stream_packet_handler()`가 `EBUSY`로 실패한다.
- raw recv 모드에서 `zlink_stream_packet_handler()`를 성공적으로 붙이면, 그 뒤
  direct recv와 `ZLINK_POLLIN` 등록은 `EBUSY`로 실패한다.
- 같은 handle에 대한 두 번째 `zlink_stream_packet_handler()` attach도
  `EBUSY`로 실패한다.

즉 `STREAM`의 수신 모드는 "raw recv", "raw callback", "packet callback" 중 하나의
일방 전환 모델로 본다.

## 6. callback payload와 ownership

packet callback은 header와 body를 `zlink_msg_t` 형태로 전달한다.

```c
typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);
```

ownership 규칙은 기존 message callback과 맞춘다.

- `source_rid_`는 이 packet을 보낸 client 연결의 routing id다.
- `source_rid_`는 callback 실행 중에만 유효한 borrowed view다.
- implementation은 이 값을 callback 인자 전달용 view로만 사용한다.
- callback 이후에도 유지하려면 호출자가 값을 복사해야 한다.
- callback 진입 시 `header_`와 `body_`의 소유권은 callback으로 이전된다.
- callback은 `header_`와 `body_`를 각각 정확히 한 번 close하거나 소비해야 한다.
- 길이가 0인 header/body도 유효한 `zlink_msg_t`로 전달한다.

즉 body가 비어 있다고 해서 `body_ == NULL`로 넘기지 않는다.

### 6.1 추가 복사 금지 방향

이 초안의 구현 기준은 packet callback delivery를 만들 때 header/body payload를
추가로 다시 복사해서 새 버퍼를 만드는 방향을 기본으로 두지 않는다.

- implementation은 packet 조립이 끝난 시점에 최종 `zlink_msg_t` ownership 객체를
  만들고, 그 객체를 callback에 직접 넘겨야 한다.
- callback delivery 직전에 header/body payload를 한 번 더 복사해서 별도 임시
  버퍼를 만드는 구현은 이 문서의 권장 구현에 맞지 않는다.
- 즉 packet callback은 "조립 후 추가 복사 없는 final `msg_t` delivery"를 목표로
  한다.

여기서 "추가 복사 금지"의 의미는 아래처럼 해석한다.

- callback delivery 직전에 header/body를 다시 별도 버퍼로 복사하지 않는다.
- packet 길이가 확정된 뒤 final `msg_t` backing buffer에 payload를 채우는 것은
  허용된다.
- 즉 이 문서는 "delivery extra copy 금지"를 요구하는 것이지, transport fragment
  수신부터 packet 완성까지 전체 경로가 절대 무복사여야 한다고 요구하지는 않는다.

이 규칙이 필요한 이유는 아래와 같다.

- 바인딩에서 다시 추가 복사가 이어지지 않게 하기 위해서다.
- packet mode를 넣는 목적 자체가 조립과 delivery 경로의 중복 비용을 줄이는 데
  있기 때문이다.

따라서 implementation은 연결별 누적 버퍼를 관리하더라도, callback으로 올리는
시점에는 header/body를 최종 `zlink_msg_t` 객체로 잘라 넘기고 추가 복사를 만들지
않는 방향으로 작성해야 한다.

## 7. packet 조립 규칙

### 7.1 내부 누적 버퍼

`STREAM` packet callback 모드에서는 implementation이 연결별 수신 바이트를 내부에
누적하고, packet 하나가 완성될 때만 callback을 호출한다.

즉 호출자는 fragment 경계를 직접 다루지 않는다.

이때 구현 기준은 아래와 같다.

- 길이 필드가 아직 완성되지 않은 단계에서는 prefix state만 별도로 누적한다.
- 2바이트 header size와 4바이트 body size가 모두 확정되면,
  implementation은 해당 크기의 `header zlink_msg_t`와 `body zlink_msg_t`를 즉시
  할당한다.
- 이후 들어오는 payload 바이트는 그 `header_`와 `body_`의 backing buffer에 직접
  누적한다.

즉 packet callback으로 넘길 최종 `msg_t` 객체 자체를 packet assembly target으로
사용하는 것을 기본 구현 방향으로 본다.

### 7.2 packet 완성 시점

callback은 아래 조건이 모두 만족될 때만 호출한다.

- 2바이트 header size가 모두 도착함
- 4바이트 body size가 모두 도착함
- 지정된 header payload가 모두 도착함
- 지정된 body payload가 모두 도착함

부분 packet 상태에서는 callback을 호출하지 않는다.

길이 필드가 확정된 뒤에는 아래 순서로 채운다.

1. `header_` backing buffer를 `header size`만큼 채운다.
2. `body_` backing buffer를 `body size`만큼 채운다.
3. 둘 다 완료되면 callback delivery를 만든다.

### 7.3 packet 경계 유지

하나의 `STREAM` 연결에서 packet이 여러 개 연속해서 들어오면 implementation은
framing 규약에 따라 정확한 packet 경계를 복원해서 callback을 여러 번 호출해야
한다.

즉 transport fragment 경계와 packet 경계는 무관하다.

## 8. malformed packet 처리

이 초안은 malformed packet을 아래 경우로 본다.

- 길이 필드 자체를 끝까지 읽지 못한 채 연결이 종료됨
- 선언된 header/body 길이가 구현 제한을 초과함
- packet 조립 중 내부 버퍼 확장이나 조립이 실패함

공개 동작은 아래 방향으로 둔다.

- malformed packet은 packet callback으로 부분 delivery 하지 않는다.
- malformed packet이 감지되면 해당 연결은 packet mode 기준 invalid stream으로
  처리한다.
- 기본 처리 방향은 연결 종료다.
- 호출자는 socket monitor 경로에서 이 실패를 관찰할 수 있어야 한다.

이 문서 단계에서는 별도 packet error callback은 추가하지 않는다.

### 8.1 길이 제한

이 초안은 아래 구현 제한을 둘 수 있다고 본다.

- 최대 header size
- 최대 body size
- 최대 packet size

제한을 넘는 길이는 malformed packet으로 처리한다.

정확한 공개 option 이름과 기본값은 구현 단계에서 확정한다.

## 9. poller와의 관계

packet callback 모드도 raw callback 모드와 같은 callback receive 모델로 본다.

- `zlink_stream_packet_handler()`가 붙은 handle에서는 data-plane `ZLINK_POLLIN`
  등록이 `EBUSY`로 실패한다.
- packet callback은 recv 표면을 대신하는 수신 모델이다.

즉 packet callback 모드는 "recv와 같이 쓸 수 있는 packet convenience callback"이
아니라, `STREAM`의 별도 수신 모드다.

## 10. thread와 실행 문맥

이 문서는 packet callback의 실행 문맥을 기존 raw `STREAM` callback과 같은 축으로
둔다.

- callback은 `STREAM` 수신을 소유한 I/O 실행 문맥에서 호출할 수 있다.
- 직렬화와 packet assembly state의 기준 단위는 현재 활성 client 연결의
  `source_rid_`다.
- implementation은 live connection별로 `source_rid_` key를 사용해 packet
  assembly state를 관리하고, 연결 종료 시 해당 key의 state를 즉시 정리해야 한다.
- 같은 client `source_rid_`에 대한 packet callback은 순서대로 직렬 실행되어야
  한다.
- 서로 다른 client `source_rid_`의 packet callback은 병렬 실행될 수 있다.
- callback 안에서 오래 걸리는 사용자 로직을 실행하면 같은 client 연결 또는 같은
  실행 문맥의 진행을 늦출 수 있다.
- callback 안에서 self-close는 raw `STREAM` callback과 같은 규칙을 따른다.

즉 packet callback은 사용성을 높이지만, dispatch executor를 새로 제공하는 모델은
아니다.

## 11. recv와 packet callback의 역할 분리

이 초안에서 raw recv와 packet callback은 역할이 다르다.

- `zlink_recv()`:
  raw stream fragment 수신
- `zlink_recv_handler()`:
  raw stream callback 수신
- `zlink_stream_packet_handler()`:
  조립된 packet 수신

이 문서는 packet 단위 recv 함수는 추가하지 않는다.

이유는 아래와 같다.

- 첫 단계에서는 callback 기반 packet delivery만으로도 사용성 이득이 크다.
- packet recv까지 함께 추가하면 `STREAM` 전용 표면이 급격히 커진다.
- recv, raw callback, packet callback 세 모델만으로도 구현과 문서 범위가 충분히
  커진다.

## 12. 기대 효과

이 개정으로 기대하는 효과는 아래와 같다.

- 응용이 stream fragment 조립 상태를 직접 들고 있지 않아도 된다.
- 바인딩 라이브러리에서 packet 누적, 길이 해석, 추가 복사를 줄일 수 있다.
- 고정 framing 규약을 쓰는 상위 프로토콜에서 `STREAM` 사용성이 좋아진다.
- `STREAM` 예외 타입의 성격이 더 분명해진다.

## 13. 구현 순서 기준

### 13.1 packet callback 타입과 함수 추가

먼저 아래 공개 타입과 함수를 헤더 초안 수준으로 정리한다.

- `zlink_stream_packet_handler_fn`
- `zlink_stream_packet_handler()`

### 13.2 STREAM mode gate 추가

그 다음 `STREAM` receive mode gate를 아래 세 모델 기준으로 확장한다.

- raw recv
- raw callback
- packet callback

### 13.3 framing parser 구현

그 다음 연결별 누적 버퍼와 framing parser를 구현한다.

- 2바이트 header size
- 4바이트 body size
- header payload
- body payload

순서대로 읽고, packet 하나가 완성될 때마다 callback delivery를 만든다.

### 13.4 malformed packet 처리

길이 제한 초과, 불완전 packet 종료, 조립 실패를 연결 종료 정책과 맞춘다.

### 13.5 테스트 추가

아래 회귀 테스트를 추가하거나 강화한다.

- 하나의 raw fragment 안에 packet 하나가 온 경우
- 여러 fragment에 걸쳐 packet 하나가 나뉘어 온 경우
- 하나의 raw fragment 안에 packet 여러 개가 붙어 온 경우
- header size = 0
- body size = 0
- header/body 둘 다 0
- malformed length
- 제한 초과 packet
- raw recv / raw callback / packet callback 상호 배타 `EBUSY`
- packet callback에서 ownership close 규칙 검증
- `source_rid_` borrowed view가 callback 이후에는 재사용되지 않는지
- 같은 `source_rid_`에 대한 packet callback이 겹치지 않고 순서대로 실행되는지
- 연결 종료 시 해당 `source_rid_`의 packet assembly state가 정리되는지
- 서로 다른 `source_rid_`에서는 socket 전체 직렬화를 강제하지 않는지

## 14. 남은 확인 사항

구현 전 마지막으로 확인해야 할 항목은 아래와 같다.

- 최대 header/body/packet 크기를 option으로 열지, 고정 내부 제한으로 둘지
- 길이 필드의 endian을 옵션 없이 완전 고정할지

## 15. 정식 spec 분해 계획

구현과 공개 헤더가 정리되면 이 초안 내용은 아래 문서들로 나누어 반영한다.

- `doc/spec/core/socket/stream*.md`
  packet callback 표면, framing 규약, mode gate
- `doc/spec/core/socket/README*.md`
  STREAM 예외 수신 모델 표
- 필요하면 `doc/spec/core/errno-map*.md`
  mode conflict와 malformed packet 진단 정리
