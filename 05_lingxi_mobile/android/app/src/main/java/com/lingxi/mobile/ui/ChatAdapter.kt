package com.lingxi.mobile.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.lingxi.mobile.R
import com.lingxi.mobile.data.ConversationMessage
import com.lingxi.mobile.data.ConversationStore
import com.lingxi.mobile.data.MessageCard

/**
 * 聊天消息列表：气泡（用户靠右／灵犀靠左）+ 可选卡片（单张=单App结果，两张=比价）。
 * 卡片点击只回调包名，真正的"跳回前台"由 MainActivity 用 AppAutomator.launchByPackage 处理。
 */
class ChatAdapter(
    private val onCardClick: (MessageCard) -> Unit,
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    private val items = mutableListOf<ConversationMessage>()

    fun submit(messages: List<ConversationMessage>) {
        items.clear()
        items.addAll(messages)
        notifyDataSetChanged()
    }

    override fun getItemCount() = items.size

    override fun getItemViewType(position: Int): Int =
        if (items[position].role == ConversationStore.ROLE_USER) TYPE_USER else TYPE_ASSISTANT

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return if (viewType == TYPE_USER) {
            UserHolder(inflater.inflate(R.layout.item_chat_user, parent, false))
        } else {
            AssistantHolder(inflater.inflate(R.layout.item_chat_assistant, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        val message = items[position]
        when (holder) {
            is UserHolder -> holder.bind(message)
            is AssistantHolder -> holder.bind(message, onCardClick)
        }
    }

    private class UserHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val text: TextView = view.findViewById(R.id.messageText)
        fun bind(message: ConversationMessage) {
            text.text = message.text
        }
    }

    private class AssistantHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val text: TextView = view.findViewById(R.id.messageText)
        private val cardRow: LinearLayout = view.findViewById(R.id.cardRow)
        private val card1: LinearLayout = view.findViewById(R.id.card1)
        private val card1Image: ImageView = view.findViewById(R.id.card1Image)
        private val card1Title: TextView = view.findViewById(R.id.card1Title)
        private val card1Subtitle: TextView = view.findViewById(R.id.card1Subtitle)
        private val card2: LinearLayout = view.findViewById(R.id.card2)
        private val card2Image: ImageView = view.findViewById(R.id.card2Image)
        private val card2Title: TextView = view.findViewById(R.id.card2Title)
        private val card2Subtitle: TextView = view.findViewById(R.id.card2Subtitle)

        fun bind(message: ConversationMessage, onCardClick: (MessageCard) -> Unit) {
            text.text = message.text
            text.visibility = if (message.text.isBlank()) View.GONE else View.VISIBLE

            val cards = message.cards.orEmpty()
            cardRow.visibility = if (cards.isEmpty()) View.GONE else View.VISIBLE
            bindCard(card1, card1Image, card1Title, card1Subtitle, cards.getOrNull(0), onCardClick)
            bindCard(card2, card2Image, card2Title, card2Subtitle, cards.getOrNull(1), onCardClick)
        }

        private fun bindCard(
            root: LinearLayout,
            image: ImageView,
            title: TextView,
            subtitle: TextView,
            card: MessageCard?,
            onCardClick: (MessageCard) -> Unit,
        ) {
            if (card == null) {
                root.visibility = View.GONE
                return
            }
            root.visibility = View.VISIBLE
            title.text = card.title
            subtitle.text = card.subtitle
            val bitmap = card.thumbnailPath?.takeIf { it.isNotBlank() }
                ?.let { decodeSampledBitmap(it, image.resources.displayMetrics.density * 96f) }
            image.setImageBitmap(bitmap)
            root.setOnClickListener { onCardClick(card) }
        }

        /** 缩略图来自真机截图（可能上千像素高），先量尺寸再降采样，别在滚动时全分辨率解码卡帧。 */
        private fun decodeSampledBitmap(path: String, reqHeightPx: Float): Bitmap? {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(path, bounds)
            if (bounds.outHeight <= 0) return null
            var sample = 1
            while (bounds.outHeight / (sample * 2) >= reqHeightPx) sample *= 2
            return BitmapFactory.decodeFile(path, BitmapFactory.Options().apply { inSampleSize = sample })
        }
    }

    private companion object {
        const val TYPE_USER = 0
        const val TYPE_ASSISTANT = 1
    }
}
