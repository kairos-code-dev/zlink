// TD-F5: 대기 중 cancellation과 shutdown 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF5 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF5();
