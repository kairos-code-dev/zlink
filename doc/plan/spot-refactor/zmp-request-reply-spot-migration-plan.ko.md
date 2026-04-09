# ZMP Request-Reply / SPOT Routed 전환 작업 계획

> 상태 메모
> 이 문서는 현재 정리된 문서 방향을 실제 작업 순서로 옮긴 실행 계획이다.
> 이번 작업의 핵심은 기존 message-level request 표식을 걷어내고,
> request-reply 와 SPOT 직접 전달을 모두 ZMP 상위 프로토콜로 다시 맞추는 것이다.
>
> 이번 계획은 다음 순서를 전제로 한다.
>
> - 먼저 문서와 core 작업만 진행한다.
> - core 기본 경로 구현과 core 테스트가 끝나면 POSD 기반 리팩토링을 먼저 진행한다.
> - timeout 은 POSD 정리까지 끝난 뒤 붙이는 마지막 core 기능 단계로 둔다.
> - bindings 적용은 core 작업이 끝난 뒤에 진행한다.
> - 구현을 시작하면 사용자 추가 응답을 기다리지 않고 합리적인 가정으로 계속 진행한다.
> - 특별한 blocker 가 없는 한 중간 단계에서 멈추지 않는다.
> - 이 계획 문서의 마지막 완료 조건까지 한 흐름으로 완료한다.

## 1. 목적

이번 작업은 아래 세 가지를 한 흐름으로 정리하는 것이 목적이다.

- 기존 core 의 message-level metadata 기능을 제거한다.
- 기존 core 의 message-level request 기능을 제거한다.
- request-reply 를 ZMP 상위 프로토콜로 다시 구현한다.
- SPOT 직접 전달 위에서도 같은 request-reply 프로토콜을 재사용한다.

이 작업이 끝나면 request-reply 의미는 `zlink_msg_t` 내부 상태가 아니라
ZMP multipart control part 로 표현된다.
또한 SPOT 은 자기 routed envelope 바깥에 request-reply envelope 를 한 겹 더 두는
방식으로 request-reply 를 지원한다.

## 2. 이번 라운드에서 먼저 끝내는 것

- 문서 기준 확정
- core 기존 기능 제거 범위 확정
- core request-reply 공개 API 확정
- core request-reply 프로토콜 encode/decode 구현
- core SPOT routed 와 request-reply 조합 구현

이번 라운드에서 나중으로 미루는 것:

- bindings 실제 이관 작업
- bindings 문서 정리
- timeout 최종 구현

## 2.1 구현 중 정리해도 되는 항목

- capability 광고 필드 이름
- SPOT rid 길이 제한
- topology/status 노출 필드 이름
- SPOT 공개 타입 이름

## 3. source of truth

이번 개발 라운드에서는 `doc/api` 가 아니라
`doc/plan/spot-refactor` 아래 문서를 구현 기준으로 사용한다.
구현과 테스트가 끝난 뒤 정리된 공개 API 문서와 설명 문서를
`doc/api`, `doc/guide`, `doc/internals` 에 반영한다.

- [`/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_PROTOCOL_OVERVIEW.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_PROTOCOL_OVERVIEW.md)
- [`/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md)
- [`/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_SPOT_ROUTED_PROTOCOL.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/ZMP_SPOT_ROUTED_PROTOCOL.md)
- [`/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md)
- [`/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md)

## 4. 핵심 결정 요약

- request-reply 는 message-level field 가 아니라 ZMP 상위 프로토콜이다.
- SPOT 직접 전달도 message-level field 가 아니라 ZMP 상위 프로토콜이다.
- SPOT request-reply 는 `SPOT routed envelope -> request-reply envelope -> payload` 순서로 싣는다.
- `DEALER.request()` 는 `peer_rid` 를 받지 않고 기존 peer 선택 규칙을 따른다.
- `ROUTER.request()` 는 `peer_rid` 를 명시적으로 받는다.
- reply 는 `ctx` 가 아니라 `상대 주소 + request_seq` 기준으로 보낸다.
- request 1건은 high-level callback/future 1회 완료로 본다.
- 같은 `request_seq` 의 추가 reply 는 무시하고 카운터만 올릴 수 있다.
- 여러 in-flight request 는 허용한다.
- timeout 은 POSD 정리 뒤 마지막 core 기능 단계에서 붙인다.

## 5. 작업 순서

### 5.1 1단계: 문서 기준 마무리

목적:

- core 구현 전에 문서 기준을 흔들리지 않게 닫는다.

작업:

- request-reply 문서에서 `request_seq`, `errno`, `peer_rid + request_seq` 계약을 최종 확인한다.
- SPOT 문서에서 request-reply 조합 규칙을 최종 확인한다.
- 기존 historical draft 문서가 현재 기준과 충돌하지 않는지 다시 점검한다.

완료 조건:

- 새 구현이 따라야 할 public surface 와 wire 형식이 문서상 하나로 정리된다.

### 5.2 2단계: 기존 core 기능 제거 범위 정리

목적:

- 예전 message-level metadata / request 기능이 새 구조와 섞이지 않게 정리한다.

제거 대상 1: metadata

- `zlink_msg_set_metadata(...)`
- `zlink_msg_get_metadata(...)`
- `zlink_msg_clear_metadata(...)`
- `zlink_msg_t` 내부 metadata 상태
- send/recv 경로의 message-level metadata 직렬화/역직렬화
- metadata extended header 경로

정리 원칙:

- metadata 는 현재 채택하지 않는 방향으로 본다.
- 필요한 부가 정보는 사용자가 multipart payload 로 직접 표현한다.
- core 는 metadata 를 공통 message 기능으로 더 이상 제공하지 않는다.
- 제거 범위는 C API 선언만이 아니라 내부 구현 코드, 헬퍼, 테스트까지 포함한다.

제거 대상 2: request-reply

- `zlink_msg_set_request(...)`
- `zlink_msg_set_reply(...)`
- `zlink_msg_get_request_info(...)`
- `zlink_msg_t` 내부 `msg_type` / `correlation_id` 계열 상태
- send/recv 경로의 message-level request envelope 직렬화/역직렬화
- request-reply 를 message 속성으로 설명하는 기존 내부 경로
- 기존 message-level request 회귀 테스트
- 기존 message-level request 전용 내부 헬퍼와 보조 코드

같이 점검할 것:

- 기존 bindings 와 샘플이 metadata API 에 어디까지 의존하는지 목록을 만든다.
- 기존 bindings 와 샘플이 request message API 에 어디까지 의존하는지 목록을 만든다.
- core 에서 제거한 뒤에도 ordinary send/recv 는 기존과 같은 의미를 유지하는지 확인한다.
- 제거 대상 테스트가 새 구조 테스트와 중복되지 않는지 확인한다.

완료 조건:

- core 안에 metadata extended header 경로가 더 이상 남지 않는다.
- core 안에 request-reply message marking 경로가 더 이상 남지 않는다.
- 제거 대상 회귀 테스트와 내부 보조 코드도 함께 정리된다.
- 이 단계에서 core 헤더 변경으로 bindings 빌드가 일시적으로 깨지는 것은 허용한다
- 단, bindings 이관 전까지 old API 제거 목록과 영향 범위는 문서로 남겨야 한다

### 5.3 3단계: core request-reply 공개 API 추가

목적:

- 새 request-reply 표면을 먼저 core 에 고정한다.

대상 API:

- `zlink_dealer_request(...)`
- `zlink_router_request(...)`
- `zlink_router_reply(...)`
- `zlink_router_request_handler(...)`
- request timeout 기본값은 각 socket 전용 option 으로 두고 구현은 마지막 단계로 미룬다

핵심 규칙:

- callback 은 `errno` 로 성공/실패를 알린다.
- `ROUTER.reply(...)` 는 `peer_rid + request_seq` 를 받는다.
- 여러 in-flight request 를 허용한다.
- high-level request 완료는 첫 reply 1건으로 끝난다.

완료 조건:

- C 공개 헤더와 내부 호출 경로가 새 API 기준으로 빌드 가능해진다.

### 5.4 4단계: core request-reply 프로토콜 구현

목적:

- ZMP request-reply envelope 를 실제 송수신 경로에 넣는다.

작업:

- send 시 request/reply control part encode
- recv 시 request/reply control part parse
- `request_seq` 생성과 pending 매칭
- out-of-order reply 허용
- extra reply drop 처리

주의:

- ordinary raw recv/send 의미를 깨지 않는다.
- request-reply 는 protocol envelope 로만 처리한다.

완료 조건:

- `ROUTER/DEALER` request-reply 가 message-level field 없이 동작한다.

### 5.5 5단계: core SPOT routed 와 request-reply 조합

목적:

- SPOT 직접 전달 위에 request-reply 를 얹는다.

작업:

- plain SPOT routed send/recv 경로를 먼저 안정화
- `SPOT routed envelope -> request-reply envelope -> payload` 순서 구현
- `spot -> spot request/reply`
- `spot -> router request/reply`
- `router -> spot request/reply`
- SPOT request handler / reply public surface 추가

핵심 규칙:

- SPOT reply 도 `ctx` 없이 `상대 주소 + request_seq` 로 보낸다.
- 같은 `Spot` 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있어야 한다.
- high-level request 완료는 첫 reply 1건으로 끝난다.

완료 조건:

- SPOT request-reply 가 `ROUTER` 기반 request-reply 와 같은 의미로 동작한다.

### 5.6 6단계: core 테스트

목적:

- 새 구조가 ordinary messaging 과 섞여도 깨지지 않는지 검증한다.

최소 테스트 범위:

- `DEALER -> ROUTER request/reply`
- `ROUTER -> ROUTER request/reply`
- multiple in-flight request
- out-of-order reply
- extra reply drop
- `DEALER -> DEALER request` 비지원
- `ROUTER.request(peer_rid)` 잘못된 대상 오류
- `spot -> spot request/reply`
- `spot -> router request/reply`
- `router -> spot request/reply`

완료 조건:

- request-reply 와 SPOT request-reply 핵심 경로가 자동 테스트로 고정된다.

### 5.7 7단계: POSD 기반 리팩토링

목적:

- core 기본 경로 구현과 core 테스트가 끝난 뒤 남아 있는 구조 복잡성을 줄인다.
- request-reply 와 SPOT request-reply 경로를 POSD 원칙에 맞게 다시 다듬는다.

이 단계는 기능 추가 단계가 아니다.
이미 동작하는 구현을 기준으로,
change amplification, 정보 누출, 얕은 모듈, 중복 표면이 남아 있는지 다시 보는 단계다.

집중해서 볼 항목:

- request-reply encode/decode 경로가 ordinary send/recv 와 불필요하게 강하게 결합되어 있지 않은지
- pending map, dispatch, reply 경로 ownership 이 한 곳에 모여 있는지
- `ROUTER` request/reply 와 `SPOT` request/reply 가 같은 개념을 중복 구현하고 있지 않은지
- helper 함수, 내부 struct, callback 경로가 지나치게 얕은 wrapper 로 쪼개져 있지 않은지
- old 구조 제거 후에도 dead branch, compat glue, 임시 우회 코드가 남아 있지 않은지
- hot path 에서 avoid 가능한 복사, 할당, 추가 hop 이 남아 있지 않은지

완료 조건:

- request-reply 와 SPOT request-reply 구현에서
  의미 중복, dead code, 얕은 wrapper, ownership 혼란이 줄어든다.
- 기능 계약과 테스트를 유지한 상태에서 내부 구조가 더 단순해진다.

### 5.8 8단계: timeout 구현

목적:

- request-reply 기본 경로와 POSD 정리까지 끝난 뒤 timeout 을 마지막 core 기능 단계로 붙인다.

이 순서를 쓰는 이유:

- timeout 까지 동시에 넣으면 routing 문제와 timeout 문제를 분리해서 보기 어렵다.
- 먼저 request/reply 매칭과 reply 경로를 안정화한 뒤 timeout 을 추가하는 편이 디버깅이 쉽다.

작업:

- socket default timeout 기본값 `5000ms`
- per-call timeout override
- timeout 시 pending 정리
- late reply drop 정책 확인
- SPOT request-reply timeout 도 같은 방식으로 맞춤

완료 조건:

- request/reply 와 SPOT request/reply 모두 timeout 계약이 동일한 방식으로 동작한다.

### 5.9 9단계: bindings native 라이브러리 임시 최신화

목적:

- bindings 이관 전에 각 언어 바인딩이 참조하는 native `zlink` 라이브러리를
  현재 core 산출물 기준으로 먼저 맞춘다.

이 단계를 먼저 두는 이유:

- bindings 코드 이관 전에 native 라이브러리부터 최신 상태여야
  언어별 로딩 문제와 API 누락 문제를 빨리 확인할 수 있다.
- bindings 이관 중에 core 산출물과 언어별 `native` 디렉터리 내용이 어긋나면
  원인 추적이 어려워진다.

작업:

- 현재 core `zlink` 라이브러리를 빌드한다
- 각 `bindings/<언어>/native` 아래 위치에 라이브러리를 임시 복사한다
- 언어별 bindings 가 새 라이브러리를 실제로 읽는지 확인한다
- 이 단계에서는 bindings API 이관까지 한 번에 하지 않고
  native 라이브러리 최신화와 로딩 확인까지만 진행한다

완료 조건:

- 각 언어별 `bindings/*/native` 아래 라이브러리가 현재 core 산출물과 같은 버전으로 맞춰진다
- bindings 이관 작업 전에 라이브러리 로딩 실패나 심볼 누락이 없는지 확인된다

### 5.10 10단계: bindings 이관

목적:

- 기존 bindings 의 message-level request 구현을 새 core API 로 옮긴다.

작업:

- 기존 bindings request 구현에서 `msg.set_request(...)` 의존 제거
- 새 core request API 호출로 교체
- 기존 언어별 async/callback 표면 유지
- 기존 언어별 enum/exception 오류 매핑 유지

순서:

core 와 가장 가까운 구현부터 먼저 옮기고,
그 다음 현재 bindings request 구현이 비교적 분명한 언어 순서로 내려간다.

1. Rust
2. Go
3. .NET
4. Node
5. Python
6. Java
7. 그 외 남은 바인딩

완료 조건:

- bindings 는 새 core API 위에서 기존 사용자 경험을 유지한다.

### 5.11 11단계: 공개 문서와 설명 문서 정리

목적:

- 구현과 테스트가 끝난 뒤 작업 스펙 기준 내용을
  공개 문서와 설명 문서에 반영한다.

작업:

- `doc/api` 에 새 request-reply, SPOT routed, timeout option 공개 표면 반영
- `doc/guide` 에 새 사용 흐름과 예제 반영
- `doc/internals` 에 encode/decode, pending, SPOT routed 조합 구조 반영
- 이번 작업 라운드에서 기준으로 쓴 `doc/plan/spot-refactor` 문서와
  공개 문서 사이 설명이 어긋나지 않는지 마지막 점검

완료 조건:

- `doc/api`, `doc/guide`, `doc/internals` 가 구현 결과와 같은 계약을 설명한다
- 개발 중 기준 문서와 공개 문서 사이에 핵심 용어와 API 이름 충돌이 남지 않는다

## 6. 기존 기능 제거 체크리스트

- `MSG_METADATA_SPEC.md` 의 historical draft 상태 확인
- `MSG_REQUEST_REPLY_SPEC.md` 의 historical draft 상태 확인
- core 헤더에서 old metadata API 제거
- core 헤더에서 old request message API 제거
- old metadata encode/decode 제거
- old request message encode/decode 제거
- old metadata 내부 헬퍼와 dead code 제거
- old request 내부 헬퍼와 dead code 제거
- bindings 가 old metadata API 를 직접 부르는 위치 전수 검색
- bindings 가 old API 를 직접 부르는 위치 전수 검색
- old metadata 회귀 테스트 제거 또는 새 구조 테스트로 교체
- old message-level request 회귀 테스트 제거 또는 새 구조 테스트로 교체
- 샘플, 테스트, 문서에서 old API 이름 제거 또는 historical 설명으로 이동

## 7. 구현 중 계속 확인할 위험

- ordinary recv/send 와 request-reply recv/send 가 서로 섞여서 의미가 흐려지는 문제
- `DEALER.request()` 가 기존 peer 선택 결과를 호출 시점에 검사하고, 잘못된 peer 면 즉시 `EOPNOTSUPP` 로 실패하는 규칙이 구현과 테스트에 일관되게 반영되는지 확인하는 문제
- SPOT reply 경로에서 source/destination 주소와 transport `routing_id` 를 혼동하는 문제
- extra reply 를 drop 한 뒤 사용자 관측 가능성을 어떻게 남길지 문제
- timeout 을 너무 일찍 붙여서 원인 분리가 어려워지는 문제

## 8. 완료 판정

아래가 모두 만족되면 이 계획의 core 범위는 완료로 본다.

- message-level request 기능이 core 에서 제거된다.
- message-level metadata 기능이 core 에서 제거된다.
- ZMP request-reply 프로토콜이 core 에 구현된다.
- SPOT routed 와 request-reply 조합이 core 에 구현된다.
- request/reply 와 SPOT request/reply 테스트가 통과한다.
- timeout 까지 반영된 뒤에도 계약이 문서와 일치한다.
- 제거 대상 old 코드와 old 회귀 테스트가 저장소에 남아 있지 않다.

bindings 범위 완료 판정은 별도다.
core 완료는 core 테스트, POSD 정리, timeout 까지 끝난 상태를 뜻한다.
그 뒤 bindings 이관과 언어별 테스트가 모두 끝나야 이번 라운드를 완전히 종료한 것으로 본다.
