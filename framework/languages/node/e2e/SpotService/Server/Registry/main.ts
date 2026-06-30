import { startRegistryHost } from './registry-host-factory';

startRegistryHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
