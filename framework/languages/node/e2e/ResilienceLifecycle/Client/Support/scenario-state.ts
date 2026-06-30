import type { ManagedProcess } from './managed-provider';
import type { ManagedRegistryProcess } from './managed-registry';

export interface ScenarioState {
  providerAProcess?: ManagedProcess;
  providerBProcess?: ManagedProcess;
  registryProcess?: ManagedRegistryProcess;
}
