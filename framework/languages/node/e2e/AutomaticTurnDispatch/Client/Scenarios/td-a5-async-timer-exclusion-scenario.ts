// TD-A5: async 대기 중 timer는 지연된다 (의도된 결과) 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA5 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA5();
