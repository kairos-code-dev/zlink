[English](socket-option-defaults.md)

# Socket option 기본값

`options_t`는 공통 raw-socket과 transport 기본값을 저장한다. Typed socket 구현은 pattern별 option을
검증한 뒤 적용한다.

## Queue 계획

Automatic HWM은 raw socket role, profile, effective message size와 관찰한 connection 수로 policy를
선택한다. 계획 결과는 제한된 send/receive HWM과 필요한 kernel-buffer 값이다. Hysteresis는 connection
수 경계에서 bucket이 빠르게 반복 전환되는 것을 막는다.

## Application에 보이는 상태

`zlink_monitor_status()`는 적용된 계획, 입력값, 선택한 bucket과 deferred shrink target을 제공한다. 이
field는 진단 snapshot이다. Application은 내부 값을 바꾸지 않고 public option으로 policy 입력을 설정한다.

## Transport 기본값

Reconnect, TCP keepalive, kernel buffer, TOS, handshake interval과 TLS field는 해당 transport가
적용한다. 지원하지 않는 조합은 typed configuration result로 실패한다.
