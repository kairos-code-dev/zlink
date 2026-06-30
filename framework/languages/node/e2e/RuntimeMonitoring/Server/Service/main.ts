import { startServiceHost } from './service-host-factory';

startServiceHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
