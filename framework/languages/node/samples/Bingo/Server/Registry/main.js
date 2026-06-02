const { buildRegistryHost } = require('./registry-host-factory');

buildRegistryHost({
  registryEndpoint: process.env.BINGO_REGISTRY_ENDPOINT
}).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
