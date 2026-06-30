import { parseClientOptions } from './Support/client-options';
import { runRcA1, runRcA2, runRcA3, runRcA4, runRcA5, runRcA6 } from './Scenarios/rc-a-registration-scenarios';
import { runRcB1, runRcB2, runRcB3, runRcB4, runRcB5 } from './Scenarios/rc-b-codec-scenarios';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  await runRcA1(options.serverUrl);
  await runRcA2(options.serverUrl);
  await runRcA3(options.serverUrl);
  await runRcA4(options.serverUrl);
  await runRcA5(options.serverUrl);
  await runRcA6(options);
  await runRcB1(options.serverUrl);
  await runRcB2(options.protobufUrl);
  await runRcB3(options.messagePackUrl);
  await runRcB4(options.serverUrl);
  await runRcB5(options.codecRequesterUrl, options.jsonOnlyUrl);
  console.log('registration-codec e2e result=passed');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
