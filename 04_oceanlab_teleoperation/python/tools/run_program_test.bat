@echo off
REM ======================================================
REM OceanLab-Python程序测试启动脚本
REM ======================================================
REM 此批处理文件用于在Windows环境下运行program/tools目录下的program_test.py测试脚本
REM 可以通过命令行参数控制测试范围和行为

setlocal enabledelayedexpansion

REM 设置Python解释器路径（默认使用系统Python）
set PYTHON_CMD=python

REM 设置测试脚本路径（相对于当前批处理文件位置）
set TEST_SCRIPT=%~dp0program_test.py

REM 获取项目根目录
set ROOT_DIR=%~dp0..\..

REM 检查Python环境
%PYTHON_CMD% --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo 错误: 未找到Python解释器
    echo 请确保Python已安装并添加到系统PATH中
    pause
    exit /b 1
)

REM 检查Python版本
for /f "tokens=2" %%V in ('%PYTHON_CMD% --version 2^>^&1') do (
    set PYTHON_VERSION=%%V
)
echo 使用Python版本: %PYTHON_VERSION%

REM 检查测试脚本是否存在
if not exist "%TEST_SCRIPT%" (
    echo 错误: 未找到测试脚本 %TEST_SCRIPT%
    pause
    exit /b 1
)

REM 显示帮助信息
if "%1"=="--help" (
    echo OceanLab-Python程序测试启动脚本
    echo 用法: run_program_test.bat [选项]
    echo.
    echo 选项:
    echo   --host HOST         指定服务器主机地址（默认为127.0.0.1）
    echo   --port PORT         指定服务器端口（默认为8000）
    echo   --pool-id ID        指定测试用实验池ID（默认为test_pool_1）
    echo   --test-key KEY      指定测试用密钥（默认为ABCDEF12）
    echo   --skip-gui          跳过GUI测试
    echo   --long-test         启用长时间测试
    echo   --verbose, -v       启用详细输出
    echo   --install-deps      安装测试依赖项
    echo   --clean             清理测试生成的临时文件
    echo   --help              显示帮助信息
    pause
    exit /b 0
)

REM 解析命令行参数
set HOST=127.0.0.1
set PORT=8000
set POOL_ID=test_pool_1
set TEST_KEY=ABCDEF12
set SKIP_GUI=
set LONG_TEST=
set VERBOSE=
set INSTALL_DEPS=0
set CLEAN=0

:parse_args
if "%1"=="" goto end_parse_args
if /i "%1"=="--host" (
    set HOST=%2
    shift
) else if /i "%1"=="--port" (
    set PORT=%2
    shift
) else if /i "%1"=="--pool-id" (
    set POOL_ID=%2
    shift
) else if /i "%1"=="--test-key" (
    set TEST_KEY=%2
    shift
) else if /i "%1"=="--skip-gui" (
    set SKIP_GUI=--skip-gui
) else if /i "%1"=="--long-test" (
    set LONG_TEST=--long-test
) else if /i "%1"=="--verbose" (
    set VERBOSE=--verbose
) else if /i "%1"=="-v" (
    set VERBOSE=--verbose
) else if /i "%1"=="--install-deps" (
    set INSTALL_DEPS=1
) else if /i "%1"=="--clean" (
    set CLEAN=1
) else (
    echo 警告: 未知选项 %1
)
shift
goto parse_args
:end_parse_args

REM 安装测试依赖项
if %INSTALL_DEPS%==1 (
    echo 正在安装测试依赖项...
    %PYTHON_CMD% -m pip install --upgrade pip
    %PYTHON_CMD% -m pip install -r "%ROOT_DIR%\requirements.txt"
    if %ERRORLEVEL% neq 0 (
        echo 错误: 安装依赖项失败
        pause
        exit /b 1
    )
    echo 依赖项安装完成
)

REM 清理测试生成的临时文件
if %CLEAN%==1 (
    echo 正在清理测试临时文件...
    if exist "%~dp0program_test_log.txt" del "%~dp0program_test_log.txt"
    if exist "%~dp0__pycache__" rmdir /s /q "%~dp0__pycache__"
    echo 清理完成
)

REM 设置环境变量，确保python能找到所有模块
set PYTHONPATH=%ROOT_DIR%;%PYTHONPATH%

echo ====================================================
echo OceanLab-Python程序测试启动
echo ====================================================
echo 测试脚本: %TEST_SCRIPT%
echo 主机地址: %HOST%
echo 服务器端口: %PORT%
echo 实验池ID: %POOL_ID%
echo 时间: %DATE% %TIME%
echo ====================================================

REM 构建命令行参数
set CMD_ARGS=--host %HOST% --port %PORT% --pool-id %POOL_ID% --test-key %TEST_KEY% %SKIP_GUI% %LONG_TEST% %VERBOSE%

REM 切换到测试脚本所在目录
cd "%~dp0"

REM 运行测试
%PYTHON_CMD% "%TEST_SCRIPT%" %CMD_ARGS%
set TEST_RESULT=%ERRORLEVEL%

echo ====================================================
if %TEST_RESULT%==0 (
    echo 测试结果: 通过
) else (
    echo 测试结果: 失败 ^(错误代码: %TEST_RESULT%^)
)
echo ====================================================

REM 确保命令行窗口不会立即关闭
pause

exit /b %TEST_RESULT% 