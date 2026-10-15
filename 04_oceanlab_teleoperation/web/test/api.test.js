/**
 * OceanLab网页应用API测试
 * 
 * 此文件包含对服务器API端点的测试
 */

const request = require('supertest');
const { app, server } = require('../server');
const path = require('path');
const fs = require('fs');
const sqlite3 = require('sqlite3').verbose();

// 使用测试数据库而不是生产数据库
const TEST_DB_PATH = path.join(__dirname, 'test_oceanedu.db');

// 在所有测试前创建测试环境
beforeAll(async () => {
  // 备份原始数据库配置
  if (fs.existsSync(TEST_DB_PATH)) {
    fs.unlinkSync(TEST_DB_PATH);
  }
  
  // 创建测试数据库
  const db = new sqlite3.Database(TEST_DB_PATH);
  
  // 创建必要的表
  await new Promise((resolve, reject) => {
    db.serialize(() => {
      // 创建用户表
      db.run(`
        CREATE TABLE IF NOT EXISTS users (
          user_id INTEGER PRIMARY KEY AUTOINCREMENT,
          username TEXT UNIQUE,
          email TEXT UNIQUE,
          password TEXT
        )
      `);
      
      // 创建设备表
      db.run(`
        CREATE TABLE IF NOT EXISTS equipment (
          equipment_id INTEGER PRIMARY KEY,
          status TEXT CHECK(status IN ('available', 'occupied', 'fault'))
        )
      `);
      
      // 创建预约表
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
      
      // 添加测试设备
      db.run(`
        INSERT INTO equipment (equipment_id, status)
        VALUES (1, 'available'), (2, 'available'), (3, 'available')
      `, (err) => {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  });
  
  // 关闭测试数据库连接
  await new Promise((resolve) => {
    db.close(resolve);
  });
  
  // 配置应用使用测试数据库
  // 注意：这里假设server.js中的数据库连接是可配置的
  // 实际实现可能需要修改server.js来支持测试
  process.env.DB_PATH = TEST_DB_PATH;
});

// 在所有测试后清理测试环境
afterAll(async () => {
  // 关闭服务器
  await new Promise((resolve) => {
    server.close(resolve);
  });
  
  // 删除测试数据库
  if (fs.existsSync(TEST_DB_PATH)) {
    fs.unlinkSync(TEST_DB_PATH);
  }
});

// 测试用户API
describe('用户API测试', () => {
  const testUser = {
    username: 'testuser',
    email: 'test@example.com',
    password: 'Test123456'
  };
  
  let authToken;
  
  test('应该能注册新用户', async () => {
    const response = await request(app)
      .post('/api/register')
      .send(testUser);
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('success', true);
    expect(response.body).toHaveProperty('message');
  });
  
  test('应该能使用有效凭据登录', async () => {
    const response = await request(app)
      .post('/api/login')
      .send({
        username: testUser.username,
        password: testUser.password
      });
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('success', true);
    expect(response.body).toHaveProperty('token');
    
    // 保存令牌用于后续测试
    authToken = response.body.token;
  });
  
  test('应该能获取用户资料', async () => {
    const response = await request(app)
      .get('/api/user/profile')
      .set('Authorization', `Bearer ${authToken}`);
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('username', testUser.username);
    expect(response.body).toHaveProperty('email', testUser.email);
  });
});

// 测试设备预约API
describe('预约API测试', () => {
  let authToken;
  
  // 创建测试用户并登录
  beforeAll(async () => {
    // 注册测试用户
    await request(app)
      .post('/api/register')
      .send({
        username: 'reservationuser',
        email: 'reservation@example.com',
        password: 'Reservation123'
      });
    
    // 登录测试用户
    const loginResponse = await request(app)
      .post('/api/login')
      .send({
        username: 'reservationuser',
        password: 'Reservation123'
      });
    
    authToken = loginResponse.body.token;
  });
  
  test('应该能查询可用设备', async () => {
    const response = await request(app)
      .get('/api/equipment/available')
      .set('Authorization', `Bearer ${authToken}`);
    
    expect(response.statusCode).toBe(200);
    expect(Array.isArray(response.body)).toBe(true);
    expect(response.body.length).toBeGreaterThan(0);
  });
  
  test('应该能创建新预约', async () => {
    // 获取当前日期
    const today = new Date();
    const date = today.toISOString().split('T')[0];
    
    const response = await request(app)
      .post('/api/reservation/create')
      .set('Authorization', `Bearer ${authToken}`)
      .send({
        equipment_id: 1,
        date: date,
        time_slot: '10:00-12:00'
      });
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('success', true);
    expect(response.body).toHaveProperty('reservation_id');
    expect(response.body).toHaveProperty('key');
  });
  
  test('应该能查询用户预约', async () => {
    const response = await request(app)
      .get('/api/reservation/user')
      .set('Authorization', `Bearer ${authToken}`);
    
    expect(response.statusCode).toBe(200);
    expect(Array.isArray(response.body)).toBe(true);
    expect(response.body.length).toBeGreaterThan(0);
  });
  
  test('应该能验证预约密钥', async () => {
    // 先获取用户预约
    const reservationsResponse = await request(app)
      .get('/api/reservation/user')
      .set('Authorization', `Bearer ${authToken}`);
    
    const reservation = reservationsResponse.body[0];
    
    const response = await request(app)
      .post('/api/verify-key')
      .send({
        key: reservation.key
      });
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('valid', true);
    expect(response.body).toHaveProperty('equipment_id');
  });
});

// 测试课程API
describe('课程API测试', () => {
  let authToken;
  
  // 创建测试用户并登录
  beforeAll(async () => {
    // 注册测试用户
    await request(app)
      .post('/api/register')
      .send({
        username: 'courseuser',
        email: 'course@example.com',
        password: 'Course123'
      });
    
    // 登录测试用户
    const loginResponse = await request(app)
      .post('/api/login')
      .send({
        username: 'courseuser',
        password: 'Course123'
      });
    
    authToken = loginResponse.body.token;
  });
  
  test('应该能获取课程列表', async () => {
    const response = await request(app)
      .get('/api/courses');
    
    expect(response.statusCode).toBe(200);
    expect(Array.isArray(response.body)).toBe(true);
  });
  
  test('应该能提交新课程', async () => {
    const response = await request(app)
      .post('/api/courses/submit')
      .set('Authorization', `Bearer ${authToken}`)
      .send({
        title: '测试课程',
        description: '这是一个测试课程',
        category: '声学基础',
        content: '# 测试课程内容\n\n这是课程的详细内容。'
      });
    
    expect(response.statusCode).toBe(200);
    expect(response.body).toHaveProperty('success', true);
    expect(response.body).toHaveProperty('course_id');
  });
});

// 测试错误处理
describe('错误处理测试', () => {
  test('应该返回404错误当访问不存在的页面', async () => {
    const response = await request(app)
      .get('/non-existent-page');
    
    expect(response.statusCode).toBe(404);
  });
  
  test('应该拒绝未授权请求', async () => {
    const response = await request(app)
      .get('/api/user/profile');
    
    expect(response.statusCode).toBe(401);
  });
  
  test('应该检测并拒绝无效输入', async () => {
    const response = await request(app)
      .post('/api/register')
      .send({
        username: 'u', // 用户名太短
        email: 'invalid-email', // 无效邮箱
        password: '123' // 密码太短
      });
    
    expect(response.statusCode).toBe(400);
    expect(response.body).toHaveProperty('errors');
  });
}); 