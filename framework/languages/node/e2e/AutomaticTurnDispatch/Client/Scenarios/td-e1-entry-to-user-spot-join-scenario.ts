// TD-E1: Entry Spot에서 user Spot으로 Join을 defer한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdE1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdE1();
