@echo off
chcp 65001 > nul
echo 正在启动OceanLab-Python主控终端...
python main.py --port 8888 %*
pause 