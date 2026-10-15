import websocket
import json
import subprocess
import time
import threading
import os
import ssl  # 导入 ssl 模块

# 全局变量，用于管理 FFmpeg 进程
ffmpeg_process = None
ffmpeg_lock = threading.Lock()

# 禁用 WebSocket 客户端的调试日志（可选）
websocket.enableTrace(False)

def check_ivcam_device():
    """
    检查 iVCam 设备是否可用。
    返回设备名（如果可用）或 None（如果不可用）。
    """
    try:
        result = subprocess.run(
            ['ffmpeg', '-list_devices', 'true', '-f', 'dshow', '-i', 'dummy'],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=False
        )
        output = result.stdout.decode('utf-8', errors='ignore')
        if 'e2eSoft iVCam' in output:
            print("iVCam 设备可用: e2eSoft iVCam")
            return 'e2eSoft iVCam'
        elif r'@device_pnp_\\?\root#image#0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\global' in output:
            print("iVCam 设备可用（使用替代名称）")
            return r'@device_pnp_\\?\root#image#0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\global'
        else:
            print("未找到 iVCam 设备")
            return None
    except Exception as e:
        print(f"检查 iVCam 设备失败: {e}")
        return None

def start_ffmpeg(equipment_id, ws, max_retries=3):
    global ffmpeg_process
    retry_count = 0

    while retry_count < max_retries:
        with ffmpeg_lock:
            # 清理旧的 FFmpeg 进程（仅在必要时）
            if ffmpeg_process and ffmpeg_process.poll() is None:
                try:
                    ffmpeg_process.terminate()
                    ffmpeg_process.wait(timeout=5)  # 等待进程终止
                    print("终止已有 FFmpeg 进程")
                except Exception as e:
                    print(f"终止旧 FFmpeg 进程失败: {e}")
                finally:
                    ffmpeg_process = None

            # 检查 iVCam 设备
            device_name = check_ivcam_device()
            if not device_name:
                print(f"第 {retry_count + 1} 次尝试: iVCam 设备不可用，等待重试...")
                retry_count += 1
                time.sleep(5)
                continue

            # 重定向 FFmpeg 的 stdout 和 stderr
            with open('ffmpeg_error.log', 'a', encoding='utf-8') as error_log:
                error_log.write(f"\n--- FFmpeg 启动 (equipment_id={equipment_id}, attempt={retry_count + 1}) ---\n")
                ffmpeg_cmd = [
                    'ffmpeg', '-f', 'dshow', '-rtbufsize', '100M', '-i', f'video={device_name}',
                    '-vf', 'scale=640:360', '-r', '15',
                    '-f', 'mjpeg', '-q:v', '5',
                    '-tls_verify', '0',  # 忽略 HTTPS 证书验证
                    'https://localhost:3002/input'  # 使用 HTTPS
                ]
                print(f"启动 FFmpeg 命令: {' '.join(ffmpeg_cmd)}")
                try:
                    ffmpeg_process = subprocess.Popen(
                        ffmpeg_cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        universal_newlines=True
                    )
                    print(f"FFmpeg 进程启动 (equipment_id={equipment_id}, attempt={retry_count + 1}), PID: {ffmpeg_process.pid}")
                    # 发送启动成功响应
                    response = {
                        'type': 'experiment_response',
                        'equipment_id': equipment_id,
                        'response': {
                            'status': 'received',
                            'message': 'FFmpeg 进程启动成功'
                        }
                    }
                    ws.send(json.dumps(response))
                    print(f"已发送响应: {response}")

                    # 持续监控 FFmpeg 进程
                    while True:
                        if ffmpeg_process.poll() is not None:  # 进程已退出
                            stdout, stderr = ffmpeg_process.communicate()
                            return_code = ffmpeg_process.poll()
                            print(f"FFmpeg 进程退出，退出码: {return_code}")
                            error_log.write(f"FFmpeg 进程退出，退出码: {return_code}\n")
                            if stdout:
                                error_log.write(f"FFmpeg 输出: {stdout}\n")
                            if stderr:
                                error_log.write(f"FFmpeg 错误: {stderr}\n")
                            retry_count += 1
                            break
                        time.sleep(1)  # 每秒检查一次
                except Exception as e:
                    print(f"第 {retry_count + 1} 次尝试: 启动 FFmpeg 进程失败: {e}")
                    error_log.write(f"启动失败: {e}\n")
                    retry_count += 1
                    time.sleep(5)
                    continue
                finally:
                    if ffmpeg_process and ffmpeg_process.poll() is not None:
                        print("FFmpeg 进程已终止（finally 块）")
                        try:
                            ws.send(json.dumps({
                                'type': 'ffmpeg_status',
                                'equipment_id': equipment_id,
                                'status': 'terminated',
                                'message': f'FFmpeg 进程退出，退出码: {ffmpeg_process.poll()}'
                            }))
                            print(f"通知后端 FFmpeg 进程终止: equipment_id={equipment_id}")
                        except Exception as e:
                            print(f"通知后端失败: {e}")
                    ffmpeg_process = None  # 确保在 finally 块末尾清理

    if retry_count >= max_retries:
        print(f"启动 FFmpeg 失败，已达到最大重试次数 ({max_retries})")
        ws.send(json.dumps({
            'type': 'equipment_status',
            'equipment_id': equipment_id,
            'status': 'fault'
        }))
        print(f"发送状态更新: equipment_id={equipment_id}, status=fault")
        return

    try:
        stdout, stderr = ffmpeg_process.communicate(timeout=30*60)
        if ffmpeg_process.returncode != 0:
            print(f"FFmpeg 进程异常退出 (return code: {ffmpeg_process.returncode})，请检查 ffmpeg_error.log")
            with open('ffmpeg_error.log', 'a', encoding='utf-8') as error_log:
                if stdout:
                    error_log.write(f"FFmpeg 输出: {stdout}\n")
                if stderr:
                    error_log.write(f"FFmpeg 错误: {stderr}\n")
    except subprocess.TimeoutExpired:
        with ffmpeg_lock:
            if ffmpeg_process and ffmpeg_process.poll() is None:
                ffmpeg_process.terminate()
                print("FFmpeg 进程超时，已终止")
    except Exception as e:
        print(f"FFmpeg 进程错误: {e}")
        with ffmpeg_lock:
            if ffmpeg_process and ffmpeg_process.poll() is None:
                ffmpeg_process.terminate()
    finally:
        with ffmpeg_lock:
            if ffmpeg_process and ffmpeg_process.poll() is None:
                ffmpeg_process.terminate()
                print("FFmpeg 进程已终止（finally 块）")
            ws.send(json.dumps({
                'type': 'ffmpeg_terminated',
                'equipment_id': equipment_id
            }))
            print(f"通知后端 FFmpeg 进程终止: equipment_id={equipment_id}")
            ffmpeg_process = None

def on_message(ws, message):
    """
    处理从服务器接收到的 WebSocket 消息。
    """
    try:
        data = json.loads(message)
        print(f"接收到消息: {data}")

        if data['type'] == 'experiment_params':
            print(f"接收到实验参数: {data['params']}")
            response = {
                'type': 'experiment_response',
                'equipment_id': data['equipment_id'],
                'response': {
                    'status': 'received',
                    'params': data['params']
                }
            }
            ws.send(json.dumps(response))
            print(f"已发送响应: {response}")

            threading.Thread(target=start_ffmpeg, args=(data['equipment_id'], ws), daemon=True).start()

        elif data['type'] == 'reservation_cancelled' and data['equipment_id'] == 1:
            print("收到预约取消通知，终止 FFmpeg 进程")
            with ffmpeg_lock:
                if ffmpeg_process and ffmpeg_process.poll() is None:
                    try:
                        ffmpeg_process.terminate()
                        ffmpeg_process.wait(timeout=5)
                        print("FFmpeg 进程已终止")
                    except Exception as e:
                        print(f"终止 FFmpeg 进程失败: {e}")
                    finally:
                        ffmpeg_process = None
                    ws.send(json.dumps({
                        'type': 'ffmpeg_terminated',
                        'equipment_id': 1
                    }))
                    print("通知后端 FFmpeg 进程终止: equipment_id=1")

        elif data['type'] == 'video_stream':
            pass

    except json.JSONDecodeError:
        print("消息解析失败: 无效的 JSON 格式")
    except Exception as e:
        print(f"处理消息时发生错误: {e}")

def on_error(ws, error):
    print(f"WebSocket 错误: {error}")

def on_close(ws, close_status_code, close_msg):
    print(f"WebSocket 连接关闭: 状态码={close_status_code}, 消息={close_msg}")
    with ffmpeg_lock:
        if ffmpeg_process and ffmpeg_process.poll() is None:
            try:
                ffmpeg_process.terminate()
                ffmpeg_process.wait(timeout=5)  # 等待进程终止
                print("FFmpeg 进程已终止（on_close）")
            except Exception as e:
                print(f"终止 FFmpeg 进程失败: {e}")
            finally:
                ffmpeg_process = None

def on_open(ws):
    registration_message = {
        'type': 'register_client',
        'equipment_id': 1
    }
    ws.send(json.dumps(registration_message))
    print(f"从端注册为实验池 1: {registration_message}")

def run_websocket():
    while True:
        try:
            ws = websocket.WebSocketApp('wss://localhost:3002',  # 修改为本地地址
                                        on_message=on_message,
                                        on_error=on_error,
                                        on_close=on_close,
                                        on_open=on_open)
            ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE})
        except Exception as e:
            print(f"WebSocket 连接失败: {e}")
        print("尝试重新连接...")
        time.sleep(5)

if __name__ == "__main__":
    print("启动从端客户端...")
    run_websocket()