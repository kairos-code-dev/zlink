// TD-F4: 대기 중 timeout은 turn을 영구 점유하지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF4 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF4();
