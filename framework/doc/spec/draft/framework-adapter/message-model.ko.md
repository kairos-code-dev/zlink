[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework Message Model

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 아래 메시지 구성과 header 필드는 방향 설명을 위한
> 제안이다.

## 1. 목적

`ZLink Framework`의 기본 메시지 단위는 `header + body` 멀티파트로 본다.
이 구조는 codec 교체와 metadata 전달을 함께 설명하기 쉽고, 요청/응답과 이벤트를
같은 큰 틀 안에서 다루기 좋다.

## 2. 기본 구조 초안

현재 초안은 기본적으로 2개 part를 전제로 한다.

1. `header`
2. `body`

다만 이것이 "항상 part가 2개뿐이다"를 뜻하지는 않는다.
앞으로 attachment나 추가 payload part가 필요해질 수 있으므로, wire 수준에서는
확장 여지를 남겨 두는 편이 낫다.

## 3. header가 담아야 할 정보 초안

| 필드 | 용도 |
|------|------|
| `message-kind` | request, response, command, event 구분 |
| `service` | 논리 서비스 이름 |
| `method` 또는 `pattern` | handler 선택에 쓰는 이름 |
| `content-type` | body codec 식별 |
| `correlation-id` | 요청과 응답 연결 |
| `deadline` 또는 `timeout` | 시간 제한 전달 |
| `status` | 응답 상태 |
| `error-code` | 공통 에러 코드 |
| `source` | 호출자 식별 정보 |
| `target` | 필요할 때 명시적 대상 정보 |
| `trace-id` | 여러 단계 호출을 잇는 추적 정보 |
| `causation-id` | 어떤 이전 메시지에서 파생됐는지 식별 |

이 중 무엇을 필수로 할지는 구현 전에 더 줄여야 한다.
지금 단계에서는 "어떤 종류의 정보가 필요한가"를 먼저 정리한다.

## 4. body codec 방향

`body`는 특정 포맷으로 고정하지 않는다.
우선 고려하는 codec은 아래와 같다.

| codec | 설명 |
|-------|------|
| `protobuf` | typed contract에 적합 |
| `json` | 빠른 개발과 디버깅에 적합 |

나중에 필요하면 다른 codec을 추가할 수 있어야 한다.

즉 `ZLink Framework`는 "body가 어떤 codec인가"를 handler와 client가 알 수 있게
해 주되, core transport가 그 codec 내용을 직접 이해하려고 하지는 않는 방향이
맞다.

## 5. 요청과 응답의 기본 의미

### 5.1 request

- `message-kind = request`
- `correlation-id` 필수
- `method` 또는 그와 같은 handler 식별값 필요

### 5.2 response

- `message-kind = response`
- 같은 `correlation-id`를 되돌려 준다
- 성공이면 `status`, 실패면 `status + error-code`를 함께 보낸다

## 6. 이벤트의 기본 의미

이벤트는 응답을 기대하지 않으므로 아래가 핵심이다.

- `message-kind = event`
- `pattern` 또는 `event-name`
- 선택적 metadata

## 7. 이 문서가 아직 확정하지 않는 것

- header를 어떤 binary 형식으로 인코딩할지
- header 자체도 codec별로 다르게 둘지
- 표준 status code 체계를 어떻게 정의할지
- body 없는 메시지를 어떻게 표현할지
- partial success나 aggregate result를 어떤 공통 형식으로 담을지

이 항목들은 use case를 더 모은 뒤에 좁히는 편이 낫다.
