#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
从端客户端实现
"""

import socket
import threading
import json
import time
import base64
import cv2
import numpy as np
import traceback

from .auth import AuthManager

class Client:
    """从端客户端"""
    
    def __init__(self, server_address, port, pool_id, auth_manager=None, debug=False):
        """初始化客户端"""
        self.server_address = server_address
        self.port = port
        self.pool_id = pool_id
        self.auth_manager = auth_manager or AuthManager()
        self.debug = debug
        
        # 客户端套接字
        self.client_socket = None
        
        # 参数
        self.params = {
            'hydrophone_x': 0.0,
            'hydrophone_y': 0.0,
            'hydrophone_z': 0.5,
            'hydrophone_rotation': 0.0,
            'transducer_x': 0.0,
            'transducer_y': 0.0,
            'transducer_z': 0.5,
            'transducer_rotation': 0.0,
            'water_depth': 0.5
        }
        self.params_lock = threading.Lock()
        
        # 命令队列
        self.command_queue = []
        self.command_queue_lock = threading.Lock()
        
        # 运行标志
        self.connected = False
        self.reconnecting = False
        self.reconnect_interval = 5  # 重连间隔（秒）
        self.max_reconnect_attempts = 3  # 最大重连尝试次数
        
        # 心跳检测
        self.last_heartbeat = 0
        self.heartbeat_interval = 10  # 心跳间隔（秒）
        
        # 接收线程
        self.receive_thread = None
        
        # 心跳线程
        self.heartbeat_thread = None
        
        # 重连线程
        self.reconnect_thread = None
        
        # 自动重连标志
        self.auto_reconnect = True
    
    def connect(self):
        """连接到服务器"""
        if self.connected:
            return True
        
        # 如果正在重连，等待重连完成
        if self.reconnecting:
            if self.debug:
                print("正在重连中，请等待...")
            return False
        
        self.reconnecting = True
        
        # 尝试连接3次
        max_retries = 3
        for retry in range(1, max_retries + 1):
            try:
                # 每次尝试前都创建一个新的客户端套接字
                self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.client_socket.settimeout(5)  # 设置超时时间为5秒，便于重试
                
                if self.debug:
                    print(f"尝试连接到服务器 {self.server_address}:{self.port}... (第{retry}次尝试)")
                else:
                    print(f"正在连接到服务器 {self.server_address}:{self.port}...")
                
                # 如果服务器地址是localhost或127.0.0.1，尝试使用本机IP连接
                if self.server_address.lower() in ['localhost', '127.0.0.1'] and self.pool_id:
                    try:
                        # 获取本机IP地址
                        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        s.connect(('8.8.8.8', 80))
                        local_ip = s.getsockname()[0]
                        s.close()
                        
                        if self.debug:
                            print(f"本机IP地址: {local_ip}")
                    except:
                        local_ip = None
                
                # 连接到服务器
                self.client_socket.connect((self.server_address, self.port))
                
                # 发送注册信息
                register_message = {
                    'type': 'register',
                    'pool_id': self.pool_id,
                    'timestamp': time.time()
                }
                if self.debug:
                    print(f"发送注册信息: {register_message}")
                self.client_socket.send(json.dumps(register_message).encode('utf-8'))
                
                # 接收注册响应
                print("等待服务器响应...")
                data = self.client_socket.recv(1024)
                if not data:
                    print("接收到空响应")
                    self.client_socket.close()
                    self.client_socket = None
                    
                    # 如果是最后一次尝试，则返回失败
                    if retry == max_retries:
                        self.reconnecting = False
                        return False
                    
                    # 否则等待后重试
                    time.sleep(1)
                    continue
                
                response = json.loads(data.decode('utf-8'))
                if self.debug:
                    print(f"接收到响应: {response}")
                
                if response.get('status') != 'success':
                    print(f"注册失败: {response}")
                    self.client_socket.close()
                    self.client_socket = None
                    
                    # 如果是最后一次尝试，则返回失败
                    if retry == max_retries:
                        self.reconnecting = False
                        return False
                    
                    # 否则等待后重试
                    time.sleep(1)
                    continue
                
                self.connected = True
                self.reconnecting = False
                self.last_heartbeat = time.time()
                
                # 启动接收线程
                self.receive_thread = threading.Thread(target=self._receive_data)
                self.receive_thread.daemon = True
                self.receive_thread.start()
                
                # 启动心跳线程
                self.heartbeat_thread = threading.Thread(target=self._heartbeat_thread)
                self.heartbeat_thread.daemon = True
                self.heartbeat_thread.start()
                
                print(f"已成功连接到服务器 {self.server_address}:{self.port}")
                
                return True
                
            except socket.timeout:
                print(f"连接超时: 无法在5秒内连接到服务器 {self.server_address}:{self.port}")
                if self.client_socket:
                    self.client_socket.close()
                    self.client_socket = None
                
                # 如果是最后一次尝试，则返回失败
                if retry == max_retries:
                    self.reconnecting = False
                    return False
                
                # 否则等待后重试
                print(f"将在1秒后进行第{retry+1}次尝试...")
                time.sleep(1)
                
            except ConnectionRefusedError:
                print(f"连接被拒绝: 服务器 {self.server_address}:{self.port} 可能未启动或拒绝连接")
                if self.client_socket:
                    self.client_socket.close()
                    self.client_socket = None
                
                # 如果是最后一次尝试，则返回失败
                if retry == max_retries:
                    self.reconnecting = False
                    return False
                
                # 否则等待后重试
                print(f"将在1秒后进行第{retry+1}次尝试...")
                time.sleep(1)
                
            except Exception as e:
                print(f"连接到服务器时出错: {e}")
                print(f"错误详情: {traceback.format_exc()}")
                
                if self.client_socket:
                    self.client_socket.close()
                    self.client_socket = None
                
                # 如果是最后一次尝试，则返回失败
                if retry == max_retries:
                    self.reconnecting = False
                    return False
                
                # 否则等待后重试
                print(f"将在1秒后进行第{retry+1}次尝试...")
                time.sleep(1)
        
        # 所有尝试都失败
        self.reconnecting = False
        return False
    
    def auto_reconnect_thread(self):
        """自动重连线程"""
        attempts = 0
        
        while self.auto_reconnect and not self.connected and attempts < self.max_reconnect_attempts:
            attempts += 1
            print(f"尝试自动重连 ({attempts}/{self.max_reconnect_attempts})...")
            
            if self.connect():
                print("自动重连成功!")
                return
            
            print(f"自动重连失败，将在 {self.reconnect_interval} 秒后再次尝试...")
            time.sleep(self.reconnect_interval)
        
        if not self.connected:
            print(f"自动重连失败，已达到最大尝试次数 ({self.max_reconnect_attempts})。")
    
    def _heartbeat_thread(self):
        """心跳检测线程"""
        while self.connected:
            # 每5秒检查一次
            time.sleep(5)
            
            # 发送心跳包
            if time.time() - self.last_heartbeat >= self.heartbeat_interval:
                try:
                    self.send_status()  # 发送状态作为心跳
                    self.last_heartbeat = time.time()
                    if self.debug:
                        print("发送心跳包")
                except:
                    # 心跳发送失败，可能连接已断开
                    if self.debug:
                        print("心跳发送失败，检查连接...")
                    
                    # 检查连接是否仍然有效
                    try:
                        # 尝试发送一个空的JSON对象，如果失败则认为连接已断开
                        if self.client_socket:
                            self.client_socket.send(b'{}')
                    except:
                        print("连接已断开，将尝试重连...")
                        self.disconnect()
                        
                        # 启动自动重连
                        if self.auto_reconnect:
                            self.reconnect_thread = threading.Thread(target=self.auto_reconnect_thread)
                            self.reconnect_thread.daemon = True
                            self.reconnect_thread.start()
                        
                        break
    
    def disconnect(self):
        """断开与服务器的连接"""
        if not self.connected:
            return
        
        self.connected = False
        
        # 关闭客户端套接字
        if self.client_socket:
            try:
                self.client_socket.close()
            except:
                pass
            self.client_socket = None
        
        if self.debug:
            print("已断开与服务器的连接")
    
    def _receive_data(self):
        """接收服务器数据"""
        while self.connected and self.client_socket:
            try:
                data = self.client_socket.recv(1024)
                if not data:
                    print("服务器断开连接")
                    break
                
                # 处理接收到的数据
                self._process_server_data(data)
                
                # 更新最后心跳时间
                self.last_heartbeat = time.time()
            except socket.timeout:
                # 超时不一定意味着连接断开，可能只是暂时没有数据
                continue
            except ConnectionResetError:
                print("连接被服务器重置")
                break
            except Exception as e:
                if self.debug and self.connected:
                    print(f"接收服务器数据时出错: {e}")
                    print(f"错误详情: {traceback.format_exc()}")
                break
        
        # 断开连接
        self.disconnect()
        
        # 启动自动重连
        if self.auto_reconnect:
            print("连接断开，启动自动重连...")
            self.reconnect_thread = threading.Thread(target=self.auto_reconnect_thread)
            self.reconnect_thread.daemon = True
            self.reconnect_thread.start()
    
    def _process_server_data(self, data):
        """处理服务器数据"""
        try:
            message = json.loads(data.decode('utf-8'))
            message_type = message.get('type')
            
            if message_type == 'params':
                # 更新参数
                with self.params_lock:
                    for key, value in message.get('data', {}).items():
                        if key in self.params:
                            self.params[key] = value
                
                if self.debug:
                    print(f"接收到参数更新: {message.get('data')}")
            
            elif message_type == 'command':
                # 获取命令和参数
                command = message.get('command')
                params = message.get('params', {})
                
                # 检查是否是断开连接命令
                if command == 'disconnect':
                    print("收到服务器断开连接命令")
                    
                    # 停止自动重连
                    self.auto_reconnect = False
                    
                    # 断开连接
                    self.disconnect()
                    
                    return
                
                # 添加命令到队列
                with self.command_queue_lock:
                    self.command_queue.append((command, params))
                
                if self.debug:
                    print(f"接收到命令: {command}, 参数: {params}")
        except json.JSONDecodeError:
            if self.debug:
                print("解析服务器数据时出错")
        except Exception as e:
            if self.debug:
                print(f"处理服务器数据时出错: {e}")
                print(f"错误详情: {traceback.format_exc()}")
    
    def send_status(self):
        """发送状态到服务器"""
        if not self.connected or not self.client_socket:
            return False
        
        try:
            # 构造状态消息
            with self.params_lock:
                status_message = {
                    'type': 'status',
                    'data': self.params.copy()
                }
            
            # 发送消息
            self.client_socket.send(json.dumps(status_message).encode('utf-8'))
            
            return True
        except Exception as e:
            if self.debug:
                print(f"发送状态时出错: {e}")
                print(f"错误详情: {traceback.format_exc()}")
            
            # 断开连接
            self.disconnect()
            
            return False
    
    def send_video_frame(self, frame):
        """发送视频帧到服务器"""
        if not self.connected or not self.client_socket:
            return False
        
        try:
            # 压缩图像
            _, img_encoded = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
            
            # 转换为Base64
            img_base64 = base64.b64encode(img_encoded).decode('utf-8')
            
            # 构造视频帧消息
            video_message = {
                'type': 'video_frame',
                'data': img_base64
            }
            
            # 发送消息
            self.client_socket.send(json.dumps(video_message).encode('utf-8'))
            
            return True
        except Exception as e:
            if self.debug:
                print(f"发送视频帧时出错: {e}")
                print(f"错误详情: {traceback.format_exc()}")
            
            return False
    
    def process_commands(self):
        """处理来自服务器的命令"""
        # 检查连接状态
        if not self.connected:
            return []
        
        # 处理所有待处理的命令
        commands = []
        with self.command_queue_lock:
            commands = list(self.command_queue)
            self.command_queue.clear()
        
        # 返回处理的命令列表
        return commands
    
    def _execute_command(self, command, params):
        """执行命令"""
        if command == 'reset':
            # 重置参数
            with self.params_lock:
                self.params = {
                    'hydrophone_x': 0.0,
                    'hydrophone_y': 0.0,
                    'hydrophone_z': 0.5,
                    'hydrophone_rotation': 0.0,
                    'transducer_x': 0.0,
                    'transducer_y': 0.0,
                    'transducer_z': 0.5,
                    'transducer_rotation': 0.0,
                    'water_depth': 0.5
                }
            
            if self.debug:
                print("已重置参数")
        
        elif command == 'custom_command':
            # 自定义命令
            if self.debug:
                print(f"执行自定义命令，参数: {params}")
            
            # 在这里实现自定义命令的处理逻辑
            pass
    
    def get_params(self):
        """获取当前参数"""
        with self.params_lock:
            return self.params.copy()
    
    def set_params(self, params):
        """设置参数"""
        with self.params_lock:
            for key, value in params.items():
                if key in self.params:
                    self.params[key] = value 
    
    def send_data_command(self, command, params=None):
        """发送数据采集命令"""
        if not self.connected or not self.client_socket:
            return False
        
        try:
            # 构建数据采集命令消息
            message = {
                'type': 'data_command',
                'command': command,
                'params': params or {}
            }
            
            # 发送命令
            self.client_socket.send(json.dumps(message).encode('utf-8'))
            
            return True
        except Exception as e:
            print(f"发送数据采集命令时出错: {e}")
            return False
    
    def send_data(self, data):
        """发送传感器数据到服务器
        
        Args:
            data: 可以是以下两种形式之一:
                1. 完整的数据字典: 
                   {
                      'samples': [...],      # 数据样本数组
                      'sample_rate': 44100,  # 采样率
                      'timestamp': 1234567,  # 时间戳
                      'signal_type': 'sine', # 信号类型
                      'signal_freq': 1000    # 信号频率
                   }
                2. 兼容旧版本的参数: samples数组
        """
        if not self.connected or not self.client_socket:
            return False
        
        try:
            # 检查参数类型
            if isinstance(data, dict) and 'samples' in data:
                # 构造数据消息，使用已有字典
                message = {
                    'type': 'data',
                    'data': data
                }
            else:
                # 兼容旧版本API: 参数是样本数组
                samples = data
                message = {
                    'type': 'data',
                    'data': {
                        'samples': samples,
                        'sample_rate': 44100,  # 默认采样率
                        'timestamp': time.time(),
                        'signal_type': 'sine',
                        'signal_freq': 1000
                    }
                }
            
            # 发送消息
            self.client_socket.send(json.dumps(message).encode('utf-8'))
            
            return True
        except Exception as e:
            if self.debug:
                print(f"发送数据时出错: {e}")
            return False 