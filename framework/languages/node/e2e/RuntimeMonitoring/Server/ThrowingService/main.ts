import { startServiceHost } from '../Service/service-host-factory';

startServiceHost({ throwMonitor: true }).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
