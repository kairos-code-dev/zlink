import { parseClientOptions } from './Support/client-options';
import { runSfA1 } from './Scenarios/SfA1BaselineScenario';
import { runSfB1 } from './Scenarios/SfB1StoreOutageScenario';
import { runSfC1 } from './Scenarios/SfC1ProviderCrashScenario';
import { runSfD1 } from './Scenarios/SfD1ShortOutageRecoveryScenario';
import { runSfD2 } from './Scenarios/SfD2LongOutageRecoveryScenario';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  if (!['SF-A1', 'SF-B1', 'SF-C1', 'SF-D1', 'SF-D2', 'all'].includes(options.scenario)) {
    throw new Error(`Unsupported scenario '${options.scenario}'.`);
  }
  if (options.scenario === 'SF-A1' || options.scenario === 'all') {
    await runSfA1(options);
  }
  if (options.scenario === 'SF-B1') {
    await runSfB1(options);
  }
  if (options.scenario === 'SF-C1') {
    await runSfC1(options);
  }
  if (options.scenario === 'SF-D1') {
    await runSfD1(options);
  }
  if (options.scenario === 'SF-D2') {
    await runSfD2(options);
  }
  console.log('store-failure-recovery scenario result=passed');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
