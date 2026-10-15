const express = require('express');
const { Server } = require('ws');
const sqlite3 = require('sqlite3').verbose();
const bodyParser = require('body-parser');
const cors = require('cors');
const path = require('path');
const https = require('https'); // 用于创建 HTTPS 服务器
const fs = require('fs'); // 用于读取证书文件

const app = express();
const port = 3002;

// 读取自签名证书和密钥（确保 key.pem 和 cert.pem 已生成并放置在同一目录下）
const options = {
    key: fs.readFileSync('key.pem'),
    cert: fs.readFileSync('cert.pem')
};

app.use(cors({
    origin: '*',
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['Content-Type']
}));
app.use(bodyParser.json());
app.use(express.static(path.join(__dirname, 'public')));

// 使用 Set 管理 videoClients，确保客户端唯一性
const videoClients = new Set();
let frameCount = 0;

// 在线用户追踪
const onlineUsers = new Set();

const db = new sqlite3.Database('oceanedu.db', (err) => {
    if (err) {
        console.error('数据库连接失败:', err.message);
        return;
    }
    console.log('成功连接到 SQLite 数据库');

    db.serialize(() => {
        db.run(`
            CREATE TABLE IF NOT EXISTS users (
                user_id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT UNIQUE,
                email TEXT UNIQUE,
                password TEXT
            )
        `);
        db.run(`
            CREATE TABLE IF NOT EXISTS course_completions (
                user_id INTEGER,
                course_id TEXT,
                FOREIGN KEY (user_id) REFERENCES users(user_id)
            )
        `);
        db.run(`
            CREATE TABLE IF NOT EXISTS equipment (
                equipment_id INTEGER PRIMARY KEY,
                status TEXT CHECK(status IN ('available', 'occupied', 'fault'))
            )
        `);
        db.run(`
            CREATE TABLE IF NOT EXISTS reservations (
                reservation_id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER,
                equipment_id INTEGER,
                date TEXT,
                time_slot TEXT,
                key TEXT UNIQUE,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (user_id) REFERENCES users(user_id),
                FOREIGN KEY (equipment_id) REFERENCES equipment(equipment_id)
            )
        `);
        db.run(`
            CREATE TABLE IF NOT EXISTS courses (
                course_id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER,
                title TEXT,
                description TEXT,
                category TEXT,
                content TEXT,
                status TEXT DEFAULT 'pending', /* 'pending', 'approved', 'rejected' */
                feedback TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (user_id) REFERENCES users(user_id)
            )
        `);
        
        db.run(`
            INSERT OR IGNORE INTO equipment (equipment_id, status)
            VALUES (1, 'available'), (2, 'available'), (3, 'available')
        `);
        
        // 检查并添加key列
        db.all("PRAGMA table_info(reservations)", (err, rows) => {
            if (err) {
                console.error('检查表结构失败:', err);
                return;
            }
            
            // 检查返回的行中是否有名为'key'的列
            const hasKeyColumn = rows && rows.some(row => row.name === 'key');
            
            if (!hasKeyColumn) {
                console.log('正在为reservations表添加key列...');
                
                // 添加普通key列（不带UNIQUE约束）
                db.run(`ALTER TABLE reservations ADD COLUMN key TEXT`, (err) => {
                    if (err) {
                        console.error('添加key列失败:', err);
                    } else {
                        console.log('成功添加key列到reservations表');
                        
                        // 为现有预约生成密钥
                        db.all(`SELECT reservation_id FROM reservations WHERE key IS NULL`, [], (err, rows) => {
                            if (err) {
                                console.error('获取无密钥预约失败:', err);
                                return;
                            }
                            
                            if (rows && rows.length > 0) {
                                console.log(`为${rows.length}个现有预约生成密钥...`);
                                
                                // 为了避免密钥冲突，使用一个Map来跟踪已使用的密钥
                                const usedKeys = new Map();
                                
                                rows.forEach(row => {
                                    // 循环生成密钥直到获得一个未使用的
                                    let key;
                                    do {
                                        key = generateReservationKey();
                                    } while (usedKeys.has(key));
                                    
                                    // 记录此密钥已使用
                                    usedKeys.set(key, true);
                                    
                                    db.run(
                                        `UPDATE reservations SET key = ? WHERE reservation_id = ?`,
                                        [key, row.reservation_id],
                                        (err) => {
                                            if (err) {
                                                console.error(`为预约 ${row.reservation_id} 设置密钥失败:`, err);
                                            } else {
                                                console.log(`预约 ${row.reservation_id} 已设置密钥 ${key}`);
                                            }
                                        }
                                    );
                                });
                                
                                // 所有数据更新后，尝试添加唯一索引
                                setTimeout(() => {
                                    console.log('正在为key列添加唯一索引...');
                                    db.run(`CREATE UNIQUE INDEX IF NOT EXISTS idx_reservations_key ON reservations(key)`, (err) => {
                                        if (err) {
                                            console.error('添加key列唯一索引失败:', err);
                                            
                                            // 检查是否存在重复值
                                            db.all(`SELECT key, COUNT(*) as count FROM reservations 
                                                   WHERE key IS NOT NULL GROUP BY key HAVING count > 1`, [], (err, duplicates) => {
                                                if (err) {
                                                    console.error('检查重复密钥失败:', err);
                                                } else if (duplicates && duplicates.length > 0) {
                                                    console.error('存在重复密钥:', duplicates);
                                                    // 这里可以添加处理重复密钥的逻辑
                                                }
                                            });
                                        } else {
                                            console.log('成功为key列添加唯一索引');
                                        }
                                    });
                                }, 2000); // 延迟2秒，确保所有UPDATE操作完成
                            }
                        });
                    }
                });
            }
        });

        // 启动时清理过期预约
        cleanExpiredReservations();
    });
});

// 清理过期预约的函数
function cleanExpiredReservations() {
    const currentTime = new Date();
    const currentDate = currentTime.toISOString().split('T')[0];
    const options = { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', hour12: false };
    const [currentHour, currentMinute] = currentTime.toLocaleString('zh-CN', options).split(':').map(Number);
    const currentTotalMinutes = currentHour * 60 + currentMinute;

    db.all(`SELECT reservation_id, equipment_id, date, time_slot FROM reservations`, [], (err, reservations) => {
        if (err) {
            console.error('查询预约记录失败:', err);
            return;
        }

        reservations.forEach(reservation => {
            const reservationDate = reservation.date;
            const [start, end] = reservation.time_slot.split('-');
            const [endHour, endMinute] = end.split(':').map(Number);
            let endTotalMinutes = endHour * 60 + endMinute;

            // 如果日期早于今天，直接删除
            if (reservationDate < currentDate) {
                deleteReservation(reservation.reservation_id, reservation.equipment_id);
                return;
            }

            // 如果日期是今天，比较结束时间
            if (reservationDate === currentDate) {
                const [startHour, startMinute] = start.split(':').map(Number);
                let startTotalMinutes = startHour * 60 + startMinute;

                // 处理跨天情况
                if (endTotalMinutes < startTotalMinutes) {
                    if (currentHour < 12) {
                        endTotalMinutes += 24 * 60;
                    } else {
                        startTotalMinutes -= 24 * 60;
                    }
                }

                // 如果当前时间晚于预约结束时间，删除该预约
                if (currentTotalMinutes > endTotalMinutes) {
                    deleteReservation(reservation.reservation_id, reservation.equipment_id);
                }
            }
        });
    });
}

// 删除预约并更新设备状态
function deleteReservation(reservationId, equipmentId) {
    db.run(`DELETE FROM reservations WHERE reservation_id = ?`, [reservationId], (err) => {
        if (err) {
            console.error(`删除预约 ${reservationId} 失败:`, err);
            return;
        }
        console.log(`已删除过期预约: reservation_id=${reservationId}`);

        // 检查设备是否还有其他有效预约
        const currentDate = new Date().toISOString().split('T')[0];
        db.get(`SELECT * FROM reservations WHERE equipment_id = ? AND date >= ?`, [equipmentId, currentDate], (err, row) => {
            if (err) {
                console.error(`检查设备 ${equipmentId} 预约失败:`, err);
                return;
            }
            if (!row) {
                // 如果没有其他预约，更新设备状态为 available
                db.run(`UPDATE equipment SET status = 'available' WHERE equipment_id = ?`, [equipmentId], (err) => {
                    if (err) {
                        console.error(`更新设备 ${equipmentId} 状态失败:`, err);
                    } else {
                        console.log(`设备 ${equipmentId} 状态已更新为 available`);
                    }
                });
            }
        });
    });
}

// 每分钟清理一次过期预约
setInterval(cleanExpiredReservations, 60 * 1000);

// 创建 HTTPS 服务器
const server = https.createServer(options, app);

// 创建 WSS 服务器
const wss = new Server({ server });

// 启动 HTTPS 服务器
server.listen(port, () => {
    console.log(`HTTPS 服务器运行在 https://localhost:${port}`);
});

const clients = new Map();
const frontendClients = new Set(); // 专门存储前端客户端

wss.on('connection', (ws) => {
    console.log('WebSocket 客户端连接');

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            if (data.type === 'register_client') {
                clients.set(data.equipment_id, ws);
                console.log(`从端注册: 实验池 ${data.equipment_id}`);
                ws.send(JSON.stringify({ type: 'registered', equipment_id: data.equipment_id }));
            } else if (data.type === 'user_connect') {
                if (data.user_id) {
                    onlineUsers.add(data.user_id);
                    frontendClients.add(ws); // 将用户客户端添加到前端集合
                    console.log(`用户上线: user_id=${data.user_id}`);
                }
            } else if (data.type === 'experiment_params') {
                const targetWs = clients.get(data.equipment_id);
                if (targetWs && targetWs.readyState === WebSocket.OPEN) {
                    targetWs.send(JSON.stringify(data));
                    console.log(`向实验池 ${data.equipment_id} 发送实验参数`);
                } else {
                    ws.send(JSON.stringify({ type: 'error', message: `实验池 ${data.equipment_id} 未连接或连接已关闭` }));
                }
            } else if (data.type === 'experiment_response') {
                // 广播给所有前端客户端
                frontendClients.forEach((client) => {
                    if (client.readyState === WebSocket.OPEN) {
                        client.send(JSON.stringify(data));
                    }
                });
                console.log(`广播从端响应: ${JSON.stringify(data)}`);
            } else if (data.type === 'video_stream') {
                const frameData = Buffer.from(data.frame, 'base64');
                videoClients.forEach(client => {
                    try {
                        client.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${frameData.length}\r\n\r\n`);
                        client.write(frameData);
                        client.write('\r\n');
                    } catch (err) {
                        console.error('视频流写入错误:', err.message);
                        videoClients.delete(client); // 移除出错的客户端
                    }
                });
                // 只广播给前端客户端
                frontendClients.forEach(client => {
                    if (client.readyState === WebSocket.OPEN) {
                        client.send(JSON.stringify({
                            type: 'video_stream',
                            equipment_id: data.equipment_id,
                            frame: data.frame
                        }));
                    }
                });
            } else if (data.type === 'equipment_status') {
                db.run(`UPDATE equipment SET status = ? WHERE equipment_id = ?`, [data.status, data.equipment_id], (err) => {
                    if (err) {
                        console.error('更新器械状态失败:', err);
                    } else {
                        console.log(`实验池 ${data.equipment_id} 状态更新为 ${data.status}`);
                    }
                });
            } else if (data.type === 'ffmpeg_terminated') {
                const equipment_id = data.equipment_id;
                const currentTime = new Date();
                const currentDate = currentTime.toISOString().split('T')[0];
                const options = { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', hour12: false };
                const [currentHour, currentMinute] = currentTime.toLocaleString('zh-CN', options).split(':').map(Number);
                const currentTotalMinutes = currentHour * 60 + currentMinute;

                db.get(
                    `SELECT date, time_slot FROM reservations WHERE equipment_id = ? AND date = ?`,
                    [equipment_id, currentDate],
                    (err, row) => {
                        if (err) {
                            console.error('检查预约失败:', err);
                            return;
                        }
                        if (row) {
                            const [start, end] = row.time_slot.split('-');
                            const [startHour, startMinute] = start.split(':').map(Number);
                            const [endHour, endMinute] = end.split(':').map(Number);
                            let startTotalMinutes = startHour * 60 + startMinute;
                            let endTotalMinutes = endHour * 60 + endMinute;

                            if (endTotalMinutes < startTotalMinutes) {
                                if (currentHour < 12) {
                                    endTotalMinutes += 24 * 60;
                                } else {
                                    startTotalMinutes -= 24 * 60;
                                }
                            }

                            if (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes <= endTotalMinutes) {
                                console.log(`实验池 ${equipment_id} 仍在预约时间内，不更新状态`);
                                return;
                            }
                        }

                        db.run(`UPDATE equipment SET status = 'available' WHERE equipment_id = ?`, [equipment_id], (err) => {
                            if (err) {
                                console.error('更新器械状态失败:', err);
                            } else {
                                console.log(`实验池 ${equipment_id} 状态更新为 available`);
                            }
                        });
                    }
                );
            }
        } catch (err) {
            console.error('WebSocket 消息处理失败:', err);
        }
    });

    ws.on('close', () => {
        for (let [equipment_id, client] of clients) {
            if (client === ws) {
                clients.delete(equipment_id);
                console.log(`从端断开: 实验池 ${equipment_id}`);
                break;
            }
        }
        frontendClients.delete(ws); // 移除前端客户端
    });
});

app.post('/input', (req, res) => {
    console.log('收到 FFmpeg 输入流');
    console.log(`当前 videoClients 数量: ${videoClients.size}`);
    console.log('请求头:', req.headers); // 添加日志，调试请求头

    // 从 Content-Type 头中提取边界分隔符
    const contentType = req.headers['content-type'] || '';
    let boundary = '--boundary'; // 默认值
    if (contentType) {
        const boundaryMatch = contentType.match(/boundary=(.+)/);
        if (boundaryMatch) {
            boundary = `--${boundaryMatch[1]}`;
        } else {
            console.warn('Content-Type 中未找到 boundary，使用默认值 --boundary');
        }
    } else {
        console.warn('请求头中缺少 Content-Type，使用默认值 --boundary');
    }
    console.log(`使用边界分隔符: ${boundary}`);

    // 设置响应头，保持连接打开
    res.writeHead(200, {
        'Content-Type': 'text/plain',
        'Connection': 'keep-alive'
    });

    let buffer = Buffer.alloc(0);
    frameCount = 0;

    req.on('data', (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);

        // 查找 MJPEG 流的边界
        while (true) {
            const boundaryIndex = buffer.indexOf(boundary);
            if (boundaryIndex === -1) break; // 未找到边界，继续接收数据

            // 查找帧的头部（Content-Type 和 Content-Length）
            const headerEndIndex = buffer.indexOf('\r\n\r\n', boundaryIndex);
            if (headerEndIndex === -1) break; // 头部未完整，继续接收数据

            const header = buffer.slice(boundaryIndex, headerEndIndex).toString();
            const contentLengthMatch = header.match(/Content-Length: (\d+)/i);
            if (!contentLengthMatch) {
                console.error('未找到 Content-Length');
                break;
            }

            const contentLength = parseInt(contentLengthMatch[1], 10);
            const frameStart = headerEndIndex + 4; // 跳过 \r\n\r\n
            const frameEnd = frameStart + contentLength;

            if (buffer.length < frameEnd) break; // 帧数据未完整，继续接收数据

            // 提取帧数据
            const frameData = buffer.slice(frameStart, frameEnd);

            // 分发帧数据给 videoClients
            videoClients.forEach(client => {
                try {
                    client.write(`--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${frameData.length}\r\n\r\n`);
                    client.write(frameData);
                    client.write('\r\n');
                } catch (err) {
                    console.error('写入客户端错误:', err.message);
                    videoClients.delete(client); // 移除出错的客户端
                }
            });

            frameCount++;
            if (frameCount % 10 === 0) {
                console.log(`已处理 ${frameCount} 帧`);
            }

            // 移除已处理的帧数据
            buffer = buffer.slice(frameEnd);
        }
    });

    req.on('end', () => {
        console.log('FFmpeg 输入流正常结束');
        videoClients.forEach(client => {
            try {
                client.end();
            } catch (err) {
                console.error('关闭客户端错误:', err.message);
            }
        });
        res.end();
    });

    req.on('error', err => {
        console.error('输入流错误:', err.message);
        res.status(500).end();
    });

    res.on('error', err => {
        console.error('响应错误:', err.message);
    });
});
// 添加 /video 端点，用于提供 MJPEG 视频流
app.get('/video', (req, res) => {
    console.log('客户端请求视频流');

    // 设置 MJPEG 流响应头
    res.writeHead(200, {
        'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
        'Cache-Control': 'no-cache',
        'Connection': 'keep-alive'
    });

    // 将客户端添加到 videoClients
    videoClients.add(res);
    console.log(`当前 videoClients 数量: ${videoClients.size}`);

    // 当客户端断开连接时，从 videoClients 中移除
    req.on('close', () => {
        videoClients.delete(res);
        console.log('客户端断开连接');
        console.log(`当前 videoClients 数量: ${videoClients.size}`);
    });

    req.on('error', (err) => {
        console.error('视频流请求错误:', err.message);
        videoClients.delete(res);
    });
});

app.post('/register', (req, res) => {
    const { username, email, password } = req.body;
    if (!username || !email || !password) {
        return res.status(400).json({ error: '用户名、邮箱和密码不能为空' });
    }

    db.get(`SELECT * FROM users WHERE username = ? OR email = ?`, [username, email], (err, row) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        if (row) {
            return res.status(400).json({ error: '用户名或邮箱已存在' });
        }

        db.run(`INSERT INTO users (username, email, password) VALUES (?, ?, ?)`, [username, email, password], function (err) {
            if (err) {
                return res.status(500).json({ error: '注册失败' });
            }
            res.json({ user_id: this.lastID });
        });
    });
});

app.post('/login', (req, res) => {
    const { username, email, password } = req.body;
    if (!username || !email || !password) {
        return res.status(400).json({ error: '用户名、邮箱和密码不能为空' });
    }

    db.get(`SELECT * FROM users WHERE username = ? AND email = ? AND password = ?`, [username, email, password], (err, row) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        if (!row) {
            return res.status(400).json({ error: '用户名、邮箱或密码错误' });
        }
        res.json({ user_id: row.user_id });
    });
});

app.post('/update-profile', (req, res) => {
    const { user_id, username, email, password } = req.body;
    if (!user_id || !username || !email) {
        return res.status(400).json({ error: '用户ID、用户名和邮箱不能为空' });
    }

    const updates = [];
    const values = [];
    if (password) {
        updates.push('password = ?');
        values.push(password);
    }
    updates.push('username = ?', 'email = ?');
    values.push(username, email, user_id);

    db.run(`UPDATE users SET ${updates.join(', ')} WHERE user_id = ?`, values, (err) => {
        if (err) {
            return res.status(500).json({ error: '更新失败' });
        }
        res.json({ message: '个人信息已更新' });
    });
});

app.post('/complete-course', (req, res) => {
    const { user_id, course_id } = req.body;
    if (!user_id || !course_id) {
        return res.status(400).json({ error: '用户ID和课程ID不能为空' });
    }

    db.get(`SELECT * FROM course_completions WHERE user_id = ? AND course_id = ?`, [user_id, course_id], (err, row) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        if (row) {
            return res.json({ message: '课程已完成' });
        }

        db.run(`INSERT INTO course_completions (user_id, course_id) VALUES (?, ?)`, [user_id, course_id], (err) => {
            if (err) {
                return res.status(500).json({ error: '记录课程完成失败' });
            }
            res.json({ message: '课程完成已记录' });
        });
    });
});

app.get('/user-completions', (req, res) => {
    const { user_id } = req.query;
    if (!user_id) {
        return res.status(400).json({ error: '用户ID不能为空' });
    }

    db.all(`SELECT course_id FROM course_completions WHERE user_id = ?`, [user_id], (err, rows) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        res.json(rows);
    });
});

app.get('/equipment-status', (req, res) => {
    const currentTime = new Date();
    const options = { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', hour12: false };
    const [currentHour, currentMinute] = currentTime.toLocaleString('zh-CN', options).split(':').map(Number);
    const currentDate = currentTime.toISOString().split('T')[0];
    const currentTotalMinutes = currentHour * 60 + currentMinute;

    db.all(`SELECT equipment_id, status FROM equipment`, [], (err, equipmentRows) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误: ' + err.message });
        }

        db.all(`SELECT equipment_id, time_slot, date FROM reservations`, [], (err, reservationRows) => {
            if (err) {
                return res.status(500).json({ error: '数据库错误: ' + err.message });
            }

            const updatedEquipment = equipmentRows.map(equipment => {
                let isOccupied = false;
                const relevantReservations = reservationRows.filter(r => r.equipment_id === equipment.equipment_id);

                for (const reservation of relevantReservations) {
                    if (reservation.date !== currentDate) continue;

                    const [start, end] = reservation.time_slot.split('-');
                    const [startHour, startMinute] = start.split(':').map(Number);
                    const [endHour, endMinute] = end.split(':').map(Number);
                    let startTotalMinutes = startHour * 60 + startMinute;
                    let endTotalMinutes = endHour * 60 + endMinute;

                    if (endTotalMinutes < startTotalMinutes) {
                        if (currentHour < 12) {
                            endTotalMinutes += 24 * 60;
                        } else {
                            startTotalMinutes -= 24 * 60;
                        }
                    }

                    if (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes <= endTotalMinutes) {
                        isOccupied = true;
                        break;
                    }
                }

                if (!isOccupied && equipment.status === 'occupied') {
                    let hasFutureReservations = false;
                    for (const reservation of relevantReservations) {
                        if (reservation.date !== currentDate) continue;
                        const [start, end] = reservation.time_slot.split('-');
                        const [startHour, startMinute] = start.split(':').map(Number);
                        const [endHour, endMinute] = end.split(':').map(Number);
                        let startTotalMinutes = startHour * 60 + startMinute;
                        let endTotalMinutes = endHour * 60 + endMinute;

                        if (endTotalMinutes < startTotalMinutes) {
                            if (currentHour < 12) {
                                endTotalMinutes += 24 * 60;
                            } else {
                                startTotalMinutes -= 24 * 60;
                            }
                        }

                        if (currentTotalMinutes < endTotalMinutes) {
                            hasFutureReservations = true;
                            break;
                        }
                    }
                    if (!hasFutureReservations) {
                        db.run(`UPDATE equipment SET status = 'available' WHERE equipment_id = ?`, [equipment.equipment_id], (updateErr) => {
                            if (updateErr) {
                                console.error(`更新设备 ${equipment.equipment_id} 状态失败:`, updateErr);
                            } else {
                                console.log(`设备 ${equipment.equipment_id} 状态已更新为 available`);
                            }
                        });
                        equipment.status = 'available';
                    }
                }

                return {
                    id: equipment.equipment_id,
                    status: equipment.status
                };
            });

            res.json(updatedEquipment);
        });
    });
});

app.post('/reserve-equipment', (req, res) => {
    const { user_id, equipment_id, date, time_slot } = req.body;
    if (!user_id || !equipment_id || !date || !time_slot) {
        return res.status(400).json({ error: '用户ID、设备ID、日期和时间段不能为空' });
    }

    const timeSlotRegex = /^(\d{1,2}:\d{2})-(\d{1,2}:\d{2})$/;
    if (!timeSlotRegex.test(time_slot)) {
        return res.status(400).json({ error: '时间段格式无效' });
    }

    db.get(`SELECT status FROM equipment WHERE equipment_id = ?`, [equipment_id], (err, row) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        if (!row || row.status !== 'available') {
            return res.status(400).json({ error: '设备不可用或不存在' });
        }

        db.get(`SELECT * FROM reservations WHERE equipment_id = ? AND date = ? AND time_slot = ?`, [equipment_id, date, time_slot], (err, row) => {
            if (err) {
                return res.status(500).json({ error: '数据库错误' });
            }
            if (row) {
                return res.status(400).json({ error: '该时间段已被预约' });
            }

            // 生成唯一密钥
            const reservationKey = generateReservationKey();

            db.run(
                `INSERT INTO reservations (user_id, equipment_id, date, time_slot, key) 
                 VALUES (?, ?, ?, ?, ?)`, 
                [user_id, equipment_id, date, time_slot, reservationKey], 
                function(err) {
                    if (err) {
                        console.error('插入预约记录失败:', err);
                        return res.status(500).json({ error: '预约失败' });
                    }
                    console.log(`预约成功: user_id=${user_id}, equipment_id=${equipment_id}, date=${date}, time_slot=${time_slot}, key=${reservationKey}`);
                    db.run(`UPDATE equipment SET status = 'occupied' WHERE equipment_id = ?`, [equipment_id], (err) => {
                        if (err) {
                            console.error('更新设备状态失败:', err);
                        }
                    });
                    res.json({ 
                        success: true, 
                        reservation_id: this.lastID,
                        key: reservationKey
                    });
                }
            );
        });
    });
});

app.get('/check-reservation', (req, res) => {
    const { user_id, equipment_id } = req.query;
    if (!user_id || !equipment_id) {
        return res.status(400).json({ error: '用户ID和设备ID不能为空' });
    }

    db.get(`SELECT status FROM equipment WHERE equipment_id = ?`, [equipment_id], (err, equipment) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        console.log(`设备状态: equipment_id=${equipment_id}, status=${equipment?.status}`);
        if (!equipment || equipment.status === 'fault') {
            return res.json({ valid: false, message: '设备故障或不存在' });
        }

        const currentTime = new Date();
        console.log('当前服务器时间:', currentTime.toISOString());
        const currentDate = currentTime.toISOString().split('T')[0];
        const options = { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', hour12: false };
        const [currentHour, currentMinute] = new Date().toLocaleString('zh-CN', options).split(':').map(Number);
        const currentTotalMinutes = currentHour * 60 + currentMinute;

        db.get(`SELECT date, time_slot FROM reservations WHERE user_id = ? AND equipment_id = ?`, [user_id, equipment_id], (err, row) => {
            if (err) {
                return res.status(500).json({ error: '数据库错误' });
            }
            console.log(`查询预约: user_id=${user_id}, equipment_id=${equipment_id}, result=${JSON.stringify(row)}`);
            if (!row) {
                return res.json({ valid: false, message: '没有有效的预约' });
            }

            if (row.date !== currentDate) {
                return res.json({ valid: false, message: '预约日期不匹配' });
            }

            const [start, end] = row.time_slot.split('-');
            const [startHour, startMinute] = start.split(':').map(Number);
            const [endHour, endMinute] = end.split(':').map(Number);
            let startTotalMinutes = startHour * 60 + startMinute;
            let endTotalMinutes = endHour * 60 + endMinute;

            if (endTotalMinutes < startTotalMinutes) {
                if (currentHour < 12) {
                    endTotalMinutes += 24 * 60;
                } else {
                    startTotalMinutes -= 24 * 60;
                }
            }

            console.log(`当前时间: ${currentHour}:${currentMinute}, 预约时间段: ${start}-${end}`);
            console.log(`时间比较: current=${currentTotalMinutes}, start=${startTotalMinutes}, end=${endTotalMinutes}`);

            if (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes <= endTotalMinutes) {
                res.json({ valid: true });
            } else {
                res.json({ valid: false, message: '当前时间不在预约时间段内' });
            }
        });
    });
});

app.post('/admin/update-equipment', (req, res) => {
    const { equipment_id, status } = req.body;
    if (!equipment_id || !['available', 'occupied', 'fault'].includes(status)) {
        return res.status(400).json({ error: '设备ID或状态无效' });
    }

    db.run(`UPDATE equipment SET status = ? WHERE equipment_id = ?`, [status, equipment_id], (err) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        res.json({ message: '设备状态已更新' });
    });
});

app.get('/ivcam-stream', (req, res) => {
    res.json({ stream_url: 'https://localhost:3002/video' }); // 修改为 HTTPS
});

app.get('/online-users', (req, res) => {
    res.json({ online_users: Array.from(onlineUsers) });
});

app.get('/all-reservations', (req, res) => {
    db.all(
        `SELECT r.reservation_id, r.user_id, r.equipment_id, r.date, r.time_slot, u.username 
         FROM reservations r 
         JOIN users u ON r.user_id = u.user_id`,
        [],
        (err, rows) => {
            if (err) {
                return res.status(500).json({ error: '数据库错误' });
            }
            res.json(rows);
        }
    );
});

app.post('/cancel-reservation', (req, res) => {
    const { reservation_id, equipment_id } = req.body;
    if (!reservation_id || !equipment_id) {
        return res.status(400).json({ error: '预约ID和设备ID不能为空' });
    }

    db.run(`DELETE FROM reservations WHERE reservation_id = ?`, [reservation_id], (err) => {
        if (err) {
            return res.status(500).json({ error: '删除预约失败' });
        }
        db.get(`SELECT * FROM reservations WHERE equipment_id = ? AND date = ?`, [equipment_id, new Date().toISOString().split('T')[0]], (err, row) => {
            if (err) {
                console.error('检查设备预约失败:', err);
            }
            if (!row) {
                db.run(`UPDATE equipment SET status = 'available' WHERE equipment_id = ?`, [equipment_id], (err) => {
                    if (err) {
                        console.error('更新设备状态失败:', err);
                    }
                });
            }
            wss.clients.forEach(client => {
                if (client.readyState === WebSocket.OPEN) {
                    client.send(JSON.stringify({
                        type: 'reservation_cancelled',
                        equipment_id: equipment_id
                    }));
                }
            });
        });
        res.json({ success: true });
    });
});

app.get('/check-all-reservations', (req, res) => {
    const { equipment_id, date } = req.query;
    if (!equipment_id || !date) {
        return res.status(400).json({ error: '设备ID和日期不能为空' });
    }

    db.all(
        `SELECT time_slot FROM reservations WHERE equipment_id = ? AND date = ?`,
        [equipment_id, date],
        (err, rows) => {
            if (err) {
                return res.status(500).json({ error: '数据库错误' });
            }
            res.json(rows);
        }
    );
});

app.get('/stats', (req, res) => {
    db.get(`SELECT COUNT(*) as total FROM users`, (err, userRow) => {
        if (err) {
            return res.status(500).json({ error: '数据库错误' });
        }
        db.all(
            `SELECT course_id, COUNT(*) as completions 
             FROM course_completions 
             GROUP BY course_id`,
            (err, completionRows) => {
                if (err) {
                    return res.status(500).json({ error: '数据库错误' });
                }
                res.json({
                    total_users: userRow.total,
                    course_completions: completionRows
                });
            }
        );
    });
});

app.get('/user-reservations', (req, res) => {
    const { user_id } = req.query;
    if (!user_id) {
        return res.status(400).json({ error: '用户ID不能为空' });
    }

    db.all(
        `SELECT r.reservation_id, r.user_id, r.equipment_id, r.date, r.time_slot, r.key, r.created_at, e.status  
         FROM reservations r 
         JOIN equipment e ON r.equipment_id = e.equipment_id
         WHERE r.user_id = ? 
         ORDER BY r.date DESC, r.time_slot DESC`,
        [user_id],
        (err, rows) => {
            if (err) {
                console.error('获取用户预约失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            res.json({ reservations: rows });
        }
    );
});

// 添加 /equipment-status/:id 端点
app.get('/equipment-status/:id', (req, res) => {
    const equipmentId = parseInt(req.params.id);
    const currentTime = new Date();
    const options = { timeZone: 'Asia/Shanghai', hour: '2-digit', minute: '2-digit', hour12: false };
    const [currentHour, currentMinute] = currentTime.toLocaleString('zh-CN', options).split(':').map(Number);
    const currentDate = currentTime.toISOString().split('T')[0];
    const currentTotalMinutes = currentHour * 60 + currentMinute;

    db.get(`SELECT equipment_id, status FROM equipment WHERE equipment_id = ?`, [equipmentId], (err, equipment) => {
        if (err) {
            console.error('查询设备状态失败:', err);
            return res.status(500).json({ error: '数据库错误: ' + err.message });
        }
        if (!equipment) {
            return res.status(404).json({ error: '设备不存在' });
        }

        db.all(`SELECT equipment_id, time_slot, date FROM reservations WHERE equipment_id = ?`, [equipmentId], (err, reservations) => {
            if (err) {
                console.error('查询预约失败:', err);
                return res.status(500).json({ error: '数据库错误: ' + err.message });
            }

            let isOccupied = false;
            for (const reservation of reservations) {
                if (reservation.date !== currentDate) continue;

                const [start, end] = reservation.time_slot.split('-');
                const [startHour, startMinute] = start.split(':').map(Number);
                const [endHour, endMinute] = end.split(':').map(Number);
                let startTotalMinutes = startHour * 60 + startMinute;
                let endTotalMinutes = endHour * 60 + endMinute;

                if (endTotalMinutes < startTotalMinutes) {
                    if (currentHour < 12) {
                        endTotalMinutes += 24 * 60;
                    } else {
                        startTotalMinutes -= 24 * 60;
                    }
                }

                if (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes <= endTotalMinutes) {
                    isOccupied = true;
                    break;
                }
            }

            if (!isOccupied && equipment.status === 'occupied') {
                let hasFutureReservations = false;
                for (const reservation of reservations) {
                    if (reservation.date !== currentDate) continue;
                    const [start, end] = reservation.time_slot.split('-');
                    const [startHour, startMinute] = start.split(':').map(Number);
                    const [endHour, endMinute] = end.split(':').map(Number);
                    let startTotalMinutes = startHour * 60 + startMinute;
                    let endTotalMinutes = endHour * 60 + endMinute;

                    if (endTotalMinutes < startTotalMinutes) {
                        if (currentHour < 12) {
                            endTotalMinutes += 24 * 60;
                        } else {
                            startTotalMinutes -= 24 * 60;
                        }
                    }

                    if (currentTotalMinutes < endTotalMinutes) {
                        hasFutureReservations = true;
                        break;
                    }
                }
                if (!hasFutureReservations) {
                    db.run(`UPDATE equipment SET status = 'available' WHERE equipment_id = ?`, [equipmentId], (updateErr) => {
                        if (updateErr) {
                            console.error(`更新设备 ${equipmentId} 状态失败:`, updateErr);
                        } else {
                            console.log(`设备 ${equipmentId} 状态已更新为 available`);
                        }
                    });
                    equipment.status = 'available';
                }
            }

            res.json({
                equipment_id: equipment.equipment_id,
                status: equipment.status
            });
        });
    });
});

// 生成随机密钥的功能
function generateReservationKey(length = 8) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789';
    let key = '';
    for (let i = 0; i < length; i++) {
        key += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return key;
}

// API - 验证密钥
app.post('/api/verify_key', (req, res) => {
    const { key } = req.body;
    
    if (!key) {
        return res.status(400).json({ valid: false, message: '密钥不能为空' });
    }
    
    db.get(
        `SELECT r.reservation_id, r.user_id, r.equipment_id, r.date, r.time_slot, r.created_at, u.username, u.email 
         FROM reservations r 
         JOIN users u ON r.user_id = u.user_id 
         WHERE r.key = ?`,
        [key],
        (err, reservation) => {
            if (err) {
                console.error('查询预约信息失败:', err);
                return res.status(500).json({ valid: false, message: '服务器内部错误' });
            }
            
            if (!reservation) {
                return res.status(404).json({ valid: false, message: '无效的密钥或预约已过期' });
            }
            
            // 检查预约是否在有效期内
            const currentDate = new Date().toISOString().split('T')[0];
            if (reservation.date < currentDate) {
                return res.status(403).json({ valid: false, message: '预约已过期' });
            }
            
            // 验证成功，返回用户和预约信息
            return res.json({
                valid: true,
                user_info: {
                    user_id: reservation.user_id,
                    username: reservation.username,
                    email: reservation.email
                },
                equipment_id: reservation.equipment_id,
                reservation: {
                    reservation_id: reservation.reservation_id,
                    date: reservation.date,
                    time_slot: reservation.time_slot,
                    equipment_id: reservation.equipment_id,
                    created_at: reservation.created_at
                }
            });
        }
    );
});

// API - 获取预约信息（需要授权）
app.get('/api/reservation', (req, res) => {
    const authHeader = req.headers.authorization;
    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        return res.status(401).json({ message: '需要身份验证' });
    }
    
    const key = authHeader.split(' ')[1];
    
    db.get(
        `SELECT r.reservation_id, r.user_id, r.equipment_id, r.date, r.time_slot, r.created_at, u.username, u.email 
         FROM reservations r 
         JOIN users u ON r.user_id = u.user_id 
         WHERE r.key = ?`,
        [key],
        (err, reservation) => {
            if (err) {
                console.error('查询预约信息失败:', err);
                return res.status(500).json({ message: '服务器内部错误' });
            }
            
            if (!reservation) {
                return res.status(404).json({ message: '找不到预约信息' });
            }
            
            return res.json({
                reservation_id: reservation.reservation_id,
                date: reservation.date,
                time_slot: reservation.time_slot,
                equipment_id: reservation.equipment_id,
                created_at: reservation.created_at,
                user: {
                    user_id: reservation.user_id,
                    username: reservation.username,
                    email: reservation.email
                }
            });
        }
    );
});

// 修改预约API，添加密钥生成
app.post('/api/reserve', (req, res) => {
    const { userId, equipmentId, date, timeSlot } = req.body;
    
    if (!userId || !equipmentId || !date || !timeSlot) {
        return res.status(400).json({ success: false, message: '缺少必要参数' });
    }
    
    // 生成预约密钥
    const reservationKey = generateReservationKey();
    
    // 检查设备是否可用
    db.get(
        `SELECT status FROM equipment WHERE equipment_id = ?`,
        [equipmentId],
        (err, equipment) => {
            if (err) {
                console.error('查询设备状态失败:', err);
                return res.status(500).json({ success: false, message: '服务器内部错误' });
            }
            
            if (!equipment) {
                return res.status(404).json({ success: false, message: '找不到指定的设备' });
            }
            
            if (equipment.status !== 'available') {
                return res.status(403).json({ success: false, message: '该设备当前不可用' });
            }
            
            // 检查该时间段是否已被预约
            db.get(
                `SELECT reservation_id FROM reservations 
                 WHERE equipment_id = ? AND date = ? AND time_slot = ?`,
                [equipmentId, date, timeSlot],
                (err, existingReservation) => {
                    if (err) {
                        console.error('查询预约冲突失败:', err);
                        return res.status(500).json({ success: false, message: '服务器内部错误' });
                    }
                    
                    if (existingReservation) {
                        return res.status(409).json({ success: false, message: '所选时间段已被预约' });
                    }
                    
                    // 创建新预约
                    db.run(
                        `INSERT INTO reservations (user_id, equipment_id, date, time_slot, key) 
                         VALUES (?, ?, ?, ?, ?)`,
                        [userId, equipmentId, date, timeSlot, reservationKey],
                        function(err) {
                            if (err) {
                                console.error('创建预约失败:', err);
                                return res.status(500).json({ success: false, message: '服务器内部错误' });
                            }
                            
                            // 更新设备状态
                            db.run(
                                `UPDATE equipment SET status = 'occupied' WHERE equipment_id = ?`,
                                [equipmentId],
                                (err) => {
                                    if (err) {
                                        console.error('更新设备状态失败:', err);
                                    }
                                }
                            );
                            
                            return res.json({ 
                                success: true, 
                                reservation_id: this.lastID,
                                key: reservationKey,
                                message: '预约成功' 
                            });
                        }
                    );
                }
            );
        }
    );
});

// 课程上传API
app.post('/upload-course', (req, res) => {
    const { user_id, title, description, category, content } = req.body;
    
    if (!user_id || !title || !description || !category) {
        return res.status(400).json({ error: '缺少必要的课程信息' });
    }
    
    db.run(
        `INSERT INTO courses (user_id, title, description, category, content) 
         VALUES (?, ?, ?, ?, ?)`,
        [user_id, title, description, category, content || ''],
        function(err) {
            if (err) {
                console.error('上传课程失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            const course_id = this.lastID;
            res.json({ 
                success: true, 
                course_id: course_id,
                message: '课程已上传，等待审核' 
            });
        }
    );
});

// 获取用户上传的课程
app.get('/user-courses', (req, res) => {
    const { user_id } = req.query;
    if (!user_id) {
        return res.status(400).json({ error: '用户ID不能为空' });
    }
    
    db.all(
        `SELECT * FROM courses WHERE user_id = ? ORDER BY created_at DESC`,
        [user_id],
        (err, rows) => {
            if (err) {
                console.error('获取用户课程失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            res.json({ courses: rows });
        }
    );
});

// 管理员获取课程列表
app.get('/admin/courses', (req, res) => {
    const { status } = req.query;
    let query = `SELECT c.*, u.username 
                FROM courses c
                LEFT JOIN users u ON c.user_id = u.user_id`;
    
    const params = [];
    if (status && status !== 'all') {
        query += ` WHERE c.status = ?`;
        params.push(status);
    }
    
    query += ` ORDER BY c.created_at DESC`;
    
    db.all(query, params, (err, rows) => {
        if (err) {
            console.error('获取课程列表失败:', err);
            return res.status(500).json({ error: '数据库错误' });
        }
        
        res.json({ courses: rows });
    });
});

// 管理员审核通过课程
app.post('/admin/approve-course', (req, res) => {
    const { course_id } = req.body;
    
    if (!course_id) {
        return res.status(400).json({ error: '课程ID不能为空' });
    }
    
    db.run(
        `UPDATE courses SET status = 'approved', feedback = NULL WHERE course_id = ?`,
        [course_id],
        function(err) {
            if (err) {
                console.error('审核课程失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            if (this.changes === 0) {
                return res.status(404).json({ error: '找不到指定课程' });
            }
            
            res.json({ success: true, message: '课程已通过审核' });
        }
    );
});

// 管理员拒绝课程
app.post('/admin/reject-course', (req, res) => {
    const { course_id, feedback } = req.body;
    
    if (!course_id) {
        return res.status(400).json({ error: '课程ID不能为空' });
    }
    
    db.run(
        `UPDATE courses SET status = 'rejected', feedback = ? WHERE course_id = ?`,
        [feedback || null, course_id],
        function(err) {
            if (err) {
                console.error('拒绝课程失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            if (this.changes === 0) {
                return res.status(404).json({ error: '找不到指定课程' });
            }
            
            res.json({ success: true, message: '课程已被拒绝' });
        }
    );
});

// 获取所有已通过的课程（用于主页展示）
app.get('/approved-courses', (req, res) => {
    db.all(
        `SELECT c.*, u.username 
         FROM courses c
         LEFT JOIN users u ON c.user_id = u.user_id
         WHERE c.status = 'approved'
         ORDER BY c.created_at DESC`,
        [],
        (err, rows) => {
            if (err) {
                console.error('获取已通过课程失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            res.json({ courses: rows });
        }
    );
});

// 获取特定课程详情
app.get('/course/:id', (req, res) => {
    const courseId = req.params.id;
    
    db.get(
        `SELECT c.*, u.username 
         FROM courses c
         LEFT JOIN users u ON c.user_id = u.user_id
         WHERE c.course_id = ?`,
        [courseId],
        (err, course) => {
            if (err) {
                console.error('获取课程详情失败:', err);
                return res.status(500).json({ error: '数据库错误' });
            }
            
            if (!course) {
                return res.status(404).json({ error: '课程不存在' });
            }
            
            res.json({ course });
        }
    );
});