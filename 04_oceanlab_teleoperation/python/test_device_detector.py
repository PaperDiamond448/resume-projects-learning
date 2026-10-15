#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
设备识别测试脚本
"""

import sys
import os
import time
from PyQt5.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget, QTableWidget, QTableWidgetItem, QHeaderView, QPushButton, QLabel, QHBoxLayout
from PyQt5.QtCore import Qt, QTimer

# 添加父目录到系统路径
current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from client.device_detector import DeviceDetector
from client.device_manager import DeviceManager

class DeviceDetectorTestWindow(QMainWindow):
    """设备识别测试窗口"""
    
    def __init__(self):
        super().__init__()
        
        # 设置窗口属性
        self.setWindowTitle("设备识别测试")
        self.resize(800, 600)
        
        # 创建设备管理器
        self.device_manager = DeviceManager(debug=True)
        
        # 设置UI
        self.setup_ui()
        
        # 启动设备管理器
        self.device_manager.start()
        self.status_label.setText("设备管理器已启动")
        
        # 设置定时器，定期刷新设备列表
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_device_list)
        self.timer.start(2000)  # 每2秒刷新一次
    
    def setup_ui(self):
        """设置UI"""
        # 创建中央部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 主布局
        main_layout = QVBoxLayout(central_widget)
        
        # 控制按钮
        control_layout = QHBoxLayout()
        
        # 刷新按钮
        refresh_button = QPushButton("刷新设备列表")
        refresh_button.clicked.connect(self.refresh_device_list)
        control_layout.addWidget(refresh_button)
        
        # 状态标签
        self.status_label = QLabel("就绪")
        control_layout.addWidget(self.status_label)
        
        # 添加控制布局
        main_layout.addLayout(control_layout)
        
        # 设备列表表格
        self.device_table = QTableWidget(0, 6)  # 6列：设备ID, 设备名称, 设备类型, 连接方式, 状态, 操作
        self.device_table.setHorizontalHeaderLabels(["设备ID", "设备名称", "设备类型", "连接方式", "状态", "操作"])
        self.device_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.device_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SingleSelection)
        self.device_table.setEditTriggers(QTableWidget.NoEditTriggers)
        
        main_layout.addWidget(self.device_table)
        
        # 设备状态表格
        self.status_table = QTableWidget(0, 4)  # 4列：设备ID, 设备类型, 状态, 位置
        self.status_table.setHorizontalHeaderLabels(["设备ID", "设备类型", "状态", "位置"])
        self.status_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.status_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.status_table.setSelectionMode(QTableWidget.SingleSelection)
        self.status_table.setEditTriggers(QTableWidget.NoEditTriggers)
        
        main_layout.addWidget(self.status_table)
        
        # 连接设备管理器信号
        self.device_manager.devices_updated.connect(self.on_devices_updated)
        self.device_manager.device_connected.connect(self.on_device_connected)
        self.device_manager.device_disconnected.connect(self.on_device_disconnected)
        self.device_manager.device_status_updated.connect(self.on_device_status_updated)
        self.device_manager.device_error.connect(self.on_device_error)
    
    def refresh_device_list(self):
        """刷新设备列表"""
        # 获取设备列表
        devices = self.device_manager.get_all_available_devices()
        
        # 清空表格
        self.device_table.setRowCount(0)
        
        # 添加设备到表格
        for i, device in enumerate(devices):
            self.device_table.insertRow(i)
            
            # 设备ID
            id_item = QTableWidgetItem(device["id"])
            self.device_table.setItem(i, 0, id_item)
            
            # 设备名称
            name_item = QTableWidgetItem(device["name"])
            self.device_table.setItem(i, 1, name_item)
            
            # 设备类型
            type_item = QTableWidgetItem(self.get_device_type_name(device["type"]))
            self.device_table.setItem(i, 2, type_item)
            
            # 连接方式
            conn_item = QTableWidgetItem(self.get_connection_type_name(device["connection_type"]))
            self.device_table.setItem(i, 3, conn_item)
            
            # 状态
            status = "未连接"
            if self.device_manager.get_device(device["id"]):
                status = "已连接"
            status_item = QTableWidgetItem(status)
            self.device_table.setItem(i, 4, status_item)
            
            # 操作按钮
            if status == "未连接":
                connect_button = QPushButton("连接")
                connect_button.clicked.connect(lambda checked, d_id=device["id"]: self.connect_device(d_id))
                self.device_table.setCellWidget(i, 5, connect_button)
            else:
                disconnect_button = QPushButton("断开")
                disconnect_button.clicked.connect(lambda checked, d_id=device["id"]: self.disconnect_device(d_id))
                self.device_table.setCellWidget(i, 5, disconnect_button)
        
        # 刷新状态表格
        self.refresh_status_table()
    
    def refresh_status_table(self):
        """刷新状态表格"""
        # 获取设备状态
        device_status = self.device_manager.get_all_device_status()
        
        # 清空表格
        self.status_table.setRowCount(0)
        
        # 添加设备状态到表格
        for i, (device_id, status) in enumerate(device_status.items()):
            self.status_table.insertRow(i)
            
            # 设备ID
            id_item = QTableWidgetItem(device_id)
            self.status_table.setItem(i, 0, id_item)
            
            # 设备类型
            device_type = status.get("device_type", "")
            type_item = QTableWidgetItem(self.get_device_type_name(device_type))
            self.status_table.setItem(i, 1, type_item)
            
            # 状态
            status_text = status.get("status", "")
            status_names = {
                "disconnected": "已断开",
                "connected": "已连接",
                "error": "错误",
                "moving": "移动中",
                "ready": "就绪"
            }
            status_item = QTableWidgetItem(status_names.get(status_text, status_text))
            self.status_table.setItem(i, 2, status_item)
            
            # 位置
            position = status.get("position", {})
            if position:
                pos_text = f"X: {position.get('x', 0.0):.2f}, Y: {position.get('y', 0.0):.2f}, Z: {position.get('z', 0.0):.2f}, 角度: {position.get('angle', 0.0):.1f}°"
                pos_item = QTableWidgetItem(pos_text)
                self.status_table.setItem(i, 3, pos_item)
            else:
                self.status_table.setItem(i, 3, QTableWidgetItem(""))
    
    def get_device_type_name(self, device_type):
        """获取设备类型名称
        
        Args:
            device_type (str): 设备类型
        
        Returns:
            str: 设备类型名称
        """
        type_names = {
            "robot_arm": "两自由度机械臂",
            "hydrophone": "水听器",
            "transducer": "换能器",
            "network_device": "网络设备",
            "unknown": "未知设备"
        }
        return type_names.get(device_type, "未知设备")
    
    def get_connection_type_name(self, connection_type):
        """获取连接方式名称
        
        Args:
            connection_type (str): 连接方式
        
        Returns:
            str: 连接方式名称
        """
        conn_names = {
            "usb": "USB串口",
            "network": "网络连接",
            "bluetooth": "蓝牙连接"
        }
        return conn_names.get(connection_type, "未知连接")
    
    def connect_device(self, device_id):
        """连接设备
        
        Args:
            device_id (str): 设备ID
        """
        self.status_label.setText(f"正在连接设备: {device_id}")
        
        if self.device_manager.connect_device(device_id):
            self.status_label.setText(f"已连接到设备: {device_id}")
            self.refresh_device_list()
        else:
            self.status_label.setText(f"连接设备失败: {device_id}")
    
    def disconnect_device(self, device_id):
        """断开设备连接
        
        Args:
            device_id (str): 设备ID
        """
        self.status_label.setText(f"正在断开设备连接: {device_id}")
        
        if self.device_manager.disconnect_device(device_id):
            self.status_label.setText(f"已断开与设备的连接: {device_id}")
            self.refresh_device_list()
        else:
            self.status_label.setText(f"断开设备连接失败: {device_id}")
    
    def on_devices_updated(self, devices):
        """设备列表更新处理
        
        Args:
            devices (list): 设备列表
        """
        self.refresh_device_list()
    
    def on_device_connected(self, device_info):
        """设备连接处理
        
        Args:
            device_info (dict): 设备信息
        """
        device_id = device_info["id"]
        self.status_label.setText(f"设备已连接: {device_info['name']} ({device_id})")
        self.refresh_device_list()
    
    def on_device_disconnected(self, device_id):
        """设备断开处理
        
        Args:
            device_id (str): 设备ID
        """
        self.status_label.setText(f"设备已断开: {device_id}")
        self.refresh_device_list()
    
    def on_device_status_updated(self, status):
        """设备状态更新处理
        
        Args:
            status (dict): 设备状态
        """
        self.refresh_status_table()
    
    def on_device_error(self, device_id, error_message):
        """设备错误处理
        
        Args:
            device_id (str): 设备ID
            error_message (str): 错误消息
        """
        self.status_label.setText(f"设备错误: {device_id} - {error_message}")
    
    def closeEvent(self, event):
        """窗口关闭事件处理
        
        Args:
            event: 关闭事件
        """
        # 停止设备管理器
        if self.device_manager:
            self.device_manager.stop()
        
        # 接受关闭事件
        event.accept()

def main():
    """主函数"""
    app = QApplication(sys.argv)
    window = DeviceDetectorTestWindow()
    window.show()
    return app.exec_()

if __name__ == "__main__":
    sys.exit(main()) 