import http from 'node:http';
import { loadSampleConfig } from '../Configuration/sample-config';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  await get(`${config.apiHttpUrl}/health`);
  console.log('supportchat topology=ready');
}

function get(url: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      res.resume();
      res.statusCode === 200 ? resolve() : reject(new Error(`GET ${url} returned ${res.statusCode}`));
    });
    req.on('error', reject);
    req.setTimeout(10000, () => {
      req.destroy(new Error(`Timed out waiting for ${url}`));
    });
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
