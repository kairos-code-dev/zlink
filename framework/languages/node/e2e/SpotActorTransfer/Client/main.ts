import { runBrowserE2e } from '../../browser-client-runtime';
import { runStA1 } from './Scenarios/st-a1-local-join-scenario';
import { runStA2 } from './Scenarios/st-a2-reject-rollback-scenario';
import { runStA3 } from './Scenarios/st-a3-concurrent-join-scenario';
import { runStB1 } from './Scenarios/st-b1-remote-transfer-scenario';
import { runStB2 } from './Scenarios/st-b2-source-cleanup-loss-scenario';
import { runStB3 } from './Scenarios/st-b3-source-failure-scenario';
import { runStB4 } from './Scenarios/st-b4-target-failure-rollback-scenario';
import { runStC1 } from './Scenarios/st-c1-duplicate-transfer-scenario';
import { runStC2 } from './Scenarios/st-c2-stale-generation-scenario';
import { runStC3 } from './Scenarios/st-c3-callback-failure-scenario';
import { runStD1 } from './Scenarios/st-d1-stateless-transfer-scenario';
import { runStD2 } from './Scenarios/st-d2-stateful-transfer-scenario';
import { runStE1 } from './Scenarios/st-e1-source-restart-recovery-scenario';
import { runStE2 } from './Scenarios/st-e2-target-restart-recovery-scenario';
import { runStF1 } from './Scenarios/st-f1-packet-order-scenario';
import { runStF2 } from './Scenarios/st-f2-in-flight-order-scenario';
import { runStF3 } from './Scenarios/st-f3-request-order-scenario';
import { runStF4 } from './Scenarios/st-f4-bound-session-transfer-scenario';
import { runStF5 } from './Scenarios/st-f5-external-route-scenario';
import { runStF6 } from './Scenarios/st-f6-backpressure-timeout-scenario';
import { closeScenarioClients, options } from './Support/scenario-support';

const scenarios: Readonly<Record<string, () => Promise<void>>> = {
  'ST-A1': runStA1, 'ST-A2': runStA2, 'ST-A3': runStA3,
  'ST-B1': runStB1, 'ST-B2': runStB2, 'ST-B3': runStB3, 'ST-B4': runStB4,
  'ST-C1': runStC1, 'ST-C2': runStC2, 'ST-C3': runStC3,
  'ST-D1': runStD1, 'ST-D2': runStD2,
  'ST-E1': runStE1, 'ST-E2': runStE2,
  'ST-F1': runStF1, 'ST-F2': runStF2, 'ST-F3': runStF3,
  'ST-F4': runStF4, 'ST-F5': runStF5, 'ST-F6': runStF6
};

async function main(): Promise<void> {
  try {
    const selected = options.scenario === 'all'
      ? Object.keys(scenarios)
      : options.scenario.split(',').map((value) => value.trim()).filter(Boolean);
    for (const name of selected) {
      const scenario = scenarios[name];
      if (scenario === undefined) throw new Error(`Unknown scenario '${name}'.`);
      await scenario();
      console.log(`scenario ${name} passed`);
    }
    console.log('spot-actor-transfer e2e result=passed');
  } finally {
    await closeScenarioClients();
  }
}

void runBrowserE2e('SpotActorTransfer', main);
