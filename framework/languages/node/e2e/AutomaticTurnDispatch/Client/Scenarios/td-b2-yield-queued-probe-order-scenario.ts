// TD-B2: yield의 continuation은 큐에 재삽입되어 순서대로 재개된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdB2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdB2();
