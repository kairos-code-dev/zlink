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

## 문서 읽는 법

가이드 본문은 **모든 언어가 같은 정본을 공유한다.** 예제 코드는 언어 탭으로 나뉘며,
탭을 고르면 그 언어의 코드로 바뀐다. 산문은 역할 이름으로 설명하고, 정확한 타입
이름과 언어별 관례는 탭 안의 코드와 주석이 담는다.

설치 방법, 옵션 목록, 인터페이스 카탈로그처럼 언어마다 달라지는 장은 언어별 문서로
따로 둔다. 어느 순서로 읽을지는 각 언어의 진입점이 제시한다.

| 언어 | 진입점 |
| --- | --- |
| `.NET` | [.NET 가이드](dotnet/guide/server/README.ko.md) |
| C++ | [C++ 가이드](cpp/guide/server/README.ko.md) |
| Java | [Java 가이드](java/guide/server/README.ko.md) |
| Kotlin | [Kotlin 가이드](kotlin/guide/server/README.ko.md) |
| Node.js | [Node.js 가이드](node/guide/server/README.ko.md) |

처음이라면 [3. 핵심 개념](common/guide/server/03-concepts.ko.md)부터 본다. channel ·
Spot · Actor · stream · relocation 다섯 개념을 잡으면 나머지 장은 그 조합이다.

## 관련 문서

- 언어 중립 의미와 공개 계약: [공통 스펙](common/README.ko.md)
- Core 메시징 엔진: [core.zlink.systems](https://core.zlink.systems/)
