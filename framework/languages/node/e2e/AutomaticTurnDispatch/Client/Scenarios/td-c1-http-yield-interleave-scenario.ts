// TD-C1: HTTP client의 yield로 외부 API를 호출한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdC1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdC1();
