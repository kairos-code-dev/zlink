import { startSubscriberHost } from './subscriber-host-factory';

startSubscriberHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
