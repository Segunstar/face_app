#pragma once
#include <pgmspace.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Facial Attendance</title>
    <style>
        :root { --primary: #007bff; --bg: #121212; --card: #1e1e1e; --text: #eee; }
        body { margin: 0; font-family: sans-serif; background: var(--bg); color: var(--text); display: flex; height: 100vh; }
        
        /* Sidebar */
        .sidebar { width: 250px; background: #000; padding: 20px; display: flex; flex-direction: column; gap: 10px; }
        .nav-btn { background: transparent; border: none; color: #888; text-align: left; padding: 15px; cursor: pointer; font-size: 16px; border-radius: 8px; }
        .nav-btn:hover, .nav-btn.active { background: var(--primary); color: white; }
        
        /* Main Content */
        .main { flex: 1; padding: 20px; overflow-y: auto; }
        .section { display: none; }
        .section.active { display: block; animation: fadein 0.3s; }
        
        /* Dashboard Cards */
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; }
        .card { background: var(--card); padding: 20px; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .card h3 { margin: 0 0 10px 0; color: #aaa; font-size: 14px; }
        .card .val { font-size: 24px; font-weight: bold; }

        /* Tables */
        table { width: 100%; border-collapse: collapse; margin-top: 20px; }
        th, td { text-align: left; padding: 12px; border-bottom: 1px solid #333; }
        th { color: var(--primary); }

        /* Forms */
        input, select { width: 100%; padding: 10px; margin: 5px 0 15px 0; background: #2c2c2c; border: 1px solid #444; color: white; border-radius: 5px; }
        button.action { background: var(--primary); color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }
        button.delete { background: #ff4444; }

        @keyframes fadein { from { opacity: 0; } to { opacity: 1; } }
    </style>
</head>
<body>

    <div class="sidebar">
        <h2 style="color:white; text-align:center;">ESP32<span style="color:var(--primary)">CAM</span></h2>
        <button class="nav-btn active" onclick="show('home')">Overview</button>
        <button class="nav-btn" onclick="show('users')">Users</button>
        <button class="nav-btn" onclick="show('database')">Attendance Logs</button>
        <button class="nav-btn" onclick="show('settings')">Settings</button>
        <button class="nav-btn" onclick="show('about')">Contact / About</button>
        <div style="flex:1"></div>
        <button class="nav-btn" style="color:#ff4444" onclick="logout()">Logout</button>
    </div>

    <div class="main">
        
        <div id="home" class="section active">
            <h1>System Overview</h1>
            <div class="grid">
                <div class="card"><h3>Total Users</h3><div class="val" id="totalUsers">0</div></div>
                <div class="card"><h3>Present Today</h3><div class="val" id="presentToday">0</div></div>
                <div class="card"><h3>Storage Used</h3><div class="val" id="storageUsed">-- MB</div></div>
                <div class="card"><h3>System Status</h3><div class="val" style="color:#0f0">Online</div></div>
            </div>
        </div>

        <div id="users" class="section">
            <div style="display:flex; justify-content:space-between; align-items:center;">
                <h1>Registered Users</h1>
                <button class="action" onclick="openEnrollModal()">+ Add User</button>
            </div>
            <table id="userTable">
                <thead><tr><th>ID</th><th>Name</th><th>Department</th><th>Action</th></tr></thead>
                <tbody></tbody>
            </table>
        </div>

        <div id="database" class="section">
            <h1>Attendance Database</h1>
            <button class="action" onclick="downloadLogs()">Download CSV</button>
            <button class="action delete" onclick="clearLogs()">Clear Logs</button>
            <table id="logTable">
                <thead><tr><th>Student ID</th><th>Name</th><th>Time In</th></tr></thead>
                <tbody></tbody>
            </table>
        </div>

        <div id="settings" class="section">
            <h1>Settings</h1>
            <div class="card" style="max-width: 500px">
                <label>Attendance Start Time</label>
                <input type="time" id="startTime">
                <label>Attendance End Time</label>
                <input type="time" id="endTime">
                <div style="margin: 10px 0;">
                    <input type="checkbox" id="buzzerToggle" style="width:auto;"> Enable Buzzer
                </div>
                <button class="action" onclick="saveSettings()">Save Configuration</button>
            </div>
        </div>
        
        <div id="about" class="section">
            <h1>About</h1>
            <p>ESP32-CAM Facial Attendance System v1.0</p>
            <p>Developed for PlatformIO/Arduino environment.</p>
        </div>

    </div>

    <div id="enrollModal" style="display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,0.8); z-index:99;">
        <div style="background:#1e1e1e; width:400px; margin:100px auto; padding:20px; border-radius:10px;">
            <h2>Enroll New User</h2>
            <img id="stream" src="" style="width:100%; border-radius:5px; margin-bottom:10px;">
            <input type="text" id="newId" placeholder="Student ID">
            <input type="text" id="newName" placeholder="Full Name">
            <input type="text" id="newDept" placeholder="Department">
            <button class="action" onclick="captureAndEnroll()">Capture & Save</button>
            <button class="action delete" onclick="document.getElementById('enrollModal').style.display='none'">Cancel</button>
        </div>
    </div>

    <script>
        const host = document.location.origin;
        
        function show(id) {
            document.querySelectorAll('.section').forEach(el => el.classList.remove('active'));
            document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));
            document.getElementById(id).classList.add('active');
            event.target.classList.add('active');
            
            if(id === 'users') fetchUsers();
            if(id === 'database') fetchLogs();
        }

        function logout() {
            window.location.href = "/logout";
        }

        // --- API Calls ---
        
        function fetchUsers() {
            fetch(host + '/api/users').then(res => res.json()).then(data => {
                const tbody = document.querySelector('#userTable tbody');
                tbody.innerHTML = '';
                document.getElementById('totalUsers').innerText = data.length;
                data.forEach(u => {
                    tbody.innerHTML += `<tr>
                        <td>${u.id}</td>
                        <td>${u.name}</td>
                        <td>${u.dept}</td>
                        <td><button class="delete" onclick="deleteUser('${u.name}')">Delete</button></td>
                    </tr>`;
                });
            });
        }

        function fetchLogs() {
            fetch(host + '/api/logs').then(res => res.json()).then(data => {
                const tbody = document.querySelector('#logTable tbody');
                tbody.innerHTML = '';
                document.getElementById('presentToday').innerText = data.length;
                data.forEach(l => {
                    tbody.innerHTML += `<tr><td>${l.id}</td><td>${l.name}</td><td>${l.time}</td></tr>`;
                });
            });
        }

        function deleteUser(name) {
            if(confirm('Are you sure?')) {
                fetch(host + '/api/delete_user?name=' + name).then(() => fetchUsers());
            }
        }

        function downloadLogs() {
            window.location.href = host + '/api/download_csv';
        }

        function clearLogs() {
            if(confirm('Are you sure you want to clear all logs?')) {
                fetch(host + '/api/clear_logs').then(() => {
                    alert('Logs cleared!');
                    fetchLogs();
                });
            }
        }

function openEnrollModal() {
            // 1. Show the Modal
            document.getElementById('enrollModal').style.display = 'block';
            
            // 2. Start the Camera Stream on the img tag
            document.getElementById('stream').src = host + ':81/stream'; 
            
            // 3. Tell ESP32: "Turn on Detection/Recognition (View Mode)"
            fetch(host + '/api/enroll_mode?active=1');
        }

        function closeEnrollModal() {
            // 1. Hide Modal
            document.getElementById('enrollModal').style.display = 'none';
            document.getElementById('stream').src = "";
            
            // 2. Tell ESP32: "Turn OFF Detection (Save Resources)"
            fetch(host + '/api/enroll_mode?active=0');
        }

        function captureAndEnroll() {
            const id = document.getElementById('newId').value;
            const name = document.getElementById('newName').value;
            const dept = document.getElementById('newDept').value;
            
            if(!id || !name) { alert("ID and Name required"); return; }

            // 3. Tell ESP32: "Actually Capture and Save now"
            // (Note: We use the new capture handler)
            fetch(`${host}/api/enroll_capture?id=${id}&name=${name}&dept=${dept}`)
                .then(res => res.text())
                .then(msg => {
                    alert(msg); // "Capturing..."
                    // Wait a second, then refresh user list
                    setTimeout(() => {
                        fetchUsers();
                        closeEnrollModal(); // Auto-close after capture
                    }, 2000);
                });
        }

        
        
        // Initial Load
        fetchUsers();
        fetchLogs();
    </script>
</body>
</html>
)rawliteral";

// function openEnrollModal() {
        //     document.getElementById('enrollModal').style.display = 'block';
        //     document.getElementById('stream').src = host + ':81/stream'; // Assumes stream on port 81
        // }

        // function captureAndEnroll() {
        //     const id = document.getElementById('newId').value;
        //     const name = document.getElementById('newName').value;
        //     const dept = document.getElementById('newDept').value;
            
        //     // Trigger enrollment command
        //     fetch(`${host}/api/enroll?id=${id}&name=${name}&dept=${dept}`).then(res => {
        //         alert('User Enrolled!');
        //         document.getElementById('enrollModal').style.display = 'none';
        //         fetchUsers();
        //     });
        // }