// TD-G1: 언어별 terminator 의미가 같다 시나리오를 검증한다.
import type { ExecutionTurnScenarioSuite } from '../Support/execution-turn-scenario-suite';
export const runTdG1 = (suite: ExecutionTurnScenarioSuite): Promise<void> => suite.tdG1();
