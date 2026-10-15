#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
实验池端(Slave)GUI实现
"""

import sys
import os
import cv2
import numpy as np
import time
from PyQt5.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                            QLabel, QPushButton, QGroupBox, QFormLayout,
                            QStatusBar, QSplitter, QAction, QMessageBox,
                            QDialog, QLineEdit, QTabWidget, QTextEdit)
from PyQt5.QtCore import Qt, QTimer, pyqtSlot, pyqtSignal
from PyQt5.QtGui import QPixmap, QImage

# 添加父目录到系统路径
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)

# 导入项目模块
from remote.client import Client
from remote.auth import AuthManager
from client.device_manager import DeviceManager
from client.robot_device_panel import RobotDevicePanel

class SlaveWindow(QMainWindow):
    """从端主窗口"""
    
    def __init__(self, client, pool_id, debug=False, auto_connect=False):
        """初始化从端窗口"""
        super().__init__()
        
        self.client = client
        self.pool_id = pool_id
        self.debug = debug
        self.auto_connect = auto_connect
        
        # 设置窗口基本属性
        self.setWindowTitle(f"OceanLab-Python 实验池端 - {pool_id}")
        self.resize(800, 600)
        
        # 添加状态栏
        self.statusBar().showMessage("正在初始化...")
        
        # 初始化设备管理器
        self.device_manager = DeviceManager(debug=debug)
        
        # 设置UI
        self.setup_ui()
        
        # 创建菜单栏
        self.create_menu_bar()
        
        # 定时器，定期更新显示
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(50)  # 20FPS
        
        # 摄像头
        self.camera = None
        self.camera_frame = None
        
        # 连接状态
        self.connection_timer = QTimer(self)
        self.connection_timer.timeout.connect(self.check_connection)
        self.connection_timer.start(1000)  # 每秒检查一次
        
        # 数据采集标志
        self.data_acquisition_running = False
        
        # 数据定时器
        self.data_timer = None
        
        # 自动连接
        if self.auto_connect:
            # 使用延迟以确保UI已完全加载
            QTimer.singleShot(500, self.connect_to_server)
        
        # 创建更新定时器
        self.update_timer = QTimer(self)
        self.update_timer.timeout.connect(self.send_status)
        self.update_timer.start(5000)  # 每5秒发送一次状态更新
        
        # 启动设备管理器
        self.device_manager.start()
        
        self.log("初始化完成")
        self.statusBar().showMessage("初始化完成")
    
    def setup_ui(self):
        """设置UI"""
        self.setWindowTitle(f"OceanLab-Python 实验池端 - {self.pool_id}")
        self.resize(1000, 700)
        
        # 创建中央部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 主布局
        main_layout = QVBoxLayout(central_widget)
        
        # 创建分割器
        splitter = QSplitter(Qt.Horizontal)
        main_layout.addWidget(splitter)
        
        # 左侧状态面板
        status_panel = QWidget()
        status_layout = QVBoxLayout(status_panel)
        splitter.addWidget(status_panel)
        
        # 连接状态
        connection_group = QGroupBox("连接状态")
        connection_layout = QVBoxLayout()
        
        # 服务器信息
        server_info = QLabel(f"服务器: {self.client.server_address}:{self.client.port}")
        connection_layout.addWidget(server_info)
        
        # 实验池ID
        pool_info = QLabel(f"实验池ID: {self.pool_id}")
        connection_layout.addWidget(pool_info)
        
        # 连接状态
        self.connection_status = QLabel("未连接")
        connection_layout.addWidget(self.connection_status)
        
        # 状态标签
        status_label = QLabel("从端状态:")
        connection_layout.addWidget(status_label)
        
        # 在线状态标签
        self.online_status = QLabel("在线")
        self.online_status.setStyleSheet("color: green; font-weight: bold;")
        connection_layout.addWidget(self.online_status)
        
        # 不再需要连接按钮，改为显示上次活动时间
        self.last_activity = QLabel("上次活动: 刚刚")
        connection_layout.addWidget(self.last_activity)
        
        connection_group.setLayout(connection_layout)
        status_layout.addWidget(connection_group)
        
        # 参数显示
        params_group = QGroupBox("当前参数")
        params_layout = QFormLayout()
        
        # 水听器参数
        self.hydrophone_x_label = QLabel("0.25")  # 水听器默认在右侧0.25米
        params_layout.addRow("水听器 X坐标 (m):", self.hydrophone_x_label)
        
        self.hydrophone_y_label = QLabel("0.0")
        params_layout.addRow("水听器 Y坐标 (m):", self.hydrophone_y_label)
        
        self.hydrophone_z_label = QLabel("0.5")
        params_layout.addRow("水听器 Z坐标 (m):", self.hydrophone_z_label)
        
        self.hydrophone_rotation_label = QLabel("0.0")
        params_layout.addRow("水听器 旋转角度 (°):", self.hydrophone_rotation_label)
        
        # 换能器参数
        self.transducer_x_label = QLabel("-0.25")  # 换能器默认在左侧-0.25米
        params_layout.addRow("换能器 X坐标 (m):", self.transducer_x_label)
        
        self.transducer_y_label = QLabel("0.0")
        params_layout.addRow("换能器 Y坐标 (m):", self.transducer_y_label)
        
        self.transducer_z_label = QLabel("0.5")
        params_layout.addRow("换能器 Z坐标 (m):", self.transducer_z_label)
        
        self.transducer_rotation_label = QLabel("0.0")
        params_layout.addRow("换能器 旋转角度 (°):", self.transducer_rotation_label)
        
        # 水池参数
        self.water_depth_label = QLabel("0.8")
        params_layout.addRow("水深 (m):", self.water_depth_label)
        
        params_group.setLayout(params_layout)
        status_layout.addWidget(params_group)
        
        # 填充空间
        status_layout.addStretch()
        
        # 右侧显示面板
        display_panel = QTabWidget()
        splitter.addWidget(display_panel)
        
        # 摄像头标签页
        self.camera_tab = QWidget()
        camera_layout = QVBoxLayout(self.camera_tab)
        
        # 摄像头状态
        self.camera_status = QLabel("摄像头未初始化")
        camera_layout.addWidget(self.camera_status)
        
        # 摄像头视图
        self.camera_view = QLabel("摄像头视图将在此处显示")
        self.camera_view.setAlignment(Qt.AlignCenter)
        self.camera_view.setMinimumSize(640, 480)
        camera_layout.addWidget(self.camera_view)
        
        display_panel.addTab(self.camera_tab, "摄像头预览")
        
        # 机器人设备标签页
        self.robot_device_tab = QWidget()
        robot_device_layout = QVBoxLayout(self.robot_device_tab)
        self.robot_device_panel = RobotDevicePanel(self.device_manager, self)
        robot_device_layout.addWidget(self.robot_device_panel)
        display_panel.addTab(self.robot_device_tab, "机器人设备")
        
        # 数据采集标签页
        self.data_tab = QWidget()
        data_layout = QVBoxLayout(self.data_tab)
        
        # 数据采集状态
        self.data_status = QLabel("数据采集模块未启动")
        data_layout.addWidget(self.data_status)
        
        # 数据控制按钮
        data_control_layout = QHBoxLayout()
        
        self.start_data_button = QPushButton("启动采集")
        self.start_data_button.clicked.connect(self.start_data_acquisition)
        data_control_layout.addWidget(self.start_data_button)
        
        self.stop_data_button = QPushButton("停止采集")
        self.stop_data_button.clicked.connect(self.stop_data_acquisition)
        self.stop_data_button.setEnabled(False)
        data_control_layout.addWidget(self.stop_data_button)
        
        data_layout.addLayout(data_control_layout)
        
        # 数据参数组
        data_params_group = QGroupBox("采集参数")
        data_params_layout = QFormLayout()
        
        self.sample_rate_label = QLabel("0 Hz")
        data_params_layout.addRow("采样率:", self.sample_rate_label)
        
        self.data_length_label = QLabel("0 点")
        data_params_layout.addRow("数据长度:", self.data_length_label)
        
        data_params_group.setLayout(data_params_layout)
        data_layout.addWidget(data_params_group)
        
        # 数据视图容器（预留给后续波形显示）
        self.data_view_container = QWidget()
        self.data_view_container.setMinimumSize(640, 300)
        self.data_view_container.setStyleSheet("background-color: #f0f0f0;")
        data_layout.addWidget(self.data_view_container)
        
        display_panel.addTab(self.data_tab, "数据采集")
        
        # 日志标签页
        self.log_tab = QWidget()
        log_layout = QVBoxLayout(self.log_tab)
        
        # 日志显示
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        log_layout.addWidget(self.log_text)
        
        display_panel.addTab(self.log_tab, "运行日志")
        
        # 设置分割器比例
        splitter.setSizes([300, 700])
        
        # 初始化日志
        self.log("从端应用已启动")
    
    def create_menu_bar(self):
        """创建菜单栏"""
        # 文件菜单
        file_menu = self.menuBar().addMenu("文件")
        
        # 连接/断开动作
        self.connect_action = QAction("连接", self)
        self.connect_action.triggered.connect(self.toggle_connection)
        file_menu.addAction(self.connect_action)
        
        # 退出动作
        exit_action = QAction("退出", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # 视图菜单
        view_menu = self.menuBar().addMenu("视图")
        
        # 刷新动作
        refresh_action = QAction("刷新", self)
        refresh_action.triggered.connect(self.refresh_ui)
        view_menu.addAction(refresh_action)
        
        # 摄像头菜单
        camera_menu = self.menuBar().addMenu("摄像头")
        
        # 启动摄像头
        start_camera_action = QAction("启动摄像头", self)
        start_camera_action.triggered.connect(self.init_camera)
        camera_menu.addAction(start_camera_action)
        
        # 停止摄像头
        stop_camera_action = QAction("停止摄像头", self)
        stop_camera_action.triggered.connect(self.stop_camera)
        camera_menu.addAction(stop_camera_action)
        
        # 设备菜单
        device_menu = self.menuBar().addMenu("设备")
        
        # 刷新设备列表
        refresh_devices_action = QAction("刷新设备列表", self)
        refresh_devices_action.triggered.connect(self.refresh_device_list)
        device_menu.addAction(refresh_devices_action)
        
        # 帮助菜单
        help_menu = self.menuBar().addMenu("帮助")
        
        # 关于动作
        about_action = QAction("关于", self)
        about_action.triggered.connect(self.show_about_dialog)
        help_menu.addAction(about_action)
    
    def toggle_connection(self):
        """切换连接状态"""
        if self.client.connected:
            self.disconnect_from_server()
        else:
            self.connect_to_server()
    
    def connect_to_server(self):
        """连接到服务器"""
        self.log(f"正在连接到服务器 {self.client.server_address}:{self.client.port}...")
        self.statusBar().showMessage(f"正在连接到服务器 {self.client.server_address}:{self.client.port}...")
        
        # 更新UI状态
        self.connection_status.setText("正在连接...")
        self.connection_status.setStyleSheet("color: orange")
        self.connect_action.setText("断开连接")
        self.statusBar().showMessage(f"已连接到服务器 {self.client.server_address}:{self.client.port}")
        
        # 尝试连接
        if self.client.connect():
            self.log(f"成功连接到服务器 {self.client.server_address}:{self.client.port}")
            self.connection_status.setText("已连接")
            self.connection_status.setStyleSheet("color: green")
            self.connect_action.setText("断开连接")
            self.statusBar().showMessage(f"已连接到服务器 {self.client.server_address}:{self.client.port}")
            
            # 发送一些数据，确保服务器能够识别到从端
            self.client.send_status()
        else:
            self.log(f"无法连接到服务器 {self.client.server_address}:{self.client.port}")
            self.connection_status.setText("连接失败")
            self.connection_status.setStyleSheet("color: red")
            self.statusBar().showMessage("连接失败")
            
            # 尝试查找可能的问题原因
            import socket
            try:
                # 测试端口是否开放
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(2)
                result = s.connect_ex((self.client.server_address, self.client.port))
                s.close()
                
                if result == 0:
                    self.log("服务器端口开放，但连接失败，可能是服务器拒绝了连接")
                else:
                    self.log(f"服务器端口未开放或被防火墙阻止，错误代码: {result}")
            except Exception as e:
                self.log(f"测试服务器端口时出错: {e}")
    
    def disconnect_from_server(self):
        """断开服务器连接"""
        self.client.disconnect()
        self.log("已断开与服务器的连接")
        self.connection_status.setText("未连接")
        self.connection_status.setStyleSheet("")
        self.connect_action.setText("连接")
        self.statusBar().showMessage("已断开连接")
    
    def check_connection(self):
        """检查连接状态"""
        if self.client.connected:
            if self.connection_status.text() != "已连接":
                self.connection_status.setText("已连接")
                self.connection_status.setStyleSheet("color: green")
                self.connect_action.setText("断开连接")
        else:
            if self.connection_status.text() != "未连接" and self.connection_status.text() != "连接失败":
                self.connection_status.setText("未连接")
                self.connection_status.setStyleSheet("")
                self.connect_action.setText("连接")
    
    def init_camera(self):
        """初始化摄像头"""
        try:
            self.camera = cv2.VideoCapture(0)
            if not self.camera.isOpened():
                self.log("警告：无法打开摄像头，视频流将不可用")
                self.camera_status.setText("摄像头打开失败")
                self.camera_status.setStyleSheet("color: red")
                self.camera = None
            else:
                self.log("摄像头已初始化")
                self.camera_status.setText("摄像头已启动")
                self.camera_status.setStyleSheet("color: green")
        except Exception as e:
            self.log(f"初始化摄像头时出错: {e}")
            self.camera_status.setText("摄像头初始化错误")
            self.camera_status.setStyleSheet("color: red")
            self.camera = None
    
    def stop_camera(self):
        """停止摄像头"""
        if self.camera:
            self.camera.release()
            self.camera = None
            self.log("摄像头已停止")
            self.camera_status.setText("摄像头已停止")
            self.camera_status.setStyleSheet("")
            # 清除摄像头预览
            self.camera_view.setText("摄像头已停止")
            self.camera_view.setPixmap(QPixmap())
    
    def refresh_device_list(self):
        """刷新设备列表"""
        if self.device_manager:
            self.log("正在刷新设备列表...")
            # 设备管理器会自动刷新设备列表
            # 这里手动触发一次刷新
            self.robot_device_panel.refresh_device_list()
            self.log("设备列表已刷新")
    
    def update_params_display(self):
        """更新参数显示"""
        params = self.client.get_params()
        
        # 更新水听器参数
        self.hydrophone_x_label.setText(f"{params['hydrophone_x']:.2f}")
        self.hydrophone_y_label.setText(f"{params['hydrophone_y']:.2f}")
        self.hydrophone_z_label.setText(f"{params['hydrophone_z']:.2f}")
        self.hydrophone_rotation_label.setText(f"{params['hydrophone_rotation']:.2f}")
        
        # 更新换能器参数
        self.transducer_x_label.setText(f"{params['transducer_x']:.2f}")
        self.transducer_y_label.setText(f"{params['transducer_y']:.2f}")
        self.transducer_z_label.setText(f"{params['transducer_z']:.2f}")
        self.transducer_rotation_label.setText(f"{params['transducer_rotation']:.2f}")
        
        # 更新水池参数
        self.water_depth_label.setText(f"{params['water_depth']:.2f}")
    
    def update_camera_preview(self):
        """更新摄像头预览"""
        if self.camera:
            ret, frame = self.camera.read()
            if ret:
                self.camera_frame = frame
                
                # 转换BGR图像到RGB
                rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                
                # 缩放图像适应显示区域
                h, w, ch = rgb_frame.shape
                bytes_per_line = ch * w
                q_img = QImage(rgb_frame.data, w, h, bytes_per_line, QImage.Format_RGB888)
                self.camera_view.setPixmap(QPixmap.fromImage(q_img).scaled(
                    self.camera_view.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation))
                
                # 发送到服务器
                if self.client.connected:
                    self.client.send_video_frame(frame)
    
    def process_commands(self):
        """处理来自服务器的命令"""
        if self.client.connected:
            commands = self.client.process_commands()
            if commands and len(commands) > 0:
                self.log(f"收到 {len(commands)} 个命令")
                
            for cmd in commands:
                # 处理命令元组或字典（兼容两种格式）
                if isinstance(cmd, tuple) and len(cmd) == 2:
                    # 元组格式：(command, params)
                    command, params = cmd
                    self.log(f"处理元组命令: {command}, 参数: {params}")
                    if command == 'start_data_acquisition':
                        # 更新数据采集参数
                        self.current_sample_rate = params.get('sample_rate', 44100)
                        self.current_data_length = params.get('data_length', 1024)
                        self.current_signal_type = params.get('signal_type', 'sine')
                        self.current_signal_freq = params.get('signal_freq', 1000)
                        
                        # 更新UI显示
                        self.sample_rate_label.setText(f"{self.current_sample_rate} Hz")
                        self.data_length_label.setText(f"{self.current_data_length} 点")
                        
                        # 启动数据采集
                        self.start_data_acquisition(from_command=True)
                    elif command == 'stop_data_acquisition':
                        self.stop_data_acquisition()
                    elif command == 'robot_command':
                        # 处理机器人设备命令
                        self.process_robot_command(params)
                elif isinstance(cmd, dict):
                    # 字典格式：{"command": command, "params": params}
                    self.log(f"处理字典命令: {cmd}")
                    command = cmd.get('command')
                    params = cmd.get('params', {})
                    if command == 'start_data_acquisition':
                        # 更新数据采集参数
                        self.current_sample_rate = params.get('sample_rate', 44100)
                        self.current_data_length = params.get('data_length', 1024)
                        self.current_signal_type = params.get('signal_type', 'sine')
                        self.current_signal_freq = params.get('signal_freq', 1000)
                        
                        # 更新UI显示
                        self.sample_rate_label.setText(f"{self.current_sample_rate} Hz")
                        self.data_length_label.setText(f"{self.current_data_length} 点")
                        
                        # 启动数据采集
                        self.start_data_acquisition(from_command=True)
                    elif command == 'stop_data_acquisition':
                        self.stop_data_acquisition()
                    elif command == 'robot_command':
                        # 处理机器人设备命令
                        self.process_robot_command(params)
                else:
                    self.log(f"未知命令格式: {type(cmd)}, 内容: {cmd}")
    
    def process_robot_command(self, params):
        """处理机器人设备命令
        
        Args:
            params (dict): 命令参数
        """
        self.log(f"处理机器人设备命令: {params}")
        
        # 获取目标设备类型和操作
        target = params.get('target', '')
        action = params.get('action', '')
        
        # 查找对应类型的设备
        devices = self.device_manager.get_device_by_type(target)
        if not devices:
            self.log(f"未找到类型为 {target} 的设备")
            return
        
        # 使用第一个匹配的设备
        device = devices[0]
        
        # 执行操作
        if action == 'move':
            # 移动命令
            position = params.get('position', {})
            if position:
                self.log(f"移动设备 {target} 到位置: {position}")
                device.move_to_position(position)
            else:
                self.log(f"移动命令缺少位置参数")
        
        elif action == 'stop':
            # 停止命令
            self.log(f"停止设备 {target} 的移动")
            device.stop_movement()
        
        elif action == 'set_params':
            # 设置参数命令
            device_params = params.get('params', {})
            if device_params:
                self.log(f"设置设备 {target} 参数: {device_params}")
                device.set_params(device_params)
            else:
                self.log(f"设置参数命令缺少参数")
        
        else:
            self.log(f"未知的设备操作: {action}")
    
    def send_status(self):
        """发送状态到服务器"""
        if not self.client.connected:
            # 如果未连接，尝试连接
            if self.auto_connect:
                self.connect_to_server()
            return
        
        try:
            # 不再发送基本状态，避免参数被重置
            # self.client.send_status()  # 注释掉这行，不再发送位置参数
            
            # 只发送设备状态
            if hasattr(self, 'device_manager') and self.device_manager:
                # 发送所有可用设备信息
                available_devices = self.device_manager.get_all_available_devices()
                
                    # 将设备状态添加到客户端状态中
                self.client.send_data_command("device_status", {
                    "devices": available_devices
                })
            
            # 更新上次活动时间
            current_time = time.strftime("%H:%M:%S")
            self.last_activity.setText(f"上次活动: {current_time}")
            
        except Exception as e:
            self.log(f"发送状态时出错: {e}")
            # 如果发送失败且断开连接，尝试重新连接
            if not self.client.connected and self.auto_connect:
                self.connect_to_server()
    
    def update_ui(self):
        """更新UI"""
        # 更新参数显示
        if self.client.connected:
            self.update_params_display()
        
        # 更新摄像头预览
        self.update_camera_preview()
        
        # 处理服务器命令
        self.process_commands()
    
    def refresh_ui(self):
        """刷新UI"""
        self.update_params_display()
        self.refresh_device_list()
    
    def show_about_dialog(self):
        """显示关于对话框"""
        QMessageBox.about(
            self,
            "关于 OceanLab-Python 实验池端",
            "OceanLab-Python 海洋信息实验教学平台\n"
            "版本: 1.0.0\n\n"
            "实验池端(Slave)应用程序\n"
            "用于控制水池中的水听器和换能器设备"
        )
    
    def log(self, message):
        """添加日志消息
        
        Args:
            message (str): 日志消息
        """
        timestamp = time.strftime("%H:%M:%S", time.localtime())
        self.log_text.append(f"[{timestamp}] {message}")
    
    def closeEvent(self, event):
        """关闭窗口事件处理"""
        try:
            # 停止定时器
            if hasattr(self, 'timer') and self.timer is not None:
                self.timer.stop()
            if hasattr(self, 'update_timer') and self.update_timer is not None:
                self.update_timer.stop()
            if hasattr(self, 'connection_timer') and self.connection_timer is not None:
                self.connection_timer.stop()
            if hasattr(self, 'data_timer') and self.data_timer is not None:
                self.data_timer.stop()
            
            # 关闭摄像头
            self.stop_camera()
            
            # 停止数据采集
            if hasattr(self, 'data_acquisition_running') and self.data_acquisition_running:
                self.stop_data_acquisition()
            
            # 停止设备管理器
            if hasattr(self, 'device_manager') and self.device_manager:
                self.device_manager.stop()
                print("设备管理器已停止")
            
            # 关闭客户端连接
            if hasattr(self, 'client') and self.client:
                self.client.disconnect()
                print("已断开与服务器的连接")
        except Exception as e:
            print(f"关闭窗口时出错: {e}")
        
            event.accept()
    
    def start_data_acquisition(self, from_command=False):
        """启动数据采集
        
        Args:
            from_command: 是否是从命令调用（避免重复启动）
        """
        # 检查是否已经处于采集状态，避免重复启动
        if not from_command and self.stop_data_button.isEnabled():
            self.log("已经处于采集状态，忽略启动请求")
            return
            
        self.log("启动数据采集...")
        self.data_status.setText("数据采集中")
        self.data_status.setStyleSheet("color: green;")
        self.start_data_button.setEnabled(False)
        self.stop_data_button.setEnabled(True)
        
        # 默认采集参数
        if not hasattr(self, 'current_sample_rate'):
            self.current_sample_rate = 44100
        if not hasattr(self, 'current_data_length'):
            self.current_data_length = 1024
        if not hasattr(self, 'current_signal_type'):
            self.current_signal_type = "sine"
        if not hasattr(self, 'current_signal_freq'):
            self.current_signal_freq = 1000
        
        # 更新采集参数显示
        self.sample_rate_label.setText(f"{self.current_sample_rate} Hz")
        self.data_length_label.setText(f"{self.current_data_length} 点")
        
        # 发送采集启动命令到服务器
        if self.client.connected:
            self.client.send_data_command("start_acquisition", {
                "sample_rate": self.current_sample_rate,
                "data_length": self.current_data_length,
                "signal_type": self.current_signal_type,
                "signal_freq": self.current_signal_freq
            })
            
            # 启动模拟数据生成
            self.start_data_simulation()
    
    def start_data_simulation(self):
        """启动模拟数据生成
        这个方法模拟数据采集，在实际环境中，这部分应该由真实的传感器数据采集替代
        """
        self.log("启动模拟数据生成...")
        
        # 创建数据生成定时器
        self.data_timer = QTimer(self)
        self.data_timer.timeout.connect(self.generate_and_send_data)
        self.data_timer.start(100)  # 每100ms生成一次数据
    
    def generate_and_send_data(self):
        """生成并发送模拟数据"""
        if not self.client.connected:
            return
        
        # 生成模拟数据
        import numpy as np
        import time
        
        sample_rate = self.current_sample_rate
        data_length = self.current_data_length
        signal_type = self.current_signal_type
        frequency = self.current_signal_freq
        
        self.log(f"生成数据: 类型={signal_type}, 频率={frequency}Hz")
        
        # 创建时间轴
        current_time = time.time()
        t = np.arange(data_length) / sample_rate
        
        # 水声传播速度 (m/s)
        sound_speed = 1500.0  # 真实水中声速约1500 m/s
        
        # 水池尺寸参数 (m) - 按照实际水池的尺寸
        pool_width = 2.0   # X轴方向宽度
        pool_length = 1.0  # Y轴方向长度
        pool_depth = 1.0   # Z轴方向深度
        
        # 从当前参数中获取发射器(换能器)和接收器(水听器)位置
        # 如果尚未从主端接收更新，则使用默认值
        tx_pos = np.array([
            float(self.transducer_x_label.text()),  # X坐标
            float(self.transducer_y_label.text()),  # Y坐标
            float(self.transducer_z_label.text())   # Z坐标
        ])
        
        rx_pos = np.array([
            float(self.hydrophone_x_label.text()),  # X坐标
            float(self.hydrophone_y_label.text()),  # Y坐标
            float(self.hydrophone_z_label.text())   # Z坐标
        ])
        
        # 获取当前水深设置
        water_depth = float(self.water_depth_label.text())
        if water_depth > 0:
            pool_depth = water_depth
        
        self.log(f"使用参数: 换能器位置={tx_pos}, 水听器位置={rx_pos}, 水深={pool_depth}m")
        
        # 计算直达路径
        direct_distance = np.linalg.norm(rx_pos - tx_pos)
        direct_delay = direct_distance / sound_speed
        
        # 定义反射路径
        # 0: 水面(z=0), 1: 水底(z=pool_depth), 2: 左壁(x=-pool_width/2),
        # 3: 右壁(x=pool_width/2), 4: 前壁(y=-pool_length/2), 5: 后壁(y=pool_length/2)
        reflection_paths = [
            [0],  # 水面反射
            [1],  # 水底反射
            [2],  # 左壁反射
            [3],  # 右壁反射
            [0, 1],  # 水面-水底反射
            [2, 3],  # 左壁-右壁反射
        ]
        
        # 计算各反射路径长度和时延
        path_distances = [direct_distance]  # 先加入直达路径
        path_delays = [direct_delay]
        
        # 反射衰减系数 (声波在边界的反射损失)
        reflection_loss = 0.2
        
        # 计算各反射路径
        for path in reflection_paths:
            current_pos = tx_pos.copy()
            total_distance = 0
            intensity = 1.0
            valid_reflection = True  # 标记当前反射路径是否有效
            
            for boundary in path:
                if boundary == 0:  # 水面反射 (z=0)
                    mirror_z = -current_pos[2]
                    mirror_pos = np.array([current_pos[0], current_pos[1], mirror_z])
                elif boundary == 1:  # 水底反射 (z=pool_depth)
                    mirror_z = 2 * pool_depth - current_pos[2]
                    mirror_pos = np.array([current_pos[0], current_pos[1], mirror_z])
                elif boundary == 2:  # 左壁反射 (x=-pool_width/2)
                    mirror_x = -pool_width - current_pos[0]
                    mirror_pos = np.array([mirror_x, current_pos[1], current_pos[2]])
                elif boundary == 3:  # 右壁反射 (x=pool_width/2)
                    mirror_x = pool_width - current_pos[0]
                    mirror_pos = np.array([mirror_x, current_pos[1], current_pos[2]])
                elif boundary == 4:  # 前壁反射 (y=-pool_length/2)
                    mirror_y = -pool_length - current_pos[1]
                    mirror_pos = np.array([current_pos[0], mirror_y, current_pos[2]])
                else:  # 后壁反射 (y=pool_length/2)
                    mirror_y = pool_length - current_pos[1]
                    mirror_pos = np.array([current_pos[0], mirror_y, current_pos[2]])
                
                # 计算到反射点的方向
                to_mirror = mirror_pos - current_pos
                to_mirror_norm = np.linalg.norm(to_mirror)
                to_mirror_dir = to_mirror / to_mirror_norm if to_mirror_norm > 0 else np.zeros(3)
                
                # 计算反射点位置
                if boundary == 0:  # 水面
                    t_intersect = (0 - current_pos[2]) / to_mirror_dir[2] if to_mirror_dir[2] != 0 else float('inf')
                elif boundary == 1:  # 水底
                    t_intersect = (pool_depth - current_pos[2]) / to_mirror_dir[2] if to_mirror_dir[2] != 0 else float('inf')
                elif boundary == 2:  # 左壁
                    t_intersect = (-pool_width/2 - current_pos[0]) / to_mirror_dir[0] if to_mirror_dir[0] != 0 else float('inf')
                elif boundary == 3:  # 右壁
                    t_intersect = (pool_width/2 - current_pos[0]) / to_mirror_dir[0] if to_mirror_dir[0] != 0 else float('inf')
                elif boundary == 4:  # 前壁
                    t_intersect = (-pool_length/2 - current_pos[1]) / to_mirror_dir[1] if to_mirror_dir[1] != 0 else float('inf')
                else:  # 后壁
                    t_intersect = (pool_length/2 - current_pos[1]) / to_mirror_dir[1] if to_mirror_dir[1] != 0 else float('inf')
                
                # 检查t_intersect是否为有效值
                if np.isnan(t_intersect) or np.isinf(t_intersect) or t_intersect <= 0:
                    # 无效的交点，跳过此反射
                    self.log(f"警告: 边界{boundary}的反射计算无效，跳过此反射")
                    valid_reflection = False
                    break
                
                reflection_point = current_pos + t_intersect * to_mirror_dir
                
                # 检查反射点是否合理（在水池边界上）
                if boundary == 0 and abs(reflection_point[2]) > 0.01:  # 水面
                    valid_reflection = False
                    break
                elif boundary == 1 and abs(reflection_point[2] - pool_depth) > 0.01:  # 水底
                    valid_reflection = False
                    break
                elif boundary == 2 and abs(reflection_point[0] + pool_width/2) > 0.01:  # 左壁
                    valid_reflection = False
                    break
                elif boundary == 3 and abs(reflection_point[0] - pool_width/2) > 0.01:  # 右壁
                    valid_reflection = False
                    break
                elif boundary == 4 and abs(reflection_point[1] + pool_length/2) > 0.01:  # 前壁
                    valid_reflection = False
                    break
                elif boundary == 5 and abs(reflection_point[1] - pool_length/2) > 0.01:  # 后壁
                    valid_reflection = False
                    break
                
                # 计算从当前点到反射点的距离
                segment_dist = np.linalg.norm(reflection_point - current_pos)
                total_distance += segment_dist
                
                # 更新当前位置为反射点
                current_pos = reflection_point
                
                # 反射损耗
                intensity *= (1.0 - reflection_loss)
            
            # 如果反射路径有效，则添加该路径
            if valid_reflection:
                # 计算从最后一个反射点到接收点的距离
                final_segment_dist = np.linalg.norm(rx_pos - current_pos)
                total_distance += final_segment_dist
                
                # 添加此反射路径的总距离和时延
                path_distances.append(total_distance)
                path_delays.append(total_distance / sound_speed)
        
        # 创建信号基础
        signal = np.zeros(data_length)
        
        # 根据信号类型生成不同波形
        if signal_type == 'sine':
            # 正弦波 - 使用当前时间的小数部分作为相位偏移使波形动态变化
            phase_offset = (current_time % 1.0) * 2 * np.pi
            # 添加一些频率变化让波形更生动
            freq_mod = 100 * np.sin(2 * np.pi * 0.2 * current_time)
            
            # 直达波
            direct_signal = np.sin(2 * np.pi * (frequency + freq_mod) * t + phase_offset)
            
            # 将直达波添加到信号中
            signal += direct_signal
            
            # 添加反射波
            for i, delay in enumerate(path_delays[1:], 1):  # 跳过直达波
                # 跳过无效的延迟值
                if np.isnan(delay) or np.isinf(delay) or delay <= 0:
                    continue
                    
                # 计算衰减系数 (基于路径长度的衰减)
                atten_factor = path_distances[0] / path_distances[i]
                
                # 计算反射强度 (每次反射损失)
                reflection_intensity = (1.0 - reflection_loss) ** len(reflection_paths[i-1])
                
                # 增大反射波的可见度
                reflection_intensity *= 3.0
                
                # 计算时延对应的样本点偏移
                try:
                    delay_samples = int(delay * sample_rate)
                    
                    # 如果延迟小于信号长度，添加反射波
                    if delay_samples < data_length:
                        # 创建延迟后的时间轴
                        t_delayed = np.arange(data_length - delay_samples) / sample_rate
                        
                        # 创建反射波 (考虑衰减和相位变化)
                        reflected_signal = atten_factor * reflection_intensity * np.sin(2 * np.pi * (frequency + freq_mod) * t_delayed + phase_offset + np.pi)  # 相位反转
                        
                        # 将反射波添加到延迟后的位置
                        signal[delay_samples:] += reflected_signal
                except (ValueError, TypeError) as e:
                    self.log(f"警告: 添加反射波时出错: {e}, 跳过该反射波")
            
        elif signal_type == 'pulse':
            # 脉冲波参数
            pulse_width = max(10, int(data_length * 0.01))  # 脉冲宽度，至少10个样本点
            
            # 在更靠前的位置创建主脉冲以留出更多反射空间
            pulse_pos = int(data_length * 0.05)
            if pulse_pos + pulse_width >= data_length:
                pulse_pos = 0
                
            # 创建脉冲波形 (加窗正弦波)
            pulse_t = np.arange(pulse_width) / sample_rate
            window = np.hamming(pulse_width)  # 使用汉明窗使信号平滑
            pulse_wave = window * np.sin(2 * np.pi * frequency * pulse_t)
            
            # 主脉冲 (直达波)
            if pulse_pos + pulse_width <= data_length:
                signal[pulse_pos:pulse_pos+pulse_width] = pulse_wave
            
            # 记录主脉冲位置和直达波信息
            self.log(f"主脉冲: 位置={pulse_pos}, 直达波时延={path_delays[0]:.6f}s, 距离={path_distances[0]:.3f}m")
            
            # 添加反射波 - 增强反射波信号强度
            for i, delay in enumerate(path_delays[1:], 1):  # 跳过直达波
                # 跳过无效的延迟值
                if np.isnan(delay) or np.isinf(delay) or delay <= 0:
                    continue
                    
                # 计算衰减系数
                distance_ratio = path_distances[0] / path_distances[i]
                reflection_intensity = (1.0 - reflection_loss) ** len(reflection_paths[i-1])
                
                # 放大反射波振幅，使其在图中更明显
                amplitude = distance_ratio * reflection_intensity * 4.0  # 进一步增大反射波强度
                
                # 计算时延对应的样本点偏移
                try:
                    delay_samples = int(delay * sample_rate)
                    reflection_pos = pulse_pos + delay_samples
                    
                    # 添加反射波
                    if reflection_pos + pulse_width <= data_length:
                        signal[reflection_pos:reflection_pos+pulse_width] += amplitude * pulse_wave
                        
                        # 记录反射波信息到日志
                        self.log(f"添加反射波: 路径={reflection_paths[i-1]}, 时延={delay:.6f}s, 位置={reflection_pos}, 距离={path_distances[i]:.3f}m")
                except (ValueError, TypeError) as e:
                    self.log(f"警告: 添加脉冲反射波时出错: {e}, 跳过该反射波")
            
        elif signal_type == 'chirp':
            # 调频脉冲波参数
            max_chirp_duration = data_length / sample_rate * 0.1  # 最大不超过总长度的10%
            chirp_duration = min(0.01, max_chirp_duration)  # 10毫秒
            samples_in_chirp = int(chirp_duration * sample_rate)
            
            # 将脉冲位置前移以留出更多反射空间
            chirp_pos = int(data_length * 0.05)
            
            # 创建时间变化的频率参数
            chirp_start = frequency * 0.7  # 起始频率
            chirp_end = frequency * 1.3    # 结束频率
            
            # 生成调频脉冲
            if samples_in_chirp > 0:
                t_chirp = np.arange(samples_in_chirp) / sample_rate
                freq_slope = (chirp_end - chirp_start) / chirp_duration
                window = np.hamming(samples_in_chirp)  # 使用汉明窗
                chirp_signal = window * np.sin(2 * np.pi * (chirp_start * t_chirp + freq_slope * t_chirp**2 / 2))
                
                # 主脉冲 (直达波)
                if chirp_pos + samples_in_chirp <= data_length:
                    signal[chirp_pos:chirp_pos+samples_in_chirp] = chirp_signal
                
                # 记录主脉冲位置和直达波信息
                self.log(f"主脉冲: 位置={chirp_pos}, 直达波时延={path_delays[0]:.6f}s, 距离={path_distances[0]:.3f}m")
                
                # 添加反射波 - 增强反射波信号强度
                for i, delay in enumerate(path_delays[1:], 1):  # 跳过直达波
                    # 跳过无效的延迟值
                    if np.isnan(delay) or np.isinf(delay) or delay <= 0:
                        continue
                        
                    # 计算衰减系数
                    distance_ratio = path_distances[0] / path_distances[i]
                    reflection_intensity = (1.0 - reflection_loss) ** len(reflection_paths[i-1])
                    
                    # 放大反射波振幅，使其在图中更明显
                    amplitude = distance_ratio * reflection_intensity * 4.0  # 增大反射波强度
                    
                    # 计算时延对应的样本点偏移
                    try:
                        delay_samples = int(delay * sample_rate)
                        reflection_pos = chirp_pos + delay_samples
                        
                        # 添加反射波
                        if reflection_pos + samples_in_chirp <= data_length:
                            signal[reflection_pos:reflection_pos+samples_in_chirp] += amplitude * chirp_signal
                            
                            # 记录反射波信息到日志
                            self.log(f"添加反射波: 路径={reflection_paths[i-1]}, 时延={delay:.6f}s, 位置={reflection_pos}, 距离={path_distances[i]:.3f}m")
                    except (ValueError, TypeError) as e:
                        self.log(f"警告: 添加调频脉冲反射波时出错: {e}, 跳过该反射波")
        
        # 添加噪声 (降低噪声水平，使反射波更明显)
        noise_level = 0.005  # 进一步减小噪声水平，使反射波更突出
        noise = noise_level * np.random.randn(data_length)
        signal += noise
        
        # 归一化信号
        if np.max(np.abs(signal)) > 0:
            signal = signal / np.max(np.abs(signal))
        
        # 准备数据包
        data_packet = {
            "samples": signal.tolist(),
            "sample_rate": sample_rate,
            "timestamp": current_time,
            "signal_type": signal_type,
            "signal_freq": frequency
        }
        
        # 发送数据
        self.client.send_data(data_packet)
    
    def stop_data_acquisition(self):
        """停止数据采集"""
        self.log("停止数据采集")
        self.data_status.setText("数据采集已停止")
        self.data_status.setStyleSheet("")
        self.start_data_button.setEnabled(True)
        self.stop_data_button.setEnabled(False)
        
        # 停止数据生成定时器
        if hasattr(self, 'data_timer') and self.data_timer.isActive():
            self.data_timer.stop()
        
        # 发送采集停止命令到服务器
        if self.client.connected:
            self.client.send_data_command("stop_acquisition", {})
    
    def update_data_display(self, data):
        """更新数据显示（未来实现）"""
        # 当接收到新数据时，更新波形显示
        # 这个函数将在数据处理模块实现后进行扩展 