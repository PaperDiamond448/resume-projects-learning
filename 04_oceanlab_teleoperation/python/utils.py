#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
工具函数和类
"""

import socket
import time
import json
import sys
import os
from pathlib import Path

class NetworkDiagnostics:
    """网络连接诊断工具"""
    
    @staticmethod
    def check_port_open(host, port, timeout=2):
        """检查主机上的端口是否开放"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(timeout)
            result = s.connect_ex((host, port))
            s.close()
            return result == 0
        except Exception as e:
            print(f"检查端口时出错: {e}")
            return False
    
    @staticmethod
    def get_local_ip():
        """获取本机IP地址"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(('8.8.8.8', 80))
            local_ip = s.getsockname()[0]
            s.close()
            return local_ip
        except:
            return "127.0.0.1"
    
    @staticmethod
    def ping_server(host, count=4):
        """Ping 主机以测试连接"""
        import subprocess
        import platform
        
        try:
            # 根据操作系统确定ping命令格式
            if platform.system().lower() == "windows":
                command = ["ping", "-n", str(count), host]
            else:
                command = ["ping", "-c", str(count), host]
            
            result = subprocess.run(command, capture_output=True, text=True)
            return result.returncode == 0, result.stdout
        except Exception as e:
            return False, f"Ping测试出错: {e}"
    
    @staticmethod
    def diagnose_connection(host, port, detailed=False):
        """诊断连接问题并返回结果"""
        results = {}
        
        # 获取本机IP
        local_ip = NetworkDiagnostics.get_local_ip()
        results["local_ip"] = local_ip
        
        # 测试DNS解析
        try:
            ip_address = socket.gethostbyname(host)
            results["dns_resolution"] = {
                "success": True,
                "ip_address": ip_address
            }
        except socket.gaierror:
            results["dns_resolution"] = {
                "success": False,
                "error": "无法解析主机名"
            }
            ip_address = host  # 如果是IP，则直接使用
        
        # 检查端口
        port_open = NetworkDiagnostics.check_port_open(host, port)
        results["port_check"] = {
            "success": port_open,
            "message": f"端口 {port} {'开放' if port_open else '未开放或被阻止'}"
        }
        
        # 如果需要详细诊断，进行ping测试
        if detailed:
            ping_success, ping_output = NetworkDiagnostics.ping_server(host)
            results["ping_test"] = {
                "success": ping_success,
                "output": ping_output
            }
        
        # 测试本地环回
        if host in ['localhost', '127.0.0.1'] or host == local_ip:
            loopback_open = NetworkDiagnostics.check_port_open('127.0.0.1', port)
            results["loopback_check"] = {
                "success": loopback_open,
                "message": f"本地环回端口 {port} {'开放' if loopback_open else '未开放或被阻止'}"
            }
        
        # 整体连接状态
        results["overall_status"] = "可连接" if port_open else "不可连接"
        
        return results

# 测试代码
if __name__ == "__main__":
    if len(sys.argv) >= 3:
        host = sys.argv[1]
        port = int(sys.argv[2])
        detailed = len(sys.argv) > 3 and sys.argv[3].lower() == "detailed"
        
        print(f"正在诊断到 {host}:{port} 的连接...")
        results = NetworkDiagnostics.diagnose_connection(host, port, detailed)
        print(json.dumps(results, indent=2, ensure_ascii=False))
    else:
        print("使用方法: python utils.py <主机> <端口> [detailed]") 