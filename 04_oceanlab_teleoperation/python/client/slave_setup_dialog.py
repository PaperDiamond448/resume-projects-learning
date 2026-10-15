#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
从端(Slave)启动设置对话框
"""

import os
import sys
from PyQt5.QtWidgets import (QDialog, QVBoxLayout, QHBoxLayout, QLabel, 
                            QPushButton, QComboBox, QLineEdit, QFormLayout, 
                            QGroupBox, QRadioButton, QButtonGroup, QSpinBox,
                            QMessageBox, QApplication, QTextEdit)
from PyQt5.QtCore import Qt, QSettings
from PyQt5.QtGui import QFont, QIcon

# 添加父目录到系统路径
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# 导入工具类
from utils import NetworkDiagnostics

class SlaveSetupDialog(QDialog):
    """从端启动设置对话框"""
    
    # 实验池端口映射
    POOL_PORTS = {
        "pool1": [8887, 8888],
        "pool2": [8885, 8886],
        "pool3": [8883, 8884]
    }
    
    def __init__(self, parent=None):
        """初始化对话框"""
        super().__init__(parent)
        
        self.setWindowTitle("OceanLab-Python 从端设置")
        self.setMinimumWidth(400)
        self.setMinimumHeight(300)
        
        # 加载保存的配置
        self.settings = QSettings("OceanLab", "SlaveApp")
        
        # 设置字体以支持中文
        font = QFont("Microsoft YaHei", 9)  # 使用微软雅黑字体
        self.setFont(font)
        
        self.setup_ui()
        self.load_settings()
        
        # 连接池ID变更信号
        self.pool_id_group.buttonClicked.connect(self.on_pool_id_changed)
        
        # 手动触发一次更新端口
        self.on_pool_id_changed()
    
    def setup_ui(self):
        """设置UI"""
        layout = QVBoxLayout(self)
        layout.setSpacing(10)
        
        # 标题
        title_label = QLabel("OceanLab-Python 从端配置")
        title_label.setAlignment(Qt.AlignCenter)
        font = title_label.font()
        font.setPointSize(12)
        font.setBold(True)
        title_label.setFont(font)
        layout.addWidget(title_label)
        
        # 实验池选择
        pool_group = QGroupBox("实验池ID")
        pool_layout = QVBoxLayout()
        
        # 标准实验池选择按钮组
        self.pool_id_group = QButtonGroup(self)
        
        # 添加预定义的实验池选项
        self.pool1_radio = QRadioButton("实验池 1 (pool1)")
        self.pool2_radio = QRadioButton("实验池 2 (pool2)")
        self.pool3_radio = QRadioButton("实验池 3 (pool3)")
        
        self.pool_id_group.addButton(self.pool1_radio, 1)
        self.pool_id_group.addButton(self.pool2_radio, 2)
        self.pool_id_group.addButton(self.pool3_radio, 3)
        
        pool_layout.addWidget(self.pool1_radio)
        pool_layout.addWidget(self.pool2_radio)
        pool_layout.addWidget(self.pool3_radio)
        
        # 自定义实验池ID选项
        self.custom_pool_radio = QRadioButton("自定义:")
        self.pool_id_group.addButton(self.custom_pool_radio, 4)
        
        custom_pool_layout = QHBoxLayout()
        custom_pool_layout.addWidget(self.custom_pool_radio)
        self.custom_pool_input = QLineEdit()
        self.custom_pool_input.setPlaceholderText("输入自定义实验池ID")
        custom_pool_layout.addWidget(self.custom_pool_input)
        
        pool_layout.addLayout(custom_pool_layout)
        pool_group.setLayout(pool_layout)
        layout.addWidget(pool_group)
        
        # 服务器设置
        server_group = QGroupBox("服务器设置")
        server_layout = QVBoxLayout()
        
        form_layout = QFormLayout()
        self.server_address = QLineEdit()
        self.server_address.setPlaceholderText("例如: localhost 或 192.168.1.100")
        form_layout.addRow("服务器地址:", self.server_address)
        
        # 修改端口选择为下拉框
        self.server_port_combo = QComboBox()
        form_layout.addRow("服务器端口:", self.server_port_combo)
        
        server_layout.addLayout(form_layout)
        
        # 添加测试连接按钮
        test_layout = QHBoxLayout()
        self.test_connection_button = QPushButton("测试连接")
        self.test_connection_button.clicked.connect(self.test_connection)
        test_layout.addWidget(self.test_connection_button)
        
        # 连接状态标签
        self.connection_status = QLabel("未测试")
        test_layout.addWidget(self.connection_status)
        
        server_layout.addLayout(test_layout)
        server_group.setLayout(server_layout)
        layout.addWidget(server_group)
        
        # 高级设置
        advanced_group = QGroupBox("高级设置")
        advanced_layout = QVBoxLayout()
        
        self.debug_mode_cb = QComboBox()
        self.debug_mode_cb.addItem("关闭调试模式", False)
        self.debug_mode_cb.addItem("开启调试模式", True)
        advanced_layout.addWidget(QLabel("调试模式:"))
        advanced_layout.addWidget(self.debug_mode_cb)
        
        advanced_group.setLayout(advanced_layout)
        layout.addWidget(advanced_group)
        
        # 按钮
        button_layout = QHBoxLayout()
        
        self.start_button = QPushButton("启动")
        self.start_button.clicked.connect(self.accept)
        button_layout.addWidget(self.start_button)
        
        self.cancel_button = QPushButton("取消")
        self.cancel_button.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_button)
        
        layout.addLayout(button_layout)
    
    def on_pool_id_changed(self):
        """实验池ID变更事件处理"""
        # 获取当前选择的池ID
        pool_id = self.get_pool_id()
        
        # 清空端口下拉框
        self.server_port_combo.clear()
        
        # 根据实验池选择添加相应的端口
        if pool_id in self.POOL_PORTS:
            for port in self.POOL_PORTS[pool_id]:
                self.server_port_combo.addItem(str(port), port)
        else:
            # 自定义实验池ID或未识别的ID，添加全部可用端口
            all_ports = []
            for ports in self.POOL_PORTS.values():
                all_ports.extend(ports)
            for port in sorted(all_ports):
                self.server_port_combo.addItem(str(port), port)
        
        # 添加默认端口8000
        self.server_port_combo.addItem("8000", 8000)
    
    def test_connection(self):
        """测试服务器连接"""
        host = self.server_address.text().strip()
        port = self.get_server_port()
        
        if not host:
            QMessageBox.warning(self, "输入错误", "请输入有效的服务器地址")
            return
        
        self.connection_status.setText("正在测试...")
        self.connection_status.setStyleSheet("color: orange")
        QApplication.processEvents()
        
        try:
            # 使用诊断工具测试连接
            results = NetworkDiagnostics.diagnose_connection(host, port)
            
            if results["overall_status"] == "可连接":
                self.connection_status.setText("连接正常")
                self.connection_status.setStyleSheet("color: green")
                
                # 显示详细连接信息
                msg = f"服务器连接正常!\n\n本机IP: {results['local_ip']}\n"
                
                if "dns_resolution" in results:
                    if results["dns_resolution"]["success"]:
                        msg += f"服务器IP: {results['dns_resolution']['ip_address']}\n"
                
                msg += f"端口检查: {results['port_check']['message']}\n"
                
                if "loopback_check" in results:
                    msg += f"本地环回: {results['loopback_check']['message']}\n"
                
                QMessageBox.information(self, "连接测试", msg)
            else:
                self.connection_status.setText("连接失败")
                self.connection_status.setStyleSheet("color: red")
                
                # 显示详细错误信息
                msg = f"无法连接到服务器!\n\n本机IP: {results['local_ip']}\n"
                
                if "dns_resolution" in results:
                    if results["dns_resolution"]["success"]:
                        msg += f"服务器IP: {results['dns_resolution']['ip_address']}\n"
                    else:
                        msg += f"DNS解析: {results['dns_resolution']['error']}\n"
                
                msg += f"端口检查: {results['port_check']['message']}\n"
                
                if "loopback_check" in results:
                    msg += f"本地环回: {results['loopback_check']['message']}\n"
                
                msg += "\n可能的解决方案:\n"
                msg += "1. 检查服务器地址和端口是否正确\n"
                msg += "2. 确保服务器已启动并监听指定端口\n"
                msg += "3. 检查网络连接和防火墙设置\n"
                
                QMessageBox.warning(self, "连接测试", msg)
        except Exception as e:
            self.connection_status.setText("测试错误")
            self.connection_status.setStyleSheet("color: red")
            QMessageBox.critical(self, "连接测试错误", f"测试连接时出错: {str(e)}")
    
    def get_pool_id(self):
        """获取选择的实验池ID"""
        button_id = self.pool_id_group.checkedId()
        
        if button_id == 1:
            return "pool1"
        elif button_id == 2:
            return "pool2"
        elif button_id == 3:
            return "pool3"
        elif button_id == 4:
            # 获取自定义实验池ID
            custom_id = self.custom_pool_input.text().strip()
            return custom_id if custom_id else "pool1"  # 默认为pool1
        
        # 默认返回
        return "pool1"
    
    def get_server_address(self):
        """获取服务器地址"""
        address = self.server_address.text().strip()
        return address if address else "localhost"
    
    def get_server_port(self):
        """获取服务器端口"""
        return self.server_port_combo.currentData()
    
    def is_debug_mode(self):
        """是否开启调试模式"""
        return self.debug_mode_cb.currentData()
    
    def load_settings(self):
        """加载保存的设置"""
        # 实验池ID
        pool_id = self.settings.value("poolId", "pool1")
        
        if pool_id == "pool1":
            self.pool1_radio.setChecked(True)
        elif pool_id == "pool2":
            self.pool2_radio.setChecked(True)
        elif pool_id == "pool3":
            self.pool3_radio.setChecked(True)
        else:
            self.custom_pool_radio.setChecked(True)
            self.custom_pool_input.setText(pool_id)
        
        # 触发端口更新
        self.on_pool_id_changed()
        
        # 服务器地址
        server_address = self.settings.value("serverAddress", "localhost")
        self.server_address.setText(server_address)
        
        # 服务器端口
        server_port = int(self.settings.value("serverPort", 8000))
        
        # 设置当前端口
        for i in range(self.server_port_combo.count()):
            if self.server_port_combo.itemData(i) == server_port:
                self.server_port_combo.setCurrentIndex(i)
                break
        
        # 调试模式
        debug_mode = self.settings.value("debugMode", "false").lower() == "true"
        self.debug_mode_cb.setCurrentIndex(1 if debug_mode else 0)
    
    def save_settings(self):
        """保存设置"""
        self.settings.setValue("poolId", self.get_pool_id())
        self.settings.setValue("serverAddress", self.get_server_address())
        self.settings.setValue("serverPort", self.get_server_port())
        self.settings.setValue("debugMode", "true" if self.is_debug_mode() else "false")
    
    def accept(self):
        """确认按钮点击"""
        # 验证输入
        pool_id = self.get_pool_id()
        server_address = self.get_server_address()
        
        if not pool_id:
            QMessageBox.warning(self, "输入错误", "请输入有效的实验池ID")
            return
        
        if not server_address:
            QMessageBox.warning(self, "输入错误", "请输入有效的服务器地址")
            return
        
        # 保存设置
        self.save_settings()
        
        # 接受对话框
        super().accept()
    
    @staticmethod
    def get_slave_setup_params(parent=None):
        """获取从端设置参数
        
        Returns:
            dict: 包含配置参数的字典，如果用户取消则返回None
        """
        dialog = SlaveSetupDialog(parent)
        result = dialog.exec_()
        
        if result == QDialog.Accepted:
            return {
                "pool_id": dialog.get_pool_id(),
                "server_address": dialog.get_server_address(),
                "server_port": dialog.get_server_port(),
                "debug_mode": dialog.is_debug_mode()
            }
        
        return None


# 测试代码
if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    # 设置全局字体以支持中文
    font = QFont("Microsoft YaHei")  # 微软雅黑
    app.setFont(font)
    
    params = SlaveSetupDialog.get_slave_setup_params()
    if params:
        print("配置参数:", params)
    else:
        print("用户取消了设置")
    
    sys.exit(0) 