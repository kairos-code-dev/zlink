// TD-B3: yield를 가로지르는 spot 상태는 보장되지 않는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdB3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdB3();
