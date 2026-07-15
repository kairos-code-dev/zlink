// TD-A1: 세 terminator를 모두 노출한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA1();
