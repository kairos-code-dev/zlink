# S3 Channel amendment 독립 리뷰 범위 — iteration 1

## 1. 검토 질문

> Core의 membership 0개 MeshNode 계약부터 framework의 ChannelName 단일 주소, RouteMesh·ClientServer 역할,
> network identity, 다섯 언어 exact interface, Config 12와 공통 sample까지 하나의 모순 없는 10.0.0 계약을
> 이루며 실제 구현과 회귀 검증을 시작할 만큼 정확하게 닫혀 있는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH` |
| iteration | `1` |
| 동결 시각 | `2026-07-20T17:40:08+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `57` |
| 파일 집합 SHA-256 | `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d` |
| 파일 목록 SHA-256 | `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

기준 HEAD는 저장소 기준점이며, 파일별 SHA-256은 그 HEAD 위의 현재 작업 트리에서 S1-CH·S2-CH 변경을
포함한 실제 리뷰 입력을 동결한다. Reviewer는 시작과 종료 시 57개 파일의 hash와 aggregate를 다시
계산한다. 하나라도 다르면 결과를 채택하지 않고 `SNAPSHOT DRIFT`로 종료한다.

## 3. 범위 구성

| 원래 범위 | 파일 수 | 중복 제거 뒤 새로 포함된 수 |
|---|---:|---:|
| S1-CH-01 Core 정식 spec 한영 | 2 | 2 |
| S2-CH-02 framework 공통·server 정식 spec | 17 | 17 |
| S2-CH-03 다섯 언어 exact interface와 공통 gap | 21 | 21 |
| S2-CH-04 E2E·sample·fixture·inventory·verifier | 22 | 17 |
| 합계 | 62 | 57 |

S2-CH-04의 22개 중 정식 spec link·anchor를 함께 고친 5개는 S2-CH-02와 겹치므로 한 번만 포함한다.
Config 12 문서와 role fixture, 공통 sample topology fixture, unified/.NET inventory와 verifier는 모두
동결 범위에 포함되어 있다.

## 4. 리뷰 기준과 경계

- 루트 `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
  `doc/principal/software-design-principles.md`를 먼저 읽는다.
- 공개 계약 근거는 동결된 Core 정식 spec, framework 공통·server spec과 언어별 exact interface다.
- `90-implementation-gap.ko.md`는 현재 구현과 목표 계약의 차이만 기록한다. 구현이 없다는 이유로 목표
  계약을 축소하는 근거로 사용하지 않는다.
- Config 12와 sample은 공개 계약을 검증하고 설명하는 입력이다. 새 공개 API의 독립 근거로 사용하지 않는다.
- Draft, 실행 ledger와 구현 source는 이번 snapshot의 계약 판정 범위가 아니다. 계약상 불가능한 구현을
  발견한 경우에만 어떤 정식 owner가 부족한지 finding으로 기록한다.
- Core bridge·relay, MeshNode descriptor 재사용, weight 0 client 표현과 MeshName을 송신 호출에 다시
  노출하는 우회는 허용된 대안으로 취급하지 않는다.

## 5. 동결 검증

저장소 root에서 다음 검사를 시작과 종료 시 각각 실행한다.

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.txt
```

두 번째 명령 결과는 파일 집합 SHA-256, 세 번째 결과는 파일 목록 SHA-256과 일치해야 한다.

## 6. Reviewer 입력과 출력

Codex와 Claude Sonnet에 [`prompt.md`](./prompt.md)를 byte 단위로 동일하게 제공한다. 각 reviewer는 다른
reviewer의 결과나 coordinator의 해석을 보지 않고 57개 전체를 처음부터 독립적으로 검토한다.

Finding 형식은 다음과 같다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

Finding이 하나도 없을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. Reviewer는 파일을 수정하지
않는다.
