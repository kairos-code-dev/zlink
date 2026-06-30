import 'reflect-metadata';
import { startRegistryHost } from './registry-host-factory';

startRegistryHost(process.argv.slice(2)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
