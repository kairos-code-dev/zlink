// SPDX-License-Identifier: MPL-2.0

import type { Context } from '../core';
import type { AutoConnectType as AutoConnectTypeValue } from './discovery/discovery_models';
import type { Discovery } from './discovery/discovery';
import type { Registry } from './registry/registry';
import type { RegistryQueryClient } from './registry/registry_query_client';
import type { SpotNodeModeValue } from './index';
import type { SpotNode } from './spot/spot_node';

export interface ZlinkFactories {
  createContext(): Context;
  createRegistry(ctx: Context): Registry;
  createRegistryQueryClient(ctx: Context): RegistryQueryClient;
  createDiscovery(ctx: Context, autoConnectType: AutoConnectTypeValue, channelName: string): Discovery;
  createSpotNode(ctx: Context, mode?: SpotNodeModeValue): SpotNode;
}
