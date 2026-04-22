import asyncio
import tkinter as tk
from tkinter import ttk, messagebox
from bleak import BleakScanner, BleakClient
import struct
import threading
from datetime import datetime
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

WRITE_SERVICE_UUID = "0000fff3-0000-1000-8000-00805f9b34fb"
RECEIVE_CHAR_UUID  = "0000fff2-0000-1000-8000-00805f9b34fb"
NOTIFY_CHAR_UUID   = "0000fff1-0000-1000-8000-00805f9b34fb"


def normalize_mac(mac: str) -> str:
    mac = mac.strip().lower().replace(":", "").replace("-", "")
    if len(mac) != 12:
        raise ValueError("MAC must contain 12 hex digits")
    int(mac, 16)
    return mac


class BLEToolGUI:
    def decode_sniffer_struct(self, data: bytes):
        MIN_SIZE = 30 + 1 + 1 + 4 + 4

        if len(data) < MIN_SIZE:
            return f"Packet too short ({len(data)} bytes): {data.hex()}"

        mac_bytes = data[0:30]
        curr_rssi = struct.unpack_from("<b", data, 30)[0]
        rssi      = struct.unpack_from("<b", data, 31)[0]
        timestamp = struct.unpack_from("<I", data, 32)[0]
        distance  = struct.unpack_from("<f", data, 36)[0]

        mac_str = mac_bytes.decode("utf-8", errors="ignore").rstrip("\x00")

        result = (
            f"MAC: {mac_str}\n"
            f"RSSI(curr): {curr_rssi}\n"
            f"RSSI(avg): {rssi}\n"
            f"Timestamp: {timestamp}\n"
            f"Distance: {distance:.2f}"
        )

        if len(data) >= 80:
            uuid_bytes = data[40:80]
            uuid_str = uuid_bytes.decode("utf-8", errors="ignore").rstrip("\x00")
            if uuid_str:
                result += f"\nUUID: {uuid_str}"

        return result

    def __init__(self, master):
        self.master = master
        self.master.title("BLE MAC Sender Tool with Battery Life Test")

        self.device_list = []
        self.selected_device = None
        self.client = None
        self.notifications_enabled = False

        # Battery life test variables
        self.battery_test_active = False
        self.battery_test_start_time = None
        self.last_successful_check = None
        self.connection_lost_time = None
        self.check_interval = 120  # 2 minutes default
        self.instability_threshold = 30  # 30 seconds default
        
        # Email settings
        self.alert_email = "rlar0203@gmail.com"
        self.sender_email = ""
        self.sender_password = ""
        self.smtp_server = "smtp.gmail.com"
        self.smtp_port = 587

        # Async loop
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()

        # ---------------------------------------------------------
        # DEVICE PANEL
        # ---------------------------------------------------------
        device_frame = ttk.LabelFrame(master, text="BLE Device")
        device_frame.pack(padx=10, pady=10, fill="x")

        self.scan_button = ttk.Button(device_frame, text="Scan Devices", command=self.start_scan)
        self.scan_button.grid(row=0, column=0, pady=5)

        self.device_combo = ttk.Combobox(device_frame, state="readonly", width=45)
        self.device_combo.grid(row=0, column=1, padx=5)

        self.connect_button = ttk.Button(device_frame, text="Connect", command=self.connect_device)
        self.connect_button.grid(row=0, column=2, padx=5)

        self.disconnect_button = ttk.Button(device_frame, text="Disconnect",
                                            command=self.disconnect_device, state="disabled")
        self.disconnect_button.grid(row=0, column=3, padx=5)

        # ---------------------------------------------------------
        # BATTERY LIFE TEST PANEL
        # ---------------------------------------------------------
        battery_frame = ttk.LabelFrame(master, text="Battery Life Test")
        battery_frame.pack(padx=10, pady=10, fill="x")

        ttk.Label(battery_frame, text="Check Interval (seconds):").grid(row=0, column=0, padx=5, pady=5)
        self.interval_entry = ttk.Entry(battery_frame, width=10)
        self.interval_entry.insert(0, "120")
        self.interval_entry.grid(row=0, column=1, padx=5)

        ttk.Label(battery_frame, text="Instability Threshold (seconds):").grid(row=0, column=2, padx=5)
        self.threshold_entry = ttk.Entry(battery_frame, width=10)
        self.threshold_entry.insert(0, "30")
        self.threshold_entry.grid(row=0, column=3, padx=5)

        # Email configuration
        ttk.Label(battery_frame, text="Gmail Address:").grid(row=1, column=0, padx=5, pady=5)
        self.email_entry = ttk.Entry(battery_frame, width=30)
        self.email_entry.grid(row=1, column=1, columnspan=2, padx=5)

        ttk.Label(battery_frame, text="App Password:").grid(row=1, column=3, padx=5)
        self.password_entry = ttk.Entry(battery_frame, width=20, show="*")
        self.password_entry.grid(row=1, column=4, padx=5)

        help_btn = ttk.Button(battery_frame, text="?", width=3, command=self.show_email_help)
        help_btn.grid(row=1, column=5, padx=2)

        self.start_test_btn = ttk.Button(battery_frame, text="Start Battery Test", 
                                         command=self.start_battery_test, state="disabled")
        self.start_test_btn.grid(row=2, column=0, columnspan=2, padx=5, pady=5)

        self.stop_test_btn = ttk.Button(battery_frame, text="Stop Battery Test", 
                                        command=self.stop_battery_test, state="disabled")
        self.stop_test_btn.grid(row=2, column=2, columnspan=2, padx=5, pady=5)

        self.test_status_label = ttk.Label(battery_frame, text="Test Status: Not Running", 
                                          foreground="gray")
        self.test_status_label.grid(row=3, column=0, columnspan=6, pady=5)

        self.test_duration_label = ttk.Label(battery_frame, text="Duration: 0h 0m 0s")
        self.test_duration_label.grid(row=4, column=0, columnspan=6, pady=5)

        # ---------------------------------------------------------
        # NOTIFICATION CONTROL
        # ---------------------------------------------------------
        notif_frame = ttk.LabelFrame(master, text="Notification Control")
        notif_frame.pack(padx=10, pady=10, fill="x")

        self.sub_btn = ttk.Button(notif_frame, text="Subscribe", command=self.manual_subscribe, state="disabled")
        self.sub_btn.grid(row=0, column=0, padx=5)

        self.unsub_btn = ttk.Button(notif_frame, text="Unsubscribe", command=self.manual_unsubscribe, state="disabled")
        self.unsub_btn.grid(row=0, column=1, padx=5)

        self.read_btn = ttk.Button(notif_frame, text="Read Now", command=self.manual_read_notify, state="disabled")
        self.read_btn.grid(row=0, column=2, padx=5)

        # ---------------------------------------------------------
        # MAC ENTRY PANEL
        # ---------------------------------------------------------
        mac_frame = ttk.LabelFrame(master, text="MAC Address Sender")
        mac_frame.pack(padx=10, pady=10, fill="x")

        self.mac_entry = ttk.Entry(mac_frame, width=30)
        self.mac_entry.grid(row=0, column=0, padx=5, pady=5)

        add_btn = ttk.Button(mac_frame, text="Add MAC", command=self.add_mac)
        add_btn.grid(row=0, column=1, padx=5)

        del_btn = ttk.Button(mac_frame, text="Remove Selected", command=self.remove_selected_mac)
        del_btn.grid(row=0, column=2, padx=5)

        self.mac_listbox = tk.Listbox(mac_frame, height=6, width=50)
        self.mac_listbox.grid(row=1, column=0, columnspan=3, padx=5, pady=5)

        send_btn = ttk.Button(mac_frame, text="Send MAC List", command=self.send_mac_list)
        send_btn.grid(row=2, column=0, columnspan=3, pady=5)

        self.mac_list = []

        # ---------------------------------------------------------
        # LOG PANEL
        # ---------------------------------------------------------
        text_frame = ttk.LabelFrame(master, text="Log Output")
        text_frame.pack(padx=10, pady=10)

        self.log_text = tk.Text(text_frame, width=90, height=20)
        self.log_text.pack()

        self.packet_count = 0
        self.stats_label = ttk.Label(master, text="Packets received: 0")
        self.stats_label.pack(pady=5)

    # =============================================================
    # EMAIL ALERT
    # =============================================================
    def show_email_help(self):
        help_text = """Email Alert Setup Instructions:

1. Use a Gmail account for sending alerts
2. Enable 2-Factor Authentication on your Gmail account
3. Generate an App Password:
   - Go to Google Account settings
   - Security → 2-Step Verification → App passwords
   - Create a new app password for "Mail"
   - Copy the 16-character password (no spaces)
4. Enter your Gmail address and the App Password

Note: The alert will be sent to rlar0203@gmail.com
when the device battery dies."""
        
        messagebox.showinfo("Email Alert Setup", help_text)

    def send_email_alert(self, device_address, total_duration, disconnect_time):
        """Send email alert when device battery dies"""
        sender = self.sender_email.strip()
        password = self.sender_password.strip()
        
        if not sender or not password:
            self.log("⚠️ Email not configured - skipping email alert")
            return
        
        try:
            # Create message
            msg = MIMEMultipart()
            msg['From'] = sender
            msg['To'] = self.alert_email
            msg['Subject'] = f"🔴 BLE Device Battery Died - {device_address}"
            
            body = f"""
BLE Battery Life Test Alert

DEVICE DISCONNECTED - BATTERY LIKELY DEAD

Device Address: {device_address}
Disconnection Time: {disconnect_time}
Total Battery Life: {total_duration}

This is an automated alert from the BLE Battery Life Testing Tool.
The device has been disconnected for longer than the configured instability threshold,
indicating that the battery has likely died.

Please review the camera footage and logs for more details.
"""
            
            msg.attach(MIMEText(body, 'plain'))
            
            # Send email
            self.log("📧 Sending email alert...")
            server = smtplib.SMTP(self.smtp_server, self.smtp_port)
            server.starttls()
            server.login(sender, password)
            server.send_message(msg)
            server.quit()
            
            self.log(f"✅ Email alert sent successfully to {self.alert_email}")
            
        except smtplib.SMTPAuthenticationError:
            self.log("❌ Email authentication failed - check your Gmail address and App Password")
        except Exception as e:
            self.log(f"❌ Failed to send email: {e}")

    # =============================================================
    # LOGGING
    # =============================================================
    def log(self, msg):
        self.master.after(0, self._log, msg)

    def _log(self, msg):
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {msg}\n")
        self.log_text.see(tk.END)

    # =============================================================
    # BATTERY LIFE TEST
    # =============================================================
    def start_battery_test(self):
        try:
            self.check_interval = int(self.interval_entry.get())
            self.instability_threshold = int(self.threshold_entry.get())
        except ValueError:
            messagebox.showerror("Invalid Input", "Please enter valid numbers for interval and threshold.")
            return

        if self.check_interval < 10:
            messagebox.showwarning("Invalid Interval", "Check interval should be at least 10 seconds.")
            return

        # Store email credentials
        self.sender_email = self.email_entry.get().strip()
        self.sender_password = self.password_entry.get().strip()
        
        if not self.sender_email or not self.sender_password:
            response = messagebox.askyesno("Email Not Configured", 
                                          "Email alert is not configured. Continue without email notifications?")
            if not response:
                return

        self.battery_test_active = True
        self.battery_test_start_time = datetime.now()
        self.last_successful_check = datetime.now()
        self.connection_lost_time = None

        self.start_test_btn.config(state="disabled")
        self.stop_test_btn.config(state="normal")
        self.interval_entry.config(state="disabled")
        self.threshold_entry.config(state="disabled")
        self.email_entry.config(state="disabled")
        self.password_entry.config(state="disabled")

        self.test_status_label.config(text="Test Status: RUNNING", foreground="green")
        
        self.log("=" * 60)
        self.log(f"BATTERY LIFE TEST STARTED")
        self.log(f"Check Interval: {self.check_interval}s")
        self.log(f"Instability Threshold: {self.instability_threshold}s")
        if self.sender_email:
            self.log(f"Email alerts: ENABLED (to {self.alert_email})")
        else:
            self.log(f"Email alerts: DISABLED")
        self.log(f"Start Time: {self.battery_test_start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log("=" * 60)

        asyncio.run_coroutine_threadsafe(self.battery_test_loop(), self.loop)
        self.update_test_duration()

    def stop_battery_test(self):
        self.battery_test_active = False
        
        if self.battery_test_start_time:
            duration = datetime.now() - self.battery_test_start_time
            self.log("=" * 60)
            self.log(f"BATTERY LIFE TEST STOPPED")
            self.log(f"Total Duration: {self.format_duration(duration)}")
            self.log("=" * 60)

        self.start_test_btn.config(state="normal" if self.client and self.client.is_connected else "disabled")
        self.stop_test_btn.config(state="disabled")
        self.interval_entry.config(state="normal")
        self.threshold_entry.config(state="normal")
        self.email_entry.config(state="normal")
        self.password_entry.config(state="normal")
        self.test_status_label.config(text="Test Status: Stopped", foreground="gray")

    async def battery_test_loop(self):
        while self.battery_test_active:
            if not self.client or not self.client.is_connected:
                now = datetime.now()
                
                if self.connection_lost_time is None:
                    self.connection_lost_time = now
                    self.log("⚠️ CONNECTION LOST - Monitoring for instability threshold...")
                
                time_since_lost = (now - self.connection_lost_time).total_seconds()
                
                if time_since_lost >= self.instability_threshold:
                    total_duration = now - self.battery_test_start_time
                    disconnect_time_str = now.strftime('%Y-%m-%d %H:%M:%S')
                    duration_str = self.format_duration(total_duration)
                    
                    self.log("=" * 60)
                    self.log("🔴 DEVICE DISCONNECTED - BATTERY LIKELY DEAD")
                    self.log(f"Device: {self.selected_device.address}")
                    self.log(f"Disconnection Time: {disconnect_time_str}")
                    self.log(f"Total Battery Life: {duration_str}")
                    self.log(f"Instability exceeded {self.instability_threshold}s threshold")
                    self.log("=" * 60)
                    
                    # Send email alert
                    threading.Thread(
                        target=self.send_email_alert,
                        args=(self.selected_device.address, duration_str, disconnect_time_str),
                        daemon=True
                    ).start()
                    
                    self.master.after(0, self.test_status_label.config, 
                                    {'text': 'Test Status: DEVICE DEAD', 'foreground': 'red'})
                    self.battery_test_active = False
                    self.master.after(0, self.stop_test_btn.config, {'state': 'disabled'})
                    break
                else:
                    remaining = self.instability_threshold - time_since_lost
                    self.log(f"Connection unstable for {int(time_since_lost)}s (threshold: {remaining:.0f}s remaining)")
            else:
                # Connection is active
                if self.connection_lost_time is not None:
                    self.log("✓ Connection restored")
                    self.connection_lost_time = None
                
                try:
                    # Perform a simple read to verify connection
                    is_connected = self.client.is_connected
                    
                    if is_connected:
                        self.last_successful_check = datetime.now()
                        time_since_start = self.last_successful_check - self.battery_test_start_time
                        self.log(f"✓ Connection check OK | Runtime: {self.format_duration(time_since_start)}")
                    else:
                        self.log("⚠️ Connection check FAILED - device appears disconnected")
                        
                except Exception as e:
                    self.log(f"⚠️ Connection check error: {e}")

            await asyncio.sleep(self.check_interval)

    def format_duration(self, delta):
        total_seconds = int(delta.total_seconds())
        hours = total_seconds // 3600
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60
        return f"{hours}h {minutes}m {seconds}s"

    def update_test_duration(self):
        if self.battery_test_active and self.battery_test_start_time:
            duration = datetime.now() - self.battery_test_start_time
            self.test_duration_label.config(text=f"Duration: {self.format_duration(duration)}")
            self.master.after(1000, self.update_test_duration)

    # =============================================================
    # SCANNING
    # =============================================================
    def start_scan(self):
        asyncio.run_coroutine_threadsafe(self.scan_devices(), self.loop)

    async def scan_devices(self):
        self.log("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=5)
        self.device_list = devices

        labels = [f"{d.name or 'Unknown'} ({d.address})" for d in devices]
        self.master.after(0, self.device_combo.config, {'values': labels})

        self.log(f"Found {len(devices)} devices")

    # =============================================================
    # CONNECTION
    # =============================================================
    def connect_device(self):
        sel = self.device_combo.current()
        if sel == -1:
            messagebox.showwarning("Select Device", "Please select a device first.")
            return

        self.selected_device = self.device_list[sel]
        asyncio.run_coroutine_threadsafe(self.connect_and_ready(), self.loop)

    async def connect_and_ready(self):
        try:
            self.log(f"Connecting to {self.selected_device.address}...")
            self.client = BleakClient(self.selected_device.address)
            await self.client.connect()

            self.log("Connected")

            self.master.after(0, self.connect_button.config, {'state': 'disabled'})
            self.master.after(0, self.disconnect_button.config, {'state': 'normal'})
            self.master.after(0, self.sub_btn.config, {'state': 'normal'})
            self.master.after(0, self.unsub_btn.config, {'state': 'disabled'})
            self.master.after(0, self.read_btn.config, {'state': 'normal'})
            self.master.after(0, self.start_test_btn.config, {'state': 'normal'})

        except Exception as e:
            self.log(f"Connection error: {e}")
            self.master.after(0, self.connect_button.config, {'state': 'normal'})

    def disconnect_device(self):
        if self.battery_test_active:
            messagebox.showwarning("Test Running", "Please stop the battery test before disconnecting.")
            return
        asyncio.run_coroutine_threadsafe(self.disconnect_and_cleanup(), self.loop)

    async def disconnect_and_cleanup(self):
        try:
            if self.notifications_enabled:
                try:
                    await self.client.stop_notify(NOTIFY_CHAR_UUID)
                except:
                    pass

            if self.client and self.client.is_connected:
                await self.client.disconnect()

            self.notifications_enabled = False

            self.master.after(0, self.sub_btn.config, {'state': 'disabled'})
            self.master.after(0, self.unsub_btn.config, {'state': 'disabled'})
            self.master.after(0, self.read_btn.config, {'state': 'disabled'})
            self.master.after(0, self.start_test_btn.config, {'state': 'disabled'})
            self.master.after(0, self.disconnect_button.config, {'state': 'disabled'})
            self.master.after(0, self.connect_button.config, {'state': 'normal'})

            self.log("Disconnected")

        except Exception as e:
            self.log(f"Disconnect error: {e}")

    # =============================================================
    # MANUAL SUBSCRIBE
    # =============================================================
    def manual_subscribe(self):
        asyncio.run_coroutine_threadsafe(self._async_subscribe(), self.loop)

    async def _async_subscribe(self):
        try:
            await self.client.start_notify(NOTIFY_CHAR_UUID, self.notification_handler)
            self.notifications_enabled = True
            self.log("Notifications ENABLED")

            self.master.after(0, self.sub_btn.config, {'state': 'disabled'})
            self.master.after(0, self.unsub_btn.config, {'state': 'normal'})

        except Exception as e:
            self.log(f"Subscribe error: {e}")

    # =============================================================
    # MANUAL UNSUBSCRIBE
    # =============================================================
    def manual_unsubscribe(self):
        asyncio.run_coroutine_threadsafe(self._async_unsubscribe(), self.loop)

    async def _async_unsubscribe(self):
        try:
            await self.client.stop_notify(NOTIFY_CHAR_UUID)
            self.notifications_enabled = False
            self.log("Notifications DISABLED")

            self.master.after(0, self.sub_btn.config, {'state': 'normal'})
            self.master.after(0, self.unsub_btn.config, {'state': 'disabled'})

        except Exception as e:
            self.log(f"Unsubscribe error: {e}")

    # =============================================================
    # MANUAL READ OF NOTIFY CHAR UUID
    # =============================================================
    def manual_read_notify(self):
        asyncio.run_coroutine_threadsafe(self._async_read_notify(), self.loop)

    async def _async_read_notify(self):
        try:
            data = await self.client.read_gatt_char(NOTIFY_CHAR_UUID)

            raw_hex = data.hex()
            try:
                decoded = data.decode("utf-8")
            except:
                decoded = "<non-utf8>"

            self.log("\n[READ RESULT]")
            self.log(f"UUID {NOTIFY_CHAR_UUID}")
            self.log(f"Raw ({len(data)} bytes): {raw_hex}")
            self.log(f"Text: {decoded}")

        except Exception as e:
            self.log(f"Read error: {e}")

    # =============================================================
    # NOTIFICATION HANDLER
    # =============================================================
    def notification_handler(self, sender, data):
        self.packet_count += 1
        self.master.after(0, self.stats_label.config,
                          {'text': f"Packets received: {self.packet_count}"})

        # Try decode as text
        try:
            txt = data.decode("utf-8")
        except:
            txt = None

        if txt:
            self.log(f"[Notification Text] {txt}")
        else:
            self.log(f"[Notification Bytes] {data.hex()}")

        decoded = self.decode_sniffer_struct(data)

        self.log(f"\n-- Packet {self.packet_count} ---")
        self.log(f"Raw({len(data)} bytes): {data.hex()}")
        self.log("Decoded:")
        self.log(decoded)

    # =============================================================
    # MAC ENTRY
    # =============================================================
    def add_mac(self):
        try:
            mac_hex = normalize_mac(self.mac_entry.get())
            self.mac_list.append(mac_hex)
            self.mac_listbox.insert(tk.END, mac_hex)
            self.mac_entry.delete(0, tk.END)
        except:
            messagebox.showerror("Invalid MAC", "Use AA:BB:CC:DD:EE:FF")

    def remove_selected_mac(self):
        sel = self.mac_listbox.curselection()
        if not sel:
            return
        idx = sel[0]
        self.mac_listbox.delete(idx)
        self.mac_list.pop(idx)

    # =============================================================
    # SEND MAC LIST
    # =============================================================
    def send_mac_list(self):
        if not self.client or not self.client.is_connected:
            messagebox.showerror("Not Connected", "Connect first.")
            return

        if not self.mac_list:
            messagebox.showwarning("Empty List", "No MAC addresses added.")
            return

        asyncio.run_coroutine_threadsafe(self._async_send_mac_list(), self.loop)

    async def _async_send_mac_list(self):
        try:
            combined_hex = "".join(self.mac_list)
            payload = bytes.fromhex(combined_hex)

            CHUNK = 253

            self.log(f"Sending {len(payload)} bytes in chunks of {CHUNK}")

            for i in range(0, len(payload), CHUNK):
                chunk = payload[i:i + CHUNK]
                await self.client.write_gatt_char(RECEIVE_CHAR_UUID, chunk)
                self.log(f"Sent chunk {i//CHUNK + 1}")

            self.log("MAC list send complete")

        except Exception as e:
            self.log(f"Send error: {e}")


# ================================================================
# MAIN
# ================================================================
if __name__ == "__main__":
    root = tk.Tk()
    app = BLEToolGUI(root)
    root.mainloop()