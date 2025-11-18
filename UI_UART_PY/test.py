import serial
import time

ser = serial.Serial(
    port='/dev/ttyUSB0',  # Cambia si usás otro puerto
    baudrate=9600,  # Debe coincidir con la LPC1769
    timeout=0.1  # Lectura no bloqueante
)

print("Escuchando UART...")

try:
    while True:
        if ser.in_waiting > 0:  # Hay datos disponibles
            data = ser.read(1)  # Leer 1 byte
            valor = data[0]  # Convertir a entero
            print(f"Decimal: {valor}   Hex: {valor:02X}")

        # Pequeña pausa para no saturar la consola
        time.sleep(0.01)

except KeyboardInterrupt:
    print("\nCerrando conexión...")

finally:
    ser.close()