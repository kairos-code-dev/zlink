# Discovery / Registry 일반 소켓 서비스 자동 연결 문서

이 디렉토리는 `core`의 discovery / registry를 확장해
raw `ROUTER`, `DEALER`, `PUB`, `SUB`도
서비스 단위로 위치투명하게 자동 연결하도록 만드는
상세 스펙과 설계 문서를 담는다.

현재 상태:

- 실행 가이드는 종료 상태까지 정리되어 있다.
- 최종 종료 판정은
  [`logs/codex_execution_guide_loop_20260325_155234/03_last_message.txt`](./logs/codex_execution_guide_loop_20260325_155234/03_last_message.txt)
  의 `미적용 사항이 없습니다.`다.
- 최종 반영 commit은 `89046e93 refactor: finish discovery-owned service execution`이다.

문서:

- [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)
  raw socket 서비스 자동 연결의 요구사항, 공개 surface,
  registry / discovery 프로토콜 확장, 런타임 설계,
  테스트 및 단계별 구현 범위를 정의한다.
- [`discovery-service-execution-guide.ko.md`](./discovery-service-execution-guide.ko.md)
  메인 스펙을 구현 순서와 종료 판정 기준으로 고정한 실행 문서다.
  단계별 체크리스트, commit / push 규칙, 스펙 섹션 링크를 포함한다.
- [`run_discovery_service_execution.sh`](./run_discovery_service_execution.sh)
  discovery 작업 전용 Codex 실행 래퍼다.
  내부적으로 [`core/tools/run_codex_execution_guide_loop.sh`](../../../core/tools/run_codex_execution_guide_loop.sh)
  를 공통 supervisor로 호출하고, guide / spec / logs 경로를 이 디렉토리 기준으로 고정한다.

권장 사용 순서:

1. 메인 설계와 정책은 [`raw-socket-service-discovery-spec.ko.md`](./raw-socket-service-discovery-spec.ko.md)에서 확인한다.
2. 실제 구현 순서와 완료 판정은 [`discovery-service-execution-guide.ko.md`](./discovery-service-execution-guide.ko.md)에서 확인한다.
3. 랄프 루프 자동 실행이 필요하면 [`run_discovery_service_execution.sh`](./run_discovery_service_execution.sh)를 사용한다.
