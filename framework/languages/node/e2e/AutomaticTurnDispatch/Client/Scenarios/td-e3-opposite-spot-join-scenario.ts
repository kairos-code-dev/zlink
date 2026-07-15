// TD-E3: 반대 방향 join 두 개가 동시에 진행된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdE3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdE3();
