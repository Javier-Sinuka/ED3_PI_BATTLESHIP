import sys
import serial
import pygame

# ------------------------
# CONFIGURACIÓN SERIAL
# ------------------------
SERIAL_PORT = '/dev/ttyUSB0'   # Cambia si hace falta
BAUDRATE    = 9600

# ------------------------
# MAPEO DE ESTADOS
# ------------------------
# 0x00 -> PANTALLA DE INICIO
# 0x01 -> PAUSA
# 0x02 -> JUGADOR 1
# 0x03 -> JUGADOR 2
# 0x04 -> GANADOR J1
# 0x05 -> GANADOR J2

STATE_IMAGES = {
    0x00: "assets/pantalla_inicio.png",
    0x01: "assets/pausa.png",
    0x02: "assets/jugador1.png",
    0x03: "assets/jugador2.png",
    0x04: "assets/ganador_j1.png",
    0x05: "assets/ganador_j2.png",
}

# Estado por defecto
current_state = 0x00

# ------------------------
# INICIALIZAR SERIAL
# ------------------------
def init_serial():
    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            timeout=0.01   # Cortito para no bloquear el loop de pygame
        )
        # Limpiar cualquier basura previa del buffer
        ser.reset_input_buffer()
        print(f"Escuchando UART en {SERIAL_PORT} @ {BAUDRATE}...")
        return ser
    except serial.SerialException as e:
        print(f"[WARN] No se pudo abrir el puerto serie: {e}")
        print("El programa seguirá corriendo SIN UART (podés probar con teclas 0–5).")
        return None

# ------------------------
# INICIALIZAR PYGAME
# ------------------------
def init_pygame():
    pygame.init()
    pygame.display.set_caption("Batalla Naval - Estados")
    screen = pygame.display.set_mode((0, 0), pygame.FULLSCREEN)  # pantalla completa
    # Si preferís ventana:
    # screen = pygame.display.set_mode((1280, 720))
    return screen

# ------------------------
# CARGAR IMÁGENES
# ------------------------
def load_images(screen):
    images = {}
    screen_rect = screen.get_rect()

    for state, path in STATE_IMAGES.items():
        try:
            img = pygame.image.load(path).convert()
            # Escalamos a tamaño pantalla
            img = pygame.transform.scale(img, (screen_rect.width, screen_rect.height))
            images[state] = img
            print(f"[OK] Cargada imagen '{path}' para estado 0x{state:02X}")
        except Exception as e:
            print(f"[ERROR] No se pudo cargar '{path}': {e}")
    return images

# ------------------------
# RENDER TEXTO SUPERPUESTO
# ------------------------
def render_text(screen, text, size=80):
    font = pygame.font.SysFont("arial", size, bold=True)
    surface = font.render(text, True, (255, 255, 255))
    rect = surface.get_rect(center=screen.get_rect().center)
    # Sombra simple
    shadow = font.render(text, True, (0, 0, 0))
    shadow_rect = shadow.get_rect(center=(rect.centerx+3, rect.centery+3))

    screen.blit(shadow, shadow_rect)
    screen.blit(surface, rect)

# ------------------------
# LOOP PRINCIPAL
# ------------------------
def main():
    global current_state

    ser = init_serial()
    screen = init_pygame()
    clock = pygame.time.Clock()
    images = load_images(screen)

    running = True

    while running:
        # --------------------
        # EVENTOS DE PYGAME
        # --------------------
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            # Escape para salir
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                # Teclas 0-5 para debug si no tenés el micro
                if event.key in (pygame.K_0, pygame.K_1, pygame.K_2,
                                 pygame.K_3, pygame.K_4, pygame.K_5):
                    current_state = event.key - pygame.K_0
                    print(f"[DEBUG] Estado cambiado por teclado a 0x{current_state:02X}")

        # --------------------
        # LECTURA SERIAL
        # --------------------
        if ser is not None:
            try:
                n = ser.in_waiting
                if n > 0:
                    # Leer TODO lo que haya en el buffer de golpe
                    data = ser.read(n)
                    # Nos quedamos con el ÚLTIMO byte (lo más nuevo)
                    val = data[-1]
                    print(f"Recibidos {n} bytes, último: {val} (0x{val:02X})")
                    if val in STATE_IMAGES:
                        current_state = val
                    else:
                        print(f"[WARN] Estado desconocido: 0x{val:02X}")
            except serial.SerialException as e:
                print(f"[ERROR] Problema leyendo serial: {e}")
                ser = None  # dejamos de usarlo

        # --------------------
        # DIBUJAR ESTADO
        # --------------------
        screen.fill((0, 0, 0))

        img = images.get(current_state)
        if img:
            screen.blit(img, (0, 0))
        else:
            # Fallback si no hay imagen cargada
            render_text(screen, f"Estado 0x{current_state:02X}", size=80)

        # Mensaje extra para cada estado (opcional)
        if current_state == 0x00:
            render_text(screen, "PULSA INICIO", size=80)
        elif current_state == 0x01:
            render_text(screen, "PAUSA - MOMENTO DE UN BRAKE", size=80)
        elif current_state == 0x02:
            render_text(screen, "", size=80)
        elif current_state == 0x03:
            render_text(screen, "", size=80)
        elif current_state == 0x04:
            render_text(screen, "", size=80)
        elif current_state == 0x05:
            render_text(screen, "", size=80)

        pygame.display.flip()
        clock.tick(60)  # 60 FPS aprox.

    # ------------------------
    # SALIDA LIMPIA
    # ------------------------
    if ser is not None and ser.is_open:
        ser.close()
    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
