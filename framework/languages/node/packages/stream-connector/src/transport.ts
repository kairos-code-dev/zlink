import * as net from 'node:net';
import * as tls from 'node:tls';
import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamConnection,
  ZlinkStreamErrorCode,
  ZlinkStreamTransport,
  ZlinkStreamTransportFactory
} from './contracts';
import { connectorError, readUInt16BE, readUInt32BE, throwIfAborted } from './support';

export class NodeStreamTransportFactory implements ZlinkStreamTransportFactory {
  async connect(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection> {
    if (options.transport === ZlinkStreamTransport.WebSocket || options.transport === ZlinkStreamTransport.WebSocketSecure) {
      throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'WebSocket stream transport is not implemented yet.');
    }

    const endpoint = parseEndpoint(options.endpoint);
    const socket = options.transport === ZlinkStreamTransport.Tls
      ? await connectTls(endpoint, options, signal)
      : await connectTcp(endpoint, options.connectTimeoutMs, signal);
    return new NodeDuplexStreamConnection(socket);
  }
}


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


class NodeDuplexStreamConnection implements ZlinkStreamConnection {
  private readonly chunks: Buffer[] = [];
  private bufferedBytes = 0;
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;

  constructor(private readonly socket: net.Socket | tls.TLSSocket) {
    socket.on('data', (chunk: Buffer) => {
      this.chunks.push(chunk);
      this.bufferedBytes += chunk.length;
      this.wakeReader();
    });
    socket.on('close', () => {
      this.closed = true;
      this.wakeReader();
    });
    socket.on('error', (error) => {
      this.error = error;
      this.closed = true;
      this.wakeReader();
    });
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await new Promise<void>((resolve, reject) => {
      this.socket.write(frame, (error) => {
        if (error != null) {
          reject(connectorError(ZlinkStreamErrorCode.SendFailed, 'Send failed.', error));
          return;
        }
        resolve();
      });
    });
  }

  async read(signal?: AbortSignal): Promise<Uint8Array | undefined> {
    throwIfAborted(signal);
    while (true) {
      const frame = this.tryReadFrame();
      if (frame !== undefined) {
        return frame;
      }
      if (this.error !== undefined) {
        throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Remote stream closed.', this.error);
      }
      if (this.closed) {
        return undefined;
      }
      await this.waitForData(signal);
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    this.socket.destroy();
    this.wakeReader();
  }

  private tryReadFrame(): Uint8Array | undefined {
    if (this.bufferedBytes < 6) {
      return undefined;
    }
    const prefix = this.peek(6);
    const headerLength = readUInt16BE(prefix, 0);
    const payloadLength = readUInt32BE(prefix, 2);
    const frameLength = 6 + headerLength + payloadLength;
    if (this.bufferedBytes < frameLength) {
      return undefined;
    }
    return this.consume(frameLength);
  }

  private peek(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    for (const chunk of this.chunks) {
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      if (offset === length) {
        break;
      }
    }
    return output;
  }

  private consume(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    while (offset < length) {
      const chunk = this.chunks[0];
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      this.bufferedBytes -= take;
      if (take === chunk.length) {
        this.chunks.shift();
      } else {
        this.chunks[0] = chunk.subarray(take);
      }
    }
    return output;
  }

  private waitForData(signal: AbortSignal | undefined): Promise<void> {
    if (this.readWaiter !== undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Only one pending stream read is supported.');
    }
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        this.readWaiter = undefined;
        reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
      };
      if (signal !== undefined) {
        signal.addEventListener('abort', onAbort, { once: true });
      }
      this.readWaiter = () => {
        if (signal !== undefined) {
          signal.removeEventListener('abort', onAbort);
        }
        this.readWaiter = undefined;
        resolve();
      };
    });
  }

  private wakeReader(): void {
    this.readWaiter?.();
  }
}


function parseEndpoint(endpoint: string): URL {
  try {
    return new URL(endpoint);
  } catch (cause) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint is invalid.', cause);
  }
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
    servername: host,
    rejectUnauthorized: !options.skipServerCertificateValidation
  }));
}

async function connectSocket<TSocket extends net.Socket>(
  endpoint: URL,
  connectTimeoutMs: number,
  signal: AbortSignal | undefined,
  create: (port: number, host: string) => TSocket
): Promise<TSocket> {
  const port = Number(endpoint.port);
  if (!Number.isInteger(port) || port <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint port is required.');
  }
  const socket = create(port, endpoint.hostname);
  return await new Promise<TSocket>((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect timed out.'));
    }, connectTimeoutMs);
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off('connect', onConnect);
      socket.off('secureConnect', onConnect);
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
    socket.once('connect', onConnect);
    socket.once('secureConnect', onConnect);
    socket.once('error', onError);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

