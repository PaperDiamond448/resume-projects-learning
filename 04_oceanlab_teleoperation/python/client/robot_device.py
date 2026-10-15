#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
机器人设备控制模块：用于控制两自由度机械臂等机器人设备
"""

import os
import sys
import time
import threading
import serial
import socket
import json
from PyQt5.QtCore import QObject, pyqtSignal

class RobotDevice(QObject):
    """机器人设备控制类，用于控制两自由度机械臂等设备"""
    
    # 定义信号
    status_updated = pyqtSignal(dict)  # 状态更新信号
    movement_completed = pyqtSignal(str)  # 运动完成信号
    error_occurred = pyqtSignal(str)  # 错误信号
    
    # 设备类型常量
    TYPE_ROBOT_ARM = "robot_arm"  # 两自由度机械臂
    TYPE_HYDROPHONE = "hydrophone"  # 水听器
    TYPE_TRANSDUCER = "transducer"  # 换能器
    TYPE_NETWORK_DEVICE = "network_device"  # 网络设备
    
    # 设备状态常量
    STATUS_DISCONNECTED = "disconnected"  # 断开连接
    STATUS_CONNECTED = "connected"  # 已连接
    STATUS_ERROR = "error"  # 错误
    STATUS_MOVING = "moving"  # 正在移动
    STATUS_READY = "ready"  # 就绪
    
    def __init__(self, device_info=None, debug=False):
        """初始化机器人设备控制器
        
        Args:
            device_info (dict): 设备信息
            debug (bool): 是否启用调试模式
        """
        super(RobotDevice, self).__init__()
        
        self.device_info = device_info or {}
        self.debug = debug
        
        # 设备连接
        self.connection = None
        self.connection_type = None
        
        # 设备状态
        self.status = self.STATUS_DISCONNECTED
        self.position = {'x': 0.0, 'y': 0.0, 'z': 0.0, 'angle': 0.0}
        self.target_position = None
        self.is_moving = False
        
        # 运动控制线程
        self.movement_thread = None
        self.running = False
        
        # 命令队列
        self.command_queue = []
        self.command_queue_lock = threading.Lock()
        
        # 设备类型
        self.device_type = self.device_info.get("type", "")
        
        # 设备参数
        self.params = {
            # 默认参数
            "speed": 50,  # 移动速度(0-100)
            "acceleration": 50,  # 加速度(0-100)
            "precision": "high",  # 精度(low/medium/high)
        }
        
        # 设备限制
        self.limits = {
            "x": (-1.0, 1.0),  # X轴范围(米)
            "y": (-0.25, 0.25),  # Y轴范围(米)
            "z": (0.3, 1.0),  # Z轴范围(米)
            "angle": (0.0, 360.0),  # 角度范围(度)
        }
    
    def connect(self):
        """连接到设备
        
        Returns:
            bool: 连接是否成功
        """
        if not self.device_info:
            self.error_occurred.emit("未指定设备信息")
            return False
        
        # 检查设备类型和连接方式
        self.connection_type = self.device_info.get("connection_type", "")
        
        try:
            if self.connection_type == "usb":
                # 串口连接
                port = self.device_info.get("port", "")
                if not port:
                    self.error_occurred.emit("未指定串口")
                    return False
                
                # 获取连接参数
                baud_rate = self.device_info.get("info", {}).get("baud_rate", 9600)
                timeout = self.device_info.get("info", {}).get("timeout", 1.0)
                
                # 打开串口
                self.connection = serial.Serial(
                    port=port,
                    baudrate=baud_rate,
                    timeout=timeout
                )
                
                if self.debug:
                    print(f"已连接到串口设备: {port}, 波特率: {baud_rate}")
                
            elif self.connection_type == "network":
                # 网络连接
                ip = self.device_info.get("ip", "")
                port = self.device_info.get("port", 0)
                
                if not ip or not port:
                    self.error_occurred.emit("未指定IP地址或端口")
                    return False
                
                # 创建TCP连接
                self.connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.connection.settimeout(3.0)
                self.connection.connect((ip, port))
                
                if self.debug:
                    print(f"已连接到网络设备: {ip}:{port}")
                
            else:
                self.error_occurred.emit(f"不支持的连接类型: {self.connection_type}")
                return False
            
            # 更新状态
            self.status = self.STATUS_CONNECTED
            
            # 启动命令处理线程
            self.running = True
            self.movement_thread = threading.Thread(target=self._command_loop)
            self.movement_thread.daemon = True
            self.movement_thread.start()
            
            # 发送初始化命令
            self._send_init_command()
            
            # 更新状态
            self._update_status()
            
            return True
            
        except Exception as e:
            self.error_occurred.emit(f"连接设备时出错: {str(e)}")
            self.status = self.STATUS_ERROR
            return False
    
    def disconnect(self):
        """断开与设备的连接"""
        self.running = False
        
        if self.movement_thread:
            self.movement_thread.join(timeout=1.0)
            self.movement_thread = None
        
        if self.connection:
            try:
                if self.connection_type == "usb":
                    self.connection.close()
                elif self.connection_type == "network":
                    self.connection.close()
            except:
                pass
            
            self.connection = None
        
        self.status = self.STATUS_DISCONNECTED
        self._update_status()
    
    def move_to_position(self, position):
        """移动到指定位置
        
        Args:
            position (dict): 目标位置，包含x, y, z, angle等键
        
        Returns:
            bool: 命令是否成功发送
        """
        if self.status != self.STATUS_CONNECTED and self.status != self.STATUS_READY:
            self.error_occurred.emit("设备未连接或未就绪")
            return False
        
        # 检查位置是否在限制范围内
        for axis, value in position.items():
            if axis in self.limits:
                min_val, max_val = self.limits[axis]
                if value < min_val or value > max_val:
                    self.error_occurred.emit(f"{axis}轴位置超出范围: {value} (范围: {min_val}-{max_val})")
                    return False
        
        # 创建移动命令
        command = {
            "type": "move",
            "position": position,
            "params": self.params
        }
        
        # 添加到命令队列
        with self.command_queue_lock:
            self.command_queue.append(command)
        
        # 更新状态
        self.target_position = position
        self.is_moving = True
        self.status = self.STATUS_MOVING
        self._update_status()
        
        return True
    
    def stop_movement(self):
        """停止当前移动"""
        # 创建停止命令
        command = {
            "type": "stop"
        }
        
        # 添加到命令队列
        with self.command_queue_lock:
            # 清空队列
            self.command_queue.clear()
            # 添加停止命令
            self.command_queue.append(command)
        
        # 更新状态
        self.is_moving = False
        self.status = self.STATUS_READY
        self._update_status()
        
        return True
    
    def set_params(self, params):
        """设置设备参数
        
        Args:
            params (dict): 参数字典
        
        Returns:
            bool: 是否成功设置
        """
        # 更新参数
        self.params.update(params)
        
        # 如果已连接，发送参数更新命令
        if self.status == self.STATUS_CONNECTED or self.status == self.STATUS_READY:
            command = {
                "type": "set_params",
                "params": self.params
            }
            
            # 添加到命令队列
            with self.command_queue_lock:
                self.command_queue.append(command)
            
            return True
        
        return False
    
    def get_status(self):
        """获取设备状态
        
        Returns:
            dict: 状态字典
        """
        return {
            "status": self.status,
            "position": self.position,
            "target_position": self.target_position,
            "is_moving": self.is_moving,
            "device_type": self.device_type,
            "device_info": self.device_info,
            "params": self.params
        }
    
    def _command_loop(self):
        """命令处理循环"""
        while self.running:
            try:
                # 检查命令队列
                with self.command_queue_lock:
                    if self.command_queue:
                        command = self.command_queue.pop(0)
                        self._process_command(command)
                
                # 短暂休眠
                time.sleep(0.05)
                
            except Exception as e:
                if self.debug:
                    print(f"命令处理错误: {e}")
                self.error_occurred.emit(f"命令处理错误: {str(e)}")
    
    def _process_command(self, command):
        """处理命令
        
        Args:
            command (dict): 命令字典
        """
        command_type = command.get("type", "")
        
        if command_type == "move":
            # 移动命令
            self._send_move_command(command)
        
        elif command_type == "stop":
            # 停止命令
            self._send_stop_command()
        
        elif command_type == "set_params":
            # 设置参数命令
            self._send_params_command(command)
    
    def _send_init_command(self):
        """发送初始化命令"""
        try:
            if self.connection_type == "usb":
                # 串口初始化命令
                init_cmd = b"INIT\r\n"
                self.connection.write(init_cmd)
                
                # 等待响应
                response = self.connection.readline().decode('utf-8', errors='ignore').strip()
                
                if "OK" in response:
                    self.status = self.STATUS_READY
                    
                    # 尝试获取当前位置
                    self.connection.write(b"GET_POS\r\n")
                    pos_response = self.connection.readline().decode('utf-8', errors='ignore').strip()
                    
                    # 解析位置信息
                    try:
                        if pos_response.startswith("POS:"):
                            pos_data = pos_response[4:].split(',')
                            if len(pos_data) >= 4:
                                self.position = {
                                    'x': float(pos_data[0]),
                                    'y': float(pos_data[1]),
                                    'z': float(pos_data[2]),
                                    'angle': float(pos_data[3])
                                }
                    except:
                        pass
                
                else:
                    self.error_occurred.emit("设备初始化失败")
                    self.status = self.STATUS_ERROR
            
            elif self.connection_type == "network":
                # 网络初始化命令
                init_cmd = {
                    "cmd": "init",
                    "params": {}
                }
                
                # 发送JSON命令
                self.connection.send(json.dumps(init_cmd).encode('utf-8') + b'\n')
                
                # 等待响应
                response_data = b""
                while b'\n' not in response_data:
                    chunk = self.connection.recv(1024)
                    if not chunk:
                        break
                    response_data += chunk
                
                if response_data:
                    try:
                        response = json.loads(response_data.decode('utf-8').strip())
                        if response.get("status") == "ok":
                            self.status = self.STATUS_READY
                            
                            # 更新位置信息
                            if "position" in response:
                                self.position = response["position"]
                        else:
                            self.error_occurred.emit(f"设备初始化失败: {response.get('message', '未知错误')}")
                            self.status = self.STATUS_ERROR
                    except:
                        self.error_occurred.emit("解析设备响应失败")
                        self.status = self.STATUS_ERROR
                else:
                    self.error_occurred.emit("设备无响应")
                    self.status = self.STATUS_ERROR
        
        except Exception as e:
            self.error_occurred.emit(f"发送初始化命令时出错: {str(e)}")
            self.status = self.STATUS_ERROR
    
    def _send_move_command(self, command):
        """发送移动命令
        
        Args:
            command (dict): 移动命令
        """
        position = command.get("position", {})
        params = command.get("params", {})
        
        try:
            if self.connection_type == "usb":
                # 构建串口移动命令
                # 格式: MOVE,x,y,z,angle,speed
                move_cmd = f"MOVE,{position.get('x', 0.0)},{position.get('y', 0.0)},{position.get('z', 0.0)},{position.get('angle', 0.0)},{params.get('speed', 50)}\r\n"
                self.connection.write(move_cmd.encode('utf-8'))
                
                # 模拟移动过程
                self._simulate_movement(position)
                
            elif self.connection_type == "network":
                # 网络移动命令
                move_cmd = {
                    "cmd": "move",
                    "position": position,
                    "params": params
                }
                
                # 发送JSON命令
                self.connection.send(json.dumps(move_cmd).encode('utf-8') + b'\n')
                
                # 模拟移动过程
                self._simulate_movement(position)
        
        except Exception as e:
            self.error_occurred.emit(f"发送移动命令时出错: {str(e)}")
            self.status = self.STATUS_ERROR
    
    def _send_stop_command(self):
        """发送停止命令"""
        try:
            if self.connection_type == "usb":
                # 串口停止命令
                stop_cmd = b"STOP\r\n"
                self.connection.write(stop_cmd)
                
            elif self.connection_type == "network":
                # 网络停止命令
                stop_cmd = {
                    "cmd": "stop"
                }
                
                # 发送JSON命令
                self.connection.send(json.dumps(stop_cmd).encode('utf-8') + b'\n')
            
            # 更新状态
            self.is_moving = False
            self.status = self.STATUS_READY
            self._update_status()
            
        except Exception as e:
            self.error_occurred.emit(f"发送停止命令时出错: {str(e)}")
    
    def _send_params_command(self, command):
        """发送参数设置命令
        
        Args:
            command (dict): 参数命令
        """
        params = command.get("params", {})
        
        try:
            if self.connection_type == "usb":
                # 串口参数命令
                # 格式: SET_PARAMS,param1=value1,param2=value2,...
                params_str = ",".join([f"{key}={value}" for key, value in params.items()])
                params_cmd = f"SET_PARAMS,{params_str}\r\n"
                self.connection.write(params_cmd.encode('utf-8'))
                
            elif self.connection_type == "network":
                # 网络参数命令
                params_cmd = {
                    "cmd": "set_params",
                    "params": params
                }
                
                # 发送JSON命令
                self.connection.send(json.dumps(params_cmd).encode('utf-8') + b'\n')
        
        except Exception as e:
            self.error_occurred.emit(f"发送参数命令时出错: {str(e)}")
    
    def _simulate_movement(self, target_position):
        """模拟移动过程
        
        Args:
            target_position (dict): 目标位置
        """
        # 计算移动时间（基于速度参数和距离）
        speed = self.params.get("speed", 50) / 100.0  # 将速度转换为0-1范围
        
        # 计算距离
        distance = 0.0
        for axis in ['x', 'y', 'z']:
            if axis in target_position and axis in self.position:
                distance += (target_position[axis] - self.position[axis]) ** 2
        
        distance = distance ** 0.5  # 欧氏距离
        
        # 估算移动时间（秒）
        # 速度越快，时间越短，最短0.5秒，最长5秒
        move_time = max(0.5, min(5.0, (1.0 - speed) * 4.5 + 0.5))
        
        # 更新状态
        self.is_moving = True
        self.status = self.STATUS_MOVING
        self._update_status()
        
        # 模拟移动过程
        time.sleep(move_time)
        
        # 更新位置
        self.position = target_position.copy()
        self.is_moving = False
        self.status = self.STATUS_READY
        
        # 发送移动完成信号
        self.movement_completed.emit(self.device_type)
        
        # 更新状态
        self._update_status()
    
    def _update_status(self):
        """更新并发送状态信号"""
        status = self.get_status()
        self.status_updated.emit(status) 