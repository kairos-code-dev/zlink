import { startMultiNodeHost } from './multi-node-host-factory';

startMultiNodeHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
