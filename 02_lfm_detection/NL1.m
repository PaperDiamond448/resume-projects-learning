%% 基于拷贝相关的时延估计精度仿真
% 评价指标：平均绝对时延误差 MAE = (1/N) * sum|tau_hat_i - tau_0|
% 方法：拷贝相关 + Hilbert包络 + 双门限检测
% 检测成功判据：时延误差 <= 50us
% 检测阈判定：连续满足法

% clear; clc; close all;

%% ===================== 参数设置 =====================
f_start     = 75e3;
bandwidth   = 2e3;
pulse_width = 10e-3;
fs          = 500e3;
Ts          = 1/fs;
Ts_us       = Ts * 1e6;

% tau_0设为整数采样点
delay_samples_true = 5000;
tau_0              = delay_samples_true / fs;   % = 10ms

N              = 500;          % 蒙特卡洛次数
MAE_limit      = 50;           % 精度指标 (μs)
consecutive    = 3;            % 连续满足点数
multinum_k     = 2.5;          % 自适应递推门限倍数
abs_gate_k     = 2.5;          % 绝对门限倍数
alpha_gate     = 0.001;        % 自适应门限更新系数
forget_gate    = 1 - alpha_gate;
delay_tol_us   = 50.0;         % 检测成功容差 (μs)
delay_tol_samp = max(1, round(delay_tol_us * 1e-6 * fs));

SNR_dB = -25:1:15;
N_snr  = length(SNR_dB);

fprintf('========== 仿真参数 ==========\n');
fprintf('fs=%.0fkHz, 采样间隔=%.2fus\n', fs/1e3, Ts_us);
fprintf('f_start=%.0fkHz, B=%.0fkHz, T=%.0fms\n', f_start/1e3, bandwidth/1e3, pulse_width*1e3);
fprintf('tau_0=%.2fms (%d点)\n', tau_0*1e3, delay_samples_true);
fprintf('蒙特卡洛次数N=%d, 精度指标MAE<=%dμs\n\n', N, MAE_limit);

%% ===================== 生成LFM信号（实数）=====================
t_tx     = (0 : Ts : pulse_width - Ts);
N_signal = length(t_tx);
k        = bandwidth / pulse_width;   % 调频率

% 实数LFM拷贝信号
s_copy   = cos(2*pi * (f_start .* t_tx + 0.5 * k .* t_tx.^2));
Ps       = mean(s_copy.^2);          % 信号功率

%% ===================== 预计算 =====================
% 帧长 = 脉冲长度 + 真实时延 + 余量
N_rx   = N_signal + delay_samples_true + round(10e-3 * fs);
N_corr = N_rx + N_signal - 1;
N_fft  = 2^nextpow2(N_corr);

% 拷贝相关：卷积形式 conv(x, flipud(s))
% 频域：FFT{x} × FFT{flipud(s)}
h_ref   = flipud(s_copy(:));          % 翻转拷贝信号（列向量）
H_ref   = fft(h_ref, N_fft);         % 预计算频域匹配滤波器

% 有效时延搜索范围
delay_axis_samp = (1:N_corr) - N_signal;   % 各点对应的时延采样数
valid_lag  = delay_axis_samp >= 0 & delay_axis_samp <= round(20e-3*fs);
valid_idx  = find(valid_lag);

% 真实峰值位置（相关输出索引）
true_peak_idx = delay_samples_true + N_signal;

% 搜索范围最大误差
max_err = 20e-3;   % 20ms

%% ===================== 蒙特卡洛主循环 =====================
MAE          = zeros(1, N_snr);
MAE_std      = zeros(1, N_snr);
MedianAE     = zeros(1, N_snr);
P95AE        = zeros(1, N_snr);
MeanErr      = zeros(1, N_snr);
Pd           = zeros(1, N_snr);   % 检测概率（误差<=50μs）
OutputPeakNum= zeros(1, N_snr);   % 有输出峰次数

fprintf('%-10s  %-12s  %-12s  %-12s  %-12s  %-10s\n', ...
    'SNR(dB)', 'MAE(μs)', 'Median(μs)', 'P95(μs)', 'STD(μs)', 'Pd(%)');
fprintf('%s\n', repmat('-',1,72));

for snr_idx = 1:N_snr

    snr_db      = SNR_dB(snr_idx);
    noise_power = Ps / (10^(snr_db/10));   % 实数噪声功率

    abs_errors       = zeros(1, N);
    detect_success   = false(1, N);
    delay_err_all    = nan(1, N);
    n_output_peak    = 0;

    for i = 1:N

        %% 1. 构造接收信号（实数）r(t) = s(t-tau_0) + n(t)
        rx_clean = zeros(1, N_rx);
        rx_clean(delay_samples_true+1 : delay_samples_true+N_signal) = s_copy;

        % 实数高斯白噪声
        noise = sqrt(noise_power) * randn(1, N_rx);
        r     = rx_clean + noise;

        %% 2. 拷贝相关 + Hilbert包络
        % 频域卷积：conv(r, flipud(s))
        R_fft   = fft(r(:), N_fft);
        r_full  = ifft(R_fft .* H_ref, N_fft, 'symmetric');
        r_corr  = real(r_full(1:N_corr));

        % Hilbert变换取包络（比直接取abs更平滑）
        mainwav = abs(hilbert(r_corr));

        %% 3. 双门限 + 局部极大值检测

        % 初始门限：用有效范围前10%估计噪声背景
        n_init   = min(3000, max(100, floor(0.10*length(valid_idx))));
        init_idx = valid_idx(1:n_init);
        meant    = mean(mainwav(init_idx));
        medt     = median(mainwav(init_idx));

        tgate = meant;               % 自适应门限初值
        gate  = abs_gate_k * medt;   % 绝对门限

        candidate_idx = [];
        candidate_val = [];

        for n = 2 : N_corr-1

            % 自适应门限递推更新
            tgate = tgate * forget_gate + mainwav(n) * alpha_gate;

            % 只在有效时延范围内搜索
            if ~valid_lag(n)
                continue;
            end

            % 双门限判决
            cond_gate = mainwav(n) > gate && ...
                        mainwav(n) > multinum_k * tgate;

            % 局部极大值
            cond_peak = mainwav(n) > mainwav(n-1) && ...
                        mainwav(n) > mainwav(n+1);

            if cond_gate && cond_peak
                candidate_idx(end+1) = n;   %#ok<SAGROW>
                candidate_val(end+1) = mainwav(n); %#ok<SAGROW>
            end
        end

        %% 4. 无插值：峰值位置换算时延
        if isempty(candidate_idx)
            % 无检测峰，记最大误差
            abs_errors(i) = max_err;
            continue;
        end

        % 多候选峰取最大峰
        [~, imax]      = max(candidate_val);
        peak_idx       = candidate_idx(imax);
        n_output_peak  = n_output_peak + 1;

        % 时延换算（相关峰位置 - 拷贝信号长度 = 时延采样数）
        delay_est_samp = peak_idx - N_signal;
        tau_hat_i      = delay_est_samp / fs;

        %% 5. 误差统计
        delay_err_samp        = delay_est_samp - delay_samples_true;
        delay_err_all(i)      = delay_err_samp / fs * 1e6;   % us
        abs_errors(i)         = abs(tau_hat_i - tau_0);

        % 检测成功判据：时延误差 <= 50μs
        if abs(delay_err_samp) <= delay_tol_samp
            detect_success(i) = true;
        end
    end

    %% MAE及各统计量计算
    MAE(snr_idx)     = (1/N) * sum(abs_errors);
    MAE_std(snr_idx) = std(abs_errors);
    Pd(snr_idx)      = sum(detect_success) / N * 100;
    OutputPeakNum(snr_idx) = n_output_peak;

    % 只对有输出峰的实验统计中位数、P95、带符号误差
    idx_out = ~isnan(delay_err_all);
    if any(idx_out)
        abs_err_us           = abs(delay_err_all(idx_out));
        MedianAE(snr_idx)    = median(abs_err_us);
        P95AE(snr_idx)       = prctile(abs_err_us, 95);
        MeanErr(snr_idx)     = mean(delay_err_all(idx_out));
    end

    fprintf('%-10.1f  %-12.4f  %-12.4f  %-12.4f  %-12.4f  %-10.1f\n', ...
        snr_db, MAE(snr_idx)*1e6, MedianAE(snr_idx), ...
        P95AE(snr_idx), MAE_std(snr_idx)*1e6, Pd(snr_idx));
end

%% ===================== CRLB 理论下界 =====================
TBP   = bandwidth * pulse_width;
B_rms = bandwidth / sqrt(3);
CRLB  = zeros(1, N_snr);
for snr_idx = 1:N_snr
    snr_eff        = 10^(SNR_dB(snr_idx)/10) * TBP;
    CRLB(snr_idx)  = 1 / (2*pi * B_rms * sqrt(2*snr_eff));
end

%% ===================== 连续满足法确定检测阈 =====================
fprintf('\n%s\n', repmat('=',1,72));
fprintf('【检测阈判定】连续%d点满足 MAE <= %dμs\n', consecutive, MAE_limit);
fprintf('%s\n', repmat('-',1,72));

meet_idx = [];
for idx = 1 : N_snr - consecutive + 1
    if all(MAE(idx:idx+consecutive-1)*1e6 <= MAE_limit)
        meet_idx = idx;
        break;
    end
end

if ~isempty(meet_idx)
    snr_threshold = SNR_dB(meet_idx);
    fprintf('✔ 检测阈 = %d dB\n', snr_threshold);
    fprintf('  该点 MAE   = %.4f μs\n', MAE(meet_idx)*1e6);
    fprintf('  该点 Pd    = %.1f%%\n',  Pd(meet_idx));
    fprintf('  连续满足区间: [%d dB, %d dB]\n', ...
        SNR_dB(meet_idx), SNR_dB(meet_idx+consecutive-1));
else
    snr_threshold = NaN;
    fprintf('✘ 测试SNR范围内未找到满足指标的检测阈\n');
end

%% ===================== 绘图 =====================
figure('Name','拷贝相关时延估计MAE与检测概率', ...
       'Position',[80,80,960,620],'Color','w');

%--- 左纵轴：MAE曲线 ---
yyaxis left;
hold on;

plot(SNR_dB, MAE*1e6, 'b-o', 'LineWidth', 2, ...
    'MarkerSize', 7, 'MarkerFaceColor',[0.15 0.35 0.8], ...
    'DisplayName','MAE');

plot(SNR_dB, MedianAE, 'b--^', 'LineWidth', 1.5, ...
    'MarkerSize', 6, 'MarkerFaceColor',[0.4 0.6 1.0], ...
    'DisplayName','中位绝对误差');

plot(SNR_dB, P95AE, 'b:s', 'LineWidth', 1.5, ...
    'MarkerSize', 6, 'MarkerFaceColor',[0.7 0.8 1.0], ...
    'DisplayName','95%分位误差');

% 50μs指标线
yline(MAE_limit, 'k-.', 'LineWidth', 1.8, ...
    'DisplayName', sprintf('精度指标: %dμs', MAE_limit));
text(SNR_dB(1)+0.3, MAE_limit*1.08, ...
    sprintf('精度指标: %dμs', MAE_limit), ...
    'FontSize', 10, 'Color', 'k', 'FontWeight', 'bold');

% 检测阈竖线
if ~isnan(snr_threshold)
    xline(snr_threshold, 'r-', 'LineWidth', 2, ...
        'DisplayName', sprintf('检测阈: %ddB', snr_threshold));
    plot(snr_threshold, MAE_limit, 'rp', ...
        'MarkerSize', 15, 'MarkerFaceColor', 'r', ...
        'HandleVisibility', 'off');
    text(snr_threshold+0.3, max(MAE*1e6)*0.55, ...
        sprintf('检测阈\n= %ddB', snr_threshold), ...
        'FontSize', 11, 'Color', 'r', 'FontWeight', 'bold', ...
        'BackgroundColor',[1 0.95 0.95], 'EdgeColor','r', 'Margin',4);
end

ylabel('绝对时延误差 (μs)', 'FontSize', 12, 'FontWeight', 'bold');
ylim([0, max(MAE*1e6)*1.15]);
set(gca, 'YColor', 'b');

%--- 右纵轴：检测概率曲线 ---
yyaxis right;

plot(SNR_dB, Pd, 'g-s', 'LineWidth', 2, ...
    'MarkerSize', 7, 'MarkerFaceColor',[0.1 0.7 0.2], ...
    'DisplayName','检测概率 Pd');

% 标注 -20dB 处检测概率
snr_mark = -20;
idx_mark = find(SNR_dB == snr_mark, 1);
if ~isempty(idx_mark)
    dr_mark = Pd(idx_mark);
    plot(snr_mark, dr_mark, 'ro', ...
        'MarkerSize', 12, 'MarkerFaceColor', 'r', ...
        'HandleVisibility', 'off');
    text(snr_mark+0.3, dr_mark-8, ...
        sprintf('-20dB: Pd=%.1f%%', dr_mark), ...
        'FontSize', 11, 'Color', 'r', 'FontWeight', 'bold', ...
        'BackgroundColor',[1 0.95 0.95], 'EdgeColor','r', 'Margin',3);
end

ylabel('检测概率 Pd (%)', 'FontSize', 12, 'FontWeight', 'bold');
ylim([0, 115]);
set(gca, 'YColor', [0.1 0.6 0.1]);

%--- 公共设置 ---
xlabel('输入信噪比 (dB)', 'FontSize', 12, 'FontWeight', 'bold');
title('基于拷贝相关的时延估计精度与检测概率', ...
    'FontSize', 13, 'FontWeight', 'bold');
legend('Location','east', 'FontSize', 10, 'Box','on');
grid on;
xlim([SNR_dB(1)-1, SNR_dB(end)+1]);
set(gca, 'FontSize', 11, 'GridAlpha', 0.3);

%% ===================== 汇总表 =====================
fprintf('\n%-10s  %-12s  %-12s  %-12s  %-12s  %-10s  %s\n', ...
    'SNR(dB)','MAE(μs)','Median(μs)','P95(μs)','STD(μs)','Pd(%)','达标');
fprintf('%s\n', repmat('=',1,85));
for i = 1:N_snr
    flag = '';
    if MAE(i)*1e6 <= MAE_limit
        if ~isnan(snr_threshold) && i == meet_idx
            flag = '✔ ← 检测阈';
        else
            flag = '✔';
        end
    end
    fprintf('%-10.1f  %-12.4f  %-12.4f  %-12.4f  %-12.4f  %-10.1f  %s\n', ...
        SNR_dB(i), MAE(i)*1e6, MedianAE(i), P95AE(i), ...
        MAE_std(i)*1e6, Pd(i), flag);
end

fprintf('\n%s\n', repmat('=',1,85));
if ~isnan(snr_threshold)
    fprintf('检测阈（连续%d点MAE<=%dμs）= %d dB，该点Pd=%.1f%%\n', ...
        consecutive, MAE_limit, snr_threshold, Pd(meet_idx));
end