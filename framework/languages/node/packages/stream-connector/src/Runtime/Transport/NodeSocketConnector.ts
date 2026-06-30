import * as net from 'node:net';
import * as tls from 'node:tls';
import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamConnection,
  ZlinkStreamErrorCode,
  ZlinkStreamTransport
} from '../../Contracts';
import { connectorError } from '../ZlinkStreamSupport';
import { NodeDuplexStreamConnection } from './NodeDuplexStreamConnection';
import { NodeWebSocketConnection } from './NodeWebSocketConnection';
import { completeWebSocketHandshake } from './WebSocketHandshake';

export function inferTransport(endpoint: string): ZlinkStreamTransport {
  const url = parseEndpoint(endpoint);
  switch (url.protocol) {
    case 'tcp:':
      return ZlinkStreamTransport.Tcp;
    case 'tls:':
      return ZlinkStreamTransport.Tls;
    case 'ws:':
      return ZlinkStreamTransport.WebSocket;
    case 'wss:':
      return ZlinkStreamTransport.WebSocketSecure;
    default:
      throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint scheme is not supported.');
  }
}

export async function connectNodeStream(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection> {
  const endpoint = parseEndpoint(options.endpoint);
  if (options.transport === ZlinkStreamTransport.WebSocket || options.transport === ZlinkStreamTransport.WebSocketSecure) {
    return await connectWebSocket(endpoint, options, signal);
  }

  const socket = options.transport === ZlinkStreamTransport.Tls
    ? await connectTls(endpoint, options, signal)
    : await connectTcp(endpoint, options.connectTimeoutMs, signal);
  return new NodeDuplexStreamConnection(socket, options.maxReceivePayloadSize);
}

function parseEndpoint(endpoint: string): URL {
  try {
    return new URL(endpoint);
  } catch (cause) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint is invalid.', cause);
  }
}

async function connectWebSocket(
  endpoint: URL,
  options: RequiredZlinkStreamConnectorOptions,
  signal?: AbortSignal
): Promise<ZlinkStreamConnection> {
  const secure = options.transport === ZlinkStreamTransport.WebSocketSecure;
  const socket = secure
    ? await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => tls.connect({
      port,
      host,
      servername: tlsServerName(host),
      rejectUnauthorized: !options.skipServerCertificateValidation
    }), 443, 'secureConnect')
    : await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => net.connect({ port, host, keepAlive: true }), 80);
  const leftover = await completeWebSocketHandshake(socket, endpoint, options.connectTimeoutMs, signal);
  return new NodeWebSocketConnection(socket, options.maxReceivePayloadSize, leftover);
}

async function connectTcp(endpoint: URL, connectTimeoutMs: number, signal?: AbortSignal): Promise<net.Socket> {
  return await connectSocket(endpoint, connectTimeoutMs, signal, (port, host) => net.connect({ port, host, keepAlive: true }));
}

async function connectTls(
  endpoint: URL,
  options: RequiredZlinkStreamConnectorOptions,
  signal?: AbortSignal
): Promise<tls.TLSSocket> {
  return await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => tls.connect({
    port,
    host,
    servername: tlsServerName(host),
    rejectUnauthorized: !options.skipServerCertificateValidation
  }), undefined, 'secureConnect');
}

function tlsServerName(host: string): string | undefined {
  return net.isIP(host) === 0 ? host : undefined;
}

async function connectSocket<TSocket extends net.Socket>(
  endpoint: URL,
  connectTimeoutMs: number,
  signal: AbortSignal | undefined,
  create: (port: number, host: string) => TSocket,
  defaultPort?: number,
  readyEvent: 'connect' | 'secureConnect' = 'connect'
): Promise<TSocket> {
  const configuredPort = endpoint.port.length > 0 ? Number(endpoint.port) : defaultPort;
  if (!Number.isInteger(configuredPort) || configuredPort === undefined || configuredPort <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint port is required.');
  }
  const port = configuredPort;
  const socket = create(port, endpoint.hostname);
  return await new Promise<TSocket>((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect timed out.'));
    }, connectTimeoutMs);
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off(readyEvent, onConnect);
      socket.off('error', onError);
      signal?.removeEventListener('abort', onAbort);
    };
    const onConnect = () => {
      cleanup();
      resolve(socket);
    };
    const onError = (error: Error) => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect failed.', error));
    };
    const onAbort = () => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Connect canceled.'));
    };
    socket.once(readyEvent, onConnect);
    socket.once('error', onError);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}
