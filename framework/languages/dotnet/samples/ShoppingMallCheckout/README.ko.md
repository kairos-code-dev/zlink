# ShoppingMallCheckout 샘플

`ShoppingMallCheckout`은 쇼핑몰 주문 checkout 흐름을 담기 위한 .NET Framework
샘플 디렉터리다. 현재 이 디렉터리는 checkout 샘플의 자리만 유지하고 있으며,
실행 스크립트와 소스 프로젝트는 아직 들어 있지 않다.

## 현재 상태

이 README는 샘플 루트에 README가 빠져 있던 상태를 보완한다. 실제 실행 방법은
소스 프로젝트와 runner가 추가된 뒤 함께 갱신해야 한다.

## 구성

- checkout 요청, 응답, 주문 상태 계약을 담을 `Shared/` 영역이 필요하다.
- checkout 요청을 받을 `Server/CommerceApi/` 역할이 필요하다.
- 주문 상태 전이와 projection rebuild를 맡을 `Server/OrderWorkflow/` 역할이 필요하다.
- 샘플 실행 중 사용할 discovery registry 역할이 필요하다.
- 역할별 endpoint, channel, packet 설정은 configuration 영역에서 관리해야 한다.

## 성공 조건

실행 가능한 샘플이 추가되면 checkout 성공 경로와 주문 workflow evidence를
검증해야 한다. 아직 이 디렉터리에는 완료 마커를 출력하는 runner가 없다.
