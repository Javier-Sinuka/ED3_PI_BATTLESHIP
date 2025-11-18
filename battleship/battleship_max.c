// battleship_max.c
#include "battleship_max.h"

// ========= Constantes internas =========
static const uint32_t BLINK_MS      = 180U;  // blink tablero jugador
static const uint32_t OPP_BLINK_MS  = 300U;  // blink MISSES oponente
static const uint32_t ERR_BLINK_MS  = 90U;   // periodo blink error
static const uint8_t  ERR_BLINK_TOGGLES = 6U; // 3 ciclos on/off

// 0U = modo juego real (NO se ven los barcos enemigos al inicio)
// 1U = modo debug (se ven también los SHIP del oponente)
#define BS_DEBUG_SHOW_ENEMY  0U

// Máximo de disparos permitidos
#define MAX_SHOTS 20U

// ========= Estado global =========

// Tableros lógicos
static BATTLESHIP_STATUS_Type player_board[8][8];
static BATTLESHIP_STATUS_Type opponent_board[8][8];

// Buffers de display
static uint8_t piv_rows[8];    // dev0
static uint8_t player_rows[8]; // dev1

// Oponente
static uint8_t  opponent_exists   = 1U;
static uint8_t  oppBlinkState     = 1U;
static uint32_t oppLastBlink      = 0U;

// Contador de disparos (dev3)
//  - Muestra dígitos 0..9
//  - Se incrementa en 1 cada 2 disparos válidos
//  - Al llegar a MAX_SHOTS se muestra una carita triste
static const uint8_t digits[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x3C}, //0
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x7E}, //1
    {0x3C,0x66,0x06,0x0C,0x18,0x60,0x66,0x7E}, //2
    {0x3C,0x66,0x06,0x1C,0x06,0x06,0x66,0x3C}, //3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x1E}, //4
    {0x7E,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C}, //5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x66,0x3C}, //6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x18}, //7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x3C}, //8
    {0x3C,0x66,0x66,0x3E,0x06,0x06,0x66,0x3C}  //9
};

static const uint8_t sad_face[8] = {
    0b00000000,
    0b01000010, // ojos
    0b01000010,
    0b00000000,
    0b00000000,
    0b00111100, // boca
    0b01000010,
    0b00000000
};

static uint8_t  current_digit = 0U;   // dígito mostrado (0..9)
static uint8_t  shots_fired   = 0U;   // disparos válidos realizados

// Colocación
static int   prow = 3, pcol = 0;               // cursor barco / disparo
static ORI_t cur_ori = ORI_H;
static const uint8_t SHIP_LIST[3] = {2U,4U,6U};  // barcos 2,4,6
static uint8_t ship_index = 0U;                // 0->2, 1->4, 2->6
static uint8_t all_ships_placed = 0U;
static BS_Mode mode = MODE_PLACE;

// Blink tablero jugador cuando terminó de colocar
static uint8_t  blink_enabled = 0U;
static uint8_t  blink_state   = 0U;
static uint32_t last_blink    = 0U;

// Blink de error al intentar colocar barco inválido
static uint8_t  errBlink_active   = 0U;
static uint8_t  errBlink_togglesRemaining = 0U;
static uint8_t  errBlink_showBase = 0U; // alterna jugador vs preview
static uint32_t errBlink_last     = 0U;

// ========= Helpers de tablero =========

static void boardClear(BATTLESHIP_STATUS_Type b[8][8]){
    int r,c;
    for(r=0;r<8;r++){
        for(c=0;c<8;c++){
            b[r][c]=WATER;
        }
    }
}

static void boardToRowsPlayer(const BATTLESHIP_STATUS_Type b[8][8],
                              uint8_t rows[8])
{
    int r,c;
    for(r=0;r<8;r++){
        uint8_t v=0U;
        for(c=0;c<8;c++){
            if(b[r][c]==SHIP || b[r][c]==HIT){
                v |= (uint8_t)(1u<<c);
            }
        }
        rows[r]=v;
    }
}

static void boardToRowsOpponent(const BATTLESHIP_STATUS_Type b[8][8],
                                uint8_t rows[8], uint8_t blinkOn)
{
    int r,c;
    for(r=0;r<8;r++){
        uint8_t v=0U;
        for(c=0;c<8;c++){
            BATTLESHIP_STATUS_Type st=b[r][c];

            // En modo juego real:
            //   - HIT  siempre visible (LED fijo)
            //   - MISS visible solo cuando blinkOn=1 (blink agua)
            //   - SHIP oculto (solo se ve cuando pasa a HIT)
            //
            // En modo debug (BS_DEBUG_SHOW_ENEMY=1):
            //   - SHIP también se ve fijo desde el inicio.
            if(
                st==HIT
#if BS_DEBUG_SHOW_ENEMY
                || st==SHIP
#endif
              ){
                v |= (uint8_t)(1u<<c);
            }else if(st==MISS && blinkOn){
                v |= (uint8_t)(1u<<c);
            }
        }
        rows[r]=v;
    }
}

static void placeShipRandom(BATTLESHIP_STATUS_Type b[8][8], int len){
    int tries;
    for(tries=0; tries<100; tries++){
        uint32_t rnd = BS_Hal_GetRandom();
        uint8_t vertical = (uint8_t)(rnd & 1u);
        int r, c;
        int k;
        uint8_t freeSpot = 1U;

        if(vertical){
            r = (int)(BS_Hal_GetRandom() % (8-len+1));
            c = (int)(BS_Hal_GetRandom() % 8);
        }else{
            r = (int)(BS_Hal_GetRandom() % 8);
            c = (int)(BS_Hal_GetRandom() % (8-len+1));
        }

        for(k=0;k<len;k++){
            int rr = r + (vertical ? k : 0);
            int cc = c + (vertical ? 0 : k);
            if(b[rr][cc]!=WATER){
                freeSpot = 0U;
                break;
            }
        }
        if(!freeSpot) continue;

        for(k=0;k<len;k++){
            int rr = r + (vertical ? k : 0);
            int cc = c + (vertical ? 0 : k);
            b[rr][cc]=SHIP;
        }
        return;
    }
}

// ========= Render oponente (dev2) =========

static void drawOpponentFrame(uint8_t blinkOn){
    uint8_t rows[8];
    if(!opponent_exists){
        uint8_t NP_rows[8] = {
            0b10000001, 0b11000001, 0b10100001, 0b10010001,
            0b10001001, 0b00011110, 0b00010001, 0b00011110
        };
        MAX_DrawRows(BS_DEV_OPPONENT, NP_rows);
        return;
    }
    boardToRowsOpponent(opponent_board, rows, blinkOn);
    MAX_DrawRows(BS_DEV_OPPONENT, rows);
}

static void genOpponentBoard(void){
    boardClear(opponent_board);
    placeShipRandom(opponent_board, 2);
    placeShipRandom(opponent_board, 4);
    placeShipRandom(opponent_board, 6);
    opponent_exists = 1U;
    // Con BS_DEBUG_SHOW_ENEMY=0, esto dibuja todo apagado (no se ven SHIP).
    drawOpponentFrame(1U);
}

// ========= Render jugador (dev1) y pivote (dev0) =========

static void renderPlayerDev1(void){
    boardToRowsPlayer(player_board, player_rows);
    MAX_DrawRows(BS_DEV_PLAYER, player_rows);
}

// redibuja dev0: base = tablero jugador, + preview de barco si corresponde
static void renderDev0_withPreview(uint8_t drawPreview){
    int r;
    for(r=0;r<8;r++){
        piv_rows[r]=player_rows[r];
    }

    if(drawPreview && !all_ships_placed){
        uint8_t len = SHIP_LIST[ship_index];
        uint8_t k;
        for(k=0;k<len;k++){
            int rr=prow+(cur_ori==ORI_V?(int)k:0);
            int cc=pcol+(cur_ori==ORI_H?(int)k:0);
            if(rr>=0 && rr<8 && cc>=0 && cc<8){
                piv_rows[rr] |= (uint8_t)(1u<<cc);
            }
        }
    }
    MAX_DrawRows(BS_DEV_PIVOT, piv_rows);
}

// ========= Movimiento / validación =========

static uint8_t canPlaceSegment(int r,int c,ORI_t ori,uint8_t len,
                               BATTLESHIP_STATUS_Type board[8][8],
                               uint8_t checkOverlap)
{
    int k;
    if(ori==ORI_H){
        if(c<0 || (c+(int)len-1)>7 || r<0 || r>7) return 0U;
    }else{
        if(r<0 || (r+(int)len-1)>7 || c<0 || c>7) return 0U;
    }
    if(!checkOverlap) return 1U;
    for(k=0;k<(int)len;k++){
        int rr=r+(ori==ORI_V?k:0);
        int cc=c+(ori==ORI_H?k:0);
        if(board[rr][cc]!=WATER) return 0U;
    }
    return 1U;
}

static uint8_t moveLeft (int *r,int *c, ORI_t ori,uint8_t len,
                         BATTLESHIP_STATUS_Type b[8][8], uint8_t chk)
{
    int nr=*r, nc=*c-1;
    if(canPlaceSegment(nr,nc,ori,len,b,chk)){*r=nr;*c=nc;return 1U;}
    return 0U;
}
static uint8_t moveRight(int *r,int *c, ORI_t ori,uint8_t len,
                         BATTLESHIP_STATUS_Type b[8][8], uint8_t chk)
{
    int nr=*r, nc=*c+1;
    if(canPlaceSegment(nr,nc,ori,len,b,chk)){*r=nr;*c=nc;return 1U;}
    return 0U;
}
static uint8_t moveUp   (int *r,int *c, ORI_t ori,uint8_t len,
                         BATTLESHIP_STATUS_Type b[8][8], uint8_t chk)
{
    int nr=*r+1, nc=*c;
    if(canPlaceSegment(nr,nc,ori,len,b,chk)){*r=nr;*c=nc;return 1U;}
    return 0U;
}
static uint8_t moveDown (int *r,int *c, ORI_t ori,uint8_t len,
                         BATTLESHIP_STATUS_Type b[8][8], uint8_t chk)
{
    int nr=*r-1, nc=*c;
    if(canPlaceSegment(nr,nc,ori,len,b,chk)){*r=nr;*c=nc;return 1U;}
    return 0U;
}

// ========= Colocación y commit de barco =========

static void commitShip(uint8_t len){
    uint8_t k;
    for(k=0;k<len;k++){
        int rr=prow+(cur_ori==ORI_V?(int)k:0);
        int cc=pcol+(cur_ori==ORI_H?(int)k:0);
        player_board[rr][cc]=SHIP;
    }
    renderPlayerDev1();
    renderDev0_withPreview(0U);
}

// ========= Blink de error (no bloqueante) =========

static void startErrorBlink(uint32_t nowMs){
    errBlink_active   = 1U;
    errBlink_togglesRemaining = ERR_BLINK_TOGGLES;
    errBlink_last     = nowMs;
    errBlink_showBase = 0U; // primero muestro preview
}

// ========= Disparos y victoria =========

static SHOT_RESULT_t applyShotAtBoard(BATTLESHIP_STATUS_Type b[8][8],
                                      int r,int c)
{
    BATTLESHIP_STATUS_Type st=b[r][c];
    if(st==SHIP){
        b[r][c]=HIT;
        return SHOT_HIT_RES;
    }
    if(st==WATER){
        b[r][c]=MISS;
        return SHOT_MISS_RES;
    }
    return SHOT_REPEAT;
}

static uint8_t boardAllShipsDestroyed(const BATTLESHIP_STATUS_Type b[8][8]){
    int r,c;
    for(r=0;r<8;r++){
        for(c=0;c<8;c++){
            if(b[r][c]==SHIP) return 0U;
        }
    }
    return 1U;
}

// ========= Contador (dev3) =========

static void drawDigit(uint8_t dev,uint8_t num){
    uint8_t r;
    if(num>9U) return;
    for(r=0;r<8;r++){
        MAX_SetRow(dev, (uint8_t)(7u-r), digits[num][r]);
    }
}

static void drawSadFace(uint8_t dev){
    uint8_t r;
    for(r=0;r<8;r++){
        MAX_SetRow(dev, (uint8_t)(7u-r), sad_face[r]);
    }
}

static void shotCounter_Init(void){
    shots_fired   = 0U;
    current_digit = 0U;
    drawDigit(BS_DEV_COUNTER, current_digit);
}

static void shotCounter_OnValidShot(void){
    if(shots_fired >= MAX_SHOTS){
        // Ya no debería pasar, pero por seguridad
        drawSadFace(BS_DEV_COUNTER);
        return;
    }

    shots_fired++;

    if(shots_fired >= MAX_SHOTS){
        // Alcanzó el máximo de disparos
        drawSadFace(BS_DEV_COUNTER);
        return;
    }

    // Cada 2 disparos, subimos un dígito (0..9)
    uint8_t new_digit = (uint8_t)(shots_fired / 2U);
    if(new_digit > 9U) new_digit = 9U;

    if(new_digit != current_digit){
        current_digit = new_digit;
        drawDigit(BS_DEV_COUNTER, current_digit);
    }
}

// ========= API pública =========

void BS_GameInit(void){
    int r;

    MAX_InitAll();

    boardClear(player_board);
    boardClear(opponent_board);
    genOpponentBoard();

    prow=3; pcol=0;
    cur_ori=ORI_H;
    ship_index=0U;
    all_ships_placed=0U;
    mode=MODE_PLACE;

    blink_enabled=0U;
    blink_state=0U;
    last_blink=BS_Hal_GetMillis();

    errBlink_active=0U;
    errBlink_togglesRemaining=0U;

    oppBlinkState=1U;
    oppLastBlink=BS_Hal_GetMillis();

    // Contador de disparos en bloque 3
    shotCounter_Init();

    // Inicial: tablero jugador vacío, preview primer barco en dev0
    for(r=0;r<8;r++){
        player_rows[r]=0U;
        piv_rows[r]=0U;
    }
    renderPlayerDev1();
    renderDev0_withPreview(1U);
}

BS_Mode BS_GetMode(void){
    return mode;
}

void BS_AnimationsUpdate(uint32_t nowMs){
    // 1) Blink tablero jugador cuando todos los barcos colocados
    if(blink_enabled && mode==MODE_PLACE){
        if(nowMs - last_blink >= BLINK_MS){
            last_blink = nowMs;
            blink_state = (uint8_t)!blink_state;
            if(blink_state){
                MAX_Clear(BS_DEV_PLAYER);
            }else{
                MAX_DrawRows(BS_DEV_PLAYER, player_rows);
            }
        }
    }

    // 2) Blink de MISSES del oponente
    if(opponent_exists){
        if(nowMs - oppLastBlink >= OPP_BLINK_MS){
            oppLastBlink = nowMs;
            oppBlinkState = (uint8_t)!oppBlinkState;
            drawOpponentFrame(oppBlinkState);
        }
    }

    // 3) Blink de error al intentar colocar barco inválido
    if(errBlink_active){
        if(nowMs - errBlink_last >= ERR_BLINK_MS){
            errBlink_last = nowMs;
            if(errBlink_togglesRemaining>0U){
                errBlink_togglesRemaining--;
                errBlink_showBase = (uint8_t)!errBlink_showBase;
                if(errBlink_showBase){
                    MAX_DrawRows(BS_DEV_PIVOT, player_rows);
                }else{
                    renderDev0_withPreview(1U);
                }
            }else{
                errBlink_active=0U;
                renderDev0_withPreview(1U);
            }
        }
    }
}

// ====== FASE DE COLOCACIÓN ======

uint8_t BS_Placement_MoveCursor(BS_Dir dir){
    uint8_t len;
    uint8_t moved=0U;

    if(mode!=MODE_PLACE) return 0U;

    len = SHIP_LIST[ship_index];
    switch(dir){
    case BS_DIR_LEFT:
        moved = moveLeft(&prow,&pcol,cur_ori,len,player_board,0U);
        break;
    case BS_DIR_RIGHT:
        moved = moveRight(&prow,&pcol,cur_ori,len,player_board,0U);
        break;
    case BS_DIR_UP:
        moved = moveUp(&prow,&pcol,cur_ori,len,player_board,0U);
        break;
    case BS_DIR_DOWN:
        moved = moveDown(&prow,&pcol,cur_ori,len,player_board,0U);
        break;
    default:
        break;
    }
    if(moved){
        renderDev0_withPreview(1U);
    }
    return moved;
}

uint8_t BS_Placement_RotateCursor(void){
    uint8_t len;
    ORI_t newOri;
    if(mode!=MODE_PLACE || all_ships_placed) return 0U;
    len = SHIP_LIST[ship_index];
    newOri = (cur_ori==ORI_H ? ORI_V : ORI_H);
    if(canPlaceSegment(prow,pcol,newOri,len,player_board,0U)){
        cur_ori = newOri;
        renderDev0_withPreview(1U);
        return 1U;
    }
    return 0U;
}

BS_PlaceResult BS_Placement_TryPlaceCurrentShip(uint32_t nowMs){
    uint8_t len;
    if(mode!=MODE_PLACE) return BS_PLACE_INVALID;
    if(all_ships_placed) return BS_PLACE_ALL_DONE;

    len = SHIP_LIST[ship_index];
    if(!canPlaceSegment(prow,pcol,cur_ori,len,player_board,1U)){
        startErrorBlink(nowMs);
        return BS_PLACE_INVALID;
    }

    commitShip(len);

    if(ship_index < 2U){
        ship_index++;
        renderDev0_withPreview(1U);
        return BS_PLACE_OK;
    }else{
        all_ships_placed=1U;
        blink_enabled=1U;
        last_blink=nowMs;
        renderDev0_withPreview(0U);
        return BS_PLACE_ALL_DONE;
    }
}

// ====== Transición a disparos ======

void BS_EnterShotMode(void){
    int r;
    if(mode!=MODE_PLACE) return;

    blink_enabled=0U;
    MAX_DrawRows(BS_DEV_PLAYER, player_rows);

    for(r=0;r<8;r++) piv_rows[r]=0U;
    prow=3; pcol=0;
    piv_rows[prow] |= (uint8_t)(1u<<pcol);
    MAX_DrawRows(BS_DEV_PIVOT, piv_rows);

    mode = MODE_SHOT;
}

// ====== FASE DE DISPARO ======

uint8_t BS_Shot_MoveCursor(BS_Dir dir){
    uint8_t moved=0U;
    int r;

    if(mode!=MODE_SHOT) return 0U;

    switch(dir){
    case BS_DIR_LEFT:
        moved = moveLeft(&prow,&pcol,ORI_H,1U,player_board,0U);
        break;
    case BS_DIR_RIGHT:
        moved = moveRight(&prow,&pcol,ORI_H,1U,player_board,0U);
        break;
    case BS_DIR_UP:
        moved = moveUp(&prow,&pcol,ORI_H,1U,player_board,0U);
        break;
    case BS_DIR_DOWN:
        moved = moveDown(&prow,&pcol,ORI_H,1U,player_board,0U);
        break;
    default:
        break;
    }
    if(moved){
        for(r=0;r<8;r++) piv_rows[r]=0U;
        piv_rows[prow] |= (uint8_t)(1u<<pcol);
        MAX_DrawRows(BS_DEV_PIVOT, piv_rows);
    }
    return moved;
}

SHOT_RESULT_t BS_Shot_FireAtCursor(void){
    SHOT_RESULT_t res;

    if(mode!=MODE_SHOT) return SHOT_NONE;

    // Si ya se usaron todos los disparos, no permitir más
    if(shots_fired >= MAX_SHOTS){
        drawSadFace(BS_DEV_COUNTER);
        return SHOT_NONE;
    }

    // Aplica disparo sobre tablero lógico del oponente:
    //  - SHIP -> HIT  (queda LED fijo en drawOpponentFrame)
    //  - WATER -> MISS (blink agua vía oppBlinkState)
    res = applyShotAtBoard(opponent_board, prow, pcol);

    // Solo contamos disparos NUEVOS (no repetidos)
    if(res == SHOT_HIT_RES || res == SHOT_MISS_RES){
        shotCounter_OnValidShot();
    }

    // Refrescar dev2 inmediatamente con blinkOn=1 (para ver impacto "rápido")
    drawOpponentFrame(1U);
    return res;
}

uint8_t BS_Shot_OpponentAllDestroyed(void){
    return boardAllShipsDestroyed(opponent_board);
}
