package com.hinnli.valoader.wxapi;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.util.SparseArray;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/**
 * WeChat OAuth entry point for Valoader.
 *
 * <p>The actual response model, parser and login observers remain the game's
 * bundled MSDK classes. This bridge only keeps the callback in the loader's
 * process and forwards it to MSDK's already registered event handlers.</p>
 */
public final class WXEntryActivity extends Activity {
    private static final String TAG = "Valoader.WeChat";
    private static final String AGENT_CLASS = "com.tencent.gcloud.msdk.WeChatAgentActivity";
    private static final String EVENT_HANDLER_CLASS =
            "com.tencent.mm.opensdk.openapi.IWXAPIEventHandler";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        dispatch(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        dispatch(intent);
    }

    private void dispatch(Intent intent) {
        try {
            ClassLoader classLoader = getClassLoader();
            Class<?> agentClass = Class.forName(AGENT_CLASS, true, classLoader);
            Class<?> handlerClass = Class.forName(EVENT_HANDLER_CLASS, true, classLoader);

            Field apiField = agentClass.getField("mWXApi");
            Object api = apiField.get(null);
            if (api == null) {
                Method initialize = agentClass.getMethod("initialWXEnv", Activity.class);
                initialize.invoke(null, this);
                api = apiField.get(null);
            }
            if (api == null) {
                throw new IllegalStateException("MSDK did not create IWXAPI");
            }

            Object handler = Proxy.newProxyInstance(
                    classLoader,
                    new Class<?>[]{handlerClass},
                    (proxy, method, args) -> {
                        String name = method.getName();
                        if (("onReq".equals(name) || "onResp".equals(name))
                                && args != null && args.length == 1) {
                            forwardToMsdkQueue(agentClass, name, args[0]);
                        }
                        return null;
                    });

            Method handleIntent = api.getClass().getMethod(
                    "handleIntent", Intent.class, handlerClass);
            boolean handled = Boolean.TRUE.equals(handleIntent.invoke(api, intent, handler));
            Log.i(TAG, "WeChat response handled=" + handled
                    + " command=" + intent.getIntExtra("_wxapi_command_type", -1));
        } catch (Throwable error) {
            Log.e(TAG, "Could not dispatch WeChat callback", error);
        } finally {
            finish();
        }
    }

    private static void forwardToMsdkQueue(Class<?> agentClass, String methodName,
                                           Object message) throws Exception {
        Field queueField = agentClass.getField("mWeChatMessagesQueue");
        SparseArray<?> queue = (SparseArray<?>) queueField.get(null);
        SparseArray<?> snapshot = queue.clone();
        Log.i(TAG, "Forwarding " + methodName + " to " + snapshot.size()
                + " MSDK handler(s)");
        for (int index = 0; index < snapshot.size(); index++) {
            Object target = snapshot.valueAt(index);
            if (target == null) {
                continue;
            }
            Method callback = findSingleArgumentMethod(target.getClass(), methodName);
            callback.setAccessible(true);
            callback.invoke(target, message);
        }
    }

    private static Method findSingleArgumentMethod(Class<?> type, String name)
            throws NoSuchMethodException {
        for (Method method : type.getMethods()) {
            if (name.equals(method.getName()) && method.getParameterCount() == 1) {
                return method;
            }
        }
        throw new NoSuchMethodException(type.getName() + "." + name);
    }
}
