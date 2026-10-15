#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
认证管理模块
"""

import os
import json
import hashlib
import time
import requests

class AuthManager:
    """认证管理器"""
    
    def __init__(self, config_file=None):
        """初始化认证管理器"""
        self.config_file = config_file or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'auth_config.json')
        
        # 默认配置
        self.config = {
            'master_key': 'demo-master-change-me',  # 默认主密钥
            'debug_key': 'demo-debug-change-me',   # 调试密钥，用于连接实验池1
            'valid_pools': ['pool1', 'pool2', 'pool3', '1'],  # 默认有效的实验池ID
            'session_timeout': 3600,  # 会话超时时间（秒）
            'reservation_api_url': 'https://localhost:3002/api/verify_key'  # 预约系统API URL
        }
        
        # 实验池端口映射
        self.pool_ports = {
            'pool1': [8887, 8888],
            'pool2': [8885, 8886],
            'pool3': [8883, 8884],
            # 添加数字ID映射到相同的端口
            '1': [8887, 8888],
            '2': [8885, 8886],
            '3': [8883, 8884]
        }
        
        # 当前会话
        self.current_session = {
            'authenticated': False,
            'timestamp': 0,
            'reserved_pool_id': None,  # 预约的实验池ID
            'selected_port': None,     # 选择的端口
            'selected_ip': 'localhost' # 选择的IP地址
        }
        
        # 加载配置
        self.load_config()
    
    def load_config(self):
        """加载配置"""
        try:
            if os.path.exists(self.config_file):
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    config = json.load(f)
                    
                    # 更新配置
                    if 'master_key' in config:
                        self.config['master_key'] = config['master_key']
                    
                    if 'debug_key' in config:
                        self.config['debug_key'] = config['debug_key']
                    
                    if 'valid_pools' in config:
                        self.config['valid_pools'] = config['valid_pools']
                    
                    if 'session_timeout' in config:
                        self.config['session_timeout'] = config['session_timeout']
                    
                    if 'reservation_api_url' in config:
                        self.config['reservation_api_url'] = config['reservation_api_url']
        except Exception as e:
            print(f"加载认证配置时出错: {e}")
    
    def save_config(self):
        """保存配置"""
        try:
            # 确保目录存在
            os.makedirs(os.path.dirname(self.config_file), exist_ok=True)
            
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(self.config, f, indent=4)
        except Exception as e:
            print(f"保存认证配置时出错: {e}")
    
    def validate_key(self, key):
        """验证密钥"""
        if not key:
            return False
        
        # 先验证是否为主密钥
        if key == self.config['master_key']:
            # 更新会话
            self.current_session['authenticated'] = True
            self.current_session['timestamp'] = time.time()
            self.current_session['reserved_pool_id'] = None  # 使用主密钥时不绑定特定实验池
            return True
            
        # 验证是否为调试密钥
        if key == self.config['debug_key']:
            # 更新会话
            self.current_session['authenticated'] = True
            self.current_session['timestamp'] = time.time()
            self.current_session['reserved_pool_id'] = 'pool1'  # 调试密钥绑定到实验池1
            return True
        
        # 尝试验证预约系统密钥
        return self.validate_reservation_key(key)
    
    def validate_reservation_key(self, key):
        """验证预约系统密钥"""
        try:
            # 构建请求数据
            data = {'key': key}
            
            print(f"正在验证密钥: {key}, 请求URL: {self.config['reservation_api_url']}")
            
            # 发送请求到预约系统API，设置更长的超时时间（考虑到ngrok可能延迟较高）
            response = requests.post(self.config['reservation_api_url'], json=data, timeout=10, 
                                    headers={'Content-Type': 'application/json'})
            
            print(f"API响应状态码: {response.status_code}")
            
            # 检查响应状态
            if response.status_code == 200:
                # 解析响应数据
                result = response.json()
                print(f"API返回数据: {result}")
                
                if result.get('valid', False):
                    # 获取预约的实验池ID
                    equipment_id = result.get('equipment_id')
                    
                    if equipment_id:
                        # 将实验池ID转换为系统中的格式
                        # 处理整数或字符串格式
                        pool_id = str(equipment_id)
                        if not pool_id.startswith('pool'):
                            pool_id = f"pool{pool_id}"
                        
                        print(f"解析的实验池ID: {pool_id}")
                        
                        # 如果不在已知的实验池列表中，加入1、2、3
                        if pool_id not in self.config['valid_pools']:
                            # 如果是数字ID，自动添加
                            numeric_id = ''.join(filter(str.isdigit, pool_id))
                            if numeric_id and int(numeric_id) in [1, 2, 3]:
                                self.config['valid_pools'].append(pool_id)
                                print(f"自动添加实验池ID: {pool_id}")
                        
                        # 检查实验池ID是否有效
                        if self.validate_pool(pool_id):
                            # 更新会话
                            self.current_session['authenticated'] = True
                            self.current_session['timestamp'] = time.time()
                            self.current_session['reserved_pool_id'] = pool_id
                            
                            print(f"验证成功，预约实验池ID: {pool_id}")
                            return True
                        else:
                            print(f"实验池ID无效: {pool_id}")
                    else:
                        print("API响应中缺少equipment_id")
                else:
                    print(f"密钥验证失败，原因: {result.get('message', '未知原因')}")
            else:
                print(f"API请求失败，状态码: {response.status_code}")
                try:
                    error_text = response.text
                    print(f"错误详情: {error_text}")
                except:
                    pass
            
            # 如果是调试模式，尝试使用调试密钥
            if key and len(key) == 8 and key.isupper() and any(c.isdigit() for c in key):
                print("密钥格式有效，但验证失败。尝试使用调试模式处理...")
                # 根据密钥的第一个字符确定实验池ID (A-I: 1, J-R: 2, S-Z: 3)
                first_char = key[0]
                if 'A' <= first_char <= 'I':
                    pool_id = 'pool1'
                elif 'J' <= first_char <= 'R':
                    pool_id = 'pool2'
                else:
                    pool_id = 'pool3'
                
                print(f"调试模式: 根据密钥首字母'{first_char}'分配到实验池 {pool_id}")
                
                # 更新会话
                self.current_session['authenticated'] = True
                self.current_session['timestamp'] = time.time()
                self.current_session['reserved_pool_id'] = pool_id
                
                return True
            
            return False
        except requests.exceptions.Timeout:
            print("验证预约系统密钥超时，请检查预约系统是否可用")
            print(f"尝试连接的URL: {self.config['reservation_api_url']}")
            return False
        except requests.exceptions.ConnectionError as e:
            print(f"连接预约系统失败: {e}")
            print("可能是ngrok连接问题，请检查ngrok是否正常运行")
            return False
        except Exception as e:
            print(f"验证预约系统密钥时出错: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def validate_pool(self, pool_id):
        """验证实验池ID"""
        if not pool_id:
            return False
        
        # 检查实验池ID是否有效
        return pool_id in self.config['valid_pools'] or pool_id in self.pool_ports
    
    def is_authenticated(self):
        """检查是否已认证"""
        # 检查会话是否过期
        if self.current_session['authenticated']:
            elapsed = time.time() - self.current_session['timestamp']
            if elapsed > self.config['session_timeout']:
                # 会话已过期
                self.current_session['authenticated'] = False
                self.current_session['reserved_pool_id'] = None
                return False
            
            # 更新时间戳
            self.current_session['timestamp'] = time.time()
            return True
        
        return False
    
    def get_reserved_pool_id(self):
        """获取预约的实验池ID"""
        if self.is_authenticated() and self.current_session['reserved_pool_id']:
            return self.current_session['reserved_pool_id']
        return None
    
    def logout(self):
        """注销当前会话"""
        self.current_session['authenticated'] = False
        self.current_session['timestamp'] = 0
        self.current_session['reserved_pool_id'] = None
    
    def add_valid_pool(self, pool_id):
        """添加有效的实验池ID"""
        if pool_id and pool_id not in self.config['valid_pools']:
            self.config['valid_pools'].append(pool_id)
            self.save_config()
            return True
        return False
    
    def remove_valid_pool(self, pool_id):
        """移除有效的实验池ID"""
        if pool_id in self.config['valid_pools']:
            self.config['valid_pools'].remove(pool_id)
            self.save_config()
            return True
        return False
    
    def change_master_key(self, old_key, new_key):
        """更改主密钥"""
        if not self.validate_key(old_key) or not new_key:
            return False
        
        self.config['master_key'] = new_key
        self.save_config()
        
        # 更新会话
        self.current_session['authenticated'] = True
        self.current_session['timestamp'] = time.time()
        
        return True
    
    def get_valid_pools(self):
        """获取有效的实验池ID列表"""
        return self.config['valid_pools'].copy()
    
    def get_available_ports(self):
        """获取当前可用的端口列表"""
        # 如果使用主密钥登录，可以使用所有端口
        if self.is_authenticated() and not self.current_session['reserved_pool_id']:
            all_ports = []
            for ports in self.pool_ports.values():
                all_ports.extend(ports)
            return all_ports
            
        # 获取预约的实验池ID
        pool_id = self.get_reserved_pool_id()
        if not pool_id:
            return []
        
        # 检查是否在端口映射中
        if pool_id in self.pool_ports:
            return self.pool_ports[pool_id]
        
        # 尝试转换数字ID (如果pool_id是"pool1"，尝试使用"1"查找)
        if pool_id.startswith('pool'):
            numeric_id = pool_id[4:]
            if numeric_id in self.pool_ports:
                return self.pool_ports[numeric_id]
        
        # 反过来尝试 (如果pool_id是"1"，尝试使用"pool1"查找)
        else:
            pool_with_prefix = f"pool{pool_id}"
            if pool_with_prefix in self.pool_ports:
                return self.pool_ports[pool_with_prefix]
        
        return []
        
    def set_connection_info(self, port, ip='localhost'):
        """设置连接信息"""
        if not self.is_authenticated():
            return False
            
        # 检查端口是否可用
        available_ports = self.get_available_ports()
        if port not in available_ports:
            print(f"端口 {port} 不在可用端口列表中: {available_ports}")
            return False
            
        # 更新连接信息
        self.current_session['selected_port'] = port
        self.current_session['selected_ip'] = ip or 'localhost'
        return True
    
    def get_connection_info(self):
        """获取当前连接信息"""
        if not self.is_authenticated():
            return None, None
        
        return self.current_session['selected_port'], self.current_session['selected_ip'] 