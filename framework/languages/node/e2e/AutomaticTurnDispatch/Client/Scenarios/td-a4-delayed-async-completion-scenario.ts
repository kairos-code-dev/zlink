// TD-A4: async 대기가 완료를 막지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA4 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA4();
