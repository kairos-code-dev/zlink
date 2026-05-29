# .NET API Reference 주석

이 문서는 .NET 바인딩 공개 API reference에 쓰이는 XML 문서 주석의
작성 기준을 정의한다.

`bindings/dotnet/src/Zlink/Contracts/`의 XML 주석은 계약 문구다. 호출자가
API를 올바르게 쓰기 위해 알아야 하는 공개 동작을 설명해야 하며,
`core/include/zlink.h`와 바인딩 계약 문서와 맞아야 한다.

## 범위

공개 멤버가 아래 계약 결정을 드러내면 XML 주석을 작성한다.

- `Message`와 `Received` payload의 소유권, 해제, 소유권 이동.
- 빌린 view와 복사된 buffer의 차이.
- blocking, timeout, cancellation, callback, non-blocking 결과 동작.
- boolean 반환값과 result enum의 의미.
- 다음에 호출할 수 있는 메서드가 계약 일부인 staged operation builder 흐름.
- 호출자가 계약을 지키면 피할 수 있는 예외.

공개 XML 주석에는 private 구현 방식을 적지 않는다. native handle, polling
전략, wire encoding, progress pump 세부 사항은 runtime 주석이나
`doc/internals/`에 둔다.

## 주석 형태

- XML 주석 본문은 영어로 작성한다.
- summary는 짧고 호출자 관점으로 작성한다.
- 한 줄 summary에 담기 어려운 계약 세부 사항만 `<remarks>`에 적는다.
- owns, borrows, copies, transfers, disposes처럼 소유권을 명확히 드러내는
  표현을 우선 사용한다.
- overload 차이를 설명해야 하는 경우가 아니면 메서드 이름을 prose로 반복하지
  않는다.
- 현재 구현 최적화나 우회 경로를 공개 보장처럼 설명하지 않는다.

## Guide와의 분리

API reference 주석은 튜토리얼이 아니다. 각 type 또는 member의 정확한 계약만
설명한다. 사용 패턴, 예제, 목적 설명은 `doc/guide/`에 둔다.

공개 멤버에 긴 배경 설명이 필요하면 XML 주석은 짧게 유지하고, guide 또는
바인딩 README에서 해당 guide 문서로 연결한다.

## 리뷰 체크리스트

공개 계약이 바뀌면 코드와 함께 XML 주석을 검토한다.

- 호출자가 반환된 모든 `Message`의 소유자를 알 수 있는가?
- buffer view가 빌린 것인지 복사된 것인지 분명한가?
- false, timeout, cancellation, callback 경로가 설명되어 있는가?
- runtime 내부 세부 사항을 피하고 있는가?
- 생성될 API reference 대상과 문구가 맞는가?
