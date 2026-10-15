#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
连接配置对话框
"""

from PyQt5.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                            QPushButton, QComboBox, QLineEdit, QGroupBox,
                            QFormLayout, QMessageBox, QFrame, QSpacerItem,
                            QSizePolicy)
from PyQt5.QtCore import Qt, pyqtSlot, pyqtSignal
from PyQt5.QtGui import QIntValidator, QRegExpValidator
from PyQt5.QtCore import QRegExp

class ConnectionDialog(QDialog):
    """连接配置对话框"""
    
    # 定义连接信息设置信号
    connection_set = pyqtSignal(int, str)  # 端口, IP地址
    
    def __init__(self, auth_manager, parent=None):
        """初始化连接配置对话框
        
        Args:
            auth_manager: 认证管理器实例
            parent: 父窗口
        """
        super(ConnectionDialog, self).__init__(parent)
        
        # 保存认证管理器引用
        self.auth_manager = auth_manager
        
        # 可用端口列表
        self.available_ports = self.auth_manager.get_available_ports()
        
        # 当前连接信息
        self.current_port, self.current_ip = self.auth_manager.get_connection_info()
        if not self.current_port and self.available_ports:
            self.current_port = self.available_ports[0]
        if not self.current_ip:
            self.current_ip = "localhost"
            
        # 设置对话框属性
        self.setWindowTitle("连接配置")
        self.setFixedWidth(400)
        
        # 初始化用户界面
        self._init_ui()
        
        # 连接信号槽
        self._connect_signals()
        
    def _init_ui(self):
        """初始化用户界面"""
        # 主布局
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(20, 20, 20, 20)
        
        # 创建主框架
        main_frame = QFrame()
        main_frame.setStyleSheet("""
            QFrame {
                background-color: white;
                border-radius: 10px;
                border: 1px solid #e0e0e0;
            }
            QLabel {
                color: #333333;
            }
            QComboBox, QLineEdit {
                padding: 8px;
                border: 1px solid #e0e0e0;
                border-radius: 4px;
                background-color: #f5f5f5;
            }
            QComboBox:focus, QLineEdit:focus {
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
            QPushButton#connectButton {
                background-color: #1976D2;
                color: white;
            }
            QPushButton#connectButton:hover {
                background-color: #1565C0;
            }
            QPushButton#connectButton:disabled {
                background-color: #BBDEFB;
            }
            QPushButton#cancelButton:hover {
                background-color: #E0E0E0;
            }
        """)
        
        frame_layout = QVBoxLayout(main_frame)
        
        # 标题
        title_label = QLabel("实验池连接配置")
        title_label.setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 10px;")
        title_label.setAlignment(Qt.AlignCenter)
        frame_layout.addWidget(title_label)
        
        # 说明标签
        info_label = QLabel("请配置与实验池的连接参数")
        info_label.setAlignment(Qt.AlignCenter)
        frame_layout.addWidget(info_label)
        
        # 分隔线
        separator = QFrame()
        separator.setFrameShape(QFrame.HLine)
        separator.setFrameShadow(QFrame.Sunken)
        separator.setStyleSheet("background-color: #e0e0e0; margin: 10px 0;")
        frame_layout.addWidget(separator)
        
        # 连接配置表单
        form_layout = QFormLayout()
        form_layout.setSpacing(10)
        
        # 实验池信息
        pool_id = self.auth_manager.get_reserved_pool_id() or "未指定"
        pool_label = QLabel(f"当前实验池: {pool_id}")
        pool_label.setStyleSheet("color: #1976D2;")
        form_layout.addRow("", pool_label)
        
        # 端口选择
        port_label = QLabel("端口:")
        self.port_combo = QComboBox()
        
        # 添加可用端口
        if self.available_ports:
            for port in self.available_ports:
                self.port_combo.addItem(str(port))
            
            # 设置当前端口
            if self.current_port and str(self.current_port) in [self.port_combo.itemText(i) for i in range(self.port_combo.count())]:
                self.port_combo.setCurrentText(str(self.current_port))
        else:
            self.port_combo.addItem("无可用端口")
            self.port_combo.setEnabled(False)
            
        form_layout.addRow("监听端口:", self.port_combo)
        
        # IP地址输入
        ip_label = QLabel("IP地址:")
        self.ip_input = QLineEdit()
        self.ip_input.setText(self.current_ip)
        self.ip_input.setPlaceholderText("输入IP地址 (默认: localhost)")
        
        # IP地址验证器
        ip_regexp = QRegExp("^(localhost|((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))$")
        ip_validator = QRegExpValidator(ip_regexp)
        self.ip_input.setValidator(ip_validator)
        
        form_layout.addRow("IP地址:", self.ip_input)
        
        frame_layout.addLayout(form_layout)
        
        # 弹性空间
        spacer = QSpacerItem(20, 20, QSizePolicy.Minimum, QSizePolicy.Expanding)
        frame_layout.addItem(spacer)
        
        # 按钮区域
        button_layout = QHBoxLayout()
        button_layout.setSpacing(10)
        
        # 取消按钮
        self.cancel_button = QPushButton("取消")
        self.cancel_button.setObjectName("cancelButton")
        button_layout.addWidget(self.cancel_button)
        
        # 连接按钮
        self.connect_button = QPushButton("保存并连接")
        self.connect_button.setObjectName("connectButton")
        button_layout.addWidget(self.connect_button)
        
        frame_layout.addLayout(button_layout)
        
        # 添加主框架到主布局
        main_layout.addWidget(main_frame)
        
    def _connect_signals(self):
        """连接信号槽"""
        # 取消按钮
        self.cancel_button.clicked.connect(self.reject)
        
        # 连接按钮
        self.connect_button.clicked.connect(self.on_connect)
        
        # IP地址输入框变化
        self.ip_input.textChanged.connect(self.validate_input)
        
    def validate_input(self):
        """验证输入"""
        ip = self.ip_input.text().strip()
        
        # IP地址验证
        if not ip or ip == "localhost" or self._is_valid_ip(ip):
            self.connect_button.setEnabled(True)
        else:
            self.connect_button.setEnabled(False)
    
    def _is_valid_ip(self, ip):
        """验证IP地址是否有效"""
        parts = ip.split(".")
        if len(parts) != 4:
            return False
        
        for part in parts:
            try:
                value = int(part)
                if value < 0 or value > 255:
                    return False
            except ValueError:
                return False
                
        return True
        
    def on_connect(self):
        """连接按钮点击事件"""
        # 获取端口和IP地址
        port = int(self.port_combo.currentText()) if self.port_combo.currentText().isdigit() else None
        ip = self.ip_input.text().strip() or "localhost"
        
        # 验证端口是否可用
        if not port or port not in self.available_ports:
            QMessageBox.warning(self, "错误", "请选择有效的端口")
            return
            
        # 验证IP地址是否有效
        if ip != "localhost" and not self._is_valid_ip(ip):
            QMessageBox.warning(self, "错误", "请输入有效的IP地址")
            return
            
        # 设置连接信息
        if self.auth_manager.set_connection_info(port, ip):
            # 发送连接信息设置信号
            self.connection_set.emit(port, ip)
            
            # 接受对话框
            self.accept()
        else:
            QMessageBox.warning(self, "错误", "连接信息设置失败") 