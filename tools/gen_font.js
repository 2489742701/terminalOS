// 调用 lv_font_conv 生成 LVGL 中文字体。
//
// 为什么包一层 Node：直接命令行传 --symbols "几千个汉字" 会被 Windows 的
// cmd/PowerShell 代码页和转义字符（!$&*()[]{};'"`\ 全是元字符）破坏，
// 上次就是这么把字体生成废的。这里用 spawn 传 argv 数组，不经过 shell，
// 且字符集从 UTF-8 文件读入，字节级无损。
//
// 用法：
//   node gen_font.js <size> <bpp> <symbols.txt> <out.c>

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const WORKSPACE = 'C:/Users/longyaosi/.workbuddy/binaries/node/workspace';
const CLI = path.join(WORKSPACE, 'node_modules/lv_font_conv/lv_font_conv.js');
const NODE = process.execPath;

const size = process.argv[2] || '16';
const bpp = process.argv[3] || '4';
const symFile = process.argv[4] || 'font_symbols.txt';
const out = process.argv[5] || 'font_zh.c';
const fontTtf = process.argv[6] || 'C:\\Windows\\Fonts\\simhei.ttf';

const symbols = fs.readFileSync(symFile, 'utf8').replace(/[\r\n]/g, '');
console.log('symbols:', symbols.length, 'chars; size:', size, 'bpp:', bpp);

const args = [
  CLI,
  '--font', fontTtf,
  '--size', size,
  '--bpp', bpp,
  '--format', 'lvgl',
  '--output', out,
  '--symbols', symbols,
];

const r = spawnSync(NODE, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
if (r.error) { console.error('SPAWN_ERROR', r.error.message); process.exit(1); }
if (r.stdout) console.log(r.stdout.slice(-2000));
if (r.stderr) console.error('STDERR:', r.stderr.slice(-3000));
console.log('exit code:', r.status);
if (r.status !== 0 || !fs.existsSync(out)) process.exit(r.status == null ? 1 : r.status);

/* ── 后处理：挂上 fallback ──
 * 本字体只含汉字 + 中文标点，ASCII/数字一概不编（ESP32 侧 LVGL 自带 montserrat，
 * 编进来纯属占 flash）。但没有 fallback 的话英文和数字会整个消失，所以必须在这里
 * 把 .fallback 指向 montserrat。
 * LVGL 8.3 没有 lv_font_set_fallback()，而且 font_zh_16 是 const，运行时改不了，
 * 只能改生成产物。每次重新生成都要跑这一步 —— 所以放在脚本里，不要手工改。
 */
const fallback = process.env.FONT_FALLBACK || 'lv_font_montserrat_16';
let src = fs.readFileSync(out, 'utf8');
if (!src.includes('.fallback = NULL,')) {
  console.error('WARN: 没找到 .fallback = NULL, —— 生成格式变了，后处理跳过');
} else {
  src = src.replace('.fallback = NULL,', `.fallback = &${fallback},`);
  const anchor = '#ifndef FONT_ZH';
  const decl = `extern const lv_font_t ${fallback};\n\n`;
  if (src.includes(anchor) && !src.includes(`extern const lv_font_t ${fallback}`)) {
    src = src.replace(anchor, decl + anchor);
  }
  fs.writeFileSync(out, src);
  console.log('fallback 已挂上: &%s', fallback);
}
const st = fs.statSync(out);
console.log('output:', out, (st.size / 1024).toFixed(0) + ' KB');
process.exit(0);
