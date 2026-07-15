// TD-B4: yield 대기 중 timer가 정상 실행된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdB4 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdB4();
