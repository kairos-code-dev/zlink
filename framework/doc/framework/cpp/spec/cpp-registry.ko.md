# C++ Registry

Core C API의 Discovery/Registry 표면은 제거되었다. 이 문서는 예전 C++ Registry-backed
lookup 계약을 더 이상 정식 공개 계약으로 정의하지 않는다.

C++ framework 에서 자동 위치 해석을 제공하려면 제거된 Core C Discovery/Registry API 를
되살리거나 compatibility wrapper 를 만들지 않는다. location runtime 과 location store 설계가
정식 계약으로 확정된 뒤 그 표면에 맞춰 별도 spec 을 작성한다.
