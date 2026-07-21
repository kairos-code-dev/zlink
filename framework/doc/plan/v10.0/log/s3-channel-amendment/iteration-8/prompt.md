# RouteMesh 10.0.0 Channel·fanout amendment 독립 문서 리뷰 — iteration 8

S3-CH·S3-FO iteration 8 snapshot을 독립적으로 검토하라. 다른 reviewer 결과를 읽지 말고 파일을 수정하지
마라. `AGENTS.md`, `doc/principal/documentation/documentation-principles.ko.md`,
`doc/principal/software-design-principles.md`, iteration 8 manifest와 scope 71개 전체를 읽는다.

시작과 종료 시 manifest의 세 hash 명령을 실행한다. 파일 집합은
`9417ae73c562ecaf6cccc423cde6b98fbd2a7b5915b21e51f722455c23588d9f`, 목록은
`00bba02f27b40d650b02d659bcd613d4e69097220565dd42419fa2a2937f7818`이어야 한다.

다음을 중점 확인한다.

1. ChannelName만 업무 송신 경로를 선택하고 MeshName과 endpoint를 호출 인자로 요구하지 않는가?
2. 일곱 sample topology와 manual peer initiator가 canonical fixture와 일치하는가?
3. Classic fanout이 ChannelName과 typed event만 받고 transport topic을 노출하지 않는가?
4. Automatic publisher descriptor, lease, generation/revision, actual endpoint와 native readiness가 일관되는가?
5. Observer bounded lifecycle, cancellation·close와 Node provider null 가능성이 다섯 exact interface와 일치하는가?
6. Location metric이 MeshName이 없는 ClientServer·fanout descriptor를 허위 label 없이 표현하는가?
7. Config 3·feature map·inventory·verifier가 공개 계약과 1,000개 scenario row를 구조적으로 고정하는가?
8. 정식 spec에는 10.0.0 목표만 있고 현재 구현 차이는 gap·feature map에만 있는가?

Finding은 `[원칙][severity] file:line — 문제 — 근거 — 제안` 또는
`[계약][severity] file:line — 문제 — 근거 — 제안` 형식으로 기록한다. Finding이 없고 hash가 같을 때만
마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다.
