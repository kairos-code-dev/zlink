# ZLink Framework

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만드는 프레임워크다.
서버 간 typed 메시징, 상태 단위(SPOT)의 직렬 실행, 외부 client 실시간 연결, 무중단
이전을 한 선언 모델 위에서 조합한다.

## 무엇을 만드는가

여러 서버 프로세스가 역할을 나눠 협력하고, 상태 변화를 실시간으로 client에 전달해야
하는 시스템에 맞다.

| 도메인 | 핵심 시나리오 |
| --- | --- |
| 실시간 게임 | 방 생성 → 입장 → 상태 갱신 → client push |
| 고객 지원 채팅 | 대화 개설 → 상담원 배정 → 메시지 중계 → 상태 push |
| 주문 워크플로 | 주문 접수 → 단계별 처리 → 상태 변경 → 알림 |
| 배송·배차 | 배차 요청 → 배정·수락 → 상태 추적 → 실시간 push |

## 언어를 먼저 고른다

가이드는 **언어마다 한 벌씩 완결되어 있다.** 고른 언어의 가이드 안에는 그 언어의 코드만
있고, 처음부터 끝까지 그 안에서 읽힌다. 장 머리의 전환 줄로 같은 장을 다른 언어에서 볼
수 있다.

| 언어 | 서버 가이드 | 바로 시작하기 | client 쪽 가이드 |
| --- | --- | --- | --- |
| `.NET` | [서버](dotnet/guide/server/README.ko.md) | [설치와 첫 동작](dotnet/guide/server/02-getting-started.ko.md) | [Stream Connector](dotnet/guide/stream-connector/README.ko.md) · [HTTP Client](dotnet/guide/http-client/README.ko.md) |
| C++ | [서버](cpp/guide/server/README.ko.md) | [설치와 첫 동작](cpp/guide/server/02-getting-started.ko.md) | [Stream Connector](cpp/guide/stream-connector/README.ko.md) · [HTTP Client](cpp/guide/http-client/README.ko.md) |
| Java | [서버](java/guide/server/README.ko.md) | [설치와 첫 동작](java/guide/server/02-getting-started.ko.md) | [Stream Connector](java/guide/stream-connector/README.ko.md) · [HTTP Client](java/guide/http-client/README.ko.md) |
| Kotlin | [서버](kotlin/guide/server/README.ko.md) | [설치와 첫 동작](kotlin/guide/server/02-getting-started.ko.md) | [Stream Connector](kotlin/guide/stream-connector/README.ko.md) · [HTTP Client](kotlin/guide/http-client/README.ko.md) |
| Node.js | [서버](node/guide/server/README.ko.md) | [설치와 첫 동작](node/guide/server/02-getting-started.ko.md) | [Stream Connector](node/guide/stream-connector/README.ko.md) · [HTTP Client](node/guide/http-client/README.ko.md) |

**client 쪽 가이드 둘**은 서버 framework와 따로 배포되는 라이브러리를 다룬다.
Stream Connector는 client가 STREAM endpoint에 붙는 쪽이고(Unity · Godot · 브라우저
포함), HTTP Client는 서버가 바깥 HTTP API를 호출할 때 쓴다.

**개념부터 잡고 싶다면** 각 언어의 `3. 핵심 개념`부터 본다 — channel · Spot · Actor ·
stream · relocation 다섯이고, 나머지 장은 그 조합이다.

어느 순서로 읽을지는 각 언어의 가이드 홈이 제시한다. 1~17장은 다섯 언어가 공유하고,
C++에만 있는 DI · configuration · HTTP hosting · 실행 모델이 18~21장으로 뒤에 붙는다.

### 문서가 만들어지는 방식

개념과 동작 설명은 언어가 달라도 같으므로 **한 벌만 쓰고 언어별 가이드로 생성한다**
(`common/guide/server/`가 소스다). 설치·옵션·인터페이스 색인처럼 내용 자체가 갈리는
장만 언어별로 직접 쓴다. 그래서 어느 언어를 보든 설명이 어긋나지 않는다.

## 관련 문서

| | |
| --- | --- |
| 언어 중립 의미와 공개 계약 | [공통 스펙](common/README.ko.md) |
| 그 아래 메시징 엔진 — 소켓 패턴, 전송, 옵션 | [Core 가이드](https://zlink.systems/core/ko/guide/01-overview/) · [Core 스펙](https://zlink.systems/core/ko/spec/core/) |
| Core를 언어에서 직접 쓸 때 — C API binding | [Bindings 가이드](https://zlink.systems/core/ko/bindings/guide/) · [Bindings 스펙](https://zlink.systems/core/ko/bindings/spec/) |
| 소스와 이슈 | [github.com/kairos-code-dev/zlink](https://github.com/kairos-code-dev/zlink) |

Core는 이 프레임워크가 올라타는 메시징 엔진이다. 프레임워크만 쓸 때는 볼 일이 없고,
소켓 수준의 동작이나 전송 옵션을 직접 만져야 할 때 그쪽으로 내려간다. 가이드는 패턴과
사용법을, 스펙은 C API의 함수·옵션·오류 코드를 다룬다.

binding은 그 C API를 언어에서 쓰는 얇은 층이다(.NET · C++ · Java · Node.js ·
Python · Go · Rust). framework가 없는 언어에서 zlink를 쓰거나, framework가 감싸지
않은 소켓 기능이 필요할 때 여기서 시작한다.
