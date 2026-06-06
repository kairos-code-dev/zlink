package systems.zlink.framework.runtime.handlers;

import java.lang.reflect.Method;
import java.util.concurrent.CompletionStage;

public interface ZLinkSuspendHandlerInvoker {
    boolean supports(Method method);

    CompletionStage<Object> invoke(Object handler, Method method, Object[] logicalArguments);
}
