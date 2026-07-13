# Channel 메시징 — 공통 스펙

[스펙 목차](README.ko.md)

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

| 경로 | handler 없음 · decode 실패 · handler 예외 · invalid frame |
|---|---|
| **request** | **error reply를 반환한다.** Error 로그 + metric + 전역 message flow observer event |
| **send** | **drop.** Warning 로그 + metric + observer event |
| **publish** | **drop.** Debug 로그 또는 metric + observer event |

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
| 서로 다른 channel에서 같은 packet name 사용 | **허용** — handler namespace는 channel별로 분리된다 |

**모든 설정 오류는 host 시작 전에 실패한다.**

**location store가 등록되어 있어도 endpoint를 명시한 역할은 manual 연결을 사용한다.** 다른 역할의
자동 연결 설정에는 영향을 주지 않는다([channel-topology §5](10-channel-topology.ko.md)).

## 5. Host 종료 중 호출

**host stopping이 시작되면 새 inbound dispatch를 받지 않는다.**

- 이미 실행 중인 handler에는 **취소 신호를 전달하고** graceful shutdown 시간 안에 끝날 기회를
  준다.
- **이 시점에 새로 시작하는 outbound request나 submit의 성공은 보장하지 않는다.**
- **runtime이 정리될 때 아직 전송되지 않은 pending submit은 예외로 완료된다.** 호출자는 **정상
  완료로 간주하면 안 된다.**

우아한 종료의 전체 수명주기는 [graceful-drain-handoff](54-graceful-drain-handoff.ko.md)가 소유한다.

## 6. Codec

- 메시지는 **header + payload**다. wire의 multipart 구성은
  [message-model](03-message-model.ko.md)이 소유한다.
- **header와 payload를 하나의 envelope로 합쳐 단일 message로 보내지 않는다.** route와 dispatch가
  **header만 먼저 읽고 payload decode를 handler 선택 이후로 늦출 수 있게** 하기 위해서다.
- **application handler는 여전히 typed payload와 context를 받는다.** multipart 구조는 adapter
  내부의 transport 계약일 뿐이다.
- **codec 등록은 binding core에 codec 구현을 직접 끼워 넣는 것이 아니다.** framework의 codec
  registry에 codec extension을 등록하는 흐름이다. 같은 registry를 framework, HTTP client, stream
  connector가 공유한다.

## 7. 회귀 테스트

| 항목 | 검증 |
|---|---|
| channel 이름 호출 | 호출자가 channel 이름만으로 요청하고 runtime이 channel view로 전달한다 |
| runtime 수명 | startup에서 만들고 shutdown에서 정리한다. lazy 생성이 없다 |
| dispatch 실패 | request·send·publish가 §3대로 갈린다 |
| observer 격리 | observer callback 실패가 reply·drop 결과를 바꾸지 않는다 |
| startup validation | §4의 각 행이 그대로 동작한다 |
| 종료 중 호출 | stopping 이후 pending submit이 예외로 완료된다 |
