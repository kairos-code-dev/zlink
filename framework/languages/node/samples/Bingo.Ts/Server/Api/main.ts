import { buildApiServerHost } from './api-server-host-factory';

buildApiServerHost({
  apiEndpoint: process.env.BINGO_API_ENDPOINT,
  playEndpoint: process.env.BINGO_PLAY_ENDPOINT
}).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
