function [peaksel, peaked] = detpart(num, peaked, block_num,T,inoutgate)
    % detpart - 峰值检测与选择函数
    % 输入:
    %   num   - 通道号
    %   peaked - 包含峰值数据的结构体数组
    % 输出:
    %   peaksel - 包含选择后峰值数据的结构体
    %   peaked - 清零后的峰值数据结构体
    Fs = 156250;
    % 初始化输出结构体
    peaksel = struct(...
        'num', 0, ...
        'max', zeros(1, 5), ...
        'sta', zeros(1, 5), ...
        'wide', zeros(1, 5), ...
        'Ain', zeros(1, 5));
    
    % 检查是否有峰值
    if peaked(num).num == 0
        return;
    end
    
    % 保存原始峰值数据用于处理
    orig_peaked = peaked(num);
    
    % 初始化变量
    maxdix = zeros(1, 6);
    fstdix = zeros(1, 6);
    sel_dix = zeros(1, 6);
    dly = zeros(1, 2);
    sel_n = 0;
    peaknum = orig_peaked.num;
    
    % 如果峰值数量不超过5，直接选择所有峰值
    if peaknum <= 5
        for cnt1 = 1:peaknum
            sel_dix(cnt1) = cnt1;
        end
        sel_n = peaknum;
    else
        % 第一阶段：寻找第一个主要峰值群
        nummax = 0;
        numfst = 0;
        maxtmp = 0;
        dix = 0;
        fsten = 0;
        
        % 寻找全局最大峰值
        for cnt1 = 1:peaknum
            if orig_peaked.max(cnt1) > maxtmp
                maxtmp = orig_peaked.max(cnt1);
                dly(1) = orig_peaked.sta(cnt1);
                dix = cnt1;
                fsten = 1;
            end
        end
        
        % 向前搜索群首
        if fsten == 1
            fstdix(numfst + 1) = dix;
            found = false;
            
            for cnt2 = 30:-1:1
                if (dix - cnt2 >= 1) && ...
                   (dly(1) - orig_peaked.sta(dix - cnt2) <= 6250) && ...
                   (orig_peaked.max(dix - cnt2) > maxtmp * 0.1)
                    
                    fstdix(numfst + 1) = dix - cnt2;
                    maxdix(nummax + 1) = dix;
                    nummax = nummax + 1;
                    found = true;
                    break;
                end
            end
            
            
            numfst = numfst + 1;
        end
        
        % 第二阶段：寻找第二个主要峰值群
        maxtmp = 0;
        dly(2) = orig_peaked.sta(1);
        dix = 0;
        fsten = 0;
        
        if numfst > 0
            for cnt1 = 1:peaknum
                condition1 = (orig_peaked.sta(cnt1) - dly(1) > 7031);
                condition2 = (dly(1) - orig_peaked.sta(cnt1) > 7031);
                
                if orig_peaked.max(cnt1) > maxtmp && (condition1 || condition2)
                    maxtmp = orig_peaked.max(cnt1);
                    dly(2) = orig_peaked.sta(cnt1);
                    dix = cnt1;
                    fsten = 1;
                end
            end
            
            if fsten == 1
                fstdix(numfst + 1) = dix;
                found = false;
                
                for cnt2 = 30:-1:1
                    if (dix - cnt2 >= 1) && ...
                       (dly(2) - orig_peaked.sta(dix - cnt2) <= 6250) && ...
                       (orig_peaked.max(dix - cnt2) > maxtmp * 0.1)
                        
                        maxdix(nummax + 1) = dix;
                        fstdix(numfst + 1) = dix - cnt2;
                        nummax = nummax + 1;
                        found = true;
                        break;
                    end
                end
                
                
                numfst = numfst + 1;
            end
        end
        
        % 组合选择的索引
        for cnt1 = 1:numfst
            sel_dix(cnt1) = fstdix(cnt1);
        end
        
        for cnt1 = 1:nummax
            if numfst + cnt1 <= 5
                sel_dix(numfst + cnt1) = maxdix(cnt1);
            end
        end
        
        sel_n = numfst + nummax;
        if sel_n > 5
            sel_n = 5;
        end
        
        % 补充不足5个峰的情况
        if sel_n < 5
            % 创建副本并排序
            arrayCopy = struct('amplitude', num2cell(orig_peaked.max(1:peaknum)), ...
                              'index', num2cell(1:peaknum));
            
            % 按幅度降序排序
            [~, idx] = sort([arrayCopy.amplitude], 'descend');
            arrayCopy = arrayCopy(idx);
            
            true_sel_n = sel_n;
            flag = 0;
            
            for j = 1:length(arrayCopy)
                for i = 1:sel_n
                    if sel_dix(i) == arrayCopy(j).index
                        flag = 1;
                        break;
                    end
                end
                
                if flag == 0
                    true_sel_n = true_sel_n + 1;
                    sel_dix(true_sel_n) = arrayCopy(j).index;
                end
                
                flag = 0;
                
                if true_sel_n == 5
                    break;
                end
            end
            
            sel_n = true_sel_n;
        end
    end
    
    % 处理选择的峰值
    for cnt1 = 1:sel_n
        cnt2 = sel_dix(cnt1);
        
        peaksel.max(cnt1) = orig_peaked.max(cnt2);
        peaksel.Ain(cnt1) = orig_peaked.Ain(cnt2);
        
        % 计算精确位置
        tmpf = 2 * orig_peaked.max(cnt2) - orig_peaked.maxl(cnt2) - orig_peaked.maxr(cnt2);
        
        if tmpf < 0.00001 && tmpf > 0
            tmpf = 0.00001;
        elseif tmpf > -0.00001 && tmpf <= 0
            tmpf = -0.00001;
        end
        
        Dn = 16; 
        peaksel.sta(cnt1) = orig_peaked.sta(cnt2) + ...
            round(Dn * (orig_peaked.maxr(cnt2) - orig_peaked.maxl(cnt2)) / (2 * tmpf));
        
        peaksel.wide(cnt1) = round(orig_peaked.wide(cnt2));
    end
    
    peaksel.num = sel_n;
    


    
    % 清零原始peaked结构体中该通道的数据
    peaked(num).sta = zeros(1, 200);
    peaked(num).max = zeros(1, 200);
    peaked(num).maxl = zeros(1, 200);
    peaked(num).maxr = zeros(1, 200);
    peaked(num).wide = zeros(1, 200);
    peaked(num).Ain = zeros(1, 200);
    peaked(num).num = 0;


    % 写入量化后的结果到文件
    if peaksel.num > 0
        fid = fopen('result.txt', 'a'); % 追加模式打开文件
        
        % 如果是第一次写入，添加标题行
        if ftell(fid) == 0
            fprintf(fid, 'Block\tChannel\tPeakID\tDelay\tAmp\tWid\tInOutRatio\n');
        end
        
        % 对每个峰值进行量化并写入
        for idx = 1:peaksel.num
            % 量化处理
            uAin = sqrt(peaksel.max(idx)) / (peaksel.Ain(idx) / inoutgate);
            umax = sqrt(peaksel.max(idx)) * 2 * 3.3 / 65535 / fix(T * Fs / Dn);
            %umax = peaksel.max(idx);
            uwide = peaksel.wide(idx)*1000/Fs;
            udelay = peaksel.sta(idx) / Fs;
            
            % 直接写入量化后的数据
            fprintf(fid, '%d\t%d\t%d\t%.4f\t%.4f\t%.1f\t%.2f\n', ...
                    block_num, ...
                    num, ...
                    idx, ...
                    udelay, ...
                    umax, ...
                    uwide, ...
                    uAin);
        end
        
        fclose(fid);
    end
end