@echo off
chcp 65001 > nul
echo 设置环境...
cd "%~dp0.."

echo 安装PyInstaller...
pip install pyinstaller

echo 清理旧文件...
if exist dist rmdir /s /q dist
if exist build rmdir /s /q build
if exist *.spec del *.spec

echo 创建资源目录(如果不存在)...
if not exist resources mkdir resources

echo 开始打包主端程序...
pyinstaller --clean --onedir --windowed --name="OceanLab-Master" main.py

echo 开始打包从端程序...
pyinstaller --clean --onedir --windowed --name="OceanLab-Slave" client\client_app.py

echo 打包完成！
echo 主端程序位于: %~dp0..\dist\OceanLab-Master\OceanLab-Master.exe
echo 从端程序位于: %~dp0..\dist\OceanLab-Slave\OceanLab-Slave.exe

pause 