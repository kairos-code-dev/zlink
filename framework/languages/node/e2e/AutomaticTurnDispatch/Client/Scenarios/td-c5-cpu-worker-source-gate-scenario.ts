// TD-C5: CPU worker에서 blocking I/O를 하지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdC5 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdC5();
