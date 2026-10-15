package com.lingxi.mobile.action

import android.Manifest
import android.content.Context
import android.content.Intent
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.provider.CalendarContract
import android.net.Uri
import android.provider.AlarmClock
import android.provider.ContactsContract
import android.provider.MediaStore
import android.provider.Settings
import android.telephony.SmsManager
import com.lingxi.mobile.accessibility.LingxiAccessibilityService

/**
 * 设备动作执行器 —— 把大脑下发的 device_action 落地为真实手机操作。
 *
 * 这是"对标 Siri"的真办事层。非 root 上限内用系统标准 API：
 *  - 发短信：通讯录解析收件人→号码，SmsManager 直发；解析不到则打开短信 App 预填（不误发）。
 *  - 打电话：同理，ACTION_CALL 直拨 / 解析不到打开拨号盘。
 *  - 开应用：按 label 匹配已装包并启动。
 *  - 网页搜索：ACTION_WEB_SEARCH。
 *  - 提醒：AlarmClock 定时器（简化）。
 *
 * 高风险动作（发短信/打电话）的"是否真执行"已由大脑两段式确认流把关：
 * 只有 confirm 后大脑才回填 device_action，这里收到即视为已确认。
 */
class DeviceActionExecutor(
    private val context: Context,
    private val allowPermissionRequest: Boolean = true,
) {

    data class Result(val success: Boolean, val message: String)

    fun execute(type: String, slots: Map<String, String>): Result {
        val missing = ActionPermissionPolicy.required(type, slots)
            .filterNot(::hasPermission)
        if (allowPermissionRequest && missing.isNotEmpty()) {
            val opened = runCatching {
                context.startActivity(ActionPermissionActivity.intent(context, type, slots))
            }.isSuccess
            if (opened) {
                val message = if (type == "query_calendar") {
                    "正在请求日历读取权限；日程内容只在手机本地处理。"
                } else {
                    "仅在执行本次操作时请求所需权限；拒绝后会安全降级。"
                }
                return Result(true, message)
            }
        }
        return when (type) {
        "send_message" -> sendMessage(slots)
        "make_call" -> makeCall(slots)
        "open_app" -> openApp(slots)
        "web_search" -> webSearch(slots["query"].orEmpty())
        "play_music" -> playMusic(slots["query"].orEmpty())
        "navigate" -> navigate(slots)
        "send_email" -> sendEmail(slots)
        "create_reminder" -> createReminder(slots)
        "open_camera" -> openCamera()
        "show_alarms" -> showAlarms()
        "start_timer" -> startTimer(slots)
        "add_calendar" -> addCalendar(slots)
        "query_calendar" -> queryCalendar(slots)
        "open_setting" -> openSetting(slots["setting"].orEmpty())
        "toggle_flashlight" -> toggleFlashlight(slots["state"].orEmpty())
        "take_note" -> Result(true, "（记笔记落地待接入本地笔记）：${slots["content"].orEmpty()}")
        "run_agent" -> runAgent(slots)
        "click_ui_target" -> clickUiTarget(slots)
        else -> Result(false, "暂不支持的动作类型：$type")
        }
    }

    // ---- 发短信 ----
    private fun sendMessage(slots: Map<String, String>): Result {
        val recipient = slots["recipient"].orEmpty()
        val content = slots["content"].orEmpty()
        if (recipient.isBlank()) return Result(false, "缺少收件人")
        val number = resolveNumber(recipient)

        if (number != null && hasPermission(Manifest.permission.SEND_SMS)) {
            return runCatching {
                smsManager().sendTextMessage(number, null, content, null, null)
                // sendTextMessage 是异步提交，接口返回不等于基站最终送达；禁止同步宣称“已发”。
                Result(true, "已把短信提交给系统发送（收件人：$recipient / $number）：$content")
            }.getOrElse { Result(false, "发送失败：${it.message}") }
        }
        // 兜底：打开短信 App 预填，由用户点发送（不需危险权限、不误发）
        val uri = Uri.parse("smsto:" + (number ?: ""))
        val intent = Intent(Intent.ACTION_SENDTO, uri).apply {
            putExtra("sms_body", content)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return launch(intent, "已打开短信编辑（收件人：$recipient）")
    }

    // ---- 打电话 ----
    private fun makeCall(slots: Map<String, String>): Result {
        val contact = slots["contact"].orEmpty()
        if (contact.isBlank()) return Result(false, "缺少联系人")
        val number = resolveNumber(contact)
        if (number == null) {
            val dial = Intent(Intent.ACTION_DIAL).apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
            return launch(dial, "通讯录没找到 $contact，已打开拨号盘")
        }
        val action = if (hasPermission(Manifest.permission.CALL_PHONE))
            Intent.ACTION_CALL else Intent.ACTION_DIAL
        val intent = Intent(action, Uri.parse("tel:$number")).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return launch(intent, "正在拨打 $contact（$number）")
    }

    // ---- 开应用 / 无障碍编排：交给 AppAutomator（中文名映射 + 预设编排脚本）----
    private fun openApp(slots: Map<String, String>): Result {
        val name = slots["app_name"].orEmpty()
        // ③ 大脑已用"已装清单+世界知识"解析出真实包名 → 直接拉起，最稳（治 b站→bilibili、新装App）
        slots["package"]?.takeIf { it.isNotBlank() }?.let { pkg ->
            if (AppAutomator.launchByPackage(context, pkg))
                return Result(true, "已打开 ${name.ifBlank { pkg }}")
        }
        // 兜底：大脑没给包名(或启动失败) → 安卓端别名表/标签匹配
        if (name.isBlank()) return Result(false, "缺少应用名")
        val msg = AppAutomator.handle(context, name)
        return Result(!msg.startsWith("没找到") && !msg.startsWith("需要先开启"), msg)
    }

    // ---- ③ GUI-agent：大脑判定为"App内多步任务"→ 无障碍边看边操作（带护栏，异步执行）----
    private fun runAgent(slots: Map<String, String>): Result {
        val goal = slots["goal"].orEmpty()
        if (goal.isBlank()) return Result(false, "缺少任务描述")
        // app 槽位（大脑解析的目标App，不管词序）优先；启动 agent 循环，进度经广播实时显示
        val msg = AppAutomator.runAgentGoal(goal, slots["app"])
        return Result(!msg.startsWith("需要先开启"), msg)
    }

    /** 注视只消解低风险“打开这个”；真正点击时仍在当前无障碍树中查找并再次做安全校验。 */
    private fun clickUiTarget(slots: Map<String, String>): Result {
        val target = slots["target"].orEmpty().trim()
        if (target.isBlank()) return Result(false, "没有稳定识别到你正在看的目标，请再看一会儿。")
        if (!GazeUiActionPolicy.isSafeTarget(target)) {
            return Result(false, "这个目标可能产生重要操作，请直接说出名称并明确确认。")
        }
        val acc = LingxiAccessibilityService.instance
            ?: return Result(false, "需要先开启灵犀无障碍，才能打开你正在看的内容。")
        val clicked = acc.activateGazeTargetByText(target)
        return if (clicked) Result(true, "已根据当前指代打开：$target")
        else Result(false, "页面已经发生变化，没找到“$target”，请重新看向目标再说一次。")
    }

    // ---- 网页搜索 ----
    private fun webSearch(query: String): Result {
        if (query.isBlank()) return Result(false, "缺少搜索关键词")
        val intent = Intent(Intent.ACTION_WEB_SEARCH).apply {
            putExtra("query", query)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        if (intent.resolveActivity(context.packageManager) != null) {
            return launch(intent, "已为你搜索：$query")
        }
        val view = Intent(Intent.ACTION_VIEW,
            Uri.parse("https://www.bing.com/search?q=" + Uri.encode(query)))
            .apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
        return launch(view, "已为你搜索：$query")
    }

    // ---- 导航：系统地图能力（国行友好：百度地图 scheme 优先 → geo: → Google → 高德网页兜底）----
    // 按序尝试候选 URI，第一个被已装地图 App 接住的就用；绕开国内打不开的 Google Maps 网页。
    private fun navigate(slots: Map<String, String>): Result {
        val dest = slots["destination"].orEmpty()
        if (dest.isBlank()) return Result(false, "缺少目的地")
        val origin = slots["origin"].orEmpty()
        val mode = travelMode(slots["mode"].orEmpty())
        val from = if (origin.isNotBlank()) "$origin → " else ""

        val candidates = buildList {
            // 1) 百度地图路线规划（国行常见；省略 origin = 从当前位置；名称直填，无需坐标）
            val bd = StringBuilder("baidumap://map/direction?destination=").append(Uri.encode(dest))
                .append("&mode=").append(baiduMode(mode)).append("&src=com.lingxi.mobile")
            if (origin.isNotBlank()) bd.append("&origin=").append(Uri.encode(origin))
            add(bd.toString())
            // 2) geo: 目的地（任意地图 App 都能接住，含高德/百度）
            add("geo:0,0?q=" + Uri.encode(dest))
            // 3) Google 起步导航（海外/有 Google Maps 时；国行通常没有，自动跳过）
            add("google.navigation:q=" + Uri.encode(dest))
        }
        for (uri in candidates) {
            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(uri))
                .apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
            if (intent.resolveActivity(context.packageManager) != null)
                return launch(intent, if (origin.isNotBlank()) "已规划${from}$dest 路线"
                                      else "正在导航到 $dest")
        }
        // 4) 网页兜底：高德网页（国内可访问，不像 Google Maps 被墙）
        val web = Intent(Intent.ACTION_VIEW,
            Uri.parse("https://uri.amap.com/search?keyword=" + Uri.encode(dest) + "&src=lingxi"))
            .apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
        return launch(web, "已用网页地图打开 $dest")
    }

    /** Google travelmode → 百度地图 mode（百度用 riding 表示骑行）。 */
    private fun baiduMode(m: String): String = when (m) {
        "transit" -> "transit"
        "walking" -> "walking"
        "bicycling" -> "riding"
        else -> "driving"
    }

    /** 中文出行方式 → Google Maps travelmode；无法识别返回空串（默认驾车）。 */
    private fun travelMode(m: String): String = when {
        m.isBlank() -> ""
        m.contains("步行") || m.contains("走路") || m.contains("走") -> "walking"
        m.contains("地铁") || m.contains("公交") || m.contains("公共交通") || m.contains("transit") -> "transit"
        m.contains("骑") || m.contains("单车") || m.contains("自行车") -> "bicycling"
        m.contains("驾") || m.contains("开车") || m.contains("自驾") || m.contains("车") -> "driving"
        else -> ""
    }

    // ---- 发邮件：ACTION_SENDTO mailto: 打开草稿（用户审阅后手动发，低风险不自动发）----
    private fun sendEmail(slots: Map<String, String>): Result {
        val to = slots["to"].orEmpty()
        val subject = slots["subject"].orEmpty()
        var body = slots["body"].orEmpty()
        val isEmail = to.contains("@")
        // 收件人是人名(非邮箱)时无法直填地址栏 → 写进正文提示，仍打开草稿让用户补全
        if (to.isNotBlank() && !isEmail) body = "【收件人：$to】\n$body".trim()
        val intent = Intent(Intent.ACTION_SENDTO,
            Uri.parse("mailto:" + if (isEmail) Uri.encode(to) else "")).apply {
            if (subject.isNotBlank()) putExtra(Intent.EXTRA_SUBJECT, subject)
            if (body.isNotBlank()) putExtra(Intent.EXTRA_TEXT, body)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        if (intent.resolveActivity(context.packageManager) != null)
            return launch(intent, "已打开邮件草稿" + if (to.isNotBlank()) "（收件人：$to）" else "")
        return Result(false, "没有可用的邮件应用")
    }

    // ---- 播放音乐（MVP 简化：交给系统媒体搜索）----
    private fun playMusic(query: String): Result {
        val intent = Intent(MediaSearch).apply {
            putExtra("query", query)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return if (intent.resolveActivity(context.packageManager) != null)
            launch(intent, "正在播放：${query.ifBlank { "音乐" }}")
        else Result(false, "没有可用的音乐应用")
    }

    // ---- 系统确定性能力：相机 / 闹钟列表 / 计时器 / 日历 / 设置直达 / 手电 ----
    private fun openCamera(): Result {
        val intent = Intent(MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA)
            .apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
        return launch(intent, "已打开相机")
    }

    private fun showAlarms(): Result {
        val intent = Intent(AlarmClock.ACTION_SHOW_ALARMS)
            .apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
        return launch(intent, "已打开闹钟列表")
    }

    private fun startTimer(slots: Map<String, String>): Result {
        val seconds = slots["seconds"]?.toIntOrNull()?.coerceAtLeast(1) ?: 60
        val message = slots["message"].orEmpty().ifBlank { "灵犀计时器" }
        val intent = Intent(AlarmClock.ACTION_SET_TIMER).apply {
            putExtra(AlarmClock.EXTRA_LENGTH, seconds)
            putExtra(AlarmClock.EXTRA_MESSAGE, message)
            putExtra(AlarmClock.EXTRA_SKIP_UI, false)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return launch(intent, "已设置计时器 ${seconds} 秒")
    }

    private fun addCalendar(slots: Map<String, String>): Result {
        val timeText = slots["time_text"].orEmpty().ifBlank { slots["title"].orEmpty() }
        // 标题剥掉日期/时间词（治"明天下午三点开会"整句当标题）
        val title = stripTimeWords(slots["title"].orEmpty()).ifBlank { "灵犀日程" }
        val begin = parseEventTime(timeText)  // 解析"明天下午三点"→起始毫秒，解析不到返回 null
        val intent = Intent(Intent.ACTION_INSERT).apply {
            data = CalendarContract.Events.CONTENT_URI
            putExtra(CalendarContract.Events.TITLE, title)
            if (begin != null) {
                putExtra(CalendarContract.EXTRA_EVENT_BEGIN_TIME, begin)
                putExtra(CalendarContract.EXTRA_EVENT_END_TIME, begin + 60 * 60 * 1000)  // 默认 1 小时
            }
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        val whenStr = if (begin != null)
            " " + java.text.SimpleDateFormat("M月d日 HH:mm", java.util.Locale.CHINA).format(java.util.Date(begin))
        else ""
        return launch(intent, "已打开日历编辑：$title$whenStr")
    }

    /**
     * 只在端侧读取 CalendarProvider；标题、时间等日程内容不会回传大脑服务。
     * 权限由 ActionPermissionActivity 首次按需申请，拒绝后明确停止而不是打开别的页面伪装成功。
     */
    private fun queryCalendar(slots: Map<String, String>): Result {
        if (!hasPermission(Manifest.permission.READ_CALENDAR)) {
            return Result(false, "未获得日历读取权限，本次没有读取任何日程。")
        }
        val range = CalendarQueryPolicy.range(
            requestedOffset = slots["day_offset"]?.toIntOrNull() ?: 0,
            requestedDays = slots["days"]?.toIntOrNull() ?: 1,
        )
        val projection = arrayOf(
            CalendarContract.Instances.TITLE,
            CalendarContract.Instances.BEGIN,
            CalendarContract.Instances.END,
            CalendarContract.Instances.ALL_DAY,
        )
        return runCatching {
            val events = mutableListOf<CalendarQueryPolicy.Event>()
            CalendarContract.Instances.query(
                context.contentResolver,
                projection,
                range.startMillis,
                range.endMillis,
            )?.use { cursor ->
                val titleIndex = cursor.getColumnIndexOrThrow(CalendarContract.Instances.TITLE)
                val beginIndex = cursor.getColumnIndexOrThrow(CalendarContract.Instances.BEGIN)
                val endIndex = cursor.getColumnIndexOrThrow(CalendarContract.Instances.END)
                val allDayIndex = cursor.getColumnIndexOrThrow(CalendarContract.Instances.ALL_DAY)
                while (cursor.moveToNext()) {
                    events += CalendarQueryPolicy.Event(
                        title = cursor.getString(titleIndex).orEmpty(),
                        beginMillis = cursor.getLong(beginIndex),
                        endMillis = cursor.getLong(endIndex),
                        allDay = cursor.getInt(allDayIndex) != 0,
                    )
                }
            }
            Result(true, CalendarQueryPolicy.summarize(events, range))
        }.getOrElse {
            Result(false, "日程读取失败：${it.message ?: "系统日历不可用"}")
        }
    }

    /** 自然语言日期偏移："今天/明天/后天/大后天/今晚/明晚" → 相对今天天数；找不到返回 null。 */
    private fun dayOffset(s: String): Int? = when {
        s.contains("大后天") -> 3
        s.contains("后天") -> 2
        s.contains("明天") || s.contains("明晚") -> 1
        s.contains("今天") || s.contains("今晚") || s.contains("今晨") -> 0
        else -> null
    }

    /** 把"明天下午三点"等解析成事件起始毫秒：日期偏移 ⊕ parseClock。无日期无时间→null；只有日期→默认 09:00；
     *  只有时间且已过当天→顺延到明天。 */
    private fun parseEventTime(text: String): Long? {
        val off = dayOffset(text)
        val (h, m) = parseClock(text)
        if (off == null && h < 0) return null
        val cal = java.util.Calendar.getInstance().apply {
            if (off != null) add(java.util.Calendar.DAY_OF_YEAR, off)
            if (h >= 0) {
                set(java.util.Calendar.HOUR_OF_DAY, h); set(java.util.Calendar.MINUTE, m)
            } else {
                set(java.util.Calendar.HOUR_OF_DAY, 9); set(java.util.Calendar.MINUTE, 0)
            }
            set(java.util.Calendar.SECOND, 0); set(java.util.Calendar.MILLISECOND, 0)
        }
        if (off == null && h >= 0 && cal.timeInMillis < System.currentTimeMillis()) {
            cal.add(java.util.Calendar.DAY_OF_YEAR, 1)  // 没说哪天且时间已过→明天
        }
        return cal.timeInMillis
    }

    /** 标题去掉日期/时间词，留下事件本体（"明天下午三点开会"→"开会"）。 */
    private fun stripTimeWords(s: String): String {
        var t = s
        t = t.replace(Regex("""大后天|后天|明天|今天|今晚|明晚|今晨"""), "")
        t = t.replace(Regex("""凌晨|早晨|早上|上午|中午|下午|傍晚|晚上"""), "")
        t = t.replace(Regex("""\d{1,2}[:：]\d{1,2}"""), "")
        t = t.replace(Regex("""[0-9一二两三四五六七八九十]{1,3}\s*点(\s*半|\s*[0-9一二三四五六七八九十]{1,3}\s*分?)?"""), "")
        t = t.replace(Regex("""[0-9一二两三四五六七八九十]{1,3}\s*分"""), "")
        return t.trim().trim('，', '。', '、', ' ', '的')
    }

    private fun openSetting(setting: String): Result {
        val action = when (setting) {
            "wifi" -> Settings.ACTION_WIFI_SETTINGS
            "bluetooth" -> Settings.ACTION_BLUETOOTH_SETTINGS
            "sound" -> Settings.ACTION_SOUND_SETTINGS
            "display" -> Settings.ACTION_DISPLAY_SETTINGS
            "location" -> Settings.ACTION_LOCATION_SOURCE_SETTINGS
            "battery" -> Settings.ACTION_BATTERY_SAVER_SETTINGS
            "accessibility" -> Settings.ACTION_ACCESSIBILITY_SETTINGS
            "network" -> Settings.ACTION_WIRELESS_SETTINGS
            else -> Settings.ACTION_SETTINGS
        }
        return launch(Intent(action).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK), "已打开系统设置")
    }

    private fun toggleFlashlight(state: String): Result {
        if (!hasPermission(Manifest.permission.CAMERA)) {
            return Result(false, "需要相机权限才能控制手电筒")
        }
        val on = state != "off"
        val cm = context.getSystemService(CameraManager::class.java)
        val cameraId = cm.cameraIdList.firstOrNull { id ->
            cm.getCameraCharacteristics(id)
                .get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        } ?: return Result(false, "这台设备没有可用手电筒")
        return runCatching {
            cm.setTorchMode(cameraId, on)
            Result(true, if (on) "已打开手电筒" else "已关闭手电筒")
        }.getOrElse { Result(false, "手电筒控制失败：${it.message}") }
    }

    // ---- 设闹钟/提醒：有时间→系统 ACTION_SET_ALARM 一行精确订；无时间→定时器兜底 ----
    private fun createReminder(slots: Map<String, String>): Result {
        val content = slots["content"].orEmpty()
        val (h, m) = parseClock(slots["time"].orEmpty())
        if (h >= 0) {
            val intent = Intent(AlarmClock.ACTION_SET_ALARM).apply {
                putExtra(AlarmClock.EXTRA_HOUR, h)
                putExtra(AlarmClock.EXTRA_MINUTES, m)
                if (content.isNotBlank()) putExtra(AlarmClock.EXTRA_MESSAGE, content)
                putExtra(AlarmClock.EXTRA_SKIP_UI, false)  // 跳到时钟 App 让用户看到
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            return launch(intent, "已设闹钟 %02d:%02d %s".format(h, m, content).trim())
        }
        val intent = Intent(AlarmClock.ACTION_SET_TIMER).apply {
            putExtra(AlarmClock.EXTRA_MESSAGE, content)
            putExtra(AlarmClock.EXTRA_LENGTH, 60)
            putExtra(AlarmClock.EXTRA_SKIP_UI, false)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        return launch(intent, "已设置提醒：$content")
    }

    private val cnDigit = mapOf(
        '零' to 0, '〇' to 0, '一' to 1, '二' to 2, '两' to 2, '三' to 3, '四' to 4,
        '五' to 5, '六' to 6, '七' to 7, '八' to 8, '九' to 9,
    )

    /** 中文/阿拉伯数字串 → int（支持 三、十、十一、二十、二十五）；失败返回 -1。 */
    private fun cnNum(raw: String): Int {
        val s = raw.trim()
        if (s.isEmpty()) return -1
        s.toIntOrNull()?.let { return it }               // 阿拉伯数字直接用
        if (s == "十") return 10
        val i = s.indexOf('十')
        if (i >= 0) {                                    // 含"十"：X十Y / 十X / X十
            val tens = if (i == 0) 1 else (cnDigit[s[i - 1]] ?: return -1)
            val ones = if (i == s.length - 1) 0 else (cnDigit[s[i + 1]] ?: return -1)
            return tens * 10 + ones
        }
        return cnDigit[s[0]] ?: -1                        // 单个中文数字
    }

    /** 解析时间："下午三点半/晚上8点半/十一点/8:30/20:00/早上8点" → (时,分)；解析不到返回 (-1,-1)。 */
    private fun parseClock(s: String): Pair<Int, Int> {
        if (s.isBlank()) return -1 to -1
        val pm = s.contains("下午") || s.contains("晚上") || s.contains("傍晚") || s.contains("中午")
        val am = s.contains("上午") || s.contains("早上") || s.contains("早晨") || s.contains("凌晨")
        fun adjust(h0: Int): Int {
            var h = h0
            if (pm && h < 12) h += 12
            if (am && h == 12) h = 0
            return h
        }
        // 1) 阿拉伯 时:分
        Regex("""(\d{1,2})[:：](\d{1,2})""").find(s)?.let {
            return adjust(it.groupValues[1].toInt()) to it.groupValues[2].toInt()
        }
        // 2) X点[Y|半]：X/Y 支持中文或阿拉伯数字（治"三点半"被当倒计时的 bug）
        Regex("""([0-9一二两三四五六七八九十]{1,3})\s*点\s*(半|[0-9一二三四五六七八九十]{1,3})?""").find(s)?.let {
            val h = cnNum(it.groupValues[1]); if (h < 0) return -1 to -1
            val mp = it.groupValues[2]
            val mm = when {
                mp == "半" -> 30
                mp.isBlank() -> 0
                else -> cnNum(mp).let { x -> if (x in 0..59) x else 0 }
            }
            return adjust(h) to mm
        }
        return -1 to -1
    }

    // ---- 工具 ----
    private fun launch(intent: Intent, okMsg: String): Result =
        runCatching { context.startActivity(intent); Result(true, okMsg) }
            .getOrElse { Result(false, "无法执行：${it.message}") }

    private fun hasPermission(p: String): Boolean =
        EffectivePermission.isGranted(context, p)

    @Suppress("DEPRECATION")
    private fun smsManager(): SmsManager =
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S)
            context.getSystemService(SmsManager::class.java)
        else SmsManager.getDefault()

    /** 收件人若本身就是号码直接用；否则查通讯录。需 READ_CONTACTS，无权限/查不到返回 null。 */
    private fun resolveNumber(name: String): String? {
        if (name.any { it.isDigit() } && name.all { it.isDigit() || it in "+-() " })
            return name.filter { it.isDigit() || it == '+' }
        if (!hasPermission(Manifest.permission.READ_CONTACTS)) return null
        val uri = Uri.withAppendedPath(
            ContactsContract.CommonDataKinds.Phone.CONTENT_FILTER_URI, Uri.encode(name))
        context.contentResolver.query(
            uri,
            arrayOf(ContactsContract.CommonDataKinds.Phone.NUMBER),
            null, null, null
        )?.use { c ->
            if (c.moveToFirst()) return c.getString(0)?.filter { it.isDigit() || it == '+' }
        }
        return null
    }

    companion object {
        private const val MediaSearch = "android.media.action.MEDIA_PLAY_FROM_SEARCH"
    }
}
