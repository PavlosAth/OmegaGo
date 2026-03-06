#ifndef GOTEAM_H
#define GOTEAM_H
#define CHNG_ARR_REALL_SIZE 2048

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
  EMPTY,
  BLACK,
  WHITE,
  BORDER
} stoneType;

typedef struct {
  stoneType color;
  int16_t liberties;
  int16_t nextStone;
} stoneVals;

typedef struct {
  stoneVals *board;
  uint8_t boardsize;
  uint16_t koPoint;
  float komi;

  // for MCTS (Dynamic based on time).
  int totalMoves;
  float totalTime;
} gameState;

typedef struct {
  int16_t index;
  int16_t prevValue;
} change;

typedef struct {
  uint16_t numOfChanges;
  change *changesArr;
} changeArray;

typedef struct change_index {
  int16_t stoneChangesStartIndex;
  int16_t libertyChangesStartIndex;
  int16_t nextChangesStartIndex;
  int16_t koPoint;
} changeIndex;

typedef struct {
  uint16_t numOfMoves;
  changeIndex *changeIndexArray;
  changeArray stoneChanges;
  changeArray libertyChanges;
  changeArray nextChanges;
} gameChanges;

// playMove
int playMove(gameState *game, int16_t index, stoneType color, gameChanges *changes);

void flattenLibs(gameState *game);

int pushToChangeArray(changeArray *arr, const int16_t index, const int16_t prevValue);

void rollbackChanges(gameChanges *changes, gameState *game);

int pushNewChange(gameChanges *changes);

int finalScore(gameState *game);

void quit(gameState *game, gameChanges *changes);

// genMove
int16_t genMove(gameState *game, gameChanges *changes, stoneType color);
void cleanupMCTSEngine(void);

// getRepres, used in both genmove and playmove.
int16_t getRepres(gameState *game, int16_t index);

#endif
