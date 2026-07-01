import { parseClientOptions } from './Support/client-options';
import { runRcA2 } from './Scenarios/AttributeRegistrationScenario';
import { runRcA1 } from './Scenarios/AutoRegistrationScenario';
import { runRcB5 } from './Scenarios/CodecMismatchScenario';
import { runRcA6 } from './Scenarios/InvalidRegistrationScenario';
import { runRcA3 } from './Scenarios/ManualRegistrationScenario';
import { runRcA4 } from './Scenarios/RcA4DiLifecycleScenario';
import { runRcA5 } from './Scenarios/RcA5FilterOrderingScenario';
import { runRcB1 } from './Scenarios/RcB1JsonCodecScenario';
import { runRcB2 } from './Scenarios/RcB2ProtobufCodecScenario';
import { runRcB3 } from './Scenarios/RcB3MessagePackCodecScenario';
import { runRcB4 } from './Scenarios/RcB4CodecCoexistenceScenario';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  await runRcA1(options.serverUrl);
  await runRcA2(options.serverUrl);
  await runRcA3(options.serverUrl);
  await runRcA4(options.serverUrl);
  await runRcA5(options.serverUrl);
  await runRcA6(options);
  await runRcB1(options.serverUrl);
  await runRcB2(options.serverUrl);
  await runRcB3(options.serverUrl);
  await runRcB4(options.serverUrl);
  await runRcB5(options.codecRequesterUrl, options.jsonOnlyUrl);
  console.log('registration-codec e2e result=passed');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
