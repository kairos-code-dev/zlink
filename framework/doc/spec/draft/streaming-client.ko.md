[Framework 초안](./README.ko.md)

# Draft -- ZLink Streaming Client

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 아직 `core/include/zlink.h`, 언어별 바인딩, 패키지,
> 테스트로 확정된 API가 아니다.
> 구현이 끝난 뒤에는 실제 public API와 테스트에 맞춰 정식 spec 문서로 나누어
> 반영한다.

## 1. 목적

`ZLink Streaming Client`는 `ZLink STREAM` 서버에 접속하는 클라이언트 쪽
커넥터다. 서버 framework의 `STREAM` packet callback이 받는 것과 같은
`header + body` 단위 메시지를 클라이언트에서도 보내고 받을 수 있게 한다.

이 클라이언트는 특정 게임 서버 모델이 아니다. room, actor, account, stage 같은
도메인 개념은 넣지 않는다. 사용자는 이 커넥터 위에서 자기 애플리케이션에 맞는
채팅 클라이언트, 게임 클라이언트, 장비 제어 클라이언트, 알림 클라이언트 같은
상위 모델을 만든다.

## 2. 문서 범위

이 문서는 모든 언어가 따라야 하는 공통 의미만 정의한다.
언어별 API 이름, 패키지 이름, runtime adapter, 샘플 코드는 각 언어별 draft 문서에
따로 둔다.

현재 언어별 상세 문서:

- [.NET streaming client](./framework-adapter/bindings/dotnet/streaming-client.ko.md)

## 3. 메시지 단위

기본 메시지 단위는 `header + body`다. 단, 일반 사용자가 매번 별도 header schema를
정의하지 않아도 되도록 상위 helper는 아래 의미를 제공한다.

- packet 이름은 기본적으로 body 타입 이름 또는 사용자가 지정한 문자열 이름이다.
- optional header는 작은 metadata 객체다. JSON object로 표현할 수 있어야 한다.
- body는 사용자가 선택한 payload다.
- JSON, MessagePack, Protobuf helper는 body 객체를 직렬화하고, packet 이름과
  optional header metadata를 공통 header로 만든다.
- raw packet API는 `header`와 `body`를 byte sequence로 직접 보낼 수 있어야 한다.

streaming client core transport는 raw `header` 내용을 해석하지 않는다. packet 이름,
metadata, correlation id 같은 helper 규칙은 core packet 전송 위에 얹는 공통 helper
계층에서 다룬다. 서버 framework는 기존처럼 `header, body`를 받는다. helper를
추가하기 위해 서버 STREAM callback 계약을 바꾸지 않는다.

서버의 packet callback과 대응되는 관계는 아래와 같다.

| 서버 framework | streaming client |
|----------------|------------------|
| `OnPacket(header, body)` 수신 | packet callback 또는 receive API로 `header`, `body` 수신 |
| `stream.Write(header, body)` 송신 | send API로 `header`, `body` 송신 |
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

모든 transport는 같은 `header + body` packet 의미를 유지해야 한다. 사용자가
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
- idle timeout
- heartbeat interval
- heartbeat timeout

### 5.3 Packet 송수신

- `header + body` packet send
- raw byte payload send
- body 타입 이름 또는 지정한 packet 이름으로 packet send
- optional JSON object header metadata 지정
- body compression flag 처리
- zero-copy 또는 copy 감소 send 경로
- segmented send 경로
- packet callback 수신
- receive API 수신
- push message 수신
- partial read 처리
- 여러 packet이 한 번에 들어온 경우 순서대로 dispatch
- large payload 처리와 최대 frame 크기 제한

### 5.4 요청/응답 helper

request/response는 core packet 전송 위의 선택 helper다. 도메인 protocol은 아니지만,
많은 client가 필요로 하므로 streaming client 기능 범위에는 포함한다.

- request callback 방식
- async request 방식
- send helper와 같은 packet 이름, optional JSON object header, body 객체 규칙
- request timeout
- pending request 관리
- response correlation
- timeout 발생 시 pending request 정리
- 연결 종료 시 pending request 실패 처리

correlation id의 위치와 header 형식은 공통 byte header 위의 helper 규칙으로
정한다. helper header는 JSON object로 표현 가능한 metadata를 담을 수 있어야 하지만,
core transport가 특정 직렬화 포맷을 강제하면 안 된다.

### 5.5 Error 처리

- disconnected 상태에서 send/request 시 error 반환 또는 callback 호출
- request timeout error
- frame decode error
- payload size limit error
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

codec extension은 packet 이름 결정, optional header metadata 생성, payload parse를
돕지만, transport framing과 request lifecycle을 바꾸면 안 된다.

### 5.8 Compression

compression은 body에만 적용한다. header는 routing, request correlation, codec,
metadata를 담기 때문에 압축하지 않는다.

server-to-client 방향은 자동 해제를 제공한다.

- 서버 쪽 send helper는 설정한 threshold 이상인 body를 압축할 수 있다.
- 압축된 body를 보낼 때 helper header `flags`에 `body_compressed`를 표시한다.
- client connector는 수신한 helper header에 `body_compressed`가 있으면 body를
  자동으로 압축 해제한다.
- client 사용자 callback이나 receive API에는 압축 해제된 body를 전달한다.

client-to-server 방향은 명시 요청일 때만 압축한다.

- 기본 client send/request는 압축하지 않는다.
- 사용자가 send/request builder에서 compression을 명시한 경우에만 body를 압축한다.
- 서버 framework는 기존처럼 `header, body`를 받는다.
- 서버 쪽 helper나 actor adapter가 helper header의 `body_compressed` flag를 보고
  필요하면 body를 압축 해제한다.

압축 알고리즘은 packet마다 header에 넣지 않고 connector 또는 서버 send helper 설정에
둔다. 한 연결 안에서 여러 압축 알고리즘을 섞는 모델은 기본 범위에 넣지 않는다.
초기 후보는 LZ4다. 알고리즘 혼용이 필요해지면 helper header v2에서 별도 필드를
추가한다.

## 6. 빼야 할 범위

아래 기능은 streaming client core에 넣지 않는다.

- room 생성, room 입장, stage, actor 같은 도메인 모델
- 인증 packet 이름과 인증 payload schema
- game server 전용 error code
- 특정 게임 protocol의 message id 목록
- 특정 직렬화 포맷 강제
- 서버 상태 저장소
- match making, lobby, party 같은 상위 서비스 모델

이 기능들은 사용자가 streaming client 위에 만드는 애플리케이션 protocol 또는
별도 framework extension에서 다룬다.

## 7. Framing 원칙

framing은 서버의 STREAM packet callback과 정확히 맞아야 한다.

- 한 packet은 `header_size`, `body_size`, `header`, `body` 순서로 인코딩한다.
- `header_size`는 2바이트 unsigned integer다.
- `body_size`는 4바이트 unsigned integer다.
- size 값은 network byte order를 사용한다.
- header와 body는 각각 빈 값일 수 있다.
- 최대 크기 제한은 반드시 적용한다.
- partial read와 여러 packet이 한 번에 들어오는 경우를 모두 처리한다.
- frame decode 실패는 연결 error로 보고 사용자에게 알려야 한다.

```text
+------------------+------------------+------------------+------------------+
| u16 header_size  | u32 body_size    | header bytes     | body bytes       |
+------------------+------------------+------------------+------------------+
```

`header_size`는 header byte 수를 나타내는 frame prefix다. connector helper의 packet
version은 이 2바이트 prefix가 아니라 `header bytes` 내부에 넣는다.

connector helper header v1은 binary header다. 문자열 JSON envelope를 header에
그대로 넣지 않는다. `kind`와 `codec`은 문자열이 아니라 1바이트 enum으로 인코딩한다.

```text
+--------+---------+----------+----------+-----------+-----------+
| ver u8 | kind u8 | codec u8 | flags u8 | rid u64?  | name u8+n |
+--------+---------+----------+----------+-----------+-----------+
| meta u16+bytes?                                           |
+-----------------------------------------------------------+
```

필드 의미는 아래와 같다.

- `ver`: connector helper header version. 최초 값은 `1`이다.
- `kind`: message kind enum이다.
- `codec`: body codec enum이다.
- `flags`: 선택 필드 존재 여부와 확장 bit를 담는다.
- `rid`: request correlation id다. request, response, error response에만 들어간다.
- `name`: `u8 name_len` 뒤에 UTF-8 packet name이 온다.
- `meta`: `u16 meta_len` 뒤에 metadata bytes가 온다. metadata가 있을 때만 들어간다.

`name_len`은 최대 255 bytes다. 긴 namespace 전체 이름을 header에 그대로 넣지 말고,
짧은 packet name 또는 alias를 사용한다. `meta_len`은 wire에서 `u16`이지만 connector
기본 최대값은 1024 bytes로 둔다. 구현은 옵션으로 이 값을 낮추거나 높일 수 있지만,
65535 bytes를 넘길 수 없다. metadata에는 trace id, tenant id, locale처럼 작은 값만
넣고 큰 업무 payload는 body에 넣는다.

enum 값은 최초 draft에서 아래처럼 둔다.

| enum | value | 의미 |
|------|-------|------|
| kind send | `1` | 응답을 기대하지 않는 message |
| kind request | `2` | response를 기대하는 request |
| kind response | `3` | request 성공 response |
| kind error | `4` | request 실패 response 또는 connector error |
| codec raw | `0` | codec helper를 쓰지 않는 bytes |
| codec json | `1` | JSON body |
| codec messagepack | `2` | MessagePack body |
| codec protobuf | `3` | Protobuf body |

`flags` bit는 최초 draft에서 아래처럼 둔다.

| flag | value | 의미 |
|------|-------|------|
| has rid | `0x01` | `rid` 필드가 있다 |
| has metadata | `0x02` | `meta` 필드가 있다 |
| body compressed | `0x04` | body가 압축되어 있다 |

서버 framework는 이 helper header를 몰라도 기존처럼 `header, body`를 받을 수 있어야
한다. helper를 쓰는 서버 쪽 adapter나 actor helper는 `header bytes`를 위 형식으로
파싱한 뒤 packet name과 body 객체를 처리한다.

## 8. 완료 기준

공통 완료 기준은 아래와 같다.

- TCP, TLS, WS, WSS transport가 모두 같은 packet API로 동작한다.
- client가 보낸 `header + body` packet을 framework STREAM 서버 packet callback이
  받는다.
- client가 body 객체만 넘겨도 packet 이름과 body가 만들어지고, optional JSON object
  header metadata를 함께 보낼 수 있다.
- 서버가 `stream.Write(header, body)`로 보낸 packet을 client가 받는다.
- callback receive와 explicit receive API가 모두 검증된다.
- request callback과 async request가 timeout, response, close 상황을 처리한다.
- partial read, multi-packet read, large frame limit, close 중 send 실패를 테스트한다.
- codec extension이 core transport 계약을 바꾸지 않는지 검증한다.
- server-to-client compressed body를 client connector가 자동으로 압축 해제한다.
- client-to-server compression은 명시 호출에서만 적용된다.
- Unity adapter는 main thread callback dispatch를 검증한다.
- Unreal plugin은 Game Thread callback dispatch를 검증한다.

## 9. Open Items

- body type 이름을 기본 packet 이름으로 쓸 때 namespace와 version 정보를 어떻게
  다룰지 정해야 한다.
- 공통 packet header helper의 byte layout과 correlation id 위치를 정해야 한다.
- TLS certificate validation option 이름을 언어별로 어느 정도 맞출지 정해야 한다.
- WebSocket path와 URI 기반 설정을 동시에 둘 때 우선순위를 정해야 한다.
- codec extension을 streaming client repo 안에 둘지, 언어별 binding codec package와
  같은 정책으로 둘지 정해야 한다.
