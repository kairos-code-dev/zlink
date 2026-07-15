// TD-B1: yield 대기 중 같은 Spot의 다른 callback이 실행된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdB1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdB1();
