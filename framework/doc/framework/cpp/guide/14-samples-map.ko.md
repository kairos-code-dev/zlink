[← 목차](README.ko.md)

# 14. 샘플 지도

정본 6종의 서버 역할, 메시지 계약, 상태 전이, client self-check와 완료 기준은
[공통 샘플 문서](../../common/sample/README.ko.md)가 소유한다. C++ 가이드는 이 내용을
다시 정의하지 않는다.

공통 시나리오는 [공통 샘플 문서](../../common/sample/README.ko.md)에서 확인한다. C++ API의
등록 방법은 각각 [channel](07-channel-messaging.ko.md), [Spot](08-spot.ko.md),
[stream](10-stream.ko.md) 가이드에서 확인한다.

샘플 구현은 C++ 공개 framework와 bindings API만 사용한다. 공통 시나리오를 현재 공개 API로
구현할 수 없으면 샘플 전용 helper나 낮은 수준의 우회 경로를 추가하지 않고 feature map 또는
implementation gap에 기록한다.

[← 목차](README.ko.md)
