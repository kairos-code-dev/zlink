// TD-A1: operation별 terminator 의미를 노출한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdA1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdA1();
