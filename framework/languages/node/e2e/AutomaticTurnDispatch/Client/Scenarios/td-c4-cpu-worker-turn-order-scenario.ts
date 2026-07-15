// TD-C4: CPU worker는 pool 스레드를 점유하고, terminator가 turn을 결정한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdC4 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdC4();
