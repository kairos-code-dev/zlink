[English](00-public-contract-governance.md) | 한국어

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# Core 공개 계약 관리

이 문서는 ZLink Core 10.0.0 공개 계약의 원본, 문서 책임과 변경 절차를 정의한다. 대상 독자는 Core 공개
C ABI를 설계·구현·검토하는 개발자다. 이 문서는 “정식 spec, 공개 header, test와 package를 어떤 기준으로
일치시키는가?”에 답한다.

## 1. 계약 원본

`core/doc/spec/`의 정식 spec은 Core 10.0.0 공개 동작을 정의한다. `core/include/zlink.h`와 이 header가
포함하는 domain header는 같은 계약의 C ABI 표현이다. 구현, contract test, bindings와 설치 package는
두 표현과 일치해야 한다.

정식 spec은 구현 편의를 이유로 축소하지 않는다. header와 spec이 다르면 구현 완료로 판정하지 않으며
둘 가운데 하나를 별도 호환 계층으로 유지하지 않는다.

## 2. 문서 책임

정식 spec은 function signature, type과 상수, result와 errno, ownership, timeout, thread safety, callback과
close 의미를 정의한다. 사용 목적과 application 예제는 guide가 소유한다. socket 배선, queue, lock,
thread와 protocol codec은 구현이 확정된 뒤 internals가 설명한다.

정식 spec·guide·internals는 모두 Core 10.0.0의 현재 계약과 구조만 설명한다. 정식 문서의 탐색 링크는
정식 목차와 공개 계약 owner만 가리킨다.

## 3. 변경 절차

공개 계약 변경은 다음 순서로 진행한다.

1. 한국어와 영문 정식 spec에 10.0.0 계약을 정확히 기록한다.
2. 함수, type, enum, 상수, result, ownership과 thread-safety 표를 검토한다.
3. 공개 header, exported symbol과 implementation을 정식 spec에 맞춘다.
4. contract test와 bindings package snapshot으로 공개 표면을 검증한다.
5. 실제 구현과 구조 test가 확정된 뒤 internals를 갱신한다.
6. 한국어·영문·header·package의 동일성을 다시 검증한다.

## 4. 한국어와 영문 동일성

한국어와 영문 문서는 heading 순서, C signature, enum·struct field와 숫자 값, 기본값, result, ownership,
thread safety와 link 대상이 같아야 한다. 한 문서에만 존재하는 public 계약은 유효한 Core 10.0.0 계약으로
인정하지 않는다.
