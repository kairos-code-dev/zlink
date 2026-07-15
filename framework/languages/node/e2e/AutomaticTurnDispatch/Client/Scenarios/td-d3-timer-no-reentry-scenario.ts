// TD-D3: 같은 timer는 yield를 가로질러도 다음 tick으로 재진입하지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD3();
