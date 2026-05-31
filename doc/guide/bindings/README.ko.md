[코어 가이드](../01-overview.ko.md)

# 언어별 바인딩 가이드

zlink는 C 코어 위에 여러 언어 바인딩을 제공합니다. 각 가이드는 **그 언어에서
zlink를 쓰는 방법**(설치, 관용 예제, 타입 매핑, 언어 고유 규칙)을 다룹니다.
메시징 **개념 자체**(소켓 패턴, 트랜스포트, 서비스, 라우팅 ID)는 언어 중립적으로
[코어 가이드](../01-overview.ko.md)가 한 번만 소유하며, 각 언어 가이드는 개념이
필요한 지점마다 코어로 링크합니다.

## 읽는 순서

- **이미 메시징을 안다 / 빨리 쓰고 싶다** → 자기 언어 가이드로 바로 가세요. 막히는
  개념은 그 자리에서 코어 링크로 확인하면 됩니다.
- **메시징이 처음이다** → 코어 [개요](../01-overview.ko.md)와
  [소켓 패턴](../03-0-socket-patterns.ko.md)을 먼저 본 뒤 언어 가이드로 오세요.

## 언어 고르기

| 언어 | 사용 가이드 | 생성 API 레퍼런스 | 패키지 |
|------|------------|------------------|--------|
| .NET | [dotnet/](./dotnet/index.ko.md) | docfx | `Systems.Zlink` |
| C++ | _(작성 예정)_ | doxygen | — |
| Java | [java/](./java/index.ko.md) | javadoc | `systems.zlink:zlink-java` |
| Node | _(작성 예정)_ | typedoc | — |
| Python | _(작성 예정)_ | sphinx | — |
| Go | [go/](./go/index.ko.md) | godoc | `zlink.systems/zlink` |
| Rust | _(작성 예정)_ | rustdoc | — |

> C는 코어 그 자체이므로 별도 바인딩 가이드 대신
> [코어 C API 가이드](../02-core-api.ko.md)를 봅니다.

## 가이드 구성 (모든 언어 공통)

각 언어 가이드는 코어 가이드의 구획에 맞춰 같은 문서 세트를 따릅니다. 한 언어를
익히면 다른 언어 가이드를 같은 위치에서 바로 읽을 수 있습니다.

| 문서 | 내용 |
|------|------|
| `index` | 기능 지도 + 진입 안내 |
| `01-getting-started` | 설치 · 5분 예제 · 핵심 타입 · 소유권 |
| `02-messaging` | 소켓 패턴별 사용법 |
| `03-services` | Registry · Discovery · SpotNode·Spot · Actor |
| `04-operations` | 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 · 네이티브 |
| `05-reference` | 에러 · 코덱 · C API↔언어 대응표 · API 레퍼런스 · 샘플 |
