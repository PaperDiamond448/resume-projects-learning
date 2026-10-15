package com.lingxi.mobile.action

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken

/**
 * 仅供 App 内部启动的权限闸门：系统权限弹窗结束后，用同一份已确认动作继续执行。
 *
 * 若用户拒绝，DeviceActionExecutor(allowPermissionRequest=false) 会走短信草稿/拨号盘等安全降级，
 * 或明确停止日程读取；不会再次拉起权限页，也不会绕过用户选择。
 */
class ActionPermissionActivity : AppCompatActivity() {

    private val gson = Gson()
    private lateinit var actionType: String
    private lateinit var slots: Map<String, String>
    private lateinit var statusView: TextView
    private lateinit var doneButton: Button

    private val requestPermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        executeAndFinish()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        actionType = intent.getStringExtra(EXTRA_ACTION_TYPE).orEmpty()
        slots = decodeSlots(intent.getStringExtra(EXTRA_SLOTS_JSON).orEmpty())

        statusView = TextView(this).apply {
            text = "灵犀仅为本次操作请求所需权限；拒绝后不会绕过你的选择。"
            gravity = Gravity.CENTER
            textSize = 18f
            setPadding(48, 48, 48, 48)
        }
        doneButton = Button(this).apply {
            text = "完成"
            visibility = View.GONE
            setOnClickListener { finish() }
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(32, 32, 32, 32)
            addView(
                statusView,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    0,
                    1f,
                ),
            )
            addView(
                doneButton,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                ),
            )
        })

        if (actionType.isBlank()) {
            finish()
            return
        }
        if (savedInstanceState == null) {
            val missing = ActionPermissionPolicy.required(actionType, slots)
                .filterNot { EffectivePermission.isGranted(this, it) }
            if (missing.isEmpty()) {
                executeAndFinish()
            } else {
                // MIUI 可能出现“PackageManager 已授权、AppOps 红叉拒绝”的双层状态。
                // 再次 requestPermissions 会瞬间返回、不会显示真实弹窗；这种情况直接
                // 交给执行器走短信草稿/拨号盘安全降级。仅原生权限位未授予时才询问。
                val requestable = missing.filter {
                    ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
                }
                if (requestable.isEmpty()) executeAndFinish()
                else requestPermissions.launch(requestable.toTypedArray())
            }
        }
    }

    private fun executeAndFinish() {
        val result = DeviceActionExecutor(this, allowPermissionRequest = false).execute(actionType, slots)
        if (actionType == "query_calendar") {
            statusView.text = result.message
            doneButton.visibility = View.VISIBLE
            return
        }
        Toast.makeText(this, result.message, Toast.LENGTH_LONG).show()
        finish()
    }

    private fun decodeSlots(json: String): Map<String, String> = runCatching {
        gson.fromJson<Map<String, String>>(json, object : TypeToken<Map<String, String>>() {}.type)
    }.getOrDefault(emptyMap())

    companion object {
        private const val EXTRA_ACTION_TYPE = "action_type"
        private const val EXTRA_SLOTS_JSON = "slots_json"

        fun intent(context: Context, type: String, slots: Map<String, String>): Intent =
            Intent(context, ActionPermissionActivity::class.java).apply {
                putExtra(EXTRA_ACTION_TYPE, type)
                putExtra(EXTRA_SLOTS_JSON, Gson().toJson(slots))
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
    }
}
