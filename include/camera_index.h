#pragma once
#include <pgmspace.h>

// Login page – shown when the user is not authenticated.
// Credentials are checked against ADMIN_USER / ADMIN_PASS in app_httpd.cpp.

const char login_html[] PROGMEM = R"LOGINEOF(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FaceGuard Pro – Login</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#080c12;color:#e8f4f8;min-height:100vh;display:flex;align-items:center;justify-content:center}
body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(0,229,255,.025) 1px,transparent 1px),linear-gradient(90deg,rgba(0,229,255,.025) 1px,transparent 1px);background-size:40px 40px;pointer-events:none}
.box{background:#111827;border:1px solid #1e3050;border-radius:14px;padding:40px 36px;width:320px;position:relative;box-shadow:0 24px 60px rgba(0,0,0,.6),0 0 40px rgba(0,229,255,.05)}
.box::before{content:'';position:absolute;top:0;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,rgba(0,229,255,.3),transparent);border-radius:14px 14px 0 0}
.logo{text-align:center;margin-bottom:28px}
.logo-icon{width:52px;height:52px;background:#00e5ff;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;font-size:26px;margin-bottom:12px;box-shadow:0 0 24px rgba(0,229,255,.3)}
.logo-title{font-size:18px;font-weight:700;letter-spacing:.5px}
.logo-sub{font-size:10px;color:#00e5ff;letter-spacing:3px;text-transform:uppercase;margin-top:2px}
label{display:block;font-size:9px;letter-spacing:1.5px;text-transform:uppercase;color:#7899a8;margin-bottom:6px}
.fg{margin-bottom:16px}
input{width:100%;background:#0d1520;border:1px solid #1e3050;border-radius:7px;padding:11px 14px;font-size:13px;color:#e8f4f8;outline:none;transition:border-color .18s,box-shadow .18s;font-family:monospace}
input:focus{border-color:#00e5ff;box-shadow:0 0 0 2px rgba(0,229,255,.08)}
button{width:100%;background:#00e5ff;color:#080c12;border:none;border-radius:7px;padding:12px;font-size:14px;font-weight:700;cursor:pointer;transition:all .18s;letter-spacing:.5px;margin-top:4px}
button:hover{background:#00b4cc;box-shadow:0 0 20px rgba(0,229,255,.3);transform:translateY(-1px)}
.err{color:#ff3d57;text-align:center;font-size:12px;font-family:monospace;margin-top:12px;display:none;background:rgba(255,61,87,.1);border:1px solid rgba(255,61,87,.25);border-radius:5px;padding:8px}
.hint{text-align:center;font-size:10px;color:#3d5a6b;margin-top:16px;font-family:monospace;line-height:1.6}
</style>
</head>
<body>
<div class="box">
  <div class="logo">
    <div class="logo-icon">&#x1F441;</div>
    <div class="logo-title">FaceGuard Pro</div>
    <div class="logo-sub">Admin Portal</div>
  </div>
  <form action="/login" method="POST">
    <div class="fg"><label>Username</label><input type="text" name="user" placeholder="admin" required autocomplete="username"></div>
    <div class="fg"><label>Password</label><input type="password" name="pass" placeholder="••••" required autocomplete="current-password"></div>
    <button type="submit">Sign In</button>
  </form>
  <div class="err" id="err">&#x2715; Invalid credentials</div>
  <div class="hint">ESP32-CAM Facial Attendance System<br>Attendance logging continues in background</div>
</div>
<script>
if(location.search.includes('error'))document.getElementById('err').style.display='block';
</script>
</body>
</html>
)LOGINEOF";
