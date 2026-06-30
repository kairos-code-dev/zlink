import { startJsonOnlyPeer } from './json-only-host-factory';

startJsonOnlyPeer(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
