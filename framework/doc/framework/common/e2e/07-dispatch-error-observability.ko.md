<!-- framework-adapter-nav:start -->
[이전: Handler 등록과 codec](06-handler-registration-codec.ko.md) | [E2E 목차](README.ko.md) | [다음: 샘플 기반 업무 흐름](08-sample-derived-flows.ko.md)
<!-- framework-adapter-nav:end -->

# Dispatch 오류와 관측성 E2E

이 문서는 샘플 정상 흐름에 넣기 어색한 negative path를 검증한다. 목표는
미등록 packet, payload decode 실패, handler 예외가 client-visible 결과와 server-side
로그 양쪽에서 확인되는지 검증하는 것이다.

## DERR-001 channel 미등록 request

우선순위: `P0`

절차:

1. channel server는 `KnownReq` handler만 등록한다.
2. client가 같은 channel로 `UnknownReq`를 보낸다.
3. server dispatch observer는 `HandlerMissing`을 기록한다.

검증:

- client는 error reply를 받는다.
- error에는 packet name과 handler missing 의미가 포함된다.
- server 로그에는 `dispatch-error`, `reason=HandlerMissing`, `action=ReplyError`가 있다.
- server process는 계속 살아 있고 이후 `KnownReq`는 성공한다.

## DERR-002 channel 미등록 send

우선순위: `P0`

절차:

1. client가 `UnknownCommand`를 send로 보낸다.
2. server는 handler를 찾지 못한다.

검증:

- client는 response를 기다리지 않는다.
- server observer는 `HandlerMissing`과 `Drop`을 기록한다.
- 다음 정상 request에는 영향이 없다.

## DERR-003 publish 미등록 topic handler

우선순위: `P1`

절차:

1. subscriber는 topic A만 등록한다.
2. publisher는 topic B를 publish한다.

검증:

- 정책이 ignore이면 dispatch error 없이 drop된다.
- 정책이 report이면 observer에 `HandlerMissing` 또는 대응 reason이 남는다.
- 어떤 정책이든 publisher는 reply를 기다리지 않는다.

## DERR-004 Spot route 미등록 request

우선순위: `P0`

절차:

1. Spot은 `KnownSpotReq` handler만 등록한다.
2. client가 같은 Spot rid로 `UnknownSpotReq`를 보낸다.

검증:

- client는 error reply를 받는다.
- observer surface는 `SpotRoute` 또는 언어별 대응 값이다.
- server 로그에는 spot rid와 packet name이 남는다.

## DERR-005 Spot actor 미등록 request

우선순위: `P1`

절차:

1. actor가 Spot에 join한다.
2. client 또는 session이 actor target으로 미등록 actor request를 보낸다.

검증:

- request는 error reply로 끝난다.
- observer surface는 `SpotActor` 또는 대응 값이다.
- actor binding은 깨지지 않고 이후 정상 actor request가 성공한다.

## DERR-006 payload decode 실패

우선순위: `P0`

절차:

1. server는 JSON handler를 등록한다.
2. client는 같은 packet name으로 잘못된 payload bytes를 보낸다.

검증:

- request면 error reply를 받는다.
- send/publish면 observer와 로그로만 확인된다.
- observer reason은 `PayloadDecodeFailed`다.
- handler는 실행되지 않는다.

## DERR-007 handler 예외

우선순위: `P0`

절차:

1. handler가 의도적으로 exception을 던진다.
2. client가 request를 보낸다.

검증:

- client는 error reply를 받는다.
- server 로그는 error level로 exception type과 packet name을 남긴다.
- observer reason은 `HandlerException`이다.
- exception stack이 process 밖으로 새어 process를 죽이지 않는다.

## DERR-008 observer 실패

우선순위: `P1`

절차:

1. dispatch error observer 자체가 exception을 던지도록 구성한다.
2. 미등록 request를 보낸다.

검증:

- 원래 client error reply는 유지된다.
- observer 실패는 runtime unhandled callback sink 또는 별도 로그로 보고된다.
- observer 실패가 server process를 죽이지 않는다.

## DERR-009 파일 로그 검증

우선순위: `P0`

절차:

1. server 로그를 파일로 저장한다.
2. 미등록 request, decode 실패, handler 예외를 각각 발생시킨다.
3. 테스트가 로그 파일을 읽어 marker를 검증한다.

검증:

- 각 오류마다 `dispatch-error` marker가 있다.
- `surface`, `messageKind`, `reason`, `action`, `packetName`, `correlationId`가 포함된다.
- channel 경로에서는 `channelName`, Spot 경로에서는 `spotRid`, actor 경로에서는
  `actorId`처럼 surface에 맞는 식별자가 함께 남는다.
- request 오류는 `ReplyError`, one-way 오류는 `Drop` 또는 문서화한 action으로 남는다.

## DERR-010 error reply wire compatibility

우선순위: `P1`

절차:

1. 언어 A client가 언어 B server에 미등록 request를 보낸다.
2. 언어 B server가 error reply를 만든다.
3. 언어 A client가 public error type으로 해석한다.

검증:

- error code와 message가 언어 간 해석 가능하다.
- correlation id가 유지된다.
- payload codec이 달라도 error envelope는 공통 규칙을 따른다.
