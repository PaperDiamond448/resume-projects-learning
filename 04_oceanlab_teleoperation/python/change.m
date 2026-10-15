% CHANGE - 更新消声水池模拟的参数并刷新3D图形
% 用于海洋信息实验教学平台，接受Python端传递的参数
% 输入参数：
%   hydrophone_x, hydrophone_y, hydrophone_z: 水听器（接收器）坐标 (m)
%   hydrophone_angle: 水听器角度 (度)
%   transducer_x, transducer_y, transducer_z: 换能器（发射器）坐标 (m)
%   transducer_angle: 换能器角度 (度)
%   water_depth: 水深 (m)

function change(hydrophone_x, hydrophone_y, hydrophone_z, hydrophone_angle, ...
                transducer_x, transducer_y, transducer_z, transducer_angle, water_depth)

    % 定义全局参数
    global pool_length pool_width pool_height
    global cylinder_radius cylinder_height rect_length rect_width rect_height
    global transmitter_pos receiver_pos transmitter_angle receiver_angle
    global ax_3d

    % 确保输入参数为 double 类型
    hydrophone_x = double(hydrophone_x);
    hydrophone_y = double(hydrophone_y);
    hydrophone_z = double(hydrophone_z);
    hydrophone_angle = double(hydrophone_angle);
    transducer_x = double(transducer_x);
    transducer_y = double(transducer_y);
    transducer_z = double(transducer_z);
    transducer_angle = double(transducer_angle);
    water_depth = double(water_depth);

    % 水池参数（固定）
    pool_length = 2; % x方向，2m
    pool_width = 1;  % y方向，1m
    pool_height = 1; % z方向，1m

    % 水听器参数（固定）
    cylinder_radius = 0.02; % 圆柱半径，0.04m直径
    cylinder_height = 0.2;  % 圆柱长度，0.2m
    rect_length = 0.2;      % 长方体长度，0.2m
    rect_width = 0.1;       % 长方体宽度，0.1m
    rect_height = 0.02;     % 长方体高度，0.02m

    % 更新全局变量
    receiver_pos = [hydrophone_x, hydrophone_y, hydrophone_z]; % 水听器坐标
    receiver_angle = hydrophone_angle;                         % 水听器角度
    transmitter_pos = [transducer_x, transducer_y, transducer_z]; % 换能器坐标
    transmitter_angle = transducer_angle;                      % 换能器角度
    global water_depth_global
    water_depth_global = water_depth;                         % 水深

    % 初始化图形（仅在首次调用时创建）
    if isempty(findobj('Type', 'figure', 'Name', '消声水池模拟'))
        fig = figure('Name', '消声水池模拟', 'NumberTitle', 'off', ...
                     'Position', [100, 100, 1200, 600], 'Units', 'normalized');
        ax_3d = axes('Parent', fig, 'Position', [0.1, 0.1, 0.85, 0.8], ...
                     'Units', 'normalized');
        hold(ax_3d, 'on');
    else
        % 复用现有图形
        fig = findobj('Type', 'figure', 'Name', '消声水池模拟');
        ax_3d = findobj(fig, 'Type', 'axes');
        if isempty(ax_3d)
            ax_3d = axes('Parent', fig, 'Position', [0.1, 0.1, 0.85, 0.8], ...
                         'Units', 'normalized');
            hold(ax_3d, 'on');
        end
    end

    % 绘制水池壁面（仅首次绘制）
    if isempty(findobj(ax_3d, 'Tag', 'Pool'))
        vertices_pool = [
            -1 -0.5 0; 1 -0.5 0; 1 0.5 0; -1 0.5 0; % 底部
            -1 -0.5 1; 1 -0.5 1; 1 0.5 1; -1 0.5 1  % 顶部
        ];
        faces_pool = [
            1 2 3 4; 5 6 7 8; 1 2 6 5; 2 3 7 6; 3 4 8 7; 4 1 5 8
        ];
        patch(ax_3d, 'Vertices', vertices_pool, 'Faces', faces_pool, ...
              'FaceColor', [0.5 0.5 0.5], 'FaceAlpha', 0.3, 'EdgeColor', 'k', ...
              'Tag', 'Pool');
    end

    % 更新水面
    delete(findobj(ax_3d, 'Tag', 'Water'));
    patch(ax_3d, ...
          'Vertices', [-1 -0.5 water_depth; 1 -0.5 water_depth; 1 0.5 water_depth; -1 0.5 water_depth], ...
          'Faces', [1 2 3 4], 'FaceColor', [0.2 0.6 0.8], 'FaceAlpha', 0.7, ...
          'EdgeColor', 'none', 'Tag', 'Water');

    % 绘制滑轨
    draw_sliding_rails(ax_3d);

    % 绘制水听器和换能器
    draw_hydrophone(ax_3d, transmitter_pos, transmitter_angle, cylinder_radius, ...
                    cylinder_height, rect_length, rect_width, rect_height, 'Transmitter', [1 0 0]);
    draw_hydrophone(ax_3d, receiver_pos, receiver_angle, cylinder_radius, ...
                    cylinder_height, rect_length, rect_width, rect_height, 'Receiver', [0 1 0]);

    % 绘制滑块
    draw_slider(ax_3d, transmitter_pos, 'TransmitterSlider', [0.8 0.8 0.8]);
    draw_slider(ax_3d, receiver_pos, 'ReceiverSlider', [0.8 0.8 0.8]);

    % 绘制手摇装置
    draw_handwheel(ax_3d, transmitter_pos, 'TransmitterHandwheel', [0.5 0.5 0.5]);
    draw_handwheel(ax_3d, receiver_pos, 'ReceiverHandwheel', [0.5 0.5 0.5]);

    % 绘制连接杆
    draw_connecting_rod(ax_3d, transmitter_pos, 'TransmitterRod', [0.7 0.7 0.7]);
    draw_connecting_rod(ax_3d, receiver_pos, 'ReceiverRod', [0.7 0.7 0.7]);

    % 设置光照和视角
    light(ax_3d, 'Position', [0 0 2], 'Style', 'local');
    lighting(ax_3d, 'gouraud');
    material shiny;
    view(ax_3d, 30, 30);
    axis(ax_3d, 'equal');
    xlim(ax_3d, [-1 1]);
    ylim(ax_3d, [-0.5 0.5]);
    zlim(ax_3d, [0 1.5]);
    xlabel(ax_3d, 'X (m)'); ylabel(ax_3d, 'Y (m)'); zlabel(ax_3d, 'Z (m)');
    grid(ax_3d, 'on');

    % 强制刷新图形
    drawnow;
end

% 函数定义（从原始 change.m 保留）
function draw_sliding_rails(ax)
    global pool_length pool_width
    rail_width = 0.05; % 滑轨宽度（y 方向）
    rail_height = 0.05; % 滑轨高度（z 方向）
    rail_y_offset = 0.025; % 滑轨在 y 方向的偏移（以 y=0 为对称中心，y=±0.025）
    rail_z_pos = 1.05; % 滑轨的 z 位置
    
    % 删除旧的滑轨
    delete(findobj(ax, 'Tag', 'Rail1'));
    delete(findobj(ax, 'Tag', 'Rail2'));
    
    % 滑轨的起点和终点（沿 x 方向）
    x_start = -pool_length/2; % -1
    x_end = pool_length/2;    % 1
    
    % 绘制第一根滑轨（y = -rail_y_offset）
    vertices_rail1 = [
        x_start, -rail_y_offset-rail_width/2, rail_z_pos-rail_height/2;
        x_end, -rail_y_offset-rail_width/2, rail_z_pos-rail_height/2;
        x_end, -rail_y_offset+rail_width/2, rail_z_pos-rail_height/2;
        x_start, -rail_y_offset+rail_width/2, rail_z_pos-rail_height/2;
        x_start, -rail_y_offset-rail_width/2, rail_z_pos+rail_height/2;
        x_end, -rail_y_offset-rail_width/2, rail_z_pos+rail_height/2;
        x_end, -rail_y_offset+rail_width/2, rail_z_pos+rail_height/2;
        x_start, -rail_y_offset+rail_width/2, rail_z_pos+rail_height/2
    ];
    faces_rail1 = [
        1 2 3 4; 5 6 7 8; 1 2 6 5; 2 3 7 6; 3 4 8 7; 4 1 5 8
    ];
    patch(ax, 'Vertices', vertices_rail1, 'Faces', faces_rail1, ...
          'FaceColor', [0.7 0.7 0.7], 'EdgeColor', 'none', 'Tag', 'Rail1'); % 灰色
    
    % 绘制第二根滑轨（y = rail_y_offset）
    vertices_rail2 = [
        x_start, rail_y_offset-rail_width/2, rail_z_pos-rail_height/2;
        x_end, rail_y_offset-rail_width/2, rail_z_pos-rail_height/2;
        x_end, rail_y_offset+rail_width/2, rail_z_pos-rail_height/2;
        x_start, rail_y_offset+rail_width/2, rail_z_pos-rail_height/2;
        x_start, rail_y_offset-rail_width/2, rail_z_pos+rail_height/2;
        x_end, rail_y_offset-rail_width/2, rail_z_pos+rail_height/2;
        x_end, rail_y_offset+rail_width/2, rail_z_pos+rail_height/2;
        x_start, rail_y_offset+rail_width/2, rail_z_pos+rail_height/2
    ];
    faces_rail2 = [
        1 2 3 4; 5 6 7 8; 1 2 6 5; 2 3 7 6; 3 4 8 7; 4 1 5 8
    ];
    patch(ax, 'Vertices', vertices_rail2, 'Faces', faces_rail2, ...
          'FaceColor', [0.7 0.7 0.7], 'EdgeColor', 'none', 'Tag', 'Rail2'); % 灰色
end

function draw_slider(ax, pos, tag, color)
    slider_length = 0.2; % 滑块长度（x 方向）
    slider_width = 0.2; % 滑块宽度（y 方向，调整为更窄以适应 y=±0.025 的滑轨）
    slider_height = 0.05; % 滑块高度（z 方向）
    slider_z_pos = 1.05; % 滑块的 z 位置（与滑轨对齐）
    hole_width = 0.06; % 滑轨孔的宽度（y 方向，略大于滑轨宽度 0.05）
    hole_height = 0.06; % 滑轨孔的高度（z 方向，略大于滑轨高度 0.05）
    
    % 删除旧的滑块
    delete(findobj(ax, 'Tag', tag));
    
    % 外框顶点（一个扁平的矩形框）
    outer_vertices = [
        pos(1)-slider_length/2, pos(2)-slider_width/2, slider_z_pos-slider_height/2;
        pos(1)+slider_length/2, pos(2)-slider_width/2, slider_z_pos-slider_height/2;
        pos(1)+slider_length/2, pos(2)+slider_width/2, slider_z_pos-slider_height/2;
        pos(1)-slider_length/2, pos(2)+slider_width/2, slider_z_pos-slider_height/2;
        pos(1)-slider_length/2, pos(2)-slider_width/2, slider_z_pos+slider_height/2;
        pos(1)+slider_length/2, pos(2)-slider_width/2, slider_z_pos+slider_height/2;
        pos(1)+slider_length/2, pos(2)+slider_width/2, slider_z_pos+slider_height/2;
        pos(1)-slider_length/2, pos(2)+slider_width/2, slider_z_pos+slider_height/2
    ];
    
    % 内框顶点（矩形孔的边界）
    inner_offset_x = 0.05; % x 方向内框偏移
    inner_offset_y = (slider_width - hole_width) / 2; % y 方向内框偏移，确保孔居中
    inner_vertices = [
        pos(1)-slider_length/2+inner_offset_x, pos(2)-hole_width/2, slider_z_pos-hole_height/2;
        pos(1)+slider_length/2-inner_offset_x, pos(2)-hole_width/2, slider_z_pos-hole_height/2;
        pos(1)+slider_length/2-inner_offset_x, pos(2)+hole_width/2, slider_z_pos-hole_height/2;
        pos(1)-slider_length/2+inner_offset_x, pos(2)+hole_width/2, slider_z_pos-hole_height/2;
        pos(1)-slider_length/2+inner_offset_x, pos(2)-hole_width/2, slider_z_pos+hole_height/2;
        pos(1)+slider_length/2-inner_offset_x, pos(2)-hole_width/2, slider_z_pos+hole_height/2;
        pos(1)+slider_length/2-inner_offset_x, pos(2)+hole_width/2, slider_z_pos+hole_height/2;
        pos(1)-slider_length/2+inner_offset_x, pos(2)+hole_width/2, slider_z_pos+hole_height/2
    ];
    
    % 合并顶点
    vertices_slider = [outer_vertices; inner_vertices];
    
    % 面（连接外框和内框，形成环形）
    faces_slider = [
        1 2 10 9; 2 3 11 10; 3 4 12 11; 4 1 9 12; % 底面
        5 6 14 13; 6 7 15 14; 7 8 16 15; 8 5 13 16; % 顶面
        1 5 13 9; 2 6 14 10; 3 7 15 11; 4 8 16 12 % 侧面
    ];
    
    % 绘制滑块（环形结构）
    patch(ax, 'Vertices', vertices_slider, 'Faces', faces_slider, ...
          'FaceColor', color, 'EdgeColor', 'k', 'Tag', tag);
end

function draw_handwheel(ax, pos, tag, color)
    handwheel_length = 0.1; % 手摇装置长度
    handwheel_width = 0.1; % 手摇装置宽度
    handwheel_height = 0.2; % 手摇装置高度
    slider_z_pos = 1.05; % 滑块的 z 位置
    slider_height = 0.05; % 滑块高度
    handwheel_z_pos = slider_z_pos + slider_height/2 + handwheel_height/2; % 手摇装置的 z 位置（在滑块上方）
    
    % 删除旧的手摇装置
    delete(findobj(ax, 'Tag', tag));
    
    % 绘制手摇装置
    vertices_handwheel = [
        pos(1)-handwheel_length/2, pos(2)-handwheel_width/2, handwheel_z_pos-handwheel_height/2;
        pos(1)+handwheel_length/2, pos(2)-handwheel_width/2, handwheel_z_pos-handwheel_height/2;
        pos(1)+handwheel_length/2, pos(2)+handwheel_width/2, handwheel_z_pos-handwheel_height/2;
        pos(1)-handwheel_length/2, pos(2)+handwheel_width/2, handwheel_z_pos-handwheel_height/2;
        pos(1)-handwheel_length/2, pos(2)-handwheel_width/2, handwheel_z_pos+handwheel_height/2;
        pos(1)+handwheel_length/2, pos(2)-handwheel_width/2, handwheel_z_pos+handwheel_height/2;
        pos(1)+handwheel_length/2, pos(2)+handwheel_width/2, handwheel_z_pos+handwheel_height/2;
        pos(1)-handwheel_length/2, pos(2)+handwheel_width/2, handwheel_z_pos+handwheel_height/2
    ];
    faces_handwheel = [1 2 3 4; 5 6 7 8; 1 2 6 5; 2 3 7 6; 3 4 8 7; 4 1 5 8];
    patch(ax, 'Vertices', vertices_handwheel, 'Faces', faces_handwheel, ...
          'FaceColor', color, 'EdgeColor', 'k', 'Tag', tag);
end

function draw_connecting_rod(ax, pos, tag, color)
    rod_radius = 0.005; % 连接杆半径
    slider_z_pos = 1.05; % 滑块的 z 位置（与滑轨对齐）
    slider_height = 0.05; % 滑块高度
    rod_top_z = slider_z_pos - slider_height/2; % 连接杆顶端（滑块底部）
    rod_bottom_z = pos(3); % 连接杆底端（水听器中心）
    
    % 删除旧的连接杆
    delete(findobj(ax, 'Tag', tag));
    
    % 绘制连接杆
    [X, Y, Z] = cylinder(rod_radius, 20);
    Z = Z * (rod_top_z - rod_bottom_z) + rod_bottom_z; % 沿 z 方向拉伸
    surf(ax, X + pos(1), Y + pos(2), Z, 'FaceColor', color, 'EdgeColor', 'none', 'Tag', tag);
end

function h = draw_hydrophone(ax, pos, angle, cylinder_radius, cylinder_height, rect_length, rect_width, rect_height, tag, color)
    % 确保 angle 为 double 类型
    angle = double(angle);
    
    delete(findobj(ax, 'Tag', tag));
    
    if strcmp(tag, 'Transmitter')
        % 旋转矩阵（绕 z 轴旋转）
        theta = deg2rad(angle); % 将角度转换为弧度
        Rz = [cos(theta) -sin(theta) 0; sin(theta) cos(theta) 0; 0 0 1];
        
        % 发射器：红色圆柱（沿 x 轴正方向）
        [Y_cyl, Z_cyl, X_cyl] = cylinder(cylinder_radius, 20); % 沿 x 轴绘制（X 作为长度）
        X_cyl = X_cyl * cylinder_height - cylinder_height/2; % 圆柱中心在原点
        Z_cyl = Z_cyl + pos(3); % 圆柱中心在 pos(3)
        Y_cyl = Y_cyl + pos(2); % 圆柱中心在 pos(2)
        X_cyl = X_cyl + pos(1); % 圆柱中心在 pos(1)
        
        % 转换为顶点矩阵
        vertices_cylinder = zeros(numel(X_cyl), 3);
        for i = 1:numel(X_cyl)
            vertices_cylinder(i, :) = [X_cyl(i), Y_cyl(i), Z_cyl(i)];
        end
        
        % 装置主体（银色圆柱，位于圆柱末端）
        device_radius = 0.03; % 装置半径，0.06m 直径
        device_height = 0.03; % 装置高度
        device_center_x = pos(1) + cylinder_height/2; % 装置中心在圆柱末端（沿 x 轴）
        
        [Y_dev, Z_dev, X_dev] = cylinder(device_radius, 20);
        X_dev = X_dev * device_height - device_height/2 + device_center_x; % 装置中心在圆柱末端
        Z_dev = Z_dev + pos(3);
        Y_dev = Y_dev + pos(2);
        
        vertices_device = zeros(numel(X_dev), 3);
        for i = 1:numel(X_dev)
            vertices_device(i, :) = [X_dev(i), Y_dev(i), Z_dev(i)];
        end
        
        % 装置末端黑色部分（位于银色装置末端）
        black_radius = 0.025; % 黑色部分半径，0.05m 直径
        black_height = 0.005; % 黑色部分高度
        black_center_x = device_center_x + device_height/2 + black_height/2; % 黑色部分在装置末端
        
        [Y_black, Z_black, X_black] = cylinder(black_radius, 20);
        X_black = X_black * black_height - black_height/2 + black_center_x;
        Z_black = Z_black + pos(3);
        Y_black = Y_black + pos(2);
        
        vertices_black = zeros(numel(X_black), 3);
        for i = 1:numel(X_black)
            vertices_black(i, :) = [X_black(i), Y_black(i), Z_black(i)];
        end
        
        % 合并所有顶点（红色圆柱、银色装置、黑色部分）
        vertices_all = [vertices_cylinder; vertices_device; vertices_black];
        
        % 应用初始旋转（确保初始方向沿 x 轴正方向）
        Rz_initial = [1 0 0; 0 1 0; 0 0 1]; % 单位矩阵，初始方向沿 x 轴正方向
        vertices_all = (vertices_all - pos) * Rz_initial' + pos;
        
        % 应用用户指定的角度旋转
        vertices_all = (vertices_all - pos) * Rz' + pos;
        
        % 分割顶点并绘制
        % 红色圆柱
        vertices_cylinder_rot = vertices_all(1:numel(X_cyl), :);
        X_cyl_rot = reshape(vertices_cylinder_rot(:, 1), size(X_cyl));
        Y_cyl_rot = reshape(vertices_cylinder_rot(:, 2), size(Y_cyl));
        Z_cyl_rot = reshape(vertices_cylinder_rot(:, 3), size(Z_cyl));
        surf(ax, X_cyl_rot, Y_cyl_rot, Z_cyl_rot, 'FaceColor', color, 'EdgeColor', 'none', 'Tag', tag);
        
        % 银色装置
        vertices_device_rot = vertices_all(numel(X_cyl)+1:numel(X_cyl)+numel(X_dev), :);
        X_dev_rot = reshape(vertices_device_rot(:, 1), size(X_dev));
        Y_dev_rot = reshape(vertices_device_rot(:, 2), size(Y_dev));
        Z_dev_rot = reshape(vertices_device_rot(:, 3), size(Z_dev));
        surf(ax, X_dev_rot, Y_dev_rot, Z_dev_rot, 'FaceColor', [0.8 0.8 0.8], 'EdgeColor', 'none', 'Tag', tag); % 银色
        
        % 黑色部分
        vertices_black_rot = vertices_all(numel(X_cyl)+numel(X_dev)+1:end, :);
        X_black_rot = reshape(vertices_black_rot(:, 1), size(X_black));
        Y_black_rot = reshape(vertices_black_rot(:, 2), size(Y_black));
        Z_black_rot = reshape(vertices_black_rot(:, 3), size(Z_black));
        surf(ax, X_black_rot, Y_black_rot, Z_black_rot, 'FaceColor', [0 0 0], 'EdgeColor', 'none', 'Tag', tag); % 黑色
        
    else
        % 接收器：话筒形状（以吊绳为中心，绕 z 轴旋转，只有 x 轴负方向有黑色部分）
        % 旋转矩阵（绕 z 轴旋转）
        theta = deg2rad(angle); % 将角度转换为弧度
        Rz = [cos(theta) -sin(theta) 0; sin(theta) cos(theta) 0; 0 0 1];
        
        % 1. 主体部分（银色圆柱，以 pos 为中心）
        receiver_radius = 0.02; % 主体半径，0.04m 直径
        receiver_height = 0.15; % 主体长度，0.15m
        [Y_rec, Z_rec, X_rec] = cylinder(receiver_radius, 20); % 沿 x 轴绘制（X 作为长度）
        X_rec = X_rec * receiver_height - receiver_height/2; % 圆柱中心在原点（[-receiver_height/2, receiver_height/2]）
        Z_rec = Z_rec + pos(3); % 圆柱中心在 pos(3)
        Y_rec = Y_rec + pos(2); % 圆柱中心在 pos(2)
        X_rec = X_rec + pos(1); % 圆柱中心在 pos(1)
        
        % 转换为顶点矩阵
        vertices_receiver = zeros(numel(X_rec), 3);
        for i = 1:numel(X_rec)
            vertices_receiver(i, :) = [X_rec(i), Y_rec(i), Z_rec(i)];
        end
        
        % 2. 底部部分（黑色圆柱，位于主体底端，x 轴负方向）
        bottom_radius = 0.015; % 底部半径，0.03m 直径
        bottom_height = 0.03; % 底部长度，0.03m
        bottom_center_x = pos(1) - receiver_height/2 - bottom_height/2; % 底部中心在主体底端（x 轴负方向）
        
        [Y_bottom, Z_bottom, X_bottom] = cylinder(bottom_radius, 20);
        X_bottom = X_bottom * bottom_height - bottom_height/2 + bottom_center_x; % 底部中心位置
        Z_bottom = Z_bottom + pos(3);
        Y_bottom = Y_bottom + pos(2);
        
        vertices_bottom = zeros(numel(X_bottom), 3);
        for i = 1:numel(X_bottom)
            vertices_bottom(i, :) = [X_bottom(i), Y_bottom(i), Z_bottom(i)];
        end
        
        % 3. 合并所有顶点（银色主体、底部黑色部分）
        vertices_all = [vertices_receiver; vertices_bottom];
        
        % 4. 应用初始旋转（确保初始方向沿 x 轴正方向，保持一致性）
        Rz_initial = [1 0 0; 0 1 0; 0 0 1]; % 初始方向沿 x 轴正方向（对称结构无需调整方向）
        vertices_all = (vertices_all - pos) * Rz_initial' + pos;
        
        % 5. 应用用户指定的角度旋转（绕 z 轴）
        vertices_all = (vertices_all - pos) * Rz' + pos;
        
        % 6. 分割顶点并绘制
        % 银色主体
        vertices_receiver_rot = vertices_all(1:numel(X_rec), :);
        X_rec_rot = reshape(vertices_receiver_rot(:, 1), size(X_rec));
        Y_rec_rot = reshape(vertices_receiver_rot(:, 2), size(Y_rec));
        Z_rec_rot = reshape(vertices_receiver_rot(:, 3), size(Z_rec));
        surf(ax, X_rec_rot, Y_rec_rot, Z_rec_rot, 'FaceColor', [0.8 0.8 0.8], 'EdgeColor', 'none', 'Tag', tag); % 银色
        
        % 底部黑色部分（x 轴负方向）
        vertices_bottom_rot = vertices_all(numel(X_rec)+1:end, :);
        X_bottom_rot = reshape(vertices_bottom_rot(:, 1), size(X_bottom));
        Y_bottom_rot = reshape(vertices_bottom_rot(:, 2), size(Y_bottom));
        Z_bottom_rot = reshape(vertices_bottom_rot(:, 3), size(Z_bottom));
        surf(ax, X_bottom_rot, Y_bottom_rot, Z_bottom_rot, 'FaceColor', [0 0 0], 'EdgeColor', 'none', 'Tag', tag); % 黑色
    end
    
    h = findobj(ax, 'Tag', tag);
end