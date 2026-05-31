#pip install pyserial

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import serial
import serial.tools.list_ports

# === Настройки ===
DEFAULT_BAUD = 115200
COMMAND_FORMAT = "{}\n"

class LEDControlApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Управление светодиодами")
        self.root.geometry("1420x500")
        self.root.resizable(True, True)

        self.ser = None
        self.checkboxes = []
        self.mode_var = tk.StringVar(value="0x01")
        self.last_command = ""  # для отображения

        self.build_ui()

    def build_ui(self):
        # === Панель управления ===
        control_frame = tk.Frame(self.root)
        control_frame.pack(pady=10, fill="x")

        tk.Label(control_frame, text="Порт:").grid(row=0, column=0, padx=5)
        self.port_combo = ttk.Combobox(control_frame, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=5)

        tk.Label(control_frame, text="Скорость:").grid(row=0, column=2, padx=5)
        tk.Label(control_frame, text=str(DEFAULT_BAUD)).grid(row=0, column=3, padx=5)

        self.connect_btn = tk.Button(control_frame, text="Подключиться", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=4, padx=10)

        tk.Button(control_frame, text="Обновить", command=self.refresh_ports).grid(row=0, column=5, padx=5)

        tk.Label(control_frame, text="Режим:").grid(row=0, column=6, padx=5)
        mode_combo = ttk.Combobox(control_frame, textvariable=self.mode_var,
                                  values=["0x00", "0x01", "0x02", "0x03"], state="readonly", width=8)
        mode_combo.grid(row=0, column=7, padx=5)
        mode_combo.bind("<<ComboboxSelected>>", lambda e: self.send_data())  # ✅ Исправлено: lambda

        # === Блоки чекбоксов (5 × 16) ===
        grid_frame = tk.Frame(self.root)
        grid_frame.pack(pady=10)

        for reg in range(5):
            reg_frame = tk.LabelFrame(grid_frame, text=f"Регистр {reg+1}")
            reg_frame.grid(row=0, column=reg, padx=10, sticky="n")

            reg_vars = []
            for bit in range(16):
                row = 15 - (bit // 8)
                col = bit % 8
                var = tk.IntVar()
                cb = tk.Checkbutton(reg_frame, variable=var, command=self.send_data)
                cb.grid(row=row, column=col, padx=2, pady=1)
                reg_vars.append(var)
            self.checkboxes.extend(reg_vars)

        # === Поле с последней командой ===
        log_frame = tk.LabelFrame(self.root, text="Последняя отправленная команда")
        log_frame.pack(pady=10, fill="x", padx=20)

        self.command_text = tk.Text(log_frame, height=2, state="normal", font=("Courier", 10))
        self.command_text.pack(fill="x", padx=5, pady=5)
        self.command_text.config(state="disabled")  # только для чтения

        # === Статус ===
        self.status_label = tk.Label(self.root, text="Не подключено", fg="red")
        self.status_label.pack(pady=5)

        # Обновляем список портов
        self.refresh_ports()

    def refresh_ports(self):
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None
            self.connect_btn.config(text="Подключиться")
            self.status_label.config(text="Отключено", fg="red")
        else:
            port = self.port_combo.get()
            if not port:
                messagebox.showerror("Ошибка", "Выберите порт!")
                return
            try:
                self.ser = serial.Serial(port, DEFAULT_BAUD, timeout=1)
                self.connect_btn.config(text="Отключиться")
                self.status_label.config(text=f"Подключено к {port}", fg="green")
                self.send_data()  # отправляем начальное состояние
            except Exception as e:
                messagebox.showerror("Ошибка подключения", str(e))

    def send_data(self):
        if not self.ser or not self.ser.is_open:
            return

        # Собираем 10 байт
        bytes_list = [0] * 10
        for i in range(80):
            if self.checkboxes[i].get():
                byte_idx = i // 8
                bit_idx = 7 - (i % 8)
                bytes_list[byte_idx] |= (1 << bit_idx)

        # Формируем команду
        hex_values = [f"0x{b:02X}" for b in bytes_list]
        mode = self.mode_var.get()
        hex_values.append(mode)
        command = ",".join(hex_values)
        self.last_command = command

        # Отправляем
        try:
            self.ser.write(COMMAND_FORMAT.format(command).encode())
            # Обновляем текстовое поле
            self.command_text.config(state="normal")
            self.command_text.delete(1.0, tk.END)
            self.command_text.insert(tk.END, command)
            self.command_text.config(state="disabled")
        except Exception as e:
            self.status_label.config(text="Ошибка отправки", fg="red")
            messagebox.showerror("Ошибка", "Не удалось отправить данные.")

    def on_closing(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()


# === Запуск ===
if __name__ == "__main__":
    root = tk.Tk()
    app = LEDControlApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
