import socket
import ssl
import json
import threading
import os
from datetime import datetime, timedelta
from glob import glob
import smtplib
import textwrap
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
import time
import pandas as pd
from flask import Flask, render_template, session, redirect, url_for, jsonify, send_file, request
from dotenv import load_dotenv

BLOCKED_IPS_FILE = "blocked_ips.json"
DATA_DIRECTORY = "received_data"
SESSION_TIMEOUT = 600

def log(msg):
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}][TID:{threading.get_ident()}] {msg}")

class SecureServer:
    def __init__(self, host="0.0.0.0", port=5001,
                 web_port=5008,
                 cert_file="/home/flintman/ssl/cert.pem",
                 key_file="/home/flintman/ssl/key.pem",
                 max_attempts=3):
        self.host = host
        self.port = port
        self.web_port = web_port
        load_dotenv()
        self.email_server = os.getenv("EMAIL_SERVER", "")
        self.email_address = os.getenv("EMAIL_ADDRESS", "")
        self.email_password = os.getenv("EMAIL_PASSWORD", "")
        self.cert_file = cert_file
        self.key_file = key_file
        self.server_socket = None
        self.failed_attempts = {}
        self.max_attempts = max_attempts
        self.blocked_ips = self.load_blocked_ips()
        self.active_alarms = {}
        self.trl_data = {}
        self.app = Flask(__name__)
        self.app.secret_key = os.urandom(24)
        self.pending_commands = {}

        if not os.path.exists(DATA_DIRECTORY):
            os.makedirs(DATA_DIRECTORY)

        self._init_web_routes()

    def load_blocked_ips(self):
        if os.path.exists(BLOCKED_IPS_FILE):
            with open(BLOCKED_IPS_FILE, "r") as f:
                return set(json.load(f))
        return set()

    def save_blocked_ips(self):
        with open(BLOCKED_IPS_FILE, "w") as f:
            json.dump(list(self.blocked_ips), f, indent=4)

    def load_data(self):
        self.trl_data.clear()
        for file in os.listdir(DATA_DIRECTORY):
            if file.endswith(".json"):
                parts = file.split("_")
                if len(parts) >= 2:
                    trl = parts[0]
                    file_path = os.path.join(DATA_DIRECTORY, file)
                    self._process_file(trl, file_path)

    def _process_file(self, trl, file_path):
        with open(file_path, "r") as f:
            data = json.load(f)
            if not isinstance(data, list):
                return
            if trl not in self.trl_data:
                self.trl_data[trl] = []
            processed_records = []
            for record in data:
                try:
                    processed_record = dict(record)
                    processed_record["timestamp"] = datetime.strptime(record["timestamp"], "%H:%M:%S  %m:%d:%Y")
                    processed_records.append(processed_record)
                except ValueError:
                    log(f"Skipping invalid timestamp in {file_path}: {record['timestamp']}")
            self.trl_data[trl].extend(processed_records)
            self.trl_data[trl] = sorted(self.trl_data[trl], key=lambda x: x["timestamp"])

    def cleanup_old_data(self, days=30):
        cutoff_date = datetime.now() - timedelta(days=days)
        try:
            files = glob(os.path.join(DATA_DIRECTORY, "*_*.json"))
            for filepath in files:
                if not os.path.exists(filepath):
                    continue
                filename = os.path.basename(filepath)
                try:
                    date_str = filename.split('_')[1].split('.')[0]
                    file_date = datetime.strptime(date_str, "%Y-%m-%d")
                    if file_date < cutoff_date:
                        try:
                            os.remove(filepath)
                            log(f"Deleted old file: {filename} (dated {date_str})")
                        except Exception as e:
                            log(f"Error deleting file {filename}: {e}")
                except (IndexError, ValueError) as e:
                    log(f"Skipping file with invalid name format: {filename} - {e}")
                    continue
        except Exception as e:
            log(f"Error during cleanup: {e}")

    def start(self):
        socket_thread = threading.Thread(target=self._start_socket_server, daemon=True)
        socket_thread.start()
        self.app.run(host=self.host, port=self.web_port, debug=False, use_reloader=False)

    def _start_socket_server(self):
        context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.set_ciphers('ECDHE+AESGCM:DHE+AESGCM:!aNULL:!eNULL:!MD5:!RC4')
        context.load_cert_chain(certfile=self.cert_file, keyfile=self.key_file)

        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen()
        log(f"Secure server listening on {self.host}:{self.port}...")

        try:
            while True:
                log("Waiting for new client connection...")
                try:
                    client_sock, addr = self.server_socket.accept()
                except Exception as e:
                    log(f"Error on accept: {e}")
                    continue
                ip_address = addr[0]
                log(f"Accepted connection from {addr}")

                if ip_address in self.blocked_ips:
                    log(f"Blocked connection attempt from {ip_address}.")
                    client_sock.close()
                    continue

                try:
                    log(f"Wrapping socket for {addr}")
                    secure_conn = context.wrap_socket(client_sock, server_side=True)
                    log(f"SSL handshake complete for {addr}")
                    threading.Thread(target=self.handle_client, args=(secure_conn, addr)).start()
                except (ssl.SSLError, ConnectionResetError) as e:
                    log(f"SSL error for {addr}: {e}")
                    self.failed_attempts[ip_address] = self.failed_attempts.get(ip_address, 0) + 1
                    if self.failed_attempts[ip_address] >= self.max_attempts:
                        self.blocked_ips.add(ip_address)
                        self.save_blocked_ips()
                        log(f"IP {ip_address} has been blocked after {self.max_attempts} failed attempts.")
                    client_sock.close()
        except KeyboardInterrupt:
            log("Server shutting down...")
        finally:
            self.server_socket.close()

    def handle_client(self, conn, addr):
        log(f"Handling client {addr}")
        try:
            conn.settimeout(30.0)
            while True:
                log(f"Waiting to receive data from {addr}")
                data = b""
                try:
                    while True:
                        chunk = conn.recv(1024)
                        log(f"Received chunk from {addr}: {chunk}")
                        if not chunk:
                            log(f"No more data from {addr}")
                            break
                        data += chunk
                        try:
                            if data.decode('utf-8', errors='ignore').endswith('}'):
                                log(f"End of JSON detected from {addr}")
                                break
                        except Exception as e:
                            log(f"Decode error from {addr}: {e}")
                            break
                except socket.timeout:
                    log(f"Connection with {addr} timed out. Closing.")
                    break
                except Exception as e:
                    log(f"Error receiving data from {addr}: {e}")
                    break

                if not data:
                    log(f"No data received from {addr}, closing connection.")
                    break

                try:
                    decoded_data = data.decode("utf-8", errors="ignore").strip()
                    received_array = json.loads(decoded_data)
                    if "alarm_codes" in received_array and isinstance(received_array["alarm_codes"], str):
                        received_array["alarm_codes"] = [
                            int(code) for code in received_array["alarm_codes"].split(",") if code.strip().isdigit()
                        ]
                    log(f"Received from {addr}: {received_array}")
                except (UnicodeDecodeError, json.JSONDecodeError) as e:
                    log(f"Malformed data received from {addr}: {e}. Closing connection.")
                    break

                if "trl" in received_array:
                    trl = received_array.get("trl")
                    self.append_data(received_array)
                    alarm_codes = set(received_array.get("alarm_codes", []))
                    response = {"status": "Received"}
                    if trl in self.pending_commands:
                        command = self.pending_commands[trl]
                        response["status"] = command
                        del self.pending_commands[trl]
                        log(f"Sending command {command} to TRL {trl}")
                    if alarm_codes:
                        if trl not in self.active_alarms or alarm_codes != self.active_alarms[trl]:
                            log(f"Sending email for TRL {trl} with alarms: {alarm_codes}")
                            self.send_email(received_array)
                            self.active_alarms[trl] = alarm_codes
                    elif trl in self.active_alarms:
                        log(f"TRL {trl} alarms cleared. Ready for next alert.")
                        del self.active_alarms[trl]
                    try:
                        conn.sendall(json.dumps(response).encode("utf-8"))
                        log(f"Sent to {addr}: {response}")
                    except Exception as e:
                        log(f"Error sending response to {addr}: {e}")
                    self.cleanup_old_data()
                    break
        except Exception as e:
            log(f"Unexpected error with client {addr}: {e}")
        finally:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except Exception as e:
                log(f"Error shutting down connection with {addr}: {e}")
            conn.close()
            log(f"Client {addr} disconnected.")
            if "trl" in locals():
                self.active_alarms.pop(trl, None)

    def append_data(self, data):
        trl_number = data.get("trl", "unknown")
        date_str = datetime.now().strftime("%Y-%m-%d")
        filepath = os.path.join(DATA_DIRECTORY, f"{trl_number}_{date_str}.json")
        log(f"Appending data for TRL {trl_number} to {filepath}")
        if os.path.exists(filepath):
            with open(filepath, "r") as f:
                existing_data = json.load(f)
            if isinstance(existing_data, list):
                existing_data.append(data)
            else:
                existing_data = [existing_data, data]
        else:
            existing_data = [data]
        with open(filepath, "w") as f:
            json.dump(existing_data, f, indent=4)
        log(f"Data appended to {filepath}")
        self.load_data()

    def send_email(self, data):
        try:
            if not isinstance(data, dict):
                log("Error: Data is not a dictionary")
                return
            alarm_codes = data.get("alarm_codes", [])
            if not alarm_codes:
                return
            trl = data.get("trl", "N/A")
            alarm_codes_str = ", ".join(map(str, alarm_codes))
            email_body = textwrap.dedent(f"""\
            **ALARM ALERT**
            Timestamp: {data.get("timestamp", "N/A")}
            TRL Number: {trl}
            Alarm Codes: {alarm_codes_str}

            System Status:
            - Setpoint: {data.get("setpoint", "N/A")}
            - Status: {data.get("status", "N/A")}
            - Return Temp: {data.get("return_temp", "N/A")}°F
            - Supply Temp: {data.get("supply_temp", "N/A")}°F
            - Coil Temp: {data.get("coil_temp", "N/A")}°F
            """)
            msg = MIMEMultipart()
            msg["From"] = self.email_address
            msg["To"] = self.email_address
            msg["Subject"] = f"ALARM: TRL {trl} Detected!"
            msg.attach(MIMEText(email_body, "plain"))
            with smtplib.SMTP_SSL(self.email_server, 465) as server:
                server.login(self.email_address, self.email_password)
                server.sendmail(self.email_address, self.email_address, msg.as_string())
            log(f"Email sent to {self.email_address} with TRL {trl} and Alarm Codes: {alarm_codes_str}")
        except Exception as e:
            log(f"Failed to send email: {e}")

    def _init_web_routes(self):
        self.app.before_request(self._check_session_timeout)
        self.app.route("/", methods=["GET"])(self._web_index)
        self.app.route("/trl/<trl>", methods=["GET"])(self._web_view_trl)
        self.app.route("/download/<trl>", methods=["GET"])(self._web_download_trl)
        self.app.route("/command/<trl>", methods=["POST"])(self._web_send_command)

    def _web_send_command(self, trl):
        if not request.json or "command" not in request.json:
            return jsonify({"status": "error", "message": "Invalid command"}), 400
        command = request.json["command"]
        self.pending_commands[trl] = command
        return jsonify({"status": "success", "message": f"Command {command} queued for {trl}"})

    def _check_session_timeout(self):
        if "logged_in" in session:
            last_activity = session.get("last_activity", time.time())
            current_time = time.time()
            if current_time - last_activity > SESSION_TIMEOUT:
                session.pop("logged_in", None)
                session.pop("last_activity", None)
                return redirect(url_for("_web_index"))
            session["last_activity"] = current_time

    def get_trl_list(self):
        return sorted(self.trl_data.keys())

    def _web_index(self):
        self.load_data()
        return render_template("index.html", trl_list=self.get_trl_list(), trl_data=self.trl_data)

    def _web_view_trl(self, trl):
        return jsonify(self.trl_data.get(trl, []))

    def _web_download_trl(self, trl):
        data = self.trl_data.get(trl, [])
        if not data:
            return "No data available", 404
        df = pd.DataFrame(data)
        df["timestamp"] = df["timestamp"].astype(str)
        download_folder = "downloaded_data"
        os.makedirs(download_folder, exist_ok=True)
        timestamp = datetime.now().strftime("%m-%d-%Y-%H-%M-%S")
        excel_filename = f"trl_{trl}_data_{timestamp}.xlsx"
        excel_path = os.path.join(download_folder, excel_filename)
        df.to_excel(excel_path, index=False)
        return send_file(excel_path, as_attachment=True, mimetype="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")

if __name__ == "__main__":
    server = SecureServer()
    server.start()