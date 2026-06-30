import 'reflect-metadata';
import { startConsumerHost } from './consumer-host-factory';

startConsumerHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
