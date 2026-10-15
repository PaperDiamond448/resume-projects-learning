clear all; clc;

%% 参数设置（与DSP一致）
Fs = 100000;        % 采样率100kHz
ADB_LEN = 24576;    % 缓冲区长度

%% 生成背景噪声
noise_amp = 50;    % 噪声幅度
noise = noise_amp * randn(1, ADB_LEN);

%% 生成脉冲信号（与DSP的陷波频率一致）
% DSP默认陷波频率：9kHz, 11kHz, 13.5kHz, 17.24kHz, 20kHz
% 生成9kHz的正弦脉冲
f_pulse = 9000;         % 脉冲频率9kHz
T_pulse = 0.0012;       % 脉宽1.2ms（对应THRESH_WIDTH_L1=80点@100kHz... 实际120点）
pulse_len = round(T_pulse * Fs);  % 120个点

pulse_amp = 32000;       % 脉冲幅度，要明显超过噪声

% 生成单个脉冲
t_pulse = (0:pulse_len-1) / Fs;
single_pulse = pulse_amp * sin(2*pi*f_pulse*t_pulse);

% 在多个位置插入脉冲（间隔约2s=200000点，但缓冲区只有24576点）
% 在5000、12000、20000点处插入脉冲
signal = noise;
pulse_positions = [3000, 10000, 18000];
for pos = pulse_positions
    if pos + pulse_len <= ADB_LEN
        signal(pos:pos+pulse_len-1) = signal(pos:pos+pulse_len-1) + single_pulse;
    end
end

%% 转成int16
signal_int16 = int16(signal);
% 找真正的峰值位置
[max_val, max_idx] = max(abs(double(signal_int16)));
fprintf('最大值位置: 第%d点, 幅度: %d\n', max_idx, signal_int16(max_idx));

% 画图放大看第一个脉冲
figure;
plot(signal_int16(2800:3200));
title('第一个脉冲放大图');
xlabel('采样点(相对2800)');
ylabel('幅度');
grid on;
xline(3000-2800, 'r--', '预期位置3000');
%% 画图确认
figure;
plot(signal_int16);
hold on;
for pos = pulse_positions
    xline(pos, 'r--', '脉冲');
end
title('仿真信号');
xlabel('采样点');
ylabel('幅度');
grid on;

fprintf('最大值: %d\n', max(signal_int16));
fprintf('最小值: %d\n', min(signal_int16));
fprintf('脉冲点处的值: %d\n', signal_int16(3050));
fprintf('噪声段均值: %.1f\n', mean(abs(double(signal_int16(1:2000)))));
%% 保存
fid = fopen('D:\sunkukii\desktop\项目相关\测姿LFM\sim_signal.bin', 'wb');
fwrite(fid, signal_int16, 'int16');
fclose(fid);
disp('仿真信号已保存到 C:\sim_signal.bin');
fid = fopen('D:\sim_signal.dat', 'w');
fprintf(fid, '1651\n');
fprintf(fid, '1\n');
fprintf(fid, '1\n');
fprintf(fid, '10002\n');
fprintf(fid, '%d\n', length(signal_int16));
for i = 1:length(signal_int16)
    fprintf(fid, '%04X\n', typecast(signal_int16(i), 'uint16'));
end
fclose(fid);
disp('保存到C:\sim_signal.dat完成');