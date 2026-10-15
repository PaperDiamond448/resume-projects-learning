@echo off
REM ======================================================
REM OceanLab网页应用截图转换脚本
REM ======================================================
REM 此批处理文件用于帮助用户将HTML截图转换为PNG图片

REM 设置命令行编码为UTF-8
chcp 65001 >nul

echo ====================================================
echo OceanLab网页应用截图转换
echo ====================================================

REM 检查截图目录是否存在
if not exist "test\screenshots" (
    mkdir "test\screenshots"
    echo 已创建screenshots目录
)

REM 确保HTML文件存在
set HTML_COUNT=0
for %%f in (test\screenshots\*.html) do set /a HTML_COUNT+=1

if %HTML_COUNT% EQU 0 (
    echo 未找到任何HTML截图文件!
    echo 请先运行 run_tests.bat 生成HTML截图文件
    goto END
) else (
    echo 找到 %HTML_COUNT% 个HTML截图文件
)

echo.
echo ==================================================
echo 请按照以下步骤手动将HTML文件转换为PNG图片:
echo.
echo 1. 浏览器将自动打开截图目录中的所有HTML文件
echo 2. 对每个页面使用浏览器的截图功能或按Print Screen键
echo 3. 将截图保存为PNG格式，与HTML文件名称相同
echo 4. 将PNG文件保存到 test\screenshots 目录中
echo ==================================================
echo.

echo 是否要打开所有HTML文件? (Y/N)
set /p CHOICE=

if /i "%CHOICE%" EQU "Y" (
    echo 正在打开HTML文件...
    for %%f in (test\screenshots\*.html) do (
        start "" "%%f"
        timeout /t 1 >nul
    )
) else (
    echo 您可以手动打开test\screenshots目录中的HTML文件
)

:END
echo.
echo 完成后，请再次运行 run_tests.bat 生成包含截图的测试报告
echo.
echo 按任意键退出...
pause > nul 