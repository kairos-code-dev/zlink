// TD-A2: async 대기 중 같은 Spot의 다른 callback이 시작하지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA2();
