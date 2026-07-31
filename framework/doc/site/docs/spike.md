# 스파이크 — 탭·스니펫 확인

이 페이지는 [공통화 런북](https://github.com/zlink-systems/zlink/blob/main/framework/doc/plan/language-guide-port-runbook.ko.md)
Phase 0의 확인용이다. 세 가지를 본다 — 스니펫 경로가 repo 루트 기준으로 풀리는가, 탭이
언어별로 전환되는가, 마커 구간이 의도한 크기로 잘리는가.

## 산문은 공통

아래 문단이 공통 정본에 해당한다. 특정 언어의 타입 이름을 부르지 않는다.

> User Spot을 새로 만들 때는 **stable type과 최초 설정만** 넘긴다. 어느 node에 배치할지는
> 호출하는 쪽이 지정하지 않는다. Framework가 그 stable type을 등록한 node 중에서 고르고,
> 전역에서 유일한 Spot ID를 발급한다. 생성 callback이 최초 설정을 검사해 수락 여부를
> 결정하며, 거절하면 쓸 수 있는 Spot은 만들어지지 않는다.

## 코드는 탭으로

=== "C#/.NET"

    ```csharp
    --8<-- "framework/languages/dotnet/samples/TicTacToe/Server/Api/Handlers/CreateGameHttpHandler.cs:doc-create"
    ```

=== "C++"

    ```cpp
    --8<-- "framework/languages/cpp/samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp:doc-create"
    ```

=== "Java"

    Java 탭은 Phase 2에서 채운다.

=== "Kotlin"

    Kotlin 탭은 Phase 2에서 채운다.

=== "Node/TypeScript"

    Node 탭은 Phase 2에서 채운다.

## 확인 항목

| 항목 | 무엇을 보나 |
| --- | --- |
| 스니펫 경로 | 두 탭의 코드가 실제 샘플 파일 내용으로 채워졌는가 |
| 마커 구간 | 생성 호출 사슬만 잘렸는가. 앞뒤 logger 호출이 섞이지 않았는가 |
| 주석 노출 | 샘플에 심은 주석이 그대로 보이는가 |
| 탭 전환 | 라벨을 눌러 언어가 바뀌는가. 다른 페이지에서도 선택이 유지되는가 |
