import { startServiceHost } from '../Service/service-host-factory';

startServiceHost({ socketFilter: true }).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
