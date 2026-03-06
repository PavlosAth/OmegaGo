#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "goteam.h"

const char *columns = "ABCDEFGHJKLMNOPQRSTUVWXYZ";

void showBoard(gameState *game);
void boardSize(gameState *game, gameChanges *changes, char *cmd);
void komi(gameState *game, char *cmd);
void clearBoard(gameState *game);
void playCMD(gameState *game, gameChanges *changes, char *cmd);
void genCMD(gameState *game, gameChanges *changes, char *cmd);

//commands for GTP
enum commandType {
  BOARDSIZE,
  KOMI,
  SHOW_BOARD,
  PLAY,
  GENMOVE,
  CLEAR_BOARD,
  QUIT,
  FINAL_SCORE,
  NAME,
  VERSION,
  TIME_SETTINGS,
  TIME_LEFT,
  LIST_COMMANDS,
  PROTOCOL_VERSION,
};

typedef struct {
  char *cmdName;
  int numOfChars;
  enum commandType type;
} command;

//identifies the input command by comparing the appropriate numbers
int getCommand(char *cmd) {
  command commands[] = {
      {"play", 4, PLAY},
      {"genmove", 7, GENMOVE},

      {"time_settings", 13, TIME_SETTINGS},
      {"time_left", 9, TIME_LEFT},

      {"boardsize", 9, BOARDSIZE},
      {"komi", 4, KOMI},
      {"showboard", 9, SHOW_BOARD},
      {"clear_board", 11, CLEAR_BOARD},
      {"name", 4, NAME},
      {"version", 7, VERSION},

      {"final_score", 11, FINAL_SCORE},
      {"quit", 4, QUIT},

      {"list_commands", 13, LIST_COMMANDS},
      {"protocol_version", 16, PROTOCOL_VERSION},
      {NULL, 0, 0},
  };

  for (int i = 0; commands[i].cmdName != NULL; i++) {
    if (strncasecmp(commands[i].cmdName, cmd, commands[i].numOfChars) == 0)
      return commands[i].type;
  }

  return -1;
}

int parseInt(char *str, int *num) {
  /* gets int from string and returns 1 if an
  error was encountered*/
  char *exc;
  errno = 0;
  while (isspace(*str))
    str++;
  if (*str == '\0')
    return 1;
  int temp = strtol(str, &exc, 10);
  while (isspace(*exc))
    exc++;
  if (strlen(exc) > 0 || errno == ERANGE)
    return 1;
  *num = temp;
  return 0;
}

// gets float from string and returns 1 if an error was encountered
int parseFloat(char *str, float *num) {
  char *exc;
  errno = 0;
  while (isspace(*str))
    str++;
  if (*str == '\0')
    return 1;
  double temp = strtof(str, &exc);
  if (errno == ERANGE)
    return 1;
  *num = temp;
  return 0;
}

int main() {
  char cmd[101] = {0};//the entire input GTP command
  gameState game = {NULL, 0, 0, 7.5, 0, 60.0};

  gameChanges changes = {0};

  while (1) {
    if(fgets(cmd, 100, stdin) == NULL) return 1;
    //get the apporopriate command
    switch(getCommand(cmd)) {
      case PLAY: playCMD(&game, &changes, cmd);            break;
      case GENMOVE: genCMD(&game, &changes, cmd);          break;

      case TIME_SETTINGS: parseFloat(cmd + 13, &game.totalTime);
                          printf("=\n");                   break;
      case TIME_LEFT: printf("=\n");                       break;

      case BOARDSIZE: boardSize(&game, &changes, cmd);     break;
      case KOMI: komi(&game, cmd);                         break;
      case SHOW_BOARD: showBoard(&game);                   break;
      case CLEAR_BOARD: clearBoard(&game); printf("= \n"); break;
      case NAME: printf("= OmegaGo\n");                    break;
      case VERSION: printf("= 2.2\n");                     break;

      case FINAL_SCORE: if(finalScore(&game)) {
                          quit(&game, &changes); 
                          return 1; }                      break;
      case QUIT: quit(&game, &changes);                    return 0;

      //For UI's like SABAKI/GOGUI. 
      case PROTOCOL_VERSION: printf("= 2\n\n");            break;
      case LIST_COMMANDS: 
        printf("= protocol_version\n");
        printf("name\n");
        printf("version\n");
        printf("known_command\n");
        printf("list_commands\n");
        printf("quit\n");
        printf("boardsize\n");
        printf("clear_board\n");
        printf("komi\n");
        printf("play\n");
        printf("genmove\n");
        printf("showboard\n");
        printf("time_settings\n");
        printf("time_left\n");
        printf("final_score\n\n");
        break;

      default: printf("? invalid instruction");            break;
    }

    putchar('\n');
    fflush(stdout);
  }
}
//displays the board state
void showBoard(gameState *game) {
  stoneVals *board = game->board;
  int dim = game->boardsize;

  if(board == NULL) {
    printf("? game is not active\n");
    return;
  }

  printf("= ");
  putchar('\n');
  printf("  ");
  for (int j = 0; j < dim; j++)//printing letters
    printf(" %c", columns[j]);

  putchar('\n');
  for (int i = 1; i <= dim; i++) {
    printf("%2d", dim + 1 - i);//printing number
    for (int j = 1; j <= dim; j++) {

      char pos;
      switch (board[j + i * (dim + 2)].color) {
        case BLACK: pos = 'X'; break;
        case WHITE: pos = 'O'; break;
        default: pos = '.';    break;
      }

      printf(" %c", pos);//printing stone
    } //bvvvgggggggggggggn -DIT Cat 2026  (very important comment)
    printf(" %d\n", dim + 1 - i);//printing number
  }

  printf("  ");
  for (int j = 0; j < dim; j++) //printing letters
    printf(" %c", columns[j]);
  

  putchar('\n');
}

//sets the boardsize and initializes the game board
void boardSize(gameState *game, gameChanges *changes, char *cmd) {
  if(game->board != NULL) {
    printf("? game already active");
    return;
  }

  int num;
  if (parseInt(cmd + 9, &num) == 1 || num <= 0 || num > 25) {//invalid number
    printf("? invalid board size");
    return;
  }
  
  game->boardsize = num;
  game->board = malloc(((game->boardsize + 2) * (game->boardsize + 2)) * sizeof(stoneVals));//allocate board 
  if(!game->board) {
    exit(1);
  }
  
  printf("= ");
  putchar('\n');

  clearBoard(game);//prepare the board

  //prepare change arrays
  changeIndex *changeIndexArray = malloc(sizeof(changeIndex) * CHNG_ARR_REALL_SIZE);//stack-like array that stores the starting indexes of the other arrays for each move
  if(changeIndexArray == NULL) {
    free(game->board);
    exit(1);
  }
  changes->changeIndexArray = changeIndexArray;
  changes->numOfMoves = 0;//initialize

  change *stoneChangesArray = malloc(sizeof(change) * CHNG_ARR_REALL_SIZE);//stack-like array that stores stone changes for the board
  if(stoneChangesArray == NULL) {
    free(changeIndexArray);
    free(game->board);
    exit(1);
  }
  changes->stoneChanges.changesArr = stoneChangesArray;
  changes->stoneChanges.numOfChanges = 0;//initialize

  change *libertyChangesArray = malloc(sizeof(change) * CHNG_ARR_REALL_SIZE);//stack-like array that stores liberty changes for the board
  if(libertyChangesArray == NULL) {
    free(changeIndexArray);
    free(stoneChangesArray);
    free(game->board);
    exit(1);
  }
  changes->libertyChanges.changesArr = libertyChangesArray;
  changes->libertyChanges.numOfChanges = 0;//initialize

  change *nextChangesArray = malloc(sizeof(change) * CHNG_ARR_REALL_SIZE);//stack-like array that stores the next stone (chains) changes for the board
  if (nextChangesArray == NULL) {
    free(changeIndexArray);
    free(libertyChangesArray);
    free(stoneChangesArray);
    free(game->board);
    exit(1);
  }
  changes->nextChanges.changesArr = nextChangesArray;
  changes->nextChanges.numOfChanges = 0;//initialize

}

//sets initial board state
void clearBoard(gameState *game) {
  stoneVals *board = game->board;
  int dim = game->boardsize;

  if(board == NULL) {
    printf("? game is not active");
    return;
  }

  for (int j = 0; j < dim + 2; j++) {//place upper border
    board[j].color = BORDER;
  }
  for (int i = 1; i <= dim; i++) {
    board[i * (dim + 2)].color = BORDER;//first border in row
    for (int j = 1; j <= dim; j++) {
      board[j + i * (dim + 2)].color = EMPTY;//set every intersection to EMPTY
    }
    board[i * (dim + 2) + dim + 1].color = BORDER;//last border in row
  }
  for(int j = 0; j < dim + 2; j++) {//place bottom border
    board[j + (dim + 2) * (dim + 1)].color = BORDER;
  }

  for(int i = 0; i < dim + 2; i++) {
    for(int j = 0; j < dim + 2; j++) {
      game->board[j + (dim + 2) * i].liberties = 0;//set liberties to 0
      game->board[j + (dim + 2) * i].nextStone = i;//point every intersection to itself (not part of a group)
    }
  }

  game->totalMoves = 0;
  cleanupMCTSEngine();
}

//sets komi for the game
void komi(gameState *game, char *cmd) {
  if(parseFloat(cmd + 4, &game->komi) == 1 || game->komi < 0) {
    printf("? invalid komi number");//invalid number
    game->komi = 0;

  } else {
    printf("= ");
    putchar('\n');
  }
}

//frees allocated memory for quiting
void quit(gameState *game, gameChanges *changes) {
  if(game != NULL && game->board != NULL) {//free board
    free(game->board);
    game->board = NULL;
  }
  
  if(changes != NULL) {
    //free the arrays that preserve the changes of each move
    if(changes->changeIndexArray != NULL) {
      free(changes->changeIndexArray);
      changes->changeIndexArray = NULL;
    }
    
    if(changes->stoneChanges.changesArr != NULL) {
      free(changes->stoneChanges.changesArr);
      changes->stoneChanges.changesArr = NULL;
    }
    
    if(changes->libertyChanges.changesArr != NULL) {
      free(changes->libertyChanges.changesArr);
      changes->libertyChanges.changesArr = NULL;
    }
    
    if(changes->nextChanges.changesArr != NULL) {
      free(changes->nextChanges.changesArr);
      changes->nextChanges.changesArr = NULL;
    }
  }

  cleanupMCTSEngine();
}

//converts the column letter to the appropriate index
int getIndFromCol(char col) {
  if(col < 'A' || col > 'Z' || col == 'I') return -1;
  int i = 0;
  while(col != columns[i]) i++;
  return i;
}

//the play gtp command handler
void playCMD(gameState *game, gameChanges *changes, char *cmd) {
  if(game->board == NULL) {
    printf("? game is not active");
    return;
  }

  int dim = game->boardsize;
  int index = -1;//index to play at

  uint8_t cmdInd = 7;

  stoneType color = EMPTY;

  //get the color for the stone to play
  if(toupper(cmd[5]) == 'B' && isspace(cmd[6])) color = BLACK;
  if(toupper(cmd[5]) == 'W' && isspace(cmd[6])) color = WHITE;
  if(!strncasecmp(cmd + 5, "BLACK", 5)) { color = BLACK; cmdInd = 11; }
  if(!strncasecmp(cmd + 5, "WHITE", 5)) { color = WHITE; cmdInd = 11; }

  if(color == EMPTY) { printf("? Invalid color"); return; }

  char col = cmd[cmdInd];

  game->totalMoves++;
  if(!strncasecmp(cmd + cmdInd, "PASS", 4)) { printf("=\n"); return; }

  int X;//get X coordinate
  if((X = getIndFromCol(col)) == -1) { printf("? illegal"); return; }

  int Y;//get Y coordinate
  if(parseInt(cmd + cmdInd + 1, &Y)) { printf("? illegal"); return; }

  index = (dim + 2) * (dim - Y + 1) + X + 1;  //translate coordinates to index (the Y axis is inverted)

  if(playMove(game, index, color, changes) != 0) { printf("? illegal"); return; }//play the move
  printf("= ");
  putchar('\n');

  flattenLibs(game);//compress the path of the DSU used for liberties
}

//converts a board index to the appropriate go format
void getCoords(int index, int dim, char *coords) {
  if (index == 0) {//handle pass
    strcpy(coords, "pass");
    return;
  }

  sprintf(coords, "%c%d", columns[index % (dim + 2) - 1], dim - (index / (dim + 2)) + 1);
}

//the genmove gtp command handler
void genCMD(gameState *game, gameChanges *changes, char *cmd) {
  if(game->board == NULL) {
    printf("? game is not active");
    return;
  }

  stoneType color = EMPTY;
  //get the color for the stone to play
  if(toupper(cmd[8]) == 'B' && isspace(cmd[9])) color = BLACK;
  if(toupper(cmd[8]) == 'W' && isspace(cmd[9])) color = WHITE;
  if(!strncasecmp(cmd + 8, "BLACK", 5)) color = BLACK; 
  if(!strncasecmp(cmd + 8, "WHITE", 5)) color = WHITE;

  if(color == EMPTY) { printf("? Invalid color"); return; } 

  int16_t index = genMove(game, changes, color);//get index of the move
  char coords[10];

  getCoords(index, game->boardsize, coords);//convert to GTP format
  game->totalMoves++;

  printf("= %s", coords);//print move
  putchar('\n');
}





