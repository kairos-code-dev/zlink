// TD-C2: HTTP client의 async는 turn을 유지한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdC2 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdC2();
