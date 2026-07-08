import { Inject, Injectable } from '@nestjs/common';
import * as net from 'node:net';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { TicTacToeSampleConfig } from './sample-config';

const TICTACTOE_SAMPLE_CONFIG = Symbol.for('TICTACTOE_SAMPLE_CONFIG');

type RoomRoute = {
  roomId: string;
  ownerPlayEndpoint: string;
  ownerSpotEndpoint: string;
  ownerSpotNodeRid: string;
};

@Injectable()
class RedisRoomRouteStore {
  constructor(@Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig) {}

  async save(route: RoomRoute): Promise<void> {
    await redisCommand(this.config.redisEndpoint, [
      'SET',
      routeKey(this.config, route.roomId),
      JSON.stringify(route)
    ]);
  }

  async load(roomId: string): Promise<RoomRoute> {
    const value = await redisCommand(this.config.redisEndpoint, ['GET', routeKey(this.config, roomId)]);
    if (value === null) {
      throw new Error(`Room route not found. roomId=${roomId}`);
    }
    return JSON.parse(value) as RoomRoute;
  }
}

function createTicTacToeLocationStore(
  config: Pick<TicTacToeSampleConfig, 'redisEndpoint' | 'redisKeyPrefix'>
): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function routeKey(config: TicTacToeSampleConfig, roomId: string): string {
  return `${config.redisKeyPrefix}${roomId}`;
}

function redisCommand(endpoint: string, parts: string[]): Promise<string | null> {
  const { host, port } = parseRedisEndpoint(endpoint);
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host, port });
    const chunks: Buffer[] = [];
    socket.once('error', reject);
    socket.on('data', (chunk) => chunks.push(chunk));
    socket.once('connect', () => {
      socket.end(encodeResp(parts));
    });
    socket.once('end', () => {
      try {
        resolve(decodeResp(Buffer.concat(chunks)));
      } catch (error) {
        reject(error);
      }
    });
  });
}

function encodeResp(parts: string[]): Buffer {
  const lines = [`*${parts.length}`];
  for (const part of parts) {
    const bytes = Buffer.from(part, 'utf8');
    lines.push(`$${bytes.length}`, part);
  }
  return Buffer.from(`${lines.join('\r\n')}\r\n`, 'utf8');
}

function decodeResp(buffer: Buffer): string | null {
  const text = buffer.toString('utf8');
  if (text.startsWith('+')) {
    return text.slice(1).trimEnd();
  }
  if (text.startsWith('$-1')) {
    return null;
  }
  if (text.startsWith('$')) {
    const firstLineEnd = text.indexOf('\r\n');
    if (firstLineEnd < 0) {
      throw new Error('Invalid Redis bulk response.');
    }
    const length = Number(text.slice(1, firstLineEnd));
    const start = firstLineEnd + 2;
    return text.slice(start, start + length);
  }
  if (text.startsWith('-')) {
    throw new Error(text.slice(1).trimEnd());
  }
  throw new Error(`Unsupported Redis response: ${text}`);
}

function parseRedisEndpoint(endpoint: string): { host: string; port: number } {
  const [host, port] = endpoint.split(':');
  return { host, port: Number(port) };
}

export {
  createTicTacToeLocationStore,
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
};
export type { RoomRoute };
