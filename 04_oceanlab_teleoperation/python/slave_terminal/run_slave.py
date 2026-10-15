#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
OceanLab 从端应用启动脚本
启动图形化设置对话框，让用户选择实验池和连接参数
"""

import sys
import os
import argparse
from PyQt5.QtWidgets import QApplication

# 添加父目录到系统路径
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.append(parent_dir)

from client.slave_setup_dialog import SlaveSetupDialog
from slave_app import SlaveMainWindow
from client.client import Client

def main():
    """主函数"""
    app = QApplication(sys.argv)
    
    # 显示设置对话框
    params = SlaveSetupDialog.get_slave_setup_params()
    
    if not params:
        print("用户取消了设置")
        return 1
    
    # 创建客户端
    client = Client(
        server_address=params["server_address"],
        port=params["server_port"],
        pool_id=params["pool_id"],
        debug=params.get("debug_mode", False)
    )
    
    # 创建主窗口
    window = SlaveMainWindow(
        client=client,
        pool_id=params["pool_id"],
        debug=params.get("debug_mode", False)
    )
    
    # 显示窗口
    window.show()
    
    # 运行应用
    return app.exec_()

if __name__ == "__main__":
    sys.exit(main()) 