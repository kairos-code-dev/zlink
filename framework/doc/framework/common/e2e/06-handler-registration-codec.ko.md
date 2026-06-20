<!-- framework-adapter-nav:start -->
[이전: Spot, actor, session](05-spot-actor-session.ko.md) | [E2E 목차](README.ko.md) | [다음: Dispatch 오류와 관측성](07-dispatch-error-observability.ko.md)
<!-- framework-adapter-nav:end -->

# Handler 등록과 Codec E2E

handler 등록 방식과 codec은 언어별 표면 차이가 크다. 이 문서는 자동 등록,
annotation/attribute/decorator 등록, 수동 등록, codec 조합을 실제 dispatch로 검증한다.

## REG-001 assembly 또는 module 자동 등록

우선순위: `P0`

절차:

1. handler가 있는 assembly 또는 module을 등록한다.
2. channel request, send, publish handler를 모두 포함한다.
3. client가 각 packet을 보낸다.

검증:

- handler type을 하나씩 나열하지 않아도 dispatch된다.
- packet name이 handler 계약에서 올바르게 계산된다.
- handler 추가 후 registration scan이 누락하지 않는다.

## REG-002 annotation, attribute, decorator 등록

우선순위: `P0`

구성:

- Java/Kotlin annotation
- .NET attribute가 있다면 attribute
- Node decorator

절차:

1. annotation 기반 handler를 등록한다.
2. 같은 packet을 처리하는 잘못된 중복 handler를 의도적으로 추가한 변형을 둔다.

검증:

- 정상 handler는 dispatch된다.
- 중복 handler는 startup validation error로 막힌다.
- annotation의 channel name 또는 packet name override가 동작한다.

## REG-003 수동 handler 등록

우선순위: `P0`

절차:

1. 자동 등록을 끈다.
2. builder에서 handler를 명시 등록한다.
3. 등록한 packet과 등록하지 않은 packet을 각각 보낸다.

검증:

- 등록한 packet만 처리된다.
- 미등록 request는 error reply로 돌아온다.
- 미등록 send/publish는 dispatch error observer 또는 로그로 관측된다.

## REG-004 DI lifecycle

우선순위: `P1`

절차:

1. singleton service를 사용하는 handler를 둔다.
2. scoped service를 사용하는 handler를 둔다.
3. handler 호출마다 scope id를 evidence에 저장한다.

검증:

- singleton은 같은 instance를 재사용한다.
- scoped handler 의존성은 request scope에 맞게 만들어진다.
- handler가 끝난 뒤 disposable scope가 정리된다.

## REG-005 filter 또는 interceptor ordering

우선순위: `P1`

절차:

1. global filter 2개와 handler-level filter 1개를 등록한다.
2. request를 보내고 실행 순서를 evidence에 남긴다.
3. 중간 filter에서 error를 반환하는 변형을 실행한다.

검증:

- 실행 순서가 문서와 맞다.
- filter에서 중단한 경우 handler가 실행되지 않는다.
- error mapping과 observer reporting이 일관된다.

## CDC-001 JSON codec

우선순위: `P0`

절차:

1. primitive, nested object, array, nullable field를 가진 payload를 보낸다.
2. server는 받은 payload를 그대로 response에 넣는다.

검증:

- 필드 이름과 null 의미가 언어별 구현에서 같아야 한다.
- 알 수 없는 필드 정책이 문서와 맞다.
- decode 실패는 `PayloadDecodeFailed`로 보고된다.

## CDC-002 Protobuf codec

우선순위: `P1`

절차:

1. Protobuf schema 기반 payload를 보낸다.
2. version-compatible field 추가 변형을 테스트한다.
3. 잘못된 bytes를 보내 decode 실패를 만든다.

검증:

- schema field가 손상되지 않는다.
- unknown field 정책이 codec 기대와 맞다.
- decode 실패 request는 error reply로 끝난다.

## CDC-003 MessagePack codec

우선순위: `P1`

절차와 검증은 JSON, Protobuf와 같다. 단, binary payload의 로그 출력은 payload 전문이
아니라 size와 packet name만 남긴다.

## CDC-004 channel별 codec 혼합

우선순위: `P1`

구성:

- API channel은 JSON
- game channel은 MessagePack
- shared event channel은 Protobuf

검증:

- channel별 codec 선택이 서로 섞이지 않는다.
- wrong codec request는 decode 실패로 끝난다.
- 같은 message 이름을 다른 codec channel에서 써도 runtime이 channel context를 유지한다.
