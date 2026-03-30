#ifndef TETRIS_GAME_H
#define TETRIS_GAME_H

#include <Arduino.h>
#include "TFT_22_ILI9225.h"

#define GRID_W 10
#define GRID_H 18
#define BLOCK_SIZE 8
#define OFFSET_X 40
#define OFFSET_Y 20

// Tetromino shapes (4x4 grids)
const uint16_t SHAPES[7][4] = {
  {0x0F00, 0x4444, 0x0F00, 0x4444}, // I
  {0x4460, 0x0E80, 0xC440, 0x2E00}, // L
  {0x44C0, 0x8E00, 0x6440, 0x0E20}, // J
  {0x0660, 0x0660, 0x0660, 0x0660}, // O
  {0x06C0, 0x8C40, 0x06C0, 0x8C40}, // S
  {0x0C60, 0x4C80, 0x0C60, 0x4C80}, // Z
  {0x4E00, 0x4640, 0x0E40, 0x4C40}  // T
};

const uint16_t SHAPE_COLORS[7] = {
  COLOR_CYAN, COLOR_ORANGE, COLOR_BLUE, COLOR_YELLOW, COLOR_GREEN, COLOR_RED, COLOR_MAGENTA
};

class TetrisGame {
  public:
    uint8_t grid[GRID_W][GRID_H] = {0};
    int px, py, pType, pRot;
    uint32_t lastFall = 0;
    int score = 0;

    void init() {
      memset(grid, 0, sizeof(grid));
      score = 0;
      newPiece();
    }

    void newPiece() {
      px = GRID_W / 2 - 2;
      py = 0;
      pType = random(7);
      pRot = 0;
    }

    bool checkCollision(int nx, int ny, int nr) {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          if ((SHAPES[pType][nr] >> (15 - (i + j * 4))) & 1) {
            int gx = nx + i;
            int gy = ny + j;
            if (gx < 0 || gx >= GRID_W || gy >= GRID_H || (gy >= 0 && grid[gx][gy])) return true;
          }
        }
      }
      return false;
    }

    void lockPiece() {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          if ((SHAPES[pType][pRot] >> (15 - (i + j * 4))) & 1) {
            if (py + j >= 0) grid[px + i][py + j] = pType + 1;
          }
        }
      }
      clearLines();
      newPiece();
    }

    void clearLines() {
      for (int y = GRID_H - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < GRID_W; x++) if (!grid[x][y]) full = false;
        if (full) {
          score += 10;
          for (int ty = y; ty > 0; ty--) {
            for (int x = 0; x < GRID_W; x++) grid[x][ty] = grid[x][ty - 1];
          }
          y++; 
        }
      }
    }

    void draw(TFT_22_ILI9225 &tft) {
      // Draw Grid
      tft.drawRectangle(OFFSET_X-1, OFFSET_Y-1, OFFSET_X + GRID_W*BLOCK_SIZE, OFFSET_Y + GRID_H*BLOCK_SIZE, COLOR_WHITE);
      for (int x = 0; x < GRID_W; x++) {
        for (int y = 0; y < GRID_H; y++) {
          uint16_t color = grid[x][y] ? SHAPE_COLORS[grid[x][y] - 1] : COLOR_BLACK;
          tft.fillRectangle(OFFSET_X + x * BLOCK_SIZE, OFFSET_Y + y * BLOCK_SIZE, 
                           OFFSET_X + (x + 1) * BLOCK_SIZE - 1, OFFSET_Y + (y + 1) * BLOCK_SIZE - 1, color);
        }
      }
      // Draw Current Piece
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          if ((SHAPES[pType][pRot] >> (15 - (i + j * 4))) & 1) {
            tft.fillRectangle(OFFSET_X + (px + i) * BLOCK_SIZE, OFFSET_Y + (py + j) * BLOCK_SIZE, 
                             OFFSET_X + (px + i + 1) * BLOCK_SIZE - 1, OFFSET_Y + (py + j + 1) * BLOCK_SIZE - 1, SHAPE_COLORS[pType]);
          }
        }
      }
    }
};

#endif