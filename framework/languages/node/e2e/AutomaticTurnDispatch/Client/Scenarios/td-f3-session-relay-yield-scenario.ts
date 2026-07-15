// TD-F3: session relay로 들어온 actor handler에서도 같다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdF3 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdF3();
