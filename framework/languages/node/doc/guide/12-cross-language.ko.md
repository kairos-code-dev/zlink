# Cross-Language — 다른 ZLink 언어와 붙기

Node framework 는 ZLink 의 언어 중립 wire 계약을 따른다. 같은 channel, stream header,
session actor packet 위에서 .NET, C++, Java 구현과 상호 호출될 수 있어야 한다.

## 1. 필수 smoke 경로

| 경로 | 확인할 것 |
|------|-----------|
| Node client -> dotnet channel server | request/reply, send, publish 의미 |
| dotnet client -> Node channel server | Node handler reply 의미 |
| Node stream connector -> dotnet stream server | header session request/reply |
| dotnet/C++/Java connector -> Node stream server | Node `onDispatch` 와 `reply` |

## 2. 지켜야 할 경계

sample 과 application 은 framework public API와 connector public API만 사용한다.
binding internal/native/generated 경로를 직접 import 하지 않는다.

## 3. 완료 판정

cross-language smoke 는 sample smoke 와 별도다. sample 은 사용성을 확인하고,
cross-language smoke 는 wire 계약을 확인한다.

## 회귀 테스트

필수 경로는 `sample-implementation-plan.ko.md` 와 release gate 에서 관리한다.
