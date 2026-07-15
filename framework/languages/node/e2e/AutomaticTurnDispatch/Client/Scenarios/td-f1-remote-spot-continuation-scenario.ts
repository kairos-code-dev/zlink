// TD-F1: remote Spot topology에서 세 terminator가 같은 의미를 갖는다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF1();
