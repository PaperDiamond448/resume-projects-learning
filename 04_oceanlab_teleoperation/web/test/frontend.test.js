/**
 * OceanLab网页应用前端测试
 * 
 * 此文件包含对前端页面功能的测试
 */

const puppeteer = require('puppeteer');
const { server } = require('../server');
const chai = require('chai');
const chaiHttp = require('chai-http');
const expect = chai.expect;
const sinon = require('sinon');
const path = require('path');
const fs = require('fs');

chai.use(chaiHttp);

// 测试配置
const BASE_URL = 'http://localhost:3002';
const HEADLESS = true; // 设置为false可以查看浏览器测试过程

describe('前端页面测试', () => {
  let browser;
  let page;
  
  // 在所有测试前启动浏览器
  beforeAll(async () => {
    browser = await puppeteer.launch({ 
      headless: HEADLESS,
      args: ['--no-sandbox', '--disable-setuid-sandbox'] 
    });
    page = await browser.newPage();
    // 设置截图目录
    page.setViewport({ width: 1280, height: 800 });
  });
  
  // 在所有测试后关闭浏览器
  afterAll(async () => {
    await browser.close();
    await new Promise((resolve) => {
      server.close(resolve);
    });
  });
  
  // 测试首页加载
  test('首页应该正确加载', async () => {
    await page.goto(`${BASE_URL}/`, { waitUntil: 'networkidle0' });
    
    // 获取页面标题
    const title = await page.title();
    expect(title).toContain('OceanLab');
    
    // 检查关键元素是否存在
    const headerExists = await page.$('header');
    expect(headerExists).toBeTruthy();
    
    const footerExists = await page.$('footer');
    expect(footerExists).toBeTruthy();
    
    // 截图，用于答辩展示
    await page.screenshot({ path: './test/screenshots/homepage.png' });
  }, 10000);
  
  // 测试登录页面
  test('登录页面应该正确工作', async () => {
    // 导航到登录页面（假设通过点击登录按钮）
    await page.goto(`${BASE_URL}/`, { waitUntil: 'networkidle0' });
    await page.click('a[href="#login"]');
    
    // 等待登录表单显示
    await page.waitForSelector('#login-form', { visible: true });
    
    // 输入测试用户名和密码
    await page.type('#username', 'testuser');
    await page.type('#password', 'Test123456');
    
    // 截图登录表单
    await page.screenshot({ path: './test/screenshots/login-form.png' });
    
    // 点击登录按钮
    const [response] = await Promise.all([
      page.waitForNavigation({ waitUntil: 'networkidle0' }),
      page.click('#login-button')
    ]);
    
    // 验证登录后的重定向
    const url = page.url();
    expect(url).not.toBe(`${BASE_URL}/login`);
    
    // 检查是否成功登录（例如检查用户名显示）
    const userElement = await page.$('.user-profile');
    expect(userElement).toBeTruthy();
  }, 15000);
  
  // 测试预约页面
  test('预约页面应该正确显示', async () => {
    // 确保用户已登录
    if (!await page.$('.user-profile')) {
      // 如果没有登录，先执行登录步骤
      await page.goto(`${BASE_URL}/`, { waitUntil: 'networkidle0' });
      await page.click('a[href="#login"]');
      await page.waitForSelector('#login-form', { visible: true });
      await page.type('#username', 'testuser');
      await page.type('#password', 'Test123456');
      await Promise.all([
        page.waitForNavigation({ waitUntil: 'networkidle0' }),
        page.click('#login-button')
      ]);
    }
    
    // 导航到预约页面
    await page.goto(`${BASE_URL}/equipment_status.html`, { waitUntil: 'networkidle0' });
    
    // 检查预约表单是否存在
    const reservationForm = await page.$('.reservation-form');
    expect(reservationForm).toBeTruthy();
    
    // 检查设备列表是否存在
    const equipmentList = await page.$('.equipment-list');
    expect(equipmentList).toBeTruthy();
    
    // 截图预约页面
    await page.screenshot({ path: './test/screenshots/reservation-page.png' });
    
    // 选择一个设备
    await page.click('.equipment-item:first-child');
    
    // 选择日期（假设有日期选择器）
    await page.click('#date-picker');
    await page.click('.date-cell:not(.disabled)'); // 选择一个可用日期
    
    // 选择时间段
    await page.select('#time-slot', '10:00-12:00');
    
    // 截图完整的预约表单
    await page.screenshot({ path: './test/screenshots/filled-reservation-form.png' });
    
    // 提交预约
    await Promise.all([
      page.waitForResponse(response => response.url().includes('/api/reservation/create')),
      page.click('#submit-reservation')
    ]);
    
    // 检查成功消息
    const successMessage = await page.$('.success-message');
    expect(successMessage).toBeTruthy();
    
    // 截图预约成功页面
    await page.screenshot({ path: './test/screenshots/reservation-success.png' });
  }, 20000);
  
  // 测试课程页面
  test('课程页面应该正确显示', async () => {
    await page.goto(`${BASE_URL}/course1.html`, { waitUntil: 'networkidle0' });
    
    // 检查课程标题是否存在
    const courseTitle = await page.$('.course-title');
    expect(courseTitle).toBeTruthy();
    
    // 检查课程内容是否存在
    const courseContent = await page.$('.course-content');
    expect(courseContent).toBeTruthy();
    
    // 截图课程页面
    await page.screenshot({ path: './test/screenshots/course-page.png' });
  }, 10000);
  
  // 测试响应式设计
  test('页面应该是响应式的', async () => {
    await page.goto(`${BASE_URL}/`, { waitUntil: 'networkidle0' });
    
    // 桌面视图截图
    await page.setViewport({ width: 1280, height: 800 });
    await page.screenshot({ path: './test/screenshots/responsive-desktop.png' });
    
    // 平板视图截图
    await page.setViewport({ width: 768, height: 1024 });
    await page.screenshot({ path: './test/screenshots/responsive-tablet.png' });
    
    // 手机视图截图
    await page.setViewport({ width: 375, height: 667 });
    await page.screenshot({ path: './test/screenshots/responsive-mobile.png' });
    
    // 检查移动菜单是否存在（通常是汉堡菜单）
    const mobileMenu = await page.$('.mobile-menu, .hamburger-menu');
    expect(mobileMenu).toBeTruthy();
  }, 15000);
  
  // 测试错误处理
  test('应该正确处理表单错误', async () => {
    await page.goto(`${BASE_URL}/`, { waitUntil: 'networkidle0' });
    await page.click('a[href="#login"]');
    
    // 等待登录表单显示
    await page.waitForSelector('#login-form', { visible: true });
    
    // 提交空表单
    await page.click('#login-button');
    
    // 检查错误消息
    const errorMessage = await page.$('.error-message');
    expect(errorMessage).toBeTruthy();
    
    // 截图错误提示
    await page.screenshot({ path: './test/screenshots/form-error.png' });
  }, 10000);
  
  // 测试404页面
  test('应该显示自定义404页面', async () => {
    await page.goto(`${BASE_URL}/non-existent-page`, { waitUntil: 'networkidle0' });
    
    // 检查是否显示404页面
    const notFoundPage = await page.$('.not-found-page');
    expect(notFoundPage).toBeTruthy();
    
    // 截图404页面
    await page.screenshot({ path: './test/screenshots/404-page.png' });
  }, 10000);
}); 