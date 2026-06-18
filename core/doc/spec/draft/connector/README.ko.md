# Connector 초안 문서

이 디렉토리는 릴리스 전 Connector 계층 초안을 모아 두는 곳이다.
여기 있는 문서는 현재 공개 계약이 아니며, 정식 spec 문서와 공개 API에
반영되기 전까지 응용은 이 동작에 의존하면 안 된다.

Connector 계층은 core socket API를 그대로 노출하는 바인딩이 아니라,
특정 사용 흐름에 맞춰 연결, 인코딩, 요청-응답, callback dispatch를 묶는
상위 API를 뜻한다. 따라서 core socket spec과 분리해서 설계한다.

## 문서 목록

| 문서 | 내용 |
|------|------|
| [dotnet-stream-connector-lifecycle.ko.md](dotnet-stream-connector-lifecycle.ko.md) | .NET Stream Connector의 생성, 연결, heartbeat, reconnect, 종료 계약 초안. reconnect 알고리즘은 언어별 Connector 공통 기준으로 사용한다 |
| [stream-connector-inbound-observer-plan.ko.md](stream-connector-inbound-observer-plan.ko.md) | C++, Java, .NET, Node Stream Connector에 수신 frame 관찰 기능을 추가하기 위한 구현, 문서, 테스트 계획 |

## 작성 기준

- 이 디렉토리의 문서는 `doc/spec/draft/` 아래에 있으므로 릴리스 전 초안이다.
- 정식 spec으로 승격되기 전에는 정식 spec 문서에 내용을 섞지 않는다.
- 릴리스 전에 공개 API, 테스트, 바인딩 문서와 맞춘 뒤 정식 문서로 나눈다.
- Connector API는 내부 소켓 구현보다 호출자가 지켜야 하는 공개 계약을 먼저 설명한다.
