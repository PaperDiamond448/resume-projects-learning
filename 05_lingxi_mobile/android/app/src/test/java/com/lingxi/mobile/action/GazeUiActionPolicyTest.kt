package com.lingxi.mobile.action

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GazeUiActionPolicyTest {

    @Test
    fun acceptsOnlyNarrowDeicticOpenCommands() {
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("打开这个"))
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("你好灵犀，帮我点开这个"))
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("请查看一下它吧"))
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("灵犀，帮我打开一下这一个啊"))
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("麻烦你点击那个"))
        assertTrue(GazeUiActionPolicy.isDirectOpenCommand("我想打开他"))
        assertFalse(GazeUiActionPolicy.isDirectOpenCommand("把这个加入购物车"))
        assertFalse(GazeUiActionPolicy.isDirectOpenCommand("打开淘宝"))
        assertFalse(GazeUiActionPolicy.isDirectOpenCommand("删除这个"))
    }

    @Test
    fun blocksSensitiveOrAmbiguousTargets() {
        assertTrue(GazeUiActionPolicy.isSafeTarget("蓝牙"))
        assertTrue(GazeUiActionPolicy.isSafeTarget("昨夜风今宵月"))
        assertFalse(GazeUiActionPolicy.isSafeTarget("立即购买"))
        assertFalse(GazeUiActionPolicy.isSafeTarget("删除联系人"))
        assertFalse(GazeUiActionPolicy.isSafeTarget("确定"))
        assertFalse(GazeUiActionPolicy.isSafeTarget("A"))
    }

    @Test
    fun touchAssistFallbackAcceptsPartialAsrButRejectsNegation() {
        assertTrue(GazeUiActionPolicy.isTouchAssistFallbackCommand("打开"))
        assertTrue(GazeUiActionPolicy.isTouchAssistFallbackCommand("这个"))
        assertTrue(GazeUiActionPolicy.isTouchAssistFallbackCommand("你好灵犀，点开"))
        assertFalse(GazeUiActionPolicy.isTouchAssistFallbackCommand("不要打开这个"))
        assertFalse(GazeUiActionPolicy.isTouchAssistFallbackCommand("别点这个"))
        assertFalse(GazeUiActionPolicy.isTouchAssistFallbackCommand("为什么打不开这个"))
        assertFalse(GazeUiActionPolicy.isTouchAssistFallbackCommand("今天天气怎么样"))
    }
}
