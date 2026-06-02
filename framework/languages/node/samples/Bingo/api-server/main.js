const { runRoleServer } = require('../../shared/role-process');
const { createBingoServer } = require('./bingo-server');

async function main() {
  await runRoleServer(
    () => createBingoServer(),
    { run: runDeterministicBingo }
  );
}

async function runDeterministicBingo(server) {
  const room = await server.spots.create(server.BingoRoomSpot);
  let winnerTask;
  await server.spots.executeOnSpot(room.spotRid, (spot) => {
    spot.join('p1', [7], server.sessionFor('p1'));
    spot.join('p2', [9], server.sessionFor('p2'));
    winnerTask = spot.start([7, 9]);
  });
  const winner = await winnerTask;

  return {
    winner,
    notifications: server.notificationHub.messages.map((message) => ({
      actorId: message.actorId,
      packetName: message.packetName,
      payload: message.payload
    }))
  };
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
