const { runRoleServer } = require('../../shared/role-process');

async function main() {
  await runRoleServer(
    () => ({ role: 'session-server' }),
    { ping: (state) => state }
  );
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
