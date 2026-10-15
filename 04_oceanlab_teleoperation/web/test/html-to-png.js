const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

const SCREENSHOTS_DIR = path.join(__dirname, 'screenshots');

async function convertHtmlToPng() {
  try {
    // 获取所有HTML文件
    const htmlFiles = fs.readdirSync(SCREENSHOTS_DIR)
      .filter(file => file.endsWith('.html'));

    if (htmlFiles.length === 0) {
      console.log('未找到任何HTML文件');
      return;
    }

    console.log(`找到 ${htmlFiles.length} 个HTML文件，开始转换...`);

    // 启动浏览器
    const browser = await puppeteer.launch({ 
      headless: true,
      args: ['--no-sandbox', '--disable-setuid-sandbox']
    });

    // 处理每个文件
    for (const htmlFile of htmlFiles) {
      const htmlPath = path.join(SCREENSHOTS_DIR, htmlFile);
      const pngFile = htmlFile.replace('.html', '.png');
      const pngPath = path.join(SCREENSHOTS_DIR, pngFile);

      console.log(`正在处理: ${htmlFile}`);

      const page = await browser.newPage();
      await page.setViewport({ width: 800, height: 600 });
      await page.goto(`file://${htmlPath}`, { waitUntil: 'networkidle0' });
      await page.screenshot({ path: pngPath });
      await page.close();

      console.log(`已生成PNG: ${pngFile}`);
    }

    await browser.close();
    console.log('所有HTML文件已成功转换为PNG!');

  } catch (error) {
    console.error('转换过程中发生错误:', error.message);
  }
}

convertHtmlToPng();
