import { startServiceHost } from '../Service/service-host-factory';

startServiceHost([...process.argv.slice(2), '--socket-filter', 'connection-ready']).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
