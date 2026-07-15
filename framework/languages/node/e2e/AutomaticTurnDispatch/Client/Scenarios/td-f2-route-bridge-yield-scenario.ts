// TD-F2: route bridge 경유 handler에서도 같다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF2();
