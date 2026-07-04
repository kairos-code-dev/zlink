import type { RoutingId } from '../Common';
import type { ZLinkLocationRole } from './Values';

export interface IZLinkLocationReadiness {
  isPeerReady(
    meshName: string,
    role: ZLinkLocationRole,
    nodeRid?: RoutingId,
    signal?: AbortSignal
  ): Promise<boolean>;
}
