// TD-F6: wait-for 사이클은 제출 전에 거부한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF6 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF6();
