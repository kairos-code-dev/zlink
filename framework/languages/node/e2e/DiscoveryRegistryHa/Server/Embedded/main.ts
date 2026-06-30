import { startEmbeddedHost } from './embedded-host-factory';

startEmbeddedHost(process.argv.slice(2)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
