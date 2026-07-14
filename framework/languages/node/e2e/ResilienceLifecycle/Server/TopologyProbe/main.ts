import 'reflect-metadata';
import { startTopologyProbeHost } from './topology-probe-host-factory';

startTopologyProbeHost().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
