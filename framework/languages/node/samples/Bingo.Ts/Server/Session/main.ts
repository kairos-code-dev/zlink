import { buildSessionServerHost } from './session-server-host-factory';

buildSessionServerHost({
  sessionEndpoint: process.env.BINGO_SESSION_ENDPOINT,
  apiEndpoint: process.env.BINGO_API_ENDPOINT,
  playEndpoint: process.env.BINGO_PLAY_ENDPOINT
}).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
