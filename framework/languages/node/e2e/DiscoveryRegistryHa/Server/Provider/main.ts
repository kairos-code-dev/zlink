import 'reflect-metadata';
import { startProviderHost } from './provider-host-factory';

startProviderHost(process.argv.slice(2)).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
