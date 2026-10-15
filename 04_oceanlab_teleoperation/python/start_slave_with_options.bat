@echo off
setlocal enabledelayedexpansion

:: OceanLab从端高级启动脚本 - 支持更多选项
title OceanLab 从端应用 (高级选项)

:: 设置Python路径 - 如果添加到PATH则无需修改
set PYTHON=python

:: 颜色设置
set COLOR_HEADER=0B
set COLOR_INFO=0A
set COLOR_WARNING=0E
set COLOR_ERROR=0C

echo.
color %COLOR_HEADER%
echo ===================================================
echo      OceanLab-Python 从端应用 - 启动配置工具
echo ===================================================
color 07

:: 检查从端脚本是否存在
if not exist "slave_terminal\slave_app.py" (
    color %COLOR_ERROR%
    echo 错误: 从端应用程序文件不存在!
    echo 请确保slave_terminal目录中包含slave_app.py文件
    pause
    exit /b 1
)

:: 检查setup_dialog是否存在
if not exist "slave_terminal\run_slave.py" (
    color %COLOR_WARNING%
    echo 警告: 启动对话框文件不存在，将使用命令行参数启动。
)

:: 获取命令行参数或使用交互式界面
set POOL_ID=
set PORT=
set SERVER=

:: 处理命令行参数
if not "%~1"=="" (
    set POOL_ID=%~1
)
if not "%~2"=="" (
    set PORT=%~2
)
if not "%~3"=="" (
    set SERVER=%~3
)

:: 如果没有足够的命令行参数，使用交互式界面或启动设置对话框
if "%POOL_ID%"=="" (
    if exist "slave_terminal\run_slave.py" (
        color %COLOR_INFO%
        echo 正在启动OceanLab从端设置对话框...
        %PYTHON% slave_terminal\run_slave.py
        exit /b %errorlevel%
    ) else (
        :: 交互式获取参数
        color %COLOR_INFO%
        echo 请输入实验池ID (默认: 1):
        set /p "POOL_ID="
        if "!POOL_ID!"=="" set POOL_ID=1
        
        echo 请输入监听端口 (默认: 8000):
        set /p "PORT="
        if "!PORT!"=="" set PORT=8000
        
        echo 请输入服务器地址 (默认: localhost):
        set /p "SERVER="
        if "!SERVER!"=="" set SERVER=localhost
    )
) else (
    :: 如果只有实验池ID，需要设置默认端口
    if "%PORT%"=="" set PORT=8000
    if "%SERVER%"=="" set SERVER=localhost
)

:: 显示启动信息
color %COLOR_INFO%
echo.
echo 正在启动 OceanLab 实验池 %POOL_ID% 从端应用...
echo 服务器地址: %SERVER%
echo 监听端口: %PORT%
echo.

:: 启动应用
%PYTHON% slave_terminal\slave_app.py --pool-id %POOL_ID% --port %PORT% --server %SERVER%

:: 检查退出代码
if %errorlevel% neq 0 (
    color %COLOR_ERROR%
    echo.
    echo 从端应用异常退出，错误代码: %errorlevel%
    echo 可能原因:
    echo 1. Python环境问题
    echo 2. 端口 %PORT% 已被占用
    echo 3. 服务器 %SERVER% 无法连接
    echo 4. 应用程序错误
    echo.
    echo 请检查日志并解决问题后重试
) else (
    color %COLOR_INFO%
    echo 从端应用已正常关闭
)

pause 