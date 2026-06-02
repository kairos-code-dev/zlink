const { buildPlayServerHost } = require('./play-server-host-factory');

async function main() {
  await buildPlayServerHost({
    playEndpoint: process.env.BINGO_PLAY_ENDPOINT
  });
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
