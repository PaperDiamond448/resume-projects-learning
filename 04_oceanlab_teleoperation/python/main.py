#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
主控终端(Master)入口点
"""

import sys
import os
import argparse
from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QFont

# 添加当前目录到系统路径
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from gui.main_window import MainWindow
from remote.server import Server
from remote.auth import AuthManager

def parse_arguments():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='OceanLab-Python 主控终端')
    parser.add_argument('--port', type=int, default=8000, help='服务器主端口')
    parser.add_argument('--additional-ports', type=str, default="8887,8888", help='额外监听的端口，用逗号分隔，例如 "8001,8002,8003"')
    parser.add_argument('--debug', action='store_true', help='启用调试模式')
    return parser.parse_args()

def main():
    """主函数"""
    print("正在启动OceanLab-Python主控终端...")
    args = parse_arguments()
    
    # 创建Qt应用
    print("正在创建Qt应用...")
    app = QApplication(sys.argv)
    app.setApplicationName("OceanLab-Python")
    
    # 设置全局字体以支持中文
    font = QFont("Microsoft YaHei", 9)  # 使用微软雅黑字体
    app.setFont(font)
    
    # 创建认证管理器
    print("正在创建认证管理器...")
    auth_manager = AuthManager()
    
    # 处理端口列表
    ports = [args.port]  # 主端口
    if args.additional_ports:
        try:
            additional_ports = [int(p.strip()) for p in args.additional_ports.split(',')]
            ports.extend(additional_ports)
            print(f"将监听以下端口: {ports}")
        except ValueError:
            print(f"警告: 无法解析额外端口 '{args.additional_ports}'，将只监听主端口 {args.port}")
    
    # 创建服务器
    print("正在创建服务器...")
    server = Server(port=ports, auth_manager=auth_manager, debug=args.debug)
    
    # 创建主窗口
    print("正在创建主窗口...")
    main_window = MainWindow(server=server, auth_manager=auth_manager, debug=args.debug)
    main_window.show()
    print("主窗口已显示")
    
    # 启动服务器
    print("正在启动服务器...")
    if server.start():
        print("服务器已成功启动")
    else:
        print("服务器启动失败，请检查端口是否被占用")
    
    # 运行应用
    print("正在启动事件循环...")
    return app.exec_()

if __name__ == "__main__":
    sys.exit(main()) 