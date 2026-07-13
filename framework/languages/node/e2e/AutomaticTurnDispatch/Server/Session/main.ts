import { startSessionHost } from './session-host-factory';

startSessionHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
