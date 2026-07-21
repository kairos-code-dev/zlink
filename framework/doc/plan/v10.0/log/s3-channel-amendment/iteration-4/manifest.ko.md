# S3 Channel·fanout amendment 독립 리뷰 범위 — iteration 4

## 1. 검토 질문

> Iteration 3의 모든 finding을 수정한 Channel·classic fanout 계약과 location store 기반 fanout 자동
> 발견 계약이 Core, framework 공통 spec, 다섯 언어 exact interface, E2E·sample·fixture·verifier에서
> 하나의 모순 없는 10.0.0 계약을 이루는가?

## 2. 동결 식별자

| 항목 | 값 |
|---|---|
| stage | `S3-CH`, `S3-FO` |
| iteration | `4` |
| 동결 시각 | `2026-07-20T19:34:59+09:00` |
| 기준 HEAD | `86258cb9a3ecbc7db2dfd86a8c18de45a562734a` |
| 검토 파일 수 | `71` |
| 파일 집합 SHA-256 | `c6c88d4db2ee18b87f99eec7c5cd80d6e1f6715c3324b2249c1996bb20c846da` |
| 파일 목록 SHA-256 | `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818` |
| 파일 목록 | [`scope-files.txt`](./scope-files.txt) |
| 파일별 SHA-256 | [`scope-files.sha256`](./scope-files.sha256) |
| 공통 prompt | [`prompt.md`](./prompt.md) |

기준 HEAD는 저장소 기준점이며 파일별 SHA-256은 그 위의 현재 작업 트리를 동결한다. Reviewer는 시작과
종료 시 71개 파일을 다시 확인한다. 하나라도 다르면 결과를 채택하지 않고 `SNAPSHOT DRIFT`로 종료한다.

## 3. Iteration 4 추가 확인

Iteration 3의 scope에 common message model을 추가해 71개를 전체 검토한다. 특히 classic fanout의
ChannelName+typed event와 bounded admission, generic peer Fanout role 제거, draining·stale generation과
revision 배제, mode·RID 충돌 startup 실패, Java·C++ 유효 예제와 C++ zero membership·startup 오류 분류를
확인한다.

## 4. 리뷰 기준과 경계

- 정식 spec과 언어별 exact interface가 공개 계약의 근거다. Gap과 E2E는 목표 계약을 만들지 않는다.
- Classic fanout caller는 transport topic이나 packet name을 넘기지 않고 typed event를 제공한다.
- Automatic fanout은 전용 publisher descriptor·store·owner lease만 사용하며 generic peer를 재사용하지
  않는다.
- Store 없는 fixed endpoint publisher와 manual subscriber는 기존 동작을 유지한다.
- Core bridge·relay, MeshName 재노출, weight 0 client, timeout 증가와 언어별 private helper는 허용된
  해결책이 아니다.

## 5. 동결 검증

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.txt
```

## 6. Reviewer 출력

Finding은 `[원칙][severity]` 또는 `[계약][severity]` 형식으로 쓰며 severity는 blocker, high, medium,
low 중 하나다. Finding이 없고 시작·종료 hash가 같을 때만 마지막 줄을 `DOC REVIEW CLEAN`으로 쓴다.
Reviewer는 파일을 수정하지 않는다.
