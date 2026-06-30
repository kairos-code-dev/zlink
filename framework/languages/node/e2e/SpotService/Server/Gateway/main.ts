import { startGatewayHost } from './gateway-host-factory';

startGatewayHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
