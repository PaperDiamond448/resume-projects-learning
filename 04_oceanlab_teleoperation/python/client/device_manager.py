#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
设备管理模块：用于管理已识别的机器人设备
"""

import os
import sys
import threading
import time
from PyQt5.QtCore import QObject, pyqtSignal

from client.device_detector import DeviceDetector
from client.robot_device import RobotDevice

class DeviceManager(QObject):
    """设备管理类，用于管理已识别的机器人设备"""
    
    # 定义信号
    devices_updated = pyqtSignal(list)  # 设备列表更新信号
    device_connected = pyqtSignal(dict)  # 设备连接信号
    device_disconnected = pyqtSignal(str)  # 设备断开信号
    device_status_updated = pyqtSignal(dict)  # 设备状态更新信号
    device_error = pyqtSignal(str, str)  # 设备错误信号 (device_id, error_message)
    
    def __init__(self, debug=False):
        """初始化设备管理器
        
        Args:
            debug (bool): 是否启用调试模式
        """
        super(DeviceManager, self).__init__()
        
        self.debug = debug
        
        # 创建设备识别器
        self.device_detector = DeviceDetector(scan_interval=5.0, debug=debug)
        
        # 连接设备识别器信号
        self.device_detector.devices_updated.connect(self._on_devices_updated)
        self.device_detector.device_connected.connect(self._on_device_connected)
        self.device_detector.device_disconnected.connect(self._on_device_disconnected)
        
        # 设备字典: {device_id: RobotDevice}
        self.devices = {}
        
        # 设备状态字典: {device_id: status_dict}
        self.device_status = {}
        
        # 自动连接标志
        self.auto_connect = True
    
    def start(self):
        """启动设备管理器"""
        # 启动设备识别
        self.device_detector.start()
        
        if self.debug:
            print("设备管理器已启动")
    
    def stop(self):
        """停止设备管理器"""
        # 停止设备识别
        self.device_detector.stop()
        
        # 断开所有设备连接
        for device_id, device in list(self.devices.items()):
            self.disconnect_device(device_id)
        
        if self.debug:
            print("设备管理器已停止")
    
    def connect_device(self, device_id):
        """连接到指定设备
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            bool: 连接是否成功
        """
        # 检查设备是否存在
        device_info = self.device_detector.get_device(device_id)
        if not device_info:
            if self.debug:
                print(f"设备 {device_id} 不存在")
            return False
        
        # 检查设备是否已连接
        if device_id in self.devices:
            if self.debug:
                print(f"设备 {device_id} 已连接")
            return True
        
        try:
            # 创建设备控制器
            device = RobotDevice(device_info, debug=self.debug)
            
            # 连接设备信号
            device.status_updated.connect(self._on_device_status_updated)
            device.movement_completed.connect(self._on_device_movement_completed)
            device.error_occurred.connect(lambda msg: self._on_device_error(device_id, msg))
            
            # 连接设备
            if device.connect():
                # 添加到设备字典
                self.devices[device_id] = device
                
                if self.debug:
                    print(f"已连接到设备: {device_info['name']} ({device_id})")
                
                # 发送设备连接信号
                self.device_connected.emit(device_info)
                
                return True
            else:
                if self.debug:
                    print(f"连接设备 {device_id} 失败")
                return False
            
        except Exception as e:
            if self.debug:
                print(f"连接设备 {device_id} 时出错: {e}")
            return False
    
    def disconnect_device(self, device_id):
        """断开与指定设备的连接
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            bool: 操作是否成功
        """
        # 检查设备是否已连接
        if device_id not in self.devices:
            if self.debug:
                print(f"设备 {device_id} 未连接")
            return False
        
        try:
            # 获取设备控制器
            device = self.devices[device_id]
            
            # 断开设备连接
            device.disconnect()
            
            # 从设备字典中移除
            del self.devices[device_id]
            
            # 从状态字典中移除
            if device_id in self.device_status:
                del self.device_status[device_id]
            
            if self.debug:
                print(f"已断开与设备 {device_id} 的连接")
            
            # 发送设备断开信号
            self.device_disconnected.emit(device_id)
            
            return True
            
        except Exception as e:
            if self.debug:
                print(f"断开设备 {device_id} 连接时出错: {e}")
            return False
    
    def get_device(self, device_id):
        """获取指定ID的设备控制器
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            RobotDevice: 设备控制器，如果设备不存在则返回None
        """
        return self.devices.get(device_id)
    
    def get_device_status(self, device_id):
        """获取指定设备的状态
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            dict: 设备状态，如果设备不存在则返回None
        """
        return self.device_status.get(device_id)
    
    def get_all_devices(self):
        """获取所有已连接的设备
        
        Returns:
            dict: 设备字典 {device_id: RobotDevice}
        """
        return self.devices
    
    def get_all_device_status(self):
        """获取所有设备的状态
        
        Returns:
            dict: 设备状态字典 {device_id: status_dict}
        """
        return self.device_status
    
    def get_all_available_devices(self):
        """获取所有可用设备的信息
        
        Returns:
            list: 设备信息列表
        """
        return self.device_detector.get_devices()
    
    def get_device_by_type(self, device_type):
        """获取指定类型的设备
        
        Args:
            device_type (str): 设备类型
        
        Returns:
            list: 设备列表
        """
        result = []
        for device_id, device in self.devices.items():
            if device.device_type == device_type:
                result.append(device)
        
        return result
    
    def _on_devices_updated(self, devices):
        """设备列表更新处理
        
        Args:
            devices (list): 设备列表
        """
        # 发送设备更新信号
        self.devices_updated.emit(devices)
    
    def _on_device_connected(self, device_info):
        """设备连接处理
        
        Args:
            device_info (dict): 设备信息
        """
        device_id = device_info["id"]
        
        # 如果启用了自动连接，尝试连接设备
        if self.auto_connect and device_id not in self.devices:
            self.connect_device(device_id)
    
    def _on_device_disconnected(self, device_id):
        """设备断开处理
        
        Args:
            device_id (str): 设备ID
        """
        # 如果设备已连接，断开连接
        if device_id in self.devices:
            self.disconnect_device(device_id)
    
    def _on_device_status_updated(self, status):
        """设备状态更新处理
        
        Args:
            status (dict): 设备状态
        """
        # 获取设备ID
        device_info = status.get("device_info", {})
        device_id = device_info.get("id", "")
        
        if device_id:
            # 更新设备状态
            self.device_status[device_id] = status
            
            # 发送设备状态更新信号
            self.device_status_updated.emit(status)
    
    def _on_device_movement_completed(self, device_type):
        """设备移动完成处理
        
        Args:
            device_type (str): 设备类型
        """
        # 这里可以添加移动完成的特殊处理逻辑
        pass
    
    def _on_device_error(self, device_id, error_message):
        """设备错误处理
        
        Args:
            device_id (str): 设备ID
            error_message (str): 错误消息
        """
        # 发送设备错误信号
        self.device_error.emit(device_id, error_message)
        
        if self.debug:
            print(f"设备 {device_id} 错误: {error_message}") 