import { startMessagePackPeer } from './messagepack-peer-host-factory';

startMessagePackPeer(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
