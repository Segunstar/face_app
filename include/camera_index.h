#pragma once
#include <pgmspace.h>

// Optional: Login page HTML (currently not used in your implementation)
// To enable login, you need to modify index_handler to check authentication first
const char login_html[] PROGMEM = R"rawtext(
<!doctype html>
<html>
    <head>
        <title>ESP32 Attendance Login</title>
        <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <style>
            body { 
                font-family: sans-serif; 
                background: #121212; 
                color: #eee; 
                display: flex; 
                align-items: center; 
                justify-content: center; 
                height: 100vh; 
                margin: 0;
            }
            .login-box {
                background: #1e1e1e;
                padding: 40px;
                border-radius: 10px;
                box-shadow: 0 4px 6px rgba(0,0,0,0.3);
                width: 300px;
            }
            h2 { 
                margin: 0 0 20px 0; 
                text-align: center;
                color: #007bff;
            }
            input {
                width: 100%;
                padding: 12px;
                margin: 10px 0;
                background: #2c2c2c;
                border: 1px solid #444;
                color: white;
                border-radius: 5px;
                box-sizing: border-box;
            }
            button {
                width: 100%;
                padding: 12px;
                background: #007bff;
                color: white;
                border: none;
                border-radius: 5px;
                cursor: pointer;
                font-size: 16px;
                margin-top: 10px;
            }
            button:hover {
                background: #0056b3;
            }
            .error {
                color: #ff4444;
                text-align: center;
                margin-top: 10px;
                display: none;
            }
        </style>
    </head>
    <body>
        <div class="login-box">
            <h2>🎓 Attendance System</h2>
            <form action="/login" method="POST" id="loginForm">
                <input type="text" name="user" placeholder="Username" required>
                <input type="password" name="pass" placeholder="Password" required>
                <button type="submit">Login</button>
            </form>
            <div class="error" id="error">Invalid credentials</div>
        </div>
        <script>
            // Check for error in URL
            if(window.location.search.includes('error')) {
                document.getElementById('error').style.display = 'block';
            }
        </script>
    </body>
</html>
)rawtext";
