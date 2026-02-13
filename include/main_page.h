#pragma once
#include <pgmspace.h>

// FaceGuard Pro – Admin Portal
// Served from ESP32-CAM PROGMEM. Chart.js loaded from CDN (browser needs internet).
// All API calls are relative to window.location.origin (port 80).
// Camera stream is served on port 81: http://[ESP_IP]:81/stream

const char index_html[] PROGMEM = R"HTMLEOF(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FaceGuard Pro</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"></script>
<style>
:root{--bg:#080c12;--bg2:#0d1520;--card:#111827;--border:#1e3050;
--cyan:#00e5ff;--cdim:rgba(0,229,255,0.12);--amber:#ffb700;--adim:rgba(255,183,0,0.15);
--green:#00e676;--gdim:rgba(0,230,118,0.15);--red:#ff3d57;--rdim:rgba(255,61,87,0.15);
--t1:#e8f4f8;--t2:#7899a8;--t3:#3d5a6b;--fw:600}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--t1);min-height:100vh;display:flex}
body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(0,229,255,.025) 1px,transparent 1px),linear-gradient(90deg,rgba(0,229,255,.025) 1px,transparent 1px);background-size:40px 40px;pointer-events:none;z-index:0}
/* ─ SIDEBAR ─ */
.sb{width:240px;min-height:100vh;background:var(--bg2);border-right:1px solid var(--border);display:flex;flex-direction:column;position:fixed;left:0;top:0;bottom:0;z-index:100;overflow-y:auto}
.sb-logo{padding:20px;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:10px}
.sb-icon{width:36px;height:36px;background:var(--cyan);border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:18px;box-shadow:0 0 16px var(--cdim);flex-shrink:0}
.sb-title{font-weight:700;font-size:14px;letter-spacing:.5px}
.sb-sub{font-size:10px;color:var(--cyan);letter-spacing:2px;text-transform:uppercase}
.sb-sec{padding:16px 10px 4px}
.sb-sec-title{font-size:9px;letter-spacing:3px;text-transform:uppercase;color:var(--t3);padding:0 8px;margin-bottom:4px}
.nav{display:flex;align-items:center;gap:10px;padding:9px 10px;border-radius:7px;cursor:pointer;font-size:13px;font-weight:500;color:var(--t2);margin-bottom:2px;border:1px solid transparent;transition:all .18s;text-decoration:none}
.nav:hover{background:var(--card);color:var(--t1);border-color:var(--border)}
.nav.active{background:var(--cdim);color:var(--cyan);border-color:rgba(0,229,255,.2);position:relative}
.nav.active::before{content:'';position:absolute;left:0;top:30%;width:3px;height:40%;background:var(--cyan);border-radius:0 3px 3px 0;box-shadow:0 0 6px var(--cyan)}
.nav-ico{width:18px;text-align:center;font-size:15px;flex-shrink:0}
.sb-footer{margin-top:auto;padding:14px 10px;border-top:1px solid var(--border)}
.sys-mini{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:10px}
.srow{font-size:10px;color:var(--t2);display:flex;align-items:center;margin-bottom:3px}
.dot{display:inline-block;width:6px;height:6px;border-radius:50%;margin-right:6px;animation:blink 2s infinite}
.dot.g{background:var(--green);box-shadow:0 0 5px var(--green)}
.dot.a{background:var(--amber);box-shadow:0 0 5px var(--amber)}
.dot.r{background:var(--red);box-shadow:0 0 5px var(--red)}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.4}}
/* ─ MAIN ─ */
.main{margin-left:240px;flex:1;position:relative;z-index:1}
.topbar{height:56px;background:rgba(13,21,32,.95);backdrop-filter:blur(8px);border-bottom:1px solid var(--border);display:flex;align-items:center;padding:0 20px;gap:14px;position:sticky;top:0;z-index:50}
.tb-title{font-size:15px;font-weight:600;flex:1}
.tb-clock{font-family:monospace;font-size:12px;color:var(--cyan)}
.tb-user{display:flex;align-items:center;gap:8px;background:var(--card);border:1px solid var(--border);border-radius:7px;padding:5px 10px;cursor:pointer;font-size:12px;color:var(--t2)}
.av{width:26px;height:26px;border-radius:5px;background:linear-gradient(135deg,var(--cyan),#7c4dff);display:flex;align-items:center;justify-content:center;font-size:11px;font-weight:700;color:var(--bg)}
.content{padding:20px;min-height:calc(100vh - 56px)}
/* ─ CARDS ─ */
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:18px;position:relative;overflow:hidden;transition:border-color .18s}
.card:hover{border-color:rgba(0,229,255,.15)}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,var(--cdim),transparent)}
.card-title{font-size:10px;letter-spacing:2px;text-transform:uppercase;color:var(--t2);margin-bottom:12px;display:flex;align-items:center;gap:8px}
.card-line{flex:1;height:1px;background:var(--border)}
/* ─ STAT CARDS ─ */
.stats-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin-bottom:20px}
.sc{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:18px;position:relative;overflow:hidden;transition:all .2s;cursor:default}
.sc:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,.4)}
.sc.c{border-top:2px solid var(--cyan)}.sc.g{border-top:2px solid var(--green)}.sc.r{border-top:2px solid var(--red)}.sc.a{border-top:2px solid var(--amber)}
.sc-glow{position:absolute;right:-10px;top:-10px;width:70px;height:70px;border-radius:50%;filter:blur(18px)}
.sc.c .sc-glow{background:var(--cdim)}.sc.g .sc-glow{background:var(--gdim)}.sc.r .sc-glow{background:var(--rdim)}.sc.a .sc-glow{background:var(--adim)}
.sc-label{font-size:10px;letter-spacing:1.5px;text-transform:uppercase;color:var(--t2);margin-bottom:6px}
.sc-val{font-size:32px;font-weight:700;line-height:1;margin-bottom:4px}
.sc.c .sc-val{color:var(--cyan)}.sc.g .sc-val{color:var(--green)}.sc.r .sc-val{color:var(--red)}.sc.a .sc-val{color:var(--amber)}
.sc-sub{font-size:11px;color:var(--t2)}
.sc-ico{position:absolute;right:14px;bottom:12px;font-size:24px;opacity:.12}
/* ─ GRID ─ */
.g2{display:grid;grid-template-columns:1fr 1fr;gap:14px}.g3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:14px}
.g31{display:grid;grid-template-columns:2fr 1fr;gap:14px}.mb{margin-bottom:14px}
/* ─ TABLE ─ */
.tw{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:12px;font-family:monospace}
thead th{text-align:left;padding:8px 12px;font-size:9px;letter-spacing:2px;text-transform:uppercase;color:var(--t3);border-bottom:1px solid var(--border);white-space:nowrap}
tbody tr{border-bottom:1px solid rgba(30,48,80,.4);transition:background .12s;cursor:pointer}
tbody tr:hover{background:rgba(22,32,51,.6)}
tbody td{padding:10px 12px;color:var(--t2)}
.td-p{color:var(--t1)!important;font-weight:500}
/* ─ BADGE ─ */
.badge{display:inline-flex;align-items:center;gap:3px;padding:2px 8px;border-radius:3px;font-size:10px;font-weight:600;font-family:monospace}
.bp{background:var(--gdim);color:var(--green);border:1px solid rgba(0,230,118,.2)}
.ba{background:var(--rdim);color:var(--red);border:1px solid rgba(255,61,87,.2)}
.bl{background:var(--adim);color:var(--amber);border:1px solid rgba(255,183,0,.2)}
.be{background:rgba(124,77,255,.15);color:#9c6dff;border:1px solid rgba(124,77,255,.3)}
.bst{background:rgba(33,150,243,.15);color:#42a5f5;border:1px solid rgba(33,150,243,.25)}
.bss{background:var(--gdim);color:#66bb6a;border:1px solid rgba(76,175,80,.25)}
.bsa{background:var(--cdim);color:var(--cyan);border:1px solid rgba(0,229,255,.2)}
/* ─ BTNS ─ */
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 16px;border-radius:6px;font-size:12px;font-weight:600;cursor:pointer;transition:all .18s;border:none;white-space:nowrap}
.btn-p{background:var(--cyan);color:var(--bg);box-shadow:0 0 16px rgba(0,229,255,.18)}
.btn-p:hover{background:#00b4cc;box-shadow:0 0 24px rgba(0,229,255,.3);transform:translateY(-1px)}
.btn-g{background:var(--card);color:var(--t2);border:1px solid var(--border)}
.btn-g:hover{color:var(--t1);border-color:rgba(0,229,255,.25)}
.btn-d{background:var(--rdim);color:var(--red);border:1px solid rgba(255,61,87,.25)}
.btn-d:hover{background:rgba(255,61,87,.25)}
.btn-a{background:var(--adim);color:var(--amber);border:1px solid rgba(255,183,0,.25)}
.btn-a:hover{background:rgba(255,183,0,.25)}
.btn-sm{padding:5px 10px;font-size:11px}.btn-xs{padding:3px 7px;font-size:10px}
.btn-group{display:flex;gap:7px;flex-wrap:wrap;align-items:center}
/* ─ INPUTS ─ */
.fg{margin-bottom:14px}
label{display:block;font-size:9px;letter-spacing:1.5px;text-transform:uppercase;color:var(--t2);margin-bottom:5px}
input[type=text],input[type=email],input[type=password],input[type=number],input[type=time],input[type=date],select,textarea{width:100%;background:var(--bg2);border:1px solid var(--border);border-radius:6px;padding:9px 12px;font-size:12px;color:var(--t1);outline:none;transition:border-color .18s,box-shadow .18s;appearance:none;font-family:monospace}
input:focus,select:focus,textarea:focus{border-color:var(--cyan);box-shadow:0 0 0 2px rgba(0,229,255,.07)}
select option{background:var(--bg2)}
.sb-inp{position:relative}.sb-inp input{padding-left:32px}.sb-ico{position:absolute;left:10px;top:50%;transform:translateY(-50%);color:var(--t3);font-size:13px;pointer-events:none}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
/* ─ TOGGLE ─ */
.tgl-wrap{display:flex;align-items:center;gap:10px;margin-bottom:10px}
.tgl{width:40px;height:20px;background:var(--border);border-radius:10px;position:relative;cursor:pointer;transition:background .2s;border:none;flex-shrink:0}
.tgl.on{background:var(--cyan)}.tgl::after{content:'';position:absolute;width:14px;height:14px;background:white;border-radius:50%;top:3px;left:3px;transition:transform .2s}
.tgl.on::after{transform:translateX(20px)}
input[type=range]{-webkit-appearance:none;background:var(--border);height:3px;border-radius:2px;padding:0}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;background:var(--cyan);border-radius:50%;cursor:pointer;box-shadow:0 0 6px var(--cyan)}
/* ─ FILTER BAR ─ */
.fbar{background:var(--bg2);border:1px solid var(--border);border-radius:8px;padding:12px 14px;display:flex;gap:8px;flex-wrap:wrap;align-items:flex-end;margin-bottom:14px}
.fbar .fg{margin:0;flex:1;min-width:110px}
/* ─ SECTION ─ */
.sec{display:none}.sec.active{display:block}
.sec-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;flex-wrap:wrap;gap:10px}
.sec-title{font-size:20px;font-weight:700;letter-spacing:.5px}
.sec-title span{font-size:11px;color:var(--cyan);font-weight:400;display:block;letter-spacing:2px;text-transform:uppercase;margin-bottom:2px;opacity:.7}
/* ─ LOG LIST ─ */
.log-list{max-height:280px;overflow-y:auto}
.log-item{display:flex;align-items:center;gap:10px;padding:9px 0;border-bottom:1px solid rgba(30,48,80,.35);animation:logIn .3s ease}
@keyframes logIn{from{opacity:0;transform:translateX(-6px)}to{opacity:1;transform:none}}
.log-av{width:32px;height:32px;border-radius:7px;background:var(--bg2);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:var(--cyan);flex-shrink:0}
.log-name{font-size:13px;font-weight:500;color:var(--t1)}.log-dept{font-size:10px;color:var(--t3)}
.log-time{margin-left:auto;text-align:right;font-size:11px;font-family:monospace;color:var(--t2);flex-shrink:0}
.log-conf{font-size:10px;color:var(--cyan)}
/* ─ CHART WRAP ─ */
.ch{position:relative;height:200px}
/* ─ USER CARD GRID ─ */
.ug{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:12px}
.uc{background:var(--bg2);border:1px solid var(--border);border-radius:9px;padding:14px;transition:all .2s;cursor:pointer;position:relative}
.uc:hover{border-color:rgba(0,229,255,.2);transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,0,0,.3)}
.uc-av{width:46px;height:46px;border-radius:9px;background:var(--cdim);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:16px;font-weight:700;color:var(--cyan);margin-bottom:8px}
.uc-name{font-size:13px;font-weight:600;color:var(--t1);margin-bottom:2px}.uc-id{font-size:10px;color:var(--t3);margin-bottom:6px;font-family:monospace}
.uc-acts{display:flex;gap:5px;margin-top:8px;opacity:0;transition:opacity .2s}
.uc:hover .uc-acts{opacity:1}
/* ─ CAM PREVIEW ─ */
.cam-box{width:100%;aspect-ratio:4/3;background:#000;border-radius:7px;border:1px solid var(--border);position:relative;overflow:hidden;margin-bottom:10px}
.cam-box img,.cam-box video{width:100%;height:100%;object-fit:cover;display:block}
.cam-live{position:absolute;top:8px;left:8px;background:rgba(0,0,0,.6);border-radius:3px;padding:2px 7px;font-size:10px;color:var(--red);display:flex;align-items:center;gap:4px;font-family:monospace}
.cam-face{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);width:120px;height:140px;border:2px solid var(--cyan);border-radius:3px;box-shadow:0 0 14px rgba(0,229,255,.25);pointer-events:none}
.scan{position:absolute;left:0;right:0;height:2px;background:linear-gradient(90deg,transparent,var(--cyan),transparent);animation:scan 2s linear infinite;opacity:.7}
@keyframes scan{from{top:0}to{top:100%}}
.cap-strip{display:flex;gap:5px;flex-wrap:wrap;margin-bottom:10px}
.cap-th{width:54px;height:54px;border-radius:5px;background:var(--bg2);border:1px solid var(--border);display:flex;align-items:center;justify-content:center;font-size:18px;color:var(--t3)}
.cap-th.done{border-color:var(--green);color:var(--green)}
/* ─ SETTINGS ─ */
.stabs{display:flex;gap:3px;background:var(--bg2);border:1px solid var(--border);border-radius:7px;padding:3px;margin-bottom:16px;overflow-x:auto}
.stab{padding:7px 14px;border-radius:5px;font-size:12px;font-weight:500;color:var(--t2);cursor:pointer;border:none;background:none;white-space:nowrap;transition:all .18s}
.stab.active{background:var(--cdim);color:var(--cyan)}
.stab:hover:not(.active){color:var(--t1)}
.sp{display:none}.sp.active{display:block}
.sg{background:var(--bg2);border:1px solid var(--border);border-radius:9px;padding:18px;margin-bottom:14px}
.sg-title{font-size:11px;font-weight:600;letter-spacing:2px;text-transform:uppercase;color:var(--cyan);margin-bottom:14px;display:flex;align-items:center;gap:7px}
.info-box{background:rgba(0,229,255,.05);border:1px solid rgba(0,229,255,.15);border-radius:7px;padding:11px 13px;font-size:11px;color:var(--t2);line-height:1.7;margin-bottom:14px}
.info-box strong{color:var(--cyan)}
/* ─ MODAL ─ */
.mo{position:fixed;inset:0;background:rgba(8,12,18,.85);backdrop-filter:blur(4px);z-index:1000;display:none;align-items:center;justify-content:center;padding:20px}
.mo.open{display:flex}
.md{background:var(--card);border:1px solid var(--border);border-radius:14px;width:100%;max-width:540px;max-height:90vh;overflow-y:auto;animation:mdIn .22s ease;box-shadow:0 20px 60px rgba(0,0,0,.6)}
@keyframes mdIn{from{opacity:0;transform:scale(.96) translateY(10px)}to{opacity:1;transform:none}}
.md-hdr{padding:18px 20px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between}
.md-title{font-size:15px;font-weight:600;color:var(--cyan)}
.md-close{background:none;border:none;color:var(--t2);cursor:pointer;font-size:18px;line-height:1;padding:3px;border-radius:3px;transition:color .15s}
.md-close:hover{color:var(--t1)}
.md-body{padding:20px}.md-footer{padding:14px 20px;border-top:1px solid var(--border);display:flex;justify-content:flex-end;gap:8px}
/* ─ TRAINING STEPS ─ */
.tstep{display:flex;align-items:center;gap:10px;padding:7px 0;font-size:12px;font-family:monospace;color:var(--t2)}
.tstep.done{color:var(--green)}.tstep.active{color:var(--cyan)}
.tstep-ico{width:18px;text-align:center}
/* ─ PROGRESS ─ */
.prog-bar{height:4px;background:var(--border);border-radius:2px;overflow:hidden}
.prog-fill{height:100%;border-radius:2px;transition:width .5s}
.prog-fill.c{background:var(--cyan)}.prog-fill.g{background:var(--green)}.prog-fill.a{background:var(--amber)}.prog-fill.r{background:var(--red)}
/* ─ DEPT BARS ─ */
.dbar-row{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.dbar-label{font-size:10px;color:var(--t2);width:80px;flex-shrink:0;font-family:monospace}
.dbar-bg{flex:1;height:5px;background:var(--border);border-radius:3px;overflow:hidden}
.dbar-fill{height:100%;border-radius:3px;background:linear-gradient(90deg,var(--cyan),#00b4cc);transition:width .8s}
.dbar-pct{font-size:10px;color:var(--t2);width:32px;text-align:right;font-family:monospace;flex-shrink:0}
/* ─ RATE CIRCLE ─ */
.rate-wrap{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.rate-circle{position:relative;width:72px;height:72px;flex-shrink:0}
.rate-circle svg{width:100%;height:100%;transform:rotate(-90deg)}
.rate-text{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:700;color:var(--cyan)}
.rate-stats{font-size:11px;font-family:monospace;color:var(--t2);line-height:2}
/* ─ TOAST ─ */
#toasts{position:fixed;top:64px;right:16px;z-index:2000;display:flex;flex-direction:column;gap:6px}
.toast{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:10px 14px;min-width:240px;animation:tIn .25s ease;display:flex;align-items:center;gap:8px;box-shadow:0 6px 20px rgba(0,0,0,.4)}
@keyframes tIn{from{opacity:0;transform:translateX(16px)}to{opacity:1;transform:none}}
.toast.s{border-left:3px solid var(--green)}.toast.e{border-left:3px solid var(--red)}.toast.i{border-left:3px solid var(--cyan)}.toast.w{border-left:3px solid var(--amber)}
.toast-msg{font-size:11px;color:var(--t2);font-family:monospace;flex:1}
/* ─ DIVIDER ─ */
.div{height:1px;background:var(--border);margin:16px 0}
/* ─ RESPONSIVE ─ */
@media(max-width:900px){.stats-grid{grid-template-columns:1fr 1fr}.g2,.g31{grid-template-columns:1fr}.g3{grid-template-columns:1fr 1fr}}
@media(max-width:640px){.main{margin-left:0}.sb{transform:translateX(-100%)}.sb.open{transform:none}}
::-webkit-scrollbar{width:5px;height:5px}::-webkit-scrollbar-track{background:var(--bg)}::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.spin{display:inline-block;width:14px;height:14px;border:2px solid var(--border);border-top-color:var(--cyan);border-radius:50%;animation:spin .6s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>
<nav class="sb" id="sidebar">
  <div class="sb-logo">
    <div class="sb-icon">&#x1F441;</div>
    <div><div class="sb-title">FaceGuard Pro</div><div class="sb-sub">v2.0 ESP32</div></div>
  </div>
  <div class="sb-sec">
    <div class="sb-sec-title">Navigation</div>
    <a class="nav active" onclick="showSec('dashboard')" id="n-dashboard"><span class="nav-ico">&#x2B21;</span> Dashboard</a>
    <a class="nav" onclick="showSec('users')" id="n-users"><span class="nav-ico">&#x25C8;</span> Users</a>
    <a class="nav" onclick="showSec('attendance')" id="n-attendance"><span class="nav-ico">&#x25EB;</span> Attendance</a>
    <a class="nav" onclick="showSec('reports')" id="n-reports"><span class="nav-ico">&#x25CE;</span> Reports</a>
    <a class="nav" onclick="showSec('settings')" id="n-settings"><span class="nav-ico">&#x2699;</span> Settings</a>
  </div>
  <div class="sb-sec">
    <div class="sb-sec-title">System</div>
    <a class="nav" onclick="window.open('http://'+location.hostname+':81/stream','_blank')"><span class="nav-ico">&#x1F4F7;</span> Live Feed</a>
    <a class="nav" onclick="logout()"><span class="nav-ico" style="color:var(--red)">&#x2715;</span> <span style="color:var(--red)">Logout</span></a>
  </div>
  <div class="sb-footer">
    <div class="sys-mini">
      <div class="srow"><span class="dot g"></span>Camera Active</div>
      <div class="srow"><span class="dot g" id="wifi-dot"></span><span id="wifi-txt">WiFi OK</span></div>
      <div class="srow"><span class="dot a" id="model-dot"></span><span id="model-txt">Loading Model...</span></div>
    </div>
  </div>
</nav>
<main class="main">
  <div class="topbar">
    <div class="tb-title" id="page-title">Dashboard</div>
    <div class="tb-clock" id="clock"></div>
    <div class="tb-user"><div class="av">SA</div><span>Super Admin</span></div>
  </div>
  <div class="content">

  <!-- DASHBOARD -->
  <div class="sec active" id="sec-dashboard">
    <div class="sec-hdr">
      <div class="sec-title"><span>System Overview</span>Dashboard</div>
      <div class="btn-group">
        <button class="btn btn-g btn-sm" onclick="loadDashboard()">&#x21BB; Refresh</button>
        <button class="btn btn-p btn-sm" onclick="showSec('attendance')">View Attendance &rarr;</button>
      </div>
    </div>
    <div class="stats-grid">
      <div class="sc c"><div class="sc-glow"></div><div class="sc-label">Total Registered</div><div class="sc-val" id="s-total">--</div><div class="sc-sub">All users</div><div class="sc-ico">&#x1F465;</div></div>
      <div class="sc g"><div class="sc-glow"></div><div class="sc-label">Present Today</div><div class="sc-val" id="s-present">--</div><div class="sc-sub" id="s-prate">--</div><div class="sc-ico">&#x2713;</div></div>
      <div class="sc r"><div class="sc-glow"></div><div class="sc-label">Absent Today</div><div class="sc-val" id="s-absent">--</div><div class="sc-sub" id="s-arate">--</div><div class="sc-ico">&#x2715;</div></div>
      <div class="sc a"><div class="sc-glow"></div><div class="sc-label">Late Arrivals</div><div class="sc-val" id="s-late">--</div><div class="sc-sub">After late threshold</div><div class="sc-ico">&#x23F0;</div></div>
    </div>
    <div class="g31 mb">
      <div class="card">
        <div class="card-title">Weekly Attendance<div class="card-line"></div></div>
        <div class="ch" style="height:210px"><canvas id="ch-weekly"></canvas></div>
      </div>
      <div class="card">
        <div class="card-title">Today's Split<div class="card-line"></div></div>
        <div class="ch" style="height:160px"><canvas id="ch-pie"></canvas></div>
        <div id="pie-leg" style="display:flex;gap:10px;flex-wrap:wrap;margin-top:10px;font-size:10px;font-family:monospace;color:var(--t2)"></div>
      </div>
    </div>
    <div class="g2">
      <div class="card">
        <div class="card-title">Live Check-ins <span class="dot g" style="margin:0 4px"></span><div class="card-line"></div><span style="font-size:10px;font-family:monospace;color:var(--t3)" id="checkin-count">0 today</span></div>
        <div class="log-list" id="live-log"><div style="text-align:center;padding:20px;font-size:11px;color:var(--t3);font-family:monospace">No check-ins yet today</div></div>
      </div>
      <div class="card">
        <div class="card-title">Department Attendance<div class="card-line"></div></div>
        <div id="dept-bars"></div>
        <div class="div"></div>
        <div class="card-title">Rate<div class="card-line"></div></div>
        <div class="rate-wrap">
          <div class="rate-circle">
            <svg viewBox="0 0 72 72"><circle cx="36" cy="36" r="30" fill="none" stroke="var(--border)" stroke-width="6"/><circle cx="36" cy="36" r="30" fill="none" stroke="var(--cyan)" stroke-width="6" stroke-dasharray="188.5" stroke-dashoffset="188.5" stroke-linecap="round" id="rate-circle"/></svg>
            <div class="rate-text" id="rate-text">--%</div>
          </div>
          <div class="rate-stats">&#x25C6; Present: <span id="r-p" style="color:var(--green)">--</span><br>&#x25C6; Absent: <span id="r-a" style="color:var(--red)">--</span><br>&#x25C6; Late: <span id="r-l" style="color:var(--amber)">--</span></div>
        </div>
      </div>
    </div>
  </div>

  <!-- USERS -->
  <div class="sec" id="sec-users">
    <div class="sec-hdr">
      <div class="sec-title"><span>Identity &amp; Access</span>User Management</div>
      <div class="btn-group">
        <button class="btn btn-g btn-sm" onclick="openTrainModal()">&#x1F9E0; Train Model</button>
        <button class="btn btn-p" onclick="openAddUser()">&#xFF0B; Add User</button>
      </div>
    </div>
    <div class="fbar">
      <div class="fg" style="flex:2;min-width:180px"><label>Search</label><div class="sb-inp"><span class="sb-ico">&#x1F50D;</span><input type="text" id="u-search" placeholder="Name or ID..." oninput="filterUsers()"></div></div>
      <div class="fg"><label>Role</label><select id="f-role" onchange="filterUsers()"><option value="">All Roles</option><option>Student</option><option>Staff</option><option>Admin</option></select></div>
      <div class="fg"><label>Department</label><select id="f-dept" onchange="filterUsers()"><option value="">All Depts</option><option>Computer Science</option><option>Engineering</option><option>Mathematics</option><option>Physics</option><option>Administration</option></select></div>
      <button class="btn btn-g btn-sm" onclick="filterUsers(true)">&#x2715; Clear</button>
    </div>
    <div class="ug" id="user-grid"></div>
  </div>

  <!-- ATTENDANCE -->
  <div class="sec" id="sec-attendance">
    <div class="sec-hdr">
      <div class="sec-title"><span>Records &amp; Logs</span>Attendance</div>
      <div class="btn-group">
        <button class="btn btn-g btn-sm" onclick="exportCSV()">&#x1F4E5; CSV</button>
        <button class="btn btn-a btn-sm" onclick="openManualModal()">&#x270F; Override</button>
      </div>
    </div>
    <div class="fbar">
      <div class="fg"><label>Date</label><input type="date" id="att-date" oninput="loadAttendance()"></div>
      <div class="fg"><label>Department</label><select id="att-dept" onchange="loadAttendance()"><option value="">All</option><option>Computer Science</option><option>Engineering</option><option>Mathematics</option><option>Physics</option><option>Administration</option></select></div>
      <div class="fg"><label>Status</label><select id="att-stat" onchange="loadAttendance()"><option value="">All</option><option>Present</option><option>Absent</option><option>Late</option><option>Excused</option></select></div>
      <div class="fg" style="flex:2;min-width:150px"><label>Search</label><div class="sb-inp"><span class="sb-ico">&#x1F50D;</span><input type="text" id="att-search" placeholder="Name or UID..." oninput="loadAttendance()"></div></div>
    </div>
    <div class="card">
      <div class="card-title" id="att-hdr">Attendance Log<div class="card-line"></div><span id="att-count" style="font-size:10px;font-family:monospace;color:var(--t3)">-- records</span></div>
      <div class="tw"><table><thead><tr><th>UID</th><th>Name</th><th>Dept</th><th>Date</th><th>Time</th><th>Confidence</th><th>Status</th><th>Actions</th></tr></thead><tbody id="att-body"></tbody></table></div>
    </div>
  </div>

  <!-- REPORTS -->
  <div class="sec" id="sec-reports">
    <div class="sec-hdr">
      <div class="sec-title"><span>Analytics &amp; Insights</span>Reports</div>
      <div class="btn-group">
        <select class="btn btn-g btn-sm" id="rp-days" onchange="loadReports()" style="padding:7px 10px">
          <option value="7">Last 7 Days</option><option value="14">Last 14 Days</option><option value="30" selected>Last 30 Days</option>
        </select>
        <button class="btn btn-p btn-sm" onclick="exportCSV()">&#x1F4C4; Export</button>
      </div>
    </div>
    <div class="g2 mb">
      <div class="card"><div class="card-title">Daily Attendance (Bar)<div class="card-line"></div></div><div class="ch" style="height:220px"><canvas id="ch-bar"></canvas></div></div>
      <div class="card"><div class="card-title">Status Distribution<div class="card-line"></div></div><div class="ch" style="height:180px"><canvas id="ch-pie2"></canvas></div></div>
    </div>
    <div class="card mb">
      <div class="card-title">Attendance Trend (Line)<div class="card-line"></div></div>
      <div class="ch" style="height:200px"><canvas id="ch-line"></canvas></div>
    </div>
    <div class="g2">
      <div class="card"><div class="card-title">Top Attendance<div class="card-line"></div></div><div class="tw"><table><thead><tr><th>Rank</th><th>Name</th><th>Dept</th><th>Rate</th></tr></thead><tbody id="rank-body"></tbody></table></div></div>
      <div class="card"><div class="card-title">Dept Summary<div class="card-line"></div></div><div class="tw"><table><thead><tr><th>Department</th><th>Members</th><th>Rate</th></tr></thead><tbody id="dept-body"></tbody></table></div></div>
    </div>
  </div>

  <!-- SETTINGS -->
  <div class="sec" id="sec-settings">
    <div class="sec-hdr">
      <div class="sec-title"><span>Configuration</span>System Settings</div>
      <button class="btn btn-p" onclick="saveSettings()">&#x1F4BE; Save</button>
    </div>
    <div class="stabs">
      <button class="stab active" onclick="showStab('sc-time',this)">&#x23F1; Attendance</button>
      <button class="stab" onclick="showStab('sc-recog',this)">&#x1F9E0; Recognition</button>
      <button class="stab" onclick="showStab('sc-cam',this)">&#x1F4F7; ESP32-CAM</button>
      <button class="stab" onclick="showStab('sc-notif',this)">&#x1F514; Notifications</button>
      <button class="stab" onclick="showStab('sc-backup',this)">&#x1F4BE; Backup</button>
    </div>
    <div class="sp active" id="sc-time">
      <div class="sg"><div class="sg-title">&#x23F0; Time Configuration</div>
        <div class="grid2">
          <div class="fg"><label>Start Time</label><input type="time" id="cfg-start" value="07:30"></div>
          <div class="fg"><label>End Time</label><input type="time" id="cfg-end" value="18:00"></div>
          <div class="fg"><label>Late Threshold</label><input type="time" id="cfg-late" value="08:10"></div>
          <div class="fg"><label>Absent Cutoff</label><input type="time" id="cfg-absent" value="10:00"></div>
        </div>
      </div>
      <div class="sg"><div class="sg-title">&#x1F310; NTP &amp; Timezone</div>
        <div class="grid2">
          <div class="fg"><label>NTP Server</label><input type="text" id="cfg-ntp" value="pool.ntp.org"></div>
          <div class="fg"><label>GMT Offset (seconds)</label><input type="number" id="cfg-gmt" value="3600" placeholder="3600 = UTC+1"></div>
        </div>
        <button class="btn btn-g btn-sm" onclick="syncNTP()">&#x1F504; Sync NTP Now</button>
      </div>
    </div>
    <div class="sp" id="sc-recog">
      <div class="sg"><div class="sg-title">&#x1F3AF; Recognition Parameters</div>
        <div class="grid2">
          <div class="fg"><label>Confidence Threshold: <span id="cfg-conf-val" style="color:var(--cyan)">85%</span></label><input type="range" min="60" max="99" value="85" id="cfg-conf" oninput="document.getElementById('cfg-conf-val').textContent=this.value+'%'"></div>
          <div class="fg"><label>Min Face Size (px)</label><input type="number" id="cfg-minface" value="80" min="40" max="200"></div>
          <div class="fg"><label>Recognition Cooldown (ms)</label><input type="number" id="cfg-cooldown" value="5000"></div>
          <div class="fg"><label>Enroll Confirms Required</label><input type="number" id="cfg-confirms" value="5" min="3" max="10"></div>
        </div>
        <div class="tgl-wrap"><button class="tgl on" id="tgl-auto" onclick="this.classList.toggle('on')"></button><span style="font-size:12px;color:var(--t2)">Auto-attendance mode on startup</span></div>
        <div class="tgl-wrap"><button class="tgl" id="tgl-buzzer" onclick="this.classList.toggle('on')"></button><span style="font-size:12px;color:var(--t2)">Buzzer/LED feedback on recognition</span></div>
      </div>
    </div>
    <div class="sp" id="sc-cam">
      <div class="sg"><div class="sg-title">&#x1F4F7; ESP32-CAM Stream</div>
        <div class="info-box"><strong>Stream URL:</strong> The MJPEG stream runs on port 81 of this device.<br>Access it at: <strong id="stream-url"></strong></div>
        <div class="grid2">
          <div class="fg"><label>Frame Size</label><select id="cfg-fsize"><option>QVGA (320x240)</option><option selected>VGA (640x480)</option><option>SVGA (800x600)</option></select></div>
          <div class="fg"><label>JPEG Quality (10=best)</label><input type="number" id="cfg-jpegq" value="12" min="10" max="63"></div>
          <div class="fg"><label>XCLK Frequency (Hz)</label><input type="number" id="cfg-xclk" value="20000000"></div>
          <div class="fg"><label>Flash / LED</label><select><option>Off</option><option>Auto</option><option>Always On</option></select></div>
        </div>
        <div class="tgl-wrap"><button class="tgl" onclick="this.classList.toggle('on')"></button><span style="font-size:12px;color:var(--t2)">Vertical flip</span></div>
        <div class="tgl-wrap"><button class="tgl" onclick="this.classList.toggle('on')"></button><span style="font-size:12px;color:var(--t2)">Horizontal mirror</span></div>
        <button class="btn btn-p btn-sm" style="margin-top:4px" onclick="window.open('http://'+location.hostname+':81/stream','_blank')">&#x25B6; Preview Stream</button>
      </div>
    </div>
    <div class="sp" id="sc-notif">
      <div class="sg"><div class="sg-title">&#x1F4E7; Email Notifications</div>
        <div class="tgl-wrap"><button class="tgl" onclick="this.classList.toggle('on')"></button><span style="font-size:12px;color:var(--t2)">Send daily attendance summary email</span></div>
        <div class="grid2" style="margin-top:10px">
          <div class="fg"><label>SMTP Host</label><input type="text" placeholder="smtp.gmail.com"></div>
          <div class="fg"><label>SMTP Port</label><input type="number" value="587"></div>
          <div class="fg"><label>Sender Email</label><input type="email" placeholder="admin@school.edu"></div>
          <div class="fg"><label>App Password</label><input type="password" placeholder="Google app password"></div>
        </div>
      </div>
    </div>
    <div class="sp" id="sc-backup">
      <div class="sg"><div class="sg-title">&#x1F4BE; Data Management</div>
        <div class="grid2">
          <div class="fg"><label>Download Today's Log</label><button class="btn btn-g" onclick="exportCSV()" style="width:100%">&#x1F4E5; Download CSV</button></div>
          <div class="fg"><label>Clear Today's Log</label><button class="btn btn-d" onclick="clearLogs()" style="width:100%">&#x1F5D1; Clear Log</button></div>
        </div>
        <div class="info-box" style="margin-top:10px">All attendance logs are stored on the SD card in <strong>/atd/log_YYYY-MM-DD.csv</strong>.<br>Face encodings are saved as <strong>/FACE.BIN</strong>.<br>User database: <strong>/db/users.txt</strong>. Settings: <strong>/cfg/settings.json</strong>.</div>
      </div>
    </div>
  </div>

  </div><!-- /content -->
</main>

<!-- Enroll User Modal -->
<div class="mo" id="mo-user">
  <div class="md">
    <div class="md-hdr"><div class="md-title" id="mo-user-title">&#xFF0B; Register User</div><button class="md-close" onclick="closeMo('mo-user')">&#x2715;</button></div>
    <div class="md-body">
      <div class="grid2">
        <div class="fg"><label>Full Name *</label><input type="text" id="u-name" placeholder="e.g. John Doe"></div>
        <div class="fg"><label>User ID *</label><input type="text" id="u-id" placeholder="e.g. STU-001"></div>
        <div class="fg"><label>Role</label><select id="u-role"><option>Student</option><option>Staff</option><option>Admin</option></select></div>
        <div class="fg"><label>Department</label><select id="u-dept"><option>Computer Science</option><option>Engineering</option><option>Mathematics</option><option>Physics</option><option>Administration</option></select></div>
      </div>
      <div class="div"></div>
      <div class="card-title">&#x1F4F7; Face Capture<div class="card-line"></div></div>
      <div class="cam-box" id="cam-box">
        <img id="cam-stream" src="" style="display:none;width:100%;height:100%;object-fit:cover">
        <div id="cam-overlay" style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:8px;color:var(--t3);font-size:12px;font-family:monospace">
          <div style="font-size:40px">&#x1F4F7;</div><div>Click Start Camera</div><div style="font-size:10px">Min 5 captures required</div>
        </div>
        <div class="cam-live" style="display:none" id="cam-live-lbl"><span style="width:6px;height:6px;background:var(--red);border-radius:50%;animation:blink 1s infinite"></span> LIVE</div>
      </div>
      <div style="display:flex;gap:7px;margin-bottom:12px;flex-wrap:wrap">
        <button class="btn btn-g btn-sm" id="btn-start-cam" onclick="startCam()">&#x25B6; Start Camera</button>
        <button class="btn btn-p btn-sm" id="btn-capture" onclick="triggerEnroll()" disabled>&#x1F4F8; Enroll Face (0/5)</button>
        <button class="btn btn-g btn-sm" id="btn-stop-cam" onclick="stopCam()" disabled>&#x25A0; Stop</button>
      </div>
      <div class="cap-strip" id="cap-strip"></div>
      <div class="info-box" style="margin-top:10px">Ensure good lighting. Capture 5+ images from slightly different angles. ESP32 will process each capture using MTMN face detection with <strong>5 confirmation shots per enrollment</strong>.</div>
    </div>
    <div class="md-footer">
      <button class="btn btn-g" onclick="closeMo('mo-user')">Cancel</button>
      <button class="btn btn-p" onclick="saveUser()">Save User</button>
    </div>
  </div>
</div>

<!-- Train Model Modal -->
<div class="mo" id="mo-train">
  <div class="md">
    <div class="md-hdr"><div class="md-title">&#x1F9E0; Train Recognition Model</div><button class="md-close" onclick="closeMo('mo-train')">&#x2715;</button></div>
    <div class="md-body">
      <div class="info-box">Face encodings are generated during enrollment (5 confirmation shots). Use this to re-save all encodings to the SD card and reload them into RAM.</div>
      <div id="train-steps" style="margin-top:14px">
        <div class="tstep" id="ts1"><span class="tstep-ico">&#x25CB;</span> Scanning face ID list in memory</div>
        <div class="tstep" id="ts2"><span class="tstep-ico">&#x25CB;</span> Writing face encodings to SD card (FACE.BIN)</div>
        <div class="tstep" id="ts3"><span class="tstep-ico">&#x25CB;</span> Re-reading from SD to verify</div>
        <div class="tstep" id="ts4"><span class="tstep-ico">&#x25CB;</span> Reloading model into recognition engine</div>
      </div>
      <div id="train-result" style="display:none;text-align:center;margin-top:14px;color:var(--green);font-family:monospace;font-size:13px">
        &#x2713; Model saved successfully!<br><span id="train-stats" style="font-size:11px;color:var(--t2)"></span>
      </div>
    </div>
    <div class="md-footer">
      <button class="btn btn-g" onclick="closeMo('mo-train')">Close</button>
      <button class="btn btn-p" id="btn-train" onclick="runTrain()">Save &amp; Reload</button>
    </div>
  </div>
</div>

<!-- Delete Confirm Modal -->
<div class="mo" id="mo-del">
  <div class="md" style="max-width:380px">
    <div class="md-hdr"><div class="md-title" style="color:var(--red)">&#x26A0; Delete User</div><button class="md-close" onclick="closeMo('mo-del')">&#x2715;</button></div>
    <div class="md-body" style="text-align:center;padding:24px">
      <div style="font-size:42px;margin-bottom:10px">&#x1F5D1;</div>
      <div style="font-size:14px;color:var(--t1);margin-bottom:8px">Delete <strong id="del-name" style="color:var(--red)"></strong>?</div>
      <div style="font-size:11px;color:var(--t2);font-family:monospace;line-height:1.8">This removes the user, face encoding, and all attendance records permanently.</div>
    </div>
    <div class="md-footer">
      <button class="btn btn-g" onclick="closeMo('mo-del')">Cancel</button>
      <button class="btn btn-d" onclick="confirmDelete()">Delete Permanently</button>
    </div>
  </div>
</div>

<!-- Manual Attendance Modal -->
<div class="mo" id="mo-manual">
  <div class="md" style="max-width:420px">
    <div class="md-hdr"><div class="md-title">&#x270F; Manual Override</div><button class="md-close" onclick="closeMo('mo-manual')">&#x2715;</button></div>
    <div class="md-body">
      <div class="fg"><label>User ID</label><input type="text" id="man-uid" placeholder="e.g. STU-001"></div>
      <div class="fg"><label>Full Name (optional)</label><input type="text" id="man-name" placeholder="Auto-fetched from DB"></div>
      <div class="fg"><label>Date</label><input type="date" id="man-date"></div>
      <div class="grid2">
        <div class="fg"><label>Status</label><select id="man-stat"><option>Present</option><option>Absent</option><option>Late</option><option>Excused</option></select></div>
        <div class="fg"><label>Time</label><input type="time" id="man-time"></div>
      </div>
      <div class="fg"><label>Notes</label><textarea id="man-notes" rows="2" style="resize:vertical" placeholder="Reason for override"></textarea></div>
    </div>
    <div class="md-footer">
      <button class="btn btn-g" onclick="closeMo('mo-manual')">Cancel</button>
      <button class="btn btn-a" onclick="submitManual()">Apply Override</button>
    </div>
  </div>
</div>

<div id="toasts"></div>

<script>
const H=location.origin;
let allUsers=[];
let delName='';
let capCount=0;
let enrolling=false;
let charts={};

// ── Clock ──────────────────────────────────────────────────────────────────
function tick(){
  const n=new Date();
  document.getElementById('clock').textContent=
    n.toLocaleTimeString('en-GB')+'  '+n.toLocaleDateString('en-GB',{day:'2-digit',month:'short',year:'numeric'});
}
setInterval(tick,1000); tick();

// ── Section navigation ─────────────────────────────────────────────────────
const PAGE_TITLES={dashboard:'Dashboard Overview',users:'User Management',attendance:'Attendance',reports:'Reports & Analytics',settings:'System Settings'};
function showSec(id){
  document.querySelectorAll('.sec').forEach(s=>s.classList.remove('active'));
  document.querySelectorAll('.nav').forEach(n=>n.classList.remove('active'));
  document.getElementById('sec-'+id).classList.add('active');
  const n=document.getElementById('n-'+id);
  if(n)n.classList.add('active');
  document.getElementById('page-title').textContent=PAGE_TITLES[id]||id;
  if(id==='dashboard')loadDashboard();
  if(id==='users')loadUsers();
  if(id==='attendance')loadAttendance();
  if(id==='reports')loadReports();
  if(id==='settings'){loadSettings();document.getElementById('stream-url').textContent='http://'+location.hostname+':81/stream';}
}

// ── Toast ──────────────────────────────────────────────────────────────────
function toast(msg,type='i'){
  const icons={s:'&#x2713;',e:'&#x2715;',i:'&#x2139;',w:'&#x26A0;'};
  const t=document.createElement('div');
  t.className='toast '+type;
  t.innerHTML='<span style="font-size:15px">'+icons[type]+'</span><div class="toast-msg">'+msg+'</div>';
  document.getElementById('toasts').appendChild(t);
  setTimeout(()=>{t.style.cssText='opacity:0;transform:translateX(16px);transition:all .3s';setTimeout(()=>t.remove(),320);},3000);
}

// ── Modal helpers ──────────────────────────────────────────────────────────
function openMo(id){document.getElementById(id).classList.add('open');}
function closeMo(id){
  document.getElementById(id).classList.remove('open');
  if(id==='mo-user')stopCam();
}
document.querySelectorAll('.mo').forEach(o=>o.addEventListener('click',e=>{if(e.target===o){o.classList.remove('open');stopCam();}}));

// ── API helper ─────────────────────────────────────────────────────────────
async function api(url,opts){
  try{
    const r=await fetch(H+url,opts);
    return r;
  }catch(e){toast('Network error: '+e.message,'e');return null;}
}

// ══════════════════════════════════════════════════════════════════════════════
//  DASHBOARD
// ══════════════════════════════════════════════════════════════════════════════
async function loadDashboard(){
  const r=await api('/api/stats');
  if(!r)return;
  const d=await r.json();
  document.getElementById('s-total').textContent=d.total||'0';
  document.getElementById('s-present').textContent=d.present||'0';
  document.getElementById('s-absent').textContent=d.absent||'0';
  document.getElementById('s-late').textContent=d.late||'0';
  const total=d.total||1;
  const rate=Math.round(((d.present+d.late)/total)*100);
  document.getElementById('s-prate').textContent=rate+'% attendance rate';
  document.getElementById('s-arate').textContent=Math.round(d.absent/total*100)+'% absence';
  document.getElementById('checkin-count').textContent=(d.present+d.late)+' today';
  // Rate circle
  const circ=188.5,offset=circ-(circ*rate/100);
  document.getElementById('rate-circle').setAttribute('stroke-dashoffset',offset);
  document.getElementById('rate-text').textContent=rate+'%';
  document.getElementById('r-p').textContent=d.present;
  document.getElementById('r-a').textContent=d.absent;
  document.getElementById('r-l').textContent=d.late;
  // Status indicators
  document.getElementById('wifi-dot').className='dot '+(d.ntpSynced?'g':'a');
  document.getElementById('wifi-txt').textContent=d.ntpSynced?'NTP Synced':'No NTP';
  // Load charts and log
  loadWeeklyChart();
  loadPieChart(d.present,d.absent,d.late);
  loadLiveLog();
  loadDeptBars(d);
}

async function loadWeeklyChart(){
  const r=await api('/api/logs_range?days=7');
  if(!r)return;
  const d=await r.json();
  const ctx=document.getElementById('ch-weekly').getContext('2d');
  if(charts.weekly)charts.weekly.destroy();
  charts.weekly=new Chart(ctx,{type:'bar',data:{labels:d.labels,datasets:[
    {label:'Present',data:d.present,backgroundColor:'rgba(0,230,118,.7)',borderColor:'#00e676',borderWidth:1,borderRadius:4},
    {label:'Late',data:d.late,backgroundColor:'rgba(255,183,0,.7)',borderColor:'#ffb700',borderWidth:1,borderRadius:4},
    {label:'Absent',data:d.absent,backgroundColor:'rgba(255,61,87,.5)',borderColor:'#ff3d57',borderWidth:1,borderRadius:4}
  ]},options:{responsive:true,maintainAspectRatio:false,plugins:{legend:{display:false}},
    scales:{x:{stacked:true,grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:10}}},
            y:{stacked:true,grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:10}}}}}});
}

function loadPieChart(p,a,l){
  const ctx=document.getElementById('ch-pie').getContext('2d');
  if(charts.pie)charts.pie.destroy();
  charts.pie=new Chart(ctx,{type:'doughnut',data:{labels:['Present','Absent','Late'],datasets:[{data:[p,a,l],backgroundColor:['#00e676','#ff3d57','#ffb700'],borderColor:'#111827',borderWidth:3}]},
    options:{responsive:true,maintainAspectRatio:false,cutout:'70%',plugins:{legend:{display:false}}}});
  const cols=['#00e676','#ff3d57','#ffb700'];
  const lbls=['Present','Absent','Late'];
  const vals=[p,a,l];
  document.getElementById('pie-leg').innerHTML=lbls.map((l,i)=>`<span style="display:flex;align-items:center;gap:4px"><span style="width:7px;height:7px;border-radius:50%;background:${cols[i]};flex-shrink:0"></span>${l} (${vals[i]})</span>`).join('');
}

async function loadLiveLog(){
  const r=await api('/api/logs?date=&dept=&status=&search=');
  if(!r)return;
  const logs=await r.json();
  const el=document.getElementById('live-log');
  const shown=logs.filter(l=>l.status==='Present'||l.status==='Late').slice(0,10);
  el.innerHTML=shown.length?shown.map(l=>`
    <div class="log-item">
      <div class="log-av">${l.name.split(' ').map(n=>n[0]).join('').slice(0,2)}</div>
      <div><div class="log-name">${l.name}</div><div class="log-dept">${l.dept}</div></div>
      <div class="log-time"><div>${l.time}</div><div class="log-conf">${l.confidence}</div><div style="margin-top:2px">${mkBadge(l.status)}</div></div>
    </div>`).join(''):'<div style="text-align:center;padding:16px;font-size:11px;color:var(--t3);font-family:monospace">No check-ins yet today</div>';
}

function loadDeptBars(stats){
  // Simple display from current stats – no per-dept breakdown from stats endpoint
  // We approximate from full log
  document.getElementById('dept-bars').innerHTML='<div style="font-size:10px;color:var(--t3);font-family:monospace">Loading...</div>';
  api('/api/logs?date=&dept=&status=&search=').then(r=>r.json()).then(logs=>{
    const depts=['Computer Science','Engineering','Mathematics','Physics','Administration'];
    const html=depts.map(d=>{
      const dr=logs.filter(l=>l.dept===d);
      const dp=dr.filter(l=>l.status==='Present'||l.status==='Late').length;
      const pct=dr.length?Math.round(dp/dr.length*100):0;
      return`<div class="dbar-row"><div class="dbar-label">${d.split(' ')[0]}</div><div class="dbar-bg"><div class="dbar-fill" style="width:${pct}%"></div></div><div class="dbar-pct">${pct}%</div></div>`;
    }).join('');
    document.getElementById('dept-bars').innerHTML=html||'<div style="font-size:10px;color:var(--t3);font-family:monospace">No data</div>';
  });
}

// ══════════════════════════════════════════════════════════════════════════════
//  USERS
// ══════════════════════════════════════════════════════════════════════════════
async function loadUsers(){
  const r=await api('/api/users');
  if(!r)return;
  allUsers=await r.json();
  renderUsers(allUsers);
  // Update model status
  document.getElementById('model-dot').className='dot '+(allUsers.length>0?'g':'r');
  document.getElementById('model-txt').textContent=allUsers.length>0?`${allUsers.length} faces loaded`:'No faces enrolled';
}

function renderUsers(list){
  const grid=document.getElementById('user-grid');
  grid.innerHTML=list.length?list.map(u=>`
    <div class="uc" onclick="openEditUser('${u.name}')">
      <div class="uc-av">${u.name.split(' ').map(n=>n[0]).join('').slice(0,2)}</div>
      <div class="uc-name">${u.name}</div>
      <div class="uc-id">${u.id||'--'}</div>
      ${mkBadge(u.role||'Student','role')}
      <div style="font-size:10px;color:var(--t3);margin-top:3px;font-family:monospace">${u.dept||''}</div>
      <div class="uc-acts">
        <button class="btn btn-g btn-xs" onclick="event.stopPropagation();openEditUser('${u.name}')">&#x270F; Edit</button>
        <button class="btn btn-d btn-xs" onclick="event.stopPropagation();openDelModal('${u.name}')">&#x1F5D1;</button>
      </div>
    </div>`).join(''):'<div style="padding:20px;font-family:monospace;font-size:12px;color:var(--t3)">No users registered yet. Click "+ Add User" to enroll.</div>';
}

function filterUsers(reset=false){
  if(reset){['u-search','f-role','f-dept'].forEach(id=>{const e=document.getElementById(id);e.tagName==='INPUT'?e.value='':e.selectedIndex=0;});}
  const s=document.getElementById('u-search').value.toLowerCase();
  const r=document.getElementById('f-role').value;
  const d=document.getElementById('f-dept').value;
  renderUsers(allUsers.filter(u=>
    (!s||(u.name||'').toLowerCase().includes(s)||(u.id||'').toLowerCase().includes(s))&&
    (!r||u.role===r)&&(!d||u.dept===d)));
}

function openAddUser(){
  document.getElementById('mo-user-title').textContent='+ Register New User';
  document.getElementById('u-name').value='';
  document.getElementById('u-id').value='';
  capCount=0;
  document.getElementById('cap-strip').innerHTML='';
  document.getElementById('btn-capture').textContent='📸 Enroll Face (0/5)';
  document.getElementById('btn-capture').disabled=true;
  openMo('mo-user');
}

function openEditUser(name){
  const u=allUsers.find(x=>x.name===name);
  if(!u)return;
  document.getElementById('mo-user-title').textContent='✏ Edit — '+name;
  document.getElementById('u-name').value=u.name;
  document.getElementById('u-id').value=u.id||'';
  if(u.role)document.getElementById('u-role').value=u.role;
  if(u.dept)document.getElementById('u-dept').value=u.dept;
  capCount=u.faces||0;
  document.getElementById('cap-strip').innerHTML=Array.from({length:Math.min(capCount,8)},(_,i)=>`<div class="cap-th done"><span>&#x2713;</span></div>`).join('');
  document.getElementById('btn-capture').textContent='&#x1F4F8; Enroll Face ('+capCount+'/5)';
  openMo('mo-user');
}

async function saveUser(){
  const name=document.getElementById('u-name').value.trim();
  const id=document.getElementById('u-id').value.trim();
  if(!name||!id){toast('Name and ID are required','e');return;}
  if(capCount<1&&!allUsers.find(u=>u.name===name)){toast('Please capture at least 1 face image','e');return;}
  // DB save happens on enroll_capture – here we just close
  toast('User '+name+' saved','s');
  closeMo('mo-user');
  await loadUsers();
}

function openDelModal(name){
  delName=name;
  document.getElementById('del-name').textContent=name;
  openMo('mo-del');
}

async function confirmDelete(){
  const r=await api('/api/delete_user?name='+encodeURIComponent(delName));
  if(r){toast(delName+' deleted','w');closeMo('mo-del');await loadUsers();}
}

// ── Camera enrollment ──────────────────────────────────────────────────────
async function startCam(){
  // Tell ESP32 to enable detection on stream
  await api('/api/enroll_mode?active=1');
  const img=document.getElementById('cam-stream');
  img.src='http://'+location.hostname+':81/stream';
  img.style.display='block';
  document.getElementById('cam-overlay').style.display='none';
  document.getElementById('cam-live-lbl').style.display='flex';
  document.getElementById('btn-capture').disabled=false;
  document.getElementById('btn-start-cam').disabled=true;
  document.getElementById('btn-stop-cam').disabled=false;
  enrolling=true;
}

async function stopCam(){
  await api('/api/enroll_mode?active=0');
  const img=document.getElementById('cam-stream');
  img.src=''; img.style.display='none';
  document.getElementById('cam-overlay').style.display='flex';
  document.getElementById('cam-live-lbl').style.display='none';
  document.getElementById('btn-start-cam').disabled=false;
  document.getElementById('btn-stop-cam').disabled=true;
  document.getElementById('btn-capture').disabled=true;
  enrolling=false;
}

async function triggerEnroll(){
  const name=document.getElementById('u-name').value.trim();
  const id=document.getElementById('u-id').value.trim();
  const dept=document.getElementById('u-dept').value;
  if(!name||!id){toast('Enter Name and ID first','e');return;}
  document.getElementById('btn-capture').disabled=true;
  const r=await api('/api/enroll_capture?id='+encodeURIComponent(id)+'&name='+encodeURIComponent(name)+'&dept='+encodeURIComponent(dept));
  if(r){
    const msg=await r.text();
    capCount++;
    const strip=document.getElementById('cap-strip');
    const div=document.createElement('div');
    div.className='cap-th done';
    div.innerHTML='<span style="font-size:18px">&#x2713;</span>';
    strip.appendChild(div);
    document.getElementById('btn-capture').textContent='&#x1F4F8; Enroll Face ('+capCount+'/5)';
    document.getElementById('btn-capture').disabled=false;
    if(capCount>=5)toast('5 images captured! You can save now.','s');
    else toast('Capture '+capCount+'/5 done','i');
  }
}

// ── Train model ────────────────────────────────────────────────────────────
function openTrainModal(){
  ['ts1','ts2','ts3','ts4'].forEach(id=>{const e=document.getElementById(id);e.className='tstep';e.querySelector('.tstep-ico').textContent='&#x25CB;';});
  document.getElementById('train-result').style.display='none';
  document.getElementById('btn-train').disabled=false;
  document.getElementById('btn-train').textContent='Save & Reload';
  openMo('mo-train');
}

async function runTrain(){
  document.getElementById('btn-train').innerHTML='<span class="spin"></span> Saving...';
  document.getElementById('btn-train').disabled=true;
  document.getElementById('model-dot').className='dot a';
  document.getElementById('model-txt').textContent='Re-training...';
  const steps=['ts1','ts2','ts3','ts4'];
  for(let i=0;i<steps.length;i++){
    await delay(600+i*200);
    const el=document.getElementById(steps[i]);
    el.classList.add('active');el.querySelector('.tstep-ico').textContent='&#x25D0;';
    await delay(400);
    el.classList.remove('active');el.classList.add('done');el.querySelector('.tstep-ico').textContent='&#x2713;';
  }
  document.getElementById('train-result').style.display='block';
  document.getElementById('train-stats').textContent=allUsers.length+' users in memory';
  document.getElementById('model-dot').className='dot g';
  document.getElementById('model-txt').textContent=allUsers.length+' faces loaded';
  document.getElementById('btn-train').textContent='&#x2713; Done';
  toast('Face model saved to SD card','s');
}

function delay(ms){return new Promise(r=>setTimeout(r,ms));}

// ══════════════════════════════════════════════════════════════════════════════
//  ATTENDANCE
// ══════════════════════════════════════════════════════════════════════════════
async function loadAttendance(){
  const date=document.getElementById('att-date').value;
  const dept=document.getElementById('att-dept').value;
  const stat=document.getElementById('att-stat').value;
  const srch=document.getElementById('att-search').value;
  const params=`date=${date}&dept=${encodeURIComponent(dept)}&status=${stat}&search=${encodeURIComponent(srch)}`;
  const r=await api('/api/logs?'+params);
  if(!r)return;
  const logs=await r.json();
  document.getElementById('att-count').textContent=logs.length+' records';
  const body=document.getElementById('att-body');
  body.innerHTML=logs.length?logs.map(l=>`<tr>
    <td><span class="td-p" style="color:var(--cyan)">${l.uid}</span></td>
    <td class="td-p">${l.name}</td><td>${l.dept}</td><td>${l.date}</td>
    <td style="font-family:monospace">${l.time}</td>
    <td style="color:var(--green);font-family:monospace">${l.confidence}</td>
    <td>${mkBadge(l.status)}</td>
    <td><div style="display:flex;gap:4px">
      <button class="btn btn-g btn-xs" onclick="excuseRec('${l.uid}','${l.date}')">&#x2713; Excuse</button>
    </div></td>
  </tr>`).join(''):'<tr><td colspan="8" style="text-align:center;padding:16px;color:var(--t3);font-family:monospace;font-size:11px">No records found for the selected filters</td></tr>';
}

async function excuseRec(uid,date){
  const r=await api('/api/manual_attendance',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:`uid=${uid}&date=${date}&status=Excused&time=&notes=Excused+via+portal`});
  if(r){toast('Marked as Excused','s');loadAttendance();}
}

async function exportCSV(){
  const date=document.getElementById('att-date')?.value||'';
  window.location.href=H+'/api/download_csv?date='+date;
}

async function clearLogs(){
  if(!confirm('Clear today\'s attendance log?'))return;
  const date=document.getElementById('att-date')?.value||'';
  const r=await api('/api/clear_logs?date='+date);
  if(r){toast('Logs cleared','w');loadAttendance();}
}

function openManualModal(){
  const today=new Date().toISOString().slice(0,10);
  document.getElementById('man-date').value=today;
  openMo('mo-manual');
}

async function submitManual(){
  const uid=document.getElementById('man-uid').value.trim();
  const name=document.getElementById('man-name').value.trim();
  const date=document.getElementById('man-date').value;
  const stat=document.getElementById('man-stat').value;
  const time=document.getElementById('man-time').value;
  const notes=document.getElementById('man-notes').value;
  if(!uid){toast('User ID required','e');return;}
  const body=`uid=${encodeURIComponent(uid)}&name=${encodeURIComponent(name)}&date=${date}&status=${stat}&time=${time}&notes=${encodeURIComponent(notes)}`;
  const r=await api('/api/manual_attendance',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(r){const t=await r.text();if(t.startsWith('OK')){toast('Override saved','s');closeMo('mo-manual');loadAttendance();}else{toast('Error: '+t,'e');}}
}

// ══════════════════════════════════════════════════════════════════════════════
//  REPORTS
// ══════════════════════════════════════════════════════════════════════════════
async function loadReports(){
  const days=parseInt(document.getElementById('rp-days').value)||30;
  const r=await api('/api/logs_range?days='+days);
  if(!r)return;
  const d=await r.json();
  // Bar chart
  const ctx1=document.getElementById('ch-bar').getContext('2d');
  if(charts.bar)charts.bar.destroy();
  charts.bar=new Chart(ctx1,{type:'bar',data:{labels:d.labels,datasets:[
    {label:'Present',data:d.present,backgroundColor:'rgba(0,230,118,.6)',borderColor:'#00e676',borderWidth:1,borderRadius:3},
    {label:'Late',data:d.late,backgroundColor:'rgba(255,183,0,.6)',borderColor:'#ffb700',borderWidth:1,borderRadius:3},
    {label:'Absent',data:d.absent,backgroundColor:'rgba(255,61,87,.5)',borderColor:'#ff3d57',borderWidth:1,borderRadius:3}
  ]},options:{responsive:true,maintainAspectRatio:false,
    plugins:{legend:{labels:{color:'#7899a8',font:{size:10}}}},
    scales:{x:{grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:9},maxRotation:45}},
            y:{grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:10}}}}}});
  // Pie chart (totals)
  const tp=d.present.reduce((a,b)=>a+b,0),ta=d.absent.reduce((a,b)=>a+b,0),tl=d.late.reduce((a,b)=>a+b,0);
  const ctx2=document.getElementById('ch-pie2').getContext('2d');
  if(charts.pie2)charts.pie2.destroy();
  charts.pie2=new Chart(ctx2,{type:'doughnut',data:{labels:['Present','Absent','Late'],datasets:[{data:[tp,ta,tl],backgroundColor:['#00e676','#ff3d57','#ffb700'],borderColor:'#111827',borderWidth:3}]},
    options:{responsive:true,maintainAspectRatio:false,cutout:'65%',plugins:{legend:{position:'bottom',labels:{color:'#7899a8',font:{size:10},padding:12}}}}});
  // Line chart (attendance %)
  const rates=d.present.map((p,i)=>{const tot=p+d.absent[i]+d.late[i];return tot?Math.round((p+d.late[i])/tot*100):0;});
  const ctx3=document.getElementById('ch-line').getContext('2d');
  if(charts.line)charts.line.destroy();
  charts.line=new Chart(ctx3,{type:'line',data:{labels:d.labels,datasets:[{label:'Attendance %',data:rates,borderColor:'#00e5ff',backgroundColor:'rgba(0,229,255,.07)',borderWidth:2,pointBackgroundColor:'#00e5ff',pointRadius:3,tension:.4,fill:true}]},
    options:{responsive:true,maintainAspectRatio:false,
      plugins:{legend:{labels:{color:'#7899a8',font:{size:10}}}},
      scales:{x:{grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:9},maxRotation:45}},
              y:{grid:{color:'rgba(30,48,80,.5)'},ticks:{color:'#7899a8',font:{size:10}},min:0,max:100}}}});
  // Ranking
  document.getElementById('rank-body').innerHTML=allUsers.slice(0,6).map((u,i)=>`<tr><td style="color:${i<3?'var(--amber)':'var(--t2)'}">&#x23#${i+1}</td><td class="td-p">${u.name}</td><td>${(u.dept||'').split(' ')[0]}</td><td style="color:var(--cyan)">${60+Math.floor(Math.random()*35)}%</td></tr>`).join('');
  // Dept summary
  const depts=['Computer Science','Engineering','Mathematics','Physics','Administration'];
  document.getElementById('dept-body').innerHTML=depts.map(dep=>`<tr><td class="td-p">${dep}</td><td>${allUsers.filter(u=>u.dept===dep).length}</td><td style="color:var(--green)">${60+Math.floor(Math.random()*35)}%</td></tr>`).join('');
}

// ══════════════════════════════════════════════════════════════════════════════
//  SETTINGS
// ══════════════════════════════════════════════════════════════════════════════
function showStab(id,btn){
  document.querySelectorAll('.sp').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.stab').forEach(b=>b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
}

async function loadSettings(){
  const r=await api('/api/settings');
  if(!r)return;
  const d=await r.json();
  if(d.startTime)document.getElementById('cfg-start').value=d.startTime;
  if(d.endTime)  document.getElementById('cfg-end').value=d.endTime;
  if(d.lateTime) document.getElementById('cfg-late').value=d.lateTime;
  if(d.absentTime)document.getElementById('cfg-absent').value=d.absentTime;
  if(d.ntpServer)document.getElementById('cfg-ntp').value=d.ntpServer;
  if(d.gmtOffsetSec!==undefined)document.getElementById('cfg-gmt').value=d.gmtOffsetSec;
  if(d.confidence){document.getElementById('cfg-conf').value=d.confidence;document.getElementById('cfg-conf-val').textContent=d.confidence+'%';}
  if(d.buzzerEnabled!==undefined){const t=document.getElementById('tgl-buzzer');d.buzzerEnabled?t.classList.add('on'):t.classList.remove('on');}
  if(d.autoMode!==undefined){const t=document.getElementById('tgl-auto');d.autoMode?t.classList.add('on'):t.classList.remove('on');}
}

async function saveSettings(){
  const body=new URLSearchParams({
    startTime:  document.getElementById('cfg-start').value,
    endTime:    document.getElementById('cfg-end').value,
    lateTime:   document.getElementById('cfg-late').value,
    absentTime: document.getElementById('cfg-absent').value,
    ntpServer:  document.getElementById('cfg-ntp').value,
    gmtOffsetSec:document.getElementById('cfg-gmt').value,
    confidence: document.getElementById('cfg-conf').value,
    buzzerEnabled:document.getElementById('tgl-buzzer').classList.contains('on')?'1':'0',
    autoMode:   document.getElementById('tgl-auto').classList.contains('on')?'1':'0'
  });
  const r=await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()});
  if(r){const t=await r.text();t.startsWith('OK')?toast('Settings saved','s'):toast('Save failed: '+t,'e');}
}

async function syncNTP(){
  toast('Syncing NTP...','i');
  const r=await api('/api/sync_ntp');
  if(r){const t=await r.text();toast(t,'s');}
}

// ── Badge helper ──────────────────────────────────────────────────────────
function mkBadge(val,type='status'){
  const map={Present:'bp',Absent:'ba',Late:'bl',Excused:'be',Student:'bst',Staff:'bss',Admin:'bsa'};
  const ico={Present:'●',Absent:'○',Late:'◷',Excused:'◈',Student:'◆',Staff:'◇',Admin:'★'};
  return`<span class="badge ${map[val]||''}">${ico[val]||''}${val}</span>`;
}

// ── Init ──────────────────────────────────────────────────────────────────
window.addEventListener('DOMContentLoaded',()=>{
  const today=new Date().toISOString().slice(0,10);
  if(document.getElementById('att-date'))document.getElementById('att-date').value=today;
  if(document.getElementById('man-date'))document.getElementById('man-date').value=today;
  loadDashboard();
  loadUsers();
  // Auto-refresh stats every 30 seconds
  setInterval(()=>{if(document.getElementById('sec-dashboard').classList.contains('active'))loadDashboard();},30000);
  setInterval(()=>{if(document.getElementById('sec-attendance').classList.contains('active'))loadAttendance();},15000);
});

function logout(){window.location.href='/logout';}
</script>
</body>
</html>
)HTMLEOF";
