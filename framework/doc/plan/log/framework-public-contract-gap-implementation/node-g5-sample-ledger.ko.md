# Node.js G5 공통 샘플 대조 기록

## 1. 기준과 범위

Node.js 샘플의 업무 흐름 기준은
[`framework/doc/framework/common/sample/`](../../../framework/common/sample/README.ko.md)뿐이다.
언어별 샘플 문서를 계약 근거로 사용하지 않는다. 이전 언어별 `README.ko.md`와
`sample-porting-inventory.ko.md`는 공통 스펙과 다른 내용을 다시 만들 수 있으므로 삭제한다.
Node.js 회귀 검사는 이 파일들이 다시 추가되지 않고 공통 sample 문서가 존재하는지 확인한다.

현재 기록은 재검토 중이다. 이전 PASS 판정 뒤 공통 스펙과 다른 메시지, 상태 전이와 검증 경로를
추가로 발견했으므로 완료 판정을 철회했다. 각 샘플의 서버 역할, 메시지 이름과 필드, 상태 전이,
codec, client self-check 순서와 완료 기준을 다시 대조하고 있으며, 수정과 독립 재검토가 모두
끝난 뒤에만 완료로 바꾼다.

## 2. 대조 상태

| 공통 스펙 | Node.js 구현 | 정적 대조 | 실행 결과 | 현재 상태 |
|-----------|--------------|-----------|-----------|-----------|
| Bingo | `samples/Bingo.Ts` | 1차 대조 완료 | shell·PowerShell PASS | Protobuf 생성 타입, flow 연속성, 실제 metric, drain handoff, no-self-join, 세 client별 inbound marker를 runner가 검증한다. 최종 독립 재검토가 남아 있다. |
| TicTacToe | `samples/TicTacToe.Ts` | 완료 | PASS | 여섯 샘플 중 유일하게 수동 서버 연결을 사용함 |
| SupportChat | `samples/SupportChat.Ts` | 완료 | shell·PowerShell 진입 PASS | 세 서버 역할, location store 자동 연결, conversation Spot 소유, 재연결 시 기존 actor 우선 조회, idle·grace timer, bound push와 음성 시나리오를 검증함 |
| DeliveryDispatch | `samples/DeliveryDispatch.Ts` | 완료 | shell·PowerShell 진입 PASS | 정본에 없는 CourierGateway를 제거했다. `delivery-couriers` route mesh와 Spot mesh의 자동 연결, 기존 actor 우선 조회, session 재연결 bind, timeout 재배정, 정확한 상태 순서와 Tracking의 고객 actor 단방향 전송을 검증했다. |
| ShoppingMall | `samples/ShoppingMall.Ts` | 완료 | shell·PowerShell PASS | expected-version, 단계별 보상, 이중 쓰기 복구와 wire 계약을 수정한 뒤 독립 재검토에서 추가 gap이 없음을 확인함 |
| GameQuest | `samples/GameQuest.Ts` | 완료 | shell 3회·PowerShell PASS | 다중·순서 quest 조건, aggregate/read model 분리와 reconcile event 순서를 수정한 뒤 독립 재검토에서 추가 gap이 없음을 확인함 |

## 3. Redis 격리와 자동 연결 원칙

각 sample과 E2E 실행은 다른 실행과 간섭하지 않는 전용 Redis container와 실행별 key prefix를
사용한다. Redis의 `/data`는 `tmpfs`로 구성하여 Docker volume을 만들지 않는다. 성공·실패 종료에서
모두 `docker rm -f -v`와 같은 동작으로 container를 정리하며 host Redis로 자동 전환하지 않는다.

서버 사이 연결은 location store의 peer discovery를 사용하는 자동 연결이 기본이다. TicTacToe만
수동 연결 동작을 보여 주므로 명시적인 peer endpoint를 사용한다. client가 stream server에 연결하는
동작은 이 서버 간 자동 연결 규칙의 대상이 아니다.

## 4. 재검증 조건

수정 뒤 각 sample의 shell·PowerShell runner, 전체 sample 회귀 검사와 6종 통합 runner를 다시
실행한다. 이어서 구현에 참여하지 않은 Codex 에이전트가 공통 스펙과 구현을 대조한다. finding이
하나라도 있으면 해당 sample을 다시 열고, 수정과 재검증을 반복한다. 모든 sample reviewer가 차이가
없다고 판정한 뒤에만 이 ledger와 상위 계획의 G5를 완료로 표시한다.

Bingo는 2026-07-13에 bindings 9.0.4 local npm package를 깨끗한 consumer에서 설치한 뒤
shell runner를 실행했다. runner는 실제 Protobuf 생성 타입을 사용한 request·reply·notify,
세 client의 `stream-inbound` marker, timer에서 이어진 flow id, framework metric,
`DrainNatural` handoff와 참가자별 leave/destroy 횟수를 확인하고 통과했다. package 소비 단계에서
prebuild가 있어도 npm이 `node-gyp` source rebuild를 시작하던 bindings package 결함도 함께
수정했으며, local package 생성 스크립트가 임시 consumer의 install과 `require`까지 검증한다.
PowerShell 진입 runner도 같은 날 통과했다. 공통 문서 전체에 대한 마지막 독립 대조가 끝나기
전에는 Node.js G5를 완료로 표시하지 않는다.

DeliveryDispatch는 공통 문서의 서버 역할과 연결 구조를 다시 대조했다. shell runner와 PowerShell
진입 runner에서 `topology=ready`, 재배정 완료, 서버 근거 확인과 최종 완료 marker를 확인했다.
두 실행 모두 Redis `/data`를 `tmpfs`로 사용했고 실행 전후 Docker volume 수의 차이는 0이며,
종료 뒤 sample label을 가진 container도 남지 않았다. route mesh와 Spot mesh 이름이 같은 경우에도
actor/session 응답이 Spot mesh를 사용하도록 framework 전송 선택을 수정하고 해당 회귀 검사를
추가했다.

SupportChat은 API, Session, Support 세 서버만 사용하며, client는 Session의 stream endpoint에만
연결한다. 서버 간 channel과 Spot 연결은 location store가 자동으로 설정한다. 재인증과 상담원별
대화 actor 재참가에서는 location store에서 기존 actor를 먼저 조회하고, 조회 결과가 없을 때만
Support 서버에 생성을 요청한다. framework가 제공하는 `ZLinkActorRefSnapshot`을 wire 모델로
사용하며 샘플 안에 같은 구조를 다시 정의하지 않는다.

client self-check는 인증 전 요청, 역할에 맞지 않는 요청, 대화 비참여자 요청, 종료된 대화 요청과
typing 무시 조건을 포함한다. 상담원 세션 재연결 뒤 세 대화방 재참가, 명시적 종료, idle 이후 재개,
grace timeout 종료, 정원 회복과 상담원 부재 대기도 실제 stream 알림으로 확인한다. shell runner와
PowerShell 진입 runner가 모두 `supportchat=completed`,
`supportchat-closed-typing-ignore=verified`, `PASS SupportChat.Ts`를 출력했다. 두 실행 모두 전용
Redis의 `/data`를 `tmpfs`로 사용했으며 실행 전후 Docker volume 수의 차이는 0이고, 종료 뒤
SupportChat Redis container도 남지 않았다.

GameQuest는 client 연결을 받는 GameApi 두 프로세스와 `PlayerQuestSpot` owner를 호스팅하는
QuestMission 두 프로세스를 사용한다. 네 프로세스의 route mesh와 Spot mesh는 location store로
자동 연결하며, 코드에는 서버 간 수동 `connect` 호출이 없다. client는 stream 연결 하나에서 join,
gameplay action, 진행·완료 알림과 조회를 처리한다. self-check는 잘못된 area, 두 player의 서로 다른
owner 처리, EventId 중복 억제, reward 결정 중복 방지, projection 삭제·event replay 재생성,
owner 비활성 뒤 재생성, 다른 Session Server 재연결, gameplay state reset 보정을 검증한다.

재연결 뒤 owner가 아닌 서버에서 보낸 actor 알림이 현재 session target을 덮어쓰던 framework 결함도
함께 수정했다. native actor packet의 `NO_BIND` 표시는 session binding 근거로 사용하지 않고,
명시적인 session-origin packet과 bound-session bind control만 현재 session target을 갱신한다.
dispatch event 사이에 나뉘어 들어온 multipart actor packet은 final part까지 보존하여 잘못된 frame으로
분리되지 않게 했다. 관련 contract·native integration 회귀와 GameQuest shell·PowerShell runner를
각각 3회 연속 실행해 모두 통과했다. 마지막 실행 뒤 Docker volume 수는 0이고 GameQuest Redis
container 수도 0이었다.
