#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
设备识别模块：用于自动识别USB设备和网络设备
"""

import os
import sys
import re
import socket
import threading
import time
import serial
import serial.tools.list_ports
import subprocess
from PyQt5.QtCore import QObject, pyqtSignal

class DeviceDetector(QObject):
    """设备识别类，用于自动识别USB设备和网络设备"""
    
    # 定义信号
    devices_updated = pyqtSignal(list)  # 设备列表更新信号
    device_connected = pyqtSignal(dict)  # 设备连接信号
    device_disconnected = pyqtSignal(str)  # 设备断开信号
    
    # 设备类型常量
    TYPE_ROBOT_ARM = "robot_arm"  # 两自由度机械臂
    TYPE_HYDROPHONE = "hydrophone"  # 水听器
    TYPE_TRANSDUCER = "transducer"  # 换能器
    TYPE_NETWORK_DEVICE = "network_device"  # 网络设备
    TYPE_UNKNOWN = "unknown"  # 未知设备
    
    def __init__(self, scan_interval=5.0, debug=False):
        """初始化设备识别模块
        
        Args:
            scan_interval (float): 设备扫描间隔时间(秒)
            debug (bool): 是否启用调试模式
        """
        super(DeviceDetector, self).__init__()
        
        self.scan_interval = scan_interval  # 扫描间隔（秒）
        self.debug = debug
        
        # 存储已识别的设备
        self.devices = {}
        
        # 设备扫描线程
        self.scan_thread = None
        self.running = False
        
        # 设备供应商ID和产品ID列表
        self.known_devices = {
            # 格式: (VID, PID): (设备类型, 设备描述)
            (0x2341, 0x0043): (self.TYPE_ROBOT_ARM, "Arduino Uno (机械臂控制器)"),
            (0x2341, 0x0042): (self.TYPE_ROBOT_ARM, "Arduino Mega (机械臂控制器)"),
            (0x2341, 0x8036): (self.TYPE_ROBOT_ARM, "Arduino Leonardo (机械臂控制器)"),
            (0x1A86, 0x7523): (self.TYPE_ROBOT_ARM, "CH340 串口 (机械臂控制器)"),
            (0x0403, 0x6001): (self.TYPE_ROBOT_ARM, "FTDI 串口 (机械臂控制器)"),
            (0x067B, 0x2303): (self.TYPE_HYDROPHONE, "Prolific USB-Serial (水听器)"),
            (0x10C4, 0xEA60): (self.TYPE_TRANSDUCER, "CP210x USB-UART (换能器控制器)"),
        }
        
        # 特定设备的连接信息
        self.device_info = {
            self.TYPE_ROBOT_ARM: {
                "connection_type": "serial",
                "baud_rate": 9600,
                "timeout": 1.0,
                "connection_params": {},
            },
            self.TYPE_HYDROPHONE: {
                "connection_type": "serial",
                "baud_rate": 115200,
                "timeout": 0.5,
                "connection_params": {},
            },
            self.TYPE_TRANSDUCER: {
                "connection_type": "serial",
                "baud_rate": 9600,
                "timeout": 1.0,
                "connection_params": {},
            },
            self.TYPE_NETWORK_DEVICE: {
                "connection_type": "network",
                "port": 8080,
                "timeout": 3.0,
                "connection_params": {},
            },
        }
    
    def start(self):
        """启动设备扫描"""
        if self.scan_thread is not None and self.scan_thread.is_alive():
            print("设备扫描已在运行中")
            return False
        
        self.running = True
        self.scan_thread = threading.Thread(target=self._scan_loop)
        self.scan_thread.daemon = True
        self.scan_thread.start()
        
        if self.debug:
            print("设备扫描线程已启动")
        
        return True
    
    def stop(self):
        """停止设备扫描"""
        self.running = False
        if self.scan_thread is not None:
            self.scan_thread.join(timeout=2.0)
            self.scan_thread = None
        
        if self.debug:
            print("设备扫描线程已停止")
    
    def _scan_loop(self):
        """设备扫描循环"""
        while self.running:
            try:
                # 扫描USB设备
                usb_devices = self._scan_usb_devices()
                
                # 扫描网络设备
                network_devices = self._scan_network_devices()
                
                # 合并设备列表
                all_devices = usb_devices + network_devices
                
                # 检查设备变化
                self._check_device_changes(all_devices)
                
                # 发送设备更新信号
                device_list = list(self.devices.values())
                self.devices_updated.emit(device_list)
                
            except Exception as e:
                if self.debug:
                    print(f"设备扫描错误: {e}")
            
            # 扫描间隔
            time.sleep(self.scan_interval)
    
    def _scan_usb_devices(self):
        """扫描USB设备
        
        Returns:
            list: USB设备列表
        """
        devices = []
        
        try:
            # 使用pyserial获取串口设备列表
            ports = list(serial.tools.list_ports.comports())
            
            for port in ports:
                try:
                    # 提取设备VID和PID
                    vid = 0
                    pid = 0
                    
                    # 尝试从hwid中提取VID和PID
                    if hasattr(port, 'hwid') and port.hwid:
                        vid_match = re.search(r'VID:PID=([0-9A-F]{4}):([0-9A-F]{4})', port.hwid, re.IGNORECASE)
                        if vid_match:
                            vid = int(vid_match.group(1), 16)
                            pid = int(vid_match.group(2), 16)
                    
                    # 检查是否为已知设备
                    device_type = self.TYPE_UNKNOWN
                    device_name = f"未知设备 ({port.device})"
                    
                    if (vid, pid) in self.known_devices:
                        device_type, device_name = self.known_devices[(vid, pid)]
                    
                    # 创建设备信息字典
                    port_name = port.device
                    safe_port_name = port_name.replace('/', '_')
                    safe_port_name = safe_port_name.replace('\\', '_')
                    device_id = f"{device_type}_{safe_port_name}"
                    
                    device = {
                        "id": device_id,
                        "type": device_type,
                        "name": device_name,
                        "port": port.device,
                        "description": port.description if hasattr(port, 'description') else "",
                        "vid": vid,
                        "pid": pid,
                        "hwid": port.hwid if hasattr(port, 'hwid') else "",
                        "connection_type": "usb",
                        "status": "available",
                        "info": self.device_info.get(device_type, {})
                    }
                    
                    devices.append(device)
                    
                except Exception as e:
                    if self.debug:
                        print(f"处理串口设备时出错: {e}")
            
        except Exception as e:
            if self.debug:
                print(f"扫描USB设备时出错: {e}")
        
        return devices
    
    def _scan_network_devices(self):
        """扫描网络设备
        
        Returns:
            list: 网络设备列表
        """
        devices = []
        
        try:
            # 检查本地网络中的特定设备
            # 这里可以实现特定的网络设备扫描逻辑
            # 例如：检查预定义IP地址的设备，或者使用mDNS/SSDP等协议发现设备
            
            # 示例：检查预定义的网络设备
            predefined_devices = [
                {
                    "ip": "192.168.1.100",
                    "port": 8080,
                    "name": "网络控制器",
                    "type": self.TYPE_ROBOT_ARM
                },
                {
                    "ip": "192.168.1.101",
                    "port": 8080,
                    "name": "网络水听器",
                    "type": self.TYPE_HYDROPHONE
                }
            ]
            
            for device_info in predefined_devices:
                # 检查设备是否可达
                if self._is_device_reachable(device_info["ip"], device_info["port"]):
                    device_id = f"net_{device_info['type']}_{device_info['ip']}_{device_info['port']}"
                    device = {
                        "id": device_id,
                        "type": device_info["type"],
                        "name": device_info["name"],
                        "ip": device_info["ip"],
                        "port": device_info["port"],
                        "connection_type": "network",
                        "status": "available",
                        "info": self.device_info.get(device_info["type"], {})
                    }
                    devices.append(device)
            
        except Exception as e:
            if self.debug:
                print(f"扫描网络设备时出错: {e}")
        
        return devices
    
    def _is_device_reachable(self, ip, port):
        """检查网络设备是否可达
        
        Args:
            ip (str): 设备IP地址
            port (int): 设备端口
        
        Returns:
            bool: 设备是否可达
        """
        try:
            # 简单的TCP连接测试
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)  # 设置超时时间
            result = sock.connect_ex((ip, port))
            sock.close()
            return result == 0
        except:
            return False
    
    def _check_device_changes(self, current_devices):
        """检查设备变化
        
        Args:
            current_devices (list): 当前扫描到的设备列表
        """
        # 当前设备ID集合
        current_ids = {device["id"]: device for device in current_devices}
        
        # 检查新设备
        for device_id, device in current_ids.items():
            if device_id not in self.devices:
                # 发现新设备
                if self.debug:
                    print(f"发现新设备: {device['name']} ({device_id})")
                
                # 添加到设备列表
                self.devices[device_id] = device
                
                # 发送设备连接信号
                self.device_connected.emit(device)
        
        # 检查断开的设备
        disconnected_ids = set(self.devices.keys()) - set(current_ids.keys())
        for device_id in disconnected_ids:
            # 设备已断开
            if self.debug:
                print(f"设备已断开: {self.devices[device_id]['name']} ({device_id})")
            
            # 发送设备断开信号
            self.device_disconnected.emit(device_id)
            
            # 从设备列表中移除
            del self.devices[device_id]
        
        # 更新现有设备状态
        for device_id, device in current_ids.items():
            if device_id in self.devices:
                # 更新设备状态
                self.devices[device_id].update(device)
    
    def get_devices(self, device_type=None):
        """获取设备列表
        
        Args:
            device_type (str, optional): 设备类型过滤，None表示获取所有设备
        
        Returns:
            list: 设备列表
        """
        if device_type is None:
            return list(self.devices.values())
        else:
            return [device for device in self.devices.values() if device["type"] == device_type]
    
    def get_device(self, device_id):
        """获取指定ID的设备
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            dict: 设备信息，如果设备不存在则返回None
        """
        return self.devices.get(device_id)
    
    def test_device_connection(self, device_id):
        """测试设备连接
        
        Args:
            device_id (str): 设备ID
        
        Returns:
            bool: 连接是否成功
        """
        device = self.get_device(device_id)
        if not device:
            return False
        
        try:
            if device["connection_type"] == "usb":
                # 尝试打开串口
                ser = serial.Serial(
                    port=device["port"],
                    baudrate=device["info"].get("baud_rate", 9600),
                    timeout=device["info"].get("timeout", 1.0)
                )
                ser.close()
                return True
            
            elif device["connection_type"] == "network":
                # 尝试TCP连接
                return self._is_device_reachable(device["ip"], device["port"])
            
            return False
        
        except Exception as e:
            if self.debug:
                print(f"测试设备连接时出错: {e}")
            return False 