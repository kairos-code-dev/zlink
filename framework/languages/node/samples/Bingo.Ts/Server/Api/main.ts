const { buildApiServerHost } = require('./api-server-host-factory');

async function main() {
  await buildApiServerHost({
    apiEndpoint: process.env.BINGO_API_ENDPOINT,
    playEndpoint: process.env.BINGO_PLAY_ENDPOINT
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
