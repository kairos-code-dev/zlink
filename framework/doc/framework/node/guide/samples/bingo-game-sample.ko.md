# Matching Room Game Sample: Bingo (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — Bingo](../../../common/sample/bingo/README.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다.
> 실행 코드(TypeScript)는 `samples/Bingo.Ts`에 있다.

## 1. 목적

분리된 `Session`·`Api`·`Play`·`Registry` 서버로 구성한 실시간 매칭 게임이다. session
gateway, actor binding, Entry Spot, room Spot, server timer 기반 진행, bound session
push를 한 흐름으로 보여 준다.

## 2. 서버 구성

- `Session`: STREAM 노드로 client 접속을 받고 ActorGateway로 player actor에 bind 한다.
- `Api`: 인증·매칭 요청을 받는 client-server channel handler를 NestJS provider로 노출한다.
- `Play`: room Spot·Entry Spot·player actor·draw timer·domain event를 가진 상태 소유 서버.
- `Registry`: embedded registry로 네 서버가 서로를 Discovery로 발견한다.

## 3. 전체 흐름

1. client는 `Session` STREAM endpoint 하나만 연다.
2. 각 player가 `AuthenticatePlayerReq`로 인증하면 player actor가 생성·bind 된다.
3. `MatchBingoReq`로 매칭을 요청한다. 두 번째 참가 시 room이 running 상태가 되고
   양쪽 bound session에 game-start가 push 된다.
4. 각 player가 3 x 3 카드를 제출한다.
5. room Spot의 draw timer가 숫자를 뽑아 push 하고, 승패가 정해지면 game-ended를 push 한다.

## 4. 호출 표면 (객체-메시징)

high-level 호출은 업무 객체를 직접 주고받는다. codec·packet name은 framework 내부가 처리한다.

```ts
const matched = await client
  .requestToChannel("play", new AllocateBingoRoomReq(actorId, mode))
  .submit<AllocateBingoRoomRes>();
```

handler는 `@zlinkRequestHandler` 계열 decorator로 등록하고 typed reply 객체를 반환한다.
`Message.from(...)`, `.toJson()` 같은 직렬화 helper를 호출부에서 쓰지 않는다.

## 5. 완료 기준

- client는 `Session` STREAM 연결 하나만 연다.
- 네 서버가 Registry/Discovery로 자동 발견된다.
- draw는 server timer가 주도하고 client는 draw 요청을 보내지 않는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
