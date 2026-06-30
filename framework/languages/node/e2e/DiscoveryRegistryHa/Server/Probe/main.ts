import { startProbeHost } from './probe-host-factory';

startProbeHost(process.argv.slice(2)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
