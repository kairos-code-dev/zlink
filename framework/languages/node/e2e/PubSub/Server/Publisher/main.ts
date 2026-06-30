import { startPublisherHost } from './publisher-host-factory';

startPublisherHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
