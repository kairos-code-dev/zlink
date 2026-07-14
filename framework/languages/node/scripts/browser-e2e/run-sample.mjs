import http from 'node:http';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { listenOnBrowserSafeLoopbackPort } from './browser-safe-listen.mjs';

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const workspaceRoot = path.resolve(scriptDir, '../..');
const sampleName = process.argv[2];
if (!sampleName) {
  throw new Error('Usage: node scripts/browser-e2e/run-sample.mjs <Sample.Ts>');
}

process.env.PLAYWRIGHT_BROWSERS_PATH ??= path.join(workspaceRoot, '.cache/ms-playwright');
const { chromium } = await import('playwright');
const definition = sampleDefinition(sampleName);
const sampleRoot = path.join(workspaceRoot, 'samples', sampleName);
const entryName = process.argv[3] ?? 'main.ts';
const bundle = await build({
  entryPoints: [path.join(sampleRoot, 'Client', entryName)],
  bundle: true,
  write: false,
  format: 'esm',
  platform: 'browser',
  target: 'es2022',
  sourcemap: 'inline',
  logLevel: 'silent'
});

const server = http.createServer((request, response) => {
  void serve(request, response, bundle.outputFiles[0].contents, definition);
});
await listen(server);
const address = server.address();
if (typeof address === 'string' || address === null) throw new Error('Browser runner did not get a TCP port.');

let browser;
try {
  browser = await chromium.launch({ headless: true });
} catch (error) {
  throw new Error(
    `Chromium is not installed. Run 'npm run browser:install' first. ${error instanceof Error ? error.message : error}`
  );
}

const page = await browser.newPage();
page.on('console', (message) => process.stdout.write(`${message.text()}\n`));
page.on('pageerror', (error) => process.stderr.write(`${error.stack ?? error.message}\n`));

try {
  await page.goto(`http://127.0.0.1:${address.port}/`, { waitUntil: 'load' });
  await page.waitForFunction(
    () => window.__zlinkSampleResult?.status === 'passed' || window.__zlinkSampleResult?.status === 'failed',
    undefined,
    { timeout: definition.timeoutMs }
  );
  const result = await page.evaluate(() => window.__zlinkSampleResult);
  if (result?.status !== 'passed') {
    throw new Error(`${sampleName} browser scenario failed: ${result?.error ?? 'missing result'}`);
  }
} finally {
  await browser.close();
  await close(server);
}

function sampleDefinition(name) {
  switch (name) {
    case 'Bingo.Ts':
      return {
        timeoutMs: 90_000,
        config: {
          sessionAEndpoint: requireEnv('BINGO_SESSION_A_ENDPOINT'),
          sessionBEndpoint: requireEnv('BINGO_SESSION_B_ENDPOINT'),
          drainExcludedNodeRid: process.env.BINGO_DRAIN_EXCLUDED_NODE_RID,
          drainGateUrl: '/control/drain-gate'
        },
        proxies: []
      };
    case 'TicTacToe.Ts':
      return {
        timeoutMs: 90_000,
        config: { apiHttpEndpoint: '/api/tictactoe' },
        proxies: [{ prefix: '/api/tictactoe', target: requireEnv('TICTACTOE_API_A_HTTP_ENDPOINT') }]
      };
    case 'SupportChat.Ts':
      return {
        timeoutMs: 90_000,
        config: { sessionStreamEndpoint: requireEnv('SUPPORTCHAT_STREAM_ENDPOINT') },
        proxies: []
      };
    case 'DeliveryDispatch.Ts':
      return {
        timeoutMs: 90_000,
        config: {
          dispatchApiHttpUrl: '/api/delivery',
          sessionStreamEndpoint: requireEnv('DELIVERYDISPATCH_SESSION_STREAM'),
          courierStreamEndpoint: requireEnv('DELIVERYDISPATCH_COURIER_STREAM')
        },
        proxies: [{ prefix: '/api/delivery', target: requireEnv('DELIVERYDISPATCH_API_HTTP') }]
      };
    case 'GameQuest.Ts':
      return {
        timeoutMs: 120_000,
        config: {
          apiAHttpUrl: '/api/gamequest/api-a',
          apiBHttpUrl: '/api/gamequest/api-b',
          apiAStreamEndpoint: requireEnv('GAMEQUEST_API_A_STREAM'),
          apiBStreamEndpoint: requireEnv('GAMEQUEST_API_B_STREAM'),
          missionAHttpUrl: '/api/gamequest/mission-a',
          missionBHttpUrl: '/api/gamequest/mission-b'
        },
        proxies: [
          { prefix: '/api/gamequest/api-a', target: requireEnv('GAMEQUEST_API_A_HTTP') },
          { prefix: '/api/gamequest/api-b', target: requireEnv('GAMEQUEST_API_B_HTTP') },
          { prefix: '/api/gamequest/mission-a', target: requireEnv('GAMEQUEST_MISSION_A_HTTP') },
          { prefix: '/api/gamequest/mission-b', target: requireEnv('GAMEQUEST_MISSION_B_HTTP') }
        ]
      };
    default:
      throw new Error(`Unknown browser sample '${name}'.`);
  }
}

async function serve(request, response, bundleBytes, definition) {
  const requestUrl = new URL(request.url ?? '/', 'http://runner.invalid');
  if (requestUrl.pathname === '/') {
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' });
    response.end('<!doctype html><meta charset="utf-8"><script type="module" src="/client.mjs"></script>');
    return;
  }
  if (requestUrl.pathname === '/client.mjs') {
    response.writeHead(200, { 'content-type': 'text/javascript; charset=utf-8', 'cache-control': 'no-store' });
    response.end(bundleBytes);
    return;
  }
  if (requestUrl.pathname === '/config.json') {
    response.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
    response.end(JSON.stringify(definition.config));
    return;
  }
  if (requestUrl.pathname === '/control/drain-gate') {
    const gateFile = process.env.BINGO_DRAIN_GATE_FILE;
    if (!gateFile) {
      response.writeHead(404).end();
      return;
    }
    const { existsSync } = await import('node:fs');
    response.writeHead(existsSync(gateFile) ? 204 : 404, { 'cache-control': 'no-store' });
    response.end();
    return;
  }
  const proxy = definition.proxies.find((candidate) =>
    requestUrl.pathname === candidate.prefix || requestUrl.pathname.startsWith(`${candidate.prefix}/`)
  );
  if (proxy) {
    await proxyRequest(request, response, requestUrl, proxy);
    return;
  }
  response.writeHead(404).end();
}

async function proxyRequest(request, response, requestUrl, proxy) {
  const target = new URL(proxy.target);
  const suffix = requestUrl.pathname.slice(proxy.prefix.length) || '/';
  const upstream = http.request({
    hostname: target.hostname,
    port: target.port,
    method: request.method,
    path: `${suffix}${requestUrl.search}`,
    headers: copyHeaders(request.headers)
  }, (upstreamResponse) => {
    response.writeHead(upstreamResponse.statusCode ?? 502, copyHeaders(upstreamResponse.headers));
    upstreamResponse.pipe(response);
  });
  upstream.on('error', (error) => {
    if (!response.headersSent) response.writeHead(502, { 'content-type': 'text/plain' });
    response.end(error.message);
  });
  request.pipe(upstream);
}

function copyHeaders(headers) {
  return Object.fromEntries(
    Object.entries(headers).filter(([name, value]) => value !== undefined && !['host', 'connection'].includes(name))
  );
}

function requireEnv(name) {
  const value = process.env[name];
  if (!value) throw new Error(`${name} is required by the browser sample runner.`);
  return value;
}

function listen(server) {
  return listenOnBrowserSafeLoopbackPort(server);
}

function close(server) {
  return new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
}
