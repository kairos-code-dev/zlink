// TD-D1: 한 actor가 yield 중이어도 다른 actor는 처리된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD1();
