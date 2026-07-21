# RouteMesh 10.0.0 amendment 통합 독립 문서 review — iteration 15

S3-CH·S3-FO·S3-DP·S3-SA·S3-IS iteration 15 snapshot을 독립적으로 검토하라. 다른 reviewer 결과를 읽지
말고 파일을 수정하지 마라. `AGENTS.md`, 문서 작성 원칙, POSD 원칙, iteration 15 manifest와 scope 113개
전체를 읽는다. 일부만 읽고 clean으로 판정하지 마라.

시작과 종료 시 manifest의 hash와 verifier 명령을 실행한다. 파일 집합은
`a50e5de86d1cc3da63e5914265b1e3a2417f0eebd615b53472d40c5b9f10e149`, 목록은
`0ad6551a53b4c0685591dbaead6a40556dfe83622dd96c6d4212276688600fb3`여야 한다.

다음을 중점 확인한다.

1. ChannelName만 업무 송신 경로를 선택하고 MeshName과 endpoint를 호출 인자로 요구하지 않는가?
2. RouteMesh client/server 역할, ClientServer 방향과 일곱 sample topology가 공통 fixture와 일치하는가?
3. Classic fanout subscriber가 같은 ChannelName의 publisher endpoint만 자동 발견하며 manual mode와 섞이지 않는가?
4. Fanout descriptor의 role, lease, generation·revision, actual endpoint와 native readiness가 모든 언어에서 같은가?
5. MeshNode fixed drain 순서가 admission seal, accepted work, Actor handoff, STREAM barrier, local Spot cleanup,
   owner resource 해제 순서이며 hidden remote creation을 보장하지 않는가?
6. 제거한 drain policy enum·builder option과 자동 재생성 주장이 정식 계약·exact interface·E2E·sample에 없는가?
7. One-way public call이 비동기 submit 함수 하나만 제공하고 local admission까지만 기다리며 remote handler
   완료를 기다리지 않는가?
8. Instance address는 application에 node RID, generation, owner token과 Store fencing 값을 노출하지 않는가?
9. Instance Core driver는 cold placement만 담당하고 Ready owner는 기존 exact Spot direct API를 재사용하는가?
10. Placement와 activation data에 `struct_size`, `version`, MeshName, Spot generation, owner ID, location generation과
    activation epoch가 남아 있지 않은가? Claim result도 version field 없이 고정 layout이며, owner claim 뒤 확정된
    `leader_spot_generation`만 exact route 구성 결과로 제공하는가?
11. `claim_owner`와 `mark_ready`, exact route redirect, handle 기반 renew·close가 token 소비와 barrier 순서를
    잘못된 조합 없이 표현하는가?
12. Location generation과 activation epoch는 Store CAS에만 쓰이고 Core message·timer fencing은 monotonic owner
    deadline으로 닫히는가?
13. Instance lifecycle이 actor-free 네 지점이며 빈 create message, Actor handler와 Logical Multicast 등록을
    허용하지 않는가?
14. Factory 기본값은 4096개와 3초이며 `0` sentinel이나 C++ 영 초기값과 충돌하지 않는가?
15. Store snapshot·fence·닫힌 result가 금지 상태를 표현하지 않고 Instance capability를 사용하지 않는 custom
    store에 operation 구현을 강제하지 않는가?
16. Config 12·13·14, sample, Redis fixture, contract inventory와 verifier가 정식 계약의 필수·금지 표면을
    구조적으로 검사하는가? 특히 Instance inline canonical JSON과 세 state fixture의 field schema가 같은가?
17. 다섯 언어의 exact interface가 같은 public capability를 제공하고 언어 특성 외의 기능 차이를 만들지 않는가?
18. 정식 spec에는 목표 계약만 있고 현재 구현 차이와 진행 상태는 gap·feature map·ledger에만 있는가?
19. API가 내부 placement, queue, retry, encoding과 token 수명 조합을 application caller에게 떠넘기지 않는가?
20. 문서의 독자·범위·산문·코드 주석·다이어그램이 저장소 문서 원칙과 POSD 원칙을 지키는가?

Finding은 `[원칙][severity] file:line — 문제 — 근거 — 제안` 또는
`[계약][severity] file:line — 문제 — 근거 — 제안` 형식으로 기록한다. Finding이 없고 모든 필수 gate가
끝났을 때만 마지막 줄을 정확히 `DOC REVIEW CLEAN`으로 쓴다.
