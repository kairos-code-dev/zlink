# C++ DiscoveryRegistryHa E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

이 문서는 C++ `DiscoveryRegistryHa` config의 현재 구현 상태를 기록한다. public query 또는 framework
기능이 부족한 항목은 E2E client나 runner에서 raw frame, private helper, test-only adapter로 우회하지
않는다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| DR-A1 | 구현 | registry, provider 2개, consumer, HTTP-only client driver가 single-registry baseline request와 provider evidence를 검증한다. |
| DR-A2 | 구현 | registry 2개를 서로 peer로 연결하고, provider는 reg-1에만 붙이며 consumer는 reg-2에만 붙인다. client는 reg-2의 public `MemberPeers` query와 consumer request/provider evidence로 peer 합산 view와 messaging을 검증한다. |
| DR-A3 | 구현 | registry 3개를 서로 peer로 연결하고, provider A는 reg-1에만, provider B는 reg-3에만 광고한다. client는 세 registry의 public `ConnectedPeerRegistryCount`와 `MemberPeers` query를 확인한 뒤 각 registry만 보는 consumer request/provider evidence를 검증한다. |
| DR-A4 | 구현 | 같은 rid `api-a`를 서로 다른 endpoint로 reg-1/reg-2에 광고하고, reg-2만 보는 consumer가 bounded wait 안에 살아 있는 `api-a` provider로 request/evidence를 완료하는지 검증한다. |
| DR-B1 | 구현 | reg-1을 먼저 띄운 뒤 reg-2/reg-3을 late-start로 합류시키고, 두 late registry가 api-a/api-b endpoint를 관측한 뒤 각 consumer request/provider evidence를 완료하는지 검증한다. |
| DR-B2 | 구현 | reg-1/reg-2 endpoint를 함께 가진 consumer에서 reg-2를 중지한 뒤, public channel request timeout 안에서 죽은 registry router를 건너뛰고 살아 있는 reg-1/api-a endpoint로 request/evidence를 완료하는지 검증한다. |
| DR-B3 | 구현 | reg-2를 중지/재시작한 뒤 재시작 registry와 survivor registry가 provider member를 관측하고 consumer request/provider evidence를 완료하는지 검증한다. 최신 통과 로그: `logs/20260702-085923-98682`. |
| DR-C1 | 구현 | reg-2를 중지한 상태에서 reg-1 기준 consumer request/provider evidence가 성공하고, 중지된 reg-2 `/health`가 bounded timeout 안에 실패하는지 검증한다. 최신 통과 로그: `logs/20260702-085945-99479`. |
| DR-C2 | 구현 | 중지했던 reg-2를 재시작한 뒤 reg-2가 provider member를 다시 관측하고 reg-2 consumer request/provider evidence를 완료하는지 검증한다. 최신 통과 로그: `logs/20260702-090002-465`. |
| DR-C3 | 구현 | full registry outage 전 request로 확인된 provider endpoint를 framework runtime이 보존하고, registry 전체 중지 뒤에도 established channel request/provider evidence가 완료되는지 검증한다. 최신 통과 로그: `logs/20260702-092546-36949`. |
| DR-D1 | 구현 | embedded registry+provider 단일 프로세스 role과 별도 consumer가 embedded provider request/evidence를 완료하는지 검증한다. 최신 통과 로그: `logs/20260702-091123-11905`. |
| DR-D2 | 구현 | standalone registry 배포에서 provider member wait, consumer request, provider evidence를 검증한다. 최신 통과 로그: `logs/20260702-090024-1357`. |
| DR-D3 | 구현 | embedded registry/provider와 standalone registry/provider가 peering된 혼합 배포에서 merged member view와 request/evidence를 검증한다. 최신 통과 로그: `logs/20260702-091135-12373`. |
| DR-D4 | 구현 | registry app의 in-process topology snapshot과 별도 probe app의 remote topology snapshot이 같은 router endpoint에서 일치하는지 검증한다. 최신 통과 로그: `logs/20260702-091147-13042`. |
