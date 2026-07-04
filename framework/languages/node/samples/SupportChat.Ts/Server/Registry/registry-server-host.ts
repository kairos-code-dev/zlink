import http from 'node:http';
import { endpointPort } from '../runtime-support';
import type { SupportChatServerConfig } from '../Configuration/sample-config';

function startRegistryServer(config: SupportChatServerConfig): http.Server {
  const server = http.createServer((_req, res) => {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ status: 'ready' }));
  });
  server.listen(endpointPort(config.registryEndpoint), '127.0.0.1');
  return server;
}

export { startRegistryServer };
