import { Inject, Injectable } from '@nestjs/common';
import { createClient } from 'redis';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { TicTacToeSampleConfig } from './sample-config';
import type { OnModuleDestroy } from '@nestjs/common';
import type { RedisClientType } from 'redis';

const TICTACTOE_SAMPLE_CONFIG = Symbol.for('TICTACTOE_SAMPLE_CONFIG');

type RoomRoute = {
  roomId: string;
  routeChannelId: string;
  ownerSpotNodeRid: string;
  spotRid: string;
  spotKind: 'User';
};

@Injectable()
class RedisRoomRouteStore implements OnModuleDestroy {
  private client: RedisClientType | undefined;

  constructor(@Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig) {}

  async save(route: RoomRoute): Promise<void> {
    await (await this.connectedClient()).hSet(routeKey(this.config, route.roomId), {
      RouteChannelId: route.routeChannelId,
      OwnerNodeRid: route.ownerSpotNodeRid,
      SpotRid: route.spotRid,
      SpotKind: route.spotKind
    });
  }

  async load(roomId: string): Promise<RoomRoute> {
    const value = await (await this.connectedClient()).hGetAll(routeKey(this.config, roomId)) as Partial<{
      RouteChannelId: string;
      OwnerNodeRid: string;
      SpotRid: string;
      SpotKind: string;
    }>;
    if (Object.keys(value).length === 0) {
      throw new Error(`Room route not found. roomId=${roomId}`);
    }
    if (
      value.RouteChannelId === undefined ||
      value.OwnerNodeRid === undefined ||
      value.SpotRid === undefined ||
      value.SpotKind !== 'User'
    ) {
      throw new Error(`Room route is incomplete. roomId=${roomId}`);
    }
    return {
      roomId,
      routeChannelId: value.RouteChannelId,
      ownerSpotNodeRid: value.OwnerNodeRid,
      spotRid: value.SpotRid,
      spotKind: value.SpotKind
    };
  }

  async onModuleDestroy(): Promise<void> {
    if (this.client?.isOpen === true) {
      await this.client.quit();
    }
    this.client = undefined;
  }

  private async connectedClient(): Promise<RedisClientType> {
    if (this.client === undefined) {
      this.client = createClient({ url: `redis://${this.config.redisEndpoint}` });
      this.client.on('error', (error) => console.error('Redis room route store error.', error));
    }
    if (!this.client.isOpen) {
      await this.client.connect();
    }
    return this.client;
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
  return `${config.redisKeyPrefix}tictactoe:rooms:${roomId}`;
}

export {
  createTicTacToeLocationStore,
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
};
export type { RoomRoute };
