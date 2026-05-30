// SPDX-License-Identifier: MPL-2.0

import type {
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../index';

/** A read-only client that queries a registry for its topology. */
export interface RegistryQueryClient {
  /** Connect to a registry's query endpoint. */
  connect(endpoint: string): void;
  /** Return the registry's topology entries, optionally filtered. */
  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  /** Close the query client and release its resources. */
  close(): void;
}
