#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
机器人设备界面：用于在从端显示设备信息和控制设备
"""

import os
import sys
from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                            QLabel, QPushButton, QGroupBox, QFormLayout,
                            QTableWidget, QTableWidgetItem, QHeaderView,
                            QComboBox, QCheckBox, QTabWidget, QSplitter,
                            QMessageBox, QDoubleSpinBox, QSlider)
from PyQt5.QtCore import Qt, pyqtSignal, pyqtSlot, QTimer
from PyQt5.QtGui import QFont, QIcon, QColor

class RobotDevicePanel(QWidget):
    """机器人设备界面"""
    
    def __init__(self, device_manager=None, parent=None):
        """初始化机器人设备界面
        
        Args:
            device_manager: 设备管理器
            parent: 父控件
        """
        super(RobotDevicePanel, self).__init__(parent)
        
        self.device_manager = device_manager
        
        # 初始化UI
        self.setup_ui()
        
        # 连接设备管理器信号
        if self.device_manager:
            self.device_manager.devices_updated.connect(self.update_device_list)
            self.device_manager.device_connected.connect(self.on_device_connected)
            self.device_manager.device_disconnected.connect(self.on_device_disconnected)
            self.device_manager.device_status_updated.connect(self.update_device_status)
            self.device_manager.device_error.connect(self.on_device_error)
        
        # 刷新设备列表
        self.refresh_device_list()
        
        # 定时刷新设备状态
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.refresh_device_status)
        self.status_timer.start(1000)  # 每秒刷新一次
    
    def setup_ui(self):
        """设置UI"""
        # 主布局
        main_layout = QVBoxLayout(self)
        
        # 创建分割器
        splitter = QSplitter(Qt.Vertical)
        main_layout.addWidget(splitter)
        
        # 设备列表组
        device_list_group = QGroupBox("已识别设备")
        device_list_layout = QVBoxLayout(device_list_group)
        
        # 设备列表表格
        self.device_table = QTableWidget(0, 5)  # 5列：设备ID, 设备名称, 设备类型, 连接方式, 状态
        self.device_table.setHorizontalHeaderLabels(["设备ID", "设备名称", "设备类型", "连接方式", "状态"])
        self.device_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.device_table.setSelectionBehavior(QTableWidget.SelectRows)
        self.device_table.setSelectionMode(QTableWidget.SingleSelection)
        self.device_table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.device_table.itemClicked.connect(self.on_device_selected)
        
        device_list_layout.addWidget(self.device_table)
        
        # 设备操作按钮
        device_buttons_layout = QHBoxLayout()
        
        self.refresh_button = QPushButton("刷新设备列表")
        self.refresh_button.clicked.connect(self.refresh_device_list)
        device_buttons_layout.addWidget(self.refresh_button)
        
        self.connect_button = QPushButton("连接设备")
        self.connect_button.clicked.connect(self.connect_selected_device)
        self.connect_button.setEnabled(False)
        device_buttons_layout.addWidget(self.connect_button)
        
        self.disconnect_button = QPushButton("断开设备")
        self.disconnect_button.clicked.connect(self.disconnect_selected_device)
        self.disconnect_button.setEnabled(False)
        device_buttons_layout.addWidget(self.disconnect_button)
        
        device_list_layout.addLayout(device_buttons_layout)
        
        # 添加到分割器
        splitter.addWidget(device_list_group)
        
        # 设备详情标签页
        self.device_tabs = QTabWidget()
        
        # 设备信息标签页
        self.info_tab = QWidget()
        info_layout = QVBoxLayout(self.info_tab)
        
        # 设备信息表单
        self.info_form = QFormLayout()
        
        self.device_id_label = QLabel("未选择设备")
        self.info_form.addRow("设备ID:", self.device_id_label)
        
        self.device_name_label = QLabel("")
        self.info_form.addRow("设备名称:", self.device_name_label)
        
        self.device_type_label = QLabel("")
        self.info_form.addRow("设备类型:", self.device_type_label)
        
        self.device_connection_label = QLabel("")
        self.info_form.addRow("连接方式:", self.device_connection_label)
        
        self.device_status_label = QLabel("")
        self.info_form.addRow("设备状态:", self.device_status_label)
        
        self.device_position_label = QLabel("")
        self.info_form.addRow("当前位置:", self.device_position_label)
        
        info_layout.addLayout(self.info_form)
        info_layout.addStretch()
        
        # 设备控制标签页
        self.control_tab = QWidget()
        control_layout = QVBoxLayout(self.control_tab)
        
        # 位置控制组
        position_group = QGroupBox("位置控制")
        position_layout = QGridLayout(position_group)
        
        # X坐标
        position_layout.addWidget(QLabel("X坐标 (m):"), 0, 0)
        self.x_spinbox = QDoubleSpinBox()
        self.x_spinbox.setRange(-1.0, 1.0)
        self.x_spinbox.setSingleStep(0.01)
        self.x_spinbox.setDecimals(2)
        position_layout.addWidget(self.x_spinbox, 0, 1)
        
        # Y坐标
        position_layout.addWidget(QLabel("Y坐标 (m):"), 1, 0)
        self.y_spinbox = QDoubleSpinBox()
        self.y_spinbox.setRange(-0.25, 0.25)
        self.y_spinbox.setSingleStep(0.01)
        self.y_spinbox.setDecimals(2)
        position_layout.addWidget(self.y_spinbox, 1, 1)
        
        # Z坐标
        position_layout.addWidget(QLabel("Z坐标 (m):"), 2, 0)
        self.z_spinbox = QDoubleSpinBox()
        self.z_spinbox.setRange(0.3, 1.0)
        self.z_spinbox.setSingleStep(0.01)
        self.z_spinbox.setDecimals(2)
        position_layout.addWidget(self.z_spinbox, 2, 1)
        
        # 角度
        position_layout.addWidget(QLabel("角度 (°):"), 3, 0)
        self.angle_spinbox = QDoubleSpinBox()
        self.angle_spinbox.setRange(0.0, 360.0)
        self.angle_spinbox.setSingleStep(1.0)
        self.angle_spinbox.setDecimals(1)
        position_layout.addWidget(self.angle_spinbox, 3, 1)
        
        # 移动按钮
        self.move_button = QPushButton("移动到位置")
        self.move_button.clicked.connect(self.move_to_position)
        self.move_button.setEnabled(False)
        position_layout.addWidget(self.move_button, 4, 0, 1, 2)
        
        # 停止按钮
        self.stop_button = QPushButton("停止移动")
        self.stop_button.clicked.connect(self.stop_movement)
        self.stop_button.setEnabled(False)
        position_layout.addWidget(self.stop_button, 5, 0, 1, 2)
        
        control_layout.addWidget(position_group)
        
        # 参数控制组
        params_group = QGroupBox("参数设置")
        params_layout = QGridLayout(params_group)
        
        # 速度滑块
        params_layout.addWidget(QLabel("速度:"), 0, 0)
        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setRange(10, 100)
        self.speed_slider.setValue(50)
        self.speed_slider.setTickPosition(QSlider.TicksBelow)
        self.speed_slider.setTickInterval(10)
        params_layout.addWidget(self.speed_slider, 0, 1)
        
        # 加速度滑块
        params_layout.addWidget(QLabel("加速度:"), 1, 0)
        self.accel_slider = QSlider(Qt.Horizontal)
        self.accel_slider.setRange(10, 100)
        self.accel_slider.setValue(50)
        self.accel_slider.setTickPosition(QSlider.TicksBelow)
        self.accel_slider.setTickInterval(10)
        params_layout.addWidget(self.accel_slider, 1, 1)
        
        # 精度选择
        params_layout.addWidget(QLabel("精度:"), 2, 0)
        self.precision_combo = QComboBox()
        self.precision_combo.addItems(["低", "中", "高"])
        self.precision_combo.setCurrentIndex(2)  # 默认高精度
        params_layout.addWidget(self.precision_combo, 2, 1)
        
        # 应用参数按钮
        self.apply_params_button = QPushButton("应用参数")
        self.apply_params_button.clicked.connect(self.apply_params)
        self.apply_params_button.setEnabled(False)
        params_layout.addWidget(self.apply_params_button, 3, 0, 1, 2)
        
        control_layout.addWidget(params_group)
        control_layout.addStretch()
        
        # 添加标签页
        self.device_tabs.addTab(self.info_tab, "设备信息")
        self.device_tabs.addTab(self.control_tab, "设备控制")
        
        # 添加到分割器
        splitter.addWidget(self.device_tabs)
        
        # 设置分割器比例
        splitter.setSizes([200, 400])
        
        # 状态栏
        self.status_label = QLabel("就绪")
        main_layout.addWidget(self.status_label)
        
        # 初始状态
        self.update_ui_state(False)
    
    def refresh_device_list(self):
        """刷新设备列表"""
        if not self.device_manager:
            return
        
        devices = self.device_manager.get_all_available_devices()
        self.update_device_list(devices)
    
    def update_device_list(self, devices):
        """更新设备列表
        
        Args:
            devices (list): 设备列表
        """
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
            if status == "已连接":
                status_item.setForeground(QColor(0, 128, 0))  # 绿色
            else:
                status_item.setForeground(QColor(128, 128, 128))  # 灰色
            self.device_table.setItem(i, 4, status_item)
    
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
    
    def on_device_selected(self, item):
        """设备选择处理
        
        Args:
            item: 选中的表格项
        """
        # 获取选中的行
        row = item.row()
        
        # 获取设备ID
        device_id = self.device_table.item(row, 0).text()
        
        # 更新设备信息
        self.update_device_info(device_id)
        
        # 更新UI状态
        is_connected = self.device_manager and self.device_manager.get_device(device_id) is not None
        self.update_ui_state(is_connected, device_id)
    
    def update_device_info(self, device_id):
        """更新设备信息
        
        Args:
            device_id (str): 设备ID
        """
        if not self.device_manager:
            return
        
        # 获取设备信息
        device_info = self.device_manager.device_detector.get_device(device_id)
        if not device_info:
            return
        
        # 更新设备信息标签
        self.device_id_label.setText(device_id)
        self.device_name_label.setText(device_info["name"])
        self.device_type_label.setText(self.get_device_type_name(device_info["type"]))
        self.device_connection_label.setText(self.get_connection_type_name(device_info["connection_type"]))
        
        # 获取设备状态
        device = self.device_manager.get_device(device_id)
        if device:
            status = device.get_status()
            self.update_device_status(status)
        else:
            self.device_status_label.setText("未连接")
            self.device_position_label.setText("")
    
    def update_device_status(self, status):
        """更新设备状态
        
        Args:
            status (dict): 设备状态
        """
        # 检查是否是当前选中的设备
        device_info = status.get("device_info", {})
        device_id = device_info.get("id", "")
        
        if device_id != self.device_id_label.text():
            return
        
        # 更新状态标签
        status_text = status.get("status", "")
        status_names = {
            "disconnected": "已断开",
            "connected": "已连接",
            "error": "错误",
            "moving": "移动中",
            "ready": "就绪"
        }
        self.device_status_label.setText(status_names.get(status_text, status_text))
        
        # 更新位置标签
        position = status.get("position", {})
        if position:
            pos_text = f"X: {position.get('x', 0.0):.2f}, Y: {position.get('y', 0.0):.2f}, Z: {position.get('z', 0.0):.2f}, 角度: {position.get('angle', 0.0):.1f}°"
            self.device_position_label.setText(pos_text)
            
            # 更新控制面板的位置值
            self.x_spinbox.setValue(position.get('x', 0.0))
            self.y_spinbox.setValue(position.get('y', 0.0))
            self.z_spinbox.setValue(position.get('z', 0.0))
            self.angle_spinbox.setValue(position.get('angle', 0.0))
        
        # 更新控制按钮状态
        is_moving = status.get("is_moving", False)
        self.move_button.setEnabled(not is_moving)
        self.stop_button.setEnabled(is_moving)
        
        # 更新参数控制
        params = status.get("params", {})
        if params:
            self.speed_slider.setValue(params.get("speed", 50))
            self.accel_slider.setValue(params.get("acceleration", 50))
            
            precision = params.get("precision", "high")
            precision_index = {"low": 0, "medium": 1, "high": 2}.get(precision, 2)
            self.precision_combo.setCurrentIndex(precision_index)
    
    def refresh_device_status(self):
        """刷新设备状态"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备":
            return
        
        if not self.device_manager:
            return
        
        device = self.device_manager.get_device(device_id)
        if device:
            status = device.get_status()
            self.update_device_status(status)
    
    def update_ui_state(self, is_connected, device_id=None):
        """更新UI状态
        
        Args:
            is_connected (bool): 是否已连接
            device_id (str, optional): 设备ID
        """
        # 更新连接/断开按钮状态
        self.connect_button.setEnabled(not is_connected and device_id is not None)
        self.disconnect_button.setEnabled(is_connected and device_id is not None)
        
        # 更新控制面板状态
        self.move_button.setEnabled(is_connected)
        self.stop_button.setEnabled(is_connected)
        self.apply_params_button.setEnabled(is_connected)
        
        # 更新位置控制状态
        self.x_spinbox.setEnabled(is_connected)
        self.y_spinbox.setEnabled(is_connected)
        self.z_spinbox.setEnabled(is_connected)
        self.angle_spinbox.setEnabled(is_connected)
        
        # 更新参数控制状态
        self.speed_slider.setEnabled(is_connected)
        self.accel_slider.setEnabled(is_connected)
        self.precision_combo.setEnabled(is_connected)
    
    def connect_selected_device(self):
        """连接选中的设备"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备" or not self.device_manager:
            return
        
        # 连接设备
        if self.device_manager.connect_device(device_id):
            self.status_label.setText(f"已连接到设备: {device_id}")
            self.update_ui_state(True, device_id)
            self.refresh_device_list()
        else:
            QMessageBox.warning(self, "连接失败", f"无法连接到设备: {device_id}")
    
    def disconnect_selected_device(self):
        """断开选中的设备"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备" or not self.device_manager:
            return
        
        # 断开设备连接
        if self.device_manager.disconnect_device(device_id):
            self.status_label.setText(f"已断开与设备的连接: {device_id}")
            self.update_ui_state(False, device_id)
            self.refresh_device_list()
        else:
            QMessageBox.warning(self, "断开失败", f"无法断开与设备的连接: {device_id}")
    
    def move_to_position(self):
        """移动到指定位置"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备" or not self.device_manager:
            return
        
        device = self.device_manager.get_device(device_id)
        if not device:
            return
        
        # 获取位置参数
        position = {
            'x': self.x_spinbox.value(),
            'y': self.y_spinbox.value(),
            'z': self.z_spinbox.value(),
            'angle': self.angle_spinbox.value()
        }
        
        # 移动到指定位置
        if device.move_to_position(position):
            self.status_label.setText(f"正在移动设备: {device_id}")
        else:
            QMessageBox.warning(self, "移动失败", "无法移动设备，请检查设备状态")
    
    def stop_movement(self):
        """停止移动"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备" or not self.device_manager:
            return
        
        device = self.device_manager.get_device(device_id)
        if not device:
            return
        
        # 停止移动
        if device.stop_movement():
            self.status_label.setText(f"已停止设备移动: {device_id}")
        else:
            QMessageBox.warning(self, "停止失败", "无法停止设备移动，请检查设备状态")
    
    def apply_params(self):
        """应用参数设置"""
        device_id = self.device_id_label.text()
        if device_id == "未选择设备" or not self.device_manager:
            return
        
        device = self.device_manager.get_device(device_id)
        if not device:
            return
        
        # 获取参数
        params = {
            "speed": self.speed_slider.value(),
            "acceleration": self.accel_slider.value(),
            "precision": ["low", "medium", "high"][self.precision_combo.currentIndex()]
        }
        
        # 设置参数
        if device.set_params(params):
            self.status_label.setText(f"已应用参数设置: {device_id}")
        else:
            QMessageBox.warning(self, "设置失败", "无法应用参数设置，请检查设备状态")
    
    def on_device_connected(self, device_info):
        """设备连接处理
        
        Args:
            device_info (dict): 设备信息
        """
        device_id = device_info["id"]
        
        # 如果是当前选中的设备，更新UI状态
        if device_id == self.device_id_label.text():
            self.update_ui_state(True, device_id)
        
        # 刷新设备列表
        self.refresh_device_list()
        
        # 更新状态栏
        self.status_label.setText(f"设备已连接: {device_info['name']} ({device_id})")
    
    def on_device_disconnected(self, device_id):
        """设备断开处理
        
        Args:
            device_id (str): 设备ID
        """
        # 如果是当前选中的设备，更新UI状态
        if device_id == self.device_id_label.text():
            self.update_ui_state(False, device_id)
        
        # 刷新设备列表
        self.refresh_device_list()
        
        # 更新状态栏
        self.status_label.setText(f"设备已断开: {device_id}")
    
    def on_device_error(self, device_id, error_message):
        """设备错误处理
        
        Args:
            device_id (str): 设备ID
            error_message (str): 错误消息
        """
        # 更新状态栏
        self.status_label.setText(f"设备错误: {device_id} - {error_message}")
        
        # 如果是当前选中的设备，显示错误消息
        if device_id == self.device_id_label.text():
            QMessageBox.warning(self, "设备错误", f"设备 {device_id} 发生错误:\n{error_message}") 