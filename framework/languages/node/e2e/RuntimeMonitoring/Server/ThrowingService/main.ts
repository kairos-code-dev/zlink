import { startServiceHost } from '../Service/service-host-factory';

startServiceHost([...process.argv.slice(2), '--throw-monitor', 'true']).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
