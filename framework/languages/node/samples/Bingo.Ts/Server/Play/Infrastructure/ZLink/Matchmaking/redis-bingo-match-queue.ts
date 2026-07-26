import * as net from 'node:net';
import type { BingoMode } from '../../../Domain/Bingo/bingo-room-models';
import type {
  BingoMatchQueue,
  ReserveWaitingRoomInput,
  ReserveWaitingRoomResult,
  WaitingRoomRecord
} from '../../../Application/RoomAllocation/bingo-match-queue';

type RedisValue = string | number | null | RedisValue[];

class RedisBingoMatchQueue implements BingoMatchQueue {
  constructor(
    private readonly redisEndpoint: string,
    private readonly keyPrefix: string
  ) {}

  async reserve(input: ReserveWaitingRoomInput): Promise<ReserveWaitingRoomResult> {
    const candidate = {
      roomId: input.createRoomId(),
      reservedActorIds: [input.actorId],
      requiredPlayers: input.requiredPlayers,
      createdAtUnixMs: Date.now()
    };
    const value = await redisCommand(this.redisEndpoint, [
      'EVAL',
      reserveRoomScript,
      '1',
      this.key(input.mode),
      input.actorId,
      String(input.requiredPlayers),
      JSON.stringify(candidate)
    ]);
    if (!Array.isArray(value) || value.length !== 3 || typeof value[0] !== 'string') {
      throw new Error('Invalid Redis match queue response.');
    }
    return {
      record: JSON.parse(value[0]) as WaitingRoomRecord,
      created: value[1] === 1,
      completed: value[2] === 1
    };
  }

  private key(mode: BingoMode): string {
    return `${this.keyPrefix}match:${mode}`;
  }
}

const reserveRoomScript = `
local key = KEYS[1]
local actorId = ARGV[1]
local requiredPlayers = tonumber(ARGV[2])
local candidate = cjson.decode(ARGV[3])
local raw = redis.call('GET', key)
local created = 0
local record

if raw then
  record = cjson.decode(raw)
else
  record = candidate
  created = 1
end

local alreadyReserved = 0
for _, reservedActorId in ipairs(record.ReservedActorIds or record.reservedActorIds) do
  if reservedActorId == actorId then
    alreadyReserved = 1
    break
  end
end

if alreadyReserved == 0 then
  table.insert(record.ReservedActorIds or record.reservedActorIds, actorId)
end

local reserved = record.ReservedActorIds or record.reservedActorIds
local completed = #reserved >= requiredPlayers
redis.call('SET', key, cjson.encode(record))
if completed then
  redis.call('DEL', key)
end

return { cjson.encode(record), created, completed and 1 or 0 }
`;

function redisCommand(endpoint: string, parts: string[]): Promise<RedisValue> {
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
        const [value] = decodeResp(Buffer.concat(chunks), 0);
        resolve(value);
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

function decodeResp(buffer: Buffer, offset: number): [RedisValue, number] {
  const prefix = String.fromCharCode(buffer[offset]);
  if (prefix === '+') {
    const end = lineEnd(buffer, offset);
    return [buffer.toString('utf8', offset + 1, end), end + 2];
  }
  if (prefix === ':') {
    const end = lineEnd(buffer, offset);
    return [Number(buffer.toString('utf8', offset + 1, end)), end + 2];
  }
  if (prefix === '-') {
    const end = lineEnd(buffer, offset);
    throw new Error(buffer.toString('utf8', offset + 1, end));
  }
  if (prefix === '$') {
    const end = lineEnd(buffer, offset);
    const length = Number(buffer.toString('utf8', offset + 1, end));
    if (length < 0) {
      return [null, end + 2];
    }
    const start = end + 2;
    return [buffer.toString('utf8', start, start + length), start + length + 2];
  }
  if (prefix === '*') {
    const end = lineEnd(buffer, offset);
    const count = Number(buffer.toString('utf8', offset + 1, end));
    const values: RedisValue[] = [];
    let cursor = end + 2;
    for (let index = 0; index < count; index += 1) {
      const [value, next] = decodeResp(buffer, cursor);
      values.push(value);
      cursor = next;
    }
    return [values, cursor];
  }
  throw new Error(`Unsupported Redis response prefix '${prefix}'.`);
}

function lineEnd(buffer: Buffer, offset: number): number {
  const end = buffer.indexOf('\r\n', offset, 'utf8');
  if (end < 0) {
    throw new Error('Invalid Redis response.');
  }
  return end;
}

function parseRedisEndpoint(endpoint: string): { host: string; port: number } {
  const [host, port] = endpoint.split(':');
  return { host, port: Number(port) };
}

export { RedisBingoMatchQueue };
