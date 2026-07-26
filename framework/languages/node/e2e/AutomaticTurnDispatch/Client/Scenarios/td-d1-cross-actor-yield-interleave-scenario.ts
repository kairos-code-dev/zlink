// TD-D1: SpotWide Actor가 yield하면 다른 Actor와 Spot callback은 처리된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD1();
