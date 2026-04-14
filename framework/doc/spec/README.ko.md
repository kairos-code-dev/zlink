# ZLink Framework 스펙

이 문서 묶음은 zlink `core`와 언어 `bindings` 위에 올라가는
`ZLink Framework`의 공개 방향과 초안 계약을 정리한다.

이 스펙 묶음은 기존 `doc/spec/`와 역할이 다르다.

- `doc/spec/`는 zlink `core`와 언어 `bindings`의 공개 계약을 다룬다.
- `framework/doc/spec/`는 그 위에 올라가는 `ZLink Framework`를 다룬다.

즉 이 디렉토리는 `core`와 `bindings`를 확장해서
`ASP.NET Core`, `Spring`, `NestJS` 같은 프레임워크에 접목하는 별도 라이브러리
영역을 위한 문서 자리다.

이 계층이 노리는 중요한 장점 하나는, 일반적인 웹 서버 환경에서 위치투명성과
provider 선택을 위해 흔히 두는 별도 gateway나 전용 로드밸런서 없이도
`service_name` 기준으로 직접 호출할 수 있다는 점이다.

즉 provider 위치는 Discovery가 숨기고, provider 선택은 adapter 쪽의
client-side policy가 맡는다. 응용은 gateway 주소나 load balancer 주소보다
논리 서비스 이름을 기준으로 요청을 보낸다.

## 문서 구조

| 섹션 | 경로 | 설명 |
|------|------|------|
| **Draft** | [draft/README.ko.md](./draft/README.ko.md) | 구현 전 초안과 use case 기반 설계 문서 |

## 현재 상태

현재는 정식 spec보다 draft 문서가 먼저 있다.
즉 요구를 먼저 모으고, use case를 먼저 정리한 뒤, 그 결과를 바탕으로 정식 계약을
나중에 분리하는 방식을 따른다.
