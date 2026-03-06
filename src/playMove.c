#include "goteam.h"

int16_t getRepres(gameState *game, int16_t index)
{ // get the representative of a point
  int16_t i = index;
  while (game->board[i].liberties > 0) i = game->board[i].liberties;
  // representatives store their liberties as a negative number and children point to the positive index of their representative
  return i;
}

void flattenLibs(gameState *game) // compresses the DSU to make every point on the board point directly to its representative
{
  for (int i = 0; i < (game->boardsize + 2) * (game->boardsize + 2); i++) {
    if (game->board[i].liberties > 0) {
      game->board[i].liberties = getRepres(game, i);
    }
  }
}

int addToGroup(gameState *game, int16_t newStone, int16_t repStone, gameChanges *changes)
{
  // liberties
  if (pushToChangeArray(&(changes->libertyChanges), repStone, game->board[repStone].liberties)) return 1;
  game->board[repStone].liberties += game->board[newStone].liberties; // add the liberties of the representatives

  if (pushToChangeArray(&(changes->libertyChanges), newStone, game->board[newStone].liberties)) return 1;
  game->board[newStone].liberties = repStone; // change representative (the old representative now points to the new representative)

  // circular pointing (chains)

  // connecting the indexes to unify the chains
  int16_t tempIndex = game->board[newStone].nextStone;
  if (pushToChangeArray(&(changes->nextChanges), newStone, game->board[newStone].nextStone)) return 1;
  game->board[newStone].nextStone = game->board[repStone].nextStone;

  if (pushToChangeArray(&(changes->nextChanges), repStone, game->board[repStone].nextStone)) return 1;
  game->board[repStone].nextStone = tempIndex;

  return 0;
}

// removes the entire chain from the board and updates the adjacent liberties
int capture(gameState *game, int16_t capturedStone, gameChanges *changes)
{
  int16_t currentStone = capturedStone;
  int16_t next;
  stoneType opp = game->board[capturedStone].color ^ 3; // opponent color
  int16_t capturedCount = 0;
  // loop through the circular pointing structure that signifies the chain
  do {
    // removing liberties
    if (pushToChangeArray(&(changes->libertyChanges), currentStone, game->board[currentStone].liberties)) return 1;
    game->board[currentStone].liberties = 0;

    // removing stone from the chain
    next = game->board[currentStone].nextStone;
    if (pushToChangeArray(&(changes->nextChanges), currentStone, game->board[currentStone].nextStone)) return 1;
    game->board[currentStone].nextStone = 0;

    // removing stone from actual board
    if (pushToChangeArray(&(changes->stoneChanges), currentStone, game->board[currentStone].color)) return 1;
    game->board[currentStone].color = EMPTY;

    // loop adjacents to increase liberties
    int8_t offsetTab[] = {1, 0, -1, 0, 1};
    for (uint8_t i = 0; i < 4; i++) {
      int16_t near = currentStone + offsetTab[i] + (game->boardsize + 2) * offsetTab[i + 1];
      stoneType nearStone = game->board[near].color;

      if (nearStone == opp) {
        int16_t opRep = getRepres(game, near);
        if (pushToChangeArray(&(changes->libertyChanges), opRep, game->board[opRep].liberties)) return 1;
        game->board[opRep].liberties--; // increasing liberties of enemy representative (liberties are stored as negative)
      }
    }
    capturedCount++;
  } while ((currentStone = next) != capturedStone); // reached the start
  if (capturedCount == 1) game->koPoint = capturedStone; // apply ko when a single stone was captured
  return 0;
}

int playMove(gameState *game, int16_t index, stoneType color, gameChanges *changes)
{
  if (game->board[index].color != EMPTY) return 1;
  if (index == game->koPoint) return 2; // playing on ko

  if (pushNewChange(changes)) { // store the indexes for later rollback
    quit(game, changes);
    exit(1);
  }

  changes->changeIndexArray[changes->numOfMoves - 1].koPoint = game->koPoint; // save ko
  game->koPoint = 0;

  stoneType opp = color ^ 3; // opponent color
  int8_t offsetTab[] = {1, 0, -1, 0, 1};

  // storing changes
  if (pushToChangeArray(&(changes->stoneChanges), index, game->board[index].color)) {
    quit(game, changes);
    exit(1);
  }
  game->board[index].color = color; // store the color

  if (pushToChangeArray(&(changes->nextChanges), index, game->board[index].nextStone)) {
    quit(game, changes);
    exit(1);
  }
  game->board[index].nextStone = index; // its only in chain with itself

  if (pushToChangeArray(&(changes->libertyChanges), index, game->board[index].liberties)) {
    quit(game, changes);
    exit(1);
  }
  game->board[index].liberties = -4; // starting liberties

  int16_t modLib = 0;
  int16_t myRep = index;
  for (uint8_t i = 0; i < 4; i++) {
    int16_t near = index + offsetTab[i] + (game->boardsize + 2) * offsetTab[i + 1]; // the adjacent index
    stoneType nearStone = game->board[near].color;

    if (nearStone != EMPTY) {
      modLib--; // reducing liberties
    }
    if (nearStone == opp) {

      int16_t opRep = getRepres(game, near); // get opponent representative
      int16_t opLib = game->board[opRep].liberties;
      opLib++;          // reduce their liberties by one (liberties are stored as negative)
      if (opLib == 0) { // 0 means 0 liberties, as at the index 0 there is border so no stone can point to that
        if (capture(game, opRep, changes)) {
          quit(game, changes);
          exit(1);
        } // capture the opponent chain
      }
      else {
        // store the previous liberty count
        if (pushToChangeArray(&(changes->libertyChanges), opRep, game->board[opRep].liberties)) {
          quit(game, changes);
          exit(1);
        }
        game->board[opRep].liberties = opLib; // and change it
      }
    }
    else if (nearStone == color) {
      int friendlyRep = getRepres(game, near); // get representative of the friendly chain
      modLib--;                                // liberties should be reduced again as the move also reduces the liberties of the friendly adjacent chain

      if (myRep == index) { // change my representative
        myRep = friendlyRep;
        if (addToGroup(game, index, myRep, changes)) {
          quit(game, changes);
          exit(1);
        } // add myself to the friendly chain
      }
      else if (friendlyRep != myRep) {
        if (addToGroup(game, friendlyRep, myRep, changes)) {
          quit(game, changes);
          exit(1);
        } // unify the friendly chain with my chain
      }
    }
  }
  // store the liberties that my representative used to have
  if (pushToChangeArray(&(changes->libertyChanges), myRep, game->board[myRep].liberties)) {
    quit(game, changes);
    exit(1);
  }
  game->board[myRep].liberties -= modLib;  // modify them, subtracting the negative (or 0) number means adding to negative number of liberties stored for the representative
  if (game->board[myRep].liberties >= 0) { // if the ending number is no longer negative its a suicide
    rollbackChanges(changes, game);
    return 3;
  }
  return 0;
}

int pushToChangeArray(changeArray *arr, const int16_t index, const int16_t prevValue) // push a change to the array
{
  arr->numOfChanges++;
  if (arr->numOfChanges % CHNG_ARR_REALL_SIZE == 0) { // reallocation logic
    change *temp = realloc(arr->changesArr, (arr->numOfChanges + CHNG_ARR_REALL_SIZE) * sizeof(change));
    if (temp == NULL) {
      arr->numOfChanges--;
      return 1;
    }
    arr->changesArr = temp;
  }

  arr->changesArr[arr->numOfChanges - 1].index = index;         // the board index at which the change happened
  arr->changesArr[arr->numOfChanges - 1].prevValue = prevValue; // the value that got replaced
  return 0;
}

int pushNewChange(gameChanges *changes) // pushing the indexes of the 3 arrays into the change index array when a new move is to be made
{
  changes->numOfMoves++;
  if (changes->numOfMoves % CHNG_ARR_REALL_SIZE == 0) { // reallocation logic
    changeIndex *temp = realloc(changes->changeIndexArray, (changes->numOfMoves + CHNG_ARR_REALL_SIZE) * sizeof(changeIndex));
    if (temp == NULL) {
      changes->numOfMoves--;
      return 1;
    }
    changes->changeIndexArray = temp;
  }

  // storing the indexes for all 3 changes array for future rollback
  changes->changeIndexArray[changes->numOfMoves - 1].stoneChangesStartIndex = changes->stoneChanges.numOfChanges;
  changes->changeIndexArray[changes->numOfMoves - 1].libertyChangesStartIndex = changes->libertyChanges.numOfChanges;
  changes->changeIndexArray[changes->numOfMoves - 1].nextChangesStartIndex = changes->nextChanges.numOfChanges;

  return 0;
}

void rollbackChanges(gameChanges *changes, gameState *game) // revert changes of the last move
{
  if (changes->numOfMoves == 0) return;

  // revert stone changes up to stored change index
  for (int i = changes->stoneChanges.numOfChanges - 1; i >= changes->changeIndexArray[changes->numOfMoves - 1].stoneChangesStartIndex; i--) {
    game->board[changes->stoneChanges.changesArr[i].index].color = changes->stoneChanges.changesArr[i].prevValue;
    changes->stoneChanges.numOfChanges--;
  }

  // revert liberty changes up to stored change index
  for (int i = changes->libertyChanges.numOfChanges - 1; i >= changes->changeIndexArray[changes->numOfMoves - 1].libertyChangesStartIndex; i--) {
    game->board[changes->libertyChanges.changesArr[i].index].liberties = changes->libertyChanges.changesArr[i].prevValue;
    changes->libertyChanges.numOfChanges--;
  }

  // revert nextStone changes up to stored change index
  for (int i = changes->nextChanges.numOfChanges - 1; i >= changes->changeIndexArray[changes->numOfMoves - 1].nextChangesStartIndex; i--) {
    game->board[changes->nextChanges.changesArr[i].index].nextStone = changes->nextChanges.changesArr[i].prevValue;
    changes->nextChanges.numOfChanges--;
  }

  // revert ko
  game->koPoint = changes->changeIndexArray[changes->numOfMoves - 1].koPoint;

  // pop the change
  changes->numOfMoves--;
}
