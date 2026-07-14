import { startServiceHost } from './service-host-factory';

startServiceHost().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
