[Framework 초안](./README.ko.md)

# Draft -- ZLink Stream Connector

> 이 문서는 **릴리스 전 초안**이다.
> 현재 공개 계약이 아니며, 아직 `core/include/zlink.h`, 언어별 바인딩, 패키지,
> 테스트로 확정된 API가 아니다.
> 릴리스 전에 실제 public API와 테스트에 맞춰 정식 spec 문서로 나누어
> 반영한다.

## 1. 목적

`ZLink Stream Connector`는 `ZLink STREAM` 서버에 접속하는 클라이언트 쪽
커넥터다. 서버 framework의 `STREAM` packet callback이 받는 것과 같은
`header + payload` 단위 메시지를 클라이언트에서도 보내고 받을 수 있게 한다.

이 클라이언트는 특정 게임 서버 모델이 아니다. room, actor, account, stage 같은
도메인 개념은 넣지 않는다. 사용자는 이 커넥터 위에서 자기 애플리케이션에 맞는
채팅 클라이언트, 게임 클라이언트, 장비 제어 클라이언트, 알림 클라이언트 같은
상위 모델을 만든다.

Stream Connector는 framework adapter 구현물에 묶여서만 배포되는 기능이 아니다.
각 언어별 connector는 별도 package로 배포할 수 있어야 한다. 애플리케이션 개발자는
ZLink 서버 framework 전체를 참조하지 않고도 connector package만 받아서 TCP, TLS, WS,
WSS transport로 STREAM 서버에 접속할 수 있어야 한다.

## 2. 문서 범위

이 문서는 모든 언어가 따라야 하는 공통 의미만 정의한다.
언어별 API 이름, 패키지 이름, runtime adapter, 샘플 코드는 각 언어별 draft 문서에
따로 둔다.

현재 언어별 상세 문서:

- [.NET stream connector](./framework-adapter/bindings/dotnet/streaming-client.ko.md)

## 3. 메시지 단위

기본 메시지 단위는 `header + payload`다. 단, 일반 사용자가 매번 별도 header schema를
정의하지 않아도 되도록 상위 helper는 아래 의미를 제공한다.

- packet 이름은 기본적으로 payload 타입 이름 또는 사용자가 지정한 문자열 이름이다.
- optional metadata는 작은 key-value 객체다.
- payload는 사용자가 선택한 payload다.
- JSON, MessagePack, Protobuf helper는 payload 객체를 직렬화하고, packet 이름과
  optional metadata를 공통 header로 만든다.
- raw packet API는 `header`와 `payload`를 byte sequence로 직접 보낼 수 있어야 한다.

Stream Connector core transport는 raw `header` 내용을 해석하지 않는다. packet 이름,
metadata, correlation id 같은 helper 규칙은 core packet 전송 위에 얹는 공통 helper
계층에서 다룬다. 서버 framework는 기존처럼 `header, payload`를 받는다. helper를
추가하기 위해 서버 STREAM callback 계약을 바꾸지 않는다.

서버의 packet callback과 대응되는 관계는 아래와 같다.

| 서버 framework | Stream Connector |
|----------------|------------------|
| `OnPacket(header, payload)` 수신 | packet callback 또는 receive API로 `header`, `payload` 수신 |
| `stream.Write(header, payload)` 송신 | send API로 `header`, `payload` 송신 |
| STREAM 연결 종료 callback | disconnected/error callback |

## 4. 필수 Transport

초기 공개 범위는 아래 네 transport를 모두 포함한다.

- TCP
- TLS over TCP
- WebSocket
- WebSocket over TLS

transport 이름은 언어별 표면에서 다르게 보일 수 있지만 의미는 같아야 한다.

| 공통 transport | URI 예 | 의미 |
|----------------|--------|------|
| TCP | `tcp://127.0.0.1:18082` | 암호화 없는 TCP 연결 |
| TLS over TCP | `tls://example.com:18082` | TLS로 보호되는 TCP 연결 |
| WebSocket | `ws://example.com/ws` | WebSocket binary message 연결 |
| WebSocket over TLS | `wss://example.com/ws` | TLS로 보호되는 WebSocket 연결 |

모든 transport는 같은 `header + payload` packet 의미를 유지해야 한다. 사용자가
transport를 바꾸더라도 상위 packet 처리 코드는 가능하면 바뀌지 않아야 한다.

## 5. Connector 기능 범위

기존 `playhouse/connectors`에서 가져올 범용 기능은 아래와 같다. 단, 이름과 타입은
각 언어 관용구에 맞게 다시 정한다.

### 5.1 연결 lifecycle

- connect
- async connect
- disconnect / close
- connected 상태 조회
- graceful close
- remote close 감지
- 연결 실패 감지
- 동일 프로세스 안에서 여러 connector 인스턴스 독립 실행

### 5.2 Transport 설정

- transport 선택
- host, port, path 또는 endpoint URI 지정
- TLS 사용
- server certificate validation 정책
- 테스트용 인증서 검증 생략 옵션
- connect timeout
- heartbeat option
- reconnect option

### 5.3 Packet 송수신

- `header + payload` packet send
- raw byte payload send
- payload 타입 이름 또는 지정한 packet 이름으로 packet send
- optional metadata 지정
- payload compression flag 처리
- zero-copy 또는 copy 감소 send 경로
- segmented send 경로
- packet callback 수신
- receive API 수신
- push message 수신
- partial read 처리
- 여러 packet이 한 번에 들어온 경우 순서대로 dispatch
- send/request 전에 송신 frame 크기 제한 검사
- large payload 수신 처리

### 5.4 요청/응답 helper

request/response는 core packet 전송 위의 선택 helper다. 도메인 protocol은 아니지만,
많은 client가 필요로 하므로 Stream Connector 기능 범위에는 포함한다.

- request callback 방식
- async request 방식
- send helper와 같은 packet 이름, optional metadata, payload 객체 규칙
- request timeout
- pending request 관리
- response correlation
- timeout 발생 시 pending request 정리
- 연결 종료 시 pending request 실패 처리

correlation id의 위치와 header 형식은 공통 byte header 위의 helper 규칙으로
정한다. helper header는 작은 metadata key-value를 담을 수 있어야 하지만, core
transport가 특정 직렬화 포맷을 강제하면 안 된다.

### 5.5 Error 처리

- disconnected 상태에서 send/request 시 error 반환 또는 callback 호출
- request timeout error
- frame decode error
- send payload size limit error
- transport connect error
- TLS certificate validation error
- remote close
- user callback 예외 격리

언어별 API가 exception 기반인지 result 기반인지는 각 언어 문서에서 정한다. 다만
error code의 의미는 언어 간 최대한 맞춘다.

### 5.6 Callback dispatch

일반 connector core는 callback thread를 숨기지 않는다. callback이 receive loop 또는
worker thread에서 호출될 수 있음을 문서화한다.

runtime adapter는 해당 runtime의 주 스레드 규칙을 맞춘다.

- Unity adapter는 Unity main thread에서 사용자 callback을 호출한다.
- Unreal plugin은 Game Thread에서 사용자 callback을 호출한다.
- 일반 서버/콘솔용 library는 특정 UI thread를 기본으로 가정하지 않는다.

### 5.7 Codec extension

core connector는 byte packet을 최저 레벨 API로 제공한다. 아래 codec helper는 선택
extension 또는 별도 package로 둔다.

- JSON
- MessagePack
- Protobuf

codec extension은 packet 이름 결정, optional metadata 생성, payload parse를
돕지만, transport framing과 request lifecycle을 바꾸면 안 된다.

### 5.8 Compression

compression은 payload에만 적용한다. header는 routing, request correlation, codec,
metadata를 담기 때문에 압축하지 않는다.

server-to-client 방향은 typed API에서 자동 해제를 제공한다.

- 서버 쪽 send helper는 설정한 threshold 이상인 payload를 압축할 수 있다.
- 압축된 payload를 보낼 때 helper header `flags`에 `payload_compressed`를 표시한다.
- client connector는 typed 수신 경로에서 helper header에 `payload_compressed`가 있으면
  payload를 자동으로 압축 해제한다.
- typed client callback이나 request reply에는 압축 해제된 payload를 전달한다.
- raw packet callback이나 raw receive API는 transport에서 받은 header와 payload를 그대로
  전달한다.

client-to-server 방향은 명시 요청일 때만 압축한다.

- 기본 client send/request는 압축하지 않는다.
- 사용자가 send/request builder에서 compression을 명시한 경우에만 payload를 압축한다.
- 서버 framework는 기존처럼 `header, payload`를 받는다.
- 서버 쪽 helper나 actor adapter가 helper header의 `payload_compressed` flag를 보고
  필요하면 payload를 압축 해제한다.

압축 알고리즘은 packet마다 header에 넣지 않고 connector 또는 서버 send helper 설정에
둔다. 현재 계약에서 지원하는 압축 알고리즘은 LZ4 하나다. 서버와 클라이언트가 같은
알고리즘을 사용하도록 설정하는 책임은 사용자에게 있다. 한 연결 안에서 여러 압축
알고리즘을 섞는 모델은 범위에 넣지 않는다.

## 6. 빼야 할 범위

아래 기능은 Stream Connector core에 넣지 않는다.

- room 생성, room 입장, stage, actor 같은 도메인 모델
- 인증 packet 이름과 인증 payload schema
- game server 전용 error code
- 특정 게임 protocol의 message id 목록
- 특정 직렬화 포맷 강제
- 서버 상태 저장소
- match making, lobby, party 같은 상위 서비스 모델

이 기능들은 사용자가 Stream Connector 위에 만드는 애플리케이션 protocol 또는
별도 framework extension에서 다룬다.

## 7. Framing 원칙

framing은 서버의 STREAM packet callback과 정확히 맞아야 한다.

- 한 packet은 `header_size`, `payload_size`, `header`, `payload` 순서로 인코딩한다.
- `header_size`는 2바이트 unsigned integer다.
- `payload_size`는 4바이트 unsigned integer다.
- size 값은 network byte order를 사용한다.
- header와 payload는 각각 빈 값일 수 있다.
- connector가 보내는 frame에는 최대 크기 제한을 적용한다.
- partial read와 여러 packet이 한 번에 들어오는 경우를 모두 처리한다.
- frame decode 실패는 연결 error로 보고 사용자에게 알려야 한다.

수신 payload에 대한 도메인별 크기 제한은 Stream Connector 공통 계약에 넣지 않는다.
필요한 애플리케이션은 handler나 상위 protocol에서 별도로 검사한다. 구현은 메모리
보호를 위해 내부 safety limit을 둘 수 있지만, 그 값은 도메인 protocol 계약으로
보장하지 않는다.

```text
+----------------+----------------+----------------+----------------+
| u16 header_len | u32 payload_sz | header bytes   | payload bytes  |
+----------------+----------------+----------------+----------------+
```

`header_size`는 header byte 수를 나타내는 frame prefix다.

connector helper header는 binary header다. 문자열 JSON envelope를 header에 그대로
넣지 않는다. `kind`와 `codec`은 문자열이 아니라 1바이트 enum으로 인코딩한다.

```text
+--------+---------+----------+----------+-----------+-----------+
| kind u8| codec u8| flags u8 | rid u64? | name u8+n | meta?     |
+--------+---------+----------+----------+-----------+-----------+
```

필드 순서와 byte order는 아래 규칙으로 고정한다.

- 모든 multi-byte integer는 network byte order를 사용한다.
- `kind`: message kind enum이다.
- `codec`: payload codec enum이다.
- `flags`: 선택 필드 존재 여부와 확장 bit를 담는다.
- `rid`: request correlation id다. request, response, error response에만 들어간다.
- `name`: `u8 name_len` 뒤에 UTF-8 packet name이 온다.
- `meta`: `u16 meta_len` 뒤에 metadata bytes가 온다. metadata가 있을 때만 들어간다.

`rid`가 있으면 항상 `flags`에 `has rid`를 켠다. `meta`가 있으면 항상 `has metadata`를
켠다. flag와 실제 필드 존재 여부가 맞지 않으면 decode error다. 알 수 없는 `kind`,
`codec`, flag bit는 decode error다.

connector가 생성하는 `rid`는 같은 connector instance 안에서 동시에 pending 상태인
request 사이에 중복되면 안 된다. 값 `0`은 사용하지 않는다.

`name_len`은 1 이상 255 bytes 이하여야 한다. 긴 namespace 전체 이름을 header에 그대로 넣지 말고,
짧은 packet name 또는 alias를 사용한다. `meta_len`은 wire에서 `u16`이지만 connector
기본 최대값은 1024 bytes로 둔다. 구현은 옵션으로 이 값을 낮추거나 높일 수 있지만,
65535 bytes를 넘길 수 없다. metadata에는 trace id, tenant id, locale처럼 작은 값만
넣고 큰 업무 payload는 payload에 넣는다.

metadata bytes는 아래 순서의 binary key-value 목록이다.

```text
+---------------+-------------+-------------+
| count u8      | entry...    | entry...    |
+---------------+-------------+-------------+

entry:
+-------------+-------------+-------------+-------------+
| key_len u8  | key bytes   | val_len u16 | value bytes |
+-------------+-------------+-------------+-------------+
```

metadata key와 value는 UTF-8 문자열이다. `key_len`은 1 이상이어야 한다. 같은 key가
두 번 나오면 decode error다. `count`는 뒤따르는 entry 개수와 정확히 일치해야 한다.

enum 값은 아래처럼 둔다.

| enum | value | 의미 |
|------|-------|------|
| kind send | `1` | 응답을 기대하지 않는 message |
| kind request | `2` | response를 기대하는 request |
| kind response | `3` | request 성공 response |
| kind error | `4` | request 실패 response 또는 connector error |
| kind control | `5` | heartbeat 같은 connector 내부 control frame |
| codec raw | `0` | codec helper를 쓰지 않는 bytes |
| codec json | `1` | JSON payload |
| codec messagepack | `2` | MessagePack payload |
| codec protobuf | `3` | Protobuf payload |

`flags` bit는 아래처럼 둔다.

| flag | value | 의미 |
|------|-------|------|
| has rid | `0x01` | `rid` 필드가 있다 |
| has metadata | `0x02` | `meta` 필드가 있다 |
| payload compressed | `0x04` | payload가 압축되어 있다 |

`kind=control`은 응용 handler로 전달하지 않는 connector 내부 frame이다.
현재 예약된 control packet 이름은 `$zlink.heartbeat.ping`과 `$zlink.heartbeat.pong`이다.
control frame은 `codec=raw`, flags 없음, request id 없음, metadata 없음, 빈 payload를
사용한다. `$zlink.`로 시작하는 packet 이름은 connector 내부 예약 영역이므로 응용 packet
이름으로 사용할 수 없다.

서버 framework는 이 helper header를 몰라도 기존처럼 `header, payload`를 받을 수 있어야
한다. helper를 쓰는 서버 쪽 adapter나 actor helper는 `header bytes`를 위 형식으로
파싱한 뒤 packet name과 payload 객체를 처리한다.

request/response 규칙은 아래와 같다.

- `send`: `rid`가 없어야 한다. response를 기대하지 않는다.
- `request`: `rid`가 있어야 한다. connector가 pending request map에 등록한다.
- `response`: `rid`가 있어야 한다. 같은 `rid`의 pending request를 완료한다.
- `error`: `rid`가 있으면 request 실패 response다. `rid`가 없으면 connector error
  message다.
- `response`와 `error`의 packet name은 원 request packet name과 같아야 한다.
- `error`는 `codec=json`을 사용한다.
- request timeout, close, disconnect 시 pending request는 실패 처리하고 map에서
  제거한다.

`error` payload는 codec과 무관하게 UTF-8 JSON object로 인코딩한다.

```json
{"code":"error_code","message":"message"}
```

error payload schema는 connector helper 전용이다. 애플리케이션 도메인 error를 payload로
보내고 싶으면 `response` kind와 사용자 payload schema를 사용한다.

## 8. 연결 생명주기

Connector 생성과 네트워크 연결은 분리한다. 생성 함수는 connector 객체를 만들 뿐이며,
실제 연결은 `ConnectAsync()` 같은 명시적 연결 API에서 시작한다.

Heartbeat와 reconnect는 옵션 객체가 있을 때만 켜진다.

- heartbeat를 켜면 connector는 1초 기본 interval로 ping을 보내고, 5초 기본 timeout 동안
  inbound frame이 없으면 연결을 끊긴 것으로 처리한다.
- reconnect를 켜면 끊긴 연결에 대해 `250ms -> 500ms -> 1s` 순서로 기본 세 번 재연결을
  시도한다.
- reconnect 중에는 새 send/request를 queue에 저장하지 않는다. submit은 disconnected
  오류로 실패한다.
- 연결이 끊기면 pending request는 모두 실패하며, reconnect 뒤 자동 재전송하지 않는다.
- close/dispose는 terminal 동작이다. 같은 connector 객체를 다시 연결하지 않는다.

언어별 connector는 상태 이름과 이벤트 표면은 각 언어 관용구에 맞춰도 되지만, 상태 전이,
heartbeat timeout 기준, reconnect delay 계산, pending request 실패 규칙은 같은 의미로
맞춘다.

## 9. 완료 기준

공통 완료 기준은 아래와 같다.

- TCP, TLS, WS, WSS transport가 모두 같은 packet API로 동작한다.
- client가 보낸 `header + payload` packet을 framework STREAM 서버 packet callback이
  받는다.
- client가 payload 객체만 넘겨도 packet 이름과 payload가 만들어지고, optional metadata를
  함께 보낼 수 있다.
- 서버가 `stream.Write(header, payload)`로 보낸 packet을 client가 받는다.
- callback receive와 explicit receive API가 모두 검증된다.
- request callback과 async request가 timeout, response, close 상황을 처리한다.
- heartbeat control ping/pong, heartbeat timeout, reconnect 성공/실패를 테스트한다.
- partial read, multi-packet read, send frame limit, close 중 send 실패를 테스트한다.
- codec extension이 core transport 계약을 바꾸지 않는지 검증한다.
- server-to-client compressed payload를 client connector typed API가 자동으로 압축
  해제한다.
- client-to-server compression은 명시 호출에서만 적용된다.
- Unity adapter는 main thread callback dispatch를 검증한다.
- Unreal plugin은 Game Thread callback dispatch를 검증한다.

## 10. 결정 사항

- payload type 이름을 기본 packet 이름으로 쓸 때는 namespace를 제외한 짧은 타입 이름을
  사용한다. 다른 이름이 필요하면 언어별 attribute나 resolver를 사용한다.
- TLS certificate validation option 이름은 언어별 관용구를 따르되, 의미는 "server
  certificate validation 생략"으로 맞춘다.
- WebSocket path는 endpoint URI에 포함한다. 별도 path option을 두지 않는다.
- codec extension은 Stream Connector package 계열의 별도 package로 둔다.
