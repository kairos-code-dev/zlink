import { parseClientOptions } from './Support/client-options';
import { runDrA1 } from './Scenarios/BasicDiscoveryScenario';
import { runDrA2 } from './Scenarios/DrA2ClusterBridgeScenario';
import { runDrA3 } from './Scenarios/DrA3ClusterBridgeScenario';
import { runDrA4 } from './Scenarios/DrA4ThirdRegistryScenario';
import { runDrB1 } from './Scenarios/DrB1FailoverScenario';
import { runDrB2 } from './Scenarios/DrB2FailoverScenario';
import { runDrB3 } from './Scenarios/DrB3RecoveryScenario';
import { runDrC1 } from './Scenarios/DrC1EmbeddedRegistryScenario';
import { runDrC2 } from './Scenarios/DrC2EmbeddedRegistryScenario';
import { runDrC3 } from './Scenarios/DrC3EmbeddedRegistryScenario';
import { runDrD1 } from './Scenarios/DrD1DirectEndpointScenario';
import { runDrD2 } from './Scenarios/DrD2DirectEndpointScenario';
import { runDrD3 } from './Scenarios/DrD3DirectEndpointScenario';
import { runDrD4 } from './Scenarios/DrD4DirectEndpointScenario';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  if (!['DR-A1', 'DR-A2', 'DR-A3', 'DR-A4', 'DR-B1', 'DR-B2', 'DR-B3', 'DR-C1', 'DR-C2', 'DR-C3', 'DR-D1', 'DR-D2', 'DR-D3', 'DR-D4', 'all'].includes(options.scenario)) {
    throw new Error(`Unsupported scenario '${options.scenario}'.`);
  }
  if (options.scenario === 'DR-A1' || options.scenario === 'all') {
    await runDrA1(options);
  }
  if (options.scenario === 'DR-A2' || options.scenario === 'all') {
    await runDrA2(options);
  }
  if (options.scenario === 'DR-A3' || options.scenario === 'all') {
    await runDrA3(options);
  }
  if (options.scenario === 'DR-A4' || options.scenario === 'all') {
    await runDrA4(options);
  }
  if (options.scenario === 'DR-B1' || options.scenario === 'all') {
    await runDrB1(options);
  }
  if (options.scenario === 'DR-B2' || options.scenario === 'all') {
    await runDrB2(options);
  }
  if (options.scenario === 'DR-B3' || options.scenario === 'all') {
    await runDrB3(options);
  }
  if (options.scenario === 'DR-C1' || options.scenario === 'all') {
    await runDrC1(options);
  }
  if (options.scenario === 'DR-C2' || options.scenario === 'all') {
    await runDrC2(options);
  }
  if (options.scenario === 'DR-C3' || options.scenario === 'all') {
    await runDrC3(options);
  }
  if (options.scenario === 'DR-D1' || options.scenario === 'all') {
    await runDrD1(options);
  }
  if (options.scenario === 'DR-D2' || options.scenario === 'all') {
    await runDrD2(options);
  }
  if (options.scenario === 'DR-D3' || options.scenario === 'all') {
    await runDrD3(options);
  }
  if (options.scenario === 'DR-D4' || options.scenario === 'all') {
    await runDrD4(options);
  }
  console.log('discovery-registry-ha e2e result=passed');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
