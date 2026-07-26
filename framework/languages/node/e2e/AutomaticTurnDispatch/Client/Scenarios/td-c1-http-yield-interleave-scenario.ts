// TD-C1: 외부 API는 I/O worker의 yield로 호출한다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdC1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdC1();
