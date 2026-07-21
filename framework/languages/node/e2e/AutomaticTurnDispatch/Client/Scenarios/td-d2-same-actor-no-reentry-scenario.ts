// TD-D2: 같은 actor의 다음 record가 yield 대기 중 실행된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD2();
