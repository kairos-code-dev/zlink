import 'reflect-metadata';
import { startTopologyProbeHost } from './topology-probe-host-factory';

startTopologyProbeHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
