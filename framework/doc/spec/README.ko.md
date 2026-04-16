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
service 단위 직접 호출을 위해 흔히 두는 별도 gateway나 전용 로드밸런서 없이도
`service_name` 기준으로 직접 호출할 수 있다는 점이다.

즉 응용은 gateway 주소나 load balancer 주소보다 논리 서비스 이름을 기준으로
요청을 보낸다. framework runtime은 접근하는 service마다 별도 channel을 두고,
그 channel이 해당 service view의 `Discovery`와 outbound socket을 관리하는
방향을 기본으로 본다.

현재 초안은 아래 공개 기반 기능을 전제로 설명한다.

- Discovery 기반 서비스 뷰와 자동 연결
- Registry topology snapshot/query 및 원격 `RegistryQueryClient`
- `service_name` 기준 direct request/reply
- `SPOT` publish/subscribe와 routed request/reply
- raw `STREAM`의 packet callback 같은 low-level helper

즉 `framework/doc/spec/`는 위 기능을 새로 정의하는 자리가 아니라, 그 기능들을
프레임워크 사용자에게 어떤 개념과 API로 다시 보일지 정리하는 자리다.

## 문서 구조

| 섹션 | 경로 | 설명 |
|------|------|------|
| **Draft** | [draft/README.ko.md](./draft/README.ko.md) | 구현 전 초안과 use case 기반 설계 문서 |

## 현재 상태

현재는 정식 spec보다 draft 문서가 먼저 있다.
즉 요구를 먼저 모으고, use case를 먼저 정리한 뒤, 그 결과를 바탕으로 정식 계약을
나중에 분리하는 방식을 따른다.
