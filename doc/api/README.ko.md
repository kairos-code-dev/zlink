[English](README.md) | [한국어](README.ko.md)

# zlink API 레퍼런스

zlink C 라이브러리는 경량 I/O 스레드와 락프리 큐를 기반으로 구축된 메시징 및
서비스 디스커버리 툴킷을 제공합니다. 이 레퍼런스는 `<zlink.h>`에서 제공하는
모든 공개 함수, 타입, 상수를 다룹니다.

## API 그룹

| 그룹 | 파일 | 설명 | 함수 수 |
|------|------|------|---------|
| 에러 처리 & 버전 | [errors.ko.md](errors.ko.md) | 에러 코드, 에러 문자열, 버전 조회 | 3 |
| Context | [context.ko.md](context.ko.md) | Context 생성, 종료, 옵션 설정 | 5 |
| Message | [message.ko.md](message.ko.md) | 메시지 생명주기, 데이터 접근, 속성 | 16 |
| Socket | [socket.ko.md](socket.ko.md) | 소켓 생성, 옵션, bind/connect, 송수신 | 13 |
| Monitoring | [monitoring.ko.md](monitoring.ko.md) | 소켓 모니터, 이벤트, 피어 검사 | 7 |
| Registry | [registry.ko.md](registry.ko.md) | 서비스 레지스트리 생성, 구성, 클러스터링 | 9 |
| Discovery | [discovery.ko.md](discovery.ko.md) | 서비스 디스커버리, 구독, 리시버 조회 | 9 |
| Gateway | [gateway.ko.md](gateway.ko.md) | 로드밸런싱 요청/응답 게이트웨이 | 10 |
| Receiver | [receiver.ko.md](receiver.ko.md) | 서버 측 요청 리시버 및 서비스 등록 | 11 |
| SPOT | [spot.ko.md](spot.ko.md) | 토픽 기반 PUB/SUB 노드, 퍼블리셔, 서브스크라이버 | 27 |
| Polling | [polling.ko.md](polling.ko.md) | I/O 다중화 및 프록시 헬퍼 | 4 |
| Utilities | [utilities.ko.md](utilities.ko.md) | 타이머, 스레드, 스톱워치, 아토믹, 기능 조회 | ~20 |

## 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_msg_t`](message.ko.md) | message.ko.md | 불투명 메시지 컨테이너 (64바이트, 스택 할당 가능) |
| [`zlink_routing_id_t`](message.ko.md) | message.ko.md | 피어 라우팅 아이덴티티 (1바이트 크기 + 255바이트 데이터) |
| [`zlink_monitor_event_t`](monitoring.ko.md) | monitoring.ko.md | 모니터 이벤트 구조체 (이벤트, 값, 주소) |
| [`zlink_peer_info_t`](monitoring.ko.md) | monitoring.ko.md | 연결된 피어 통계 (라우팅 아이디, 주소, 카운터) |
| [`zlink_receiver_info_t`](discovery.ko.md) | discovery.ko.md | 디스커버리된 서비스 리시버 항목 (이름, 엔드포인트, 가중치) |
| [`zlink_pollitem_t`](polling.ko.md) | polling.ko.md | I/O 다중화를 위한 폴 아이템 (소켓 또는 fd) |

## 콜백 타입

| 타입 | 정의 위치 | 설명 |
|------|-----------|------|
| [`zlink_free_fn`](message.ko.md) | message.ko.md | 제로카피 메시지를 위한 해제 콜백 |
| [`zlink_timer_fn`](utilities.ko.md) | utilities.ko.md | 타이머 만료 콜백 |
| [`zlink_thread_fn`](utilities.ko.md) | utilities.ko.md | 스레드 진입점 함수 |
| [`zlink_spot_sub_handler_fn`](spot.ko.md) | spot.ko.md | SPOT 서브스크라이버 메시지 디스패치 콜백 |

---

개념 가이드와 튜토리얼은 [사용자 가이드](../guide/01-overview.ko.md)를 참조하세요.
