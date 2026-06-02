const { runRoleServer } = require('../../shared/role-process');
const { createSessionGateway } = require('./gateway');
const { SessionGatewayRound } = require('./tictactoe-round');

async function main() {
  await runRoleServer(
    () => createSessionGateway(),
    { run: runReconnectScenario }
  );
}

async function runReconnectScenario(gateway) {
  const firstBinding = await gateway.bind('p1', 'session-1', 1);
  const opponentBinding = await gateway.bind('p2', 'session-o', 1);
  const first = await gateway.manager.getOrCreate('p1', 'player');
  const opponent = await gateway.manager.getOrCreate('p2', 'player');
  await first.notifyTurn(0);

  const secondBinding = await gateway.bind('p1', 'session-2', 2);
  const second = await gateway.manager.getOrCreate('p1', 'player');
  await second.notifyTurn(1);
  gateway.staleUnbind(firstBinding);
  const round = new SessionGatewayRound('match-1', second, opponent);
  await round.notifyJoined();
  const moves = [
    await round.place('p1', 0),
    await round.place('p2', 3),
    await round.place('p1', 1),
    await round.place('p2', 4),
    await round.place('p1', 2)
  ];
  const finalState = moves.at(-1);

  return {
    sameActor: first === second,
    opponentActor: opponent.actorId,
    staleAccepted: gateway.boundSessions.disconnected.some((entry) => entry.token === firstBinding.actor.bindingToken),
    finalState,
    moves,
    delivered: gateway.boundSessions.delivered.map((entry) => ({
      sessionId: sessionIdForToken(entry.token, firstBinding, secondBinding, opponentBinding),
      actorId: entry.actorId,
      packetName: entry.packetName,
      cell: entry.payload.cell
    }))
  };
}

function sessionIdForToken(token, firstBinding, secondBinding, opponentBinding) {
  if (token === firstBinding.actor.bindingToken) {
    return firstBinding.sessionId;
  }
  if (token === secondBinding.actor.bindingToken) {
    return secondBinding.sessionId;
  }
  if (token === opponentBinding.actor.bindingToken) {
    return opponentBinding.sessionId;
  }
  return 'unknown';
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
