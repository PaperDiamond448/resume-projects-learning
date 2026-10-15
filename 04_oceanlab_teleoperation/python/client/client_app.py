#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
实验池端(Slave)入口点
"""

import sys
import os
import argparse
import cv2
import time
import socket
from PyQt5.QtWidgets import QApplication, QMessageBox
from PyQt5.QtGui import QFont

# 添加父目录到系统路径
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)

# 导入项目模块
from remote.client import Client
from remote.auth import AuthManager
from client.slave_window import SlaveWindow
from client.slave_setup_dialog import SlaveSetupDialog
from client.device_manager import DeviceManager

class ClientApp:
    """实验池端应用程序"""
    
    def __init__(self, server_address, port, pool_id, debug=False, headless=False):
        """初始化客户端应用
        
        Args:
            server_address (str): 服务器地址
            port (int): 服务器端口
            pool_id (str): 实验池ID
            debug (bool): 是否启用调试模式
            headless (bool): 是否使用无界面模式（用于自动化测试）
        """
        self.server_address = server_address
        self.port = port
        self.pool_id = pool_id
        self.debug = debug
        self.headless = headless
        
        # 创建认证管理器
        self.auth_manager = AuthManager()
        
        # 创建客户端连接
        self.client = Client(
            server_address=server_address,
            port=port,
            pool_id=pool_id,
            auth_manager=self.auth_manager,
            debug=debug
        )
        
        # 初始化摄像头
        self.camera = None
        
        # 初始化设备管理器
        self.device_manager = DeviceManager(debug=debug)
        
        self.running = False
    
    def start(self):
        """启动客户端应用"""
        print(f"正在连接到服务器 {self.server_address}:{self.port}...")
        
        # 连接到服务器
        if not self.client.connect():
            print("无法连接到服务器，请检查网络连接和服务器状态")
            return False
        
        # 初始化摄像头（只在非headless模式下初始化）
        if not self.headless:
            self.init_camera()
        
        # 启动设备管理器
        self.device_manager.start()
        print("设备管理器已启动")
        
        # 启动主循环
        self.running = True
        self.main_loop()
        
        return True
    
    def init_camera(self):
        """初始化摄像头"""
        try:
            self.camera = cv2.VideoCapture(0)
            if not self.camera.isOpened():
                print("警告：无法打开摄像头，视频流将不可用")
                self.camera = None
            else:
                print("摄像头已初始化")
                
                # 设置较低的分辨率以提高传输效率
                self.camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                self.camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        except Exception as e:
            print(f"初始化摄像头时出错: {e}")
            self.camera = None
    
    def main_loop(self):
        """主循环"""
        try:
            while self.running:
                # 处理来自服务器的命令
                self.client.process_commands()
                
                # 发送摄像头图像（只在非headless模式下发送）
                if self.camera and not self.headless:
                    ret, frame = self.camera.read()
                    if ret:
                        self.client.send_video_frame(frame)
                
                # 发送设备状态
                self.send_status()
                
                # 短暂休眠以减少CPU使用率
                time.sleep(0.03)  # 约30FPS
        except KeyboardInterrupt:
            print("接收到中断信号，正在关闭...")
        except Exception as e:
            print(f"运行时错误: {e}")
        finally:
            self.stop()
    
    def send_status(self):
        """发送状态到服务器"""
        # 不再发送基本状态，避免参数被重置
        # self.client.send_status()  # 注释掉这行，避免参数被重置
        
        # 发送设备状态
        device_status = self.device_manager.get_all_device_status()
        
        # 发送所有可用设备信息
        available_devices = self.device_manager.get_all_available_devices()
        
            # 将设备状态添加到客户端状态中
        self.client.send_data_command("device_status", {
            "devices": available_devices
            })
    
    def stop(self):
        """停止客户端应用"""
        self.running = False
        
        # 关闭摄像头
        if self.camera:
            self.camera.release()
            self.camera = None
        
        # 停止设备管理器
        if self.device_manager:
            self.device_manager.stop()
            print("设备管理器已停止")
        
        # 断开与服务器的连接
        self.client.disconnect()
        print("客户端已关闭")

def parse_arguments():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='OceanLab-Python 实验池端')
    parser.add_argument('--server', type=str, default='localhost', help='服务器地址')
    parser.add_argument('--port', type=int, default=8000, help='服务器端口')
    parser.add_argument('--pool-id', type=str, required=False, help='实验池ID')
    parser.add_argument('--debug', action='store_true', help='启用调试模式')
    parser.add_argument('--console', action='store_true', help='使用控制台模式（无GUI）')
    parser.add_argument('--gui', action='store_true', help='强制使用GUI模式')
    parser.add_argument('--headless', action='store_true', help='无界面模式（用于自动化测试）')
    return parser.parse_args()

def get_local_ip():
    """获取本机IP地址，用于显示连接信息"""
    try:
        # 创建一个临时socket连接来确定本机IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        local_ip = s.getsockname()[0]
        s.close()
        return local_ip
    except:
        return "未知"

def main():
    """主函数"""
    args = parse_arguments()
    
    # 创建Qt应用，无论是否使用GUI模式都需要
    app = QApplication(sys.argv)
    app.setApplicationName("OceanLab-Python 实验池端")
    
    # 设置全局字体以支持中文
    font = QFont("Microsoft YaHei", 9)  # 微软雅黑
    app.setFont(font)
    
    # 处理headless模式（优先级最高，用于自动化测试）
    if args.headless:
        # 确认是否有必要的参数
        server_address = args.server
        port = args.port
        pool_id = args.pool_id
        
        # 如果缺少必要参数，提示错误并退出
        if not pool_id:
            print("错误: 无界面模式需要指定 --pool-id 参数")
            return 1
        
        debug = args.debug
        
        # 无界面模式，使用ClientApp类的headless模式
        client_app = ClientApp(
            server_address=server_address,
            port=port,
            pool_id=pool_id,
            debug=debug,
            headless=True
        )
        return 0 if client_app.start() else 1
    # 控制台模式次之
    elif args.console:
        # 使用命令行参数，确认是否有必要的参数
        server_address = args.server
        port = args.port
        pool_id = args.pool_id
        
        # 如果缺少必要参数，提示错误并退出
        if not pool_id:
            print("错误: 控制台模式需要指定 --pool-id 参数")
            return 1
        
        debug = args.debug
        
        # 控制台模式，使用原有的ClientApp类
        client_app = ClientApp(
            server_address=server_address,
            port=port,
            pool_id=pool_id,
            debug=debug
        )
        return 0 if client_app.start() else 1
    else:
        # GUI模式，显示设置对话框
        params = SlaveSetupDialog.get_slave_setup_params()
        if not params:
            print("用户取消了设置，程序退出")
            return 0
        
        # 使用对话框的配置
        server_address = params["server_address"]
        port = params["server_port"]
        pool_id = params["pool_id"]
        debug = params["debug_mode"]
        
        # 显示连接信息
        local_ip = get_local_ip()
        print(f"本机IP地址: {local_ip}")
        print(f"实验池ID: {pool_id}")
        print(f"服务器地址: {server_address}:{port}")
        
        # 创建认证管理器
        auth_manager = AuthManager()
        
        # 创建客户端连接
        client = Client(
            server_address=server_address,
            port=port,
            pool_id=pool_id,
            auth_manager=auth_manager,
            debug=debug
        )
        
        # 创建主窗口并自动连接
        main_window = SlaveWindow(client, pool_id, debug=debug, auto_connect=True)
        main_window.show()
        
        # 显示连接信息
        main_window.statusBar().showMessage(f"实验池ID: {pool_id} | 服务器: {server_address}:{port} | 本机IP: {local_ip}")
        
        # 初始化摄像头
        main_window.init_camera()
        
        # 运行应用
        return app.exec_()

if __name__ == "__main__":
    sys.exit(main()) 