const { runRoleServer } = require('../../shared/role-process');
const { createGameServer, playDeterministicGame } = require('../server/game-server');

async function main() {
  await runRoleServer(
    () => createGameServer(),
    {
      ping: () => ({ role: 'play-server' }),
      play: playDeterministicGame
    }
  );
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
