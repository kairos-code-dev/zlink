const { runRoleServer } = require('../../shared/role-process');
const { createSessionGateway } = require('./gateway');

async function main() {
  await runRoleServer(
    () => createSessionGateway(),
    { run: runReconnectScenario }
  );
}

async function runReconnectScenario(gateway) {
  const firstBinding = await gateway.bind('p1', 'session-1', 1);
  const first = await gateway.manager.getOrCreate('p1', 'player');
  await first.notifyTurn(0);

  const secondBinding = await gateway.bind('p1', 'session-2', 2);
  const second = await gateway.manager.getOrCreate('p1', 'player');
  await second.notifyTurn(1);
  gateway.staleUnbind(firstBinding);

  return {
    sameActor: first === second,
    staleAccepted: gateway.boundSessions.disconnected.some((entry) => entry.token === firstBinding.actor.bindingToken),
    delivered: gateway.boundSessions.delivered.map((entry) => ({
      sessionId: entry.token === firstBinding.actor.bindingToken ? firstBinding.sessionId : secondBinding.sessionId,
      packetName: entry.packetName,
      cell: entry.payload.cell
    }))
  };
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
