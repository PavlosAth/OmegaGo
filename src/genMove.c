#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "goteam.h"

#define NODE_POOL_SIZE 850000 
#define MAX_GAME_LENGTH 700

#define RAVE_CONSTANT 250.0 
#define OPENING_MOVES 4 //6 To avoid making instant captures easy, could have been more.
#define FPU_REDUCTION 0.25f 
#define MOVES_TO_SIMULATE 30

//Heuristic Weights for patterns
//Defined because I change them commonly.
#define PATTERN_CRITICAL 3000
#define PATTERN_MEDIUM 900
#define PATTERN_BAD -3000

typedef struct { 
  int16_t move; 
  int score; 
} MoveScore;

typedef struct MCTSNode {
  int16_t move; 
  stoneType color;
  uint32_t visits;
  float wins;
  
  //RAVE stats
  uint32_t amafVisits;
  float amafWins; //Used for early search, by using global statistics.
  
  uint8_t isIllegal; 
  uint8_t urgency; 
  //0 = Low,
  //1 = Medium, (Unused for now) 
  //2 = Critical (Atari/Ladder)

  struct MCTSNode *parent; //Move that led here
  struct MCTSNode *firstChild; //First child in linked list of possible next moves
  struct MCTSNode *nextSibling; //Next alternative move from same parent
} MCTSNode;

typedef struct {
  MCTSNode *nodePool; //Pre-allocated array of all MCTS nodes
  uint32_t nodePoolIndex; //Next free node in pool (simmilar to an arena allocator)
  MCTSNode *root; //Starting position of the tree
  
  //Moves played in current simulation
  int16_t moveHistory[MAX_GAME_LENGTH];
  stoneType colorHistory[MAX_GAME_LENGTH];
  uint16_t historyLength; //How many moves in current simulation
  
  gameChanges simChanges; //Track changes to allow rollbacks after simulations.
  
  MCTSNode *lastMove; //Node repressenting last played move

  int initialized; //Wether or not MCTS is initialized. 
} MCTSEngine;

static MCTSEngine globalEngine = {0};

static uint32_t rngState = 123456789;
uint32_t fastRand(void) {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState; //Using a Xor-shift approach we create a fastRand function
}

void initMCTSEngine(MCTSEngine *engine, gameState *game, gameChanges *changes) {
  if(engine->initialized) return; //Check if engine has already started.
  engine->initialized = 1; //Update the engine to initialized

  engine->nodePool = calloc(NODE_POOL_SIZE, sizeof(MCTSNode));
  if(engine->nodePool == NULL) {
    fprintf(stderr, "Error! Memory allocation failed\n");
    quit(game, changes);
    exit(1);
  }
  
  //Large buffer for simulation changes
  int maxChanges = 2500; 
  engine->simChanges.stoneChanges.changesArr = malloc(maxChanges * 40 * sizeof(change));
  if(engine->simChanges.stoneChanges.changesArr == NULL) {
    fprintf(stderr, "Error! Memory allocation failed\n");
    quit(game, changes);
    exit(1);
  }
  
  engine->simChanges.libertyChanges.changesArr = malloc(maxChanges * 120 * sizeof(change));
  if(engine->simChanges.libertyChanges.changesArr == NULL) {
    fprintf(stderr, "Error! Memory allocation failed\n");
    quit(game, changes);
    exit(1);
  }
  
  engine->simChanges.nextChanges.changesArr = malloc(maxChanges * 40 * sizeof(change));
  if(engine->simChanges.nextChanges.changesArr == NULL) {
    fprintf(stderr, "Error! Memory allocation failed\n");
    quit(game, changes);
    exit(1);
  }
  
  engine->simChanges.changeIndexArray = malloc(maxChanges * sizeof(changeIndex));
  if(engine->simChanges.changeIndexArray == NULL) {
    fprintf(stderr, "Error! Memory allocation failed\n");
    quit(game, changes);
    exit(1);
  }

  rngState = (uint32_t)time(NULL) ^ (uint32_t)clock(); //Get the current rngState based on time.
}

void cleanupMCTSEngine(void) {
  MCTSEngine *engine = &globalEngine;
  
  if(!engine->initialized) return;
  
  if(engine->nodePool != NULL) {
    free(engine->nodePool);
    engine->nodePool = NULL;
  }
  
  //Make sure to free every value.
  if(engine->simChanges.stoneChanges.changesArr != NULL) {
    free(engine->simChanges.stoneChanges.changesArr);
    engine->simChanges.stoneChanges.changesArr = NULL;
  }
  
  if(engine->simChanges.libertyChanges.changesArr != NULL) {
    free(engine->simChanges.libertyChanges.changesArr);
    engine->simChanges.libertyChanges.changesArr = NULL;
  }
  
  if(engine->simChanges.nextChanges.changesArr != NULL) {
    free(engine->simChanges.nextChanges.changesArr);
    engine->simChanges.nextChanges.changesArr = NULL;
  }
  
  if(engine->simChanges.changeIndexArray != NULL) {
    free(engine->simChanges.changeIndexArray);
    engine->simChanges.changeIndexArray = NULL;
  }
  
  //Reset the values (in case of clear_board).
  engine->nodePoolIndex = 0;
  engine->historyLength = 0;
  engine->root = NULL;
  engine->lastMove = NULL;
  engine->initialized = 0;
}


void resetNodePool(MCTSEngine *engine) {
  //Reset every value in the node pool.
  engine->nodePoolIndex = 0;
  engine->historyLength = 0;
  engine->root = NULL;
  
  engine->simChanges.numOfMoves = 0;
  engine->simChanges.stoneChanges.numOfChanges = 0;
  engine->simChanges.libertyChanges.numOfChanges = 0;
  engine->simChanges.nextChanges.numOfChanges = 0;
}

MCTSNode* allocNode(MCTSEngine *engine) {
  //If our index surpasses the max size, we have run out of memory.
  if(engine->nodePoolIndex >= NODE_POOL_SIZE) return NULL;

  //Allocate a node using something simmilar to an arena allocator.
  MCTSNode *node = &engine->nodePool[engine->nodePoolIndex++];
  memset(node, 0, sizeof(MCTSNode)); 
  return node;
}

//Comparator for QuickSort (Descending order)
int compareMoves(const void *a, const void *b) {
    MoveScore *m1 = (MoveScore*)a;
    MoveScore *m2 = (MoveScore*)b;
    //We want higher scores first
    return m2->score - m1->score;
}

//Check if the eye we are looking at is true 
static int isTrueEye(gameState *game, int16_t index, stoneType color) {
  int dim = game->boardsize;
  int dimTrue = dim + 2;
  int16_t neighbors[4] = {index - 1, index + 1, index - dimTrue, index + dimTrue};
  
  for(int i=0; i<4; i++) {
    stoneType curr = game->board[neighbors[i]].color;
    if(curr != color && curr != BORDER) return 0; //If 1 of our "neighbors" is not the same color as us then its not even an eye.
  }
  
  int diagonals[4] = {index - dimTrue - 1, index - dimTrue + 1, index + dimTrue - 1, index + dimTrue + 1};
  int enemies = 0; 
  int border = 0;
  
  //After we know that its an eye for sure,
  //We check the diagonals to judge if its true. 
  for(int i = 0; i < 4; i++) {
    stoneType curr = game->board[diagonals[i]].color;
    if(curr == BORDER) border++;
    else if(curr != color && curr != EMPTY) enemies++;
  }
  
  //If on edge/corner and has at least one diagonal enemy, it's not a true eye
  //Or if in center and has two or more diagonal enemies, it's not a true eye
  if((border > 0 && enemies >= 1) || (enemies >= 2)) return 0;

  return 1;
}

static int isSelfAtari(gameState *game, int16_t move, stoneType color) {
  int dim = game->boardsize;
  int dimTrue = dim + 2;
  int liberties = 0;
  stoneType opp = color ^ 3;
  int captures = 0;
  int16_t neighbors[4] = {move - 1, move + 1, move - dimTrue, move + dimTrue};
  
  //Scan neightbors to find total liberties. 
  for(int i=0; i<4; i++) {
    int16_t n = neighbors[i];
    stoneType type = game->board[n].color;
    if(type == EMPTY) {
      liberties++;
    } else if(type == color) {
      int16_t rep = getRepres(game, n);

      if(game->board[rep].liberties >= -2 && game->board[rep].liberties <= 0) 
        liberties += (2 + game->board[rep].liberties); 
      else liberties += 2; 

    } else if(type == opp) {

      int16_t rep = getRepres(game, n);
      if(game->board[rep].liberties == -1) captures++;
    }
  }

  if(captures > 0) return 0; //If we capture, instantly return 0
  if(liberties <= 1) return 1; //If after playing a move, we have 1 librty total, then punish it.
  return 0;
}

//3x3 Pattern Shape Recognition
static int getPatternScore(gameState *game, int16_t move, stoneType color) {
    int dim = game->boardsize;
    int dimTrue = dim + 2;
    stoneType opp = color ^ 3;
    int score = 0;

    //Offsets
    int16_t up = move - dimTrue;
    int16_t down = move + dimTrue;
    int16_t left = move - 1;
    int16_t right = move + 1;

    //Diagonals
    int16_t ul = up - 1;
    int16_t ur = up + 1;
    int16_t dl = down - 1;
    int16_t dr = down + 1;

    stoneType cUp = game->board[up].color;
    stoneType cDown = game->board[down].color;
    stoneType cLeft = game->board[left].color;
    stoneType cRight = game->board[right].color;

    //1. Detect Empty Triangle (Bad Shape)
    //Case: Playing at corner of 'L' shape of own stones
    if((cUp == color && cLeft == color && game->board[ul].color == EMPTY) ||
        (cUp == color && cRight == color && game->board[ur].color == EMPTY) ||
        (cDown == color && cLeft == color && game->board[dl].color == EMPTY) ||
        (cDown == color && cRight == color && game->board[dr].color == EMPTY)) {
        score += PATTERN_BAD; 
    }

    //2. Tiger's Mouth (Good Shape / Vital Point)
    if(game->board[ul].color == opp && cUp == opp && cLeft == EMPTY) score += PATTERN_CRITICAL;
    if(game->board[ur].color == opp && cUp == opp && cRight == EMPTY) score += PATTERN_CRITICAL;
    
    //Check Own Tiger Mouth (Making good shape)
    if(game->board[ul].color == color && cUp == EMPTY && cLeft == EMPTY) score += PATTERN_MEDIUM;

    //3. Hane/Cut Logic
    if((cUp == opp && cLeft == opp) || (cUp == opp && cRight == opp) ||
        (cDown == opp && cLeft == opp) || (cDown == opp && cRight == opp)) {
        score += PATTERN_MEDIUM; //Encourages fighting/cutting
    }

    return score;
}

float calculateUCB(MCTSNode *node, MCTSNode *parent, float logParent) {
  if(node->isIllegal) return -1e9f; 
  float parentVisits = (float)parent->visits;
  
  if(node->visits == 0) { 
    float parentWinRate = (parent->visits > 0) ? (parent->wins / parentVisits) : 0.5f;
    //If it has 0 visits, instantly reduce it by FPU_REDUCTION
    //Aka, assume new move is ~25% worse than parent's winrate
    float fpu = parentWinRate - FPU_REDUCTION;
    
    //Change that if the move is "urgent".
    if(node->urgency == 2) fpu += 0.45f; 
    
    if(node->amafVisits > 0) { //In case RAVE still data exists, mix it with fpu. 
      float raveRate = node->amafWins / (float)node->amafVisits;
      return (fpu + raveRate) * 0.5f + 1.1f; 
    }

    return fpu + 1.1f; //Add 1.1 to encourage trying the move.
    //Unexplored nodes get high scores to ensure they get tried. 
  }
  
  //How often this move leads to wins from BLACK's perspective specifically
  //Wins tracks black's wins, so winRate is loss rate for white nodes
  float winRate = node->wins / (float)node->visits; 

  //Standard UCB1/UCT (Upper confidence bound) formula.
  float explore = 0.7f * sqrtf(logParent / (float)node->visits);
  
  if(node->urgency == 2) winRate += 0.2f; //Critical moves get +0.2 to the winrate (to encourage them).

  if(node->amafVisits > 5) { 
    //When parentVisits is small, beta is large so we use more of RAVE, opposite goes when parentVisits is large.
    //RAVE_CONSTANT controls the "mixing" speed.
    float beta = sqrtf(RAVE_CONSTANT / (3.0f * parentVisits + RAVE_CONSTANT));
    float raveValue = node->amafWins / (float)node->amafVisits;
    //New nodes typically have few visits, but they might have a lot of RAVE data
    //Which typically might appear in other simulations.
    return (1.0f - beta) * (winRate + explore) + beta * raveValue;
  }
  return winRate + explore;
}

MCTSNode* selectNode(MCTSEngine *engine, gameState *game) {
  MCTSNode *node = engine->root; //Begin at the current board position (root of the tree).

  while(node->firstChild != NULL) { //While the node has children.
    MCTSNode *bestChild = NULL;
    float bestUCB = -1e9f; 
  
    //Calculate the log for every UCB calculation (since it's the same for every child, it depends on the parent).
    float logParent = logf((float)node->visits);

    //Consider fewer children early and widen the search later (Based on node visits).
    int maxChildren = 4 + (int)(8.0f * powf((float)node->visits, 0.45f));
    int childCount = 0;

    for(MCTSNode *child = node->firstChild; child != NULL; child = child->nextSibling) {
      //Once we have considered maxChildren, stop looking at unvisited children
      if(childCount >= maxChildren && child->visits == 0) break;

      childCount++;
      
      //Calculate the UCB of the children, and find the max 
      float ucb = calculateUCB(child, node, logParent); 
      if(ucb > bestUCB) { 
        bestUCB = ucb; 
        bestChild = child; 
      }
    }
    
    if(bestChild == NULL) break; //Handle having no best child.
    
    if(bestChild->move != -1) { //If the move is not a "pass".
      if(playMove(game, bestChild->move, bestChild->color, &engine->simChanges) != 0) {
        bestChild->isIllegal = 1; 
        bestChild->visits = 0; 
        continue; //Incase the move played is illegal, zero out it's visits and continue.
      }
      //Save the moves to the history.
      engine->moveHistory[engine->historyLength++] = bestChild->move;
      engine->colorHistory[engine->historyLength-1] = bestChild->color;
    }
    //Change the new node to the current bestChild, and repeat.
    node = bestChild;
  }
  return node;
}

static int getMoveScore(gameState *game, int16_t move, int16_t lastMove, stoneType color) {
  int score = 0;
  int dim = game->boardsize;
  int dimTrue = dim + 2;
  
  //If we are simply filling our own eyes, punish the move.
  //Moves here are either completely wasted or suicide (Which is also checked in playMove).
  if(isTrueEye(game, move, color)) return -50000; 
  
  //Keep the offsets to check the neighboring stones.
  int16_t neighbors[4] = {move - 1, move + 1, move - dimTrue, move + dimTrue};
  stoneType opp = color ^ 3;
  
  //If the last move was not a pass, make sure to prioritize playing near it
  //Or if it's not the very first move.
  if(lastMove != -1) {
    int r = move / dimTrue;
    int c = move % dimTrue;

    int lr = lastMove / dimTrue;
    int lc = lastMove % dimTrue;

    //Find the distance between the stone we will play and the last move.
    int dist = abs(r - lr) + abs(c - lc);
    if(dist <= 1) score += 8000; //Incase the distance is 1 stone (next to it), give a 9000 score bonus
    else if(dist <= 2) score += 4000; //Incase the distance is 2 stones, give it just a 4000 score bonus
    //This happens to prioritize locality, and in general, fighting close.
  }
  
  //Save captures and saves for later.
  int captures = 0;
  int saves = 0;

  for(int i = 0; i < 4; i++) {
    stoneType nc = game->board[neighbors[i]].color;

    if(nc == opp) { //Incase our neighbor is an enemy:
      int16_t rep = getRepres(game, neighbors[i]);
      if(game->board[rep].liberties == -1) { 
        score += 400000; //If we capture the enemy, give the move +400000 (instant prio). 
        captures++; //Also increase our captures counter.
      }
    } else if(nc == color) { //If our neighbor is a stone of the same color.
      int16_t rep = getRepres(game, neighbors[i]);
      if(game->board[rep].liberties == -1) { 
        score += 500000; //Save ourselves out of atari, attempt to increase our liberties.
        saves++; 
      }
      else if(game->board[rep].liberties == -2) score += 5000; //If our "friend" only has 2 liberties, prio playing near him for ladders.
    }
  }
  
  if(captures == 0 && saves == 0) { //If we neither capture nor save any stone.
    if(isSelfAtari(game, move, color)) return -20000; //And we don't put ourselves in atari (We punish that).

    //Pattern/Shape Heuristics (Using the new function)
    score += getPatternScore(game, move, color);

    int diagonals[4] = {move - dimTrue - 1, move - dimTrue + 1, move + dimTrue - 1, move + dimTrue + 1};
    for(int k = 0; k < 4; k++) {
      if(game->board[diagonals[k]].color == opp) score += 500; //+500 per diagonal opponent, to reduce territory.
    }
  }
  
  int row = move / dimTrue;
  int col = move % dimTrue;
  //Give it a slight edge penalty to avoid playing moves like A1-A19.
  if(row == 1 || row == dim || col == 1 || col == dim) score -= 250; 
  
  score += (fastRand() & 63); //Add 0-128 extra points to break ties between moves.
  return score;
}

void expandNode(MCTSEngine *engine, gameState *game, MCTSNode *node, stoneType color) {
  int dim = game->boardsize;
  MoveScore legalMoves[650]; //25x25 is 625, thus 650 is fine (25x25 is out capped board size).
  int moveCount = 0;
  //Find the last move from history.
  int16_t lastMove = (engine->historyLength > 0) ? engine->moveHistory[engine->historyLength-1] : -1;
  int filledStones = 0;
  
  for(int i = 1; i <= dim; i++) {
    int16_t rowBase = i * (dim + 2);
    //Iterate through every section on the board.
    for(int j = 1; j <= dim; j++) {
      int16_t index = rowBase + j;
      
      //Skip occupied stones + the Ko point.
      if(game->board[index].color != EMPTY) { filledStones++; continue; }
      if(index == game->koPoint) continue;
      
      //Get the score for every move
      int score = getMoveScore(game, index, lastMove, color);
      if(score < -10000) continue; //If the score is too low (Fills eyes/Puts in atari), simply continue.
      
      //Save the required data.
      legalMoves[moveCount].move = index;
      legalMoves[moveCount].score = score;
      moveCount++;
    }
  }

  //Only save the N best moves. 
  if(moveCount > 0) {
    //Sort all the moves descending (highest score first).
    qsort(legalMoves, moveCount, sizeof(MoveScore), compareMoves);
    if(moveCount > MOVES_TO_SIMULATE) moveCount = MOVES_TO_SIMULATE; 
  }

  MCTSNode *prevChild = NULL;
  //For every Move, create child nodes.
  for(int i = 0; i < moveCount; i++) {
    MCTSNode *child = allocNode(engine);
    if(child == NULL) break; //Out of memory...
    child->move = legalMoves[i].move;
    child->color = color;
    child->parent = node;
    
    //If a move has high enough score, set it as urgent.
    //Raised threshold slightly to account for new bonuses
    if(legalMoves[i].score > 80000) child->urgency = 2; 
    else child->urgency = 0;

    //Build a linked list of children. 
    if(prevChild == NULL) node->firstChild = child;
    else prevChild->nextSibling = child;
    prevChild = child;
  }
  
  //Pass is only appropriate for 2 reasons: 
  //1. There are no legal moves
  //2. We have filled ~70% of the board (Endgame).
  if(moveCount == 0 || filledStones > (dim * dim * 0.7)) {
    MCTSNode *passNode = allocNode(engine); //Allocate a node for passing.
    if(passNode != NULL) {
      //Pass is repressented as -1
      passNode->move = -1; 
      passNode->color = color; 
      passNode->parent = node; 

      if(prevChild == NULL) node->firstChild = passNode;
      else prevChild->nextSibling = passNode;
    }
  }
}

static inline int16_t heuristicMove(MCTSEngine *engine, gameState *game, stoneType currentColor, int16_t lastMove) {
  int dim = game->boardsize;
  int dimTrue = dim + 2;
  stoneVals *board = game->board;

  int16_t neighbors[4] = {lastMove - 1, lastMove + 1, lastMove - dimTrue, lastMove + dimTrue};

  for(int k = 0; k < 4; k++) {
    int16_t n = neighbors[k];
    //If the neighbors are EMPTY simply continue.
    if(board[n].color == BORDER || board[n].color == EMPTY) continue;
    
    int16_t rep = getRepres(game, n);
    
    //Check if a group is in atari.
    if(board[rep].liberties == -1) { 
      int16_t adj[4] = {n-1, n+1, n-dimTrue, n+dimTrue};
      
      for(int j = 0; j < 4; j++) {
        int16_t target = adj[j];
        //Look at adjacent moves to try and escape.
        if(board[target].color == EMPTY && target != game->koPoint) {
          if(isTrueEye(game, target, currentColor)) continue;

          //If the move is legal, count it.              
          if(playMove(game, target, currentColor, &engine->simChanges) == 0) {
            engine->moveHistory[engine->historyLength++] = target;
            engine->colorHistory[engine->historyLength-1] = currentColor;
            return target; // Found move, return immediately
          }
                
        //Try and Counter Capture incase our target stone is of an enemy.
        } else if(game->board[n].color == currentColor && game->board[target].color == (currentColor^3)) {
          int16_t enemyRep = getRepres(game, target);
          if(board[enemyRep].liberties == -1) {
            //The enemy is in atari, try and kill it.
            int16_t enemyAdj[4] = {target-1, target+1, target-dimTrue, target+dimTrue};

            for(int m = 0; m < 4; m++) {
              int16_t killSpot = enemyAdj[m];

              if(board[killSpot].color == EMPTY && killSpot != game->koPoint) {
                //As long as the move is legal, play it.
                if(playMove(game, killSpot, currentColor, &engine->simChanges) == 0) {
                  engine->moveHistory[engine->historyLength++] = killSpot;
                  engine->colorHistory[engine->historyLength - 1] = currentColor;

                  return killSpot; //Once we find a move, return it.
                }
              }
            }
          }
        }
      }
    }
  }

  return -1; //No heuristic move found
}

float simulate(MCTSEngine *engine, gameState *game, stoneType startColor) {
  stoneType currentColor = startColor;
  int passCount = 0;
  int moveCount = 0;
  
  //Calculate max possible depth
  int maxSimMoves = (game->boardsize * game->boardsize) * 2; //2x the board area.
  if(maxSimMoves > 400) maxSimMoves = 400; //Capped at 400, as not to explore too deep.

  int dim = game->boardsize; 
  stoneVals *board = game->board;
  int dimTrue = dim + 2;
  uint32_t rSeed = fastRand();

  //While 2 both players haven't passed, and we haven't played up to the max simulation moves (400).
  while(passCount < 2 && moveCount < maxSimMoves) {
    int16_t move = -1;
    int16_t lastMove = (engine->historyLength > 0) ? engine->moveHistory[engine->historyLength - 1] : -1;

    move = heuristicMove(engine, game, currentColor, lastMove);

    //If it finds no smart move, try N pseudo random moves
    if(move == -1) {
      int attempts = 0;
      //Increased to 64 from 12, as before we had a bias for black specifically.
      //The issue was due to the way our score counting works.
      while(attempts < 16) {
        rSeed ^= rSeed << 13; 
        rSeed ^= rSeed >> 17; 
        rSeed ^= rSeed << 5;
        
        int16_t index;
        //60% of the moves should be played locally. 
        //We run a random sim (0-100) and if its within 60 play the next move close to the last one.
        if(lastMove != -1 && (rSeed % 100) < 60) {
          int offset = (int)((rSeed % 50) - 25); 
          index = lastMove + offset;
          if(index < dimTrue || index > dimTrue*dim) index = (rSeed % (dim*dim)) + dimTrue;

        } else { //Else, just play a random global move.
          index = (rSeed % (dim*dim)) + 1; 
          int r = index / dim + 1; 
          int c = index % dim + 1;
          index = r*dimTrue + c;
        }

        //If the move is not a koPoint.
        if(board[index].color == EMPTY && index != game->koPoint) {
          //If the move is filling an eye, just continue.
          if(isTrueEye(game, index, currentColor)) { 
            attempts++; 
            continue; 
          }

          //Fast self-atari check for sims (simplified to avoid filling single eyes)
          int16_t neighboors[4] = {index - 1, index + 1, index - dimTrue, index + dimTrue};
          int myStones = 0;
          for(int z = 0; z < 4; z++) if(board[neighboors[z]].color == currentColor) myStones++;
          if(myStones == 4 && board[index].liberties < 2) { 
            attempts++; 
            continue; 
          } 
          
          if(playMove(game, index, currentColor, &engine->simChanges) == 0) {
            //If it finds a valid move, play it and add it to history.
            engine->moveHistory[engine->historyLength++] = index;
            engine->colorHistory[engine->historyLength-1] = currentColor;
            move = index;
            break;
          }
        }
        attempts++;
      }
    }
    
    //If move is -1, increase the passcount (for 2 consecutive passes)
    if(move == -1) passCount++; 
    else passCount = 0;
    currentColor ^= 3; //Update the current color to the enemy.
    moveCount++;
  }
  
  int diff = 0;
  //Iterate through every stone to calculate who has the most stones on the board.
  //Whilst this is not efficient for calculating score accurately, simulations need to be fast
  //And thus heavy scoring logic (even a lighter function), might slow it down.
  for(int i = 1; i <= dim; i++) {
    int16_t r = i * dimTrue;
    for(int j = 1; j <= dim; j++) {
        stoneType curr = game->board[r+j].color;
        if(curr == BLACK) diff++;
        else if(curr == WHITE) diff--;
    }
  } 

  //Give the win to the person with the most stones, accounting for komi. 
  float score = (float)diff - game->komi;
  return (score > 0) ? 1.0f : 0.0f;
}

void backpropagate(MCTSEngine *engine, MCTSNode *node, float blackWon) {
  if(engine->root != NULL) {
    for(uint16_t i = 0; i < engine->historyLength; i++) {
      int16_t move = engine->moveHistory[i];
      stoneType moveColor = engine->colorHistory[i];
      
      //Iterate through the played moves, and update RAVE only for root's immediate children
      for(MCTSNode *child = engine->root->firstChild; child != NULL; child = child->nextSibling) {
        if(child->move == move && child->color == moveColor) {
          child->amafVisits++;
          child->amafWins += (moveColor == BLACK) ? blackWon : (1.0f - blackWon);
          break;
        }
      }
    }
  }
  
  //Give a visit to the exact path followed during selection.
  while(node != NULL) {
    node->visits++;
    
    //Also track wins, 1.0 means black won while 0.0 means white won.
    if(node->color == BLACK) node->wins += blackWon;
    else node->wins += (1.0f - blackWon);
    node = node->parent;
  }
}

int16_t selectMove(MCTSNode *root) {
  MCTSNode *best = NULL; 
  uint32_t maxVisits = 0;

  for(MCTSNode *child = root->firstChild; child != NULL; child = child->nextSibling) {
    if(child->isIllegal) continue;

    uint32_t score = child->visits; //Select by visits, not winrate. 
    //Winrate typically is not accurate, e.g a move with 2 visits and 80% winrate is not always good.
    if(child->move == -1) score /= 2; //If move is a pass, divide it's score (visits) by 2.

    if(score > maxVisits) { //If the score is higher than the current max visits, update it.
      maxVisits = score; 
      best = child; 
    }
  }
  //Return the best found move
  return (best != NULL) ? best->move : -1;
}

int16_t getOpeningMove(gameState *game) {
    int dim = game->boardsize;
    if(dim < 9) return -1;  //Too small for standard openings
    
    int dimTrue = dim + 2;  //Board with borders
    
    // Standard opening points (in board coordinates, not array indices)
    int16_t openingPoints[16];
    int count = 0;
    
    //Always include the 4 star points (4-N points)
    openingPoints[count++] = 4 * dimTrue + 4;                  //top-left star
    openingPoints[count++] = 4 * dimTrue + (dim - 3);          //top-right star  
    openingPoints[count++] = (dim - 3) * dimTrue + 4;          //bottom-left star
    openingPoints[count++] = (dim - 3) * dimTrue + (dim - 3);  //bottom-right star
    
    //Only for large boards, add 3-N
    if(dim >= 13) {
        openingPoints[count++] = 3 * dimTrue + 4;             //3-4 point
        openingPoints[count++] = 4 * dimTrue + 3;             //4-3 point
        openingPoints[count++] = 3 * dimTrue + (dim - 3);     //3-(dim-3)
        openingPoints[count++] = (dim - 3) * dimTrue + 3;     //(dim-3)-3
    } else {
        // Small boards (9x9, 11x11): add 3-3 and 5-5 points
        openingPoints[count++] = 3 * dimTrue + 3;
        openingPoints[count++] = 3 * dimTrue + (dim - 2);              //3-7 on 9x9
        openingPoints[count++] = (dim - 2) * dimTrue + 3;              //7-3 on 9x9
        openingPoints[count++] = (dim - 2) * dimTrue + (dim - 2);      //7-7 on 9x9
        openingPoints[count++] = ((dim+1)/2) * dimTrue + ((dim+1)/2);  //5-5 on 9x9
    }
    
    //Randomize which one we pick.
    for(int i = 0; i < count; i++) {
        int r = fastRand() % count;
        int16_t temp = openingPoints[i];
        openingPoints[i] = openingPoints[r];
        openingPoints[r] = temp;
    }
    
    //Return first available opening point
    for(int i = 0; i < count; i++) {
        if(game->board[openingPoints[i]].color == EMPTY) {
            return openingPoints[i];
        }
    }
    
    return -1;  //Incase all our opening points are occupied somehow...
}

//TODO: Possibly improve this function - Make it heavier.
//It is only run once (and situationally) so it must be fine.
int16_t getTacticalMove(gameState *game, stoneType color) {
  int dim = game->boardsize;
  int dimTrue = dim + 2;
  
  //Incase MCTS tries to pass early, we run this function
  //It selects moves based on the following priorities.

  //1. Save groups in atari.
  for(int i = 0; i < dimTrue * dimTrue; i++) {
    
    if(game->board[i].color != color) continue;

    int16_t rep = getRepres(game, i);
    if(game->board[rep].liberties == -1) { //Check if our group is in atari.
      int16_t neighbors[4] = {i - 1, i + 1, i - dimTrue, i + dimTrue};
      for(int j = 0; j < 4; j++) {
        //If the move is valid, simply return that.
        if(game->board[neighbors[j]].color == EMPTY && neighbors[j] != game->koPoint) {
          if(!isTrueEye(game, neighbors[j], color)) { 
            return neighbors[j]; 
          }
        }
      }
    }
  }
  
  //2. Capture opponent.
  stoneType opp = color ^ 3;

  for(int i = 0; i < dimTrue * dimTrue; i++) {

    if(game->board[i].color != opp) continue;

    int16_t rep = getRepres(game, i);
    if(game->board[rep].liberties == -1) { //Check if the opponent group is in atari.
      int16_t neighbors[4] = {i - 1, i + 1, i - dimTrue, i + dimTrue};
      for(int j = 0; j < 4; j++) {
        //If the move is available, simply return that.
        if(game->board[neighbors[j]].color == EMPTY && neighbors[j] != game->koPoint) {
          return neighbors[j]; 
        }
      }
    }
  }
  
  //If we find no valid move.. just return to pass.
  return -1;
}

int16_t genMove(gameState *game, gameChanges *changes, stoneType color) {
  initMCTSEngine(&globalEngine, game, changes); 
  
  int dim = game->boardsize;
  
  //If there haven't been played a total of OPENING_MOVES (6-10) moves, use our opening book strategy.
  if(game->totalMoves < OPENING_MOVES) {
    int16_t op = getOpeningMove(game); //If the move is valid, just return it instantly to save time.
    if(op != -1 && playMove(game, op, color, changes) == 0) return op; 
  }

  //Using Tree-Reuse we save rebuilding the entire tree from scratch.
  int reused = 0;
  if(globalEngine.lastMove) {
    //Incase our opponent replies with a move we expected, then just make it the root of our tree. 
    for(MCTSNode *curr = globalEngine.lastMove->firstChild; curr; curr = curr->nextSibling) {
      if(curr->move != -1 && game->board[curr->move].color == (color ^ 3)) {
        globalEngine.root = curr; 
        curr->parent = NULL; 
        reused = 1; 
        break;
      }
    }
  }

  if(!reused || globalEngine.nodePoolIndex > NODE_POOL_SIZE * 0.9) {
    //In case we get to close to the max number of nodes (and we are not reusing the tree), reset it.
    resetNodePool(&globalEngine);
    globalEngine.root = allocNode(&globalEngine);
    globalEngine.root->visits = 1;
  }
  
  clock_t start = clock();
  double timeUsed = 0.0;
  int iter = 1;
  
  //Assume that the max number of moves we can have is equal to the boardsize + 1 column of extra moves. 
  int maxMoves = dim * dim + dim;
  //If the total moves surpass our limit, there is no point playing any more.
  if(game->totalMoves > maxMoves) return 0;

  //Approximate how much time we have per move.
  float timePMove = (game->totalTime / (float)(maxMoves / 2)) - 0.16 * (game->totalTime / (float)(maxMoves / 2));
  while(timeUsed < timePMove) {
    //Save current state.
    uint16_t savedLen = globalEngine.historyLength;
    
    //Select a "leaf" based on UCB.
    MCTSNode *leaf = selectNode(&globalEngine, game);
    int runSim = 1;
    
    //If needed, run through the expansion phase.
    if(leaf && leaf->visits > 0 && !leaf->firstChild) {
      expandNode(&globalEngine, game, leaf, (leaf->color == BLACK ? WHITE : BLACK));
      if(leaf->firstChild) {
        leaf = leaf->firstChild; //Pick the first child to simulate.
        if(leaf->move != -1) { //If its not a pass and its a valid move, "play" it.
          if(playMove(game, leaf->move, leaf->color, &globalEngine.simChanges) == 0) {
            globalEngine.moveHistory[globalEngine.historyLength++] = leaf->move;
            globalEngine.colorHistory[globalEngine.historyLength-1] = leaf->color;

          } else {
            leaf->isIllegal = 1; //If its not a  valid move, mark it as illegal. 
            runSim = 0; //Skip simulation loop
          }
        }
      }
    }
    
    if(runSim) {
      //simColor should be the opposite of the color that played last.
      stoneType simColor = (globalEngine.historyLength & 1) ? (color ^ 3) : color; 
      if(leaf) simColor = (leaf->color == BLACK) ? WHITE : BLACK;
      
      //Run the simulations and update the global engine.
      float res = simulate(&globalEngine, game, simColor);
      backpropagate(&globalEngine, leaf, res);
    }
    
    while(globalEngine.historyLength > savedLen) {
      //Rollback every change back to the original game state.
      rollbackChanges(&globalEngine.simChanges, game);
      globalEngine.historyLength--;
    }
    
    iter++; //Only check for the time used every 256 iterations. 
            //(At least I hope thats correct)
    if(!(iter & 255)) timeUsed = (double)(clock() - start) / CLOCKS_PER_SEC;
  }
  
  //DEBUG: Simulation count.
  //printf("iter: %d\ntimeUsed: %.2f\nDone: %d%%\nMoves: %d\n", iter, timeUsed, (game->totalMoves * 100) / maxMoves, game->totalMoves); 

  //Select the best move from root.
  int16_t bestMove = selectMove(globalEngine.root);
  
  //If MCTS tries to pass early, find a decent move to play.
  if(bestMove <= 0) {
    int16_t save = getTacticalMove(game, color);
    if(save != -1) bestMove = save;
  }
  
  if(bestMove == -1) return 0; //If the ideal move is passing and we are past our threshhold.
  
  globalEngine.lastMove = NULL; 
  if(bestMove > 0 && globalEngine.root) {
    for(MCTSNode *c = globalEngine.root->firstChild; c; c = c->nextSibling) {
      if(c->move == bestMove) { globalEngine.lastMove = c; break; }
    }
  }
  
  //If we find a move, and its legal, immediately return it.
  if(bestMove > 0 && playMove(game, bestMove, color, changes) == 0) return bestMove;
  
  //If the best move found was actually illegal, find a decent random move
  //- Somehow had this issue, it's not a useless check.
  int area = dim * dim;
  int offset = fastRand() % area;

  for(int i = 0; i < area; i++) {
    int index = (offset + i) % area;
    int r = index / dim + 1;
    int c = index % dim + 1;
    //Pick a pseudo random move that is not filling an eye.
    int16_t candidate = r * (dim + 2) + c;

    if(game->board[candidate].color == EMPTY && candidate != game->koPoint) {
      if(!isTrueEye(game, candidate, color)) {
        //Check again if the move is valid, if by luck its not, just return 0 (pass).
        if(playMove(game, candidate, color, changes) == 0) return candidate; 
      }
    }

  }

  //If it passes all of the filters without finding a suitable move, just pass...
  return 0;
}
