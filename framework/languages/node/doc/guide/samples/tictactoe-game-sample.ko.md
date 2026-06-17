# Smallest Realtime Game Sample: TicTacToe (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — TicTacToe](../../../../../doc/spec/sample/tictactoe/README.ko.md)다.
> 실행 코드(TypeScript)는 `samples/TicTacToe.Ts`에 있다.

## 1. 목적

별도 `Session` 서버 없이 `Api`와 `Play` 두 서버만으로 구성한 가장 작은 실시간 게임이다.
`Play`가 stream session을 함께 소유하고, 서버는 수동 endpoint로 직접 연결한다(Registry
자동 발견을 쓰지 않음). Bingo와 달리 분리 gateway·자동 discovery 흐름을 반복하지 않는다.

## 2. 서버 구성

- `Api`: 인증과 game 생성 요청을 받는 client-server channel 서버.
- `Play`: stream 노드 + ActorGateway + room Spot + Entry Spot을 한 서버에 둔다.
  session은 framework typed session dispatch로 인증을 처리하고, 이후 packet은 actor
  gateway가 bound actor로 relay 한다.
- 수동 endpoint 연결을 쓴다.

## 3. 전체 흐름

1. client가 `Play` stream에 접속해 player actor에 bind 한다.
2. `Api` channel로 game 생성/매칭을 요청한다.
3. 두 player가 같은 room Spot에 join 하면 game이 시작된다.
4. mark 제출 → board·turn 갱신 → move/ended notify push.
5. client는 board, turn, winner 같은 의미 값을 검증한다.

## 4. 호출 표면 (객체-메시징)

```ts
const created = await client
  .requestToChannel("api", new CreateGameReq(roomId))
  .submit<CreateGameRes>();
```

high-level 호출은 업무 객체를 직접 주고받고, codec(JSON)·packet name은 framework 내부가
처리한다. 호출부에서 `Message.from(...)`/`.toJson()`을 쓰지 않는다.

## 5. 완료 기준

- `Api`·`Play` 두 서버만으로 동작하고 별도 Session 서버가 없다.
- 수동 endpoint로 직접 연결한다(Registry/Discovery 미사용).
- session이 framework typed session dispatch를 쓴다(수동 payload 역직렬화 없음).
- high-level 호출이 업무 객체 기반이다.
