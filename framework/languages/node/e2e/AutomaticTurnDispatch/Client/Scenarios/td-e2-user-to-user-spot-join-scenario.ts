// TD-E2: user Spot에서 다른 user Spot으로 join한다 (async) 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdE2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdE2();
