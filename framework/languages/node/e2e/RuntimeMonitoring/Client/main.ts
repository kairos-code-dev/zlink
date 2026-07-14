import { runMonA1 } from './Scenarios/mon-a1-socket-events-scenario';
import { runMonA2 } from './Scenarios/mon-a2-location-runtime-events-scenario';
import { runMonA3 } from './Scenarios/mon-a3-spot-events-scenario';
import { runMonA4 } from './Scenarios/mon-a4-availability-transition-scenario';
import { runMonA5 } from './Scenarios/mon-a5-fixed-kinds-scenario';
import { runMonB1 } from './Scenarios/mon-b1-kind-filter-scenario';
import { runMonB2 } from './Scenarios/mon-b2-registration-validation-scenario';
import { runMonC1 } from './Scenarios/mon-c1-dispatch-failure-scenario';
import { runMonD1 } from './Scenarios/mon-d1-failure-recovery-scenario';
import { parseClientOptions } from './Support/client-options';
import type { ManagedProcess } from './Support/managed-service';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  let serviceBProcess: ManagedProcess | undefined;
  const scenarios: Record<string, () => Promise<void>> = {
    'MON-A1': () => runMonA1(options),
    'MON-A2': async () => { serviceBProcess = await runMonA2(options); },
    'MON-A3': () => runMonA3(options),
    'MON-A4': () => runMonA4(options),
    'MON-A5': () => runMonA5(options),
    'MON-B1': () => runMonB1(options),
    'MON-B2': () => runMonB2(options),
    'MON-C1': () => runMonC1(options),
    'MON-D1': () => runMonD1(options)
  };
  const gaps: Record<string, string> = {};
  const defaultScenarioIds = ['MON-A1', 'MON-A2', 'MON-A3', 'MON-A4', 'MON-A5', 'MON-B1', 'MON-B2', 'MON-C1', 'MON-D1'];

  try {
    if (options.scenario.toLowerCase() === 'all') {
      for (const scenarioId of defaultScenarioIds) await scenarios[scenarioId]();
    } else {
      const selected = options.scenario.toUpperCase();
      const gap = gaps[selected];
      if (gap !== undefined) {
        throw new Error(`Scenario ${selected} is a public contract gap: ${gap}`);
      }
      const scenario = scenarios[selected];
      if (scenario === undefined) {
        throw new Error(`Unknown scenario '${options.scenario}'.`);
      }
      await scenario();
    }
  } finally {
    await serviceBProcess?.stop();
  }

  console.log('runtime-monitoring e2e result=passed');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
