const { runRoleServer } = require('../../shared/role-process');
const { createSessionGateway } = require('./gateway');

async function main() {
  await runRoleServer(
    () => createSessionGateway(),
    { run: runReconnectScenario }
  );
}

async function runReconnectScenario(gateway) {
  gateway.bind('p1', 'session-1');
  const first = await gateway.manager.getOrCreate('p1', 'player');
  await first.notifyTurn(0);

  gateway.bind('p1', 'session-2');
  const second = await gateway.manager.getOrCreate('p1', 'player');
  await second.notifyTurn(1);
  const staleAccepted = gateway.staleSend('p1', 'session-1');

  return {
    sameActor: first === second,
    staleAccepted,
    delivered: gateway.bindings.delivered.map((entry) => ({
      token: entry.token,
      packetName: entry.packetName,
      cell: entry.payload.cell
    }))
  };
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
