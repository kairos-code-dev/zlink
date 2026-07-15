// TD-D2: 같은 actor는 yield를 가로질러도 재진입하지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD2();
