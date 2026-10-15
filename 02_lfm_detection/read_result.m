% 通道2时延分析
clear all;
%close all;
clc;

% 设置结果文件名
output_filename = 'result.txt'; % 替换为您的文件名

% 读取结果文件
fid = fopen(output_filename, 'r');
if fid == -1
    error('无法打开文件: %s', output_filename);
end

% 读取表头
header_line = fgetl(fid);
disp('文件表头:');
disp(header_line);

% 初始化存储结构
all_delays = [];          % 所有延迟值
packet_sequence = [];     % 数据包序列号 (1, 2, 3...)

% 用于跟踪数据包出现的顺序
packet_map = containers.Map('KeyType', 'double', 'ValueType', 'double');
sequence_counter = 0;
data_point_counter = 0;

% 读取数据行
line_count = 0;
while ~feof(fid)
    line = fgetl(fid);
    if isempty(line)
        continue;
    end
    
    % 解析数据行 (7个字段)
    data_fields = sscanf(line, '%d %d %d %f %f %f %f');
    if numel(data_fields) < 7
        warning('跳过无效行: %s', line);
        continue;
    end
    
    % 提取字段 (新文件结构)
    block_id = data_fields(1);   % 对应Block
    channel = data_fields(2);     % 对应Channel
    peak_id = data_fields(3);     % 对应PeakID
    dela = data_fields(4);        % 对应Delay
    amp = data_fields(5);         % 对应Amp
    wid = data_fields(6);         % 对应Wid
    ratio = data_fields(7);       % 对应InOutRatio
    
    % 只处理通道2的数据点
    if channel == 1
        % 检查该数据包是否已记录
        if ~isKey(packet_map, block_id)
            % 新数据包，分配序列号
            sequence_counter = sequence_counter + 1;
            packet_map(block_id) = sequence_counter;
        end
        
        % 获取数据包序列号
        seq = packet_map(block_id);
        
        % 记录数据点
        data_point_counter = data_point_counter + 1;
        packet_sequence(data_point_counter) = seq;
        all_delays(data_point_counter) = dela;
    end
    
    line_count = line_count + 1;
end

fclose(fid);
fprintf('成功读取 %d 行数据\n', line_count);
fprintf('提取了 %d 个数据包\n', sequence_counter);
fprintf('提取了 %d 个通道2的数据点\n', data_point_counter);

% 创建绘图
figure;plot(packet_sequence, all_delays, 'b.', 'MarkerSize', 15);
title('新挑峰结果');
grid on;

% 添加标签和标题
xlabel('帧号','FontSize',30);
ylabel('时延 (秒)','FontSize',30);
set(gca,'FontSize',20);