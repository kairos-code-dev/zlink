# RouteMesh 10.0.0 Channel·fanout amendment 독립 문서 리뷰 — iteration 4 공통 prompt

너는 S3-CH·S3-FO iteration 4의 독립 문서 reviewer다. 다른 reviewer 결과나 coordinator의 해석을 보지
말고 snapshot 전체를 처음부터 검토하라. 파일을 수정하지 마라.

## Snapshot

- 기준 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- scope: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.txt`
- 파일 수: `71`
- 파일 집합 SHA-256: `c6c88d4db2ee18b87f99eec7c5cd80d6e1f6715c3324b2249c1996bb20c846da`
- 파일 목록 SHA-256: `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`
- 파일별 hash: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.sha256`

시작과 종료 시 아래 명령을 실행하라. 하나라도 다르면 검토하지 말고 `SNAPSHOT DRIFT`로 종료하라.

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-4/scope-files.txt
```

## 반드시 읽을 기준

1. 루트 `AGENTS.md`
2. `doc/principal/documentation/documentation-principles.ko.md`
3. `doc/principal/software-design-principles.md`
4. iteration 4 `manifest.ko.md`, 목록과 scope 71개 전체

## 전체 검토 질문

1. Iteration 1~3 finding이 모두 해소됐는가?
2. ChannelName이 process-local 송신 경로를 유일하게 선택하고 MeshName이나 endpoint를 caller에게 다시
   요구하지 않는가?
3. Classic fanout caller는 ChannelName과 typed event만 제공하고 Framework가 packet name을 결정하며,
   전용 call이 bounded admission 결과를 반환하는가? Logical Multicast 결과와 섞이지 않는가?
4. Fanout automatic subscriber가 endpoint 없이 같은 ChannelName의 live publisher 전용 descriptor를 모두
   연결하고 다른 kind·ChannelName·draining·expired·낮은 generation/revision을 제외하는가?
5. Store 등록 publisher만 fixed/allocated Publisher RID, actual advertised endpoint, owner lease와 전용
   descriptor를 게시하고 subscriber descriptor는 만들지 않는가?
6. Fanout descriptor·key·HASH·JSON field order·change stamp와 store operation이 다섯 언어 exact interface,
   Redis fixture와 verifier에서 같은가?
7. Publisher 추가·정상 제거, crash lease 만료, 재등록, port 0, store fail-static/recovery, drain·stale,
   manual 회귀와 mode·RID startup 충돌을 Config 3과 다섯 feature map이 검증하는가?
8. RouteMesh·ClientServer·fanout의 topology, descriptor, drain과 completion owner가 섞이지 않는가?
9. Java·C++ 예제가 startup 계약을 만족하고 C++ zero membership과 duplicate handler 오류 분류가 공통
   계약과 같은가?
10. Config 12와 sample topology가 중복 연결, wildcard pseudo channel, reciprocal manual peer와 구현 전용
    helper를 정상 패턴으로 허용하지 않는가?
11. Inventory와 verifier가 exact declaration, 995개 scenario row, fanout outbound와 discovery 의미를
    구조적으로 고정하는가?
12. 정식 spec에는 10.0.0 목표 계약만 있고 구현 진행 상태는 gap·feature map에만 있는가?

## 출력 계약

시작 hash와 71개 전체를 읽었다는 사실을 기록한다. Finding severity는 `blocker`, `high`, `medium`,
`low` 중 하나를 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

같은 원인은 하나로 묶고 공개 의미, signature, owner 또는 검증 가능성의 위반을 근거로 판정하라. 종료
hash를 다시 확인한다. Finding이 없고 snapshot이 같을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로
쓰고, 그 밖에는 `DOC REVIEW NOT CLEAN`으로 쓴다.
