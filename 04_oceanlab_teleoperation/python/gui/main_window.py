#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
主窗口GUI实现
"""

import sys
import os
import time
from PyQt5.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QLabel, QPushButton, QSlider, QComboBox,
                             QTabWidget, QGroupBox, QFormLayout, QSpinBox,
                             QDoubleSpinBox, QStatusBar, QSplitter, QAction,
                             QFileDialog, QMessageBox, QDialog, QLineEdit,
                             QFrame, QShortcut, QGraphicsDropShadowEffect,
                             QTableWidget, QHeaderView, QTableWidgetItem,
                             QDockWidget)
from PyQt5.QtCore import Qt, QTimer, pyqtSlot, pyqtSignal
from PyQt5.QtGui import QPixmap, QImage, QColor, QKeySequence, QIcon, QFont, QPalette
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from PyQt5.QtWidgets import QApplication

# 导入matplotlib并配置
import matplotlib
matplotlib.use("Qt5Agg")
# 添加中文字体支持
matplotlib.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'SimSun', 'sans-serif']
matplotlib.rcParams['axes.unicode_minus'] = False  # 正确显示负号

from remote.server import Server
from remote.auth import AuthManager
from simulator.renderer import PoolRenderer
from matlab_interface import MatlabInterface
from gui.connection_dialog import ConnectionDialog  # 导入连接配置对话框

class LoginDialog(QDialog):
    """登录对话框"""
    
    def __init__(self, auth_manager, parent=None):
        """初始化登录对话框"""
        print("开始初始化登录对话框...")
        super().__init__(parent)
        self.auth_manager = auth_manager
        print("调用setup_ui...")
        self.setup_ui()
        print("setup_ui完成")
        
        # 使用标准窗口样式，临时禁用无边框窗口
        print("使用标准窗口样式...")
        # self.setWindowFlags(Qt.FramelessWindowHint)
        # self.setAttribute(Qt.WA_TranslucentBackground)
        
        # 临时禁用阴影效果
        print("跳过阴影效果...")
        # self.shadow = QGraphicsDropShadowEffect(self)
        # self.shadow.setBlurRadius(20)
        # self.shadow.setColor(QColor(0, 0, 0, 60))
        # self.shadow.setOffset(0, 0)
        # self.main_frame.setGraphicsEffect(self.shadow)
        print("使用标准窗口设置完成")
        
        # 支持按ESC关闭窗口
        print("设置快捷键...")
        self.shortcut_esc = QShortcut(QKeySequence(Qt.Key_Escape), self)
        self.shortcut_esc.activated.connect(self.reject)
        
        # 支持按Enter登录
        self.shortcut_enter = QShortcut(QKeySequence(Qt.Key_Return), self)
        self.shortcut_enter.activated.connect(self.on_login)
        print("登录对话框初始化完成")
    
    def setup_ui(self):
        """设置UI"""
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 20, 20, 20)  # 外部边距
        
        # 创建主框架
        self.main_frame = QFrame()
        self.main_frame.setObjectName("mainFrame")
        self.main_frame.setStyleSheet("""
            #mainFrame {
                background-color: white;
                border-radius: 10px;
                border: 1px solid #e0e0e0;
            }
            QLabel {
                color: #333333;
            }
            QLabel#titleLabel {
                font-size: 18px;
                font-weight: bold;
                color: #1976D2;
            }
            QLineEdit {
                padding: 8px;
                border: 1px solid #e0e0e0;
                border-radius: 4px;
                background-color: #f5f5f5;
            }
            QLineEdit:focus {
                border: 1px solid #1976D2;
                background-color: white;
            }
            QPushButton {
                padding: 8px 16px;
                border-radius: 4px;
                border: none;
                background-color: #e0e0e0;
                color: #333333;
            }
            QPushButton#loginButton {
                background-color: #1976D2;
                color: white;
            }
            QPushButton#loginButton:hover {
                background-color: #1565C0;
            }
            QPushButton#loginButton:pressed {
                background-color: #0D47A1;
            }
            QPushButton#loginButton:disabled {
                background-color: #BBDEFB;
            }
            QPushButton#cancelButton:hover {
                background-color: #E0E0E0;
            }
            QPushButton#cancelButton:pressed {
                background-color: #BDBDBD;
            }
        """)
        
        frame_layout = QVBoxLayout(self.main_frame)
        frame_layout.setSpacing(15)
        frame_layout.setContentsMargins(20, 20, 20, 20)  # 内部边距
        
        # 标题栏
        title_layout = QHBoxLayout()
        
        # 标题
        title_label = QLabel("OceanLab-Python 登录")
        title_label.setObjectName("titleLabel")
        title_layout.addWidget(title_label)
        
        # 关闭按钮
        close_button = QPushButton("×")
        close_button.setObjectName("closeButton")
        close_button.setFixedSize(20, 20)
        close_button.setStyleSheet("""
            QPushButton {
                background-color: transparent;
                border-radius: 10px;
                font-size: 16px;
                color: #757575;
            }
            QPushButton:hover {
                background-color: #ffcdd2;
                color: #d32f2f;
            }
        """)
        close_button.clicked.connect(self.reject)
        title_layout.addWidget(close_button)
        
        frame_layout.addLayout(title_layout)
        
        # 分隔线
        separator = QFrame()
        separator.setFrameShape(QFrame.HLine)
        separator.setFrameShadow(QFrame.Sunken)
        separator.setStyleSheet("background-color: #e0e0e0;")
        frame_layout.addWidget(separator)
        
        # 欢迎信息
        welcome_label = QLabel("欢迎使用海洋信息实验教学平台")
        welcome_label.setAlignment(Qt.AlignCenter)
        frame_layout.addWidget(welcome_label)
        
        # 说明标签
        info_label = QLabel("请输入从预约系统获取的实验密钥进行登录")
        info_label.setAlignment(Qt.AlignCenter)
        frame_layout.addWidget(info_label)
        
        # 实验密钥输入
        key_layout = QVBoxLayout()
        key_layout.setSpacing(5)
        
        key_label = QLabel("实验密钥:")
        key_layout.addWidget(key_label)
        
        self.key_input = QLineEdit()
        self.key_input.setPlaceholderText("输入8位大写字母和数字组合的密钥")
        key_layout.addWidget(self.key_input)
        
        frame_layout.addLayout(key_layout)
        
        # 登录状态标签
        self.status_label = QLabel("")
        self.status_label.setAlignment(Qt.AlignCenter)
        self.status_label.setStyleSheet("color: #F44336;")  # 默认错误消息为红色
        frame_layout.addWidget(self.status_label)
        
        # 按钮
        button_layout = QHBoxLayout()
        button_layout.setSpacing(10)
        
        self.login_button = QPushButton("登录")
        self.login_button.setObjectName("loginButton")
        self.login_button.clicked.connect(self.on_login)
        button_layout.addWidget(self.login_button)
        
        self.cancel_button = QPushButton("取消")
        self.cancel_button.setObjectName("cancelButton")
        self.cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_button)
        
        frame_layout.addLayout(button_layout)
        
        # 底部链接
        bottom_layout = QHBoxLayout()
        
        help_link = QLabel("<a href='#'>需要帮助?</a>")
        help_link.setOpenExternalLinks(False)
        help_link.linkActivated.connect(self.show_help)
        help_link.setStyleSheet("color: #1976D2;")
        bottom_layout.addWidget(help_link)
        
        system_link = QLabel("<a href='#'>访问预约系统</a>")
        system_link.setOpenExternalLinks(False)
        system_link.linkActivated.connect(self.open_reservation_system)
        system_link.setStyleSheet("color: #1976D2;")
        bottom_layout.addWidget(system_link, 0, Qt.AlignRight)
        
        frame_layout.addLayout(bottom_layout)
        
        main_layout.addWidget(self.main_frame)
    
    def on_login(self):
        """登录按钮点击事件"""
        key = self.key_input.text().strip()
        if not key:
            self.status_label.setText("请输入实验密钥")
            self.key_input.setFocus()
            return
        
        # 禁用登录按钮，防止重复点击
        self.login_button.setEnabled(False)
        self.status_label.setStyleSheet("color: #2196F3;")  # 加载状态为蓝色
        self.status_label.setText("正在验证密钥...")
        
        # 添加简单的动画效果，模拟验证过程
        QApplication.processEvents()
        time.sleep(0.5)  # 短暂延迟增加用户体验
        
        # 验证密钥
        if self.auth_manager.validate_key(key):
            # 检查是否有预约的实验池
            reserved_pool_id = self.auth_manager.get_reserved_pool_id()
            
            self.status_label.setStyleSheet("color: #4CAF50;")  # 成功消息为绿色
            if reserved_pool_id:
                self.status_label.setText(f"登录成功！您已预约实验池 {reserved_pool_id}")
            else:
                self.status_label.setText("登录成功！")
            
            # 延迟关闭对话框
            QTimer.singleShot(1000, self.accept)
        else:
            self.status_label.setStyleSheet("color: #F44336;")  # 错误消息为红色
            self.status_label.setText("实验密钥无效，请重试")
            self.login_button.setEnabled(True)
            self.key_input.setFocus()
            self.key_input.selectAll()
    
    def show_help(self):
        """显示帮助信息"""
        QMessageBox.information(
            self,
            "登录帮助",
            "实验密钥是预约实验池时生成的8位字符串，由大写字母和数字组成。\n\n"
            "如果您尚未预约实验池，请先访问预约系统进行预约。\n\n"
            "如果您已经预约但忘记了密钥，请联系管理员或重新预约。"
        )
    
    def open_reservation_system(self):
        """打开预约系统"""
        # 这里可以使用QDesktopServices打开预约系统URL
        # 现在只显示一个消息框作为示例
        QMessageBox.information(
            self,
            "预约系统",
            "预约系统访问地址：https://localhost:3002/\n\n"
            "请在预约系统中预约实验池，获取实验密钥后返回此系统登录。"
        )
    
    # 添加鼠标事件处理，支持拖动无边框窗口
    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.drag_position = event.globalPos() - self.frameGeometry().topLeft()
            event.accept()
    
    def mouseMoveEvent(self, event):
        if event.buttons() == Qt.LeftButton:
            self.move(event.globalPos() - self.drag_position)
            event.accept()

class MainWindow(QMainWindow):
    """主窗口"""
    
    def __init__(self, server, auth_manager, debug=False):
        """初始化主窗口"""
        super().__init__()
        
        self.server = server
        self.auth_manager = auth_manager
        self.debug = debug
        
        # 连接的实验池
        self.connected_pools = {}
        
        # 当前选中的实验池
        self.current_pool_id = None
        
        # 参数自动更新标志位
        self.auto_update_params = False
        
        # 3D渲染器
        self.pool_renderer = None
        
        # MATLAB接口
        self.matlab_interface = MatlabInterface(use_matlab=True, debug=self.debug)
        
        # 当前渲染引擎类型
        self.rendering_engine = "python"  # 默认使用Python渲染引擎
        
        # 标记点和注释的引用列表
        self.marker_plots = []
        self.marker_annotations = []
        
        # UI是否已初始化的标志
        self.ui_initialized = False
        
        # 连接服务器信号
        self.server.pool_connected.connect(self.on_pool_connected)
        self.server.pool_disconnected.connect(self.on_pool_disconnected)
        self.server.video_frame_received.connect(self.on_video_frame_received)
        self.server.data_received.connect(self.on_data_received)
        self.server.pools_discovered.connect(self.on_pools_discovered)
        
        # 显示登录对话框
        self.show_login_dialog()
        
    def show_login_dialog(self):
        """显示登录对话框"""
        dialog = LoginDialog(self.auth_manager, self)
        result = dialog.exec_()
        
        if result == QDialog.Accepted:
            # 如果登录成功，初始化UI（只初始化一次）
            if not self.ui_initialized:
                self.setup_ui()
                self.ui_initialized = True
                
                # 设置定时器，定期更新UI
                self.update_timer = QTimer(self)
                self.update_timer.timeout.connect(self.update_ui)
                self.update_timer.start(33)  # 约30FPS
        
        # 手动刷新一次以显示当前可用的实验池
                QTimer.singleShot(1000, self.refresh_pools)
        
        # 尝试恢复保存的窗口布局
                self.restore_window_layout()
    
            # 显示连接配置界面
                self.show_connection_dialog()
        else:
            # 如果取消登录，关闭程序
            self.close()
            
    def show_connection_dialog(self):
        """显示连接配置对话框"""
        dialog = ConnectionDialog(self.auth_manager, self)
        dialog.connection_set.connect(self.on_connection_set)
        result = dialog.exec_()
        
        if result == QDialog.Accepted:
            # 用户已经设置了连接参数，初始化必要的状态
            self.refresh_pools()
        else:
            # 如果用户取消，则重新显示登录对话框
            self.show_login_dialog()
            
    @pyqtSlot(int, str)
    def on_connection_set(self, port, ip):
        """连接信息设置槽函数"""
        # 更新服务器连接参数
        self.server.set_connection_params(ip, port)
        
        # 显示状态信息
        status_message = f"已配置连接: {ip}:{port}"
        if self.auth_manager.get_reserved_pool_id():
            status_message += f" (实验池: {self.auth_manager.get_reserved_pool_id()})"
        
        self.statusBar().showMessage(status_message, 5000)
        
        # 如果设置了自动连接，尝试连接到实验池
        if self.auth_manager.get_reserved_pool_id():
            QTimer.singleShot(1000, lambda: self.refresh_pools())
    
    def show_about_dialog(self):
        """显示关于对话框"""
        QMessageBox.about(
            self,
            "关于 OceanLab-Python",
            "OceanLab-Python 海洋信息实验教学平台\n"
            "版本: 1.0.0\n\n"
            "用于模拟和控制水池中的水听器和换能器设备"
        )
    
    def refresh_pools(self):
        """刷新实验池列表"""
        if not self.auth_manager.is_authenticated():
            QMessageBox.warning(self, "错误", "请先登录")
            return
        
        self.statusBar().showMessage("正在搜索实验池...")
        
        # 获取连接的实验池
        connected_pools = self.server.get_connected_pools()
        
        # 获取发现的实验池
        discovered_pools = {}
        with self.server.pools_lock:
            discovered_pools = self.server.discovered_pools.copy()
        
        # 更新下拉列表
        self.pool_combo.clear()
        if not connected_pools:
            self.pool_combo.addItem("未连接到实验池", None)
        else:
            for pool_id in connected_pools:
                self.pool_combo.addItem(f"实验池 {pool_id}", pool_id)
        
        # 更新实验池表格
        self.pool_table.setRowCount(0)
        
        # 添加已连接的实验池
        for pool_id, pool_info in connected_pools.items():
            row = self.pool_table.rowCount()
            self.pool_table.insertRow(row)
            
            # 设置实验池ID
            id_item = QTableWidgetItem(pool_id)
            self.pool_table.setItem(row, 0, id_item)
            
            # 设置状态
            status_item = QTableWidgetItem("已连接")
            status_item.setForeground(QColor("green"))
            self.pool_table.setItem(row, 1, status_item)
            
            # 设置操作按钮
            disconnect_btn = QPushButton("断开")
            disconnect_btn.clicked.connect(lambda checked, pid=pool_id: self.disconnect_from_pool(pid))
            self.pool_table.setCellWidget(row, 2, disconnect_btn)
        
        # 添加发现但未连接的实验池
        for pool_id, pool_info in discovered_pools.items():
            # 跳过已连接的实验池
            if pool_id in connected_pools:
                continue
            
            row = self.pool_table.rowCount()
            self.pool_table.insertRow(row)
            
            # 设置实验池ID
            id_item = QTableWidgetItem(pool_id)
            self.pool_table.setItem(row, 0, id_item)
            
            # 设置状态
            status_item = QTableWidgetItem("在线")
            status_item.setForeground(QColor("blue"))
            self.pool_table.setItem(row, 1, status_item)
            
            # 设置操作按钮
            connect_btn = QPushButton("连接")
            connect_btn.clicked.connect(lambda checked, pid=pool_id: self.connect_to_pool(pid))
            self.pool_table.setCellWidget(row, 2, connect_btn)
        
        # 如果没有任何实验池
        if self.pool_table.rowCount() == 0:
            self.pool_table.insertRow(0)
            no_pool_item = QTableWidgetItem("未找到实验池")
            no_pool_item.setTextAlignment(Qt.AlignCenter)
            self.pool_table.setSpan(0, 0, 1, 3)  # 合并单元格
            self.pool_table.setItem(0, 0, no_pool_item)
        
        # 更新状态栏
        total_pools = len(connected_pools) + len([p for p in discovered_pools if p not in connected_pools])
        self.statusBar().showMessage(f"找到 {total_pools} 个实验池，其中 {len(connected_pools)} 个已连接")
        
        # 检查是否有预约的实验池ID
        reserved_pool_id = self.auth_manager.get_reserved_pool_id()
        if reserved_pool_id:
            # 在表格中标记预约的实验池
            for row in range(self.pool_table.rowCount()):
                pool_id = self.pool_table.item(row, 0).text()
                if pool_id == reserved_pool_id:
                    for col in range(self.pool_table.columnCount()):
                        item = self.pool_table.item(row, col)
                        if item:
                            item.setBackground(QColor(255, 255, 200))  # 淡黄色背景
                    
                    # 如果预约的实验池已连接，自动选择它
                    if reserved_pool_id in connected_pools:
                        for i in range(self.pool_combo.count()):
                            if self.pool_combo.itemData(i) == reserved_pool_id:
                                self.pool_combo.setCurrentIndex(i)
                                break
    
    def on_pool_connected(self, pool_id):
        """实验池连接事件处理"""
        print(f"实验池 {pool_id} 已连接")
        
        # 初始化设备状态
        if hasattr(self, 'connected_pools'):
            self.connected_pools[pool_id] = {}
        else:
            self.connected_pools = {pool_id: {}}
        
        # 更新下拉列表
        self.refresh_pools()
        
        # 不再自动选择和连接实验池，由用户来选择
        # 更新状态栏，显示有新的实验池可用
        pools_count = len(self.server.get_connected_pools())
        self.statusBar().showMessage(f"实验池 {pool_id} 已连接，可以从实验池列表中选择")
        
        # 显示新实验池可用的提示
        if self.current_pool_id != pool_id:
            QMessageBox.information(
                self,
                "新实验池已连接",
                f"实验池 {pool_id} 已上线，您可以从池列表中选择连接。"
            )
    
    def on_pool_disconnected(self, pool_id):
        """实验池断开连接事件处理"""
        print(f"实验池 {pool_id} 已断开连接")
        
        # 从设备状态中移除
        if hasattr(self, 'connected_pools') and pool_id in self.connected_pools:
            del self.connected_pools[pool_id]
        
        # 更新下拉列表
        self.refresh_pools()
        
        # 如果断开的是当前选择的实验池，清空当前选择并更新UI
        if self.current_pool_id == pool_id:
            self.current_pool_id = None
            # 更新状态栏
            self.statusBar().showMessage("实验池已断开连接")
            # 清空摄像头显示
            self.camera_view.setText("摄像头未连接")
            # 清空设备表格
            self.device_table.setRowCount(0)
            # 添加一行提示信息
            self.device_table.setRowCount(1)
            info_item = QTableWidgetItem("未连接到实验池")
            info_item.setTextAlignment(Qt.AlignCenter)
            self.device_table.setSpan(0, 0, 1, 5)  # 合并单元格
            self.device_table.setItem(0, 0, info_item)
            # 清空设备详情
            self.device_id_label.setText("")
            self.device_name_label.setText("")
            self.device_type_label.setText("")
            self.device_connection_label.setText("")
            self.device_status_label.setText("")
            self.device_position_label.setText("")
    
    def on_video_frame_received(self, pool_id, frame):
        """接收到视频帧事件"""
        # 如果是当前选中的实验池，更新视频显示
        if self.current_pool_id == pool_id:
            self.update_video_display(frame)
    
    def on_pool_selected(self, index):
        """实验池选择事件"""
        if index < 0 or not self.connected_pools:
            self.current_pool_id = None
            return
        
        # 获取选中的实验池ID
        pool_ids = list(self.connected_pools.keys())
        if index < len(pool_ids):
            # 获取新的实验池ID
            new_pool_id = pool_ids[index]
            
            # 如果选择了一个不同的实验池，确认是否切换
            if self.current_pool_id is not None and new_pool_id != self.current_pool_id:
                # 显示确认对话框
                reply = QMessageBox.question(
                    self,
                    "切换实验池",
                    f"您正在从实验池 {self.current_pool_id} 切换到实验池 {new_pool_id}。\n"
                    f"这将更改显示的参数和数据。是否继续？",
                    QMessageBox.Yes | QMessageBox.No,
                    QMessageBox.Yes
                )
                
                if reply == QMessageBox.No:
                    # 恢复下拉框选择
                    for i in range(self.pool_combo.count()):
                        if self.pool_combo.itemData(i) == self.current_pool_id:
                            self.pool_combo.setCurrentIndex(i)
                            break
                    return
            
            # 设置当前实验池ID
            self.current_pool_id = new_pool_id
            
            # 检查是否有预约的实验池ID
            reserved_pool_id = self.auth_manager.get_reserved_pool_id()
            if reserved_pool_id and reserved_pool_id != self.current_pool_id:
                # 显示警告，但允许用户选择
                QMessageBox.warning(
                    self,
                    "实验池配对警告",
                    f"您已预约实验池 {reserved_pool_id}，但当前选择的是 {self.current_pool_id}。\n"
                    "建议您使用预约的实验池以避免冲突。"
                )
            
            self.statusBar().showMessage(f"已选择实验池 {self.current_pool_id}")
            
            # 临时启用参数自动更新，只更新一次
            old_auto_update = self.auto_update_params
            self.auto_update_params = True
            
            # 更新控制面板的参数值
            self.update_control_values()
            
            # 恢复之前的自动更新设置
            self.auto_update_params = old_auto_update
            
            # 如果使用MATLAB引擎，更新MATLAB模型
            if self.rendering_engine == "matlab":
                self.update_matlab_model()
    
    def update_control_values(self):
        """更新控制值"""
        if not self.current_pool_id or self.current_pool_id not in self.connected_pools:
            return
            
        # 如果禁用了自动更新参数，则不更新控制值
        if not self.auto_update_params:
            return
        
        pool = self.connected_pools[self.current_pool_id]
        
        # 更新水听器参数
        self.hydrophone_x.setValue(pool.get("hydrophone_x", 0.25))
        self.hydrophone_y.setValue(pool.get("hydrophone_y", 0.0))
        self.hydrophone_z.setValue(pool.get("hydrophone_z", 0.5))
        self.hydrophone_rotation.setValue(pool.get("hydrophone_rotation", 0.0))
        
        # 更新换能器参数
        self.transducer_x.setValue(pool.get("transducer_x", -0.25))
        self.transducer_y.setValue(pool.get("transducer_y", 0.0))
        self.transducer_z.setValue(pool.get("transducer_z", 0.5))
        self.transducer_rotation.setValue(pool.get("transducer_rotation", 0.0))
        
        # 更新水池参数
        self.water_depth.setValue(pool.get("water_depth", 0.5))
        
        # 更新3D渲染器参数
        if self.pool_renderer:
            self.pool_renderer.update_params(pool)
        
        # 如果使用MATLAB引擎，更新MATLAB模型
        if self.rendering_engine == "matlab":
            self.update_matlab_model()
    
    def on_hydrophone_params_changed(self):
        """水听器参数变更事件"""
        if not self.current_pool_id:
            return
        
        # 获取参数值
        params = {
            "hydrophone_x": self.hydrophone_x.value(),
            "hydrophone_y": self.hydrophone_y.value(),
            "hydrophone_z": self.hydrophone_z.value(),
            "hydrophone_rotation": self.hydrophone_rotation.value()
        }
        
        # 发送参数到实验池
        self.server.send_params_to_pool(self.current_pool_id, params)
        
        # 更新3D渲染器
        if self.pool_renderer:
            self.pool_renderer.update_params(params)
        
        # 如果使用MATLAB引擎，更新MATLAB模型
        if self.rendering_engine == "matlab":
            self.update_matlab_model()
    
    def on_transducer_params_changed(self):
        """换能器参数变更事件"""
        if not self.current_pool_id:
            return
        
        # 获取参数值
        params = {
            "transducer_x": self.transducer_x.value(),
            "transducer_y": self.transducer_y.value(),
            "transducer_z": self.transducer_z.value(),
            "transducer_rotation": self.transducer_rotation.value()
        }
        
        # 发送参数到实验池
        self.server.send_params_to_pool(self.current_pool_id, params)
        
        # 更新3D渲染器
        if self.pool_renderer:
            self.pool_renderer.update_params(params)
        
        # 如果使用MATLAB引擎，更新MATLAB模型
        if self.rendering_engine == "matlab":
            self.update_matlab_model()
    
    def on_pool_params_changed(self):
        """水池参数变更事件"""
        if not self.current_pool_id:
            return
        
        # 获取参数值
        params = {
            "water_depth": self.water_depth.value()
        }
        
        # 发送参数到实验池
        self.server.send_params_to_pool(self.current_pool_id, params)
        
        # 更新3D渲染器
        if self.pool_renderer:
            self.pool_renderer.update_params(params)
        
        # 如果使用MATLAB引擎，更新MATLAB模型
        if self.rendering_engine == "matlab":
            self.update_matlab_model()
    
    def update_video_display(self, frame):
        """更新视频显示"""
        try:
            if frame is None:
                return
            
            height, width, channel = frame.shape
            bytes_per_line = 3 * width
            q_img = QImage(frame.data, width, height, bytes_per_line, QImage.Format_RGB888)
            self.camera_view.setPixmap(QPixmap.fromImage(q_img))
        except Exception as e:
            print(f"更新视频显示时出错: {e}")
            # 仅打印错误，不要让程序崩溃
            # 在标签上显示错误信息
            self.camera_view.setText(f"摄像头错误: {e}")
            self.camera_status.setText("摄像头访问出错")
    
    def update_ui(self):
        """更新UI"""
        if not self.current_pool_id or self.current_pool_id not in self.connected_pools:
            return
        
        pool = self.connected_pools[self.current_pool_id]
        
        # 更新摄像头视图
        if "video_frame" in pool and pool["video_frame"] is not None:
            self.update_video_display(pool["video_frame"])
    
    def refresh_ui(self):
        """刷新UI"""
        self.update_control_values()
        self.update_ui()
        self.statusBar().showMessage("UI已刷新")
    
    def closeEvent(self, event):
        """窗口关闭事件"""
        # 停止服务器
        self.server.stop()
        
        # 停止定时器（如果存在）
        if hasattr(self, 'update_timer'):
            self.update_timer.stop()
        
        # 关闭MATLAB引擎
        if self.matlab_interface:
            self.matlab_interface.close()
        
        # 保存窗口状态和布局
        try:
            import os
            import pickle
            
            # 确保存储目录存在
            config_dir = os.path.join(os.path.expanduser("~"), ".oceanlab")
            if not os.path.exists(config_dir):
                os.makedirs(config_dir)
                
            # 保存窗口状态
            window_state = {
                'size': (self.width(), self.height()),
                'pos': (self.x(), self.y()),
                'geometry': self.saveGeometry(),
                'state': self.saveState()
            }
            
            # 保存到文件
            with open(os.path.join(config_dir, "window_state.pkl"), "wb") as f:
                pickle.dump(window_state, f)
        except Exception as e:
            print(f"保存窗口布局时出错: {e}")
        
        event.accept()
    
    def start_data_acquisition(self):
        """开始数据采集"""
        if not self.current_pool_id:
            QMessageBox.warning(self, "错误", "请先选择一个实验池")
            return
        
        # 获取采集参数
        sample_rate_text = self.sample_rate_combo.currentText()
        sample_rate = int(sample_rate_text.split()[0])  # 提取数字部分
        
        data_length = int(self.data_length_combo.currentText())
        
        # 获取信号参数
        signal_type_map = {
            "正弦波": "sine",
            "脉冲波": "pulse",
            "调频脉冲波": "chirp"
        }
        signal_type = signal_type_map[self.signal_type_combo.currentText()]
        signal_freq = self.signal_freq_spin.value()
        
        # 对于脉冲波和调频脉冲波，确保有足够的数据长度来显示反射波
        if signal_type in ["pulse", "chirp"] and data_length < 1024:
            # 自动增加数据长度以获取更好的反射波效果
            data_length = 1024
            # 更新UI显示
            index = self.data_length_combo.findText(str(data_length))
            if index >= 0:
                self.data_length_combo.setCurrentIndex(index)
            
            # 提示用户
            self.statusBar().showMessage(f"已自动增加数据长度至 {data_length} 以获取更好的反射波效果")
        
        # 清空之前的数据显示
        self.data_line.set_data([], [])
        self.data_ax.set_title('传感器数据波形 - 采集中', fontsize=12)
        self.data_fig.canvas.draw_idle()
        
        # 发送数据采集命令到实验池
        params = {
            "sample_rate": sample_rate,
            "data_length": data_length,
            "signal_type": signal_type,
            "signal_freq": signal_freq
        }
        
        self.server.send_command_to_pool(self.current_pool_id, "start_data_acquisition", params)
        
        # 更新UI状态
        self.data_status_label.setText("采集中")
        self.data_status_label.setStyleSheet("color: green;")
        self.start_data_button.setEnabled(False)
        self.stop_data_button.setEnabled(True)
        self.data_info_left.setText(f"采样率: {sample_rate} Hz, 数据点数: {data_length}")
        
        self.statusBar().showMessage(f"已开始数据采集，采样率: {sample_rate} Hz, 数据点数: {data_length}")
    
    def stop_data_acquisition(self):
        """停止数据采集"""
        if not self.current_pool_id:
            return
        
        # 发送停止采集命令到实验池
        self.server.send_command_to_pool(self.current_pool_id, "stop_data_acquisition")
        
        # 更新图表标题
        self.data_ax.set_title('传感器数据波形', fontsize=12)
        self.data_fig.canvas.draw()
        
        # 暂停显示更新
        self.is_paused = True
        self.pause_button.setChecked(True)
        self.pause_button.setText("继续")
        
        # 更新UI状态
        self.data_status_label.setText("未采集")
        self.data_status_label.setStyleSheet("")
        self.start_data_button.setEnabled(True)
        self.stop_data_button.setEnabled(False)
        
        self.statusBar().showMessage("已停止数据采集")
    
    def toggle_pause_display(self):
        """切换暂停/继续波形显示"""
        self.is_paused = self.pause_button.isChecked()
        if self.is_paused:
            self.pause_button.setText("继续")
            self.statusBar().showMessage("波形显示已暂停")
        else:
            self.pause_button.setText("暂停")
            self.statusBar().showMessage("波形显示已继续")
            # 如果有缓存的数据，立即更新显示
            if self.last_data:
                self.update_data_display(self.last_data)
    
    def single_trigger(self):
        """设置单次触发模式"""
        self.single_trigger_mode = True
        self.is_paused = False
        self.pause_button.setChecked(False)
        self.pause_button.setText("暂停")
        self.statusBar().showMessage("单次触发模式已启用，等待下一帧数据...")
    
    def auto_scale_display(self):
        """自动缩放波形显示"""
        # 只有在有数据的情况下才进行缩放
        if len(self.data_line.get_xdata()) > 0:
            self.data_ax.relim()
            self.data_ax.autoscale_view()
            self.data_fig.canvas.draw()
            self.statusBar().showMessage("波形已自动缩放")
    
    def clear_waveform(self):
        """清除波形显示"""
        self.data_line.set_data([], [])
        self.data_fig.canvas.draw()
        self.statusBar().showMessage("波形已清除")
    
    def update_timebase(self):
        """更新时基设置"""
        timebase_text = self.timebase_combo.currentText()
        # 解析时基值
        value = float(timebase_text.split('/')[0].replace('μs', 'e-6').replace('ms', 'e-3'))
        # 时基影响x轴显示范围 (10个格子)
        self.data_ax.set_xlim(0, value * 10)
        self.data_fig.canvas.draw()
        self.statusBar().showMessage(f"时基已设置为 {timebase_text}")
    
    def on_mouse_move(self, event):
        """处理鼠标移动事件，显示坐标"""
        if event.inaxes:
            x, y = event.xdata, event.ydata
            self.data_info_center.setText(f"光标: X={x:.6f}s, Y={y:.3f}")
            
            # 更新光标注释
            self.cursor_annotation.xy = (x, y)
            self.cursor_annotation.set_text(f'X: {x:.6f}s\nY: {y:.3f}')
            self.cursor_annotation.set_visible(True)
            
            # 仅当移动较大时才更新，避免频繁重绘
            self.data_fig.canvas.draw_idle()
        else:
            self.cursor_annotation.set_visible(False)
            self.data_fig.canvas.draw_idle()
    
    def on_mouse_click(self, event):
        """处理鼠标点击事件，添加测量标记"""
        if event.inaxes and event.button == 1:  # 左键点击
            x, y = event.xdata, event.ydata
            # 添加一个永久标记点
            marker, = self.data_ax.plot(x, y, 'ro', markersize=4)
            annotation = self.data_ax.annotate(f'({x:.4f}s, {y:.2f})',
                             xy=(x, y),
                             xytext=(10, 10),
                             textcoords="offset points",
                             bbox=dict(boxstyle="round", fc="white", alpha=0.7))
            
            # 保存标记点和注释的引用
            self.marker_plots.append(marker)
            self.marker_annotations.append(annotation)
            
            self.data_fig.canvas.draw()
    
    def update_data_display(self, data):
        """更新数据显示
        
        Args:
            data: 包含数据波形信息的字典，应包含以下键:
                - samples: 数据样本数组
                - sample_rate: 采样率
                - timestamp: 采集时间戳
                - signal_type: 信号类型
                - signal_freq: 信号频率
        """
        if not data:
            return
        
        # 保存最新数据用于暂停恢复
        self.last_data = data
        
        # 如果在暂停状态，不更新显示
        if self.is_paused:
            return
        
        sample_rate = data.get("sample_rate", 0)
        timestamp = data.get("timestamp", 0)
        samples = data.get("samples", [])
        signal_type = data.get("signal_type", "sine")
        signal_freq = data.get("signal_freq", 1000)
        
        # 信号类型的显示名称
        signal_type_names = {
            "sine": "正弦波",
            "pulse": "脉冲波",
            "chirp": "调频脉冲波"
        }
        signal_type_name = signal_type_names.get(signal_type, "未知信号")
        
        if samples:
            # 计算简单统计信息
            data_min = min(samples)
            data_max = max(samples)
            data_mean = sum(samples) / len(samples)
            
            # 更新信息显示
            self.data_info_left.setText(f"采样率: {sample_rate} Hz, 数据点数: {len(samples)}")
            self.data_info_right.setText(f"信号类型: {signal_type_name}, 频率: {signal_freq} Hz, 振幅: {data_min:.2f}~{data_max:.2f}")
            
            # 绘制波形
            import numpy as np
            
            # 创建时间轴
            time_axis = np.arange(len(samples)) / sample_rate
            
            # 更新数据
            self.data_line.set_data(time_axis, samples)
            
            # 确保X轴范围显示完整数据 - 根据信号类型调整显示范围
            # 脉冲波和调频脉冲波需要看到完整的脉冲序列，包括反射波
            if signal_type in ["pulse", "chirp"]:
                # 对于脉冲波，确保显示完整时间段，即使超出了当前时基设置
                self.data_ax.set_xlim(0, time_axis[-1])
            else:
                # 对于连续波，可以使用用户设置的时基
                current_timebase = self.timebase_combo.currentText()
                value = float(current_timebase.split('/')[0].replace('μs', 'e-6').replace('ms', 'e-3'))
                timebase_range = value * 10  # 时基范围（10格）
                
                # 如果时基范围太小，无法显示完整信号，则自动调整
                if timebase_range < time_axis[-1]:
                    self.data_ax.set_xlim(0, time_axis[-1])
                else:
                    self.data_ax.set_xlim(0, timebase_range)
            
            # 增大Y轴范围余量以确保完整显示所有峰值
            y_margin = (data_max - data_min) * 0.3 if data_max != data_min else 0.3
            self.data_ax.set_ylim(data_min - y_margin, data_max + y_margin)
            
            # 确保标题正确显示
            self.data_ax.set_title(f'传感器数据波形 - {signal_type_name}', fontsize=12)
            
            # 如果是单次触发模式，收到数据后切换为暂停状态
            if self.single_trigger_mode:
                self.is_paused = True
                self.pause_button.setChecked(True)
                self.pause_button.setText("继续")
                self.single_trigger_mode = False
                self.statusBar().showMessage("单次触发完成，波形已暂停")
            
            # 强制重绘图形以显示完整波形
            self.data_fig.canvas.draw()
            self.data_fig.canvas.flush_events()
            
            # 保存当前数据供导出使用
            self.current_wave_data = {
                'time': time_axis,
                'amplitude': samples,
                'sample_rate': sample_rate,
                'signal_type': signal_type_name,
                'signal_freq': signal_freq
            }
    
    def on_data_received(self, pool_id, data):
        """处理从实验池接收到的数据"""
        # 检查是否为参数更新，如果是则忽略自动更新
        if any(key in data for key in ['hydrophone_x', 'hydrophone_y', 'hydrophone_z', 
                                     'hydrophone_rotation', 'transducer_x', 'transducer_y', 
                                     'transducer_z', 'transducer_rotation', 'water_depth']):
            # 这是一个参数更新消息，但我们不希望自动更新UI控件
            # 只更新池连接信息，但不更新控件值
            if hasattr(self, 'connected_pools'):
                for key, value in data.items():
                    if pool_id in self.connected_pools:
                        self.connected_pools[pool_id][key] = value
            return
        
        # 正常处理其他类型的数据
        # 更新UI - 直接将接收到的数据传递给更新函数
        if self.current_pool_id == pool_id:
            self.update_data_display(data)
        
        # 处理设备状态信息
        if 'device_status' in data and self.current_pool_id == pool_id:
            self.update_device_status(pool_id, data['device_status'])
    
    def update_device_status(self, pool_id, device_status):
        """更新设备状态信息
        
        Args:
            pool_id (str): 实验池ID
            device_status (dict): 设备状态信息
        """
        # 存储设备状态
        if hasattr(self, 'connected_pools'):
            self.connected_pools[pool_id] = device_status
        else:
            self.connected_pools = {pool_id: device_status}
        
        # 清空设备表格
        self.device_table.setRowCount(0)
        
        # 如果没有设备，显示提示信息
        if not device_status or 'devices' not in device_status or not device_status['devices']:
            # 添加一行提示信息
            self.device_table.setRowCount(1)
            info_item = QTableWidgetItem("未检测到设备")
            info_item.setTextAlignment(Qt.AlignCenter)
            self.device_table.setSpan(0, 0, 1, 5)  # 合并单元格
            self.device_table.setItem(0, 0, info_item)
            
            # 清空详情区域
            self.device_id_label.setText("")
            self.device_name_label.setText("")
            self.device_type_label.setText("")
            self.device_connection_label.setText("")
            self.device_status_label.setText("")
            self.device_position_label.setText("")
            
            return
        
        # 添加设备信息到表格
        devices = device_status['devices']
        self.device_table.setRowCount(len(devices))
        
        for i, device in enumerate(devices):
            # 设备ID
            id_item = QTableWidgetItem(device.get('id', ''))
            self.device_table.setItem(i, 0, id_item)
            
            # 设备名称
            name_item = QTableWidgetItem(device.get('name', ''))
            self.device_table.setItem(i, 1, name_item)
            
            # 设备类型
            type_name = self.get_device_type_name(device.get('type', ''))
            type_item = QTableWidgetItem(type_name)
            self.device_table.setItem(i, 2, type_item)
            
            # 连接方式
            connection_type = self.get_connection_type_name(device.get('connection_type', ''))
            connection_item = QTableWidgetItem(connection_type)
            self.device_table.setItem(i, 3, connection_item)
            
            # 状态
            status_name = self.get_device_status_name(device.get('status', ''))
            status_item = QTableWidgetItem(status_name)
            self.device_table.setItem(i, 4, status_item)
        
        # 如果只有一个设备，自动选中并显示详情
        if len(devices) == 1:
            self.update_device_details(devices[0])
    
    def get_device_type_name(self, device_type):
        """获取设备类型名称
        
        Args:
            device_type (str): 设备类型编码
        
        Returns:
            str: 设备类型名称
        """
        type_map = {
            'robot_arm': '两自由度机械臂',
            'hydrophone': '水听器',
            'transducer': '换能器',
            'network_device': '网络设备',
            'unknown': '未知设备'
        }
        return type_map.get(device_type, '未知设备')
    
    def get_connection_type_name(self, connection_type):
        """获取连接方式名称
        
        Args:
            connection_type (str): 连接方式编码
        
        Returns:
            str: 连接方式名称
        """
        type_map = {
            'usb': 'USB连接',
            'serial': '串口连接',
            'network': '网络连接',
            'bluetooth': '蓝牙连接',
            'unknown': '未知连接'
        }
        return type_map.get(connection_type, '未知连接')
    
    def get_device_status_name(self, status):
        """获取设备状态名称
        
        Args:
            status (str): 设备状态编码
        
        Returns:
            str: 设备状态名称
        """
        status_map = {
            'disconnected': '未连接',
            'connected': '已连接',
            'error': '错误',
            'moving': '运动中',
            'ready': '就绪',
            'unknown': '未知状态'
        }
        return status_map.get(status, '未知状态')
    
    def update_device_details(self, device):
        """更新设备详情显示
        
        Args:
            device (dict): 设备信息
        """
        # 设备ID
        self.device_id_label.setText(device.get('id', ''))
        
        # 设备名称
        self.device_name_label.setText(device.get('name', ''))
        
        # 设备类型
        type_name = self.get_device_type_name(device.get('type', ''))
        self.device_type_label.setText(type_name)
        
        # 连接方式
        connection_type = self.get_connection_type_name(device.get('connection_type', ''))
        self.device_connection_label.setText(connection_type)
        
        # 状态
        status_name = self.get_device_status_name(device.get('status', ''))
        self.device_status_label.setText(status_name)
        
        # 位置
        position = device.get('position', {})
        if position:
            pos_text = f"X: {position.get('x', 0):.2f}m, Y: {position.get('y', 0):.2f}m, Z: {position.get('z', 0):.2f}m, 角度: {position.get('angle', 0):.1f}°"
            self.device_position_label.setText(pos_text)
        else:
            self.device_position_label.setText("未知")

    def auto_refresh_pools(self):
        """自动刷新实验池列表"""
        # 只有在用户已登录时才自动刷新
        # 此处不再自动刷新,避免参数重置问题
        # 用户可以手动点击刷新按钮
        pass

    def clear_markers(self):
        """清除所有测量标记点"""
        # 移除所有标记点
        for marker in self.marker_plots:
            marker.remove()
        
        # 移除所有标记注释
        for annotation in self.marker_annotations:
            annotation.remove()
        
        # 清空引用列表
        self.marker_plots = []
        self.marker_annotations = []
        
        # 重绘画布
        self.data_fig.canvas.draw()
        self.statusBar().showMessage("已清除所有测量标记")
    
    def on_render_engine_changed(self, index):
        """渲染引擎变更事件"""
        # 获取选择的渲染引擎
        self.rendering_engine = self.render_engine_combo.itemData(index)
        
        # 如果选择了MATLAB引擎，但MATLAB不可用，则回退到Python引擎
        if self.rendering_engine == "matlab" and not self.matlab_interface.is_change_m_available():
            self.rendering_engine = "python"
            # 找到Python引擎的索引
            python_index = self.render_engine_combo.findText("Python虚拟引擎")
            if python_index >= 0:
                self.render_engine_combo.setCurrentIndex(python_index)
            QMessageBox.warning(self, "警告", "MATLAB 3D建模不可用，将使用Python虚拟引擎")
        
        # 更新UI状态
        if self.rendering_engine == "matlab":
            # 使用MATLAB引擎时，隐藏Python 3D视图
            self.pool_renderer.setVisible(False)
            self.statusBar().showMessage("已切换到MATLAB 3D建模")
            
            # 更新MATLAB模型
            self.update_matlab_model()
        else:
            # 使用Python引擎时，显示Python 3D视图
            self.pool_renderer.setVisible(True)
            self.statusBar().showMessage("已切换到Python虚拟引擎")
    
    def update_matlab_model(self):
        """更新MATLAB模型"""
        if self.rendering_engine != "matlab" or not self.matlab_interface.is_change_m_available():
            return
        
        # 获取当前参数
        params = {
            "hydrophone_x": self.hydrophone_x.value(),
            "hydrophone_y": self.hydrophone_y.value(),
            "hydrophone_z": self.hydrophone_z.value(),
            "hydrophone_rotation": self.hydrophone_rotation.value(),
            "transducer_x": self.transducer_x.value(),
            "transducer_y": self.transducer_y.value(),
            "transducer_z": self.transducer_z.value(),
            "transducer_rotation": self.transducer_rotation.value(),
            "water_depth": self.water_depth.value()
        }
        
        # 更新MATLAB模型
        self.matlab_interface.update_3d_model(params) 

    def on_device_selected(self, item):
        """处理设备表格选中事件"""
        row = item.row()
        device_id = self.device_table.item(row, 0).text()
        self.update_device_details(self.get_device_info(device_id))

    def get_device_info(self, device_id):
        """获取设备信息
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            dict: 设备信息
        """
        for pool_id, device_status in self.connected_pools.items():
            if 'devices' in device_status:
                for device in device_status['devices']:
                    if device.get('id') == device_id:
                        return device
        return {}

    def update_device_details(self, device):
        """更新设备详情显示
        
        Args:
            device (dict): 设备信息
        """
        # 设备ID
        self.device_id_label.setText(device.get('id', ''))
        
        # 设备名称
        self.device_name_label.setText(device.get('name', ''))
        
        # 设备类型
        type_name = self.get_device_type_name(device.get('type', ''))
        self.device_type_label.setText(type_name)
        
        # 连接方式
        connection_type = self.get_connection_type_name(device.get('connection_type', ''))
        self.device_connection_label.setText(connection_type)
        
        # 状态
        status_name = self.get_device_status_name(device.get('status', ''))
        self.device_status_label.setText(status_name)
        
        # 位置
        position = device.get('position', {})
        if position:
            pos_text = f"X: {position.get('x', 0):.2f}m, Y: {position.get('y', 0):.2f}m, Z: {position.get('z', 0):.2f}m, 角度: {position.get('angle', 0):.1f}°"
            self.device_position_label.setText(pos_text)
        else:
            self.device_position_label.setText("未知")

    def refresh_device_list(self):
        """刷新设备列表"""
        self.refresh_pools()

    def connect_to_selected_pool(self):
        """连接到选定的实验池"""
        # 获取当前选择的实验池
        selected_rows = self.pool_table.selectedItems()
        if not selected_rows:
            QMessageBox.warning(self, "提示", "请先选择一个实验池")
            return
        
        # 获取选中行的实验池ID
        row = selected_rows[0].row()
        pool_id = self.pool_table.item(row, 0).text()
        
        # 检查是否已连接
        if self.server.is_pool_connected(pool_id):
            QMessageBox.information(self, "提示", f"已经连接到实验池 {pool_id}")
            return
        
        # 获取实验池IP地址
        with self.server.pools_lock:
            if pool_id in self.server.discovered_pools:
                pool_info = self.server.discovered_pools[pool_id]
                address = pool_info.get('address')
            else:
                QMessageBox.warning(self, "错误", f"找不到实验池 {pool_id} 的地址信息")
                return
        
        # 显示连接中状态
        self.statusBar().showMessage(f"正在连接到实验池 {pool_id}...")
        
        # 尝试连接
        # 这里需要实现主动连接的逻辑，可以通过发送特殊命令给从端
        # 或者等待从端主动连接
        
        # 更新状态
        self.statusBar().showMessage(f"已连接到实验池 {pool_id}")
        
        # 更新下拉列表
        self.refresh_pools()
        
        # 选择该实验池
        for i in range(self.pool_combo.count()):
            if self.pool_combo.itemData(i) == pool_id:
                self.pool_combo.setCurrentIndex(i)
                break 

    def connect_to_pool(self, pool_id):
        """连接到指定实验池"""
        # 检查是否已连接
        if self.server.is_pool_connected(pool_id):
            # 如果已连接，则选择该池
            for i in range(self.pool_combo.count()):
                if self.pool_combo.itemData(i) == pool_id:
                    self.pool_combo.setCurrentIndex(i)
                    break
            
            # 将此实验池设为当前选中的实验池
            self.current_pool_id = pool_id
            
            # 更新状态栏
            self.statusBar().showMessage(f"已连接到实验池 {pool_id}")
            
            # 临时启用参数自动更新，只更新一次
            old_auto_update = self.auto_update_params
            self.auto_update_params = True
            
            # 更新控制面板的参数值
            self.update_control_values()
            
            # 恢复之前的自动更新设置
            self.auto_update_params = old_auto_update
            
            QMessageBox.information(self, "提示", f"已经连接到实验池 {pool_id}")
            return
        
        # 检查是否有任何其他实验池已连接
        connected_pools = self.server.get_connected_pools()
        if connected_pools:
            # 询问是否断开现有连接
            other_pool_ids = list(connected_pools.keys())
            reply = QMessageBox.question(
                self,
                "已有连接的实验池",
                f"您当前已连接到实验池 {', '.join(other_pool_ids)}。\n"
                f"本系统一次只能连接一个实验池，是否断开现有连接并连接到 {pool_id}？",
                QMessageBox.Yes | QMessageBox.No,
                QMessageBox.No
            )
            
            if reply == QMessageBox.No:
                return
            
            # 断开所有现有连接
            for other_pool_id in other_pool_ids:
                # 使用优化的断开连接方法，保持在线状态
                if hasattr(self.server, 'disconnect_pool'):
                    self.server.disconnect_pool(other_pool_id)
                else:
                    self.disconnect_from_pool(other_pool_id)
                    
                # 稍等断开完成
                time.sleep(0.5)
            
            # 刷新列表确保所有连接已断开
            self.refresh_pools()
        
        # 获取实验池信息
        with self.server.pools_lock:
            if pool_id in self.server.discovered_pools:
                pool_info = self.server.discovered_pools[pool_id]
                address = pool_info.get('address')
            else:
                QMessageBox.warning(self, "错误", f"找不到实验池 {pool_id} 的地址信息")
                return
        
        # 显示连接中状态
        self.statusBar().showMessage(f"正在连接到实验池 {pool_id}...")
        
        # 从系统设计来看，从端已经连接到服务器，只需要刷新列表
        # 由于实验池已经连接到服务器，我们不需要主动建立连接
        # 我们只需要将这个池设为当前选择的池即可
        self.refresh_pools()
                
        # 如果实验池已经在连接列表中，选择它
        if self.server.is_pool_connected(pool_id):
            # 将此实验池设为当前选中的实验池
            self.current_pool_id = pool_id
            # 找到下拉列表中对应的索引
            for i in range(self.pool_combo.count()):
                if self.pool_combo.itemData(i) == pool_id:
                    self.pool_combo.setCurrentIndex(i)
                    break
                    
            # 更新状态栏
            self.statusBar().showMessage(f"已连接到实验池 {pool_id}")
            
            # 临时启用参数自动更新，只更新一次
            old_auto_update = self.auto_update_params
            self.auto_update_params = True
            
            # 更新控制面板的参数值
            self.update_control_values()
            
            # 恢复之前的自动更新设置
            self.auto_update_params = old_auto_update
        else:
            self.statusBar().showMessage(f"等待实验池 {pool_id} 连接...")

    def disconnect_from_pool(self, pool_id):
        """断开与指定实验池的连接"""
        # 检查是否已连接
        if not self.server.is_pool_connected(pool_id):
            QMessageBox.warning(self, "错误", f"实验池 {pool_id} 未连接")
            return
        
        # 确认是否断开
        reply = QMessageBox.question(
            self,
            "确认断开",
            f"确定要断开与实验池 {pool_id} 的连接吗？",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No
        )
        
        if reply == QMessageBox.No:
            return
        
        # 显示断开中状态
        self.statusBar().showMessage(f"正在断开与实验池 {pool_id} 的连接...")
        
        # 先将当前池ID重置为None，防止自动重连
        if self.current_pool_id == pool_id:
            self.current_pool_id = None
        
        # 使用新的断开连接方法
        if hasattr(self.server, 'disconnect_pool'):
            # 使用disconnect_pool方法，此方法已修改为保持实验池在线
            success = self.server.disconnect_pool(pool_id)
        else:
            # 旧的断开连接逻辑（兼容模式）
            # 发送断开命令
            self.server.send_command_to_pool(pool_id, "disconnect")
            
            # 手动移除连接，但保留在discovered_pools中
            with self.server.clients_lock:
                if pool_id in self.server.clients:
                    try:
                        self.server.clients[pool_id].close()
                        del self.server.clients[pool_id]
                    except:
                        pass
            
            with self.server.pools_lock:
                if pool_id in self.server.pools:
                    # 将信息保存到discovered_pools
                    pool_info = self.server.pools[pool_id]
                    address = pool_info.get('address')
                    
                    if address and 'discovered_pools' in dir(self.server):
                        self.server.discovered_pools[pool_id] = {
                            'address': address,
                            'port': self.server.ports[0],
                            'discovered_at': time.time(),
                            'status': 'online'
                        }
                    
                    # 然后从连接池中删除
                    del self.server.pools[pool_id]
                
            # 手动调用断开连接处理
            self.on_pool_disconnected(pool_id)
            success = True
        
        if success:
            self.statusBar().showMessage(f"已成功断开与实验池 {pool_id} 的连接，实验池仍保持在线状态")
        else:
            self.statusBar().showMessage(f"断开与实验池 {pool_id} 的连接可能未完全成功")
        
        # 刷新实验池列表，确保显示最新状态
        QTimer.singleShot(500, self.refresh_pools)

    def on_pools_discovered(self, pools):
        """处理发现实验池的信号
        
        Args:
            pools: 发现的实验池列表
        """
        # 如果当前已登录，自动刷新实验池列表
        if self.auth_manager.is_authenticated():
            # 刷新实验池列表
            self.refresh_pools()
            
            # 如果有预约的实验池，检查是否在发现的实验池中
            reserved_pool_id = self.auth_manager.get_reserved_pool_id()
            if reserved_pool_id and reserved_pool_id in pools and not self.server.is_pool_connected(reserved_pool_id):
                # 提示用户有预约的实验池在线
                self.statusBar().showMessage(f"您预约的实验池 {reserved_pool_id} 已上线，可以从池列表中选择连接", 5000)
                
                # 如果是首次发现预约的实验池，弹出提示
                if not hasattr(self, 'notified_reserved_pool') or reserved_pool_id not in self.notified_reserved_pool:
                    QMessageBox.information(
                        self,
                        "预约实验池已上线",
                        f"检测到您预约的实验池 {reserved_pool_id} 已上线，您可以从实验池列表中手动连接。"
                    )
                    
                    # 记录已通知过的实验池
                    if not hasattr(self, 'notified_reserved_pool'):
                        self.notified_reserved_pool = set()
                    self.notified_reserved_pool.add(reserved_pool_id)

    def restore_window_layout(self):
        """恢复保存的窗口布局"""
        try:
            import os
            import pickle
            
            # 检查配置文件是否存在
            config_file = os.path.join(os.path.expanduser("~"), ".oceanlab", "window_state.pkl")
            if not os.path.exists(config_file):
                return
                
            # 加载窗口状态
            with open(config_file, "rb") as f:
                window_state = pickle.load(f)
            
            # 恢复窗口尺寸和位置
            if 'size' in window_state:
                self.resize(*window_state['size'])
            if 'pos' in window_state:
                self.move(*window_state['pos'])
            if 'geometry' in window_state:
                self.restoreGeometry(window_state['geometry'])
            if 'state' in window_state:
                self.restoreState(window_state['state'])
                
        except Exception as e:
            print(f"恢复窗口布局时出错: {e}")
            # 错误时不做处理，使用默认布局即可

    def create_menu_bar(self):
        """创建菜单栏"""
        menu_bar = self.menuBar()
        
        # 文件菜单
        file_menu = menu_bar.addMenu("文件")
        
        exit_action = QAction("退出", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # 视图菜单
        view_menu = menu_bar.addMenu("视图")
        
        # 控制面板显示切换
        self.toggle_control_panel_action = QAction("显示控制面板", self)
        self.toggle_control_panel_action.setCheckable(True)
        self.toggle_control_panel_action.setChecked(True)  # 默认显示
        self.toggle_control_panel_action.triggered.connect(self.toggle_control_panel)
        view_menu.addAction(self.toggle_control_panel_action)
        
        # 重置布局
        reset_layout_action = QAction("重置窗口布局", self)
        reset_layout_action.triggered.connect(self.reset_window_layout)
        view_menu.addAction(reset_layout_action)
        
        # 帮助菜单
        help_menu = menu_bar.addMenu("帮助")
        
        about_action = QAction("关于", self)
        about_action.triggered.connect(self.show_about_dialog)
        help_menu.addAction(about_action)

    def toggle_control_panel(self, checked):
        """切换控制面板显示状态"""
        self.control_dock.setVisible(checked)
        
        # 更新菜单项标题
        if checked:
            self.toggle_control_panel_action.setText("隐藏控制面板")
        else:
            self.toggle_control_panel_action.setText("显示控制面板")
    
    def reset_window_layout(self):
        """重置窗口布局"""
        # 确认对话框
        reply = QMessageBox.question(
            self,
            "重置布局",
            "确定要重置窗口布局吗？",
            QMessageBox.Yes | QMessageBox.No,
            QMessageBox.No
        )
        
        if reply == QMessageBox.No:
            return
            
        # 重置停靠窗口位置
        self.control_dock.setVisible(True)
        self.removeDockWidget(self.control_dock)
        self.addDockWidget(Qt.LeftDockWidgetArea, self.control_dock)
        
        # 更新菜单项选中状态
        self.toggle_control_panel_action.setChecked(True)
        self.toggle_control_panel_action.setText("隐藏控制面板")
        
        # 删除保存的窗口状态文件
        try:
            import os
            config_file = os.path.join(os.path.expanduser("~"), ".oceanlab", "window_state.pkl")
            if os.path.exists(config_file):
                os.remove(config_file)
        except Exception as e:
            print(f"删除窗口状态文件时出错: {e}")

    def setup_ui(self):
        """设置UI"""
        self.setWindowTitle("OceanLab-Python 海洋信息实验教学平台")
        self.resize(1200, 800)
        
        # 创建菜单栏
        self.create_menu_bar()
        
        # 创建中央部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 主布局
        central_layout = QVBoxLayout(central_widget)
        
        # 顶部控制面板
        top_panel = QWidget()
        top_layout = QHBoxLayout(top_panel)
        top_layout.setContentsMargins(0, 0, 0, 0)
        
        # 实验池选择
        pool_group = QGroupBox("实验池连接")
        pool_layout = QVBoxLayout(pool_group)
        
        # 实验池下拉列表
        pool_combo_layout = QHBoxLayout()
        pool_combo_layout.addWidget(QLabel("选择实验池:"))
        self.pool_combo = QComboBox()
        self.pool_combo.setMinimumWidth(200)
        self.pool_combo.currentIndexChanged.connect(self.on_pool_selected)
        pool_combo_layout.addWidget(self.pool_combo)
        
        # 刷新按钮
        refresh_button = QPushButton("刷新")
        refresh_button.clicked.connect(self.refresh_pools)
        pool_combo_layout.addWidget(refresh_button)
        
        # 连接按钮
        self.connect_button = QPushButton("连接")
        self.connect_button.clicked.connect(self.connect_to_selected_pool)
        pool_combo_layout.addWidget(self.connect_button)
        
        pool_layout.addLayout(pool_combo_layout)
        
        # 实验池列表
        pool_list_label = QLabel("可用实验池:")
        pool_layout.addWidget(pool_list_label)
        
        self.pool_table = QTableWidget(0, 3)  # 3列：ID, 状态, 操作
        self.pool_table.setHorizontalHeaderLabels(["实验池ID", "状态", "操作"])
        self.pool_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.pool_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.pool_table.setSelectionMode(QTableWidget.SingleSelection)
        self.pool_table.setEditTriggers(QTableWidget.NoEditTriggers)
        pool_layout.addWidget(self.pool_table)
        
        top_layout.addWidget(pool_group)
        
        # 渲染引擎选择
        render_group = QGroupBox("渲染引擎")
        render_layout = QVBoxLayout(render_group)
        
        render_combo_layout = QHBoxLayout()
        render_combo_layout.addWidget(QLabel("选择引擎:"))
        self.render_engine_combo = QComboBox()
        self.render_engine_combo.addItem("Python虚拟引擎", "python")
        
        # 检查MATLAB引擎是否可用
        if self.matlab_interface.is_change_m_available():
            self.render_engine_combo.addItem("MATLAB建模引擎", "matlab")
        
        self.render_engine_combo.currentIndexChanged.connect(self.on_render_engine_changed)
        render_combo_layout.addWidget(self.render_engine_combo)
        
        render_layout.addLayout(render_combo_layout)
        top_layout.addWidget(render_group)
        
        central_layout.addWidget(top_panel)
        
        # 创建标签页控件
        self.display_panel = QTabWidget()
        central_layout.addWidget(self.display_panel)
        
        # 创建控制面板作为可停靠窗口
        self.control_dock = QDockWidget("控制面板", self)
        self.control_dock.setFeatures(QDockWidget.DockWidgetFloatable | 
                                     QDockWidget.DockWidgetMovable)
        self.control_dock.setAllowedAreas(Qt.LeftDockWidgetArea | 
                                         Qt.RightDockWidgetArea)
        
        # 创建控制面板内容
        control_widget = self.create_control_group()
        self.control_dock.setWidget(control_widget)
        
        # 添加停靠窗口
        self.addDockWidget(Qt.LeftDockWidgetArea, self.control_dock)
        
        # 摄像头标签页
        self.camera_tab = QWidget()
        camera_layout = QVBoxLayout(self.camera_tab)
        
        # 摄像头状态
        self.camera_status = QLabel("摄像头未连接")
        camera_layout.addWidget(self.camera_status)
        
        # 摄像头视图
        self.camera_view = QLabel("摄像头视图将在此处显示")
        self.camera_view.setAlignment(Qt.AlignCenter)
        self.camera_view.setMinimumSize(640, 480)
        camera_layout.addWidget(self.camera_view)
        
        self.display_panel.addTab(self.camera_tab, "摄像头视图")
        
        # 数据波形标签页
        self.data_tab = QWidget()
        data_layout = QVBoxLayout(self.data_tab)
        
        # 数据采集控制区域
        data_control_group = QGroupBox("数据采集控制")
        data_control_layout = QHBoxLayout(data_control_group)
        
        # 开始采集按钮
        self.start_data_button = QPushButton("开始采集")
        self.start_data_button.clicked.connect(self.start_data_acquisition)
        data_control_layout.addWidget(self.start_data_button)
        
        # 停止采集按钮
        self.stop_data_button = QPushButton("停止采集")
        self.stop_data_button.clicked.connect(self.stop_data_acquisition)
        self.stop_data_button.setEnabled(False)
        data_control_layout.addWidget(self.stop_data_button)
        
        # 信号类型选择
        signal_group_layout = QHBoxLayout()
        signal_group_layout.addWidget(QLabel("信号类型:"))
        self.signal_type_combo = QComboBox()
        self.signal_type_combo.addItems(["正弦波", "脉冲波", "调频脉冲波"])
        signal_group_layout.addWidget(self.signal_type_combo)
        
        # 信号频率
        signal_group_layout.addWidget(QLabel("频率(Hz):"))
        self.signal_freq_spin = QSpinBox()
        self.signal_freq_spin.setRange(100, 20000)
        self.signal_freq_spin.setSingleStep(100)
        self.signal_freq_spin.setValue(1000)
        signal_group_layout.addWidget(self.signal_freq_spin)
        
        data_control_layout.addLayout(signal_group_layout)
        
        # 采样率和数据长度
        sample_group_layout = QHBoxLayout()
        sample_group_layout.addWidget(QLabel("采样率:"))
        self.sample_rate_combo = QComboBox()
        self.sample_rate_combo.addItems(["8000 Hz", "16000 Hz", "44100 Hz", "48000 Hz"])
        self.sample_rate_combo.setCurrentIndex(2)  # 默认44100Hz
        sample_group_layout.addWidget(self.sample_rate_combo)
        
        sample_group_layout.addWidget(QLabel("数据长度:"))
        self.data_length_combo = QComboBox()
        self.data_length_combo.addItems(["512", "1024", "2048", "4096", "8192"])
        self.data_length_combo.setCurrentIndex(1)  # 默认1024点
        sample_group_layout.addWidget(self.data_length_combo)
        
        data_control_layout.addLayout(sample_group_layout)
        
        # 添加到数据布局
        data_layout.addWidget(data_control_group)
        
        # 数据信息区域
        data_info_group = QGroupBox("数据信息")
        data_info_layout = QVBoxLayout(data_info_group)
        
        # 状态信息
        status_layout = QHBoxLayout()
        status_layout.addWidget(QLabel("采集状态:"))
        self.data_status_label = QLabel("未采集")
        status_layout.addWidget(self.data_status_label)
        
        # 显示控制
        display_control_layout = QHBoxLayout()
        
        # 暂停/继续按钮
        self.pause_button = QPushButton("暂停")
        self.pause_button.setCheckable(True)
        self.pause_button.clicked.connect(self.toggle_pause_display)
        display_control_layout.addWidget(self.pause_button)
        
        # 单次触发按钮
        self.single_button = QPushButton("单次触发")
        self.single_button.clicked.connect(self.single_trigger)
        display_control_layout.addWidget(self.single_button)
        
        # 自动缩放按钮
        self.autoscale_button = QPushButton("自动缩放")
        self.autoscale_button.clicked.connect(self.auto_scale_display)
        display_control_layout.addWidget(self.autoscale_button)
        
        # 清除波形按钮
        self.clear_button = QPushButton("清除波形")
        self.clear_button.clicked.connect(self.clear_waveform)
        display_control_layout.addWidget(self.clear_button)
        
        # 清除标记按钮
        self.clear_markers_button = QPushButton("清除标记")
        self.clear_markers_button.clicked.connect(self.clear_markers)
        self.clear_markers_button.setToolTip("清除所有测量标记点")
        display_control_layout.addWidget(self.clear_markers_button)
        
        # 导出波形按钮
        self.export_button = QPushButton("导出波形")
        self.export_button.clicked.connect(self.export_waveform)
        self.export_button.setToolTip("将当前波形数据导出为CSV文件")
        display_control_layout.addWidget(self.export_button)
        
        # 时基设置
        display_control_layout.addWidget(QLabel("时基:"))
        self.timebase_combo = QComboBox()
        self.timebase_combo.addItems(["1μs/div", "10μs/div", "100μs/div", "1ms/div", "10ms/div", "100ms/div", "1s/div"])
        self.timebase_combo.setCurrentIndex(3)  # 默认1ms/div
        self.timebase_combo.currentIndexChanged.connect(self.update_timebase)
        display_control_layout.addWidget(self.timebase_combo)
        
        status_layout.addLayout(display_control_layout)
        data_info_layout.addLayout(status_layout)
        
        # 数据详情
        data_details_layout = QHBoxLayout()
        
        # 左侧信息
        self.data_info_left = QLabel("采样率: 0 Hz, 数据点数: 0")
        data_details_layout.addWidget(self.data_info_left)
        
        # 中间信息（光标位置）
        self.data_info_center = QLabel("光标: X=0.000s, Y=0.000")
        self.data_info_center.setAlignment(Qt.AlignCenter)
        data_details_layout.addWidget(self.data_info_center)
        
        # 右侧信息
        self.data_info_right = QLabel("信号类型: 未知, 频率: 0 Hz")
        self.data_info_right.setAlignment(Qt.AlignRight)
        data_details_layout.addWidget(self.data_info_right)
        
        data_info_layout.addLayout(data_details_layout)
        data_layout.addWidget(data_info_group)
        
        # 创建Matplotlib画布
        self.data_fig = Figure(figsize=(5, 4), dpi=100)
        self.data_canvas = FigureCanvasQTAgg(self.data_fig)
        self.data_canvas.setMinimumHeight(300)
        self.data_ax = self.data_fig.add_subplot(111)
        self.data_ax.set_title('传感器数据波形', fontsize=12)
        self.data_ax.set_xlabel('时间 (s)')
        self.data_ax.set_ylabel('振幅')
        self.data_ax.grid(True)
        self.data_line, = self.data_ax.plot([], [], 'b-', linewidth=1)
        
        # 初始化光标注释
        self.cursor_annotation = self.data_ax.annotate('',
                                xy=(0, 0),
                                xytext=(10, 10),
                                textcoords="offset points",
                                bbox=dict(boxstyle="round", fc="white", alpha=0.7),
                                arrowprops=dict(arrowstyle="->"))
        self.cursor_annotation.set_visible(False)
        
        # 设置交互式工具栏
        from matplotlib.backends.backend_qt5agg import NavigationToolbar2QT
        
        # 自定义工具栏，添加额外的缩放功能和提示
        class EnhancedNavToolbar(NavigationToolbar2QT):
            def __init__(self, canvas, parent):
                super().__init__(canvas, parent)
                # 添加波形缩放按钮
                self.addSeparator()
                self.zoom_help_action = QAction("缩放帮助", parent)
                self.zoom_help_action.setToolTip("显示如何放大/缩小波形的帮助")
                self.zoom_help_action.triggered.connect(parent.show_zoom_help)
                self.addAction(self.zoom_help_action)
                
                # 添加区域放大按钮
                self.box_zoom_action = QAction("区域放大", parent)
                self.box_zoom_action.setToolTip("拖拽鼠标选择区域进行放大")
                self.box_zoom_action.setCheckable(True)
                self.box_zoom_action.triggered.connect(parent.activate_box_zoom)
                self.addAction(self.box_zoom_action)
                
                # 添加恢复原始视图按钮
                self.reset_view_action = QAction("重置视图", parent)
                self.reset_view_action.setToolTip("恢复到原始视图")
                self.reset_view_action.triggered.connect(self.home)
                self.addAction(self.reset_view_action)
        
        # 使用增强的工具栏
        self.data_toolbar = EnhancedNavToolbar(self.data_canvas, self)
        data_layout.addWidget(self.data_toolbar)
        
        # 连接鼠标事件
        self.data_canvas.mpl_connect('motion_notify_event', self.on_mouse_move)
        self.data_canvas.mpl_connect('button_press_event', self.on_mouse_click)
        
        data_layout.addWidget(self.data_canvas)
        
        # 初始化标记点和注释的引用列表
        self.marker_plots = []
        self.marker_annotations = []
        
        # 初始化数据相关变量
        self.is_paused = False
        self.single_trigger_mode = False
        self.last_data = None
        
        self.display_panel.addTab(self.data_tab, "数据波形")
        
        # 设备标签页
        self.device_tab = QWidget()
        device_layout = QVBoxLayout(self.device_tab)
        
        # 设备表格
        device_table_group = QGroupBox("检测到的设备")
        device_table_layout = QVBoxLayout(device_table_group)
        
        # 设备表格
        self.device_table = QTableWidget(0, 5)  # 5列：ID、名称、类型、连接、状态
        self.device_table.setHorizontalHeaderLabels(["ID", "名称", "类型", "连接方式", "状态"])
        self.device_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.device_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SingleSelection)
        self.device_table.setEditTriggers(QTableWidget.NoEditTriggers)
        device_table_layout.addWidget(self.device_table)
        
        # 刷新设备列表按钮
        refresh_devices_btn = QPushButton("刷新设备列表")
        refresh_devices_btn.clicked.connect(self.refresh_device_list)
        device_table_layout.addWidget(refresh_devices_btn)
        
        device_layout.addWidget(device_table_group)
        
        # 设备详情
        device_details_group = QGroupBox("设备详情")
        device_details_layout = QFormLayout(device_details_group)
        
        # 设备ID
        self.device_id_label = QLabel("")
        device_details_layout.addRow("设备ID:", self.device_id_label)
        
        # 设备名称
        self.device_name_label = QLabel("")
        device_details_layout.addRow("设备名称:", self.device_name_label)
        
        # 设备类型
        self.device_type_label = QLabel("")
        device_details_layout.addRow("设备类型:", self.device_type_label)
        
        # 连接方式
        self.device_connection_label = QLabel("")
        device_details_layout.addRow("连接方式:", self.device_connection_label)
        
        # 设备状态
        self.device_status_label = QLabel("")
        device_details_layout.addRow("设备状态:", self.device_status_label)
        
        # 设备位置
        self.device_position_label = QLabel("")
        device_details_layout.addRow("设备位置:", self.device_position_label)
        
        device_layout.addWidget(device_details_group)
        
        self.display_panel.addTab(self.device_tab, "设备管理")
        
        # 3D视图标签页
        self.view_3d_tab = QWidget()
        view_3d_layout = QVBoxLayout(self.view_3d_tab)
        
        # 3D渲染器
        self.pool_renderer = PoolRenderer()
        view_3d_layout.addWidget(self.pool_renderer)
        
        self.display_panel.addTab(self.view_3d_tab, "3D视图")
    
    def create_control_group(self):
        """创建控制面板"""
        # 创建一个控件来包含布局
        control_widget = QWidget()
        control_layout = QVBoxLayout(control_widget)
        
        # 水听器控制
        hydrophone_group = QGroupBox("水听器控制")
        hydrophone_layout = QFormLayout()
        
        # X坐标
        self.hydrophone_x = QDoubleSpinBox()
        self.hydrophone_x.setRange(-1.0, 1.0)
        self.hydrophone_x.setSingleStep(0.01)
        self.hydrophone_x.setValue(0.25)  # 默认位置在右侧，距中心0.25米
        self.hydrophone_x.valueChanged.connect(self.on_hydrophone_params_changed)
        hydrophone_layout.addRow("X坐标 (m):", self.hydrophone_x)
        
        # Y坐标
        self.hydrophone_y = QDoubleSpinBox()
        self.hydrophone_y.setRange(-0.25, 0.25)
        self.hydrophone_y.setSingleStep(0.01)
        self.hydrophone_y.valueChanged.connect(self.on_hydrophone_params_changed)
        hydrophone_layout.addRow("Y坐标 (m):", self.hydrophone_y)
        
        # Z坐标
        self.hydrophone_z = QDoubleSpinBox()
        self.hydrophone_z.setRange(0.3, 1.0)
        self.hydrophone_z.setSingleStep(0.01)
        self.hydrophone_z.setValue(0.5)  # 默认深度0.5米
        self.hydrophone_z.valueChanged.connect(self.on_hydrophone_params_changed)
        hydrophone_layout.addRow("Z坐标 (m):", self.hydrophone_z)
        
        # 旋转角度
        self.hydrophone_rotation = QDoubleSpinBox()
        self.hydrophone_rotation.setRange(0.0, 360.0)
        self.hydrophone_rotation.setSingleStep(1.0)
        self.hydrophone_rotation.valueChanged.connect(self.on_hydrophone_params_changed)
        hydrophone_layout.addRow("旋转角度 (°):", self.hydrophone_rotation)
        
        hydrophone_group.setLayout(hydrophone_layout)
        control_layout.addWidget(hydrophone_group)
        
        # 换能器控制
        transducer_group = QGroupBox("换能器控制")
        transducer_layout = QFormLayout()
        
        # X坐标
        self.transducer_x = QDoubleSpinBox()
        self.transducer_x.setRange(-1.0, 1.0)
        self.transducer_x.setSingleStep(0.01)
        self.transducer_x.setValue(-0.25)  # 默认位置在左侧，距中心0.25米
        self.transducer_x.valueChanged.connect(self.on_transducer_params_changed)
        transducer_layout.addRow("X坐标 (m):", self.transducer_x)
        
        # Y坐标
        self.transducer_y = QDoubleSpinBox()
        self.transducer_y.setRange(-0.25, 0.25)
        self.transducer_y.setSingleStep(0.01)
        self.transducer_y.valueChanged.connect(self.on_transducer_params_changed)
        transducer_layout.addRow("Y坐标 (m):", self.transducer_y)
        
        # Z坐标
        self.transducer_z = QDoubleSpinBox()
        self.transducer_z.setRange(0.3, 1.0)
        self.transducer_z.setSingleStep(0.01)
        self.transducer_z.setValue(0.5)  # 默认深度0.5米
        self.transducer_z.valueChanged.connect(self.on_transducer_params_changed)
        transducer_layout.addRow("Z坐标 (m):", self.transducer_z)
        
        # 旋转角度
        self.transducer_rotation = QDoubleSpinBox()
        self.transducer_rotation.setRange(0.0, 360.0)
        self.transducer_rotation.setSingleStep(1.0)
        self.transducer_rotation.valueChanged.connect(self.on_transducer_params_changed)
        transducer_layout.addRow("旋转角度 (°):", self.transducer_rotation)
        
        transducer_group.setLayout(transducer_layout)
        control_layout.addWidget(transducer_group)
        
        # 水池参数
        pool_params_group = QGroupBox("水池参数")
        pool_params_layout = QFormLayout()
        
        # 水深
        self.water_depth = QDoubleSpinBox()
        self.water_depth.setRange(0.0, 1.0)
        self.water_depth.setSingleStep(0.01)
        self.water_depth.setValue(0.8)  # 默认水深0.8米
        self.water_depth.valueChanged.connect(self.on_pool_params_changed)
        pool_params_layout.addRow("水深 (m):", self.water_depth)
        
        pool_params_group.setLayout(pool_params_layout)
        control_layout.addWidget(pool_params_group)
        
        # 填充空间
        control_layout.addStretch()
        
        return control_widget

    def export_waveform(self):
        """导出波形数据为CSV文件"""
        if not hasattr(self, 'current_wave_data') or not self.current_wave_data:
            QMessageBox.warning(self, "警告", "没有可导出的波形数据，请先采集数据")
            return
        
        # 获取保存文件路径
        filename, _ = QFileDialog.getSaveFileName(
            self, 
            "导出波形数据", 
            os.path.join(os.path.expanduser("~"), "waveform_data.csv"),
            "CSV文件 (*.csv);;所有文件 (*.*)"
        )
        
        if not filename:
            return
        
        try:
            import csv
            import numpy as np
            
            # 准备数据
            time_data = self.current_wave_data['time']
            amplitude_data = self.current_wave_data['amplitude']
            
            # 写入CSV文件
            with open(filename, 'w', newline='') as csvfile:
                writer = csv.writer(csvfile)
                
                # 写入文件头
                writer.writerow([
                    f"信号类型: {self.current_wave_data['signal_type']}",
                    f"频率: {self.current_wave_data['signal_freq']} Hz",
                    f"采样率: {self.current_wave_data['sample_rate']} Hz",
                    f"数据点数: {len(amplitude_data)}"
                ])
                
                # 写入列标题
                writer.writerow(["时间 (s)", "振幅"])
                
                # 写入数据
                for i in range(len(time_data)):
                    writer.writerow([time_data[i], amplitude_data[i]])
            
            self.statusBar().showMessage(f"波形数据已导出到 {filename}", 5000)
            QMessageBox.information(self, "导出成功", f"波形数据已成功导出到:\n{filename}")
            
        except Exception as e:
            QMessageBox.critical(self, "导出失败", f"导出波形数据时出错:\n{str(e)}")
    
    def show_zoom_help(self):
        """显示波形缩放帮助"""
        message = """放大方式：
1. 选择工具栏上的放大图标，然后在波形上拖拽选择区域进行放大
2. 或使用"区域放大"按钮，直接拖拽选择要放大的区域
3. 滚动鼠标滚轮也可以进行放大和缩小

其他操作：
- 选择工具栏上的移动图标可以平移波形
- 点击"还原"按钮或"重置视图"恢复到原始视图
- 右键点击波形可以添加测量标记"""
        QMessageBox.information(self, "波形缩放帮助", message)
    
    def activate_box_zoom(self, checked):
        """激活区域放大模式"""
        if not hasattr(self, 'data_toolbar'):
            return
            
        if checked:
            # 激活区域放大模式
            self.data_toolbar.mode = self.data_toolbar._active = 'ZOOM'
            self.data_canvas.setFocus()
            self.data_canvas.setCursor(Qt.CrossCursor)
        else:
            # 取消区域放大模式
            self.data_toolbar._active = None
            self.data_canvas.setCursor(Qt.ArrowCursor)