# S3 Channel·fanout amendment 독립 리뷰 범위 — iteration 6

## 1. 검토 질문

> Framework RouteMesh 10.0.0의 Channel·classic fanout 계약이 현재 Core service 계약, framework 공통
> spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서 모순 없이 이어지는가?

Framework RouteMesh와 Core는 version 축이 독립적이다. 이 snapshot은 Framework RouteMesh 10.0.0이
현재 Core service 계약을 사용하는 관계를 검토한다. Core 정식 spec의 10.1.0 표기는 Framework version을
10.1.0으로 바꾸거나 두 version을 같은 값으로 맞추라는 뜻이 아니다. 현재 Core runtime의 bugfix version은
10.6.0이며 이 문서 검토는 Core package 배포나 version 변경을 수행하지 않는다.

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO` |
| iteration | `6` |
| 동결 시각 | `2026-07-20T20:39:52+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `71` |
| 파일 집합 SHA-256 | `f9b575eebbc99f598192eee61627df8bc179c8cc268d0015a62c13ed5bff7f8a` |
| 파일 목록 SHA-256 | `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

기준 HEAD는 저장소 기준점이며 파일별 SHA-256은 그 위의 현재 작업 트리를 동결한다. Reviewer는 시작과
종료 시 71개 파일을 다시 확인한다. 하나라도 다르면 결과를 채택하지 않고 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 6 추가 확인

Iteration 5 finding을 반영해 observer 취소·close 의미, capacity 1 bounded coalescing과 sequence gap 복구,
manual endpoint mutation 격리, store publisher의 RID 설정 누락 오류를 고정했다. 다섯 Pub/Sub feature
map은 public fanout snapshot·닫힌 event variant·actual native readiness만 완료 증거로 사용한다. Node는
RouteMesh·ClientServer·fanout public runtime을 얻는 NestJS token과 provider 조건을 각각 제공한다.

## 4. 리뷰 기준과 경계

- 정식 spec과 언어별 exact interface가 공개 계약의 근거다. Gap과 E2E는 목표 계약을 만들지 않는다.
- Classic fanout caller는 transport topic이나 packet name을 넘기지 않고 typed event를 제공한다.
- Automatic fanout은 전용 publisher descriptor·store·owner lease만 사용하며 generic peer를 재사용하지
  않는다.
- `connect()` 성공과 connection intent는 ready가 아니며 실제 native SUB connection 상태만 ready를
  결정한다.
- Store 없는 fixed endpoint publisher와 manual subscriber는 기존 동작을 유지한다.
- Core bridge·relay, MeshName 재노출, weight 0 client, timeout 증가와 언어별 private helper는 허용된
  해결책이 아니다.

## 5. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-6/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-6/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-6/scope-files.txt
```

## 6. Reviewer 출력

Finding은 `[원칙][severity]` 또는 `[계약][severity]` 형식으로 쓰며 severity는 blocker, high, medium,
low 중 하나다. Finding이 없고 시작·종료 hash가 같을 때만 마지막 줄을 `DOC REVIEW CLEAN`으로 쓴다.
Reviewer는 파일을 수정하지 않는다.
