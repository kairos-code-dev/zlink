# TicTacToe Sample Spec

> 이 문서는 틱택토 샘플의 문서 묶음이다.
> 일반 버전은 client가 Play 서버의 stream endpoint에 직접 연결하는 구조를 보여 주고,
> Session actor dispatch 버전은 client 연결을 가진 Session 서버와 actor를 가진 Play
> 서버를 분리하는 구조를 보여 준다.

## 문서 구성

| 문서 | 목적 |
|------|------|
| [direct.ko.md](./direct.ko.md) | 일반 TicTacToe 샘플. client가 Play 서버에 직접 stream 연결을 맺고, Play 서버가 actor와 game room을 처리한다. |
| [session-gateway.ko.md](./session-gateway.ko.md) | Session actor dispatch TicTacToe 샘플. client는 Session 서버에만 연결하고, Session 서버가 actor create/dispatch helper로 Play 서버에 packet을 relay한다. |

## 공통 게임 규칙

두 샘플은 같은 틱택토 규칙을 사용한다.

- 한 match에는 두 player가 참가한다.
- 첫 player는 `X`, 두 번째 player는 `O`를 받는다.
- board는 0부터 8까지의 cell index로 표현한다.
- 같은 cell에는 두 번 둘 수 없다.
- 현재 turn의 player만 `PlaceMarkReq`를 보낼 수 있다.
- 가로, 세로, 대각선 중 한 줄을 먼저 완성한 player가 이긴다.
- 모든 cell이 찼고 승자가 없으면 draw다.

## 공통 메시지 이름 규칙

샘플 메시지는 아래 접미어를 사용한다.

| 접미어 | 의미 |
|--------|------|
| `Req` | request로 보내는 메시지 |
| `Res` | request에 대한 response 메시지 |
| `Notify` | 서버가 client에게 push하는 알림 메시지 |

## 두 버전의 차이

| 항목 | 일반 버전 | Session actor dispatch 버전 |
|------|-----------|----------------------|
| client 연결 대상 | Play 서버 stream endpoint | Session 서버 stream endpoint |
| 인증 처리 위치 | Play session이 API 서버에 인증 요청 | Session이 API 서버에 인증 요청 |
| actor 위치 | Play 서버 내부 | Play 서버 내부 |
| client stream 소유 | Play 서버 | Session 서버 |
| play -> client push | Play actor가 stream으로 직접 전송 | Play 서버가 `SessionProxy`로 Session 서버에 전송 |
| 재접속 시 장점 | 같은 Play 서버 endpoint로 다시 연결해야 함 | 다른 Session 서버에 연결해도 `actorId` 기준으로 다시 묶을 수 있음 |

일반 버전은 구조가 단순해서 framework의 stream, actor, spot 흐름을 먼저 이해하기
좋다. Session actor dispatch 버전은 서버간 통신과 session relay가 필요한 운영 구조를
보여 주기 위한 샘플이다.
