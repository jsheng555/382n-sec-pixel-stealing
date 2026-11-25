# Pixel stealing

Did this on linux server (Intel Xeon) with Firefox 27.0.1


## How to run it

**1. Download Firefox 27.0.1 on LRC & VNC viewer** :
```sh
wget https://ftp.mozilla.org/pub/firefox/releases/27.0.1/linux-x86_64/en-US/firefox-27.0.1.tar.bz2
tar xjf firefox-27.0.1.tar.bz2
```
I downloaded [TigerVNC Viewer](https://github.com/TigerVNC/tigervnc/releases): tigervnc-1.16.2.exe


**2. Make a profile**:
```sh
mkdir -p ~/ff27profile
cat > ~/ff27profile/user.js <<'EOF'
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("app.update.enabled", false);
EOF
```

**3. Start a virtual display + VNC** :
```sh
mkdir -p ~/.vnc && vncpasswd ~/.vnc/passwd   
Xvnc :1 -geometry 1024x768 -depth 24 -SecurityTypes VncAuth -PasswordFile ~/.vnc/passwd &
export DISPLAY=:1
```
Run `ps aux | grep Xvnc`, look for `Xvnc:N`, where N is the number used for DISPLAY 

From Windows: `ssh -L 5901:localhost:5901 user@host`, then connect VNC viewer to `localhost:5901`, where the port is 5900 + N (mine was 1 but i'm not sure if it's always that)

**4. Start the server and Firefox:**
```sh
python3 subnormal_stealing/serve.py &
./firefox/firefox -no-remote -profile ~/ff27profile http://127.0.0.1:8000
```

**5. In the Firefox window (via VNC):**
1. Click **"Calibration"**
2. Click **"Scan victim pixels"**

The victim page is on 8001, super small image on the top left corner

**6. Check how well it recovered:**
```sh
python3 subnormal_stealing/score.py
```

## Next steps
- Compare Area = 100 / 200 / 300
