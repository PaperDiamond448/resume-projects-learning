clc;
clear all;
close all;
%% 拆包
FS = 156250;
load('signal_all_35_50.mat');
num = 1;%水听器号
sig_all = signal_all(num).sig;
syn_all = signal_all(num).syn;
figure,
plot((0:length(sig_all)-1)/FS,sig_all);hold on;
plot((0:length(syn_all)-1)/FS,syn_all*32768);hold on;
xlabel('时间/s');ylabel('幅度');
syn_idx =[]; %syn_idx 中保存的就是所有值为 1 的元素的位置
for i = 1:length(syn_all)
if (syn_all(i) == 1)
    syn_i = i;
    syn_idx = [syn_idx syn_i];
end
end

segment_indices = ceil(syn_idx / 16384);        % 所属段号
offsets = syn_idx - (segment_indices - 1) * 16384; % 段内位置
sync_map = containers.Map(segment_indices, offsets);

fid = fopen('result.txt', 'w');
if fid ~= -1 %处理文件操作的标准安全模式
    fclose(fid);
end

%% 信号参数设置  
f = 10e3;          % 初始频率 10kHz  
T = 20e-3;         % 脉宽 20ms  
B = 2e3;          % 带宽 10kHz  
Fs = 156.25e3;        % 采样频率 150kHz  

%初始化常量  
Dn = 16;           %降采样因子 
NumFft = 2048;     %FFT点数
NumSigr = 1024;    %接收信号长度
NbandLen = 128;    %带通滤波器长度
DATA_LEN = 16384;  %一包长度
% 初始化数组
IN = zeros(1,2048+142);        % 输入数据缓冲区
band = zeros(1,2048+15);       % 带通滤波器数据缓冲区
orth = zeros(1,2048);          % 正交滤波器数据缓冲区
freq2 =[];
yn_all =[];
tig = zeros(1,1024+1023);
BAND_COEF =zeros(1,NbandLen);
mainwav = zeros(1,1024+500);

%指令门限参数初始化
powergate  = 30*0.001/3.3*65535*(T*Fs/Dn);
agate      = powergate^2; %保护门限
temgate    = 100;        %自适应门限初始化
corwingate = zeros(1,1024+100);%输入输出比门限
wav1       = zeros(1,300);%重叠保留过渡数组
inoutgate  = 0.3;           %输入输出比门限
multinum_k = 2;           %自适应门限倍数
timenum    = 65535;       %同步位置初始化

% 用于测试存储
count_cor = 1;
band_l = [];
orth_l = [];
tig_l = [];
%tgate2 =[];
%mainwav2 = [];
%corwingate_l =[];
%tig_test =[];
k_m = 1;
cout_t = 1;
% 初始化结构体数组
num_number = 1;
m = 200; % 每个通道最多可能有 200 个峰值
% for i = 1:num_number
%     peak(i).sta = zeros(1, m);
%     peak(i).max = zeros(1, m);
%     peak(i).maxl = zeros(1, m);
%     peak(i).maxr = zeros(1, m);
%     peak(i).wide = zeros(1, m);
%     peak(i).Ain = zeros(1, m);
%     peak(i).num = 0; % 初始时没有记录任何峰值
% end

for i = 1:num_number
    peaked(i).sta = zeros(1, m);
    peaked(i).max = zeros(1, m);
    peaked(i).maxl = zeros(1, m);
    peaked(i).maxr = zeros(1, m);
    peaked(i).wide = zeros(1, m);
    peaked(i).Ain = zeros(1, m);
    peaked(i).num = 0; % 初始时没有记录任何峰值
end

% 生成本地LFM信号 
fs =Fs/Dn;  
Nc = round(T * fs); % 信号点数
% 拷贝信号超长保护
NumSigr = 1024;
Nl = NumSigr - Nc; % 左侧零填充点数
t = 0:1/fs:T-1/fs;  
k = B/T;           % 线性调频斜率
nowtime = 0;
s1 = sin(2*pi*f*t+pi*k*t.^2);
s2= cos(2*pi*f*t+pi*k*t.^2);
s = s1+1j*s2;
s =[zeros(1,Nl),s];
%s1 = [fliplr(s),zeros(1,Nl)];
sc_square(num_number) = 0;
for count = 1:NumSigr
    sc_square(num_number) = sc_square(num_number)+real(s(count)).^2;
end
%sc_square = sc_square/2;


% 定义滤波器系数
Bt=f*6.0/1500;                %多普勒频率偏移
wl=2*pi*(f-Bt)/Fs;             %低频截止频率
wh=2*pi*(f+B+Bt)/Fs;           %高频截止频率
for count=1:NbandLen
   BAND_COEF(count+1)=(32767*(0.54-0.46*cos(2*pi*count/(NbandLen-1)))*(sin((count-(NbandLen+1)/2.0)*wh)-sin((count-(NbandLen+1)/2.0)*wl))/pi/(count-(NbandLen+1)/2.0));
end
orth_coef1 = [-212,-21,-336,-1298,796,-2963,-3279,17011,17011,-3279,-2963,796,-1298,-336,-21,-212]; % 正交滤波器系数1
orth_coef2 = [131,407,30,1519,1912,423,12247,11972,-11972,-12247,-423,-1912,-1519,-30,-407,-131];   % 正交滤波器系数2

%% 构造理论输入信号 
tc = 0:1/Fs:T; 
sc = sin(2*pi*f*tc+pi*k*tc.^2);   %接收信号
delay = 40e-3;
delay_samples = round(delay * Fs);%理论时延点数  
PP = zeros(1,2048*4*4);
%sc_1 = [zeros(1,delay_samples) sc zeros(1,delay_samples) sc zeros(1,delay_samples) sc zeros(1,delay_samples)];
sc_1 = [zeros(1,delay_samples) sc zeros(1,delay_samples)];
PP(1:length(sc_1))=sc_1;

PP = sig_all;
final_result.delay = 0;
final_result.Amp = 0;
final_result.num = 0;
time_all= 0;

L_sig = length(PP);
% 计算总段数
num_segments = ceil(L_sig / DATA_LEN);
% 创建全零矩阵
segmented_data = zeros(num_segments, DATA_LEN);
% 计算有效数据范围
valid_length = min(DATA_LEN * num_segments, L_sig);
segmented_data(1:num_segments, 1:DATA_LEN) = ...
    reshape([PP(1:valid_length), zeros(1,DATA_LEN * num_segments - valid_length)], ...
    DATA_LEN, num_segments)';

PP_copy = zeros(1, 142);
%% 信号分段重叠保留
for block_num = 1:num_segments
    for cnt=1:1023%保留M-1个点
	    tig(cnt)=tig(cnt+1024);
    end
    % 提取当前段
   if sync_map.isKey(block_num)
        %timenum = 16380;      %每16384点中的同步位置
        timenum = sync_map(block_num);  % 获取段内同步位置
        fprintf('在段 %d 检测到同步位置 %d\n', block_num, timenum);
    end
    current_segment = segmented_data(block_num, :);%获取当前段
    if block_num == 1
        PP_1 = [zeros(1, 142), current_segment];
    else
        PP_1 = [PP_copy, current_segment];
    end
    
    % 更新PP_copy为当前段的最后142个点（用于下一段）
    PP_copy = current_segment(end-142+1:end);

    %%算均值标准差
    meant = mean(PP_1(:));
    stdt = std(PP_1(:));
    %% 重叠保留滤波
    % 将 PP 数组元素放入 IN 数组
    for cnt2 = 1:8
        for cnt = 1:(2048 + 142)
            IN(cnt) = PP_1(2048*(cnt2-1) + cnt)-meant; % 重叠保留后滤波
        end
          % 带通滤波
        band = fix(conv(BAND_COEF,IN)/32768);
        band = band(128:2048+143); %保留2048+15点
        % 正交滤波降采样
        orth1 = fix(conv(orth_coef1,band)/32767);%  正交滤波
        orth2 = fix(conv(orth_coef2,band)/32767);%  正交滤波
        orth = orth1+1j*orth2;%  正交滤波
        orth = orth(16:2048+15); % 保留2048点
        for cnt = 0:127
		      tig(1023+cnt+1+(cnt2-1)*128)=orth(Dn*cnt+1);
        end

        %band_l = [band_l,band(1:2048)];
        %orth_l = [orth_l,orth];
    end
        %tig_l = [tig_l,tig(1024:end)];
   %% 计算输入输出比门限
    T0num=floor(T*Fs/Dn); 
    for count=1:100
        corwingate(num_number,count) = corwingate(num_number,1024+count);
    end
    %gatewin = corwingate(num_number,1024+100);
    sum_gate = 0;
    for count=1:1024
        if count == 1
	        for i = 1:T0num
		        sum_gate = sum_gate + real(tig(1023-T0num+i))^2;
            end
        else
            sum_gate = sum_gate-real(tig(1023-T0num+count))^2+real(tig(1023-T0num+count+T0num))^2;
        end
        gatewin = sum_gate;
        corwingate(num_number,count+100)=sqrt(gatewin)*sqrt(sc_square)*2*inoutgate;
        corwingate_l(count_cor) = corwingate(num_number,count+100);
        count_cor = count_cor + 1;
    end
    corwingate(num_number,1024+100)=gatewin;
    %% 进行拷贝相关
    % 频域拷贝相关 
    freq1=[tig,zeros(1,2048-length(tig))];
    S = fft(s,NumFft);  %2048点FFT
    Rx = fft(freq1,NumFft);  
    corr_freq = Rx.* conj(S); % 点乘共轭以计算频域的相关性  
    freq2 = ifft(corr_freq);
    freq2 = freq2(1:1024);
    %yn_all = [yn_all,freq2];%将freq存储便于分析
    %figure;plot(abs(yn_all).^2);
    %% 以下为门限部分
    %% mycor函数，计算门限
    means=meant;
    
    for count=1:1024
	    mainwav(300+count)=abs(freq2(count)).^2;%相关结果取平方存入mainwav
    end
    for count=1:300
        mainwav(count)=wav1(count);%wav1赋值给mainwav前300个点，wav用于存储重叠保留的数据300点
	    wav1(count)=mainwav(count+1024);%mainwav后300个点赋值给wav1,供下次重叠保留使用
    end
    
    mainwav2((k_m-1)*1024+1:k_m*1024) = mainwav(301:1024+300);
    k_m = k_m + 1;
    gate = agate;
    tgate = temgate; % 幅值迭代门限
    Tsysdis = round(timenum / 16);   %降采样后同步位置
    Tsysold = 16*1024;
    offset = floor(T*Fs+71);  %信号脉宽与滤波器偏移量  
    % 检测峰值
      for count = -1:1023 
          if(Tsysdis==count+1)%判断是否为同步位置
              if block_num == 277
              a = block_num;
              end
              [peaksel,peaked] = detpart(1,peaked, block_num,T,inoutgate);
              final_result.delay(final_result.num+1:final_result.num+peaksel.num) = (peaksel.sta(1:peaksel.num)+time_all*Dn);
              final_result.Amp(final_result.num+1:final_result.num+peaksel.num) = peaksel.max(1:peaksel.num);
              final_result.num = final_result.num+peaksel.num;
              time_all = time_all+nowtime;
              fprintf('在段 %d 的同步位置 %d 执行了挑峰操作\n', block_num, timenum);
              timenum=65535;
              nowtime = 0;
          end
          tgate = tgate * 0.999 + mainwav(count + 300) * 0.001; % 迭代自适应门限
          if(count ~= -1)
          tgate2(cout_t) = tgate;
          cout_t = cout_t+1;
          end
          if mainwav(count + 300) > gate && mainwav(count + 300) > tgate*multinum_k % 幅度门限,双门限判决
              if (count ~=1023)&&(mainwav(count + 300) > mainwav(count + 299) && mainwav(count + 300) > mainwav(count + 301)) % 峰的条件
                  ingate = corwingate(count + 100 + 1);
                    
                  if sqrt(mainwav(count + 300)) > ingate % 输入输出比
                      maxtmp = mainwav(count + 300) * 0.5; % 0.9倍脉宽
                      % 查找左侧边界
                      cnt = 1;
                      while cnt < 100 && maxtmp <= mainwav(count - cnt + 300)
                          cnt = cnt + 1;
                      end
                        
                      % 查找右侧边界
                      cnt2 = 1;
                      while cnt2 < 100 && maxtmp <= mainwav(count + cnt2 + 300)
                          cnt2 = cnt2 + 1;
                      end
                      %存数
                      %if count > Tsysdis
                          if peaked(num_number).num >= 200 %大于200不存
                              break;
                          end
                          peaked(num_number).sta(peaked(num_number).num + 1) = (nowtime*16 - offset);
                          %peaked(num_number).sta(peak(num_number).num + 1) = ((count) * 16 - offset-timenum);%(count * 16 - PP(20400) - offset);
                          peaked(num_number).max(peaked(num_number).num + 1) = mainwav(count + 300);%峰值
                          peaked(num_number).maxl(peaked(num_number).num + 1) = mainwav(count + 299);%峰值左侧
                          peaked(num_number).maxr(peaked(num_number).num + 1) = mainwav(count + 301);%峰值右侧
                          peaked(num_number).wide(peaked(num_number).num + 1) = (cnt + cnt2)*Dn;%宽度
                          peaked(num_number).Ain(peaked(num_number).num + 1) = corwingate(num_number,count + 100+1);%输入输出比门限
                          peaked(num_number).num = peaked(num_number).num + 1;%计数器
%                       else
%                           if peak(num_number).num >= 200 %大于200不存
%                               break;
%                           end
%                             
%                           peak(num_number).sta(peak(num_number).num + 1) = (count * 16 - Tsysold - offset);%峰值位置
%                           peak(num_number).max(peak(num_number).num + 1) = mainwav(count + 300);%峰值幅值
%                           peak(num_number).maxl(peak(num_number).num + 1) = mainwav(count + 299);%峰值左侧幅值
%                           peak(num_number).maxr(peak(num_number).num + 1) = mainwav(count + 301);%峰值右侧幅值
%                           peak(num_number).wide(peak(num_number).num + 1) = (cnt + cnt2)*Dn;%峰值宽度
%                           peak(num_number).Ain(peak(num_number).num + 1) = corwingate(num_number,count + 100 +1);
%                           peak(num_number).num = peak(num_number).num + 1;%更新 peak(num) 结构体中已存储的峰值数量
                      end
                  end
              end
          if  (count ~= -1) 
          nowtime = nowtime +1;
          end
      end
      temgate(num_number)=tgate;%重新赋值回门限
end

% yn2 = abs(yn_all).^2;
% % 包络检波解调  
% t_r=(0:length(yn2)-1);
% 
% % 绘图  
% tp = 1:length(PP);
% figure;plot(tp,PP,'r');   
% title('接收信号');  xlabel('时间 (s)');  ylabel('幅度');  grid on;
% 
% t_a=(0:length(yn2)-1)*Dn;
% t_g =(1:length(tgate2))*Dn;
%gate2= gate * ones(1, length(t_a));

% figure;
% subplot(2,1,1);plot(t_a,yn2,'r'); 
% title('拷贝相关');  xlabel('时间 (s)');  ylabel('幅度');  grid on; 
% subplot(2,1,2);plot(t_r,yn2,'r'); 
% title('拷贝相关');  xlabel('采样点');  ylabel('幅度');  grid on; 


figure;plot((0:length(mainwav2)-1)/FS*Dn,sqrt(mainwav2));hold on;
plot((0:length(tgate2)-1)/FS*Dn,sqrt(tgate2*multinum_k));hold on;
plot((0:length(corwingate_l)-1)/FS*Dn,corwingate_l);hold on;
plot((final_result.delay+64)/(FS)+T,sqrt(final_result.Amp),'o');

max_amplitude = max([max(sqrt(mainwav2)), max(corwingate_l)]); % 获取最大幅度值
y_range = [0, max_amplitude * 0.8]; % 创建Y轴范围
for i = 1:length(syn_idx)
    line([syn_idx(i)/FS, syn_idx(i)/FS], y_range, 'Color', 'r', 'LineStyle', '--', 'LineWidth', 1.5);
end

legend('拷贝相关结果','自适应门限','输入输出比'); xlabel('时间/s');  ylabel('幅度');  grid on;

% figure;plot(corwingate_l);hold on;plot(sqrt(mainwav2));


