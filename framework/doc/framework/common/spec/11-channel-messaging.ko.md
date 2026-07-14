# Channel 메시징 — 공통 스펙

[스펙 목차](README.ko.md) | [이전: ZLink Framework Channel Topology](10-channel-topology.ko.md) | [다음: SPOT 메시징](20-spot-messaging.ko.md)

> 이 문서는 **channel messaging의 런타임 계약 정본**이다. channel runtime의 수명, dispatch 실패
> 정책, startup validation, host 종료 중 호출의 의미를 소유한다.
>
> channel 종류와 topology 매핑, 자동·수동 연결은 [channel-topology](10-channel-topology.ko.md)가,
> wire의 multipart 구성은 [message-model](03-message-model.ko.md)이,
> 상호작용 모델은 [interaction-model](02-interaction-model.ko.md)이 소유한다.
>
> 언어별 등록 표면과 시그니처는 `languages/<lang>/`의 channel 문서가 고정한다.

## 1. 호출 모델

**호출자는 channel 이름만 지정한다.** gateway 주소도, 인스턴스 주소도 지정하지 않는다.

- **channel client는 등록된 channel 이름마다 별도의 channel runtime을 가진다.**
- 각 channel runtime은 자기 channel view에 묶인 **자동 연결 reconcile과 outbound 소켓**을
  가진다.
- 자동 연결 reconcile이 location store의 peer row로 그 channel view의 **provider 목록을
  유지한다**([location-runtime](40-location-runtime.ko.md)).
- framework는 그 channel의 **rid 집합과 연결 상태**를 보고 요청을 보낸다.

**이 모델의 핵심은 하나다.**

> **내부 서비스 호출에서 별도 gateway나 load balancer를 강제하지 않으면서**, core의 fixed
> channel view 철학을 그대로 이어 간다.

- **channel별 typed wrapper를 기본 표면으로 제공하지 않는다.** 공용 outbound 표면은 **channel
  client 하나로 유지한다.**
- **같은 channel 안의 여러 provider는 그 channel 안에서만 관리한다.**

## 2. channel runtime의 수명

- **channel runtime은 host startup 단계에서 등록된 역할을 보고 만든다.** host shutdown 단계에서
  정리한다.
- **lazy first-call 생성으로 숨기지 않는다.** 설정 오류가 **startup 단계에서 미리 드러나도록**
  하기 위해서다.
- **topology query를 운영용 HTTP endpoint 전용의 숨은 API로 두지 않는다.** 앱 내부에서도 쓸 수
  있는 일반 DI 서비스로 열고, 운영 API는 그 서비스를 얇게 감싼다.

## 3. Dispatch 실패 정책

**reply path가 있으면 error reply를 반환하고, 없으면 drop한다.**

| 경로 | 결과 | `action` |
|---|---|---|
| **request** — reply 상관관계를 복원할 수 있다 | **error reply를 반환한다** | `ReplyError` |
| **request** — **reply 상관관계를 복원할 수 없다** | **drop한다** | `Drop` |
| **send** | **drop** | `Drop` |
| **publish** | **drop** | `Drop` |

**request라고 해서 항상 error reply가 나가는 것이 아니다.** frame이 손상돼 **누구에게 보낼지
복원할 수 없으면 응답할 대상이 없다.** request sequence가 있고 reply 상관관계를 복원할 수 있을
때만 error reply를 보낸다.

**reply 상관관계의 키는 request sequence 단독이다.** 호출자는 그 sequence로 pending request를
찾아 완료시킨다. **`Response`와 `Error`는 packet name을 담지 않는다** — 응답은 handler를 고르지
않으므로 그 필드가 필요 없다. typed reply는 호출자가 지정한 reply 타입으로 바로 decode한다.
전체 규칙은 [03 message model](03-message-model.ko.md)의 "reply 상관관계"가 소유한다.

### 3.1 로그 수준

**drop 여부와 오류 분류는 별개다.** 같은 drop이라도 원인에 따라 로그 수준이 다르다.

| 원인 | 로그 수준 |
|---|---|
| **handler 예외**(application 코드가 던졌다) | **Error** — one-way라도 낮추지 않는다 |
| handler 없음 · payload decode 실패 · invalid frame | send는 Warning, publish는 Debug 또는 metric |

- **observer event의 공통 스키마는 [framework API §2.4.3](05-framework-api.ko.md)이 소유한다.**
- **observer가 없더라도 기본 로그와 metric은 생략하지 않는다.**
- **observer callback 실패는 runtime error sink로 분리한다.** 원래 reply 또는 drop 결과를 바꾸지
  않는다.

## 4. Startup validation

| 구성 | 결과 |
|------|------|
| **같은 channel 이름을 두 번 등록** | **설정 오류** |
| **server 또는 publisher의 빈 bind endpoint** | **설정 오류** |
| **client/subscriber에 store도 manual endpoint도 없음** | **설정 오류** |
| **server에 request/send handler가 없음** | **설정 오류** |
| **subscriber에 publish handler가 없음** | **설정 오류** |
| **client/server channel에 publish handler 등록** | **설정 오류** |
| **fanout channel에 request/send handler 등록** | **설정 오류** |
| **channel 종류와 맞지 않는 handler group 매핑** | **설정 오류** |
| **매핑한 handler group에 handler가 없음** | **설정 오류** |
| **같은 channel에서 같은 `kind + packet name` handler 중복** | **설정 오류** |
| **framework를 두 번 구성** | **설정 오류** — 등록 루트는 프로세스당 하나다 |
| **handler group 이름이 비어 있음** | **설정 오류** |
| **metadata key가 비어 있음** | **설정 오류** |
| 서로 다른 channel에서 같은 packet name 사용 | **허용** — handler namespace는 channel별로 분리된다 |

**모든 설정 오류는 host 시작 전에 실패한다.**

**location store가 등록되어 있어도 endpoint를 명시한 역할은 manual 연결을 사용한다.** 다른 역할의
자동 연결 설정에는 영향을 주지 않는다([channel-topology §5](10-channel-topology.ko.md)).

## 5. Host 종료 중 호출

**우아한 종료의 전체 수명주기는 [graceful-drain-handoff](54-graceful-drain-handoff.ko.md)가
소유한다.** 이 절은 channel 호출자가 관찰하는 결과만 정리한다.

**host stopping이 시작돼도 기존 연결 위의 신규 request를 즉시 전면 차단하지 않는다.** 차단 대상은
**신규 상태 배정**(spot 생성, actor join, 새 STREAM 연결)이며, **전파 지연 창 동안 도착한 channel
request는 정상 처리한다** — 완전 차단은 분산 시스템에서 불가능하다
([§3.3](54-graceful-drain-handoff.ko.md)).

- **전파 지연 창을 지나 draining node에 직접 도착한 channel/route request는 `RequestRejected`로
  거부한다**([§5](54-graceful-drain-handoff.ko.md)).
- 이미 실행 중인 handler에는 **취소 신호를 전달하고** graceful shutdown 시간 안에 끝날 기회를
  준다. **in-flight reply까지 마무리한 뒤 unbind한다.**
- **one-way `submit`은 완료 객체를 반환하지 않는다.** 따라서 반환 뒤에 발생한 실패(종료 중 전송
  실패 등)를 **호출자에게 예외로 알릴 표면이 없다.** 이 늦은 실패는 **runtime error sink로
  보고한다**(§3의 observer·metric 경로). **호출자는 `submit` 반환을 "전송 성공"으로 간주하면 안
  된다** — local 수락까지만 확인한 것이다.
- **완료값을 반환하는 request 계열만** 호출자에게 실패를 전달한다.

## 6. Codec

- 메시지는 **header + payload**다. wire의 multipart 구성은
  [message-model](03-message-model.ko.md)이 소유한다.
- **header와 payload를 하나의 envelope로 합쳐 단일 message로 보내지 않는다.** route와 dispatch가
  **header만 먼저 읽고 payload decode를 handler 선택 이후로 늦출 수 있게** 하기 위해서다.
- **application handler는 여전히 typed payload와 context를 받는다.** multipart 구조는 adapter
  내부의 transport 계약일 뿐이다.
- **codec 등록은 binding core에 codec 구현을 직접 끼워 넣는 것이 아니다.** framework의 codec
  registry에 codec extension을 등록하는 흐름이다.
- **공유되는 것은 codec extension이지 registry 인스턴스가 아니다.** framework와 STREAM
  connector는 하나의 registry를 함께 쓰지만, **HTTP client는 자기 registry를 따로 가진다.** 같은
  codec extension 객체를 양쪽에 각각 등록할 수 있으나, **등록은 host마다 따로 해야 한다.**

## 7. 회귀 테스트

| 항목 | 검증 |
|---|---|
| channel 이름 호출 | 호출자가 channel 이름만으로 요청하고 runtime이 channel view로 전달한다 |
| runtime 수명 | startup에서 만들고 shutdown에서 정리한다. lazy 생성이 없다 |
| dispatch 실패 | request·send·publish가 §3대로 갈린다 |
| reply 상관관계 | reply를 request sequence 단독으로 매칭한다. `Response`·`Error` header에 packet name이 없다 |
| observer 격리 | observer callback 실패가 reply·drop 결과를 바꾸지 않는다 |
| startup validation | §4의 각 행이 그대로 동작한다 |
| 종료 중 신규 호출 | stopping 이후의 `submit` 호출은 동기 예외로 즉시 거부된다 |
| 종료 중 pending submit | stopping 시점에 큐에 남아 있던 submit은 실패하고 **runtime error sink로만** 보고된다(호출자에게 예외를 전달하지 않는다) |
