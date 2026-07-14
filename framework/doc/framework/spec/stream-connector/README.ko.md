# Stream Connector 스펙

[스펙 목차](../README.ko.md)

**Stream connector는 브라우저·게임엔진에서 도는 client 산출물이다.** framework host와 배포 단위,
실행 환경, 의존성이 다르다. 별도 패키지로 배포하지만 **wire와 세션 계약은 framework가 소유한다.**

| 문서 | 범위 |
|------|------|
| [32 Stream Connector](32-stream-connector.ko.md) | **framework-facing 계약** — 대상 실행 환경(엔진 × 빌드 타깃), transport, wire 계약, 생명주기, 배포 산출물 |

**서버 쪽 짝은 여기 없다.** connector가 붙는 서버 세션은
[server/30 STREAM 서버 세션](../server/30-stream-session.ko.md)과
[server/31 Session Actor Dispatch](../server/31-session-actor-dispatch.ko.md)가 소유한다.

## 언어별 public API

| 언어 | 문서 |
|------|------|
| `.NET` | [03 Stream Connector](languages/dotnet/03-stream-connector.ko.md) — Unity·Godot 포함 |
| Java | [03 Stream Connector](languages/java/03-stream-connector.ko.md) |
| TypeScript | [languages/typescript](languages/typescript/README.ko.md) — browser connector |

**Node.js framework 계약과 분리한다.** browser connector는 Node.js host의 표면이 아니다
([00 §4](../00-public-contract-governance.ko.md)).

## 사용 안내

이 트리는 **계약**만 소유한다. 언어별 사용 가이드는
[`framework/doc/stream-connector/`](../../../stream-connector/README.ko.md)에 있다.
