# S1 Core 정식 스펙 리뷰 범위 — iteration 3

## 1. 리뷰 목적

Core 10.0.0 공개 계약을 구현하기 전에 정식 spec 전체를 다시 검증한다. iteration 2 finding만 확인하는
수정 확인 리뷰가 아니라, 동결한 52개 파일을 처음부터 다시 읽는 독립 전체 리뷰다.

리뷰는 Codex와 Claude Sonnet이 서로의 결과를 보지 않은 상태에서 각각 수행한다. 한 리뷰에서라도
수정 사항이 나오면 이 범위를 다시 동결하고 두 리뷰를 모두 처음부터 반복한다. 두 리뷰가 모두
`DOC REVIEW CLEAN`을 반환해야 S1을 완료한다.

## 2. 동결 범위

- `core/doc/spec/README.ko.md`
- `core/doc/spec/README.md`
- `core/doc/spec/core/` 아래의 모든 한국어·영문 Markdown 문서

동결 범위는 52개 파일이다.

```bash
files=$( { printf '%s\n' core/doc/spec/README.ko.md core/doc/spec/README.md; \
  rg --files core/doc/spec/core -g '*.md'; } | sort -u )
printf '%s\n' "$files" | xargs sha256sum | sha256sum
```

```text
cb6a9e554176c62645974a3c61661e92ed7b0f5cacb83fe8f0345b5ece942873  -
```

## 3. 계약 근거와 보조 자료

- 공개 구현 기준 header: `core/include/zlink.h`와 이 header가 포함하는 공개 header
- 양방향 API inventory: `framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md`
- iteration 2 finding과 수정 증거:
  `framework/doc/plan/v10.0/log/s1-core-review/iteration-2/finding-ledger.ko.md`
- 문서 원칙: 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`
- 설계 원칙: `doc/principal/software-design-principles.md`

보조 자료는 정식 계약을 대신하지 않는다. 정식 spec은 계획 문서를 참조하지 않는다.

## 4. 필수 리뷰 축

1. 공개 C ABI의 함수 signature, `ZLINK_EXPORT`, type, enum, field와 macro가 완결되어 있는가.
2. 성공·실패 result, errno, ownership, lifetime, thread safety, callback 재진입과 close 규칙이 서로
   모순되지 않는가.
3. MeshNode, dispatch, Spot, Actor, STREAM session과 raw socket의 책임 경계가 명확한가.
4. Logical Multicast, request/reply, metadata, timer, Actor transfer와 session barrier를 추측 없이 구현할
   수 있는가.
5. monitoring, polling, status와 event가 service lifecycle 및 오류 계약과 일치하는가.
6. 한국어·영문 문서가 heading, C block, 동작과 제한을 동일하게 정의하고 번호·link가 정확한가.
7. 정식 spec이 현재 10.0.0 공개 계약만 설명하고 plan, 이전 버전 또는 내부 구현을 섞지 않는가.
8. POSD 관점에서 transport, queue, correlation, registry 같은 결정을 호출자에게 노출하거나 얕은 API를
   추가하지 않았는가.

iteration 2 회귀 여부도 확인하되 이 목록에 검토 범위를 한정하지 않는다. 특히 raw socket은 공개
header의 retained `*_part` API만 사용해야 하고, Actor location은 Spot lifecycle generation을 보존해야
하며, Utilities의 export·Timer thread-safety와 한영 순서가 같아야 한다.

## 5. 결과 형식

이슈가 있으면 심각도, 문서와 줄, 계약이 깨지는 이유와 수정 방향을 기록한다. 중요한 이슈가 하나도
없으면 정확히 다음 한 줄만 반환한다.

```text
DOC REVIEW CLEAN
```
