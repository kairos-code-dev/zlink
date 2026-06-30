import { startDelayHost } from './delay-host-factory';

startDelayHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
