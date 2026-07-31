// ST-I6: Actor·Spot multi-hop Message Follow와 route 정리 시나리오를 검증한다.
import { runStF5 } from './st-f5-external-route-scenario';

export async function runStI6(): Promise<void> {
  // 세 Actor node를 거친 실제 relocation chain에 지연된 one-way와
  // request를 전달하고, hop별 fence·operation identity와 route expiry를 검증한다.
  await runStF5('ST-I6');
}
