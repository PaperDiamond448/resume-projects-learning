package com.lingxi.mobile.action

import android.Manifest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ActionPermissionPolicyTest {

    @Test
    fun `ordinary actions never request dangerous communication permissions`() {
        assertEquals(emptyList<String>(), ActionPermissionPolicy.required("navigate", mapOf("destination" to "学校")))
        assertEquals(emptyList<String>(), ActionPermissionPolicy.required("open_app", mapOf("app_name" to "天气")))
    }

    @Test
    fun `incomplete high risk actions report missing slots before requesting permission`() {
        assertEquals(emptyList<String>(), ActionPermissionPolicy.required("send_message", emptyMap()))
        assertEquals(emptyList<String>(), ActionPermissionPolicy.required("make_call", emptyMap()))
    }

    @Test
    fun `message to named contact needs sms and contacts only`() {
        assertEquals(
            listOf(Manifest.permission.SEND_SMS, Manifest.permission.READ_CONTACTS),
            ActionPermissionPolicy.required("send_message", mapOf("recipient" to "妈妈")),
        )
    }

    @Test
    fun `message to phone number does not ask for contacts`() {
        assertEquals(
            listOf(Manifest.permission.SEND_SMS),
            ActionPermissionPolicy.required("send_message", mapOf("recipient" to "+86 138-0013-8000")),
        )
    }

    @Test
    fun `call to named contact needs call and contacts only`() {
        assertEquals(
            listOf(Manifest.permission.CALL_PHONE, Manifest.permission.READ_CONTACTS),
            ActionPermissionPolicy.required("make_call", mapOf("contact" to "爸爸")),
        )
    }

    @Test
    fun `calendar query requests read only calendar permission`() {
        assertEquals(
            listOf(Manifest.permission.READ_CALENDAR),
            ActionPermissionPolicy.required(
                "query_calendar",
                mapOf("day_offset" to "1", "days" to "1"),
            ),
        )
        assertFalse(
            ActionPermissionPolicy.required("query_calendar", emptyMap())
                .contains("android.permission.WRITE_CALENDAR")
        )
    }

    @Test
    fun `phone detector accepts common formatting but rejects names`() {
        assertTrue(ActionPermissionPolicy.looksLikePhoneNumber("138 0013 8000"))
        assertTrue(ActionPermissionPolicy.looksLikePhoneNumber("+86-138-0013-8000"))
        assertFalse(ActionPermissionPolicy.looksLikePhoneNumber("妈妈"))
        assertFalse(ActionPermissionPolicy.looksLikePhoneNumber("12"))
    }
}
