// TD-A3: async를 가로지르는 spot 상태 불변식이 유지된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA3();
