#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
主端服务器实现
"""

import socket
import threading
import json
import time
import base64
import numpy as np
import cv2
import ipaddress
from PyQt5.QtCore import QObject, pyqtSignal

from .auth import AuthManager

class Server(QObject):
    """主端服务器"""
    
    # 信号
    pool_connected = pyqtSignal(str)
    pool_disconnected = pyqtSignal(str)
    video_frame_received = pyqtSignal(str, np.ndarray)
    data_received = pyqtSignal(str, dict)  # 新增：接收到数据时发射此信号
    error_occurred = pyqtSignal(str)  # 新增：出错时发射此信号
    pools_discovered = pyqtSignal(list)  # 新增：发现实验池时发射此信号
    
    def __init__(self, port=8000, auth_manager=None, debug=False):
        """初始化服务器
        
        Args:
            port (int or list): 服务器端口，可以是单个端口或端口列表
            auth_manager (AuthManager): 认证管理器实例
            debug (bool): 是否开启调试模式
        """
        super().__init__()
        
        # 保存实例引用
        self.auth_manager = auth_manager
        self.debug = debug
        
        # 服务器配置
        self.host = '0.0.0.0'  # 监听所有网络接口
        
        # 处理端口参数，确保它是一个列表
        if isinstance(port, list):
            self.ports = port  # 端口已经是列表
        elif isinstance(port, int):
            self.ports = [port]  # 将单个端口转换为列表
        else:
            self.ports = [8000]  # 默认端口
            print(f"警告: 提供的端口类型无效: {type(port)}，使用默认端口8000")
            
        self.default_port = self.ports[0] if self.ports else 8000  # 设置默认端口为第一个端口
        
        # 服务器套接字字典
        self.server_sockets = {}  # 存储服务器套接字
        
        # 连接的客户端
        self.clients = {}  # 存储客户端连接: {pool_id: socket}
        self.clients_lock = threading.Lock()
        
        # 实验池数据
        self.pools = {}  # 存储实验池数据: {pool_id: {...}}
        self.pools_lock = threading.Lock()
        
        # 系统状态
        self.running = False
        self.scanning = False
        
        # 超时设置
        self.heartbeat_timeout = 30  # 心跳超时时间（秒）
        self.refresh_interval = 10  # 刷新间隔（秒）
        
        # 统计信息
        self.stats = {
            'start_time': time.time(),
            'total_connections': 0,
            'active_connections': 0,
            'messages_received': 0,
            'messages_sent': 0,
            'errors': 0,
            'discoveries': 0,
            'uptime': 0
        }
        
        # 自动发现的池
        self.discovered_pools = {}
        
        # 线程
        self.accept_threads = {}
        self.monitor_thread = None
        self.scan_thread = None
        
        # 连接状态映射
        self.connection_states = {}
    
    def start(self):
        """启动服务器"""
        if self.running:
            return
        
        try:
            # 创建服务器套接字
            for port in self.ports:
                server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                
                # 绑定地址并开始监听
                print(f"正在绑定服务器地址 0.0.0.0:{port}...")
                server_socket.bind((self.host, port))
                server_socket.listen(5)
                
                self.server_sockets[port] = server_socket
            
            self.running = True
            
            # 启动接受连接线程
            for port, server_socket in self.server_sockets.items():
                accept_thread = threading.Thread(target=self._accept_connections, args=(server_socket, port))
                accept_thread.daemon = True
                accept_thread.start()
            
            # 启动连接监控线程
            self.monitor_thread = threading.Thread(target=self._monitor_connections)
            self.monitor_thread.daemon = True
            self.monitor_thread.start()
            
            # 启动网络扫描线程
            self.start_scanning()
            
            print(f"服务器已成功启动，监听端口 {self.ports}")
            print(f"有效的实验池ID: {self.auth_manager.get_valid_pools()}")
            
            return True
        except Exception as e:
            print(f"启动服务器时出错: {e}")
            import traceback
            traceback.print_exc()
            self.error_occurred.emit(f"启动服务器时出错: {e}")
            return False
    
    def stop(self):
        """停止服务器"""
        if not self.running:
            return
        
        self.running = False
        
        # 关闭所有客户端连接
        with self.clients_lock:
            for server_socket in self.server_sockets.values():
                for client_socket in self.clients.values():
                    try:
                        client_socket.close()
                    except:
                        pass
            self.clients.clear()
        
        # 关闭所有服务器套接字
        for server_socket in self.server_sockets.values():
            try:
                server_socket.close()
            except:
                pass
        
        # 清空实验池数据
        with self.pools_lock:
            self.pools.clear()
        
        if self.debug:
            print("服务器已停止")
    
    def _accept_connections(self, server_socket, port):
        """接受客户端连接"""
        while self.running:
            try:
                client_socket, address = server_socket.accept()
                
                # 启动客户端处理线程
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_socket, address, port)
                )
                client_thread.daemon = True
                client_thread.start()
                
                if self.debug:
                    print(f"接受来自 {address} 的连接")
            except:
                if self.running:
                    time.sleep(0.1)
    
    def _monitor_connections(self):
        """监控连接状态的线程"""
        while self.running:
            try:
                current_time = time.time()
                disconnected_pools = []
                
                # 检查每个实验池的最后活动时间
                with self.pools_lock:
                    for pool_id, pool_data in self.pools.items():
                        last_seen = pool_data.get('last_seen', 0)
                        # 如果超过心跳超时时间没有收到消息，认为连接断开
                        if current_time - last_seen > self.heartbeat_timeout:
                            if self.debug:
                                print(f"实验池 {pool_id} 超过 {self.heartbeat_timeout} 秒没有响应，认为连接断开")
                            disconnected_pools.append(pool_id)
                
                # 断开超时的连接
                for pool_id in disconnected_pools:
                    with self.clients_lock:
                        if pool_id in self.clients:
                            try:
                                self.clients[pool_id].close()
                                del self.clients[pool_id]
                            except:
                                pass
                    
                    with self.pools_lock:
                        if pool_id in self.pools:
                            del self.pools[pool_id]
                    
                    # 更新状态
                    self.stats['active_connections'] = len(self.clients)
                    
                    # 发送信号
                    self.pool_disconnected.emit(pool_id)
                    
                    print(f"实验池 {pool_id} 超时断开连接")
                
                # 定期发送刷新信号（可被前端用于更新UI）
                if disconnected_pools or current_time % self.refresh_interval < 1:
                    with self.pools_lock:
                        current_pools = list(self.pools.keys())
                    self.pools_discovered.emit(current_pools)
                
                # 休眠一段时间
                time.sleep(5)
            except Exception as e:
                if self.debug:
                    print(f"监控连接时出错: {e}")
                    import traceback
                    traceback.print_exc()
                self.stats['errors'] += 1
                self.error_occurred.emit(f"监控连接时出错: {e}")
    
    def _handle_client(self, client_socket, address, port):
        """处理客户端连接"""
        pool_id = None
        
        try:
            # 设置接收超时
            client_socket.settimeout(10)
            
            # 更新统计信息
            self.stats['total_connections'] += 1
            
            # 接收客户端注册信息
            if self.debug:
                print(f"等待来自 {address} 的注册信息...")
            
            data = client_socket.recv(1024)
            if not data:
                if self.debug:
                    print(f"从 {address} 接收到空数据")
                return
            
            # 解析注册信息
            try:
                message = json.loads(data.decode('utf-8'))
                if self.debug:
                    print(f"收到来自 {address} 的消息: {message}")
                
                # 更新统计信息
                self.stats['messages_received'] += 1
                
                if message.get('type') != 'register':
                    if self.debug:
                        print(f"来自 {address} 的消息类型不是 'register'")
                    
                    # 发送错误响应
                    response = {
                        'type': 'register_response',
                        'status': 'error',
                        'message': 'Invalid message type'
                    }
                    client_socket.send(json.dumps(response).encode('utf-8'))
                    
                    # 更新统计信息
                    self.stats['messages_sent'] += 1
                    self.stats['errors'] += 1
                    return
                
                pool_id = message.get('pool_id')
                if not pool_id:
                    if self.debug:
                        print(f"来自 {address} 的注册消息没有指定pool_id")
                    
                    # 发送错误响应
                    response = {
                        'type': 'register_response',
                        'status': 'error',
                        'message': 'Missing pool_id'
                    }
                    client_socket.send(json.dumps(response).encode('utf-8'))
                    
                    # 更新统计信息
                    self.stats['messages_sent'] += 1
                    self.stats['errors'] += 1
                    return
                
                # 记录连接状态
                self.connection_states[pool_id] = "正在验证"
                
                # 打印详细的连接信息
                print(f"实验池 {pool_id} 尝试从 {address} 连接")
                
                # 验证客户端
                if not self.auth_manager.validate_pool(pool_id):
                    if self.debug:
                        print(f"实验池 {pool_id} 验证失败")
                    
                    # 发送错误响应
                    response = {
                        'type': 'register_response',
                        'status': 'error',
                        'message': 'Invalid pool_id'
                    }
                    client_socket.send(json.dumps(response).encode('utf-8'))
                    
                    # 更新统计信息
                    self.stats['messages_sent'] += 1
                    self.stats['errors'] += 1
                    
                    # 更新连接状态
                    self.connection_states[pool_id] = "验证失败"
                    return
                
                # 更新连接状态
                self.connection_states[pool_id] = "已验证"
                
                # 检查是否已存在同ID的连接，如果有则关闭旧连接
                with self.clients_lock:
                    if pool_id in self.clients and self.clients[pool_id] != client_socket:
                        if self.debug:
                            print(f"实验池 {pool_id} 已经连接，关闭旧连接")
                        try:
                            old_socket = self.clients[pool_id]
                            old_socket.close()
                        except:
                            pass
                        
                        # 更新连接状态
                        self.connection_states[pool_id] = "替换旧连接"
                
                # 注册客户端
                with self.clients_lock:
                    self.clients[pool_id] = client_socket
                    
                    # 更新统计信息
                    self.stats['active_connections'] = len(self.clients)
                
                # 创建实验池数据
                with self.pools_lock:
                    self.pools[pool_id] = {
                        'address': address,
                        'connected_at': time.time(),
                        'last_seen': time.time(),
                        'hydrophone_x': 0.0,
                        'hydrophone_y': 0.0,
                        'hydrophone_z': 0.5,
                        'hydrophone_rotation': 0.0,
                        'transducer_x': 0.0,
                        'transducer_y': 0.0,
                        'transducer_z': 0.5,
                        'transducer_rotation': 0.0,
                        'water_depth': 0.5,
                        'video_frame': None,
                        'connection_state': 'connected'
                    }
                
                # 发送确认消息
                response = {
                    'type': 'register_response',
                    'status': 'success',
                    'timestamp': time.time()
                }
                client_socket.send(json.dumps(response).encode('utf-8'))
                
                # 更新统计信息
                self.stats['messages_sent'] += 1
                
                # 更新连接状态
                self.connection_states[pool_id] = "已连接"
                
                # 发射信号
                self.pool_connected.emit(pool_id)
                
                # 发送实验池列表更新信号
                with self.pools_lock:
                    current_pools = list(self.pools.keys())
                self.pools_discovered.emit(current_pools)
                
                print(f"实验池 {pool_id} 已成功注册")
                
                # 向新连接的实验池发送心跳请求
                try:
                    heartbeat_message = {
                        'type': 'heartbeat_request',
                        'timestamp': time.time()
                    }
                    client_socket.send(json.dumps(heartbeat_message).encode('utf-8'))
                    
                    # 更新统计信息
                    self.stats['messages_sent'] += 1
                except:
                    if self.debug:
                        print(f"发送心跳请求到实验池 {pool_id} 失败")
                
                # 进入接收循环
                while self.running:
                    try:
                        # 设置较大的缓冲区接收视频帧等大数据
                        data = client_socket.recv(1024 * 1024)  # 1MB缓冲区
                        if not data:
                            if self.debug:
                                print(f"实验池 {pool_id} 连接关闭")
                            break
                        
                        # 处理接收到的数据
                        self._process_client_data(pool_id, data)
                        
                        # 更新最后活动时间
                        with self.pools_lock:
                            if pool_id in self.pools:
                                self.pools[pool_id]['last_seen'] = time.time()
                        
                        # 更新统计信息
                        self.stats['messages_received'] += 1
                    except socket.timeout:
                        # 发送心跳请求
                        try:
                            heartbeat_message = {
                                'type': 'heartbeat_request',
                                'timestamp': time.time()
                            }
                            client_socket.send(json.dumps(heartbeat_message).encode('utf-8'))
                            
                            # 更新统计信息
                            self.stats['messages_sent'] += 1
                            
                            if self.debug:
                                print(f"发送心跳请求到实验池 {pool_id}")
                        except:
                            if self.debug:
                                print(f"发送心跳请求到实验池 {pool_id} 失败，连接可能已断开")
                            break
                    except ConnectionResetError:
                        if self.debug:
                            print(f"与实验池 {pool_id} 的连接被重置")
                        break
                    except Exception as e:
                        if self.debug:
                            print(f"从实验池 {pool_id} 接收数据时出错: {e}")
                            import traceback
                            traceback.print_exc()
                        
                        # 更新统计信息
                        self.stats['errors'] += 1
                        self.error_occurred.emit(f"从实验池 {pool_id} 接收数据时出错: {e}")
                        break
                    
            except json.JSONDecodeError:
                if self.debug:
                    print(f"解析来自 {address} 的注册信息时出错: 无效的JSON")
                
                # 发送错误响应
                response = {
                    'type': 'register_response',
                    'status': 'error',
                    'message': 'Invalid JSON'
                }
                client_socket.send(json.dumps(response).encode('utf-8'))
                
                # 更新统计信息
                self.stats['messages_sent'] += 1
                self.stats['errors'] += 1
                return
        except Exception as e:
            if self.debug:
                print(f"处理客户端 {address} 时出错: {e}")
                import traceback
                traceback.print_exc()
            
            # 更新统计信息
            self.stats['errors'] += 1
            self.error_occurred.emit(f"处理客户端 {address} 时出错: {e}")
        finally:
            # 关闭连接
            try:
                client_socket.close()
            except:
                pass
            
            # 移除客户端
            if pool_id:
                with self.clients_lock:
                    if pool_id in self.clients:
                        del self.clients[pool_id]
                        
                        # 更新统计信息
                        self.stats['active_connections'] = len(self.clients)
                
                with self.pools_lock:
                    if pool_id in self.pools:
                        self.pools[pool_id]['connection_state'] = 'disconnected'
                        del self.pools[pool_id]
                
                # 更新连接状态
                self.connection_states[pool_id] = "已断开"
                
                # 发射信号
                self.pool_disconnected.emit(pool_id)
                
                # 发送实验池列表更新信号
                with self.pools_lock:
                    current_pools = list(self.pools.keys())
                self.pools_discovered.emit(current_pools)
                
                print(f"实验池 {pool_id} 已断开连接")
    
    def _process_client_data(self, pool_id, data):
        """处理客户端数据"""
        try:
            # 尝试解析JSON
            message = json.loads(data.decode('utf-8'))
            message_type = message.get('type')
            
            if message_type == 'status':
                # 更新实验池状态
                with self.pools_lock:
                    if pool_id in self.pools:
                        for key, value in message.get('data', {}).items():
                            self.pools[pool_id][key] = value
                            
                if self.debug:
                    print(f"收到来自实验池 {pool_id} 的状态更新")
            
            elif message_type == 'video_frame':
                # 处理视频帧
                frame_data = message.get('data')
                if frame_data:
                    try:
                        # 解码Base64图像
                        img_data = base64.b64decode(frame_data)
                        nparr = np.frombuffer(img_data, np.uint8)
                        frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                        
                        if frame is None:
                            if self.debug:
                                print(f"无法解码来自实验池 {pool_id} 的视频帧")
                            return
                        
                        # 更新实验池视频帧
                        with self.pools_lock:
                            if pool_id in self.pools:
                                self.pools[pool_id]['video_frame'] = frame
                        
                        # 发射信号
                        self.video_frame_received.emit(pool_id, frame)
                        
                        if self.debug:
                            print(f"收到来自实验池 {pool_id} 的视频帧, 分辨率: {frame.shape[1]}x{frame.shape[0]}")
                    except Exception as e:
                        if self.debug:
                            print(f"处理视频帧时出错: {e}")
                        self.stats['errors'] += 1
            
            elif message_type == 'data':
                # 处理数据
                data_content = message.get('data', {})
                if data_content:
                    # 更新实验池数据
                    with self.pools_lock:
                        if pool_id in self.pools:
                            if 'sensor_data' not in self.pools[pool_id]:
                                self.pools[pool_id]['sensor_data'] = {}
                            
                            self.pools[pool_id]['sensor_data'] = data_content
                    
                    # 发射信号
                    self.data_received.emit(pool_id, data_content)
                    
                    if self.debug:
                        print(f"接收到来自实验池 {pool_id} 的数据: {data_content}")
            
            elif message_type == 'heartbeat_response':
                # 处理心跳响应
                if self.debug:
                    print(f"收到来自实验池 {pool_id} 的心跳响应")
                
                # 更新最后活动时间
                with self.pools_lock:
                    if pool_id in self.pools:
                        self.pools[pool_id]['last_seen'] = time.time()
            
            elif message_type == 'error':
                # 处理错误消息
                error_msg = message.get('message', '未知错误')
                print(f"收到来自实验池 {pool_id} 的错误消息: {error_msg}")
                self.error_occurred.emit(f"实验池 {pool_id} 报告错误: {error_msg}")
                self.stats['errors'] += 1
            
            elif message_type == 'data_command':
                # 处理数据命令
                command = message.get('command')
                params = message.get('params', {})
                
                if command == 'device_status' and 'devices' in params:
                    # 更新设备状态
                    with self.pools_lock:
                        if pool_id in self.pools:
                            self.pools[pool_id]['device_status'] = params
                    
                    # 发射信号 - 使用data_received信号转发设备状态
                    self.data_received.emit(pool_id, {'device_status': params})
                    
                    if self.debug:
                        print(f"收到来自实验池 {pool_id} 的设备状态更新")
                else:
                    # 处理其他类型的数据命令
                    self.data_received.emit(pool_id, {'command': command, 'params': params})
                    
                    if self.debug:
                        print(f"收到来自实验池 {pool_id} 的数据命令: {command}")
            
            else:
                # 未知消息类型
                if self.debug:
                    print(f"收到来自实验池 {pool_id} 的未知类型消息: {message_type}")
        except json.JSONDecodeError:
            # 不是JSON格式的数据，可能是二进制数据
            if self.debug:
                print(f"收到来自实验池 {pool_id} 的非JSON格式数据，长度: {len(data)}")
        except Exception as e:
            if self.debug:
                print(f"处理来自实验池 {pool_id} 的数据时出错: {e}")
                import traceback
                traceback.print_exc()
            self.stats['errors'] += 1
            self.error_occurred.emit(f"处理来自实验池 {pool_id} 的数据时出错: {e}")
    
    def send_params_to_pool(self, pool_id, params):
        """发送参数到实验池"""
        if not pool_id:
            if self.debug:
                print("发送参数失败: 未指定实验池ID")
            return False
            
        with self.clients_lock:
            if pool_id not in self.clients:
                if self.debug:
                    print(f"发送参数失败: 实验池 {pool_id} 未连接")
                return False
            
            try:
                # 构造消息
                message = {
                    'type': 'params',
                    'data': params,
                    'timestamp': time.time()
                }
                
                # 发送消息
                self.clients[pool_id].send(json.dumps(message).encode('utf-8'))
                
                # 更新统计信息
                self.stats['messages_sent'] += 1
                
                # 更新本地实验池数据
                with self.pools_lock:
                    if pool_id in self.pools:
                        for key, value in params.items():
                            self.pools[pool_id][key] = value
                
                if self.debug:
                    print(f"已发送参数到实验池 {pool_id}: {params}")
                return True
            except ConnectionResetError:
                if self.debug:
                    print(f"发送参数到实验池 {pool_id} 时连接被重置")
                
                # 断开连接
                try:
                    self.clients[pool_id].close()
                except:
                    pass
                
                # 清理客户端连接
                del self.clients[pool_id]
                
                # 更新统计信息
                self.stats['active_connections'] = len(self.clients)
                self.stats['errors'] += 1
                
                # 更新实验池状态
                with self.pools_lock:
                    if pool_id in self.pools:
                        self.pools[pool_id]['connection_state'] = 'connection_reset'
                        del self.pools[pool_id]
                
                # 更新连接状态
                self.connection_states[pool_id] = "连接重置"
                
                # 发射信号
                self.pool_disconnected.emit(pool_id)
                
                # 发送实验池列表更新信号
                with self.pools_lock:
                    current_pools = list(self.pools.keys())
                self.pools_discovered.emit(current_pools)
                
                return False
            except Exception as e:
                if self.debug:
                    print(f"发送参数到实验池 {pool_id} 时出错: {e}")
                    import traceback
                    traceback.print_exc()
                
                # 更新统计信息
                self.stats['errors'] += 1
                self.error_occurred.emit(f"发送参数到实验池 {pool_id} 时出错: {e}")
                return False
    
    def send_command_to_pool(self, pool_id, command, params=None):
        """发送命令到实验池"""
        if not pool_id:
            if self.debug:
                print("发送命令失败: 未指定实验池ID")
            return False
            
        with self.clients_lock:
            if pool_id not in self.clients:
                if self.debug:
                    print(f"发送命令失败: 实验池 {pool_id} 未连接")
                return False
            
            try:
                # 构造消息
                message = {
                    'type': 'command',
                    'command': command,
                    'timestamp': time.time()
                }
                
                if params:
                    message['params'] = params
                
                # 发送消息
                self.clients[pool_id].send(json.dumps(message).encode('utf-8'))
                
                # 更新统计信息
                self.stats['messages_sent'] += 1
                
                if self.debug:
                    print(f"已发送命令到实验池 {pool_id}: {command}, 参数: {params}")
                return True
            except ConnectionResetError:
                if self.debug:
                    print(f"发送命令到实验池 {pool_id} 时连接被重置")
                
                # 断开连接
                try:
                    self.clients[pool_id].close()
                except:
                    pass
                
                # 清理客户端连接
                del self.clients[pool_id]
                
                # 更新统计信息
                self.stats['active_connections'] = len(self.clients)
                self.stats['errors'] += 1
                
                # 更新实验池状态
                with self.pools_lock:
                    if pool_id in self.pools:
                        self.pools[pool_id]['connection_state'] = 'connection_reset'
                        del self.pools[pool_id]
                
                # 更新连接状态
                self.connection_states[pool_id] = "连接重置"
                
                # 发射信号
                self.pool_disconnected.emit(pool_id)
                
                # 发送实验池列表更新信号
                with self.pools_lock:
                    current_pools = list(self.pools.keys())
                self.pools_discovered.emit(current_pools)
                
                return False
            except Exception as e:
                if self.debug:
                    print(f"发送命令到实验池 {pool_id} 时出错: {e}")
                    import traceback
                    traceback.print_exc()
                
                # 更新统计信息
                self.stats['errors'] += 1
                self.error_occurred.emit(f"发送命令到实验池 {pool_id} 时出错: {e}")
                return False
    
    def get_connected_pools(self):
        """获取连接的实验池"""
        with self.pools_lock:
            return self.pools.copy()
    
    def is_pool_connected(self, pool_id):
        """检查实验池是否连接"""
        with self.pools_lock:
            return pool_id in self.pools
    
    def get_connection_state(self, pool_id):
        """获取实验池的连接状态"""
        return self.connection_states.get(pool_id, "未知")
    
    def get_server_stats(self):
        """获取服务器统计信息"""
        return self.stats.copy()
    
    def get_pool_details(self, pool_id):
        """获取实验池详细信息"""
        with self.pools_lock:
            if pool_id in self.pools:
                # 复制一份数据，但移除视频帧（太大）
                pool_data = self.pools[pool_id].copy()
                if 'video_frame' in pool_data:
                    pool_data['video_frame'] = 'video_frame_data_object' if pool_data['video_frame'] is not None else None
                return pool_data
            return None
    
    def ping_pool(self, pool_id):
        """测试与实验池的连接"""
        if not pool_id:
            return False, "未指定实验池ID"
            
        with self.clients_lock:
            if pool_id not in self.clients:
                return False, f"实验池 {pool_id} 未连接"
            
            try:
                # 构造心跳消息
                ping_message = {
                    'type': 'ping',
                    'timestamp': time.time()
                }
                
                # 发送消息
                self.clients[pool_id].send(json.dumps(ping_message).encode('utf-8'))
                
                # 更新统计信息
                self.stats['messages_sent'] += 1
                
                return True, f"已向实验池 {pool_id} 发送Ping请求"
            except Exception as e:
                return False, f"Ping实验池 {pool_id} 时出错: {e}"
    
    def start_scanning(self):
        """启动扫描线程，自动发现网络中的实验池"""
        if self.scanning:
            return
        
        self.scanning = True
        
        # 启动扫描线程
        self.scan_thread = threading.Thread(target=self._scan_networks)
        self.scan_thread.daemon = True
        self.scan_thread.start()
        
        if self.debug:
            print("已启动网络扫描线程")

    def stop_scanning(self):
        """停止扫描线程"""
        self.scanning = False
        
        if self.debug:
            print("已停止网络扫描")

    def _scan_networks(self):
        """扫描网络中的实验池"""
        if self.debug:
            print("开始扫描网络中的实验池...")
        
        try:
            # 获取本机网络接口
            local_ip = self._get_local_ip()
            if not local_ip:
                if self.debug:
                    print("无法获取本机IP地址，无法扫描网络")
                return
            
            # 解析网络地址
            try:
                ip_parts = local_ip.split('.')
                network_prefix = '.'.join(ip_parts[:3])  # 例如 192.168.1
            except:
                if self.debug:
                    print(f"无法解析本机IP地址: {local_ip}")
                return
            
            # 扫描同一网段的所有地址
            while self.scanning:
                if self.debug:
                    print(f"扫描网络: {network_prefix}.0/24")
                
                # 扫描本地网络
                for i in range(1, 255):
                    if not self.scanning:
                        break
                    
                    ip_address = f"{network_prefix}.{i}"
                    
                    # 跳过本机地址
                    if ip_address == local_ip:
                        continue
                    
                    # 扫描该地址的所有监听端口
                    for port in self.ports:
                        self._scan_address(ip_address, port)
                    
                    # 短暂休眠以减少网络负载
                    time.sleep(0.01)
                
                # 发送发现的实验池列表
                with self.pools_lock:
                    discovered = list(self.discovered_pools.keys())
                
                self.pools_discovered.emit(discovered)
                
                # 每隔一段时间重新扫描
                time.sleep(60)  # 每分钟扫描一次
        except Exception as e:
            if self.debug:
                print(f"扫描网络时出错: {e}")
                import traceback
                traceback.print_exc()

    def _scan_address(self, ip_address, port):
        """扫描指定地址是否有实验池服务"""
        try:
            # 创建临时套接字
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.1)  # 设置超时时间为0.1秒
            
            # 尝试连接
            result = s.connect_ex((ip_address, port))
            s.close()
            
            if result == 0:
                # 端口开放，可能是实验池
                if self.debug:
                    print(f"发现可能的实验池: {ip_address}:{port}")
                
                # 记录发现的地址
                with self.pools_lock:
                    pool_id = f"{ip_address}:{port}"
                    self.discovered_pools[pool_id] = {
                        'address': ip_address,
                        'port': port,
                        'discovered_at': time.time(),
                        'status': 'discovered'
                    }
        except:
            pass

    def _get_local_ip(self):
        """获取本机IP地址"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(('8.8.8.8', 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return None

    def disconnect_pool(self, pool_id):
        """手动断开实验池连接
        
        此方法会彻底断开与实验池的连接，并确保不会自动重连，但保留实验池的"在线"状态
        """
        if not pool_id:
            if self.debug:
                print("断开连接失败: 未指定实验池ID")
            return False
            
        # 先发送断开命令
        disconnect_success = False
        try:
            with self.clients_lock:
                if pool_id in self.clients:
                    client_socket = self.clients[pool_id]
                    disconnect_msg = {
                        'type': 'command',
                        'command': 'disconnect',
                        'timestamp': time.time()
                    }
                    client_socket.send(json.dumps(disconnect_msg).encode('utf-8'))
                    disconnect_success = True
                    
                    # 关闭socket连接
                    try:
                        client_socket.shutdown(socket.SHUT_RDWR)
                    except:
                        pass
                    try:
                        client_socket.close()
                    except:
                        pass
                    
                    # 从客户端列表中移除
                    del self.clients[pool_id]
                    
                    # 更新统计信息
                    self.stats['active_connections'] = len(self.clients)
        except Exception as e:
            if self.debug:
                print(f"发送断开命令失败: {e}")
                traceback.print_exc()
            disconnect_success = False
        
        # 保存实验池信息用于保持在线状态
        pool_info = None
        with self.pools_lock:
            if pool_id in self.pools:
                # 保存重要信息如地址、端口等
                pool_info = {
                    'address': self.pools[pool_id].get('address'),
                    'port': self.ports[0],  # 使用当前监听端口
                    'discovered_at': time.time(),
                    'status': 'online',  # 标记为在线状态
                    'connection_state': 'disconnected'  # 但连接状态为已断开
                }
                # 从连接池列表中删除
                del self.pools[pool_id]
        
        # 更新discovered_pools列表，确保断开的实验池仍然显示为"在线"
        if pool_info and 'address' in pool_info:
            with self.pools_lock:
                self.discovered_pools[pool_id] = pool_info
        
        # 标记连接状态
        self.connection_states[pool_id] = "已断开但在线"
        
        # 发出断开信号
        self.pool_disconnected.emit(pool_id)
        
        # 更新池列表
        with self.pools_lock:
            current_pools = list(self.pools.keys())
        self.pools_discovered.emit(current_pools)
        
        print(f"已断开与实验池 {pool_id} 的连接，但保持在线状态")
        
        return disconnect_success

    def set_connection_params(self, host='localhost', port=None):
        """设置连接参数
        
        Args:
            host (str): 主机地址
            port (int): 端口号
            
        Returns:
            bool: 是否设置成功
        """
        # 如果服务器已经在运行，先停止
        was_running = self.running
        if was_running:
            self.stop()
        
        # 设置新的主机地址
        if host and host != '0.0.0.0':
            self.host = host
            
        # 设置新的端口
        if port:
            self.ports = [port]
            self.default_port = port
        
        # 如果之前在运行，则重新启动
        if was_running:
            return self.start()
        
        return True 