[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md)

# Draft -- ZLink Framework C++ Registry

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 Registry를 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`C++` host는 아래 registry 표면을 가져야 한다.

- embedded registry bootstrap
- in-process topology query
- remote registry query client

일반 request 핫패스는 각 channel discovery view를 기준으로 설명하고, registry query는
운영 점검과 topology snapshot 용도로 분리한다.
