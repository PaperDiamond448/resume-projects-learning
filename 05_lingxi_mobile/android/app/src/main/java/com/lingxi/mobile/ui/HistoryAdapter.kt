package com.lingxi.mobile.ui

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.lingxi.mobile.R
import com.lingxi.mobile.data.ConversationRecord

sealed class HistoryListItem {
    data class Header(val label: String) : HistoryListItem()
    data class Row(val record: ConversationRecord, val subtitle: String) : HistoryListItem()
}

/** 历史对话面板：按日期分组（今天/昨天/7天内…），点行=跳进对应聊天内容。 */
class HistoryAdapter(
    private val items: List<HistoryListItem>,
    private val onRowClick: (ConversationRecord) -> Unit,
) : RecyclerView.Adapter<RecyclerView.ViewHolder>() {

    override fun getItemCount() = items.size

    override fun getItemViewType(position: Int) =
        if (items[position] is HistoryListItem.Header) TYPE_HEADER else TYPE_ROW

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return if (viewType == TYPE_HEADER) {
            HeaderHolder(inflater.inflate(R.layout.item_history_header, parent, false))
        } else {
            RowHolder(inflater.inflate(R.layout.item_history_row, parent, false))
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        when (val item = items[position]) {
            is HistoryListItem.Header -> (holder as HeaderHolder).bind(item)
            is HistoryListItem.Row -> (holder as RowHolder).bind(item, onRowClick)
        }
    }

    private class HeaderHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val text = view as TextView
        fun bind(item: HistoryListItem.Header) {
            text.text = item.label
        }
    }

    private class RowHolder(view: View) : RecyclerView.ViewHolder(view) {
        private val title: TextView = view.findViewById(R.id.rowTitle)
        private val subtitle: TextView = view.findViewById(R.id.rowSubtitle)
        fun bind(item: HistoryListItem.Row, onRowClick: (ConversationRecord) -> Unit) {
            title.text = item.record.title
            subtitle.text = item.subtitle
            itemView.setOnClickListener { onRowClick(item.record) }
        }
    }

    private companion object {
        const val TYPE_HEADER = 0
        const val TYPE_ROW = 1
    }
}
