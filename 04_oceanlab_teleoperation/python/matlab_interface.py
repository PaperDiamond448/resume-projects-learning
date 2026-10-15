#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
MATLAB接口模块
"""

import os
import sys
import numpy as np
import tempfile
import json

class MatlabInterface:
    """MATLAB接口"""
    
    def __init__(self, use_matlab=True, debug=False):
        """初始化MATLAB接口
        
        Args:
            use_matlab (bool): 是否使用MATLAB
            debug (bool): 是否启用调试模式
        """
        self.use_matlab = use_matlab
        self.matlab_engine = None
        self.change_m_path = None
        self.debug = debug
        
        if self.debug:
            print(f"MatlabInterface初始化: use_matlab={use_matlab}, debug={debug}")
        
        if use_matlab:
            try:
                # 尝试导入MATLAB引擎
                import matlab.engine
                
                # 启动MATLAB引擎
                if self.debug:
                    print("正在启动MATLAB引擎...")
                self.matlab_engine = matlab.engine.start_matlab()
                if self.debug:
                    print("MATLAB引擎已启动")
                
                # 添加当前目录到MATLAB路径
                current_dir = os.path.dirname(os.path.abspath(__file__))
                self.matlab_engine.addpath(current_dir)
                
                # 添加simulator目录到MATLAB路径
                simulator_dir = os.path.join(current_dir, 'simulator')
                if os.path.exists(simulator_dir):
                    self.matlab_engine.addpath(simulator_dir)
                
                # 查找并添加change.m文件
                self.find_change_m_file()
                
            except ImportError:
                if self.debug:
                    print("警告：无法导入MATLAB引擎，将使用Python模拟引擎")
                self.use_matlab = False
                self.matlab_engine = None
            except Exception as e:
                if self.debug:
                    print(f"启动MATLAB引擎时出错: {e}")
                self.use_matlab = False
                self.matlab_engine = None
    
    def find_change_m_file(self):
        """查找change.m文件"""
        # 检查当前目录
        current_dir = os.path.dirname(os.path.abspath(__file__))
        change_m_path = os.path.join(current_dir, 'change.m')
        
        if os.path.exists(change_m_path):
            self.change_m_path = change_m_path
            if self.debug:
                print(f"在当前目录找到change.m: {change_m_path}")
            return True
        
        # 检查根目录
        root_dir = os.path.dirname(current_dir)
        change_m_path = os.path.join(root_dir, 'change.m')
        
        if os.path.exists(change_m_path):
            self.change_m_path = change_m_path
            # 将根目录添加到MATLAB路径
            self.matlab_engine.addpath(root_dir)
            if self.debug:
                print(f"在根目录找到change.m: {change_m_path}")
            return True
        
        # 检查上级目录
        parent_dir = os.path.dirname(root_dir)
        change_m_path = os.path.join(parent_dir, 'change.m')
        
        if os.path.exists(change_m_path):
            self.change_m_path = change_m_path
            # 将上级目录添加到MATLAB路径
            self.matlab_engine.addpath(parent_dir)
            if self.debug:
                print(f"在上级目录找到change.m: {change_m_path}")
            return True
        
        if self.debug:
            print("未找到change.m文件")
        return False
    
    def is_matlab_available(self):
        """检查MATLAB是否可用"""
        return self.use_matlab and self.matlab_engine is not None
    
    def is_change_m_available(self):
        """检查change.m是否可用"""
        return self.is_matlab_available() and self.change_m_path is not None
    
    def close(self):
        """关闭MATLAB引擎"""
        if self.matlab_engine:
            try:
                self.matlab_engine.quit()
                if self.debug:
                    print("MATLAB引擎已关闭")
            except Exception as e:
                if self.debug:
                    print(f"关闭MATLAB引擎时出错: {e}")
            finally:
                self.matlab_engine = None
    
    def update_3d_model(self, params):
        """更新3D模型"""
        if not self.is_change_m_available():
            return False
        
        try:
            # 提取参数
            hydrophone_x = float(params.get('hydrophone_x', 0.0))
            hydrophone_y = float(params.get('hydrophone_y', 0.0))
            hydrophone_z = float(params.get('hydrophone_z', 0.5))
            hydrophone_rotation = float(params.get('hydrophone_rotation', 0.0))
            
            transducer_x = float(params.get('transducer_x', 0.0))
            transducer_y = float(params.get('transducer_y', 0.0))
            transducer_z = float(params.get('transducer_z', 0.5))
            transducer_rotation = float(params.get('transducer_rotation', 0.0))
            
            water_depth = float(params.get('water_depth', 0.5))
            
            # 调用MATLAB函数
            self.matlab_engine.change(
                hydrophone_x, hydrophone_y, hydrophone_z, hydrophone_rotation,
                transducer_x, transducer_y, transducer_z, transducer_rotation,
                water_depth, nargout=0
            )
            
            return True
        except Exception as e:
            if self.debug:
                print(f"更新MATLAB 3D模型时出错: {e}")
            return False
    
    def run_simulation(self, params):
        """运行仿真"""
        if self.is_matlab_available():
            return self._run_matlab_simulation(params)
        else:
            return self._run_python_simulation(params)
    
    def _run_matlab_simulation(self, params):
        """使用MATLAB运行仿真"""
        try:
            # 将参数转换为MATLAB可接受的格式
            matlab_params = {}
            for key, value in params.items():
                matlab_params[key] = float(value)
            
            # 将参数写入临时JSON文件
            with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f:
                json.dump(matlab_params, f)
                params_file = f.name
            
            # 调用MATLAB函数
            result_file = self.matlab_engine.run_simulation(params_file, nargout=1)
            
            # 从结果文件读取结果
            with open(result_file, 'r') as f:
                result = json.load(f)
            
            # 删除临时文件
            try:
                os.remove(params_file)
                os.remove(result_file)
            except:
                pass
            
            return result
        except Exception as e:
            print(f"运行MATLAB仿真时出错: {e}")
            return None
    
    def _run_python_simulation(self, params):
        """使用Python运行仿真"""
        # 这里实现一个简单的Python模拟引擎
        result = {
            'success': True,
            'data': {}
        }
        
        # 提取参数
        hydrophone_x = params.get('hydrophone_x', 0.0)
        hydrophone_y = params.get('hydrophone_y', 0.0)
        hydrophone_z = params.get('hydrophone_z', 0.5)
        hydrophone_rotation = params.get('hydrophone_rotation', 0.0)
        
        transducer_x = params.get('transducer_x', 0.0)
        transducer_y = params.get('transducer_y', 0.0)
        transducer_z = params.get('transducer_z', 0.5)
        transducer_rotation = params.get('transducer_rotation', 0.0)
        
        water_depth = params.get('water_depth', 0.5)
        
        # 计算水听器和换能器之间的距离
        distance = np.sqrt(
            (hydrophone_x - transducer_x) ** 2 +
            (hydrophone_y - transducer_y) ** 2 +
            (hydrophone_z - transducer_z) ** 2
        )
        
        # 计算声波传播时间（假设声速为1500 m/s）
        sound_speed = 1500.0  # 水中声速，单位：m/s
        propagation_time = distance / sound_speed
        
        # 计算接收信号强度（简化模型）
        # 假设信号强度与距离的平方成反比
        signal_strength = 100.0 / (distance ** 2) if distance > 0 else 100.0
        
        # 考虑水面反射
        if water_depth > 0:
            # 计算经过水面反射的路径长度
            reflection_path = np.sqrt(
                (hydrophone_x - transducer_x) ** 2 +
                (hydrophone_y - transducer_y) ** 2 +
                (2 * water_depth - hydrophone_z - transducer_z) ** 2
            )
            
            # 计算反射信号的传播时间
            reflection_time = reflection_path / sound_speed
            
            # 计算反射信号强度（假设反射损失为0.5）
            reflection_strength = 50.0 / (reflection_path ** 2) if reflection_path > 0 else 50.0
            
            # 考虑直达信号和反射信号的相位差
            phase_difference = 2 * np.pi * (reflection_path - distance) / 0.02  # 假设波长为2cm
            
            # 计算合成信号强度（简化模型）
            total_strength = signal_strength + reflection_strength * np.cos(phase_difference)
        else:
            total_strength = signal_strength
            reflection_time = 0
        
        # 填充结果
        result['data'] = {
            'distance': float(distance),
            'propagation_time': float(propagation_time),
            'signal_strength': float(signal_strength),
            'reflection_time': float(reflection_time),
            'total_strength': float(total_strength)
        }
        
        return result
    
    def process_signal(self, signal_data):
        """处理信号"""
        if self.is_matlab_available():
            return self._process_signal_matlab(signal_data)
        else:
            return self._process_signal_python(signal_data)
    
    def _process_signal_matlab(self, signal_data):
        """使用MATLAB处理信号"""
        try:
            # 将信号数据转换为MATLAB可接受的格式
            signal_array = np.array(signal_data, dtype=np.float64)
            
            # 调用MATLAB函数
            result = self.matlab_engine.process_signal(signal_array.tolist(), nargout=1)
            
            # 转换结果为Python格式
            return np.array(result)
        except Exception as e:
            print(f"使用MATLAB处理信号时出错: {e}")
            return None
    
    def _process_signal_python(self, signal_data):
        """使用Python处理信号"""
        # 这里实现一个简单的Python信号处理
        signal = np.array(signal_data)
        
        # 计算信号的FFT
        fft_result = np.fft.fft(signal)
        
        # 计算功率谱
        power_spectrum = np.abs(fft_result) ** 2
        
        # 找到峰值频率
        peak_idx = np.argmax(power_spectrum[:len(power_spectrum)//2])
        
        # 假设采样率为44100 Hz
        sampling_rate = 44100
        peak_frequency = peak_idx * sampling_rate / len(signal)
        
        return {
            'peak_frequency': float(peak_frequency),
            'power_spectrum': power_spectrum.tolist()
        } 