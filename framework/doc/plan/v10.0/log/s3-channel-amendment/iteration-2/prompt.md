# RouteMesh 10.0.0 Channel·fanout amendment 독립 문서 리뷰 — iteration 2 공통 prompt

너는 S3-CH·S3-FO iteration 2의 독립 문서 reviewer다. 다른 reviewer 결과나 coordinator의 해석을 보지
말고 snapshot 전체를 처음부터 검토하라. 파일을 수정하지 마라.

## Snapshot

- 기준 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- scope: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.txt`
- 파일 수: `70`
- 파일 집합 SHA-256: `72431e8feef1b758a6879fc2f8e866db935f038d32f547cc978ebb1ea633f76f`
- 파일 목록 SHA-256: `0f1172592c455d39fd208e01517133c2b583989ad8deff6e4d66644be71d8208`
- 파일별 hash: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.sha256`

시작과 종료 시 아래 명령을 실행하라. 하나라도 다르면 검토하지 말고 `SNAPSHOT DRIFT`로 종료하라.

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-2/scope-files.txt
```

## 반드시 읽을 기준

1. 루트 `AGENTS.md`
2. `doc/principal/documentation/documentation-principles.ko.md`
3. `doc/principal/software-design-principles.md`
4. iteration 2 `manifest.ko.md`, 목록과 scope 70개 전체

## 전체 검토 질문

1. Iteration 1의 fanout, Logical Multicast MeshName, Config 12 오류 분리, Bingo·TicTacToe·ZoneWorld·
   ShoppingMall·GameQuest topology, Java·C++·Node exact, Redis fixture와 verifier finding이 모두 해소됐는가?
2. ChannelName이 process-local 송신 경로를 유일하게 선택하고 MeshName이나 endpoint를 호출자에게 다시
   요구하지 않는가? 충돌, target 없음, 연결 미준비와 protocol 오류가 구분되는가?
3. RouteMesh와 ClientServer의 역할·물리 topology·descriptor·drain을 섞지 않고, 다른 egress에서 온
   completion을 exactly-once로 원래 Spot turn에 돌려주는가?
4. Fanout automatic subscriber가 endpoint 입력 없이 같은 ChannelName의 live publisher 전용 descriptor를
   모두 연결하고 다른 kind·ChannelName·draining·expired·stale record를 제외하는가?
5. Store 등록 publisher만 fixed/allocated Publisher RID, actual advertised endpoint, owner lease와 전용
   descriptor를 게시하는가? Store 없는 fixed endpoint publisher/manual subscriber 회귀를 보존하는가?
6. Fanout descriptor·key·HASH·JSON field order·change stamp와 세 store operation이 다섯 언어 exact
   interface, Redis fixture와 verifier에서 같은 계약인가?
7. Publisher 추가·정상 제거, crash lease 만료, 같은 RID 재등록, port 0 endpoint 변경, store fail-static과
   recovery, manual mode와 store 누락을 Config 3과 다섯 feature map이 빠짐없이 검증하는가?
8. BindHost, AdvertiseHost와 actual port가 RouteMesh, ClientServer, fanout과 STREAM에서 같은 원칙을
   사용하면서 topology별 record를 분리하는가?
9. Config 12와 일곱 sample topology가 중복 연결, wildcard pseudo channel, reciprocal manual peer와
   구현 전용 helper를 정상 패턴으로 허용하지 않는가?
10. Unified/.NET inventory와 verifier가 exact declaration, 995개 언어별 scenario row, Config 12·sample과
    Redis fixture의 의미를 실제로 고정하며 단순 문자열 존재만으로 잘못된 계약을 통과시키지 않는가?
11. 정식 spec에는 10.0.0 목표 계약만 있고 구현 진행 상태는 gap·feature map에만 있는가?

## 출력 계약

시작 hash와 70개 전체를 읽었다는 사실을 기록한다. Finding severity는 `blocker`, `high`, `medium`, `low` 중
하나를 사용한다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

같은 원인은 하나로 묶고 공개 의미, signature, owner 또는 검증 가능성의 위반을 근거로 판정하라. 종료
hash를 다시 확인한다. Finding이 없고 snapshot이 같을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로
쓰고, 그 밖에는 `DOC REVIEW NOT CLEAN`으로 쓴다.
