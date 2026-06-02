import { buildPlayServerHost } from './play-server-host-factory';

buildPlayServerHost({
  playEndpoint: process.env.BINGO_PLAY_ENDPOINT
}).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
