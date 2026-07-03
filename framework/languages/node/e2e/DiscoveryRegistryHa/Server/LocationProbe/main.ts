import 'reflect-metadata';
import { startLocationProbeHost } from './location-probe-host-factory';

startLocationProbeHost(process.argv.slice(2)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
