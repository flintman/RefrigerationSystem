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

BLOCKED_IPS_FILE = "blocked_ips.json"
DATA_DIRECTORY = "received_data"  # Directory to store received data
SESSION_TIMEOUT = 600  # 10 minutes for web interface

class SecureServer:
    def __init__(self, host="0.0.0.0", port=5001,
                 web_port=5008,
                 cert_file="/home/flintman/ssl/cert.pem",
                 key_file="/home/flintman/ssl/key.pem",
                 max_attempts=3):
        self.host = host
        self.port = port
        self.web_port = web_port
        self.email_server = "mail.bellavance.co"
        self.email_address = "william@bellavance.co"
        self.email_password = ""
        self.cert_file = cert_file
        self.key_file = key_file
        self.server_socket = None
        self.failed_attempts = {}  # Tracks failed SSL attempts per IP
        self.max_attempts = max_attempts
        self.blocked_ips = self.load_blocked_ips()  # Persistent blocking
        self.active_alarms = {}  # {trl_number: set(alarm_codes)}
        self.trl_data = {}  # For storing and managing TRL data
        self.app = Flask(__name__)
        self.app.secret_key = os.urandom(24)
        self.pending_commands = {}

        if not os.path.exists(DATA_DIRECTORY):
            os.makedirs(DATA_DIRECTORY)  # Ensure the directory exists

        # Initialize web routes
        self._init_web_routes()

    def load_blocked_ips(self):
        """Loads blocked IPs from file."""
        if os.path.exists(BLOCKED_IPS_FILE):
            with open(BLOCKED_IPS_FILE, "r") as f:
                return set(json.load(f))
        return set()

    def save_blocked_ips(self):
        """Saves blocked IPs to file."""
        with open(BLOCKED_IPS_FILE, "w") as f:
            json.dump(list(self.blocked_ips), f, indent=4)

    def load_data(self):
        """Loads all TRL data from files."""
        # Clear existing data first
        self.trl_data.clear()

        for file in os.listdir(DATA_DIRECTORY):
            if file.endswith(".json"):
                parts = file.split("_")
                if len(parts) >= 2:
                    trl = parts[0]
                    file_path = os.path.join(DATA_DIRECTORY, file)
                    self._process_file(trl, file_path)

    def _process_file(self, trl, file_path):
        """Processes a single TRL data file."""
        with open(file_path, "r") as f:
            data = json.load(f)
            if not isinstance(data, list):
                return

            if trl not in self.trl_data:
                self.trl_data[trl] = []

            # Create a new list for the processed records
            processed_records = []

            for record in data:
                try:
                    # Create a new dictionary for each record to avoid modifying the original
                    processed_record = dict(record)
                    processed_record["timestamp"] = datetime.strptime(record["timestamp"], "%H:%M:%S  %m:%d:%Y")
                    processed_records.append(processed_record)
                except ValueError:
                    print(f"Skipping invalid timestamp in {file_path}: {record['timestamp']}")

            # Extend the trl_data with the new processed records
            self.trl_data[trl].extend(processed_records)

            # Sort using a copy of the list to avoid modification during sorting
            self.trl_data[trl] = sorted(self.trl_data[trl], key=lambda x: x["timestamp"])

    def cleanup_old_data(self, days=30):
        """Deletes data files older than specified number of days."""
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
                            print(f"Deleted old file: {filename} (dated {date_str})")
                        except Exception as e:
                            print(f"Error deleting file {filename}: {e}")
                except (IndexError, ValueError) as e:
                    print(f"Skipping file with invalid name format: {filename} - {e}")
                    continue

        except Exception as e:
            print(f"Error during cleanup: {e}")

    def start(self):
        """Starts both servers in separate threads."""
        # Start socket server
        socket_thread = threading.Thread(target=self._start_socket_server, daemon=True)
        socket_thread.start()

        # Start web server (without debug mode)
        self.app.run(host=self.host, port=self.web_port, debug=False, use_reloader=False)

    def _start_socket_server(self):
        """Starts the secure socket server."""
        context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        context.minimum_version = ssl.TLSVersion.TLSv1_2  # Enforce TLS 1.2+
        context.set_ciphers('ECDHE+AESGCM:DHE+AESGCM:!aNULL:!eNULL:!MD5:!RC4')

        context.load_cert_chain(certfile=self.cert_file, keyfile=self.key_file)

        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen()
        print(f"Secure server listening on {self.host}:{self.port}...")

        try:
            while True:
                client_sock, addr = self.server_socket.accept()
                ip_address = addr[0]

                if ip_address in self.blocked_ips:
                    print(f"Blocked connection attempt from {ip_address}.")
                    client_sock.close()
                    continue  # Ignore blocked IPs

                try:
                    secure_conn = context.wrap_socket(client_sock, server_side=True)
                    print(f"Client {addr} connected successfully.")
                    threading.Thread(target=self.handle_client, args=(secure_conn, addr)).start()
                except (ssl.SSLError, ConnectionResetError):
                    print(f"Unauthorized connection attempt from {ip_address}.")
                    self.failed_attempts[ip_address] = self.failed_attempts.get(ip_address, 0) + 1

                    if self.failed_attempts[ip_address] >= self.max_attempts:
                        self.blocked_ips.add(ip_address)
                        self.save_blocked_ips()  # Save new blocked IP
                        print(f"IP {ip_address} has been blocked after {self.max_attempts} failed attempts.")

                    client_sock.close()

        except KeyboardInterrupt:
            print("\nServer shutting down...")
        finally:
            self.server_socket.close()

    def handle_client(self, conn, addr):
        """Handles communication with a single client."""
        try:
            conn.settimeout(30.0)  # Set a reasonable timeout (30 seconds)

            while True:
                try:
                    # Receive data with buffer and timeout
                    data = b""
                    while True:
                        chunk = conn.recv(1024)
                        if not chunk:  # Client disconnected
                            break
                        data += chunk
                        try:
                            if data.decode('utf-8', errors='ignore').endswith('}'):
                                break
                        except:
                            break

                    if not data:
                        break  # Client disconnected

                    try:
                        decoded_data = data.decode("utf-8", errors="ignore").strip()
                        received_array = json.loads(decoded_data)
                        if "alarm_codes" in received_array and isinstance(received_array["alarm_codes"], str):
                            received_array["alarm_codes"] = [
                            int(code) for code in received_array["alarm_codes"].split(",") if code.strip().isdigit()
                            ]
                        print(f"Received from {addr}: {received_array}")
                    except (UnicodeDecodeError, json.JSONDecodeError) as e:
                        print(f"Malformed data received from {addr}: {e}. Closing connection.")
                        break

                    # Process TRL data
                    if "trl" in received_array:
                        trl = received_array.get("trl")
                        self.append_data(received_array)
                        alarm_codes = set(received_array.get("alarm_codes", []))

                        # Check for pending commands for this trailer
                        response = {"status": "Received"}
                        if trl in self.pending_commands:
                            command = self.pending_commands[trl]
                            response["status"] = command
                            del self.pending_commands[trl]  # Remove after sending
                            print(f"Sending command {command} to TRL {trl}")

                        # Alarm handling logic
                        if alarm_codes:
                            if trl not in self.active_alarms or alarm_codes != self.active_alarms[trl]:
                                print(f"Sending email for TRL {trl} with alarms: {alarm_codes}")
                                self.send_email(received_array)
                                self.active_alarms[trl] = alarm_codes
                        elif trl in self.active_alarms:
                            print(f"TRL {trl} alarms cleared. Ready for next alert.")
                            del self.active_alarms[trl]

                        # Send response
                        conn.sendall(json.dumps(response).encode("utf-8"))
                        print(f"Sent to {addr}: {response}")
                        self.cleanup_old_data()
                except socket.timeout:
                    print(f"Connection with {addr} timed out. Closing.")
                    break
                except Exception as e:
                    print(f"Error with client {addr}: {e}")
                    break

        except Exception as e:
            print(f"Unexpected error with client {addr}: {e}")
        finally:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except:
                pass
            conn.close()
            print(f"Client {addr} disconnected.")
            # Clean up any active alarms if needed
            if "trl" in locals():
                self.active_alarms.pop(trl, None)

    def append_data(self, data):
        """Appends received data to a file labeled with TrlNumber and Date."""
        trl_number = data.get("trl", "unknown")
        date_str = datetime.now().strftime("%Y-%m-%d")
        filepath = os.path.join(DATA_DIRECTORY, f"{trl_number}_{date_str}.json")

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

        print(f"Data appended to {filepath}")
        self.load_data()  # Refresh in-memory data after appending new data

    def send_email(self, data):
        """Sends an email if alarm codes are present."""
        try:
            if not isinstance(data, dict):
                print("Error: Data is not a dictionary")
                return

            alarm_codes = data.get("alarm_codes", [])
            if not alarm_codes:
                return  # Skip sending email if no alarm codes

            trl = data.get("trl", "N/A")
            alarm_codes_str = ", ".join(map(str, alarm_codes))

            # Format email content
            email_body = textwrap.dedent(f"""\
            **ALARM ALERT**
            Timestamp: {data.get("timestamp", "N/A")}
            TRL Number: {trl}
            Alarm Codes: {alarm_codes_str}

            System Status:
            - Setpoint: {data.get("setpoint", "N/A")}
            - Status: {data.get("status", "N/A")}
            - Compressor: {"ON" if data.get("compressor") else "OFF"}
            - Fan: {"ON" if data.get("fan") else "OFF"}
            - Valve: {"ON" if data.get("valve") else "OFF"}
            - Electric Heater: {"ON" if data.get("electric_heater") else "OFF"}
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

            print(f"Email sent to {self.email_address} with TRL {trl} and Alarm Codes: {alarm_codes_str}")

        except Exception as e:
            print(f"Failed to send email: {e}")

    def _init_web_routes(self):
        """Initializes all web interface routes."""
        self.app.before_request(self._check_session_timeout)
        self.app.route("/", methods=["GET"])(self._web_index)
        self.app.route("/trl/<trl>", methods=["GET"])(self._web_view_trl)
        self.app.route("/download/<trl>", methods=["GET"])(self._web_download_trl)
        self.app.route("/command/<trl>", methods=["POST"])(self._web_send_command)

    def _web_send_command(self, trl):
        """Handles sending commands to trailers."""
        if not request.json or "command" not in request.json:
            return jsonify({"status": "error", "message": "Invalid command"}), 400

        command = request.json["command"]
        # Store the command to be sent when the trailer connects
        # You might want to persist this in a file or database in a real application
        self.pending_commands[trl] = command
        return jsonify({"status": "success", "message": f"Command {command} queued for {trl}"})

    def _check_session_timeout(self):
        """Checks if the web session has timed out."""
        if "logged_in" in session:
            last_activity = session.get("last_activity", time.time())
            current_time = time.time()

            if current_time - last_activity > SESSION_TIMEOUT:
                session.pop("logged_in", None)
                session.pop("last_activity", None)
                return redirect(url_for("_web_index"))

            session["last_activity"] = current_time

    def get_trl_list(self):
        """Returns sorted list of TRL numbers."""
        return sorted(self.trl_data.keys())

    def _web_index(self):
        """Handles the web interface index page."""
        self.load_data()
        return render_template("index.html", trl_list=self.get_trl_list(), trl_data=self.trl_data)

    def _web_view_trl(self, trl):
        """Returns JSON data for a specific TRL."""
        return jsonify(self.trl_data.get(trl, []))

    def _web_download_trl(self, trl):
        """Generates and downloads Excel file for a TRL."""
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