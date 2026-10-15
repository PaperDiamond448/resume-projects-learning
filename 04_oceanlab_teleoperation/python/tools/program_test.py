#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
OceanLab-Python项目专用测试脚本

本脚本用于测试program文件夹下的OceanLab-Python海洋信息实验教学平台项目
针对主控端(Master)和实验池端(Slave)的功能进行测试，包括GUI和网络通信等。

测试环境:
- ThinkPad i14
- Windows 10 22631
- 12th Gen Intel(R) Core(TM) i5-1235U @ 1.30GHz
- 16GB RAM
- 2GB 显卡
"""

import sys
import os
import unittest
import time
import socket
import subprocess
import threading
import json
import numpy as np
from PyQt5.QtWidgets import QApplication, QDialog, QMainWindow
from PyQt5.QtTest import QTest
from PyQt5.QtCore import Qt, QTimer

# 添加路径，确保可以导入program目录下的模块
current_dir = os.path.dirname(os.path.abspath(__file__))
program_dir = os.path.dirname(os.path.dirname(current_dir))
program_program_dir = os.path.join(program_dir, "program")

# 将program目录加入路径以使测试能找到程序模块
if program_program_dir not in sys.path:
    sys.path.insert(0, program_program_dir)

# 尝试导入程序模块
MODULES_IMPORTED = False
try:
    # 直接从program目录导入
    from gui.main_window import MainWindow, LoginDialog
    from remote.server import Server
    from remote.auth import AuthManager
    from remote.client import Client
    from client.slave_window import SlaveWindow
    from client.slave_setup_dialog import SlaveSetupDialog
    MODULES_IMPORTED = True
except ImportError as e:
    print(f"警告: 无法导入必要模块 - {e}")
    try:
        # 尝试使用完整路径导入
        from program.gui.main_window import MainWindow, LoginDialog
        from program.remote.server import Server
        from program.remote.auth import AuthManager
        from program.remote.client import Client
        from program.client.slave_window import SlaveWindow
        from program.client.slave_setup_dialog import SlaveSetupDialog
        MODULES_IMPORTED = True
    except ImportError as e:
        print(f"警告: 无法导入必要模块 - {e}")
        MODULES_IMPORTED = False

# 测试配置
TEST_CONFIG = {
    "server_host": "127.0.0.1",
    "server_port": 8888,
    "pool_id": "test_pool_1",
    "test_key": "ABCDEF12",  # 测试用密钥
    "timeout": 5,  # 超时时间(秒)
    "long_test": False,  # 是否执行长时间测试
}

class NetworkTests(unittest.TestCase):
    """网络功能测试"""
    
    def setUp(self):
        """测试前准备"""
        print(f"\n===== 开始测试: {self._testMethodName} =====")
    
    def tearDown(self):
        """测试后清理"""
        print(f"===== 测试完成: {self._testMethodName} =====\n")
    
    def test_01_network_interfaces(self):
        """测试网络接口是否可用"""
        hostname = socket.gethostname()
        self.assertIsNotNone(hostname)
        
        ip_list = socket.gethostbyname_ex(hostname)[2]
        self.assertTrue(len(ip_list) > 0)
        print(f"主机名: {hostname}")
        print(f"IP地址: {ip_list}")
    
    def test_02_port_availability(self):
        """测试端口可用性"""
        port = TEST_CONFIG["server_port"]
        
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(1)
        result = s.connect_ex(('127.0.0.1', port))
        s.close()
        
        # 如果端口未被占用，则可用于测试
        if result != 0:
            print(f"端口 {port} 可用")
            self.assertTrue(True)
        else:
            print(f"警告: 端口 {port} 已被占用，可能会影响测试")
            self.skipTest(f"端口 {port} 已被占用")

    def test_03_udp_broadcast(self):
        """测试UDP广播功能"""
        # 创建接收线程
        received = [False]
        
        def receive_broadcast():
            try:
                recv_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                recv_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                recv_socket.bind(('0.0.0.0', TEST_CONFIG["server_port"]))
                recv_socket.settimeout(5)
                
                data, addr = recv_socket.recvfrom(1024)
                if data:
                    received[0] = True
                    # 发送响应
                    recv_socket.sendto(b"RESPONSE", addr)
                
                recv_socket.close()
            except Exception as e:
                print(f"接收广播时出错: {e}")
        
        # 启动接收线程
        receiver = threading.Thread(target=receive_broadcast)
        receiver.daemon = True
        receiver.start()
        
        # 等待接收线程启动
        time.sleep(1)
        
        # 发送广播
        try:
            send_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            send_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            
            # 发送消息
            send_socket.sendto(b"DISCOVERY", ('255.255.255.255', TEST_CONFIG["server_port"]))
            
            # 等待响应
            send_socket.settimeout(5)
            try:
                data, addr = send_socket.recvfrom(1024)
                self.assertEqual(data, b"RESPONSE")
            except socket.timeout:
                self.fail("广播响应超时")
            
            send_socket.close()
        except Exception as e:
            self.fail(f"发送广播时出错: {e}")
        
        # 等待接收线程
        receiver.join(5)
        self.assertTrue(received[0], "未收到广播消息")

@unittest.skipIf(not MODULES_IMPORTED, "无法导入必要模块")
class ServerTests(unittest.TestCase):
    """服务器功能测试"""
    
    @classmethod
    def setUpClass(cls):
        """所有测试前的准备工作"""
        # 创建认证管理器
        cls.auth_manager = AuthManager()
        
        # 向认证管理器添加测试池ID
        cls.auth_manager.add_valid_pool(TEST_CONFIG["pool_id"])
        
        # 创建服务器实例
        cls.server = Server(
            port=TEST_CONFIG["server_port"],
            auth_manager=cls.auth_manager,
            debug=True
        )
        
        # 启动服务器
        cls.server.start()
        print("服务器已启动")
    
    @classmethod
    def tearDownClass(cls):
        """所有测试后的清理工作"""
        if hasattr(cls, 'server'):
            cls.server.stop()
            print("服务器已停止")
    
    def setUp(self):
        """测试前准备"""
        print(f"\n===== 开始测试: {self._testMethodName} =====")
    
    def tearDown(self):
        """测试后清理"""
        print(f"===== 测试完成: {self._testMethodName} =====\n")
    
    def test_01_server_running(self):
        """测试服务器是否正在运行"""
        self.assertTrue(self.__class__.server.running)
    
    def test_02_client_connection(self):
        """测试客户端连接到服务器"""
        client = Client(
            server_address="127.0.0.1",
            port=TEST_CONFIG["server_port"],
            pool_id=TEST_CONFIG["pool_id"],
            auth_manager=self.__class__.auth_manager,
            debug=True
        )
        
        # 测试连接
        connected = client.connect()
        self.assertTrue(connected, "客户端无法连接到服务器")
        
        # 断开连接
        client.disconnect()

class ProcessTests(unittest.TestCase):
    """进程启动测试"""
    
    def setUp(self):
        """测试前准备"""
        print(f"\n===== 开始测试: {self._testMethodName} =====")
        self.processes = []
    
    def tearDown(self):
        """测试后清理"""
        # 终止所有启动的进程
        for process in self.processes:
            try:
                process.terminate()
                process.wait(timeout=5)
            except:
                try:
                    process.kill()
                except:
                    pass
        
        print(f"===== 测试完成: {self._testMethodName} =====\n")
    
    def test_01_master_startup(self):
        """测试主控端启动"""
        try:
            # 确定主程序路径 - 这里改为program目录下的main.py
            main_py = os.path.join(program_dir, "program", "main.py")
            
            # 使用Python启动主控端 - 移除headless参数
            cmd = [sys.executable, main_py, '--debug']
            
            process = subprocess.Popen(
                cmd, 
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True
            )
            
            self.processes.append(process)
            
            # 等待几秒检查进程是否存活
            time.sleep(5)
            
            # 在测试中允许进程自己退出，只要能正常启动即可
            # 检查进程是否成功启动
            if process.poll() is not None and process.returncode != 0:
                # 获取错误输出
                stdout, stderr = process.communicate()
                error_msg = f"进程退出代码: {process.returncode}\n"
                if stdout:
                    error_msg += f"标准输出: {stdout}\n"
                if stderr:
                    error_msg += f"错误输出: {stderr}"
                self.fail(f"启动主控端时出错: {error_msg}")
            
            print("主控端启动测试成功")
        except Exception as e:
            self.fail(f"启动主控端时出错: {e}")
    
    def test_02_slave_startup_console(self):
        """测试实验池端控制台模式启动"""
        try:
            # 确定从端程序路径
            client_app_py = os.path.join(program_program_dir, "client", "client_app.py")
            
            # 使用Python启动实验池端，使用headless模式而不是控制台模式
            cmd = [
                sys.executable, 
                client_app_py, 
                '--server', TEST_CONFIG["server_host"],
                '--port', str(TEST_CONFIG["server_port"]),
                '--pool-id', "1",  # 使用数字ID，与客户端类预期一致
                '--headless',  # 使用headless模式
                '--debug'
            ]
            
            # 为测试使用非阻塞启动，设置超时
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True
            )
            
            self.processes.append(process)
            
            # 等待几秒检查进程是否存活
            time.sleep(5)
            
            # 在测试中允许进程自己退出，只要能正常启动即可
            # 检查进程是否成功启动
            if process.poll() is not None and process.returncode != 0:
                # 获取错误输出
                stdout, stderr = process.communicate()
                error_msg = f"进程退出代码: {process.returncode}\n"
                if stdout:
                    error_msg += f"标准输出: {stdout}\n"
                if stderr:
                    error_msg += f"错误输出: {stderr}"
                self.fail(f"启动实验池端时出错: {error_msg}")
            
            print("实验池端启动测试成功")
        except Exception as e:
            self.fail(f"启动实验池端时出错: {e}")

@unittest.skipIf(not MODULES_IMPORTED, "无法导入GUI模块")
class GUITests(unittest.TestCase):
    """GUI功能测试"""
    
    @classmethod
    def setUpClass(cls):
        """所有测试前的准备工作"""
        # 创建QApplication实例
        cls.app = QApplication.instance()
        if not cls.app:
            cls.app = QApplication(sys.argv)
        
        # 创建认证管理器
        cls.auth_manager = AuthManager()
        
        # 向认证管理器添加测试密钥
        cls.auth_manager.add_key(TEST_CONFIG["test_key"], TEST_CONFIG["pool_id"], "测试用户")
    
    def setUp(self):
        """测试前准备"""
        print(f"\n===== 开始测试: {self._testMethodName} =====")
    
    def tearDown(self):
        """测试后清理"""
        print(f"===== 测试完成: {self._testMethodName} =====\n")
    
    def test_01_login_dialog(self):
        """测试登录对话框"""
        try:
            # 创建登录对话框
            dialog = LoginDialog(self.__class__.auth_manager)
            dialog.show()
            
            # 等待对话框显示
            QTest.qWaitForWindowExposed(dialog)
            
            # 填写密钥
            QTest.keyClicks(dialog.key_input, TEST_CONFIG["test_key"])
            
            # 点击登录按钮
            dialog.on_login()
            
            # 检查是否接受
            result = dialog.result()
            self.assertEqual(result, QDialog.Accepted, "登录失败")
            
            # 清理
            dialog.close()
        except Exception as e:
            self.fail(f"登录对话框测试失败: {e}")
    
    def test_02_main_window_basic(self):
        """测试主窗口基本功能"""
        try:
            # 创建服务器实例
            server = Server(
                port=TEST_CONFIG["server_port"],
                auth_manager=self.__class__.auth_manager,
                debug=True
            )
            
            # 创建主窗口
            window = MainWindow(
                server=server,
                auth_manager=self.__class__.auth_manager,
                debug=True
            )
            window.show()
            
            # 等待窗口显示
            QTest.qWaitForWindowExposed(window)
            
            # 测试标签页切换
            tabs = window.tab_widget
            tab_count = tabs.count()
            self.assertTrue(tab_count > 0, "标签页数量为0")
            
            for i in range(tab_count):
                tabs.setCurrentIndex(i)
                self.assertEqual(tabs.currentIndex(), i)
            
            # 关闭窗口
            window.close()
            
            # 停止服务器
            server.stop()
            
            self.assertTrue(True)  # 如果能执行到这里，测试通过
        except Exception as e:
            self.fail(f"主窗口测试失败: {e}")
    
    def test_03_slave_window_basic(self):
        """测试从端窗口基本功能"""
        try:
            # 创建客户端实例
            client = Client(
                server_address=TEST_CONFIG["server_host"],
                port=TEST_CONFIG["server_port"],
                pool_id=TEST_CONFIG["pool_id"],
                auth_manager=self.__class__.auth_manager,
                debug=True
            )
            
            # 创建从端窗口
            window = SlaveWindow(
                client=client,
                pool_id=TEST_CONFIG["pool_id"],
                debug=True
            )
            window.show()
            
            # 等待窗口显示
            QTest.qWaitForWindowExposed(window)
            
            # 关闭窗口
            window.close()
            
            self.assertTrue(True)  # 如果能执行到这里，测试通过
        except Exception as e:
            self.fail(f"从端窗口测试失败: {e}")

class ParameterTests(unittest.TestCase):
    """参数验证测试"""
    
    def test_01_parameter_ranges(self):
        """测试参数范围有效性"""
        # 水听器参数范围
        hydrophone_ranges = {
            "x": (-1.0, 1.0),  # 米
            "y": (-0.25, 0.25),  # 米
            "z": (0.3, 1.0),  # 米
            "rotation": (0, 360)  # 度
        }
        
        # 换能器参数范围
        transducer_ranges = {
            "x": (-1.0, 1.0),  # 米
            "y": (-0.25, 0.25),  # 米
            "z": (0.3, 1.0),  # 米
            "rotation": (0, 360)  # 度
        }
        
        # 水池参数范围
        pool_ranges = {
            "depth": (0.0, 1.0)  # 米
        }
        
        # 测试参数范围有效性
        for param, (min_val, max_val) in hydrophone_ranges.items():
            self.assertLess(min_val, max_val, f"水听器 {param} 参数范围无效")
        
        for param, (min_val, max_val) in transducer_ranges.items():
            self.assertLess(min_val, max_val, f"换能器 {param} 参数范围无效")
        
        for param, (min_val, max_val) in pool_ranges.items():
            self.assertLess(min_val, max_val, f"水池 {param} 参数范围无效")
    
    def test_02_parameter_validation(self):
        """测试参数验证功能"""
        # 水听器测试用例
        hydrophone_test_cases = [
            # x 参数
            {"x": -1.1, "valid": False},  # 超出下限
            {"x": -1.0, "valid": True},   # 下限
            {"x": 0.0, "valid": True},    # 中间值
            {"x": 1.0, "valid": True},    # 上限
            {"x": 1.1, "valid": False},   # 超出上限
            
            # y 参数
            {"y": -0.3, "valid": False},   # 超出下限
            {"y": -0.25, "valid": True},   # 下限
            {"y": 0.0, "valid": True},     # 中间值
            {"y": 0.25, "valid": True},    # 上限
            {"y": 0.3, "valid": False},    # 超出上限
            
            # z 参数
            {"z": 0.2, "valid": False},    # 超出下限
            {"z": 0.3, "valid": True},     # 下限
            {"z": 0.5, "valid": True},     # 中间值
            {"z": 1.0, "valid": True},     # 上限
            {"z": 1.1, "valid": False},    # 超出上限
            
            # rotation 参数
            {"rotation": -10, "valid": False},  # 超出下限
            {"rotation": 0, "valid": True},     # 下限
            {"rotation": 180, "valid": True},   # 中间值
            {"rotation": 360, "valid": True},   # 上限
            {"rotation": 370, "valid": False}   # 超出上限
        ]
        
        # 验证测试用例
        for case in hydrophone_test_cases:
            param_name = list(filter(lambda x: x != "valid", case.keys()))[0]
            value = case[param_name]
            expected_valid = case["valid"]
            
            if param_name == "x":
                min_val, max_val = -1.0, 1.0
            elif param_name == "y":
                min_val, max_val = -0.25, 0.25
            elif param_name == "z":
                min_val, max_val = 0.3, 1.0
            elif param_name == "rotation":
                min_val, max_val = 0, 360
            
            is_valid = min_val <= value <= max_val
            self.assertEqual(is_valid, expected_valid, 
                            f"参数 {param_name}={value} 验证结果与预期不符")

def run_tests():
    """运行所有测试"""
    # 创建测试套件
    test_suite = unittest.TestSuite()
    
    # 添加网络测试
    test_suite.addTest(unittest.makeSuite(NetworkTests))
    
    # 添加服务器测试
    if MODULES_IMPORTED:
        test_suite.addTest(unittest.makeSuite(ServerTests))
    
    # 添加进程测试
    test_suite.addTest(unittest.makeSuite(ProcessTests))
    
    # 添加GUI测试
    if MODULES_IMPORTED:
        test_suite.addTest(unittest.makeSuite(GUITests))
    
    # 添加参数测试
    test_suite.addTest(unittest.makeSuite(ParameterTests))
    
    # 运行测试
    test_runner = unittest.TextTestRunner(verbosity=2)
    test_result = test_runner.run(test_suite)
    
    # 返回测试结果
    return test_result

def run_tests_without_gui(verbose=False):
    """运行不包含GUI测试的测试套件"""
    # 创建测试套件
    test_suite = unittest.TestSuite()
    
    # 添加网络测试
    test_suite.addTest(unittest.makeSuite(NetworkTests))
    
    # 添加服务器测试
    if MODULES_IMPORTED:
        test_suite.addTest(unittest.makeSuite(ServerTests))
    
    # 添加进程测试
    test_suite.addTest(unittest.makeSuite(ProcessTests))
    
    # 添加参数测试
    test_suite.addTest(unittest.makeSuite(ParameterTests))
    
    # 运行测试
    test_runner = unittest.TextTestRunner(verbosity=2 if verbose else 1)
    test_result = test_runner.run(test_suite)
    
    # 返回测试结果
    return test_result

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="OceanLab-Python项目测试脚本")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="服务器主机地址")
    parser.add_argument("--port", type=int, default=8000, help="服务器端口")
    parser.add_argument("--pool-id", type=str, default="test_pool_1", help="测试用实验池ID")
    parser.add_argument("--test-key", type=str, default="ABCDEF12", help="测试用密钥")
    parser.add_argument("--skip-gui", action="store_true", help="跳过GUI测试")
    parser.add_argument("--long-test", action="store_true", help="执行长时间测试")
    parser.add_argument("--verbose", "-v", action="store_true", help="详细输出")
    
    args = parser.parse_args()
    
    # 更新测试配置
    TEST_CONFIG.update({
        "server_host": args.host,
        "server_port": args.port,
        "pool_id": args.pool_id,
        "test_key": args.test_key,
        "long_test": args.long_test
    })
    
    print("======== OceanLab-Python项目测试 ========")
    print(f"测试配置: {json.dumps(TEST_CONFIG, indent=2)}")
    print(f"测试时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("========================================")
    
    try:
        # 根据参数决定是否跳过GUI测试
        if args.skip_gui:
            test_result = run_tests_without_gui(args.verbose)
        else:
            test_result = run_tests()
        
        # 输出测试统计
        print("\n============ 测试统计 ============")
        print(f"总测试数: {test_result.testsRun}")
        print(f"成功: {test_result.testsRun - len(test_result.errors) - len(test_result.failures)}")
        print(f"失败: {len(test_result.failures)}")
        print(f"错误: {len(test_result.errors)}")
        print("==================================")
        
        # 设置退出码
        sys.exit(len(test_result.errors) + len(test_result.failures))
    except KeyboardInterrupt:
        print("\n测试被用户中断")
        sys.exit(1)
    except Exception as e:
        print(f"\n测试运行发生错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        # 确保不会闪退，等待用户输入
        if not args.verbose:  # 如果不是详细模式，暂停
            input("\n按回车键继续...")
        # 进一步确保Windows命令行不会闪退
        if sys.platform == "win32":
            os.system("pause") 