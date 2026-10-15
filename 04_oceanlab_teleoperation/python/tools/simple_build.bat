@echo off
chcp 65001 > nul
echo 正在安装必要的依赖...
pip install -r "%~dp0..\requirements.txt"
pip install pyinstaller

echo 清理旧的构建文件...
rd /s /q "%~dp0..\build" 2>nul
rd /s /q "%~dp0..\dist" 2>nul
del "%~dp0..\*.spec" 2>nul

echo 创建资源目录...
mkdir "%~dp0..\resources" 2>nul

echo 创建示例图标文件...
echo 如果您有自己的图标文件，请替换这些文件
copy "%WINDIR%\System32\cmd.exe" "%~dp0..\resources\master.ico" >nul 2>&1
copy "%WINDIR%\System32\cmd.exe" "%~dp0..\resources\slave.ico" >nul 2>&1

echo 开始构建主端程序...
cd "%~dp0.."
pyinstaller --clean --noconfirm --onedir --windowed --name="OceanLab-Master" ^
  --icon="%~dp0..\resources\master.ico" ^
  --add-data "%~dp0..\resources;resources" ^
  --hidden-import=PyQt5 --hidden-import=matplotlib --hidden-import=numpy ^
  "%~dp0..\main.py"

echo 开始构建从端程序...
pyinstaller --clean --noconfirm --onedir --windowed --name="OceanLab-Slave" ^
  --icon="%~dp0..\resources\slave.ico" ^
  --add-data "%~dp0..\resources;resources" ^
  --hidden-import=PyQt5 --hidden-import=matplotlib --hidden-import=numpy ^
  "%~dp0..\client\client_app.py"

echo 打包完成！
echo 主端程序位于: "%~dp0..\dist\OceanLab-Master\OceanLab-Master.exe"
echo 从端程序位于: "%~dp0..\dist\OceanLab-Slave\OceanLab-Slave.exe"

pause 