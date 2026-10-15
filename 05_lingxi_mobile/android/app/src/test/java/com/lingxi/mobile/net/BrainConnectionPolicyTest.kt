package com.lingxi.mobile.net

import com.lingxi.mobile.model.AgentPlanRequest
import com.lingxi.mobile.model.InteractRequest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.UUID

class BrainConnectionPolicyTest {
    @Test
    fun releaseAcceptsHttpsAndNormalizesTrailingSlash() {
        val result = BrainConnectionPolicy.validateEndpoint(" https://brain.example.com/ ", isDebug = false)
        assertTrue(result.valid)
        assertEquals("https://brain.example.com", result.normalized)
    }

    @Test
    fun releaseRejectsCleartext() {
        assertFalse(BrainConnectionPolicy.validateEndpoint("http://brain.example.com", isDebug = false).valid)
    }

    @Test
    fun debugCleartextIsLoopbackOnly() {
        assertTrue(BrainConnectionPolicy.validateEndpoint("http://127.0.0.1:8000", isDebug = true).valid)
        assertTrue(BrainConnectionPolicy.validateEndpoint("http://10.0.2.2:8000", isDebug = true).valid)
        assertFalse(BrainConnectionPolicy.validateEndpoint("http://192.168.3.2:8000", isDebug = true).valid)
    }

    @Test
    fun endpointRejectsCredentialsQueryAndApiPath() {
        assertFalse(BrainConnectionPolicy.validateEndpoint("https://u:p@brain.example.com", false).valid)
        assertFalse(BrainConnectionPolicy.validateEndpoint("https://brain.example.com?token=x", false).valid)
        assertFalse(BrainConnectionPolicy.validateEndpoint("https://brain.example.com/interact", false).valid)
    }

    @Test
    fun tokenMustUseLingxiOpaqueFormat() {
        assertTrue(BrainConnectionPolicy.validToken("lx1_" + "a".repeat(43)))
        assertFalse(BrainConnectionPolicy.validToken("sk-" + "a".repeat(43)))
        assertFalse(BrainConnectionPolicy.validToken("lx1_short"))
    }

    @Test
    fun uuidGenerationIsUniqueAndCanonical() {
        val first = InteractRequest(text = "打开相机").request_id
        val second = AgentPlanRequest(goal = "打开相机").request_id
        assertNotEquals(first, second)
        assertEquals(first, UUID.fromString(first).toString())
        assertEquals(second, UUID.fromString(second).toString())
    }
}
