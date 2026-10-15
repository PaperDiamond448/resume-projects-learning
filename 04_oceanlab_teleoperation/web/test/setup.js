/**
 * OceanLab网页应用测试环境设置脚本
 * 此脚本用于准备测试环境和检查依赖
 */

const fs = require('fs');
const path = require('path');
const { execSync, exec } = require('child_process');

// 配置目录
const TEST_DIR = __dirname;
const PROJECT_ROOT = path.join(TEST_DIR, '..');
const SCREENSHOTS_DIR = path.join(TEST_DIR, 'screenshots');
const COVERAGE_DIR = path.join(TEST_DIR, 'coverage');

// 控制台颜色
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m'
};

/**
 * 打印带颜色的信息
 */
function log(message, color = 'reset') {
  console.log(`${colors[color]}${message}${colors.reset}`);
}

/**
 * 检查命令是否存在
 */
function commandExists(command) {
  try {
    execSync(command + ' --version', { stdio: 'ignore' });
    return true;
  } catch (e) {
    return false;
  }
}

/**
 * 安装必要的依赖
 */
function installDependencies() {
  log('检查和安装必要的依赖...', 'cyan');
  
  const requiredDeps = [
    'jest',
    'supertest',
    'puppeteer'
  ];
  
  // 检查package.json
  const packageJsonPath = path.join(PROJECT_ROOT, 'package.json');
  let packageJson;
  
  try {
    packageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf8'));
  } catch (err) {
    log('错误: 无法读取package.json', 'red');
    process.exit(1);
  }
  
  // 确保devDependencies存在
  packageJson.devDependencies = packageJson.devDependencies || {};
  
  // 检查每个依赖
  const missingDeps = [];
  requiredDeps.forEach(dep => {
    if (!packageJson.devDependencies[dep] && !packageJson.dependencies?.[dep]) {
      missingDeps.push(dep);
    }
  });
  
  // 安装缺失的依赖
  if (missingDeps.length > 0) {
    log(`需要安装以下依赖: ${missingDeps.join(', ')}`, 'yellow');
    try {
      log('正在安装依赖...这可能需要几分钟时间', 'cyan');
      execSync(`npm install --save-dev ${missingDeps.join(' ')}`, { 
        cwd: PROJECT_ROOT,
        stdio: 'inherit'
      });
      log('依赖安装成功!', 'green');
    } catch (error) {
      log('安装依赖时出错! 请手动运行: npm install --save-dev ' + missingDeps.join(' '), 'red');
      log('错误详情: ' + error.message, 'red');
    }
  } else {
    log('所有必要的依赖已安装', 'green');
  }
  
  // 检查Jest命令是否可用
  const jestAvailable = commandExists('jest') || commandExists('npx jest');
  if (!jestAvailable) {
    log('Jest命令不可用。尝试全局安装Jest...', 'yellow');
    try {
      execSync('npm install -g jest', { stdio: 'inherit' });
      log('Jest已全局安装成功!', 'green');
    } catch (error) {
      log('警告: 全局安装Jest失败。将使用npx运行Jest。', 'yellow');
      log('错误详情: ' + error.message, 'yellow');
    }
  } else {
    log('Jest命令可用', 'green');
  }
  
  // 检查Jest配置
  if (!packageJson.jest) {
    log('添加Jest配置到package.json', 'yellow');
    packageJson.jest = {
      testEnvironment: 'node',
      coverageDirectory: './test/coverage'
    };
    
    // 保存更新后的package.json
    fs.writeFileSync(packageJsonPath, JSON.stringify(packageJson, null, 2));
    log('Jest配置已更新', 'green');
  }
  
  // 添加脚本
  if (!packageJson.scripts || !packageJson.scripts.test) {
    log('添加测试脚本到package.json', 'yellow');
    packageJson.scripts = packageJson.scripts || {};
    packageJson.scripts.test = 'jest --coverage';
    packageJson.scripts['test:api'] = 'jest api.test.js';
    packageJson.scripts['test:frontend'] = 'jest frontend.test.js';
    packageJson.scripts['test:with-screenshots'] = 'jest --coverage && node ./test/generate-report.js';
    
    // 保存更新后的package.json
    fs.writeFileSync(packageJsonPath, JSON.stringify(packageJson, null, 2));
    log('测试脚本已添加', 'green');
  }
}

/**
 * 创建目录结构
 */
function createDirectories() {
  log('创建测试目录结构...', 'cyan');
  
  // 创建screenshots目录
  if (!fs.existsSync(SCREENSHOTS_DIR)) {
    fs.mkdirSync(SCREENSHOTS_DIR, { recursive: true });
    log(`创建目录: ${SCREENSHOTS_DIR}`, 'green');
  }
  
  // 创建coverage目录
  if (!fs.existsSync(COVERAGE_DIR)) {
    fs.mkdirSync(COVERAGE_DIR, { recursive: true });
    log(`创建目录: ${COVERAGE_DIR}`, 'green');
  }
  
  // 创建示例截图
  const exampleScreenshotPath = path.join(SCREENSHOTS_DIR, 'example.png');
  if (!fs.existsSync(exampleScreenshotPath)) {
    // 创建一个简单的文本提示图片
    try {
      // 如果无法创建图片，只是记录一个消息
      log('无法创建示例截图，测试过程中会自动生成截图', 'yellow');
    } catch (err) {
      log('注意: 无法创建示例截图文件', 'yellow');
    }
  }
}

/**
 * 验证服务器配置
 */
function validateServerConfig() {
  log('验证服务器配置...', 'cyan');
  
  const serverPath = path.join(PROJECT_ROOT, 'server.js');
  if (!fs.existsSync(serverPath)) {
    log('警告: 找不到server.js文件', 'yellow');
    return;
  }
  
  log('服务器配置验证完成', 'green');
}

/**
 * 检查和设置中文编码
 */
function setupEncoding() {
  log('检查并设置编码环境...', 'cyan');
  
  try {
    // 尝试设置命令行编码为UTF-8
    execSync('chcp 65001', { stdio: 'ignore' });
    log('已设置控制台编码为UTF-8', 'green');
    
    // 验证Node.js环境变量包含正确的编码
    if (!process.env.NODE_ENV) {
      process.env.NODE_ENV = 'test';
    }
    
    // 确保puppeteer使用正确的编码
    process.env.LANG = 'zh_CN.UTF-8';
    process.env.LANGUAGE = 'zh_CN.UTF-8';
    process.env.LC_ALL = 'zh_CN.UTF-8';
    
    log('Node.js环境变量已设置', 'green');
  } catch (error) {
    log('设置编码环境时遇到问题，但将继续执行测试', 'yellow');
  }
}

/**
 * 主函数
 */
function main() {
  log('\n========== OceanLab网页应用测试设置 ==========', 'bright');
  
  setupEncoding();
  installDependencies();
  createDirectories();
  validateServerConfig();
  
  log('\n测试环境设置完成!', 'green');
  log('可以运行以下命令开始测试:', 'cyan');
  log('  npx jest            # 运行所有测试', 'cyan');
  log('  npx jest api.test.js    # 只运行API测试', 'cyan');
  log('  npx jest frontend.test.js   # 只运行前端测试', 'cyan');
  log('  或使用run_tests.bat脚本一键运行所有测试', 'cyan');
  log('========================================\n', 'bright');
}

// 执行主函数
main(); 