@echo off
REM ======================================================
REM OceanLab网页应用测试报告演示脚本
REM ======================================================
REM 此批处理文件用于快速打开测试报告进行答辩展示

REM 设置命令行编码为UTF-8
chcp 65001 >nul

REM 设置错误级别，避免提前退出
setlocal enabledelayedexpansion
set ERROR_LEVEL=0

echo ====================================================
echo OceanLab网页应用测试报告演示
echo ====================================================

REM 设置环境变量确保中文显示正确
set "LANG=zh_CN.UTF-8"
set "NODE_ENV=test"

REM 创建测试目录结构
if not exist "test\screenshots" mkdir test\screenshots
if not exist "test\coverage" mkdir test\coverage

REM 检查Node.js环境
node --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo 错误: 未找到Node.js
    echo 请确保Node.js已安装并添加到系统PATH中
    pause
    exit /b 1
)

REM 创建test-report-template.html如果不存在
if not exist "test\test-report-template.html" (
    echo 警告: 找不到测试报告模板，尝试创建基本模板...
    (
        echo ^<!DOCTYPE html^>
        echo ^<html lang="zh-CN"^>
        echo ^<head^>
        echo   ^<meta charset="UTF-8"^>
        echo   ^<title^>OceanLab网页应用测试报告^</title^>
        echo   ^<style^>
        echo     body { font-family: 'Microsoft YaHei', Arial, sans-serif; }
        echo   ^</style^>
        echo ^</head^>
        echo ^<body^>
        echo   ^<h1^>OceanLab网页应用测试报告^</h1^>
        echo   ^<p^>演示用测试报告^</p^>
        echo ^</body^>
        echo ^</html^>
    ) > "test\test-report-template.html"
)

REM 如果报告存在则使用已有报告，否则生成模拟演示报告
if exist "test\test-report.html" (
    echo 使用已有的测试报告...
) else (
    echo 生成演示测试报告...
    
    REM 尝试创建模拟截图
    if exist "test\create-mock-screenshots.js" (
        echo 正在生成模拟测试截图...
        node test/create-mock-screenshots.js
        if %ERRORLEVEL% neq 0 (
            echo 警告: 生成模拟截图时遇到问题，但将继续执行
            set ERROR_LEVEL=1
        )
    )
    
    echo 正在生成测试报告...
    node test/generate-report.js
    if %ERRORLEVEL% neq 0 (
        echo 生成报告时遇到问题，但将继续执行
        set ERROR_LEVEL=1
    )
)

echo.
echo ====================================================
if %ERROR_LEVEL% neq 0 (
    echo 警告: 测试报告生成过程中遇到一些问题
) else (
    echo 测试报告已准备就绪!
)
echo ====================================================

REM 检查测试报告是否存在，如果不存在则创建一个简单的报告
if not exist "test\test-report.html" (
    echo 错误: 未能生成测试报告，创建基本报告...
    (
        echo ^<!DOCTYPE html^>
        echo ^<html lang="zh-CN"^>
        echo ^<head^>
        echo   ^<meta charset="UTF-8"^>
        echo   ^<title^>OceanLab网页应用测试报告^</title^>
        echo   ^<style^>
        echo     body { font-family: 'Microsoft YaHei', Arial, sans-serif; }
        echo     .error { color: red; }
        echo   ^</style^>
        echo ^</head^>
        echo ^<body^>
        echo   ^<h1^>OceanLab网页应用测试报告^</h1^>
        echo   ^<p class="error"^>生成报告时遇到问题。这是一个基本的演示报告。^</p^>
        echo   ^<p^>请检查测试环境并重新运行测试。^</p^>
        echo ^</body^>
        echo ^</html^>
    ) > "test\test-report.html"
)

REM 打开测试报告
start "" "test\test-report.html"

echo 按任意键退出...
pause 