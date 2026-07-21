package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSubmitResult;

public interface ZLinkActorSendCall {
    CompletionStage<ZLinkSubmitResult> submit();
}
