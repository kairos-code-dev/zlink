# RouteMesh 10.0.0 Channel amendment 독립 문서 리뷰 — iteration 1 공통 prompt

너는 S3-CH Channel amendment iteration 1의 독립 문서 reviewer다. 이 prompt는 Codex와 Claude Sonnet에
byte 단위로 동일하게 제공된다. 다른 reviewer의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- 기준 HEAD: `86258cb9a3ecbc7db2dfd86a8c18de45a562734a`
- scope: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.txt`
- 파일 수: `57`
- 파일 집합 SHA-256: `d5ecc21f01266decb8e9c075c4fbedce2c453287827e2a83cc087d9325afff6d`
- 파일 목록 SHA-256: `89c2155f4d1644a26fdfae4b98719d1e5b6ebdb1992b914db96a879ffb369428`
- 파일별 hash: `framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.sha256`

저장소 root에서 시작과 종료 시 아래 세 명령을 실행하라.

```bash
sha256sum -c framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.sha256
sha256sum framework/doc/plan/v10.0/log/s3-channel-amendment/iteration-1/scope-files.txt
```

57개 파일 중 하나라도 다르거나 두 aggregate가 위 값과 다르면 문서 검토를 진행하지 말고 마지막 줄을
`SNAPSHOT DRIFT`로 써라. 파일을 수정하지 마라.

## 반드시 읽을 기준

1. 루트 `AGENTS.md`
2. `doc/principal/documentation/documentation-principles.ko.md`
3. `doc/principal/software-design-principles.md`
4. iteration 1 `manifest.ko.md`, `scope-files.txt`와 scope 57개 전체

## 전체 검토 질문

이전 단계의 완료 주장을 신뢰해 검토 범위를 줄이지 말고 57개 전체를 처음부터 검토하라.

1. Core membership 0개 MeshNode가 호출 전용·Node direct 구성에서 start, ready, peer admission, remote
   Channel, multicast, drain, query와 오류를 서로 모순 없이 정의하는가?
2. ChannelName 하나가 process-local 송신 경로를 유일하게 선택하고 MeshName이나 endpoint를 호출자에게
   다시 요구하지 않는가? 충돌과 target 없음·연결 미준비·protocol 오류가 즉시 구분되는가?
3. RouteMesh `Channel(name).Client()/Server()`와 ClientServer `Client()/Server()`가 같은 역할 문법을
   사용하면서도 물리 topology, descriptor, runtime과 location record를 섞지 않는가?
4. Server만 handler·weight를 제공하고 ClientServer server가 역방향 업무 요청을 시작하지 못하는가?
   `SetWeight(0)`이 client 역할을 대신하거나 방향 제한을 우회하지 않는가?
5. 다른 RouteMesh 또는 ClientServer egress에서 온 reply가 원래 request를 정확히 한 번 완료하며 Spot의
   async/yield serial turn, timeout, cancellation, disconnect와 shutdown 경쟁 계약을 보존하는가?
6. BindHost, AdvertiseHost, port 0, wildcard와 actual bound endpoint가 RouteMesh, ClientServer, classic
   fanout과 STREAM에서 같은 원칙을 사용하면서 topology별 descriptor를 올바르게 분리하는가?
7. .NET, C++, Java, Kotlin, Node.js exact interface가 같은 공개 동작을 각 언어의 자연스러운 signature로
   완전하게 표현하며 handler context, runtime snapshot/event와 오류 타입이 공통 계약과 일치하는가?
8. Config 12의 `CH-E2E-01~10`, `CH-REG-01~09`, role fixture와 일곱 공통 sample topology가 정식 계약을
   빠짐없이 검증하며 중복 물리 연결, relay, timeout 증가나 구현 전용 helper를 정상 동작으로 허용하지
   않는가?
9. Unified/.NET inventory와 verifier가 위 계약, exact code fixture, declaration, transition owner, Config 12
   scenario ID와 두 JSON fixture를 실제로 고정하며 잘못된 계약도 통과시키는 빈 검증이 없는가?
10. 정식 spec에는 현재 10.0.0 계약만 있고 설계 이력·구현 진행 상태가 섞이지 않으며, gap 문서와 E2E·sample이
    정식 계약의 owner를 침범하지 않는가?

## 판정 경계

- 공개 계약의 근거는 scope 안의 Core 정식 spec, framework 공통·server spec과 언어별 exact interface다.
- `90-implementation-gap.ko.md`는 현재 구현 차이만 소유한다. 현재 source가 미구현이라는 이유로 target
  계약을 이전 표면으로 되돌리거나 축소하지 마라.
- Config 12와 sample은 검증·사용 예시이며 새 public API를 독자적으로 정할 수 없다.
- Draft, 실행 ledger와 구현 source는 이번 snapshot의 계약 근거가 아니다. 확인이 필요하면 finding에서
  부족한 정식 owner와 필요한 검증을 구체적으로 지적하라.
- Core relay·bridge, MeshNode descriptor 재사용, MeshName 재노출, weight 0 client 표현과 언어별 private
  helper를 해결책으로 제안하지 마라.

## 출력 계약

먼저 시작 hash 확인 결과와 57개 전체를 읽었다는 사실을 기록한다. Finding은 심각도
`blocker/high/medium/low` 중 하나를 사용하고 다음 형식으로 쓴다.

```text
[원칙][severity] file:line — 문제 — 근거 — 제안
[계약][severity] file:line — 문제 — 근거 — 제안
```

같은 원인의 여러 위치는 하나의 finding에 대표 위치와 영향 목록을 함께 적는다. 모호한 선호나 구현 방식
취향은 finding으로 만들지 말고 공개 의미, 정확한 signature, 책임 owner 또는 검증 가능성의 위반을 근거로
판정한다.

마지막에 종료 hash 확인 결과를 기록한다. Finding이 하나도 없고 시작·종료 snapshot이 모두 같을 때만
마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다. 그 밖에는 마지막 줄을 `DOC REVIEW NOT CLEAN`으로 쓴다.
