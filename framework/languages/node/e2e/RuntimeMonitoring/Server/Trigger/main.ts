import { startTriggerHost } from './trigger-host-factory';

startTriggerHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
