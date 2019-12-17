package org.mozilla.geckoview.test.crash

import android.content.Intent
import android.os.Message
import android.os.Messenger
import android.support.test.InstrumentationRegistry
import android.support.test.filters.MediumTest
import android.support.test.rule.ServiceTestRule
import android.support.test.runner.AndroidJUnit4
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.notNullValue
import org.junit.After
import org.junit.Assert.assertThat
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.BuildConfig
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.test.TestCrashHandler
import org.mozilla.geckoview.test.util.Environment
import java.io.File
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
@MediumTest
class ParentCrashTest {
    lateinit var messenger: Messenger
    val env = Environment()

    @get:Rule val rule = ServiceTestRule()

    val client = TestCrashHandler.Client(InstrumentationRegistry.getTargetContext())

    @Before
    fun setup() {
        val context = InstrumentationRegistry.getTargetContext()
        val binder = rule.bindService(Intent(context, RemoteGeckoService::class.java))
        messenger = Messenger(binder)
        assertThat("messenger should not be null", binder, notNullValue())

        assertTrue(client.connect(env.defaultTimeoutMillis))
        client.setEvalNextCrashDump(/* expectFatal */ true)
    }

    @Test
    fun crashParent() {
        messenger.send(Message.obtain(null, RemoteGeckoService.CMD_CRASH_PARENT_NATIVE))

        var evalResult = client.getEvalResult(env.defaultTimeoutMillis)
        assertTrue(evalResult.mMsg, evalResult.mResult)
    }

    @After
    fun teardown() {
        client.disconnect()
    }
}
