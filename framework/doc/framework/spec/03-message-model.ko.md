# Framework 메시지 계약

[스펙 목차](README.ko.md) · [상호작용 모델](02-interaction-model.ko.md) ·
[비동기 실행](04-async-execution-policy.ko.md)

이 문서는 ZLink Framework 11.0.0의 typed 메시지, application metadata, 응답과 오류 계약을 정의한다.
대상 독자는 framework 공개 계약과 언어별 service runtime을 구현하는 개발자다. Framework envelope와 내부
multipart encoding은 모든 언어가 같은 wire schema와 golden fixture를 사용하고, 각 언어 service runtime이
raw transport 위에서 처리한다. Application에는 이 형식을 노출하지 않는다.

## 1. Typed 메시지

application은 payload type과 등록된 handler를 기준으로 메시지를 보낸다. Framework는 기본 typed JSON
serializer를 사용해 payload를 encoding하고 수신 handler의 인자 type으로 decoding한다. 호출자는 메시지
type마다 codec, serializer registry, encoder 또는 decoder를 등록하지 않는다.

별도의 wire format이 필요한 package는 framework가 정의한 codec extension을 사용할 수 있다. extension은
host 단위 정책이며 업무 handler나 개별 send/request 호출에 반복해서 전달하지 않는다. raw bytes를 직접
다루는 API는 transport 검사와 codec extension 구현에만 사용한다.

## 2. 메시지 종류

| 종류 | 의미 | 완료 |
|---|---|---|
| Send | 대상 handler에 한 번 전달하는 one-way 메시지 | submit 결과만 반환하며 원격 handler 완료를 기다리지 않는다 |
| Request | 대상 handler가 reply 또는 오류를 반환하는 메시지 | reply, 오류, timeout 또는 cancellation로 한 번 완료된다 |
| Logical Multicast | target ChannelName의 각 MeshNode에서 조건에 맞는 Spot에 발행하는 메시지 | target별 ROUTER·local queue 제출을 집계한 submit 결과를 반환한다 |
| Classic fanout publish | 독립 fanout channel의 subscriber에 발행하는 메시지 | local publisher transport의 bounded admission 결과를 반환하며 subscriber 수신은 확인하지 않는다 |
| STREAM send/request | 연결된 session에 보내는 one-way 메시지 또는 reply를 요구하는 메시지 | session sequence와 lifecycle 계약을 따른다 |

Request의 reply 상관관계는 transport가 발급한 operation ID 또는 session sequence가 소유한다. packet name이나
application metadata를 reply matching key로 사용하지 않는다. reply는 성공 payload와 framework 오류 중 하나로
완료되며 같은 request를 두 번 완료할 수 없다.

Object creation request는 일반 Send·Request와 다른 manager operation 입력이다. Framework는 typed codec으로
encode한 최대 1 MiB payload의 immutable content reference와 hash를 placement reservation 전에 durable creation
intent에 기록한다. Factory는 logical key, ObjectGeneration과 creation attempt를 함께 받아 같은 attempt의
at-least-once 실행에도 같은 결과로 수렴해야 한다. CAS loser는 creation request를 일반 message로 보내지 않는다.
Ready commit 또는 fenced failure cleanup이 끝날 때까지 content reference를 유지한다. ObjectGeneration,
AuthorityOwnerGeneration, attempt와 owner lease token은 Store fencing에만 사용하며 application message payload나
handler context에 포함하지 않는다.

## 3. Application metadata

Application metadata는 업무 payload와 별도로 전달하는 작은 key-value snapshot이다. Node direct, ChannelName,
Spot direct, Actor와 STREAM send/request가 같은 계약을 사용한다.

| 항목 | 계약 |
|---|---|
| key와 value | UTF-8이며 NUL을 포함하지 않는다 |
| 전체 크기 | encoding된 key와 value 및 구조 overhead를 포함해 최대 1024 bytes다 |
| 같은 key | outbound builder에서 마지막으로 설정한 값이 적용된다 |
| 수신 | handler context가 변경할 수 없는 snapshot을 제공한다 |
| lifetime | handler turn이 끝날 때까지 유효하며 보관하려면 application이 복사한다 |
| malformed input | handler를 호출하지 않고 protocol 오류로 처리한다 |
| reply | request metadata를 자동으로 복사하지 않으며 일반 reply에 metadata setter를 제공하지 않는다 |

Metadata의 내부 frame 배치와 encoding은 공개 계약이 아니다. Framework는 payload와 metadata의 경계를
유지하고, relay가 필요한 경로에서도 application이 frame을 조립하거나 parsing하게 하지 않는다.

### 3.1 Message Context

Inbound handler가 받는 현재 message 정보는 object lifecycle Context와 구분한다. 공통 이름은
`MessageContext`이며 nullable MeshName, nullable ChannelName, packet name, nullable content type, immutable
metadata와 UTF-8 exact nullable correlation을 제공한다. Correlation은 send에서 null이고 request에서
non-null이다. MeshName은 RouteMesh와 Spot·Actor dispatch에서 non-null이며 ClientServer·STREAM처럼 Mesh에
속하지 않는 경로에서는 null이다. Connection cancellation은 universal MessageContext에 넣지 않고 언어별
handler cancellation 인자나 Session 전용 context가 소유한다. Actor나 Spot identity·operation capability를
Message Context에 넣지 않는다.

Send와 request는 고유 field가 없으므로 별도 `SendContext`, `RequestContext` 또는 Spot Actor 전용 context를
제공하지 않는다. Node direct는 source node를 추가한 `RouteMessageContext`, Publish는 topic과 nullable source를
추가한 `PublishMessageContext`를 사용한다. STREAM Session은 reply 가능 여부와 session 전용 정보를 제공하므로
`SessionMessageContext`를 유지한다. Handler filter의 descriptor, payload와 chain 실행 객체는 Message Context가
아니며 `HandlerInvocation`으로 부른다.

일반 request reply payload는 handler 반환값을 Framework 고정 policy로 encode한다. STREAM Session Reply call
이외에는 reply metadata/compression option 또는 Reply builder를 제공하지 않는다. 따라서 Spot Actor 전용
reply option도 없다.

## 4. 전달 규칙

| 경로 | metadata 전달 |
|---|---|
| Node direct와 ChannelName | source snapshot을 선택된 MeshNode의 handler context에 전달한다 |
| Spot | global Spot ID의 current Ready owner에 있는 application claim에 전달한다 |
| Logical Multicast | 같은 publish snapshot을 각 matching Spot handler에 전달한다 |
| Actor | Actor handler context에 전달하며 Spot callback을 거치지 않는다 |
| STREAM session | session send/request context에 전달한다 |
| bound session에서 Actor로 relay | root metadata policy의 session-to-actor allowlist가 허용한 key만 전달한다 |
| Actor에서 bound session으로 relay | root metadata policy의 actor-to-session allowlist가 허용한 key만 전달한다 |

Framework가 새 request를 만드는 경우에는 원본 metadata를 자동 복사하지 않는다. 호출자가 현재 handler의
metadata를 명시적으로 넘긴 경우에만 새 outbound snapshot에 포함한다. 자동 전파가 필요한 trace 정보는
[메시지 흐름 상관관계](server/53-flow-correlation.ko.md)가 별도 framework field로 관리한다.

## 5. Ownership과 크기 제한

submit 호출이 반환되기 전까지 outbound builder와 payload는 호출자가 소유한다. Framework가 submit을
수락하면 필요한 payload와 metadata reference 또는 복사본을 operation lifetime 동안 유지한다. 호출자가
transport buffer, native message pointer 또는 multipart part의 lifetime을 관리하게 하지 않는다.

Object creation이 pending인 동안에도 같은 ownership 규칙이 적용된다. Location Store I/O와 factory가 caller의
payload object나 native buffer 수명에 의존하지 않도록 Framework service runtime이 immutable encoded payload를
content store에 고정한다. Ready 또는 fenced failure 뒤에는 해당 attempt가 소유한 payload storage를 한 번
해제한다.

payload 최대 크기는 대상 transport의 `MaxMessageSize`를 따른다. 전체 message가 제한을 넘으면 일부 part를
전달하지 않고 submit 또는 receive 전체가 실패한다. Logical Multicast의 target별 제출과 결과 집계는
[Spot 메시징](server/20-spot-messaging.ko.md)이 정의한다.
