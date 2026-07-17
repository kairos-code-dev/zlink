# S5 iteration 3 finding ledger

R1: 해소 9/11 + 잔여 재판정 2(high) + 신규 2(high 1, low 1).
R2: 해소 11/11(N1 데드락 소멸 재현 3/3 실증) + 신규 2(medium 1, low 1).
중복: R1 N3-I3-01 = R2 N6. 유효 처리 대상 5건 + 해석 상충 1건.
snapshot: `25617130eee`.

| ID | 출처 | 축 | 심각도 | 요지 | 상태 |
|---|---|---|---|---|---|
| F-I1-01(재재) | R1 | I1 | high | post-commit snapshot 차감이 "한 번 만든 snapshot의 all-or-none"과 충돌한다는 재판정 (R2는 해소 판정 — 상충) | **rejected(코디네이터 판정)** — 아래 근거 |
| F-I1-03(재재) | R1 | I1 | high | bind가 actor 검증→binding 삽입 사이 창에서 destroy의 제거 pass 이후 stale binding 등록 가능 | fixed: 삽입 후 node lock 재검증(부재/세대 불일치/draining → 삽입 롤백+ESTALE) |
| N3-I1-01 | R1 | I1 | high | remote commit 후 local record 할당·복사 실패 시 partial | fixed: 모든 fallible 준비(local record 선구축)를 첫 commit 이전으로 이동 — commit 이후는 무실패 move만 |
| N5 | R2 | I1 | medium | destroy drain의 lock 해제 창 뒤 무효 `owner_it` 사용(UAF write) | fixed: 재획득 직후 `owner_it` 재조회 |
| N6/N3-I3-01 | R1+R2 | I3 | low | 대상 잃은 UTF-8 주석 잔존 | fixed: 주석 제거 |

## F-I1-01 rejected 판정 근거 (coordinator)

정식 spec `service/01-mesh-node.ko.md` §5는 "peer drain은 새 snapshot에서
제외하지만 **이미 commit한 message를 취소하지 않는다**"고 명시한다. reserve가
성공한 뒤 commit 도중 pipe가 소실되는 경우는 그 peer의 단절이며, TCP 위에서
이미 발송된 프레임의 회수는 어떤 설계로도 불가능하다 — spec 스스로 이 비대칭을
인정한다. 따라서 all-or-none은 **admission(capacity) 차원의 계약**이고, 구현은
이를 reserve 단계에서 보장한다(용량 기인 partial 불가능 — probe 1 slot=전체
multipart admission). 단절 기인 미수신 target을 snapshot에서 제외(사후 이탈로
보고)하는 회계는 "성공 event의 dropped=0" 계약과 §5의 취소-불가 조항을 함께
만족하는 유일한 정합 해석이다. R2도 같은 해석으로 해소 판정했다. §2.4에 따라
이 rejected는 iteration 4에서 두 리뷰어의 재검토를 받는다.

검증: 전체 85/85, ASAN 5바이너리 clean.
