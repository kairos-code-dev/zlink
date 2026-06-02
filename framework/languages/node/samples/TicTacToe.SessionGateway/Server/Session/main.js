const framework = require('../../../../packages/framework/dist');
const { startRouteServer } = require('../../../shared/route-runtime');
const { SampleBoundSessionRuntime } = require('../../../shared/bound-session-runtime');
const { SessionPlayerActorFactory } = require('../../Shared/Actors/player-actor');
const { SessionGatewayRound } = require('../../Shared/Contracts/round');

async function main() {
  const gateway = createGateway();
  await startRouteServer({
    endpoint: process.env.TICTACTOE_SG_SESSION_ENDPOINT,
    routingId: 'session-server',
    handlers: [{ packetName: 'RunSessionGateway', handle: () => runScenario(gateway) }]
  });
}

function createGateway() {
  const boundSessions = new SampleBoundSessionRuntime();
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', SessionPlayerActorFactory]]),
    boundSessionFactory(actorId) {
      return boundSessions.createBoundSession(actorId);
    }
  });
  return { boundSessions, manager };
}

async function bind(gateway, actorId, sessionId, generation) {
  const context = gateway.boundSessions.runtime.createSessionContext({
    sessionId,
    routingId: `rid-${sessionId}`,
    async close() {},
    write() { return true; }
  });
  const actor = await context.actors.bind({ nodeRid: 'session-node', actorId, generation: BigInt(generation) });
  return { actor, context, sessionId };
}

async function runScenario(gateway) {
  const firstBinding = await bind(gateway, 'p1', 'session-1', 1);
  const opponentBinding = await bind(gateway, 'p2', 'session-o', 1);
  const first = await gateway.manager.getOrCreate('p1', 'player');
  const opponent = await gateway.manager.getOrCreate('p2', 'player');
  await first.notifyTurn(0);
  const secondBinding = await bind(gateway, 'p1', 'session-2', 2);
  const second = await gateway.manager.getOrCreate('p1', 'player');
  await second.notifyTurn(1);
  gateway.boundSessions.unbind(firstBinding);
  const round = new SessionGatewayRound('match-1', second, opponent);
  await round.notifyJoined();
  const moves = [
    await round.place('p1', 0),
    await round.place('p2', 3),
    await round.place('p1', 1),
    await round.place('p2', 4),
    await round.place('p1', 2)
  ];
  return {
    sameActor: first === second,
    finalState: moves.at(-1),
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
  if (token === firstBinding.actor.bindingToken) return firstBinding.sessionId;
  if (token === secondBinding.actor.bindingToken) return secondBinding.sessionId;
  if (token === opponentBinding.actor.bindingToken) return opponentBinding.sessionId;
  return 'unknown';
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
