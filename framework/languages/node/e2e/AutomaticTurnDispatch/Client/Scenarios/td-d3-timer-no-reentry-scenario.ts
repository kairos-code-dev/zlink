// TD-D3: 같은 Spot의 다음 timer record가 yield 대기 중 실행된다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdD3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdD3();
