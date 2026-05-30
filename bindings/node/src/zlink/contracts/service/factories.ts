// SPDX-License-Identifier: MPL-2.0

import type { Context } from '../core';
import type { AutoConnectType as AutoConnectTypeValue } from './discovery/discovery_models';
import type { Discovery } from './discovery/discovery';
import type { Registry } from './registry/registry';
import type { RegistryQueryClient } from './registry/registry_query_client';
import type { SpotNodeModeValue } from './index';
import type { SpotNode } from './spot/spot_node';

/** The factory functions that create top-level zlink resources; the caller owns each result. */
export interface ZlinkFactories {
  /** Create a messaging context. */
  createContext(): Context;
  /** Create a service registry on `ctx`. */
  createRegistry(ctx: Context): Registry;
  /** Create a read-only registry query client on `ctx`. */
  createRegistryQueryClient(ctx: Context): RegistryQueryClient;
  /** Create a discovery service for `channelName` that connects peers per `autoConnectType`. */
  createDiscovery(ctx: Context, autoConnectType: AutoConnectTypeValue, channelName: string): Discovery;
  /** Create a spot node on `ctx` in `mode` (or the default). */
  createSpotNode(ctx: Context, mode?: SpotNodeModeValue): SpotNode;
}
