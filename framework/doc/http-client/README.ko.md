# ZLink HTTP Client — 사용 안내 (언어별)

> **이 트리는 사용 안내만 갖는다. 계약은 여기 없다.**
>
> HTTP client의 공개 계약은 [`framework/spec/http-client/`](../framework/spec/http-client/README.ko.md)가
> 소유한다 — framework-facing 계약([12](../framework/spec/http-client/12-http-client.ko.md)), 상세
> 계약(01~11), 언어별 public API(`languages/<lang>/`). **가이드와 계약이 어긋나면 계약이 이긴다.**

## 언어별 가이드

| 언어 | 가이드 | 상태 |
|------|--------|------|
| `.NET` | [framework/dotnet/http-client](../framework/dotnet/http-client/README.ko.md) | **정비 완료** — 언어별 문서 진입점으로 합쳤다 |
| C++ | [cpp](cpp/README.ko.md) | 낡음 — 재작성 예정 |
| Java | [java](java/README.ko.md) | 낡음 — 재작성 예정 |
| Kotlin | [kotlin](kotlin/README.ko.md) | 낡음 — 재작성 예정 |
| Node.js | [node](node/README.ko.md) | 낡음 — 재작성 예정 |

**`.NET` 가이드만 리뷰를 마쳤다.** 나머지 넷은 그 이전 상태이며, `.NET` 가이드가 완성되면
**삭제하고 그것을 기준으로 다시 쓴다.** 그때 이 트리는 사라지고 각 언어의
`framework/doc/framework/<lang>/http-client/`로 들어간다.

## 그 밖에

| | |
|---|---|
| [perf](perf/README.ko.md) | 성능 시나리오·지표·회귀 판정 |
| [통일 계획](http-client-unification-plan.ko.md) | 5개 언어 통일 작업 기록 |
