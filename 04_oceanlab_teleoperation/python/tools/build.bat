@echo off
chcp 65001 > nul
echo 正在安装必要的依赖...
pip install -r "%~dp0..\requirements.txt"

echo 正在构建程序...
python "%~dp0build.py"

echo 构建完成！
echo 主端程序位于: "%~dp0..\dist\OceanLab-Master\OceanLab-Master.exe"
echo 从端程序位于: "%~dp0..\dist\OceanLab-Slave\OceanLab-Slave.exe"

pause 