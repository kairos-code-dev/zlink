# RouteMesh 10.0.0 Channel·fanout amendment 독립 문서 리뷰 — iteration 7 공통 prompt

너는 S3-CH·S3-FO iteration 7의 독립 문서 reviewer다. 다른 reviewer 결과나 coordinator의 해석을 보지
말고 snapshot 전체를 처음부터 검토하라. 파일을 수정하지 마라.

## Snapshot

- 기준 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- scope: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-7/scope-files.txt`
- 파일 수: `71`
- 파일 집합 SHA-256: `0f58a4cc8368b9aad99cc69213c2f3bf99facc3a3587f482847c86ac7ebab608`
- 파일 목록 SHA-256: `00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`
- 파일별 hash: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-7/scope-files.sha256`

시작과 종료 시 세 hash 명령을 manifest대로 실행한다. 하나라도 다르면 `SNAPSHOT DRIFT`로 종료한다.

## 반드시 읽을 기준

1. 루트 `AGENTS.md`
2. `doc/principal/documentation/documentation-principles.ko.md`
3. `doc/principal/software-design-principles.md`
4. iteration 7 `manifest.ko.md`, 목록과 scope 71개 전체

## 전체 검토 질문

1. 이전 iteration finding이 모두 해소됐는가?
2. ChannelName이 송신 경로를 선택하고 MeshName이나 endpoint를 업무 호출 인자로 다시 요구하지 않는가?
3. 일곱 sample이 canonical fixture의 topology와 정확히 일치하고 reciprocal manual peer를 만들지 않는가?
4. Classic fanout caller는 ChannelName과 typed event만 제공하고 Framework가 packet name을 결정하는가?
5. Automatic subscriber가 endpoint 없이 같은 ChannelName의 live publisher descriptor만 연결하는가?
6. Publisher record의 RID·actual endpoint·lease·generation/revision과 store failure 동작이 공통·exact·fixture에서 같은가?
7. 실제 native readiness, observer bounded lifecycle, cancellation·close, event variant가 다섯 언어에서 같은가?
8. Node 정적 provider 부재와 동적 provider `null`이 signature·설명·verifier에서 일치하는가?
9. Config 3의 자동·manual·negative·lifecycle scenario와 다섯 feature map이 공개 표면으로 검증 가능한가?
10. Inventory와 verifier가 public declaration, 1,000개 scenario row, sample topology와 금지 표현을 구조적으로 고정하는가?
11. Framework 정식 spec에는 10.0.0 목표 계약만 있고 현재 구현 차이는 gap·feature map에만 있는가?

## 출력 계약

시작 hash와 71개 전체 검토 사실을 기록한다. Finding은 다음 형식으로만 기록한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

종료 hash를 다시 확인한다. Finding이 없고 snapshot이 같을 때만 마지막 줄을 정확히
`DOC REVIEW CLEAN`으로 쓰고, 그 밖에는 `DOC REVIEW NOT CLEAN`으로 쓴다.
