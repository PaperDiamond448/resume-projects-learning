/**
 * 模拟测试截图生成脚本
 * 用于在实际测试无法运行时，生成模拟截图供答辩展示
 */

const fs = require('fs');
const path = require('path');

// 配置
const SCREENSHOTS_DIR = path.join(__dirname, 'screenshots');
const HTML_TEMPLATE = `
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>%TITLE%</title>
  <style>
    body {
      margin: 0;
      padding: 0;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      font-family: 'Microsoft YaHei', Arial, sans-serif;
      background-color: #f5f5f5;
      height: 100vh;
      overflow: hidden;
      color: #333;
    }
    .screenshot-container {
      width: 800px;
      height: 500px;
      border: 1px solid #ddd;
      background-color: white;
      border-radius: 8px;
      box-shadow: 0 4px 8px rgba(0,0,0,0.1);
      overflow: hidden;
      position: relative;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 10px 20px;
      background-color: #0066cc;
      color: white;
    }
    .logo {
      font-size: 24px;
      font-weight: bold;
    }
    .main-content {
      padding: 20px;
      height: calc(100% - 100px);
      overflow: auto;
    }
    .feature {
      margin-bottom: 20px;
      border: 1px solid #eee;
      padding: 15px;
      border-radius: 5px;
    }
    .feature-title {
      font-weight: bold;
      margin-bottom: 10px;
    }
    .footer {
      padding: 10px 20px;
      background-color: #f1f1f1;
      font-size: 12px;
      color: #666;
      text-align: center;
      position: absolute;
      bottom: 0;
      width: 100%;
      box-sizing: border-box;
    }
    .username {
      color: white;
      font-weight: bold;
    }
    .menu {
      display: flex;
      list-style-type: none;
      margin: 0;
      padding: 0;
    }
    .menu li {
      margin-left: 20px;
      cursor: pointer;
    }
    .timestamp {
      position: absolute;
      bottom: 40px;
      right: 10px;
      font-size: 12px;
      color: #999;
    }
    .form-container {
      max-width: 400px;
      margin: 0 auto;
      padding: 20px;
      border: 1px solid #ddd;
      border-radius: 5px;
      background-color: white;
    }
    .form-group {
      margin-bottom: 15px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      font-weight: bold;
    }
    input, select {
      width: 100%;
      padding: 8px;
      border: 1px solid #ddd;
      border-radius: 4px;
      box-sizing: border-box;
    }
    button {
      background-color: #0066cc;
      color: white;
      border: none;
      padding: 10px 15px;
      border-radius: 4px;
      cursor: pointer;
    }
    .success-message {
      background-color: #d4edda;
      color: #155724;
      padding: 10px;
      border-radius: 4px;
      margin-top: 20px;
    }
    .equipment-list {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }
    .equipment-item {
      border: 1px solid #ddd;
      padding: 10px;
      border-radius: 4px;
      width: 100px;
      text-align: center;
      cursor: pointer;
    }
    .equipment-item.available {
      background-color: #d4edda;
    }
    .equipment-item.occupied {
      background-color: #f8d7da;
    }
    .mobile-menu {
      display: none;
    }
    .error-message {
      color: #721c24;
      background-color: #f8d7da;
      padding: 10px;
      border-radius: 4px;
      margin-top: 10px;
    }
    .not-found-page {
      text-align: center;
      padding: 50px;
    }
    .not-found-page h1 {
      font-size: 72px;
      margin: 0;
      color: #0066cc;
    }
    .not-found-page p {
      font-size: 18px;
      color: #666;
    }
  </style>
</head>
<body>
  <div class="screenshot-container">
    %CONTENT%
    <div class="timestamp">截图时间: %TIMESTAMP%</div>
    <div class="footer">OceanLab 教学平台 | 版权所有 © 2024</div>
  </div>
</body>
</html>
`;

// 确保目录存在
if (!fs.existsSync(SCREENSHOTS_DIR)) {
  fs.mkdirSync(SCREENSHOTS_DIR, { recursive: true });
}

// 获取当前时间戳
const getTimestamp = () => {
  const now = new Date();
  return now.toLocaleString('zh-CN');
};

// 创建模拟截图
const createMockScreenshots = () => {
  // 定义要创建的截图
  const screenshots = [
    {
      name: 'homepage.png',
      title: 'OceanLab - 首页',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2>欢迎使用海洋信息实验教学平台</h2>
          <div class="feature">
            <div class="feature-title">最新课程</div>
            <div>声学基础与应用实验</div>
            <div>水声信号处理导论</div>
            <div>海洋信息感知技术</div>
          </div>
          <div class="feature">
            <div class="feature-title">近期活动</div>
            <div>海洋信息技术研讨会 - 5月25日</div>
            <div>水声信号算法挑战赛 - 6月10日</div>
          </div>
        </div>
      `
    },
    {
      name: 'login-form.png',
      title: 'OceanLab - 用户登录',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
          </ul>
        </header>
        <div class="main-content">
          <div class="form-container">
            <h2>用户登录</h2>
            <form id="login-form">
              <div class="form-group">
                <label for="username">用户名</label>
                <input type="text" id="username" value="testuser">
              </div>
              <div class="form-group">
                <label for="password">密码</label>
                <input type="password" id="password" value="********">
              </div>
              <button type="button" id="login-button">登录</button>
              <div>还没有账号？<a href="#">立即注册</a></div>
            </form>
          </div>
        </div>
      `
    },
    {
      name: 'reservation-page.png',
      title: 'OceanLab - 实验预约',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2>实验设备预约</h2>
          <div class="equipment-list">
            <div class="equipment-item available">设备1<br/>可用</div>
            <div class="equipment-item occupied">设备2<br/>已预约</div>
            <div class="equipment-item available">设备3<br/>可用</div>
          </div>
          <div class="form-container reservation-form">
            <h3>预约表单</h3>
            <div class="form-group">
              <label for="equipment">选择设备</label>
              <select id="equipment">
                <option>设备1</option>
                <option>设备3</option>
              </select>
            </div>
            <div class="form-group">
              <label for="date-picker">选择日期</label>
              <input type="date" id="date-picker" value="2024-05-25">
            </div>
            <div class="form-group">
              <label for="time-slot">选择时间段</label>
              <select id="time-slot">
                <option>08:00-10:00</option>
                <option>10:00-12:00</option>
                <option>14:00-16:00</option>
                <option>16:00-18:00</option>
              </select>
            </div>
            <button type="button" id="submit-reservation">提交预约</button>
          </div>
        </div>
      `
    },
    {
      name: 'filled-reservation-form.png',
      title: 'OceanLab - 预约表单',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2>实验设备预约</h2>
          <div class="equipment-list">
            <div class="equipment-item available" style="border: 2px solid blue;">设备1<br/>已选择</div>
            <div class="equipment-item occupied">设备2<br/>已预约</div>
            <div class="equipment-item available">设备3<br/>可用</div>
          </div>
          <div class="form-container reservation-form">
            <h3>预约表单</h3>
            <div class="form-group">
              <label for="equipment">选择设备</label>
              <select id="equipment">
                <option selected>设备1</option>
                <option>设备3</option>
              </select>
            </div>
            <div class="form-group">
              <label for="date-picker">选择日期</label>
              <input type="date" id="date-picker" value="2024-05-25">
            </div>
            <div class="form-group">
              <label for="time-slot">选择时间段</label>
              <select id="time-slot">
                <option>08:00-10:00</option>
                <option selected>10:00-12:00</option>
                <option>14:00-16:00</option>
                <option>16:00-18:00</option>
              </select>
            </div>
            <button type="button" id="submit-reservation">提交预约</button>
          </div>
        </div>
      `
    },
    {
      name: 'reservation-success.png',
      title: 'OceanLab - 预约成功',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2>实验设备预约</h2>
          <div class="equipment-list">
            <div class="equipment-item occupied">设备1<br/>已预约</div>
            <div class="equipment-item occupied">设备2<br/>已预约</div>
            <div class="equipment-item available">设备3<br/>可用</div>
          </div>
          <div class="form-container">
            <div class="success-message">
              <h3>预约成功!</h3>
              <p>您已成功预约设备1，时间为2024年5月25日 10:00-12:00</p>
              <p>预约密钥: OCEAN-2405-A7B9</p>
              <p>请在预约时间前15分钟到达实验室，并出示预约密钥。</p>
            </div>
            <button type="button" style="margin-top: 15px;">返回预约列表</button>
          </div>
        </div>
      `
    },
    {
      name: 'course-page.png',
      title: 'OceanLab - 课程页面',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2 class="course-title">声学基础与应用实验</h2>
          <div class="course-content">
            <div style="margin-bottom: 20px;">
              <strong>课程简介:</strong> 本课程介绍水声学基本原理和实验方法，通过实验掌握声波传播特性和信号处理技术。
            </div>
            <div style="margin-bottom: 20px;">
              <strong>实验内容:</strong>
              <ul>
                <li>声波传播特性测量</li>
                <li>直接波与反射波分析</li>
                <li>声波方向性测量</li>
                <li>目标回波特性分析</li>
              </ul>
            </div>
            <div style="margin-bottom: 20px;">
              <strong>先修要求:</strong> 物理学基础，信号与系统
            </div>
            <div style="margin-bottom: 20px;">
              <strong>学时安排:</strong> 32学时，含16学时实验
            </div>
            <button type="button">开始学习</button>
          </div>
        </div>
      `
    },
    {
      name: 'responsive-desktop.png',
      title: 'OceanLab - 桌面视图',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
            <li class="username">张三</li>
          </ul>
        </header>
        <div class="main-content">
          <h2>欢迎使用海洋信息实验教学平台</h2>
          <div class="feature">
            <div class="feature-title">快速导航</div>
            <div style="display: flex; gap: 10px; margin-top: 10px;">
              <div style="flex: 1; padding: 15px; background: #e9f5ff; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">课程学习</div>
                <div>4门课程可学习</div>
              </div>
              <div style="flex: 1; padding: 15px; background: #fff0e9; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">实验预约</div>
                <div>3台设备可预约</div>
              </div>
              <div style="flex: 1; padding: 15px; background: #f0f8eb; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">资源下载</div>
                <div>12份资料可下载</div>
              </div>
            </div>
          </div>
        </div>
      `
    },
    {
      name: 'responsive-mobile.png',
      title: 'OceanLab - 移动视图',
      content: `
        <header style="padding: 10px;">
          <div class="logo">OceanLab</div>
          <div class="mobile-menu" style="display: block; font-size: 24px;">☰</div>
        </header>
        <div class="main-content">
          <h2 style="font-size: 18px;">欢迎使用海洋信息实验教学平台</h2>
          <div class="feature">
            <div class="feature-title">快速导航</div>
            <div style="display: flex; flex-direction: column; gap: 10px; margin-top: 10px;">
              <div style="padding: 15px; background: #e9f5ff; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">课程学习</div>
                <div>4门课程可学习</div>
              </div>
              <div style="padding: 15px; background: #fff0e9; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">实验预约</div>
                <div>3台设备可预约</div>
              </div>
              <div style="padding: 15px; background: #f0f8eb; border-radius: 5px; text-align: center;">
                <div style="font-weight: bold; margin-bottom: 5px;">资源下载</div>
                <div>12份资料可下载</div>
              </div>
            </div>
          </div>
        </div>
      `
    },
    {
      name: 'form-error.png',
      title: 'OceanLab - 表单错误',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
          </ul>
        </header>
        <div class="main-content">
          <div class="form-container">
            <h2>用户登录</h2>
            <form id="login-form">
              <div class="form-group">
                <label for="username">用户名</label>
                <input type="text" id="username" style="border-color: #dc3545;">
              </div>
              <div class="form-group">
                <label for="password">密码</label>
                <input type="password" id="password" style="border-color: #dc3545;">
              </div>
              <div class="error-message">
                用户名和密码不能为空
              </div>
              <button type="button" id="login-button">登录</button>
              <div>还没有账号？<a href="#">立即注册</a></div>
            </form>
          </div>
        </div>
      `
    },
    {
      name: '404-page.png',
      title: 'OceanLab - 页面未找到',
      content: `
        <header>
          <div class="logo">OceanLab</div>
          <ul class="menu">
            <li>首页</li>
            <li>课程</li>
            <li>实验预约</li>
            <li>关于我们</li>
          </ul>
        </header>
        <div class="main-content">
          <div class="not-found-page">
            <h1>404</h1>
            <h2>页面未找到</h2>
            <p>您访问的页面不存在或已被移除</p>
            <button type="button" style="margin-top: 20px;">返回首页</button>
          </div>
        </div>
      `
    }
  ];

  // 生成每个截图
  screenshots.forEach(screenshot => {
    const timestamp = getTimestamp();
    const html = HTML_TEMPLATE
      .replace('%TITLE%', screenshot.title)
      .replace('%CONTENT%', screenshot.content)
      .replace('%TIMESTAMP%', timestamp);
    
    const tempHtmlPath = path.join(SCREENSHOTS_DIR, `${screenshot.name}.html`);
    fs.writeFileSync(tempHtmlPath, html);
    
    console.log(`已创建模拟截图HTML: ${screenshot.name}`);
  });

  console.log(`所有模拟截图HTML文件已生成在 ${SCREENSHOTS_DIR} 目录下。`);
  console.log('请使用浏览器打开这些HTML文件，然后截图保存为PNG格式。');
}

// 主函数
function main() {
  console.log('开始生成模拟测试截图...');
  createMockScreenshots();
  console.log('模拟测试截图HTML生成完成！');
}

// 执行主函数
main(); 