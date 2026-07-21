# RouteMesh 10.0.0 Channel·fanout·fixed drain 독립 문서 리뷰 — iteration 11

S3-CH·S3-FO·S3-DP iteration 11 snapshot을 독립적으로 검토하라. 다른 reviewer 결과를 읽지 말고 파일을
수정하지 마라. `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
`doc/principal/software-design-principles.md`, iteration 11 manifest와 scope 96개 전체를 읽는다. 일부만
읽고 clean으로 판정하지 마라.

시작과 종료 시 manifest의 hash와 verifier 명령을 실행한다. 파일 집합은
`00d6cbefc6d8d0470d42efc672a93ddf8da6776886ead7adcdfa202ad92dfa46`, 목록은
`7d428591a096c04333984c8dc75f75364fcb40f106546327f71d3a6e8de18390`이어야 한다.

다음을 중점 확인한다.

1. ChannelName만 업무 송신 경로를 선택하고 MeshName과 endpoint를 호출 인자로 요구하지 않는가?
2. 일곱 sample topology와 manual peer initiator가 canonical fixture와 일치하는가?
3. Classic fanout이 ChannelName과 typed event만 받고 transport topic을 노출하지 않는가?
4. Automatic publisher descriptor, lease, generation/revision, actual endpoint와 native readiness가 일관되는가?
5. Observer bounded lifecycle, cancellation·close와 Node provider null 가능성이 다섯 exact interface와
   일치하는가?
6. Location metric이 MeshName이 없는 ClientServer·fanout descriptor를 허위 label 없이 표현하는가?
7. Config 3, feature-map, inventory와 verifier가 공개 계약과 실제 scenario 행을 구조적으로 고정하는가?
8. Spot create/GetOrCreate가 local-only이고 remote resolve·send/request와 명확히 구분되는가?
9. MeshNode fixed drain 순서가 admission seal → accepted work → Actor handoff → STREAM barrier → local Spot
   close → owner cleanup이며 normal request completion만으로 Spot을 닫지 않는가?
10. Row 제거 뒤 stale SpotHandle이 hidden remote GetOrCreate를 시작하지 않고 explicit local GetOrCreate만
    새 generation을 만드는가?
11. Policy enum·builder option과 자동 재생성 주장이 formal spec, exact interface, guide, sample, Config 11과
    다섯 ObservabilityOps feature map에서 제거됐는가?
12. `.NET` guide가 current filter·metadata·STREAM·Spot 표면과 MeshName별 drain runtime을 exact interface와
    동일하게 설명하는가?
13. Fixed drain terminal reason이 exact interface와 regression matrix에서 같은 닫힌 snake_case 값인가?
14. Consolidation plan이 fixed drain의 Actor·STREAM·Spot cleanup 순서를 뒤집지 않는가?
15. 정식 spec에는 10.0.0 목표만 있고 현재 구현 차이는 gap·feature map과 ledger에만 있는가?
16. `90-implementation-gap.ko.md`가 날짜별 기준선, 완료 이력이나 review 회차를 포함하지 않고 현재 열린
    목표와 구현 차이만 기록하는가?

Finding은 `[원칙][severity] file:line — 문제 — 근거 — 제안` 또는
`[계약][severity] file:line — 문제 — 근거 — 제안` 형식으로 기록한다. Finding이 없고 모든 필수 gate가
끝났을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다.
