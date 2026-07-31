# zlink 가이드

zlink 문서에 오신 것을 환영합니다. zlink은 libzmq 기반의 현대적 메시징
라이브러리로, Boost.Asio I/O, TLS 트랜스포트, 간결한 소켓 API를 제공합니다.

## 시작하기

- [개요](guide/01-overview.md) — 아키텍처와 libzmq 대비 주요 변경점
- [핵심 API](guide/02-core-api.md) — Context, Socket, Message 기본

## 소켓 패턴

- [PAIR](guide/03-1-pair.md) — 1:1 양방향
- [PUB/SUB](guide/03-2-pubsub.md) — 토픽 기반 발행/구독
- [DEALER](guide/03-3-dealer.md) — 비동기 요청
- [ROUTER](guide/03-4-router.md) — 라우팅
- [STREAM](guide/03-5-stream.md) — Raw TCP 통신

## 바인딩

- [언어별 바인딩 가이드](bindings/guide/README.ko.md) — .NET · C++ · Java · Node.js · Python · Go · Rust

## 코드 예제

모든 코드 예제는 언어별 탭으로 표시됩니다. 한 번 언어를 선택하면 페이지의
모든 예제가 해당 언어로 전환됩니다.
