# Public Contract 디렉토리 그룹 초안

> 이 문서는 구현 전 설계 초안이다. 현재 공개 계약은 아니다.
> 현재 정식 공개 계약의 기준은 `core/include/zlink.h` 와 그 헤더가 포함하는
> 기존 `core/include/zlink/*.h` 파일이다.

## 목적

현재 C 공개 헤더는 기능이 늘어나면서 평면 파일 목록에 많은 계약이 모여 있다.
특히 socket, service, spot 계약은 하나의 헤더 안에 여러 역할이 섞여 있어
처음 읽는 사람이 어떤 API를 어떤 영역의 계약으로 봐야 하는지 파악하기 어렵다.

새 레이아웃의 목적은 공개 계약을 기능 영역별 디렉토리로 나누어 읽기 경로를
단순하게 만드는 것이다. 초기 단계에서는 기존 헤더를 그대로 유지하고, 새 경로의
헤더는 기존 헤더를 포함하는 얇은 진입점으로 둔다. 이렇게 하면 호출자는 새
경로를 미리 사용할 수 있고, 내부 구현과 바인딩은 기존 계약을 깨지 않고 천천히
이동할 수 있다.

## 제안 레이아웃

| 영역 | 새 진입점 | 포함하는 기존 계약 |
|------|-----------|--------------------|
| Core | `zlink/core/api.h` | `zlink/common.h`, `zlink/core.h`, `zlink_enum.h`, `zlink_errno.h` |
| Message | `zlink/message/api.h` | `zlink/message.h` |
| Socket | `zlink/socket/api.h` | `zlink/socket.h` |
| Socket options | `zlink/socket/options.h` | `zlink/socket.h` |
| Socket parts | `zlink/socket/parts.h` | `zlink/socket.h` |
| Eventing | `zlink/eventing/api.h` | `zlink/monitoring.h` |
| Service | `zlink/service/api.h` | `zlink/service.h` |
| Service registry | `zlink/service/registry.h` | `zlink/registry.h` |
| Service discovery | `zlink/service/discovery.h` | `zlink/discovery.h` |
| Service spot | `zlink/service/spot.h` | `zlink/spot.h` |
| Service actor | `zlink/service/actor.h` | `zlink/actor.h` |

## POSD 관점 판단

### 위험 신호

1. `zlink/socket.h` 는 socket 생성, 옵션, 메시지 송수신, stream 보조 계약을
   한 파일에 함께 노출한다. 호출자가 필요한 계약보다 큰 표면을 읽게 되므로
   인터페이스가 얕아진다.
2. service 계약은 `service.h`, `registry.h`, `discovery.h`, `spot.h`,
   `actor.h` 사이의 관계를 파일 이름만으로 알기 어렵다. 공개 계약의 분류
   지식이 문서와 바인딩에 흩어질 수 있다.
3. 설치 규칙이 평면 헤더와 하위 디렉토리 헤더를 구분하지 않으면, 공개 계약
   구조라는 설계 결정이 빌드 시스템 밖으로 새어 나간다.

### 검토한 방향

첫 번째 방향은 기존 헤더를 바로 잘게 쪼개는 것이다. 최종 구조는 가장 깔끔하지만
`zlink.h` 를 포함하는 기존 사용자와 바인딩에 미치는 영향이 크다.

두 번째 방향은 새 디렉토리 진입점을 먼저 추가하고, 기존 헤더는 정식 계약으로
유지하는 것이다. 이 방식은 중복 경로가 잠시 생기지만 호출자와 바인딩의 변경
부담을 낮추며, 이후 실제 선언 이동을 기능별로 검증할 수 있다.

현재 단계에서는 두 번째 방향을 선택한다. 공개 계약의 읽기 경로를 먼저 안정화한
뒤, `socket.h` 와 service 계열 선언을 기능 단위로 옮기는 것이 변경 위험이 작다.

## 다음 단계

1. 새 디렉토리 헤더가 설치 패키지에 포함되는지 테스트한다.
2. 바인딩 문서에서 공개 계약 분류를 새 경로 기준으로 맞춘다.
3. `socket/options.h` 와 `socket/parts.h` 에 실제 선언을 옮길 때는
   `socket.h` 를 호환 집계 헤더로 남긴다.
4. service 계열 선언 이동은 registry, discovery, spot, actor 순서로 진행한다.
   각 단계마다 `core/include/zlink.h`, 테스트, 바인딩 문서를 함께 확인한다.
