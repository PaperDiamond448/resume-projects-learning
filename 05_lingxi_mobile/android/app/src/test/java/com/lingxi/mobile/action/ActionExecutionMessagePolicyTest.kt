package com.lingxi.mobile.action

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class ActionExecutionMessagePolicyTest {

    @Test
    fun `sms does not expose premature completion wording`() {
        val message = ActionExecutionMessagePolicy.beforeExecution("send_message", "短信已经发送完成")

        assertFalse(message.contains("完成"))
        assertFalse(message.contains("已经发送"))
    }

    @Test
    fun `call does not expose premature completion wording`() {
        val message = ActionExecutionMessagePolicy.beforeExecution("make_call", "电话已经拨出")

        assertFalse(message.contains("已经拨出"))
    }

    @Test
    fun `non gated action preserves brain reply`() {
        assertEquals(
            "计时器已创建",
            ActionExecutionMessagePolicy.beforeExecution("start_timer", "计时器已创建"),
        )
    }
}
