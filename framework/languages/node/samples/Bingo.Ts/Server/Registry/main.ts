import { buildRegistryHost } from './registry-host-factory';

buildRegistryHost({
  registryEndpoint: process.env.BINGO_REGISTRY_ENDPOINT
}).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
