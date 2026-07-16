# S1 Core 정식 스펙 리뷰 범위 — iteration 2

## 1. 리뷰 목적

이 iteration은 Core 10.0.0 공개 계약을 구현하기 전에 마지막으로 검증한다. 리뷰 대상은 현재 계약만
설명하는 정식 spec이며, `framework/doc/plan/v10.0/` 문서는 계약의 근거로 사용하지 않는다.

리뷰는 Codex와 Claude Sonnet이 서로의 결과를 보지 않은 상태에서 각각 수행한다. 한 리뷰에서라도
수정 사항이 나오면 이 범위를 다시 동결하고 두 리뷰를 모두 처음부터 반복한다. 두 리뷰가 모두
`DOC REVIEW CLEAN`을 반환해야 S1을 완료한다.

## 2. 동결 범위

- `core/doc/spec/README.ko.md`
- `core/doc/spec/README.md`
- `core/doc/spec/core/` 아래의 모든 한국어·영문 Markdown 문서

동결 범위는 52개 파일이다. 다음 명령의 출력 전체를 다시 SHA-256으로 계산한 값은 아래와 같다.

```bash
files=$( { printf '%s\n' core/doc/spec/README.ko.md core/doc/spec/README.md; \
  rg --files core/doc/spec/core -g '*.md'; } | sort -u )
printf '%s\n' "$files" | xargs sha256sum | sha256sum
```

```text
d0b550591986c1950d1903b3ea3fc0be21bb9c29a32a356de8425892a0c7b731  -
```

## 3. 계약 근거와 보조 자료

- 공개 구현 기준 header: `core/include/zlink.h`와 이 header가 포함하는 공개 header
- 기존 공개 표면과 10.0.0 목표 표면의 양방향 검증:
  `framework/doc/plan/v10.0/s1-core-public-api-inventory.ko.md`
- 문서 분리와 작성 원칙: 루트 `AGENTS.md`,
  `doc/principal/documentation/documentation-principles.ko.md`
- 설계 판단 원칙: `doc/principal/software-design-principles.md`

보조 자료는 정식 계약을 대신하지 않는다. 공개 header에 아직 없는 10.0.0 목표 계약은 inventory가
누락 여부를 검증하고, 구현은 S1 완료 뒤 별도 단계에서 spec에 맞춘다.

## 4. 필수 리뷰 축

각 리뷰는 다음 항목을 모두 확인한다.

1. 공개 C ABI의 함수 signature, type, enum, field, macro와 export 선언이 완결되어 있는가.
2. 성공·실패 결과, errno, ownership, lifetime, thread safety, callback 재진입과 close 규칙이 서로
   모순되지 않는가.
3. MeshNode, dispatch, Spot, Actor, STREAM session의 책임 경계와 raw socket 비변경 경계가 명확한가.
4. Logical Multicast, request/reply, metadata, timer, Actor transfer와 session barrier를 구현자가 추측
   없이 구현할 수 있는가.
5. monitoring, polling, status와 event가 service lifecycle 및 오류 계약과 일치하는가.
6. 한국어·영문 문서가 같은 공개 계약을 정의하고, 목차·번호·상호 링크가 정확한가.
7. 정식 spec이 현재 10.0.0 계약만 설명하고 plan, 이전 버전, migration 또는 구현 내부 구조를 계약에
   섞지 않는가.
8. POSD 관점에서 transport, queue, correlation, registry 같은 내부 결정을 호출자에게 불필요하게
   노출하거나 얕은 공개 API를 추가하지 않았는가.

## 5. 결과 형식

이슈가 있으면 심각도, 문서와 줄, 계약이 깨지는 구체적인 이유, 필요한 수정 방향을 기록한다. 중요한
이슈가 하나도 없으면 정확히 다음 한 줄만 반환한다.

```text
DOC REVIEW CLEAN
```
