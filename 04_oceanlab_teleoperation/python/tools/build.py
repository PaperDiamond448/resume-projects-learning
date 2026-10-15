import os
import sys
import shutil
from PyInstaller.__main__ import run

# 获取项目根目录
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def clean_build_dirs():
    """清理构建目录"""
    dirs_to_clean = ['build', 'dist']
    for dir_name in dirs_to_clean:
        dir_path = os.path.join(ROOT_DIR, dir_name)
        if os.path.exists(dir_path):
            shutil.rmtree(dir_path)

def build_master():
    """构建主端程序"""
    print("开始构建主端程序...")
    
    # 主端程序打包参数
    master_args = [
        os.path.join(ROOT_DIR, 'main.py'),  # 主程序入口
        '--name=OceanLab-Master',  # 生成的exe名称
        '--windowed',  # 使用GUI模式
        f'--icon={os.path.join(ROOT_DIR, "resources", "master.ico")}',  # 程序图标
        f'--add-data={os.path.join(ROOT_DIR, "resources")};resources',  # 添加资源文件
        '--hidden-import=PyQt5',
        '--hidden-import=matplotlib',
        '--hidden-import=numpy',
        '--hidden-import=matlab',
        '--noconfirm',  # 不询问确认
        '--clean',  # 清理临时文件
        f'--workpath={os.path.join(ROOT_DIR, "build")}',  # 指定工作目录
        f'--distpath={os.path.join(ROOT_DIR, "dist")}',  # 指定输出目录
    ]
    
    # 执行打包
    run(master_args)
    
    print("主端程序构建完成！")

def build_slave():
    """构建从端程序"""
    print("开始构建从端程序...")
    
    # 从端程序打包参数
    slave_args = [
        os.path.join(ROOT_DIR, 'client', 'client_app.py'),  # 从端程序入口
        '--name=OceanLab-Slave',  # 生成的exe名称
        '--windowed',  # 使用GUI模式
        f'--icon={os.path.join(ROOT_DIR, "resources", "slave.ico")}',  # 程序图标
        f'--add-data={os.path.join(ROOT_DIR, "resources")};resources',  # 添加资源文件
        '--hidden-import=PyQt5',
        '--hidden-import=matplotlib',
        '--hidden-import=numpy',
        '--hidden-import=matlab',
        '--noconfirm',  # 不询问确认
        '--clean',  # 清理临时文件
        f'--workpath={os.path.join(ROOT_DIR, "build")}',  # 指定工作目录
        f'--distpath={os.path.join(ROOT_DIR, "dist")}',  # 指定输出目录
    ]
    
    # 执行打包
    run(slave_args)
    
    print("从端程序构建完成！")

def main():
    """主函数"""
    # 切换到项目根目录
    os.chdir(ROOT_DIR)
    
    # 清理旧的构建文件
    clean_build_dirs()
    
    # 构建主端
    build_master()
    
    # 构建从端
    build_slave()
    
    print("\n打包完成！")
    print(f"主端程序位于: {os.path.join(ROOT_DIR, 'dist', 'OceanLab-Master', 'OceanLab-Master.exe')}")
    print(f"从端程序位于: {os.path.join(ROOT_DIR, 'dist', 'OceanLab-Slave', 'OceanLab-Slave.exe')}")

if __name__ == '__main__':
    main() 