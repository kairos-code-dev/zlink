[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md)

# Draft -- ZLink Framework NestJS Registry

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `NestJS`에서 Registry를 어떤 표면으로 통합할지 정리한다.

## 1. 방향

- embedded registry startup
- in-process topology query
- remote registry query client

일반 request 핫패스는 각 channel discovery view를 기준으로 설명하고, registry query는
운영 점검과 topology snapshot 용도로 분리한다.
