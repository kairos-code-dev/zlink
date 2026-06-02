const { runRoleServer } = require('../../shared/role-process');
const { createGameServer } = require('./game-server');

async function main() {
  await runRoleServer(
    () => createGameServer(),
    { play: playDeterministicGame }
  );
}

async function playDeterministicGame(server) {
  const match = await server.channelClient
    .requestToChannel('match', Buffer.from('start'))
    .packetName('CreateMatch')
    .timeout(1000)
    .submit();
  const created = await server.spots.create(server.GameSpot);
  const p1 = await server.actors.getOrCreate('p1', 'player');
  const p2 = await server.actors.getOrCreate('p2', 'player');

  let winner;
  await server.spots.executeOnSpot(created.spotRid, (spot) => {
    spot.join(p1.actorId, 'X');
    spot.join(p2.actorId, 'O');
    spot.place('p1', 0);
    spot.place('p2', 3);
    spot.place('p1', 1);
    spot.place('p2', 4);
    winner = spot.place('p1', 2);
  });

  return {
    match: match.toString(),
    winner,
    firstPacket: server.channelEvents[0]?.packetName
  };
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
